# Service Placement for Cost, Failure Exposure, and Energy in the Fog–Edge Continuum

This repository contains the reproducibility package for the Mid4CC 2026
paper:

**Service Placement for Cost, Failure Exposure, and Energy in the Fog–Edge Continuum**

The work studies service placement over heterogeneous and
capacity-constrained fog–edge nodes under three independently selectable
objectives:

1. **Monetary Cost**
2. **Criticality-Weighted Failure Exposure**
3. **Physical Energy Consumption**

The repository includes the complete C++17 implementation of the proposed
SEQ, MIN, and MAX greedy service-ordering methods, the exact
branch-and-bound reference solver used on small instances, final
experimental outputs, processed summaries, and scripts for reproducing
the figures reported in the paper.

---

## Overview

A placement assigns each application service to exactly one candidate
fog–edge node while respecting node-capacity constraints.

The three objectives share the same feasible placement region but are
optimized independently.

### Monetary Cost

The cost objective includes:

- fixed node activation cost,
- non-energy node operating cost, and
- service-to-node deployment cost.

Electricity is not included in this objective because physical energy is
modeled separately.

### Failure Exposure

The failure-oriented objective combines:

- historical node failure rate,
- estimated service exposure duration, and
- service criticality.

The resulting metric is referred to as **failure exposure**, rather than
full reliability or availability, because it does not model repair,
replication, or correlated failures.

### Energy

The energy objective uses a utilization-dependent node power model that
includes:

- idle power, and
- load-dependent dynamic power.

Energy is measured in watt-hours (Wh).

---

## Placement Methods

The placement procedure separates two decisions:

1. **Which service should be placed next?**
2. **Which feasible node should receive that service?**

For a selected service, all methods rank capacity-feasible nodes according
to the incremental increase in the currently optimized objective and
prefer the node producing the smallest increase.

SEQ, MIN, and MAX differ only in how they select the next unplaced service.

### SEQ

**SEQ** processes services in increasing identifier order.

It provides a deterministic baseline that does not use the current
objective values to decide which service is processed next.

### MIN

For every unplaced service, MIN computes the smallest objective increase
currently available across its feasible destination nodes.

**MIN selects the service having the smallest such value.**

Thus, MIN follows an *easy-first* strategy: services with inexpensive
current placement opportunities are processed first.

### MAX

MAX uses the same current best-placement value but selects the service
having the largest value.

Thus, **MAX follows a hard-first strategy**, giving priority to services
whose best currently available placement is already relatively expensive.

For MIN and MAX, service-selection ties are resolved deterministically
using service demand and then service identifier.

---

## Feasibility Safeguard and Refinement

A locally preferred placement can occasionally reduce residual capacity
so that the remaining services can no longer be assigned.

The construction procedure therefore contains a feasibility-aware
backtracking safeguard. Candidate nodes are considered in greedy
objective order. If the preferred assignment makes completion
impossible, the algorithm rolls back that decision and tries the
next-best candidate node.

This mechanism is used only to obtain a complete feasible greedy
construction; it is **not** an exact optimization procedure.

After construction, solutions are improved using best-improving
single-service relocation.

For the Cost and Energy objectives, an additional active-node elimination
step attempts to remove an active node and repack its services when doing
so improves the selected objective.

---

## Repository Structure

```text
cost-failure-energy-service-placement/
│
├── README.md
├── Makefile
├── .gitignore
│
├── src/
│   └── placement_heuristics.cpp
│
├── results/
│   ├── final_heuristic_results.csv
│   ├── final_exact_results.csv
│   │
│   └── summaries/
│       ├── best_of_three_summary.csv
│       ├── variant_gap_summary.csv
│       └── combined_runtime_by_scale.csv
│
├── scripts/
│   └── make_paper_figures.py
│
└── figures/
    ├── heuristic_quality.png
    ├── heuristic_quality.pdf
    ├── runtime_scaling.png
    └── runtime_scaling.pdf
```

---

## Requirements

### C++ Experiment Code

The implementation requires:

- a C++17-compatible compiler
- GNU Make

The reported experiments were compiled using:

```text
g++ 13.3.0
C++17
-O3 -DNDEBUG -Wall -Wextra -Wpedantic
```

No external optimization solver is required. The exact reference solver
is implemented directly in C++ using branch-and-bound.

### Figure Generation

The figure-generation script requires:

- Python 3
- Matplotlib

For example:

```bash
python3 -m pip install matplotlib
```

---

## Building the Implementation

Clone the repository:

```bash
git clone https://github.com/sanaulcse123/cost-failure-energy-service-placement.git
cd cost-failure-energy-service-placement
```

Compile with:

```bash
make
```

This produces:

```text
placement_heuristics
```

---

## Quick Test

A small test can be run with:

```bash
make quick
```

or equivalently:

```bash
./placement_heuristics --quick
```

Quick mode evaluates two deterministic seeds on the two smallest
configurations:

```text
8 services / 4 nodes
10 services / 4 nodes
```

---

## Reproducing the Main Experiment

The final experiment reported in the paper was executed with:

```bash
./placement_heuristics \
  --seeds 30 \
  --slack-min 0.15 \
  --slack-max 0.35 \
  --prefix final_
```

The experiment evaluates the following six deployment scales:

```text
8 services  / 4 nodes
10 services / 4 nodes
12 services / 5 nodes
20 services / 8 nodes
40 services / 12 nodes
80 services / 20 nodes
```

For every generated instance, the implementation evaluates:

```text
3 objectives × 3 service-ordering methods
```

namely:

```text
Cost             × {SEQ, MIN, MAX}
Failure Exposure × {SEQ, MIN, MAX}
Energy           × {SEQ, MIN, MAX}
```

The complete experiment therefore contains:

- **6 deployment scales**
- **30 deterministic seeds per scale**
- **180 generated instances**
- **1,620 heuristic result records**

The exact branch-and-bound solver is additionally executed for:

```text
8/4
10/4
12/5
```

for all three objectives, producing:

- **270 exact-reference records**
- **90 exact-reference instances per objective**

All exact-reference records used in the paper have status `OPTIMAL`.

---

## Output Files

The experiment command generates:

```text
final_heuristic_results.csv
final_exact_results.csv
```

The exact copies used for the reported paper analysis are preserved in:

```text
results/final_heuristic_results.csv
results/final_exact_results.csv
```

### `final_heuristic_results.csv`

The heuristic result file contains fields including:

- number of services,
- number of nodes,
- seed,
- placement objective,
- heuristic variant,
- feasibility status,
- selected objective value,
- monetary cost,
- failure exposure,
- physical energy,
- number of active nodes,
- construction runtime,
- refinement runtime,
- total runtime,
- exact-reference status,
- exact-reference value, and
- optimality gap when an exact optimum is available.

### `final_exact_results.csv`

The exact result file contains:

- deployment scale,
- seed,
- objective,
- solver status,
- exact objective value,
- exact runtime, and
- number of branch-and-bound nodes visited.

---

## Exact Reference Solver

The exact solver is a custom branch-and-bound implementation used to
provide proven reference solutions on tractable instances.

The solver:

1. starts from heuristic incumbents,
2. assigns services recursively,
3. removes branches that violate necessary capacity conditions,
4. computes a lower bound for the remaining assignments,
5. prunes a branch whenever its partial objective plus the lower bound
   cannot improve the incumbent, and
6. explores feasible node choices in increasing incremental-objective
   order.

The default exact-solver time limit is:

```text
30 seconds per objective
```

and can be changed using:

```bash
--exact-limit SEC
```

The paper makes optimality-gap claims only for instances for which the
exact solver reports `OPTIMAL`.

---

## Synthetic Instance Generation

The experiments use deterministic synthetic instances so that the same
seed reproduces the same placement problem.

Service demands and heterogeneous node characteristics are generated
from fixed parameter ranges.

To guarantee that each generated instance admits at least one
capacity-feasible assignment, node capacities are constructed around a
hidden longest-processing-time (LPT) anchor packing with randomized
capacity slack.

The anchor assignment is used only during instance generation to ensure
feasibility. It is **not provided to either the greedy heuristics or the
exact solver**.

For the reported experiment, the capacity-slack interval is:

```text
0.15 to 0.35
```

---

## Main Experimental Results

The experiments show that the preferred service ordering depends on the
placement objective.

For the fixed individual methods:

| Objective | Strongest fixed method | Mean exact-reference gap |
|---|---:|---:|
| Cost | MIN | 9.94% |
| Failure Exposure | MAX | 7.48% |
| Energy | MAX | 1.89% |

Retaining the best solution produced by SEQ, MIN, and MAX further reduces
the mean optimality gap:

| Objective | Mean gap | Median gap | Exact hits |
|---|---:|---:|---:|
| Cost | 7.16% | 3.50% | 16/90 |
| Failure Exposure | 6.95% | 6.53% | 3/90 |
| Energy | 0.81% | 0.00% | 66/90 |

These values correspond only to the three scales for which proven exact
optima are available. The paper does not make optimality-gap claims for
the larger configurations. :chatgpt-content-reference{index="3"} :chatgpt-content-reference{index="4"}

At the largest evaluated configuration of **80 services and 20 nodes**,
the mean combined runtime for executing SEQ, MIN, and MAX is:

| Objective | Combined runtime |
|---|---:|
| Cost | 34.85 ms |
| Failure Exposure | 2.02 ms |
| Energy | 8.49 ms |

These measurements characterize the tested implementation and hardware
environment and should be interpreted as comparative runtimes rather than
hardware-independent latency. :chatgpt-content-reference{index="5"}

---

## Result Summaries

Processed values used in the paper are provided under:

```text
results/summaries/
```

### `variant_gap_summary.csv`

Contains the exact-reference quality statistics for each individual
SEQ/MIN/MAX method.

### `best_of_three_summary.csv`

Contains exact-reference statistics after retaining the best result
among SEQ, MIN, and MAX for each instance and objective.

### `combined_runtime_by_scale.csv`

Contains the combined mean runtime of executing all three service-order
methods at each deployment scale.

---

## Reproducing the Figures

From the repository root, run:

```bash
python3 scripts/make_paper_figures.py
```

The script reads the summary CSV files and generates:

```text
figures/heuristic_quality.pdf
figures/heuristic_quality.png
figures/runtime_scaling.pdf
figures/runtime_scaling.png
```

`heuristic_quality` compares the exact-reference optimality gaps of SEQ,
MIN, and MAX.

`runtime_scaling` shows the combined SEQ+MIN+MAX runtime as deployment
size increases.

---

## Experimental Environment

The experiments were executed under Windows Subsystem for Linux (WSL).

The environment used for the reported runs was:

```text
CPU:            11th Gen Intel Core i5-1135G7 @ 2.40 GHz
WSL-visible CPU(s): 2
Memory:         approximately 3.8 GiB
Compiler:       g++ 13.3.0
Language:       C++17
Optimization:   -O3
```

Because runtime measurements depend on the execution environment, the
reported times are intended primarily for comparison among the evaluated
placement methods and deployment scales.

---

## Cleaning Generated Files

To remove the compiled binary and root-level generated experiment files:

```bash
make clean
```

---

## Command-Line Options

Available options include:

```text
--quick
    Run a small two-scale, two-seed test.

--seeds N
    Number of deterministic seeds per deployment scale.
    Default: 30.

--exact-limit SEC
    Time limit for exact branch-and-bound optimization per objective.
    Default: 30 seconds.

--slack-min X
    Minimum capacity slack.
    Default: 0.15.

--slack-max X
    Maximum capacity slack.
    Default: 0.35.

--prefix STR
    Prefix added to generated CSV output filenames.

--no-exact
    Skip exact branch-and-bound evaluation.
```

Help can also be displayed with:

```bash
./placement_heuristics --help
```

---

## Reproducibility Notes

The repository preserves the implementation and processed results
corresponding to the experiments reported in the paper.

The principal reproduction workflow is:

```bash
make

./placement_heuristics \
  --seeds 30 \
  --slack-min 0.15 \
  --slack-max 0.35 \
  --prefix final_

python3 scripts/make_paper_figures.py
```

The generated heuristic and exact CSV files can then be compared with the
reference copies under `results/`.

---

## Paper

**Service Placement for Cost, Failure Exposure, and Energy in the Fog–Edge Continuum**

4th International Workshop on Middleware for the Computing Continuum
(Mid4CC 2026), co-located with ACM/IFIP Middleware 2026.

This repository serves as the accompanying reproducibility package for
the manuscript.

---

## Citation

If this implementation or experimental package is useful in your work,
please cite the corresponding paper.

Final bibliographic information will be added after publication.

---

## Repository

https://github.com/sanaulcse123/cost-failure-energy-service-placement
