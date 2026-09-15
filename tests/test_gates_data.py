import copy
import io
from unittest.mock import patch
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from dark_factory.gates_host import LlamaClassifier, ModalityClassifier
from dark_factory.gates import load_gate_contract, validate_adapter_training, MESSAGE_LAYOUT, model_export_alias

ROOT = Path(__file__).resolve().parents[1]

def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts/model" / (name + ".py"))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result

PREPARE = module("prepare_lora")
EVALUATE = module("evaluate_gates")
TRAIN = module("train_lora")
EXPORT = module("verify_model_export")
CALIBRATE = module("calibrate_gates")

class GateDataTests(unittest.TestCase):
    def test_test_episodes_require_explicit_heldout_selection(self):
        rows = [{"split": "train"}, {"split": "validation"}, {"split": "test"}]
        self.assertEqual(EVALUATE.select_samples(rows, "validation", "development"), [{"split": "validation"}])
        self.assertEqual(EVALUATE.select_samples(rows, "test", "heldout"), [{"split": "test"}])
        for split, purpose in (("", "development"), ("test", "development"), ("", "heldout"), ("validation", "heldout")):
            with self.assertRaises(ValueError):
                EVALUATE.select_samples(rows, split, purpose)

    def test_adapter_contract_rejects_legacy_or_changed_base(self):
        c = load_gate_contract()
        report = {"contract_version": c["version"], "message_layout": MESSAGE_LAYOUT,
            "prompt_hashes": {k:v["prompt_sha256"] for k,v in c["stages"].items()},
            "image_contract": c["image"], "thermal_window_c": c["thermal_window_c"], "base_model_config_sha256": "base"}
        self.assertEqual(validate_adapter_training(report, c, "base"), report)
        for field in ("contract_version", "message_layout", "image_contract", "thermal_window_c", "prompt_hashes", "base_model_config_sha256"):
            broken = {**report, field: "wrong"}
            with self.assertRaises(ValueError):
                validate_adapter_training(broken, c, "base")

    def test_adapter_stage_scope_rejects_modality_misrouting(self):
        c = load_gate_contract()
        report = {"contract_version": c["version"], "message_layout": MESSAGE_LAYOUT,
            "prompt_hashes": {k:v["prompt_sha256"] for k,v in c["stages"].items()},
            "image_contract": c["image"], "thermal_window_c": c["thermal_window_c"],
            "training_stages": ["panel_presence", "panel_seated"]}
        validate_adapter_training(report,c,required_stages=["panel_presence","panel_seated"])
        for stages in (["adhesive"], list(c["stages"])):
            with self.assertRaises(ValueError):
                validate_adapter_training(report,c,required_stages=stages)

    def test_presence_and_seating_have_distinct_action_semantics(self):
        self.assertEqual(EVALUATE.gate_action("panel_presence","G"),"preassembled")
        self.assertEqual(EVALUATE.gate_action("panel_presence","R"),"assemble")
        self.assertEqual(EVALUATE.gate_action("panel_presence","A"),"hold")
        self.assertEqual(EVALUATE.gate_action("panel_seated","R"),"exception_hold")
        self.assertEqual(EVALUATE.gate_action("weld","R"),"hold")
        row = {"reply":{"valid":True}, "identity_valid":True, "stage":"panel_presence", "decision":"R",
            "expected_decision":"G", "episode_id":"one", "end_to_end_ms":1}
        summary = EVALUATE.summarize([row])
        self.assertEqual(summary["wrong_presence_branches"],1)
        self.assertEqual(summary["false_proceed"],0)
        self.assertEqual(summary["action_accuracy"],0)

    def test_preparation_rejects_old_contract_even_with_new_prompt(self):
        c = load_gate_contract()
        spec = c["stages"]["panel_presence"]
        sample = {"sample_id":"one", "body_id":"body", "stage":"panel_presence", "expected_decision":"G",
            "prompt_sha256":spec["prompt_sha256"], "images":[]}
        with tempfile.TemporaryDirectory() as d:
            for version in (None,"automotive-process-v1","automotive-panel-process-v2"):
                sample["contract_version"] = version
                with self.assertRaisesRegex(ValueError,"contract version"):
                    PREPARE.prepare([sample],Path(d),Path(d)/"out",c,"shared","panel")

    def test_continuation_checks_actual_adapter_configuration(self):
        c = load_gate_contract()
        targets = ["q_proj", "qkv"]
        report = {"contract_version": c["version"], "message_layout": MESSAGE_LAYOUT,
            "prompt_hashes": {k:v["prompt_sha256"] for k,v in c["stages"].items()},
            "image_contract": c["image"], "thermal_window_c": c["thermal_window_c"],
            "base_model_config_sha256": "base", "rank": 8, "lora_scope": "multimodal", "target_modules": targets, "steps": 128}
        config = {"target_modules": list(reversed(targets)), "r":8,"lora_alpha":32,"lora_dropout":.05,
            "bias":"none","peft_type":"LORA","use_dora":False,"use_rslora":False}
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root/"training_report.json").write_text(json.dumps(report),encoding="utf-8")
            (root/"adapter_model.safetensors").write_bytes(b"private-fixture-weights")
            (root/"adapter_config.json").write_text(json.dumps(config),encoding="utf-8")
            result = TRAIN.initial_adapter_provenance(root,c,"base",8,"multimodal",targets)
            self.assertEqual(result["previous_steps"],128)
            self.assertEqual(len(result["weights_sha256"]),64)
            for mutation in ({"r":4},{"lora_alpha":16},{"lora_dropout":0},{"target_modules":["q_proj"]},{"rank_pattern":{"q_proj":4}}):
                (root/"adapter_config.json").write_text(json.dumps({**config,**mutation}),encoding="utf-8")
                with self.assertRaises(ValueError):
                    TRAIN.initial_adapter_provenance(root,c,"base",8,"multimodal",targets)

    def test_new_prompts_require_explicit_validated_warm_start_contract(self):
        import copy, hashlib
        parent=load_gate_contract(); current=copy.deepcopy(parent)
        current['stages']['adhesive']['prompt'] += ' Inspect side contacts.'
        current['stages']['adhesive']['prompt_sha256']=hashlib.sha256(current['stages']['adhesive']['prompt'].encode()).hexdigest()
        targets=['q_proj','qkv']
        report={'contract_version':parent['version'],'message_layout':MESSAGE_LAYOUT,
            'prompt_hashes':{k:v['prompt_sha256'] for k,v in parent['stages'].items()},
            'image_contract':parent['image'],'thermal_window_c':parent['thermal_window_c'],
            'base_model_config_sha256':'base','rank':8,'lora_scope':'multimodal','target_modules':targets,'steps':8}
        config={'target_modules':targets,'r':8,'lora_alpha':32,'lora_dropout':.05,'bias':'none','peft_type':'LORA','use_dora':False,'use_rslora':False}
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            (root/'training_report.json').write_text(json.dumps(report))
            (root/'adapter_config.json').write_text(json.dumps(config))
            (root/'adapter_model.safetensors').write_bytes(b'fixture')
            with self.assertRaises(ValueError): TRAIN.initial_adapter_provenance(root,current,'base',8,'multimodal',targets)
            result=TRAIN.initial_adapter_provenance(root,current,'base',8,'multimodal',targets,parent)
            self.assertEqual(result['changed_prompt_stages'],['adhesive'])
            bad=copy.deepcopy(parent);bad['thermal_window_c']=[0,120]
            with self.assertRaises(ValueError): TRAIN.initial_adapter_provenance(root,current,'base',8,'multimodal',targets,bad)

    def test_resident_route_is_fixed_by_stage_and_preserves_model_identity(self):
        from types import SimpleNamespace
        class Model:
            def __init__(self, name):
                self.model_alias=name; self.provenance={"weights":name}; self.adapter=name
                self.gpu_name="fixture"; self.model_config_sha256="base"; self.vision_patch_embedding="linear-nonoverlap-v1"
                self.calls=[]
            def inspect(self, request):
                self.calls.append(request.identity["stage"])
                return {"decision":"A","backend_alias":self.model_alias,"adapter":self.adapter}
        nir, thermal = Model("nir-hash"), Model("thermal-hash")
        with patch("dark_factory.gates_host.Reason2Classifier",side_effect=[nir,thermal]) as factory:
            router=ModalityClassifier("base",load_gate_contract(),"nir","thermal",linear_patch_embedding=True)
        self.assertEqual(factory.call_count,2)
        self.assertEqual(router.stage_backend_aliases,{"panel_presence":"nir-hash","adhesive":"thermal-hash","panel_seated":"nir-hash","weld":"thermal-hash"})
        for stage in ("weld","panel_presence","adhesive","panel_seated"):
            reply=router.inspect(SimpleNamespace(identity={"stage":stage}))
            self.assertEqual(reply["backend_alias"],router.model_alias)
            self.assertEqual(reply["stage_backend_alias"],router.stage_backend_aliases[stage])
            self.assertEqual(reply["decision"],"A")
        self.assertEqual(nir.calls,["panel_presence","panel_seated"])
        self.assertEqual(thermal.calls,["weld","adhesive"])

    def test_proxy_readiness_tracks_backend_health(self):
        classifier = object.__new__(LlamaClassifier)
        classifier.endpoint = "http://127.0.0.1:18194/v1/chat/completions"
        classifier.warmed = True
        with patch("urllib.request.urlopen", return_value=io.BytesIO(b'{"status":"ok"}')) as request:
            self.assertTrue(classifier.is_ready())
            request.assert_called_once_with("http://127.0.0.1:18194/health", timeout=1)
        with patch("urllib.request.urlopen", side_effect=ConnectionRefusedError):
            self.assertFalse(classifier.is_ready())
        classifier.warmed = False
        with patch("urllib.request.urlopen") as request:
            self.assertFalse(classifier.is_ready())
            request.assert_not_called()

    def test_export_pair_hashes_reject_changed_projector(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            for name in ("text", "vision"):
                (root/name).write_bytes(name.encode())
            report = {"model": "nvidia/Cosmos-Reason2-2B", "adapter": "automotive",
                "files": {key: {"bytes": (root/name).stat().st_size, "sha256": EXPORT.file_sha256(root/name)}
                    for key,name in (("text_bf16", "text"), ("vision_f16", "vision"))}}
            (root/"provenance.json").write_text(json.dumps(report), encoding="utf-8")
            alias = model_export_alias(report)
            self.assertEqual(EXPORT.verify_export(root, "text", "vision", alias)["event"], "PROCESS_EXPORT_VERIFIED")
            with self.assertRaises(ValueError):
                EXPORT.verify_export(root, "text", "vision", "wrong")
            (root/"vision").write_bytes(b"broken")
            with self.assertRaises(ValueError):
                EXPORT.verify_export(root, "text", "vision", alias)

    def test_backend_identity_is_checked_against_export_hash_alias(self):
        classifier = object.__new__(LlamaClassifier)
        classifier.endpoint = "http://127.0.0.1:18194/v1/chat/completions"
        classifier.model_alias = "expected-content-hash"
        with patch("urllib.request.urlopen", return_value=io.BytesIO(b'{"data":[{"id":"expected-content-hash"}]}')):
            classifier.verify_backend_identity()
        with patch("urllib.request.urlopen", return_value=io.BytesIO(b'{"data":[{"id":"different-adapter"}]}')):
            with self.assertRaises(ValueError):
                classifier.verify_backend_identity()

    def test_stricter_validation_calibration_never_selects_on_test(self):
        c = load_gate_contract()
        # Exercise the full prespecified calibration grid from its baseline,
        # independently of the confidence threshold selected for the demo.
        c["decision_policy"]["minimum_conditional_probability"] = .8
        rows = []
        for stage in c["stages"]:
            labels = ["G", "R"] if stage.startswith("panel_") else ["G", "A", "R"]
            for i in range(12 if not stage.startswith("panel_") else 6):
                for label in labels:
                    scores = {k: .99 if k == label else .005 for k in "GAR"}
                    if stage == "weld" and label == "A":
                        scores = {"G": .88, "A": .11, "R": .01}
                    rows.append({"sample_id":f"{stage}-{i}-{label}", "episode_id":str(i), "stage":stage,
                        "decision":max(scores, key=scores.get), "expected_decision":label, "identity_valid":True,
                        "reply":{"valid":True,"label_mass":1,"conditional_label_scores":scores}})
        report = {"purpose":"development", "split":"validation", "contract_version":c["version"],
            "message_layout":MESSAGE_LAYOUT,"decision_policy":c["decision_policy"],"rows":rows}
        result = CALIBRATE.calibrate(report,c)
        self.assertEqual(result["recommended_threshold"], .98)
        self.assertFalse(result["outcomes"][0]["qualified"])
        report["split"] = "test"
        with self.assertRaises(ValueError):
            CALIBRATE.calibrate(report,c)

    def test_episode_hash_is_stable(self):
        values = {PREPARE.episode_split("body-12", "v1") for _ in range(20)}
        self.assertEqual(len(values), 1)
        self.assertTrue(values <= {"train", "validation", "test"})

    def test_all_modalities_of_episode_stay_in_same_split(self):
        c = load_gate_contract()
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "camera.png").write_bytes(b"local fixture")
            rows = [{"sample_id": stage, "body_id": "body-1", "stage": stage, "expected_decision": "G", "contract_version":c["version"], "prompt_sha256":spec["prompt_sha256"],
                     "images": [{"camera_id": camera, "path": "camera.png"} for camera in spec["camera_ids"]]} for stage,spec in c["stages"].items()]
            a = PREPARE.prepare(rows, root, root / "a", c, "shared", "v1")
            b = PREPARE.prepare(rows, root, root / "b", c, "modality", "v1")
            self.assertEqual(a["whole_episode_splits"], b["whole_episode_splits"])
            self.assertEqual(len(a["whole_episode_splits"]), 1)
            self.assertEqual(sum(x["batches"] for x in a["counts"].values()), 4)

    def test_episode_leakage_rejected(self):
        c = load_gate_contract()
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "camera.png").write_bytes(b"local fixture")
            row = {"sample_id": "one", "body_id": "same", "stage": "panel_presence", "expected_decision": "R", "split": "train", "contract_version":c["version"], "prompt_sha256":c["stages"]["panel_presence"]["prompt_sha256"],
                   "images": [{"camera_id": k, "path": "camera.png"} for k in ["spot1", "spot2", "spot3"]]}
            with self.assertRaisesRegex(ValueError, "multiple dataset splits"):
                PREPARE.prepare([row, {**row, "split": "test"}], root, root / "output", c, "shared", "v1")

    def test_repeated_benchmark_frames_not_counted_as_independent_accuracy(self):
        rows = [{"repeat": repeat, "reply": {"valid": True}, "identity_valid": True, "expected_decision": "G", "decision": "G", "episode_id": "one", "end_to_end_ms": 100} for repeat in range(3)]
        report = EVALUATE.summarize(rows)
        self.assertEqual(report["requests"], 3)
        self.assertEqual(report["labelled_batches"], 1)
        self.assertEqual(report["correct"], 1)
        self.assertEqual(report["per_class"]["G"]["recall"], 1)
        self.assertIsNone(report["per_class"]["R"]["recall"])

    def test_transport_or_identity_hold_never_earns_amber_accuracy(self):
        for identity, valid in ((True, False), (False, True), (False, False)):
            row = {"reply": {"valid": valid}, "identity_valid": identity,
                "expected_decision": "A", "decision": "A", "episode_id": "one", "end_to_end_ms": 1}
            report = EVALUATE.summarize([row])
            self.assertEqual(report["correct"], 0)
            self.assertEqual(report["valid_replies"], 0)
            self.assertEqual(report["invalid_reply_holds"], 1)
            self.assertEqual(report["confusion_expected_predicted"], {"A->ERROR": 1})
            self.assertFalse(report["episodes"]["one"]["all_correct"])

    def test_action_metric_does_not_relabel_thermal_letters(self):
        row = {"reply": {"valid": True}, "identity_valid": True, "stage": "adhesive",
            "expected_decision": "A", "decision": "R", "episode_id": "one", "end_to_end_ms": 1}
        report = EVALUATE.summarize([row])
        self.assertEqual(report["accuracy"], 0)
        self.assertEqual(report["action_accuracy"], 1)
        row["stage"] = "panel_presence"
        self.assertEqual(EVALUATE.summarize([row])["action_accuracy"], 0)

    def test_unlabelled_transport_probe_has_no_accuracy(self):
        row = {"reply": {"valid": True}, "expected_decision": "", "decision": "A", "end_to_end_ms": 100}
        self.assertNotIn("accuracy", EVALUATE.summarize([row]))
        self.assertEqual(EVALUATE.summarize([])["requests"], 0)

    def test_train_prompt_replay_must_match(self):
        c = load_gate_contract()
        spec = c["stages"]["panel_presence"]
        row = {"sample_id": "one", "episode_id": "body", "stage": "panel_presence", "images": ["spot1", "spot2", "spot3"], "answer": "G", "split":"train", "camera_ids":spec["camera_ids"], "modality":"nir",
               "contract_version": c["version"], "prompt": spec["prompt"], "prompt_sha256": spec["prompt_sha256"]}
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "train.jsonl"
            path.write_text(json.dumps(row) + "\n")
            self.assertEqual(len(TRAIN.load_rows(path, c)), 1)
            self.assertEqual(TRAIN.load_rows(path, c)[0]["images"][0], str((Path(d) / "spot1").resolve()))
            for mutation in ({"split":"test"},{"camera_ids":["spot3","spot2","spot1"]},{"modality":"thermal"}):
                path.write_text(json.dumps({**row, **mutation}) + "\n", encoding="utf-8")
                with self.assertRaises(ValueError):
                    TRAIN.load_rows(path,c,"train")
            path.write_text(json.dumps(row) + "\n", encoding="utf-8")
            with self.assertRaises(ValueError):
                TRAIN.load_rows(path,c,"validation")
            row["prompt"] = "Use the ground truth"
            path.write_text(json.dumps(row) + "\n")
            with self.assertRaises(ValueError):
                TRAIN.load_rows(path, c)

if __name__ == "__main__":
    unittest.main()
