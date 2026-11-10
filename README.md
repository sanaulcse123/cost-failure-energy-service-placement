# Service Placement for Cost, Failure Exposure, and Energy in the Fog–Edge Continuum

**Reproducibility code and results** for the accepted Mid4CC 2026 paper by
Md Sanaullah Miah, Md Saiful Islam, and Sergey Bereg.

Paper DOI: **https://doi.org/10.1145/3861040.3862566**

This repository contains **one current implementation and one published
experimental dataset**. The previous submission is preserved in Git history,
not duplicated in this working tree. No manuscript, compiled binary, PDF plot,
or intermediate benchmark outputs are included.

## Files

```text
README.md
Makefile
.gitignore
src/
  placement_heuristics_stronger_lb.cpp
results/
  strong_final_heuristic_results.csv             # 2,700 heuristic records
  strong_final_exact_results.csv                 # 270 exact records
  strong_final_best_of_three_cross_objective.csv # 540 selected-placement records
  run_environment.txt                           # PC/compiler used for the published timings
figures/
  heuristic_quality.png
  runtime_scaling.png
scripts/
  verify_results.py
  make_paper_figures.py
  statistical_tests.py
```

## What is evaluated

Three **independently selected** objectives are considered over a shared
capacity-feasible placement set: monetary cost (USD), criticality-weighted
failure exposure (dimensionless score), and modeled electricity use (Wh).
The code/CSV label for failure exposure is `RELIABILITY`; it does **not** mean
end-to-end system reliability or application availability.

The comparison uses SEQ, MIN and MAX service ordering, each followed by
feasibility recovery and local refinement; the **best-of-three** selects
among these three final placements. RANDOM (one seeded order) and LPT
(descending service demand) are **single-order baselines**, not additional
members of best-of-three. The exact branch-and-bound solver provides proven
optima for scales 8/4, 10/4 and 12/5. At larger scales, admissible lower
bounds give conservative gap certificates, not proven exact gaps.

All 180 instances are synthetic (6 service/node scales x 30 deterministic
seeds). CPU demand/capacity are **abstract units**, not calibrated CPU cores.
There is no recovery, redundancy or failure-event simulation; inactive nodes
consume zero modeled power. See the paper for the complete assumptions.

## Run on Ubuntu / WSL

Requirements: `g++` with C++17 support, `make`, `python3`; `matplotlib` for
regenerating PNG plots; optional `scipy` for Wilcoxon/Holm statistics.

```bash
sudo apt update
sudo apt install -y build-essential python3 python3-matplotlib
cd ~/mid4cc_github_clean
make                   # compile the single C++ source
make verify            # verify the checked-in final data: expected PASS
make quick             # optional 2-seed smoke test in ignored runs/
make reproduce         # optional full 180-instance run in ignored runs/
make figures           # regenerate two PNG plots from checked-in data
```

For optional paired Wilcoxon signed-rank tests with Holm correction:

```bash
sudo apt install -y python3-scipy
make stats
```

**Important:** A fresh run does **not** replace the checked-in final results:
`make reproduce` writes to `runs/repro_*.csv`, a Git-ignored location.
The published `results/strong_final_*.csv` were measured on the WSL system
recorded in `results/run_environment.txt`. Runtime numbers **will vary**
across machines/compiler settings; compare deterministic objective values,
not exact elapsed milliseconds. `make verify` checks record counts,
feasibility, objective consistency, refinement, best-of-three selection,
timing sums, bound certificates and exact-status consistency; it is not an
independent mathematical proof of algorithm correctness.

The PNG figures and reported runtimes come from the same 2026-11-10 PC-run
CSV snapshot. No PDF versions of the PNG plots are distributed here.
