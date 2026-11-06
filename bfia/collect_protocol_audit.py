#!/usr/bin/env python3
"""Validate emitted runtime events, never synthesize successful phases."""
import argparse, collections, hashlib, json
from pathlib import Path

def inspect_pair(base,language):
    by_party={};hashes={}
    for role in ('alice','bob'):
        path=base/(role+('.stdout.log' if language=='python' else '.log'))
        raw=path.read_bytes();hashes[path.name]=hashlib.sha256(raw).hexdigest()
        by_party[role]=[json.loads(line.split('BFIA_EVENT ',1)[1]) for line in raw.decode(errors='replace').splitlines() if 'BFIA_EVENT ' in line]
    checks={};sent=collections.Counter();received=collections.Counter()
    for role,events in by_party.items():
        states={(e['stage'],e['event']) for e in events}
        for phase in ('handshake','compute'):checks[role+'_'+phase]=(phase,'complete') in states
        checks[role+'_identity']=bool(events) and all(e.get('task_id') and e.get('trace_id') and e.get('party') and e.get('algorithm') for e in events)
        if language=='cpp':
            checks[role+'_lifecycle']=('lifecycle','complete') in states
            checks[role+'_transport_drained']=('transport','drained') in states
        checks[role+'_no_failure']=not any(e['event']=='failed' for e in events)
        checks[role+'_ordered']=all(a['seq']<b['seq'] for a,b in zip(events,events[1:]))
        for e in events:
            if e['event'] in ('send_queued','received'):
                key=(e['source'],e['destination'],e['bytes'],e['message_sha256'])
                (sent if e['event']=='send_queued' else received)[key]+=1
    if language=='python':checks['message_correlation']=bool(sent) and sent==received
    else:
        for name in ('request_sent','request_received','response_received'):
            checks[name]=any(e['event']==name for events in by_party.values() for e in events)
    if language=='cpp':
        flat=[e for events in by_party.values() for e in events]
        for outgoing,incoming in [('request_sent','request_received'),('response_queued','response_received')]:
            a=collections.Counter((e.get('task_id'),e.get('bytes'),e.get('parameters')) for e in flat if e['event']==outgoing)
            b=collections.Counter((e.get('task_id'),e.get('bytes'),e.get('parameters')) for e in flat if e['event']==incoming)
            checks[outgoing+'_correlation']=bool(a) and a==b
        checks['response_success']=all(e.get('error_code')==0 for e in flat if e['event']=='response_received')
    combined=sorted([dict(e,observed_party=role) for role,events in by_party.items() for e in events],key=lambda e:(e['timestamp_ms'],e['observed_party'],e['seq']))
    (base/'protocol-events.jsonl').write_text(''.join(json.dumps(e,ensure_ascii=False)+'\n' for e in combined))
    lines=['# '+base.name+' BFIA execution trace','','Only emitted runtime events are included. Cross-host ordering uses message correlation, not wall clock alone.','','| UTC epoch ms | Party | Stage | Event | Detail |','| --- | --- | --- | --- | --- |']
    for e in combined:
        detail={k:v for k,v in e.items() if k not in ('schema','timestamp_ms','monotonic_ns','stage','event','observed_party')}
        lines.append('| {} | {} | {} | {} | {} |'.format(e['timestamp_ms'],e['observed_party'],e['stage'],e['event'],json.dumps(detail,ensure_ascii=False).replace('|','/')))
    (base/'protocol-trace.md').write_text('\n'.join(lines)+'\n')
    result={'pass':all(checks.values()),'checks':checks,'event_count':len(combined),'sent':sum(sent.values()),'received':sum(received.values()),'log_sha256':hashes}
    (base/'protocol-validation.json').write_text(json.dumps(result,indent=2));return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('--language',choices=['python','cpp'],required=True);p.add_argument('--algorithms',nargs='+');a=p.parse_args()
    names=['sgb','he_woe'] if a.language=='python' else ['ecdh-psi','ss-lr','ss-xgb','ss-pearson','ss-vif','ss-compare']
    if a.algorithms:
        assert set(a.algorithms)<=set(names),'unknown algorithm'
        names=a.algorithms
    result={}
    for name in names:
        try:result[name]=inspect_pair(a.run/name,a.language)
        except Exception as exc:result[name]={'pass':False,'error':str(exc)}
    (a.run/'protocol-validation.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
    raise SystemExit(0 if all(v['pass'] for v in result.values()) else 1)
