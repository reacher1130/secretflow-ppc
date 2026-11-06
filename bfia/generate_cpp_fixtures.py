#!/usr/bin/env python3
"""Generate deterministic synthetic BFIA smoke inputs; never use business data."""
import argparse
import csv
import random
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('directory',type=Path)
parser.add_argument('--rows',type=int,default=256)
args=parser.parse_args()
if args.rows<32:
    parser.error('at least 32 rows are required for the multivariate smoke tests')
args.directory.mkdir(parents=True,exist_ok=True)
rng=random.Random(20260909)
with (args.directory/'perfect_logit_a.csv').open('w',newline='') as af, (args.directory/'perfect_logit_b.csv').open('w',newline='') as bf:
    a,b=csv.writer(af,lineterminator="\n"),csv.writer(bf,lineterminator="\n")
    a.writerow(['x'+str(i) for i in range(1,11)]+['y'])
    b.writerow(['x'+str(i) for i in range(11,21)])
    for _ in range(args.rows):
        features=[rng.uniform(-1,1) for _ in range(20)]
        label=int(features[8]+features[13]-0.3*features[1]>0)
        a.writerow(features[:10]+[label]);b.writerow(features[10:])
for number,start in [(1,0),(2,args.rows//2)]:
    with (args.directory/('psi_'+str(number)+'.csv')).open('w',newline='') as file:
        writer=csv.writer(file,lineterminator="\n");writer.writerow(['id'+str(number)])
        writer.writerows([[100000+i] for i in range(start,start+args.rows)])
