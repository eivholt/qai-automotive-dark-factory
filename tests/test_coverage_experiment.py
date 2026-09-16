import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('coverage_study',ROOT/'scripts/model/evaluate_sealant_coverage.py')
study=importlib.util.module_from_spec(spec);spec.loader.exec_module(study)

class CoverageIsolationTests(unittest.TestCase):
    def test_production_loader_rejects_study(self):
        from dark_factory.gates import load_gate_contract
        from dark_factory.protocol import ProtocolError
        with self.assertRaises(ProtocolError):load_gate_contract(ROOT/'configs/sealant_coverage_experiment.json')

    def test_study_rejects_prompt_and_policy_drift(self):
        original=study.load_study(ROOT/'configs/sealant_coverage_experiment.json')
        for kind in ('prompt','policy','use','stage'):
            candidate=copy.deepcopy(original)
            if kind=='prompt':candidate['stages']['sealant_coverage']['prompt']+=' Altered.'
            if kind=='policy':candidate['decision_policy']['minimum_conditional_probability']=.5
            if kind=='use':candidate['use']='live'
            if kind=='stage':candidate['stages']['panel_presence']=candidate['stages'].pop('sealant_coverage')
            with tempfile.TemporaryDirectory() as d:
                path=Path(d)/'contract.json';path.write_text(json.dumps(candidate))
                with self.subTest(kind=kind),self.assertRaises(ValueError):study.load_study(path)

    def test_production_request_cannot_cross_into_study(self):
        from dark_factory.gates import parse_gate_request
        from dark_factory.protocol import ProtocolError
        contract=study.load_study(ROOT/'configs/sealant_coverage_experiment.json')
        with self.assertRaises(ProtocolError):
            parse_gate_request({'contract_version':'automotive-panel-process-v3','request_id':'request','body_id':'body',
                'stage':'panel_presence','captured_at_ms':0,'images':[]},contract)
