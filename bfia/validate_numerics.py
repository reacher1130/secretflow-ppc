#!/usr/bin/env python3
"""Public-fixture oracle. Never reconstruct shares from private jobs implicitly."""
import argparse
import csv
import json
from pathlib import Path
import numpy as np


def reconstruct(paths):
    parts=[json.loads(Path(p).read_text()) for p in paths]
    first=parts[0]
    for part in parts:
        assert part['format']=='bfia-ring-share-v1'
        assert all(part[k]==first[k] for k in ('ring_bits','fraction_bits','shape'))
    assert sorted(p['rank'] for p in parts)==list(range(len(parts)))
    assert len({len(p['shares_hex']) for p in parts})==1
    bits=first['ring_bits']; modulus=1<<bits; scale=1<<first['fraction_bits']
    values=[]
    for shares in zip(*(p['shares_hex'] for p in parts)):
        v=sum(int(x,16) for x in shares)%modulus
        if v >= modulus//2: v-=modulus
        values.append(v/scale)
    result=np.asarray(values,dtype=float).reshape(first['shape'])
    assert np.isfinite(result).all()
    return result


def matrix(path):
    return np.loadtxt(path, delimiter=',', skiprows=1, ndmin=2)


def reference_xgb(X,y,rounds=1,depth=1,bins=4,rate=.1):
    candidates=[X[:,f] < (X[:,f].min()+(X[:,f].max()-X[:,f].min())*b/bins) for f in range(X.shape[1]) for b in range(1,bins)]
    pred=np.zeros(len(y));model=[]
    for t in range(rounds):
        prob=1/(1+np.exp(-pred));G=prob-y;H=prob*(1-prob)
        def grow(mask,d):
            g=(G*mask).sum();h=(H*mask).sum();weight=-rate*g/(h+1)
            leaf=mask*weight
            if d==depth:return leaf
            best=-1e6;selected=np.zeros(len(y));parent=g*g/(h+1)
            for c in candidates:
                left=mask*c;right=mask-left
                gl=(G*left).sum();hl=(H*left).sum();gr=g-gl;hr=h-hl
                gain=.5*(gl*gl/(hl+1)+gr*gr/(hr+1)-parent)
                if left.sum()>=1 and right.sum()>=1 and gain>best:
                    best=gain;selected=left
            children=grow(selected,d+1)+grow(mask-selected,d+1)
            return children if best>0 else leaf
        pred+=grow(np.ones(len(y)),0)
    return 1/(1+np.exp(-pred))


def predict_saved_xgb(base,X):
    prefix='result.shares.json'
    model=reconstruct([base/'output'/role/(prefix+'.model.json') for role in ('alice','bob')]).reshape(-1)
    metas=[json.loads((base/'output'/role/(prefix+'.model.metadata.json')).read_text()) for role in ('alice','bob')]
    assert metas[0]==metas[1]
    meta=metas[0];f=meta['features'];assert f==X.shape[1]
    lo=model[:f];hi=model[f:2*f];offset=2*f
    pred=np.full(len(X),meta['base_margin'],dtype=float)
    def node(depth):
        nonlocal offset
        weight=model[offset];offset+=1
        if depth==meta['max_depth']:return np.full(len(X),weight)
        candidate=int(round(model[offset]));offset+=1
        left=node(depth+1);right=node(depth+1)
        if candidate<0:return np.full(len(X),weight)
        feature,bin_index=divmod(candidate,meta['bins']-1)
        assert feature<f
        threshold=lo[feature]+(hi[feature]-lo[feature])*(bin_index+1)/meta['bins']
        return np.where(X[:,feature]<threshold,left,right)
    for _ in range(meta['tree_count']):pred+=node(0)
    assert offset==len(model)
    return (1/(1+np.exp(-pred)) if meta['objective']==1 else pred).reshape(-1,1)


def validate(run, lr_rate=.0001, lr_epochs=1,xgb_rounds=1, algorithms=None):
    report={};derived=run/'validated';derived.mkdir(exist_ok=True)
    for algo in ['ss-lr','ss-xgb','ss-pearson','ss-vif']:
        if algorithms is not None and algo not in algorithms: continue
        base=run/algo
        try:
            suffix='result.csv.shares.json' if algo in ('ss-pearson','ss-vif') else 'result.shares.json'
            got=reconstruct([base/'output'/role/suffix for role in ('alice','bob')])
            if algo in ('ss-pearson','ss-vif'):
                for role in ('alice','bob'):
                    plain=np.loadtxt(base/'output'/role/'result.csv',delimiter=',',ndmin=2)
                    assert np.allclose(plain,got,atol=1e-7), 'recipient reconstruction differs' 
            a=matrix(base/'data/perfect_logit_a.csv');b=matrix(base/'data/perfect_logit_b.csv')
            blocks={'alice':a[:,[8,1,2,3,4,5,6,7]],'bob':b[:,[3,4,5,6,7,8,9]]}
            ranks={role:json.loads((base/'output'/role/suffix).read_text())['rank'] for role in blocks}
            X=np.column_stack([blocks[role] for role in sorted(blocks,key=ranks.get)]);y=a[:,-1]
            if algo=='ss-lr':
                X=np.column_stack([X,np.ones(len(X))]);w=np.zeros(X.shape[1])
                for _ in range(lr_epochs):
                    for offset in range(0,len(X)//128*128,128):
                        x=X[offset:offset+128];labels=y[offset:offset+128]
                        prob=.5+.125*(x@w) # negotiated Minimax_1
                        grad=x.T@(prob-labels)+.5*np.r_[w[:-1],0]
                        w-=lr_rate*(grad/128)
                ref=w.reshape(-1,1);tol=2e-5 if lr_epochs==1 else .002
                assert np.count_nonzero(got)>0,'training update vanished'
            elif algo=='ss-xgb':
                ref=reference_xgb(X,y,rounds=xgb_rounds).reshape(-1,1);tol=.004
                assert ((got>=0)&(got<=1)).all(),'not probabilities'
                model_error=float(np.max(np.abs(got-predict_saved_xgb(base,X))))
                assert model_error<.002,('saved model differs',model_error)
            else:
                corr=np.corrcoef(X,rowvar=False)
                ref=corr if algo=='ss-pearson' else np.diag(np.linalg.inv(corr)).reshape(-1,1)
                tol=.01 if algo=='ss-pearson' else .03
            assert got.shape==ref.shape,(got.shape,ref.shape)
            err=float(np.max(np.abs(got-ref)))
            np.savetxt(derived/(algo+'.csv'),got,delimiter=',',fmt='%.12g')
            report[algo]={'pass':err<=tol,'max_abs_error':err,'tolerance':tol,'shape':list(got.shape),'min':float(got.min()),'max':float(got.max())}
            if algo=='ss-xgb':report[algo]['saved_model_prediction_error']=model_error
        except Exception as exc:
            report[algo]={'pass':False,'error':str(exc)}
    for algo in ('ecdh-psi','ss-compare'):
        if algorithms is not None and algo not in algorithms: continue
        base=run/algo
        try:
            if algo=='ecdh-psi':
                expected=set(matrix(base/'data/psi_1.csv').reshape(-1))&set(matrix(base/'data/psi_2.csv').reshape(-1))
                outputs=[set(matrix(base/'output'/role/'result.csv').reshape(-1)) for role in ('alice','bob')]
                assert all(v==expected for v in outputs)
                report[algo]={'pass':True,'intersection_count':len(expected)}
            else:
                X=matrix(base/'data/perfect_logit_a.csv');B=matrix(base/'data/perfect_logit_b.csv');expected=X[:,0]>B[:,0]
                outputs=[np.asarray([(v.strip()=='true') for v in (base/'output'/role/'result.csv').read_text().splitlines()]) for role in ('alice','bob')]
                assert all(np.array_equal(v.astype(bool),expected) for v in outputs)
                report[algo]={'pass':True,'count':len(expected),'mode':'two-column Alice x1 > Bob x11'}
        except Exception as exc:report[algo]={'pass':False,'error':str(exc)}
    return report

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('run',type=Path);ap.add_argument('--public-fixtures',action='store_true',required=True);ap.add_argument('--lr-rate',type=float,default=.0001);ap.add_argument('--lr-epochs',type=int,default=1);ap.add_argument('--xgb-rounds',type=int,default=1);ap.add_argument('--algorithms',nargs='+',choices=['ss-lr','ss-xgb','ss-pearson','ss-vif','ecdh-psi','ss-compare']);args=ap.parse_args()
    r=validate(args.run,args.lr_rate,args.lr_epochs,args.xgb_rounds,args.algorithms)
    (args.run/'numerical-validation.json').write_text(json.dumps(r,indent=2))
    print(json.dumps(r,indent=2));raise SystemExit(0 if all(x['pass'] for x in r.values()) else 1)
