#!/usr/bin/env python3
"""Stdlib-only consistency checks for the published CSVs or reruns.

Run with: python3 scripts/verify_results.py --prefix results/strong_final_
Run quick with: python3 scripts/verify_results.py --prefix runs/quick_ --quick
"""
import argparse, collections, csv, math
from pathlib import Path

SC=[(8,4),(10,4),(12,5),(20,8),(40,12),(80,20)]
QS=['COST','RELIABILITY','ENERGY']
VS=['SEQ','MIN','MAX','RANDOM','LPT']
M={'COST':'cost_value','RELIABILITY':'reliability_value','ENERGY':'energy_value'}
ERRS=[]
def check(cond,message):
    if not cond: ERRS.append(message)
def close(a,b,eps=2e-8):
    try:
        x,y=float(a),float(b)
        return math.isfinite(x) and math.isfinite(y) and math.isclose(x,y,rel_tol=eps,abs_tol=2e-9)
    except (TypeError,ValueError):return False

def load(path):
    with path.open(newline='') as f:return list(csv.DictReader(f))

def keyh(r):return int(r['m']),int(r['p']),int(r['seed']),r['objective'],r['variant']
def keyx(r):return int(r['m']),int(r['p']),int(r['seed']),r['objective']
def keyb(r):return int(r['m']),int(r['p']),int(r['seed']),r['optimized_objective']

p=argparse.ArgumentParser()
p.add_argument('--prefix',default='results/strong_final_')
p.add_argument('--quick',action='store_true')
p.add_argument('--output',default='')
a=p.parse_args()
pre=Path(a.prefix)
h=load(Path(str(pre)+'heuristic_results.csv'))
e=load(Path(str(pre)+'exact_results.csv'))
b=load(Path(str(pre)+'best_of_three_cross_objective.csv'))
sc=SC[:2] if a.quick else SC
seeds=2 if a.quick else 30
expect={(m,pp,s) for m,pp in sc for s in range(1,seeds+1)}
check(len(h)==len(expect)*15,f'heuristic count {len(h)} expected {len(expect)*15}')
check(len(b)==len(expect)*3,f'best-of-three count {len(b)} expected {len(expect)*3}')
check(len(e)==(12 if a.quick else 270),f'exact count {len(e)} expected {12 if a.quick else 270}')
H={keyh(r):r for r in h}; E={keyx(r):r for r in e}; B={keyb(r):r for r in b}
check(len(H)==len(h),'Duplicate heuristic keys')
check(len(E)==len(e),'Duplicate exact keys')
check(len(B)==len(b),'Duplicate best-of-three keys')
for m,pp,s in expect:
    for q in QS:
        rr=[H.get((m,pp,s,q,v)) for v in VS]
        check(all(x is not None for x in rr),f'Missing heuristic record {m}/{pp} seed {s} {q}')
        if not all(x is not None for x in rr):continue
        check(all(r['feasible']=='1' for r in rr),f'Infeasible CSV heuristic {m}/{pp} {s} {q}')
        for r in rr:
            check(close(r['objective_value'],r[M[q]]),f'Objective/metric mismatch {keyh(r)}')
            check(float(r['objective_value']) <= float(r['construction_objective_value'])+2e-8,f'Refinement worsens {keyh(r)}')
            check(float(r['total_ms']) >=0,f'Negative runtime {keyh(r)}')
        row=B.get((m,pp,s,q));check(row is not None,f'Missing best record {m}/{pp} {s} {q}')
        if row is None:continue
        main=rr[:3];val=min(float(r['objective_value']) for r in main)
        check(close(row['optimized_value'],val),f'Best-of-three mismatch {m}/{pp} {s} {q}')
        check(close(row['optimized_value'],row[M[q]]),f'Best selected objective mismatch {m}/{pp} {s} {q}')
        chosen=H.get((m,pp,s,q,row['chosen_variant']))
        check(chosen is not None and close(chosen['objective_value'],val),f'Chosen placement incorrect {m}/{pp} {s} {q}')
        if chosen:
            for col in ['cost_value','reliability_value','energy_value']:
                check(close(row[col],chosen[col]),f'Cross-objective inconsistency {m}/{pp} {s} {q} {col}')
        check(close(row['best_three_total_ms'],sum(float(r['total_ms']) for r in main),eps=3e-7),f'Combined runtime mismatch {m}/{pp} {s} {q}')
        check(close(row['random_objective_value'],rr[3]['objective_value']),f'RANDOM mismatch {m}/{pp} {s} {q}')
        check(close(row['lpt_objective_value'],rr[4]['objective_value']),f'LPT mismatch {m}/{pp} {s} {q}')
        LB=float(row['lower_bound'])
        check(LB >0 and LB <= val+1e-8,f'Invalid lower bound {m}/{pp} {s} {q}: {LB} vs {val}')
        check(close(row['certified_gap_upper_pct'],100*(val-LB)/LB,eps=1e-7),f'Invalid bound percentage {m}/{pp} {s} {q}')
        if q=='RELIABILITY':
            check(close(row['lower_bound'],row['old_lower_bound']),f'RELIABILITY bound changed {m}/{pp} {s}')
        else:
            check(float(row['lower_bound'])+1e-8>=float(row['old_lower_bound']),f'Strong bound weaker {m}/{pp} {s} {q}')
        if m<=12:
            er=E.get((m,pp,s,q));check(er is not None,f'Missing exact solution {m}/{pp} {s} {q}')
            if er:
                check(er['status']=='OPTIMAL',f'Exact not certified: {m}/{pp} {s} {q} {er["status"]}')
                ex=float(er['value']);check(LB <= ex+1e-8 and ex <= val+1e-8,f'Exact outside interval {m}/{pp} {s} {q}')
                check(close(row['exact_value'],ex),f'Exact mismatch {m}/{pp} {s} {q}')
        else:
            check(row['exact_status']=='NOT_RUN',f'Unexpected exact status larger scale {m}/{pp} {s} {q}')
message=(f'VERIFICATION: {"PASS" if not ERRS else "FAIL"}\n'
         f'heuristic={len(h)}; exact={len(e)}; best_of_three={len(b)}; seeds={seeds}; scales={len(sc)}\n'
         f'checks=counts, feasibility, objective values, refinement, best-of-three, timing sums, bounds, exact status\n'
         f'error_count={len(ERRS)}\n')
if ERRS:message+='\n'.join(ERRS[:40])+'\n'
print(message)
if a.output:Path(a.output).write_text(message)
raise SystemExit(1 if ERRS else 0)
