"""EVK-owned conversation and tool loop. Unreal is a polling equipment worker.

Only actual arrival/start/result events enter the conversation. Model choices
are never masked, repaired or checked against a process recipe here.
"""
from concurrent.futures import ThreadPoolExecutor
from collections import OrderedDict, deque
from dataclasses import dataclass, field
import copy
import hashlib
import json
import re
import threading
import time
from urllib.request import Request, urlopen

from single_token import SIMPLE_ACTIONS, action_code, observation

SCHEMA = "darkfactory-edge-agent-runtime-v1"
LEASE_SECONDS = 5.
MAX_TURNS = 40
ID = re.compile(r"^[A-Za-z0-9_-]{1,128}$")


def identifier(value):
    if not isinstance(value, str) or not ID.fullmatch(value):
        raise ValueError("Invalid transport identity")
    return value


def invoke(endpoint, payload):
    request = Request(endpoint, json.dumps(payload).encode(), {"Content-Type": "application/json"})
    with urlopen(request, timeout=120) as response:
        raw = response.read(65537)
    if len(raw) > 65536:
        raise ValueError("Oversize agent reply")
    return json.loads(raw)


def selected_message(reply):
    choices = reply.get("choices", [])
    if len(choices) != 1 or choices[0].get("finish_reason") != "tool_calls":
        raise ValueError("Agent must return exactly one complete tool call")
    message = choices[0]["message"]
    code = action_code(message, SIMPLE_ACTIONS)
    call = message["tool_calls"][0]
    identifier(call["id"])
    args = json.loads(call["function"]["arguments"])
    if not isinstance(args["reason"], str) or not 0 < len(args["reason"]) <= 240:
        raise ValueError("Invalid tool reason")
    return copy.deepcopy(message), code


@dataclass
class Cell:
    session: str
    seen: float
    body: str = ""
    epoch: int = 0
    ack: int = 0
    hashes: OrderedDict = field(default_factory=OrderedDict)
    history: list = field(default_factory=list)
    pending: dict | None = None
    running: dict | None = None
    scope: dict = field(default_factory=dict)
    paused: bool = False
    busy: bool = True
    closed: bool = False
    departed: bool = False
    held: bool = False
    error: str = ""
    turns: int = 0
    decisions: int = 0
    future: object = None
    future_scope: tuple | None = None
    started: float = 0.


class Runtime:
    def __init__(self, config, endpoints, call=invoke, clock=time.monotonic, recorder=None):
        if config.get("version") != "darkfactory-live-agent-v2":
            raise ValueError("Edge harness requires the eight-tool v2 contract")
        self.config = copy.deepcopy(config)
        self.config_hash = hashlib.sha256(json.dumps(config, sort_keys=True).encode()).hexdigest()
        self.endpoints, self.call, self.clock, self.recorder = endpoints, call, clock, recorder
        self.cells = OrderedDict()
        self.lock = threading.RLock()
        self.pool = ThreadPoolExecutor(max_workers=1, thread_name_prefix="edge-agent")
        self.events = deque(maxlen=256)
        self.requests = 0
        self.discarded = 0

    def record(self, cell, kind, **detail):
        event = dict(event=kind, session_id=cell.session, body_id=cell.body, epoch=cell.epoch,
                     runtime_clock=self.clock(), detail=detail)
        self.events.append(event)
        if self.recorder:
            self.recorder(event)

    def exchange(self, packet):
        required = {"schema", "session_id", "body_id", "epoch", "paused", "busy", "closed", "model_source", "agent_device", "events"}
        if not isinstance(packet, dict) or set(packet) != required or packet["schema"] != SCHEMA:
            raise ValueError("Unexpected cell API packet; prompts/history are not accepted")
        session, body = identifier(packet["session_id"]), identifier(packet["body_id"])
        epoch = packet["epoch"]
        if type(epoch) is not int or not 0 <= epoch < 2**53:
            raise ValueError("Invalid epoch")
        if any(type(packet[k]) is not bool for k in ("paused", "busy", "closed")):
            raise ValueError("Invalid worker state")
        backend = (packet["model_source"], packet["agent_device"])
        if backend not in self.endpoints:
            raise ValueError("Unknown inference backend")
        events = packet["events"]
        if not isinstance(events, list) or len(events) > 16:
            raise ValueError("Unbounded equipment events")
        with self.lock:
            now = self.clock()
            cell = self.cells.get(session)
            if cell is None:
                if any(not c.closed and now-c.seen < LEASE_SECONDS for c in self.cells.values()):
                    raise ValueError("Another equipment session owns the runtime lease")
                # Superseded sessions must not publish late model completions.
                for c in self.cells.values():
                    c.closed = True
                    c.pending = None
                while len(self.cells) >= 4:
                    self.cells.popitem(last=False)
                cell = Cell(session=session, seen=now)
                self.cells[session] = cell
                self.record(cell, "session_opened")
            if cell.closed and not packet["closed"]:
                raise ValueError("Closed session cannot be resumed; start a fresh session")
            if epoch < cell.epoch:
                raise ValueError("Stale worker epoch")
            if (packet["model_source"], packet["agent_device"]) != (cell.scope.get("model_source"), cell.scope.get("agent_device")) and cell.scope and epoch == cell.epoch:
                raise ValueError("Backend change requires a new cancellation epoch")
            # Events belong to their captured scope, including a completed old
            # body followed by a new arrival. No full history arrives from UE.
            for event in events:
                self.apply_event(cell, event, epoch)
            if epoch != cell.epoch or body != cell.scope.get("body_id"):
                cell.pending = None
                if cell.held and not packet["paused"] and cell.turns < MAX_TURNS:
                    cell.held = False
                    cell.error = ""
                self.record(cell, "scope_changed", new_epoch=epoch, worker_body=body, backend=backend)
            cell.epoch = epoch
            cell.scope = dict(body_id=body, model_source=backend[0], agent_device=backend[1])
            cell.paused, cell.busy, cell.closed = packet["paused"], packet["busy"], packet["closed"]
            cell.seen = now
            if cell.paused or cell.closed:
                cell.pending = None
            response = dict(schema=SCHEMA, session_id=session, body_id=body, epoch=epoch, ack=cell.ack,
                            turns=cell.turns, decisions=cell.decisions, error=cell.error,
                            state="closed" if cell.closed else "held" if cell.held else "thinking" if cell.future else "executing" if cell.running else "idle")
            if cell.pending and not cell.paused and not cell.busy and cell.body == body and not cell.held:
                response["command"] = cell.pending["reply"]
                response["latency_ms"] = cell.pending["latency_ms"]
            return response

    def apply_event(self, cell, event, root_epoch):
        if not isinstance(event, dict) or set(event) != {"seq", "kind", "body_id", "epoch", "call_id", "result"}:
            raise ValueError("Invalid equipment event fields")
        seq = event["seq"]
        if type(seq) is not int or seq < 1 or type(event["epoch"]) is not int or not 0 <= event["epoch"] <= root_epoch:
            raise ValueError("Invalid equipment event sequence/epoch")
        digest = hashlib.sha256(json.dumps(event, sort_keys=True).encode()).hexdigest()
        if seq <= cell.ack:
            if cell.hashes.get(seq) != digest:
                raise ValueError("Replayed event changed or exceeded replay window")
            return
        if seq != cell.ack+1:
            raise ValueError("Equipment event sequence gap")
        body = identifier(event["body_id"])
        kind = event["kind"]
        if kind == "arrival":
            if event["call_id"] or (cell.body and (body == cell.body or not cell.departed)):
                raise ValueError("Unexpected body arrival or unfinished previous body")
            result = observation(json.dumps(event["result"]))
            if result.get("status") != "completed":
                raise ValueError("Arrival is not complete")
            cell.body = body
            cell.history = [dict(role="user", content=json.dumps(dict(body_id=body,
                event="new_body_arrived_choose_tools_to_complete_the_task", arrival=result), separators=(",", ":")))]
            cell.running = cell.pending = None
            cell.departed = cell.held = False
            cell.error = ""
            cell.turns = 0
        elif kind == "started":
            identifier(event["call_id"])
            if event["result"] or body != cell.body or cell.running or not cell.pending:
                raise ValueError("No matching issued command to start")
            message = cell.pending["message"]
            if event["call_id"] != message["tool_calls"][0]["id"] or event["epoch"] != cell.epoch:
                raise ValueError("Started command scope mismatch")
            cell.running = message
            cell.history.append(copy.deepcopy(message))
            cell.pending = None
        elif kind == "result":
            if body != cell.body or not cell.running or event["call_id"] != cell.running["tool_calls"][0]["id"]:
                raise ValueError("Tool result has no matching actual start")
            result = observation(json.dumps(event["result"]))
            tool = cell.running["tool_calls"][0]["function"]["name"]
            if result.get("body_id", body) != body:
                raise ValueError("Result body mismatch")
            if not isinstance(result.get("status"), str):
                raise ValueError("Missing actual result status")
            if tool == "inspect_temperatures":
                rows = result.get("results", [])
                if [r.get("stage") for r in rows] != ["adhesive", "weld"][:len(rows)]:
                    raise ValueError("Thermal results out of order")
                if result["status"] == "completed" and len(rows) != 2:
                    raise ValueError("Completed temperature tool needs both actual results")
            cell.history.append(dict(role="tool", tool_call_id=event["call_id"],
                                     content=json.dumps(result, separators=(",", ":"))))
            cell.running = None
            cell.departed = tool == "finish_body" and result["status"] == "completed"
            # A genuine late completion records the job, but cannot release a
            # heartbeat/model-error hold. Only an explicit new resume epoch may.
            cell.held = cell.held or (tool == "hold" and result["status"] == "held")
        else:
            raise ValueError("Unsupported equipment event")
        cell.ack = seq
        cell.hashes[seq] = digest
        while len(cell.hashes) > 128:
            cell.hashes.popitem(last=False)
        self.record(cell, "equipment_"+kind, event=copy.deepcopy(event))

    def tick(self):
        """Autonomous EVK loop; never waits for a host 'choose next' request."""
        with self.lock:
            now = self.clock()
            for cell in self.cells.values():
                if not cell.closed and now-cell.seen >= LEASE_SECONDS and not cell.held:
                    cell.pending = None
                    cell.held, cell.error = True, "Equipment heartbeat expired; pause/resume to reconnect"
                    self.record(cell, "lease_expired")
                scope = (cell.body, cell.epoch, tuple(cell.scope.items()))
                if cell.future and cell.future.done():
                    future, issued_scope = cell.future, cell.future_scope
                    cell.future = None
                    if issued_scope != scope or cell.paused or cell.closed or cell.held or now-cell.seen >= LEASE_SECONDS:
                        self.discarded += 1
                        self.record(cell, "decision_discarded")
                        continue
                    try:
                        reply = future.result()
                        message, code = selected_message(reply)
                        cell.decisions += 1
                        cell.pending = dict(reply=reply, message=message, latency_ms=(now-cell.started)*1000)
                        self.record(cell, "model_decision", code=code, reply=reply, latency_ms=cell.pending["latency_ms"])
                    except Exception as exc:
                        cell.held, cell.error = True, str(exc)
                        self.record(cell, "model_error", error=str(exc))
                if (cell.closed or cell.paused or cell.busy or cell.held or cell.departed or
                    cell.running or cell.pending or cell.future or not cell.history or
                    cell.body != cell.scope.get("body_id") or now-cell.seen >= LEASE_SECONDS):
                    continue
                if cell.turns >= MAX_TURNS:
                    cell.held, cell.error = True, "Agent decision budget exhausted for this body"
                    continue
                cell.turns += 1
                payload = dict(model="dark-factory-agent",
                    messages=[dict(role="system", content=self.config["system_prompt"]), *copy.deepcopy(cell.history)],
                    tools=self.config["tools"], tool_choice="required", parallel_tool_calls=False,
                    temperature=0, seed=42, max_tokens=256, chat_template_kwargs=dict(enable_thinking=False))
                endpoint = self.endpoints[(cell.scope["model_source"], cell.scope["agent_device"])]
                cell.started, cell.future_scope = now, scope
                self.requests += 1
                self.record(cell, "model_request", endpoint=endpoint, payload=payload)
                cell.future = self.pool.submit(self.call, endpoint, payload)

    def snapshot(self):
        with self.lock:
            return dict(schema=SCHEMA, status="ok", owner="evk", tool_contract=self.config["version"],
                config_sha256=self.config_hash, model_requests=self.requests, discarded_decisions=self.discarded,
                sessions=[dict(session_id=c.session, body_id=c.body, epoch=c.epoch, ack=c.ack,
                    closed=c.closed, paused=c.paused, held=c.held, error=c.error, turns=c.turns,
                    decisions=c.decisions, history_messages=len(c.history), executing=bool(c.running),
                    pending_command=bool(c.pending)) for c in self.cells.values()])
