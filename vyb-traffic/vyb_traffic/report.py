"""Human-readable report of current and archived GitHub traffic.

The report clearly separates two kinds of numbers:

* rolling 14-day values currently reported by GitHub (snapshot), and
* archived daily event counts summed over every stored date.

Daily *uniques* are reported only as "unique-cloner observations" with an
explicit note that the same person can appear on multiple days. They are never
labelled as lifetime unique users, because GitHub does not expose stable
anonymous identities between days.
"""

from __future__ import annotations

from .db import TrafficDB


def _fmt(n: int | None) -> str:
    return "-" if n is None else f"{n:,}"


def render_report(
    db: TrafficDB, owner: str, repo: str, recent_days: int = 14, include_uniques: bool = True
) -> str:
    """Return the full text report for the archived database."""
    title = "Vyb GitHub Traffic" if (owner == "rickenator" and repo == "Vyb") else \
        f"{owner}/{repo} GitHub Traffic"
    lines = [title, "=" * len(title)]

    latest = db.latest_snapshot()
    snap_range = db.snapshot_range()
    totals = db.archived_totals()
    clone_rows = db.daily_clone_rows()
    view_rows = db.daily_view_rows()

    # --- current GitHub 14-day window ---
    lines.append("")
    lines.append("Current GitHub 14-day window")
    if latest is None:
        lines.append("  (no collection has been stored yet)")
    else:
        lines.append(f"  Clones:          {_fmt(latest['clone_count_14d'])}")
        lines.append(f"  Unique cloners:  {_fmt(latest['clone_uniques_14d'])}")
        lines.append(f"  Views:           {_fmt(latest['view_count_14d'])}")
        lines.append(f"  Unique viewers:  {_fmt(latest['view_uniques_14d'])}")

    # --- archived history ---
    lines.append("")
    lines.append("Archived history")
    if snap_range is None:
        lines.append("  (empty)")
    else:
        first_date = snap_range[0][:10]
        last_date = snap_range[1][:10]
        lines.append(f"  First recorded:  {first_date}")
        lines.append(f"  Last recorded:   {last_date}")
        lines.append(f"  Archived clone events:  {_fmt(totals['clone_events'])}")
        lines.append(f"  Archived view events:   {_fmt(totals['view_events'])}")

    # --- recent daily clones ---
    lines.append("")
    lines.append("Recent daily clones")
    if not clone_rows:
        lines.append("  (none yet)")
    else:
        for row in reversed(clone_rows[-recent_days:]):
            lines.append(f"  {row['date']}   {_fmt(row['count'])}")

    lines.append("")
    lines.append("Recent daily views")
    if not view_rows:
        lines.append("  (none yet)")
    else:
        for row in reversed(view_rows[-recent_days:]):
            lines.append(f"  {row['date']}   {_fmt(row['count'])}")

    # --- daily unique-cloner observations (never lifetime uniques) ---
    if include_uniques:
        lines.append("")
        lines.append("Daily unique observations (archived sums, not unique people)")
        lines.append("  Note: the same person may appear on more than one day, so these")
        lines.append("  observations cannot be summed into a true lifetime unique-user count.")
        if clone_rows:
            lines.append(f"  Sum of daily clone uniques:  {_fmt(totals['clone_unique_observations'])}")
        if view_rows:
            lines.append(f"  Sum of daily view uniques:   {_fmt(totals['view_unique_observations'])}")

    return "\n".join(lines)
