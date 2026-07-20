"""
plot_results.py -- Visualise CPU Cache Simulator benchmark results
Reads results/results.csv and produces two publication-quality plots.

Usage:
    python plot_results.py
    python plot_results.py results/results.csv   # custom path

Output:
    results/policy_comparison.png    -- grouped bar chart (policy vs workload)
    results/cache_size_sensitivity.png -- line chart (size sweep, LRU)
"""

import sys
import os
import csv
import matplotlib
matplotlib.use("Agg")          # non-interactive backend (no display needed)
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker

# ── paths ────────────────────────────────────────────────────────────────────
CSV_PATH  = sys.argv[1] if len(sys.argv) > 1 else "results/results.csv"
OUT_DIR   = "results"
os.makedirs(OUT_DIR, exist_ok=True)

# ── parse both sections from results.csv ─────────────────────────────────────
policy_rows = []   # [{policy, sequential_hit_rate, random_hit_rate}, ...]
size_rows   = []   # [{size_kb, sequential_hit_rate, random_hit_rate}, ...]

current_header = None

with open(CSV_PATH, newline="") as f:
    for line in f:
        line = line.strip()
        if not line or line.startswith("#"):   # skip blanks and comments
            continue
        if line.startswith("policy,"):         # Section 1 header
            current_header = "policy"
            continue
        if line.startswith("size_kb,"):        # Section 2 header
            current_header = "size"
            continue

        parts = line.split(",")
        if current_header == "policy" and len(parts) == 3:
            policy_rows.append({
                "policy":            parts[0].strip(),
                "sequential":        float(parts[1]),
                "random":            float(parts[2]),
            })
        elif current_header == "size" and len(parts) == 3:
            size_rows.append({
                "size_kb":   int(parts[0]),
                "sequential": float(parts[1]),
                "random":     float(parts[2]),
            })

if not policy_rows:
    raise SystemExit(f"ERROR: No policy data found in {CSV_PATH}")
if not size_rows:
    raise SystemExit(f"ERROR: No size-sweep data found in {CSV_PATH}")

# ── shared style ─────────────────────────────────────────────────────────────
plt.rcParams.update({
    "font.family":      "DejaVu Sans",
    "font.size":        11,
    "axes.titlesize":   13,
    "axes.titleweight": "bold",
    "axes.spines.top":  False,
    "axes.spines.right":False,
    "figure.dpi":       150,
})

SEQ_COLOR = "#2196F3"   # blue
RND_COLOR = "#FF5722"   # deep orange

# ── Plot 1: Grouped bar chart — policy vs workload ───────────────────────────
policies   = [r["policy"]    for r in policy_rows]
seq_rates  = [r["sequential"] for r in policy_rows]
rnd_rates  = [r["random"]    for r in policy_rows]

x     = range(len(policies))
width = 0.35

fig1, ax1 = plt.subplots(figsize=(8, 5))
fig1.patch.set_facecolor("#FAFAFA")
ax1.set_facecolor("#FAFAFA")

bars_seq = ax1.bar([i - width/2 for i in x], seq_rates,
                   width, label="Sequential", color=SEQ_COLOR,
                   edgecolor="white", linewidth=0.8, zorder=3)
bars_rnd = ax1.bar([i + width/2 for i in x], rnd_rates,
                   width, label="Random",     color=RND_COLOR,
                   edgecolor="white", linewidth=0.8, zorder=3)

# value labels on top of each bar
for bar in bars_seq:
    h = bar.get_height()
    ax1.text(bar.get_x() + bar.get_width() / 2, h + 0.8,
             f"{h:.1f}%", ha="center", va="bottom",
             fontsize=9, color="#1565C0", fontweight="bold")

for bar in bars_rnd:
    h = bar.get_height()
    ax1.text(bar.get_x() + bar.get_width() / 2, h + 0.8,
             f"{h:.1f}%", ha="center", va="bottom",
             fontsize=9, color="#BF360C", fontweight="bold")

ax1.set_xticks(list(x))
ax1.set_xticklabels(policies, fontsize=12)
ax1.set_ylim(0, 115)
ax1.yaxis.set_major_formatter(mticker.PercentFormatter(xmax=100, decimals=0))
ax1.set_xlabel("Replacement Policy", labelpad=8)
ax1.set_ylabel("Hit Rate (%)", labelpad=8)
ax1.set_title("Cache Hit Rate by Policy and Workload Type")
ax1.legend(loc="upper right", framealpha=0.85)
ax1.grid(axis="y", linestyle="--", alpha=0.5, zorder=0)

fig1.tight_layout()
out1 = os.path.join(OUT_DIR, "policy_comparison.png")
fig1.savefig(out1, bbox_inches="tight")
plt.close(fig1)

# ── Plot 2: Line chart — cache size sweep (LRU) ───────────────────────────────
sizes      = [r["size_kb"]   for r in size_rows]
seq_sweep  = [r["sequential"] for r in size_rows]
rnd_sweep  = [r["random"]    for r in size_rows]

x_labels = [f"{s}KB" for s in sizes]

fig2, ax2 = plt.subplots(figsize=(8, 5))
fig2.patch.set_facecolor("#FAFAFA")
ax2.set_facecolor("#FAFAFA")

ax2.plot(x_labels, seq_sweep, marker="o", markersize=8,
         color=SEQ_COLOR, linewidth=2.2, label="Sequential", zorder=3)
ax2.plot(x_labels, rnd_sweep, marker="s", markersize=8,
         color=RND_COLOR, linewidth=2.2, label="Random",     zorder=3)

# annotate each data point
for xi, (sq, rn) in enumerate(zip(seq_sweep, rnd_sweep)):
    ax2.annotate(f"{sq:.1f}%", (xi, sq),
                 textcoords="offset points", xytext=(0, 9),
                 ha="center", fontsize=8.5, color="#1565C0", fontweight="bold")
    ax2.annotate(f"{rn:.1f}%", (xi, rn),
                 textcoords="offset points", xytext=(0, -15),
                 ha="center", fontsize=8.5, color="#BF360C", fontweight="bold")

ax2.set_ylim(0, 110)
ax2.yaxis.set_major_formatter(mticker.PercentFormatter(xmax=100, decimals=0))
ax2.set_xlabel("Cache Size", labelpad=8)
ax2.set_ylabel("Hit Rate (%)", labelpad=8)
ax2.set_title("LRU Hit Rate vs Cache Size")
ax2.legend(loc="center right", framealpha=0.85)
ax2.grid(linestyle="--", alpha=0.5, zorder=0)

fig2.tight_layout()
out2 = os.path.join(OUT_DIR, "cache_size_sensitivity.png")
fig2.savefig(out2, bbox_inches="tight")
plt.close(fig2)

print(f"Plots saved to {OUT_DIR}/")
print(f"  {out1}")
print(f"  {out2}")
