import unittest
from types import SimpleNamespace
from unittest.mock import patch
from dark_factory.gates import load_gate_contract
from dark_factory.gates_host import ModalityClassifier, make_handler

class FakeClassifier:
    def __init__(self, model_path, contract, adapter, **kwargs):
        self.adapter=adapter; self.allowed=kwargs["allowed_stages"]
        self.model_alias=adapter+":"+",".join(self.allowed)
        self.provenance={"weights":adapter,"allowed_stages":self.allowed}
        self.gpu_name="fake"; self.model_config_sha256="config"; self.vision_patch_embedding="fake"
        self.torch=SimpleNamespace(cuda=SimpleNamespace(memory_allocated=lambda:100,max_memory_allocated=lambda:200))
        self.calls=[]; self.warmed=False; self.closed=False
    def inspect(self, request):
        stage=request.identity["stage"]
        if stage not in self.allowed: raise ValueError("wrong branch")
        self.calls.append(stage)
        return {"decision":"A","adapter":self.adapter}
    def warmup(self): self.warmed=True
    def is_ready(self): return self.warmed
    def close(self): self.closed=True

class GateRoutingTests(unittest.TestCase):
    @patch("dark_factory.gates_host.Reason2Classifier",FakeClassifier)
    def test_weld_override_routes_only_by_stage_and_never_retries_holds(self):
        c=ModalityClassifier("base",load_gate_contract(),"new","weld",weld_only=True)
        self.assertEqual(c.models["primary"].allowed,["panel_presence","adhesive","panel_seated"])
        self.assertEqual(c.models["weld"].allowed,["weld"])
        for stage in load_gate_contract()["stages"]:
            reply=c.inspect(SimpleNamespace(identity={"stage":stage}))
            self.assertEqual(reply["adapter"],"weld" if stage=="weld" else "new")
            self.assertEqual(reply["decision"],"A")
            self.assertEqual(reply["backend_alias"],c.model_alias)
            self.assertEqual(reply["stage_backend_alias"],c.stage_backend_aliases[stage])
        self.assertEqual(sum(len(m.calls) for m in c.models.values()),4)
        with self.assertRaises(KeyError): c.inspect(SimpleNamespace(identity={"stage":"unknown"}))
        self.assertFalse(c.is_ready());c.warmup();self.assertTrue(c.is_ready())
        c.close();self.assertTrue(all(m.closed for m in c.models.values()))
    @patch("dark_factory.gates_host.Reason2Classifier",FakeClassifier)
    def test_modality_routing_remains_available_and_identity_tracks_weights(self):
        contract=load_gate_contract()
        c=ModalityClassifier("base",contract,"nir","thermal")
        self.assertEqual(c.models["thermal"].allowed,["adhesive","weld"])
        d=ModalityClassifier("base",contract,"different","thermal")
        self.assertNotEqual(c.model_alias,d.model_alias)
        self.assertEqual(c.routing,"resident-merged-modality-routing-v1")

    @patch("dark_factory.gates_host.Reason2Classifier",FakeClassifier)
    def test_health_reports_weld_override_ready_and_global_memory(self):
        c=ModalityClassifier("base",load_gate_contract(),"new","weld",weld_only=True)
        c.warmup();handler_class=make_handler(c);handler=handler_class.__new__(handler_class)
        handler.path="/health";replies=[];handler.send_json=lambda status,payload:replies.append((status,payload))
        handler.do_GET();status,payload=replies[0]
        self.assertEqual(status,200);self.assertEqual(payload["status"],"ready")
        self.assertEqual(payload["cuda_allocated_bytes"],100)
        self.assertEqual(payload["adapter"],{"primary":"new","weld":"weld"})
