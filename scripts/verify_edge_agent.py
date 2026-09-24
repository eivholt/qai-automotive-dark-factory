"""Offline cross-machine audit. Never used to choose or veto live actions."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent/'agent'))
from single_token import SIMPLE_ACTIONS, action_code, named_observation, observation


def verify(report, records, identity):
    errors = []
    def check(value, message):
        if not value:
            errors.append(message)
    agent = report['agent']
    check(agent.get('harness_location') == 'evk', 'Harness is not on EVK')
    check(agent.get('conversation_sent_from_unreal') is False, 'Host supplied conversation')
    check(identity.get('machine') == 'aarch64' and identity.get('owner') == 'evk', 'Missing actual EVK runtime identity')
    rows = [r for r in records if r['session_id'] == report['session_id']]
    choices = [r for r in agent['trace'] if r['event'] == 'agent_choice']
    actual_results = [r for r in agent['trace'] if r['event'] == 'tool_result' and r['detail']['tool'] != 'arrival']
    decisions = {}
    starts, results, arrivals, requests = [], [], [], []
    history = []
    running = None
    for row in rows:
        kind, detail = row['event'], row['detail']
        if kind.startswith('equipment_'):
            event = detail['event']
            if kind == 'equipment_arrival':
                arrivals.append(event)
                history = [dict(role='user', content=json.dumps(dict(body_id=event['body_id'],
                    event='new_body_arrived_choose_tools_to_complete_the_task',
                    arrival=observation(json.dumps(event['result']))), separators=(',', ':')))]
            elif kind == 'equipment_started':
                starts.append(event)
                command = decisions.get(event['call_id'])
                check(command is not None, 'Started job not selected by model')
                if command:
                    check(command['body_id'] == event['body_id'], 'Decision/start body mismatch')
                    history.append(command['message'])
                    running = event['call_id']
            elif kind == 'equipment_result':
                results.append(event)
                check(running == event['call_id'], 'Result does not match actual started job')
                history.append(dict(role='tool', tool_call_id=event['call_id'],
                    content=json.dumps(observation(json.dumps(event['result'])), separators=(',', ':'))))
                running = None
        elif kind == 'model_request':
            requests.append(row)
            payload = detail['payload']
            check(payload['messages'][1:] == history, 'EVK history differs from actual equipment events')
            check(payload['messages'][0] == dict(role='system', content=agent['system_prompt']), 'Changed agent policy')
            check(payload['tools'] == agent['tools'], 'Changed tool schemas')
            check(running is None, 'Agent requested a decision before actual tool completion')
        elif kind == 'model_decision':
            message = detail['reply']['choices'][0]['message']
            call = message['tool_calls'][0]
            check(call['id'] not in decisions, 'Duplicate model call ID')
            check(detail['code'] == action_code(message, SIMPLE_ACTIONS), 'Decision code/tool mismatch')
            decisions[call['id']] = dict(body_id=row['body_id'], message=message)
    check(len(arrivals) == 3, 'Missing three actual arrivals')
    check(len(starts) == len(choices) == len(results) > 0, 'Missing or duplicate equipment execution')
    check(len(results) == len(actual_results), 'Missing actual worker results')
    check(len(requests) == len(decisions) == len(choices), 'Extra/missing model decisions in clean run')
    for event, choice in zip(starts, choices):
        message = decisions[event['call_id']]['message']
        fn = message['tool_calls'][0]['function']
        check(event['body_id'] == choice['body_id'], 'Host/EVK body mismatch')
        check(fn['name'] == choice['detail']['tool'], 'Host executed a different tool')
        check(json.loads(fn['arguments'])['reason'] == choice['detail']['reason'], 'Host changed selected action')
        if 'call_id' in choice['detail']:
            check(event['call_id'] == choice['detail']['call_id'], 'Host changed actual call ID')
    for event, actual in zip(results, actual_results):
        check(event['body_id'] == actual['body_id'], 'Result body differs across machines')
        check(named_observation(event['result']) == named_observation(actual['detail']), 'EVK result differs from actual worker observation')
    count = len(arrivals)+len(starts)+len(results)
    check(agent.get('equipment_events_sent') == agent.get('equipment_events_acknowledged') == count,
          'Final event not acknowledged')
    sessions = [s for s in identity['sessions'] if s['session_id'] == report['session_id']]
    check(len(sessions) == 1 and sessions[0]['closed'] and sessions[0]['ack'] == count, 'EVK session did not close with final result')
    return dict(passed=not errors, errors=errors, decisions=len(decisions), equipment_events=count,
                runtime_machine=identity.get('machine'), runtime_pid=identity.get('pid'),
                inference_endpoints=sorted({r['detail']['endpoint'] for r in requests}),
                scope='Actual cross-machine orchestration and event audit; not model accuracy')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--evk-log', type=Path, required=True)
    parser.add_argument('--identity', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = verify(json.loads(args.runtime.read_text(encoding='utf-8-sig')),
        [json.loads(line) for line in args.evk_log.read_text().splitlines()],
        json.loads(args.identity.read_text(encoding='utf-8-sig')))
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))
    raise SystemExit(int(not result['passed']))
