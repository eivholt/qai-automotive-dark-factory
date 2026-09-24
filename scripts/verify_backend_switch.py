"""Check real EVK/GPU/EVK routing in a completed three-body backend fixture."""
import argparse
import json
from pathlib import Path


def verify(report):
    errors = []
    def check(ok, message):
        if not ok:
            errors.append(message)
    switching = report.get('backend_switch', {})
    check(report.get('passed') is True and report.get('completed_bodies') == 3, 'Incomplete or failed process')
    check(switching.get('enabled') is True and switching.get('smoke') is True, 'Not the backend-switch fixture')
    check(switching.get('switches') == 2 and switching.get('selected') == 'evk_hexagon', 'Wrong switch count or final backend')
    check(report.get('source') == 'evk_hexagon', 'Final source is not EVK')
    changes = [r for r in report.get('events', []) if r.get('event') == 'backend_switch']
    check([(r.get('body_sequence'), r.get('model_source')) for r in changes] == [(2, 'host_gpu'), (3, 'evk_hexagon')],
          'Missing or unexpected backend transitions')
    agent = report.get('control_mode') == 'agent'
    bundled = report.get('agent', {}).get('tool_contract') == 'darkfactory-live-agent-v2'
    rows = report.get('agent', {}).get('trace', []) if agent else report.get('events', [])
    observed = {}
    for body, source in ((1, 'evk_hexagon'), (2, 'host_gpu'), (3, 'evk_hexagon')):
        inspections = [r for r in rows if r.get('body_sequence') == body and
                       ((r.get('event') == 'inspection_result' if bundled else
                         r.get('event') == 'tool_result' and r.get('detail', {}).get('tool') == 'inspect')
                        if agent else r.get('event') == 'decision')]
        check(bool(inspections), f'Body {body} has no recorded inspection results')
        check(all(r.get('model_source') == source for r in inspections), f'Body {body} used the wrong vision backend')
        if agent:
            check(all(r['detail'].get('valid') is True for r in inspections), f'Body {body} has invalid inspections')
            choices = [r for r in rows if r.get('body_sequence') == body and r.get('event') == 'agent_choice']
            evk_port = 18197 if switching.get('evk_agent_npu') is True else 18196
            endpoint = f'http://127.0.0.1:{18194 if source == "host_gpu" else evk_port}/v1/chat/completions'
            check(bool(choices) and all(r.get('agent_endpoint') == endpoint and r.get('model_source') == source for r in choices),
                  f'Body {body} used the wrong agent backend')
        observed[str(body)] = {'source': source, 'inspections': len(inspections)}
    return {'passed': not errors, 'errors': errors, 'control_mode': report.get('control_mode'),
            'bodies': observed, 'scope': 'Rendered routing integration; not held-out accuracy'}


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('report', type=Path)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    if args.report.resolve() == args.output.resolve():
        p.error('Output must not overwrite the runtime report')
    result = verify(json.loads(args.report.read_text(encoding='utf-8-sig')))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(result, indent=2)
    args.output.write_text(text+'\n', encoding='utf-8', newline='\n')
    print(text)
    raise SystemExit(0 if result['passed'] else 1)
