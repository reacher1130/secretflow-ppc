#!/usr/bin/env python3
"""Numerical acceptance for the explicitly public breast-cancer fixture."""
import argparse,json,math
from pathlib import Path
import numpy as np
from sklearn.datasets import load_breast_cancer
from sklearn.metrics import roc_auc_score

def validate(run):
    a=json.loads((run/'sgb/alice-report.json').read_text());b=json.loads((run/'sgb/bob-report.json').read_text())
    preds=np.asarray(a['predictions']);labels=np.asarray(a['labels'])
    auc=float(roc_auc_score(labels,preds))
    result={'sgb':{'pass':a==b and np.isfinite(preds).all().item() and ((preds>=0)&(preds<=1)).all().item() and abs(auc-a['auc'])<1e-12,'samples':len(preds),'auc_recomputed':auc,'scope':'training fixture; no generalization claim'}}
    data=load_breast_cancer();X=data.data;y=data.target;pos=int(y.sum());neg=len(y)-pos
    errors=[];bins=0;feature_count=0
    for role in ('alice','bob'):
        report=json.loads((run/'he_woe'/(role+'-report.json')).read_text())['report']
        for v in report['variables']:
            feature_count+=1;column=X[:,list(data.feature_names).index(v['name'])]
            assignment=np.searchsorted(v['split_points'],column,side='left')
            for k,(count,got,iv) in enumerate(zip(v['total_counts'],v['filling_values'],v['ivs'])):
                mask=assignment==k;n=int(mask.sum());p=int(y[mask].sum());q=n-p
                pd=(p+.5)/pos if p==0 or q==0 else p/pos
                nd=(q+.5)/neg if p==0 or q==0 else q/neg
                expected=math.log(pd/nd);expected_iv=(pd-nd)*expected
                errors += [abs(n-count),abs(got-expected),abs(iv-expected_iv)];bins+=1
    result['he_woe']={'pass':max(errors)<1e-10,'features':feature_count,'bins':bins,'max_abs_error':max(errors),'scope':'counts, WOE and IV recomputed from public fixture; privacy-suppressed rate NaNs are permitted'}
    return result
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('run',type=Path);a=p.parse_args();r=validate(a.run);(a.run/'numerical-validation.json').write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2));raise SystemExit(0 if all(v['pass'] for v in r.values()) else 1)
