#!/usr/bin/env python3
"""Reproduce paired two-sided Wilcoxon/Holm tests for 180 matched instances.
Requires scipy (`sudo apt install python3-scipy`).
"""
import csv, math
from pathlib import Path
from itertools import combinations
try:
    from scipy.stats import wilcoxon
except ImportError as e:
    raise SystemExit('SciPy required: sudo apt install python3-scipy') from e
root=Path(__file__).resolve().parents[1]
with (root/'results/strong_final_heuristic_results.csv').open(newline='') as f:
    r=list(csv.DictReader(f))
vals={}
for row in r:
    if row['variant'] in ('SEQ','MIN','MAX'):
        key=(int(row['m']),int(row['p']),int(row['seed']),row['objective'],row['variant'])
        vals[key]=float(row['objective_value'])
scales=[(8,4),(10,4),(12,5),(20,8),(40,12),(80,20)]
rows=[]
for q in ['COST','RELIABILITY','ENERGY']:
    for a,b in combinations(['SEQ','MIN','MAX'],2):
        p=[];u=[]
        for m,n in scales:
            for seed in range(1,31):
                p.append(vals[(m,n,seed,q,a)])
                u.append(vals[(m,n,seed,q,b)])
        w=wilcoxon(p,u,zero_method='wilcox',alternative='two-sided',method='auto')
        rows.append([q,f'{a} vs {b}',float(w.pvalue)])
# Holm familywise adjustment across ALL nine tests.
order=sorted(range(len(rows)),key=lambda i:rows[i][2]);adjusted=[1.0]*len(rows)
previous=0.0
for rank,i in enumerate(order):
    previous=max(previous,min(1.0,(len(rows)-rank)*rows[i][2]))
    adjusted[i]=previous
lines=['# Wilcoxon signed-rank tests, two-sided, paired', '', 'Paired 180-instance tests, `zero_method="wilcox"`, Holm correction over 9 tests.', '', '| Objective | Pair | Raw p | Holm-adjusted p |','|---|---|---:|---:|']
for i,(q,p,v) in enumerate(rows):
    lines.append(f'| {q} | {p} | {v:.8g} | {adjusted[i]:.8g} |')
print('\n'.join(lines))
