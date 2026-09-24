"""Synthetic audit regression; these fixtures are not rendered/model evidence."""
import copy
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'scripts'), str(ROOT/'scripts/agent')]
from edge_runtime import Runtime, SCHEMA
from single_token import tool_response, SIMPLE_ACTIONS
from verify_edge_agent import verify


class EdgeAuditTests(unittest.TestCase):
    def setUp(self):
        config = json.loads((ROOT/'configs/live_agent.json').read_text())
        runtime = Runtime(config, {('evk_hexagon', 'cpu'): 'synthetic'},
            call=lambda *_: tool_response(dict(code='F'), {}, 'fixture', SIMPLE_ACTIONS))
        self.addCleanup(runtime.pool.shutdown)
        packet = dict(schema=SCHEMA, session_id='test', body_id='body1', epoch=1, paused=False,
            busy=False, closed=False, model_source='evk_hexagon', agent_device='cpu', events=[])
        trace, seq = [], 0
        for i in range(3):
            packet.update(body_id=f'body{i+1}', epoch=i+1, busy=False)
            def event(kind, result, call=''):
                nonlocal seq
                seq += 1
                packet['events'] = [dict(seq=seq, kind=kind, result=result, call_id=call,
                    body_id=packet['body_id'], epoch=packet['epoch'])]
                return runtime.exchange(packet)
            event('arrival', dict(status='completed'))
            runtime.tick()
            runtime.cells['test'].future.result(timeout=2)
            runtime.tick()
            packet['events'] = []
            reply = runtime.exchange(packet)['command']
            call = reply['choices'][0]['message']['tool_calls'][0]
            trace.append(dict(event='agent_choice', body_id=packet['body_id'], detail=dict(
                tool='finish_body', call_id=call['id'], reason=json.loads(call['function']['arguments'])['reason'])))
            packet['busy'] = True
            event('started', {}, call['id'])
            packet.update(busy=False, closed=i==2)
            result = dict(status='completed', completed_steps=['send_forward'])
            event('result', result, call['id'])
            trace.append(dict(event='tool_result', body_id=packet['body_id'], detail=dict(result, tool='finish_body')))
        self.report = dict(session_id='test', agent=dict(harness_location='evk', conversation_sent_from_unreal=False,
            trace=trace, system_prompt=config['system_prompt'], tools=config['tools'],
            equipment_events_sent=seq, equipment_events_acknowledged=seq))
        self.records = list(runtime.events)
        self.identity = dict(runtime.snapshot(), machine='aarch64', pid=123)

    def test_reconstructs_history_and_matches_all_actual_choices(self):
        self.assertTrue(verify(self.report, self.records, self.identity)['passed'])

    def test_detects_changed_host_action_and_call_identity(self):
        self.report['agent']['trace'][0]['detail'].update(tool='weld_panel', call_id='invented')
        result = verify(self.report, self.records, self.identity)
        self.assertFalse(result['passed'])
        self.assertIn('Host executed a different tool', result['errors'])
        self.assertIn('Host changed actual call ID', result['errors'])

    def test_detects_fabricated_equipment_result(self):
        self.report['agent']['trace'][1]['detail']['status'] = 'failed'
        result = verify(self.report, self.records, self.identity)
        self.assertIn('EVK result differs from actual worker observation', result['errors'])

    def test_detects_conversation_injection_and_missing_final_ack(self):
        rows = copy.deepcopy(self.records)
        request = next(r for r in rows if r['event'] == 'model_request')
        request['detail']['payload']['messages'].append(dict(role='user', content='invented'))
        self.report['agent']['equipment_events_acknowledged'] -= 1
        result = verify(self.report, rows, self.identity)
        self.assertIn('EVK history differs from actual equipment events', result['errors'])
        self.assertIn('Final event not acknowledged', result['errors'])


if __name__ == '__main__':
    unittest.main()
