#!/usr/bin/env python3
"""Acceptance checks for real recipient and handshake-refusal runs."""
import argparse,json
from pathlib import Path
import numpy as np
from collect_protocol_audit import inspect_pair
from validate_numerics import reconstruct,matrix

def validate(run,case,recipient):
    report={}
    for algo in (['ss-lr'] if case=='refusal' else ['ss-pearson','ss-vif','ss-compare']):
        base=run/algo
        audit=inspect_pair(base,'cpp')
        events=[json.loads(s) for s in (base/'protocol-events.jsonl').read_text().splitlines()]
        if case=='refusal':
            assert all(any(e['observed_party']==role and e['stage']=='lifecycle' and e['event']=='failed' for e in events) for role in ('alice','bob'))
            assert any(e['event']=='request_received' for e in events)
            assert any(e['event']=='response_received' and e['error_code']!=0 for e in events)
            assert not any(e['stage']=='compute' for e in events)
            assert not list((base/'output').rglob('result*'))
            report[algo]={'pass':True,'expected':'handshake refused before compute','normal_protocol_pass':audit['pass']}
        else:
            assert audit['pass']
            outputs=[e for e in events if e['stage']=='output']
            assert len(outputs)==(2 if algo=='ss-compare' else 4)
            plain=[e for e in outputs if e.get('semantics') in ('plaintext','comparison_boolean')]
            denied=[e for e in outputs if e['event']=='withheld']
            assert len(plain)==len(denied)==1
            assert plain[0]['rank']==recipient and denied[0]['rank']!=recipient
            owner=plain[0]['observed_party'];other=denied[0]['observed_party']
            assert not (base/'output'/other/'result.csv').exists()
            if algo=='ss-compare':
                expected=matrix(base/'data/perfect_logit_a.csv')[:,0]>matrix(base/'data/perfect_logit_b.csv')[:,0]
                got=np.array([v=='true' for v in (base/'output'/owner/'result.csv').read_text().splitlines()])
                assert np.array_equal(got,expected)
            else:
                expected=reconstruct([base/'output'/role/'result.csv.shares.json' for role in ('alice','bob')])
                got=np.loadtxt(base/'output'/owner/'result.csv',delimiter=',',ndmin=2)
                assert np.allclose(got,expected,atol=1e-7)
            report[algo]={'pass':True,'recipient_rank':recipient,'recipient_party':owner,'nonrecipient_plaintext_absent':True,'rows':len(got)}
    return report
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('--case',choices=['refusal','recipient'],required=True);p.add_argument('--recipient',type=int,default=0);a=p.parse_args()
    r=validate(a.run,a.case,a.recipient);(a.run/'boundary-validation.json').write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2))
