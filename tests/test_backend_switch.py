import copy
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('backend_switch', Path(__file__).resolve().parents[1]/'scripts/verify_backend_switch.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class BackendSwitchAuditTest(unittest.TestCase):
    def fixture(self, agent=True):
        events = [{'event': 'backend_switch', 'body_sequence': n, 'model_source': source}
                  for n, source in ((2, 'host_gpu'), (3, 'evk_hexagon'))]
        trace = []
        for n, source in ((1, 'evk_hexagon'), (2, 'host_gpu'), (3, 'evk_hexagon')):
            events.append(dict(event='decision', body_sequence=n, model_source=source))
            trace.extend([
                dict(event='agent_choice', body_sequence=n, model_source=source,
                     agent_endpoint=f'http://127.0.0.1:{18194 if n==2 else 18196}/v1/chat/completions'),
                dict(event='tool_result', body_sequence=n, model_source=source, detail={'tool': 'inspect', 'valid': True})])
        return dict(passed=True, completed_bodies=3, source='evk_hexagon', control_mode='agent' if agent else 'flow',
                    backend_switch=dict(enabled=True, smoke=True, switches=2, selected='evk_hexagon'), events=events, agent={'trace': trace})

    def test_both_modes(self):
        for agent in (False, True):
            self.assertTrue(module.verify(self.fixture(agent))['passed'])

    def test_wrong_vision_backend_rejected(self):
        for agent in (False, True):
            r = self.fixture(agent)
            if agent:
                r['agent']['trace'][3]['model_source'] = 'evk_hexagon'
            else:
                r['events'][3]['model_source'] = 'evk_hexagon'
            self.assertFalse(module.verify(r)['passed'])

    def test_bundled_tools_use_each_actual_inspection_result(self):
        r = self.fixture()
        r['agent']['tool_contract'] = 'darkfactory-live-agent-v2'
        for row in r['agent']['trace']:
            if row['event'] == 'tool_result':
                row['event'] = 'inspection_result'
                row['detail'] = dict(stage='adhesive', valid=True)
        self.assertTrue(module.verify(r)['passed'])
        r['agent']['trace'][3]['model_source'] = 'evk_hexagon'
        self.assertFalse(module.verify(r)['passed'])

    def test_wrong_agent_backend_rejected(self):
        r = self.fixture()
        r['agent']['trace'][2]['agent_endpoint'] = 'http://127.0.0.1:18196/v1/chat/completions'
        self.assertFalse(module.verify(r)['passed'])

    def test_optional_npu_agent_must_route_back_to_npu_proxy(self):
        r = self.fixture()
        r['backend_switch']['evk_agent_npu'] = True
        self.assertFalse(module.verify(r)['passed'])
        for row in r['agent']['trace']:
            if row['model_source'] == 'evk_hexagon':
                row['agent_endpoint'] = 'http://127.0.0.1:18197/v1/chat/completions'
        self.assertTrue(module.verify(r)['passed'])
        r['agent']['trace'][-2]['agent_endpoint'] = 'http://127.0.0.1:18196/v1/chat/completions'
        self.assertFalse(module.verify(r)['passed'])

    def test_missing_or_invalid_evidence_rejected(self):
        r = self.fixture()
        mutations = [lambda x: x.update(passed=False), lambda x: x.update(completed_bodies=2),
                     lambda x: x['backend_switch'].update(switches=0), lambda x: x['events'].pop(0),
                     lambda x: x['agent']['trace'].pop(3),
                     lambda x: x['agent']['trace'][3]['detail'].update(valid=False)]
        for mutate in mutations:
            altered = copy.deepcopy(r)
            mutate(altered)
            self.assertFalse(module.verify(altered)['passed'])
