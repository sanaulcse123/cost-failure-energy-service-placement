import csv
import math
from collections import defaultdict
import matplotlib.pyplot as plt


# ============================================================
# Figure 1: Exact-reference quality of SEQ / MIN / MAX
# ============================================================

gap = {}
ci = {}

with open("results/summaries/variant_gap_summary.csv", newline="") as f:
    r = csv.DictReader(f)
    for row in r:
        key = (row["objective"], row["variant"])
        gap[key] = float(row["mean_gap_pct"])
        ci[key] = float(row["ci95_halfwidth_pct"])

objectives = ["COST", "RELIABILITY", "ENERGY"]
variants = ["SEQ", "MIN", "MAX"]

x = range(len(objectives))
width = 0.24

fig, ax = plt.subplots(figsize=(7.2, 4.2))

for k, variant in enumerate(variants):
    offset = [(v + (k - 1) * width) for v in x]
    values = [gap[(obj, variant)] for obj in objectives]
    errors = [ci[(obj, variant)] for obj in objectives]

    ax.bar(
        offset,
        values,
        width=width,
        yerr=errors,
        capsize=3,
        label=variant
    )

ax.set_xticks(list(x))
ax.set_xticklabels(["Cost", "Failure Exposure", "Energy"])
ax.set_ylabel("Mean optimality gap (%)")
ax.set_xlabel("Placement objective")
ax.legend(title="Greedy heuristic")
ax.grid(axis="y", alpha=0.25)

fig.tight_layout()
fig.savefig("figures/heuristic_quality.pdf", bbox_inches="tight")
fig.savefig("figures/heuristic_quality.png", dpi=300, bbox_inches="tight")
plt.close(fig)


# ============================================================
# Figure 2: Combined SEQ+MIN+MAX runtime across scales
# ============================================================

runtime = defaultdict(list)

with open("results/summaries/combined_runtime_by_scale.csv", newline="") as f:
    r = csv.DictReader(f)
    for row in r:
        obj = row["objective"]
        m = int(row["m"])
        p = int(row["p"])
        value = float(row["mean_runtime_ms"])
        runtime[obj].append((m, p, value))

fig, ax = plt.subplots(figsize=(7.2, 4.2))

display_name = {
    "COST": "Cost",
    "RELIABILITY": "Failure Exposure",
    "ENERGY": "Energy",
}

for obj in objectives:
    data = sorted(runtime[obj])
    labels = [f"{m}/{p}" for m, p, _ in data]
    values = [v for _, _, v in data]

    ax.plot(
        labels,
        values,
        marker="o",
        linewidth=1.8,
        label=display_name[obj]
    )

ax.set_yscale("log")
ax.set_xlabel("Deployment scale (services/nodes)")
ax.set_ylabel("Combined heuristic runtime (ms)")
ax.legend()
ax.grid(axis="y", alpha=0.25)

fig.tight_layout()
fig.savefig("figures/runtime_scaling.pdf", bbox_inches="tight")
fig.savefig("figures/runtime_scaling.png", dpi=300, bbox_inches="tight")
plt.close(fig)

print("Created:")
print(" figures/heuristic_quality.pdf")
print(" figures/heuristic_quality.png")
print(" figures/runtime_scaling.pdf")
print(" figures/runtime_scaling.png")
