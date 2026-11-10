#!/usr/bin/env python3
"""Recreate Figure 1 directly from the same canonical strong_final CSV files.
Requires matplotlib; 95% CI is two-sided Student-t on n=90 exact gaps.
"""
import csv, math, statistics as st,collections
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
BASE=Path(__file__).resolve().parents[1]
with (BASE/'results/strong_final_heuristic_results.csv').open(newline='') as f: h=list(csv.DictReader(f))
with (BASE/'results/strong_final_best_of_three_cross_objective.csv').open(newline='') as f: b=list(csv.DictReader(f))
ob=['COST','RELIABILITY','ENERGY'];vs=['SEQ','MIN','MAX']
gaps=collections.defaultdict(list)
for r in h:
    if r['variant'] not in vs or r['exact_status']!='OPTIMAL':continue
    gaps[(r['objective'],r['variant'])].append(float(r['gap_pct']))
assert all(len(gaps[k])==90 for k in [(q,v) for q in ob for v in vs])
labels=['Cost','Failure Exposure','Energy']
width=.24;fig,ax=plt.subplots(figsize=(7.2,4.2));tt_89=1.986979
for v_idx,variant in enumerate(vs):
  vals=[st.mean(gaps[(q,variant)]) for q in ob]
  ci=[tt_89*st.stdev(gaps[(q,variant)])/math.sqrt(90) for q in ob]
  ax.bar([i+(v_idx-1)*width for i in range(3)],vals,width,yerr=ci,capsize=3,label=variant)
ax.set_xticks(range(3));ax.set_xticklabels(labels);ax.set_ylabel('Mean optimality gap (%)');ax.set_xlabel('Placement objective');ax.legend(title='Greedy heuristic');ax.grid(axis='y',alpha=.25)
fig.tight_layout()
out=BASE/'figures';out.mkdir(exist_ok=True)
fig.savefig(out/'heuristic_quality.png',dpi=300,bbox_inches='tight');plt.close(fig)
rt=collections.defaultdict(list)
for r in b:
    rt[(r['optimized_objective'],int(r['m']),int(r['p']))].append(float(r['best_three_total_ms']))
fig,ax=plt.subplots(figsize=(7.2,4.2))
for q,label in zip(ob,labels):
    data=sorted((m,p,st.mean(vals)) for (qq,m,p),vals in rt.items() if qq==q)
    ax.plot([f'{m}/{p}' for m,p,t in data],[t for m,p,t in data],marker='o',linewidth=1.8,label=label)
ax.set_yscale('log');ax.set_xlabel('Deployment scale (services/nodes)');ax.set_ylabel('Combined heuristic runtime (ms)');ax.legend();ax.grid(axis='y',alpha=.25);fig.tight_layout()
fig.savefig(out/'runtime_scaling.png',dpi=300,bbox_inches='tight');plt.close(fig)
print('Generated heuristic_quality.png and runtime_scaling.png from published CSVs')
