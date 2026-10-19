"""Exhaustive event-order exploration of an explicitly bounded abstraction.

One old command, one cancellation, one revocation, one acknowledgement.
No physical failure probabilities or unbounded verification claims.
"""
import itertools
import json
from pathlib import Path

POLICIES = ['local_stop', 'ingress_fence', 'commit_immediate',
            'split_check_barrier', 'atomic_commit_barrier']

def explore(policy):
    events = ['dispatch', 'check', 'start', 'finish', 'cancel', 'revoke', 'ack']
    counts = dict(schedules=0, post_ack_starts=0, post_ack_completions=0,
                  false_quiescence=0)
    witnesses = {}
    for order in itertools.permutations(events):
        pos = {x: order.index(x) for x in events}
        if not (pos['dispatch'] < pos['check'] < pos['start'] < pos['finish']
                and pos['dispatch'] < pos['cancel'] < pos['revoke']
                and pos['cancel'] < pos['ack']):
            continue
        if policy.endswith('barrier') and pos['revoke'] > pos['ack']:
            continue
        running = False
        admitted = False
        started = False
        revoked = False
        acked = False
        post_start = post_finish = false_ack = False
        valid = True
        for event in order:
            if event == 'check':
                admitted = (not revoked or policy in ['local_stop', 'commit_immediate',
                                                       'atomic_commit_barrier'])
            elif event == 'start':
                allowed = admitted
                if policy in ['commit_immediate', 'atomic_commit_barrier']:
                    allowed = allowed and not revoked
                if allowed:
                    started = running = True
                    post_start |= acked
            elif event == 'finish':
                if started:
                    post_finish |= acked
                    running = False
            elif event == 'revoke':
                revoked = True
            elif event == 'ack':
                if policy.endswith('barrier') and running:
                    valid = False
                    break
                acked = True
                false_ack = running
        if not valid:
            continue
        counts['schedules'] += 1
        for name, bad in [('post_ack_starts', post_start),
                          ('post_ack_completions', post_finish),
                          ('false_quiescence', false_ack)]:
            counts[name] += bad
            if bad:
                witnesses.setdefault(name, list(order))
    return dict(policy=policy, **counts, witnesses=witnesses)

if __name__ == '__main__':
    result = [explore(p) for p in POLICIES]
    assert result[-1]['post_ack_starts'] == 0
    assert result[-1]['post_ack_completions'] == 0
    assert result[-2]['post_ack_starts'] > 0
    output = Path(__file__).resolve().parents[1] / 'evidence' / 'model_results.json'
    output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
