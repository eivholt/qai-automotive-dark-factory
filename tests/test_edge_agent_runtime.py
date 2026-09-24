import copy
import json
from pathlib import Path
import sys
import threading
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'scripts/agent'))
import edge_runtime as er
import single_token as st

CONFIG = json.loads((Path(__file__).resolve().parents[1]/'configs/live_agent.json').read_text())


class EdgeRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.now = 100.
        self.calls = []
        self.choice = 'A'
        def call(endpoint, payload):
            self.calls.append((endpoint, copy.deepcopy(payload)))
            return st.tool_response(dict(code=self.choice), {}, 'test', st.SIMPLE_ACTIONS)
        self.runtime = er.Runtime(CONFIG, {('evk_hexagon', 'cpu'): 'edge', ('host_gpu', 'cpu'): 'reverse-gpu'},
                                  call=call, clock=lambda: self.now)
        self.addCleanup(self.runtime.pool.shutdown)
        self.packet = dict(schema=er.SCHEMA, session_id='session', body_id='body1', epoch=1,
            paused=False, busy=False, closed=False, model_source='evk_hexagon', agent_device='cpu', events=[])

    @property
    def cell(self):
        return self.runtime.cells['session']

    def event(self, kind, result=None, call_id='', body='body1', epoch=None):
        return dict(seq=self.cell.ack+1 if self.runtime.cells else 1, kind=kind, body_id=body,
                    epoch=self.packet['epoch'] if epoch is None else epoch, call_id=call_id,
                    result=result if result is not None else {})

    def exchange(self, events=(), **changes):
        self.packet.update(changes)
        self.packet['events'] = list(events)
        return self.runtime.exchange(copy.deepcopy(self.packet))

    def arrive(self):
        return self.exchange([self.event('arrival', dict(status='completed', holder_engaged=False))])

    def decide(self, code='A'):
        self.choice = code
        self.runtime.tick()
        self.cell.future.result(timeout=2)
        self.runtime.tick()
        reply = self.exchange()
        return reply['command']['choices'][0]['message']['tool_calls'][0]['id']

    def start(self, code='A'):
        call = self.decide(code)
        self.exchange([self.event('started', call_id=call)], busy=True)
        return call

    def finish(self, call, result=None, **changes):
        return self.exchange([self.event('result', result or dict(status='completed'), call)], busy=False, **changes)

    def test_autonomous_loop_owns_prompt_tools_and_actual_history(self):
        self.arrive()
        call = self.start()
        self.finish(call, dict(status='observed', stage='panel_presence', decision='R', valid=True))
        self.decide('B')
        endpoint, payload = self.calls[-1]
        self.assertEqual(endpoint, 'edge')
        self.assertEqual(payload['messages'][0]['content'], CONFIG['system_prompt'])
        self.assertEqual(payload['tools'], CONFIG['tools'])
        self.assertEqual([r['role'] for r in payload['messages']], ['system', 'user', 'assistant', 'tool'])
        self.assertEqual(json.loads(payload['messages'][-1]['content'])['decision'], 'R')
        self.assertFalse(payload['parallel_tool_calls'])
        self.assertEqual(payload['messages'][-1]['tool_call_id'], call)

    def test_reject_host_conversation_or_source_truth(self):
        self.arrive()
        for field in ('messages', 'tools', 'system_prompt', 'expected_action', 'temperature'):
            with self.assertRaises(ValueError):
                self.runtime.exchange(dict(self.packet, **{field: []}))
        call = self.start()
        with self.assertRaises(ValueError):
            self.finish(call, dict(status='completed', source_preassembled=True))

    def test_no_recipe_veto_or_action_mask(self):
        self.arrive()
        call = self.start('D')  # Deliberately weld without an inspection.
        self.finish(call)
        self.assertFalse(self.cell.held)
        self.assertEqual(self.cell.history[1]['tool_calls'][0]['function']['name'], 'weld_panel')

    def test_replayed_events_are_acknowledged_once(self):
        arrival = self.event('arrival', dict(status='completed'))
        self.exchange([arrival])
        self.exchange([arrival])
        self.assertEqual(len(self.cell.history), 1)
        call = self.decide()
        started = self.event('started', call_id=call)
        self.exchange([started], busy=True)
        self.exchange([started])
        self.assertEqual(len(self.cell.history), 2)
        result = self.event('result', dict(status='observed', decision='G', valid=True), call)
        self.exchange([result], busy=False)
        self.exchange([result])
        self.assertEqual(len(self.cell.history), 3)
        changed = copy.deepcopy(result)
        changed['result']['decision'] = 'R'
        with self.assertRaises(ValueError):
            self.exchange([changed])

    def test_sequence_gaps_and_unknown_starts_rejected(self):
        self.arrive()
        event = self.event('started', call_id='invented')
        with self.assertRaises(ValueError):
            self.exchange([event])
        event['seq'] += 1
        with self.assertRaises(ValueError):
            self.exchange([event])

    def test_not_started_command_never_enters_conversation(self):
        self.arrive()
        call = self.decide()
        self.exchange(paused=True, epoch=2)
        self.exchange(paused=False, epoch=3)
        self.assertEqual(len(self.cell.history), 1)
        with self.assertRaises(ValueError):
            self.finish(call)

    def test_late_inference_after_pause_is_discarded(self):
        entered, release = threading.Event(), threading.Event()
        original = self.runtime.call
        def delayed(endpoint, payload):
            entered.set()
            release.wait(2)
            return original(endpoint, payload)
        self.runtime.call = delayed
        self.arrive()
        self.runtime.tick()
        self.assertTrue(entered.wait(2))
        future = self.cell.future
        self.exchange(paused=True, epoch=2)
        release.set()
        future.result(timeout=2)
        self.runtime.tick()
        self.assertEqual(self.runtime.discarded, 1)
        self.assertIsNone(self.cell.pending)
        self.assertEqual(len(self.cell.history), 1)

    def test_started_motion_survives_epoch_pause_and_returns_once(self):
        self.arrive()
        call = self.decide('B')
        event = self.event('started', call_id=call)
        # Cancellation can overtake the POST carrying the start acknowledgement.
        self.exchange([event], paused=True, busy=True, epoch=2)
        self.exchange(paused=False, epoch=3)
        self.assertIsNotNone(self.cell.running)
        self.finish(call, dict(status='completed', completed_steps=['apply_sealant', 'place_panel']))
        self.assertEqual(len(self.cell.history), 3)

    def test_partial_temperature_cancel_keeps_only_actual_observation(self):
        self.arrive()
        call = self.start('C')
        rows = [dict(stage='adhesive', decision='R', valid=True, status='observed')]
        self.finish(call, dict(status='cancelled_by_backend_switch', results=rows), epoch=2, model_source='host_gpu')
        self.decide('G')
        self.assertEqual(self.calls[-1][0], 'reverse-gpu')
        self.assertEqual(json.loads(self.calls[-1][1]['messages'][-1]['content'])['results'], rows)

    def test_temperature_bundle_cannot_claim_unsent_second_check(self):
        self.arrive()
        call = self.start('C')
        with self.assertRaises(ValueError):
            self.finish(call, dict(status='completed', results=[dict(stage='adhesive')]))
        with self.assertRaises(ValueError):
            self.finish(call, dict(status='completed', results=[dict(stage='weld'), dict(stage='adhesive')]))

    def test_backend_change_requires_new_epoch(self):
        self.arrive()
        with self.assertRaises(ValueError):
            self.exchange(model_source='host_gpu')
        self.exchange(epoch=2)
        self.decide()
        self.assertEqual(self.calls[-1][0], 'reverse-gpu')
        with self.assertRaises(ValueError):
            self.exchange(epoch=1)

    def test_departure_then_next_arrival_can_share_packet(self):
        self.arrive()
        call = self.start('F')
        result = self.event('result', dict(status='completed'), call)
        arrival = self.event('arrival', dict(status='completed'), body='body2', epoch=2)
        arrival['seq'] += 1
        self.exchange([result, arrival], body_id='body2', epoch=2, busy=False)
        self.assertEqual(self.cell.body, 'body2')
        self.assertEqual(len(self.cell.history), 1)
        self.assertEqual(self.cell.turns, 0)
        self.decide()
        self.assertEqual(json.loads(self.calls[-1][1]['messages'][1]['content'])['body_id'], 'body2')

    def test_arrival_before_departure_rejected(self):
        self.arrive()
        with self.assertRaises(ValueError):
            self.exchange([self.event('arrival', dict(status='completed'), body='body2')], body_id='body2')

    def test_final_result_acknowledged_when_closing(self):
        self.arrive()
        call = self.start('F')
        reply = self.finish(call, closed=True)
        self.assertEqual(reply['ack'], 3)
        self.assertEqual(reply['state'], 'closed')
        self.assertTrue(self.cell.departed)
        with self.assertRaises(ValueError):
            self.exchange(closed=False)

    def test_lease_expires_and_does_not_restart_without_epoch(self):
        self.arrive()
        self.decide()
        self.now += er.LEASE_SECONDS
        self.runtime.tick()
        self.assertTrue(self.cell.held)
        self.assertIsNone(self.cell.pending)
        self.exchange()
        self.assertTrue(self.cell.held)
        self.exchange(epoch=2)
        self.assertFalse(self.cell.held)

    def test_only_one_active_equipment_session(self):
        self.arrive()
        packet = dict(self.packet, session_id='other')
        with self.assertRaises(ValueError):
            self.runtime.exchange(packet)
        self.now += er.LEASE_SECONDS
        self.runtime.exchange(packet)
        self.assertTrue(self.cell.closed)

    def test_model_error_and_turn_budget_hold_without_repair(self):
        self.arrive()
        self.runtime.call = lambda *_: dict(choices=[])
        self.runtime.tick()
        self.cell.future.result(timeout=2)
        self.runtime.tick()
        self.assertTrue(self.cell.held)
        self.assertIsNone(self.cell.pending)
        self.cell.turns = er.MAX_TURNS
        self.exchange(epoch=2)
        self.assertTrue(self.cell.held)

    def test_explicit_model_hold_is_not_an_extra_decision(self):
        self.arrive()
        call = self.start('H')
        self.finish(call, dict(status='held'))
        self.runtime.tick()
        self.assertTrue(self.cell.held)
        self.assertEqual(len(self.calls), 1)


if __name__ == '__main__':
    unittest.main()
