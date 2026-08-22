"""Render simple PNG graphs of archived traffic.

Graphs are produced only when explicitly requested (``vyb-traffic graph``) and
require ``matplotlib``. The daily event-count plots are the primary output;
unique observations are optional and always labelled as observations, never as
unique people.
"""

from __future__ import annotations

from pathlib import Path

from .db import TrafficDB
from .errors import VybTrafficError

GRAPH_FILES = ("daily-clones.png", "daily-views.png",
               "cumulative-clones.png", "cumulative-views.png")


def _pyplot():
    try:
        # Force a headless backend so generation works over SSH/cron without a display.
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        return plt
    except Exception as exc:  # pragma: no cover - depends on environment
        raise VybTrafficError(
            "matplotlib is required for 'vyb-traffic graph'. Install it with: "
            "pip install matplotlib"
        ) from exc


def _daily_series(db: TrafficDB, table_rows):
    dates = [r["date"] for r in table_rows]
    counts = [r["count"] for r in table_rows]
    uniques = [r["uniques"] for r in table_rows]
    return dates, counts, uniques


def _render_single(plt, title, filename, out_dir, dates, counts, uniques,
                   cumulative: bool, include_uniques: bool):
    x = list(range(len(dates)))
    if cumulative:
        cum = []
        acc = 0
        for v in counts:
            acc += v
            cum.append(acc)
        counts = cum

    fig, ax = plt.subplots(figsize=(10, 4), dpi=130)
    if cumulative:
        ax.plot(x, counts, color="#1294ff", linewidth=2, label="archived events")
        ax.fill_between(x, counts, color="#1294ff", alpha=0.15)
        ax.set_ylabel("cumulative event count")
    else:
        ax.bar(x, counts, color="#1294ff", alpha=0.85, label="events/day")
        ax.set_ylabel("daily event count")
    if include_uniques and not cumulative:
        ax.plot(x, uniques, color="#31dd83", marker=".", label="daily unique observations")
    ax.set_title(title)
    ax.set_xlabel("date")
    # Only label a handful of x positions to avoid clutter.
    step = max(1, len(dates) // 12)
    ticks = x[::step]
    labels = dates[::step]
    ax.set_xticks(ticks)
    ax.set_xticklabels(labels, rotation=45, ha="right", fontsize=8)
    ax.grid(axis="y", linestyle=":", alpha=0.4)
    if not dates:
        ax.text(0.5, 0.5, "No archived data yet", ha="center", va="center",
                transform=ax.transAxes)
    if include_uniques and not cumulative:
        ax.legend(fontsize=8)
    fig.tight_layout()
    path = out_dir / filename
    fig.savefig(path)
    plt.close(fig)
    return path


def render_graphs(db: TrafficDB, graphs_dir: Path, include_uniques: bool = True) -> list[Path]:
    """Render the four standard graphs and return the written paths."""
    plotting = _pyplot()
    graphs_dir.mkdir(parents=True, exist_ok=True)
    written: list[Path] = []

    clone_dates, clone_counts, clone_uniques = _daily_series(db, db.daily_clone_rows())
    view_dates, view_counts, view_uniques = _daily_series(db, db.daily_view_rows())

    written.append(_render_single(plotting, "Daily clones", "daily-clones.png",
                                  graphs_dir, clone_dates, clone_counts,
                                  clone_uniques, cumulative=False, include_uniques=include_uniques))
    written.append(_render_single(plotting, "Daily views", "daily-views.png",
                                  graphs_dir, view_dates, view_counts,
                                  view_uniques, cumulative=False, include_uniques=include_uniques))
    written.append(_render_single(plotting, "Cumulative archived clone events",
                                  "cumulative-clones.png", graphs_dir, clone_dates,
                                  clone_counts, clone_uniques, cumulative=True,
                                  include_uniques=False))
    written.append(_render_single(plotting, "Cumulative archived view events",
                                  "cumulative-views.png", graphs_dir, view_dates,
                                  view_counts, view_uniques, cumulative=True,
                                  include_uniques=False))

    return written
