"""CSV export of archived traffic data.

CSV files are derived views intended for spreadsheets/dashboards. The SQLite
database remains the authoritative datastore; exporting never modifies it.
"""

from __future__ import annotations

import csv
import os
import tempfile
from pathlib import Path

from .db import TrafficDB
from .errors import VybTrafficError

CSV_FILES = ("summary.csv", "clone_daily.csv", "view_daily.csv",
             "referrers.csv", "popular_paths.csv")


def _write_csv(path: Path, rows: list[list], header: list[str]) -> None:
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            "w", newline="", encoding="utf-8", dir=path.parent,
            prefix=f".{path.name}.", delete=False,
        ) as fh:
            temporary = Path(fh.name)
            writer = csv.writer(fh)
            writer.writerow(header)
            writer.writerows(rows)
        os.replace(temporary, path)
    except OSError as exc:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
        raise VybTrafficError(f"could not write CSV {path}: {exc}") from exc


def _summary_rows(db: TrafficDB) -> tuple[list[str], list[list]]:
    latest = db.latest_snapshot()
    totals = db.archived_totals()
    span = db.snapshot_range()
    rows = []

    rows.append(("source", "SQLite (vyb-traffic)"))
    if latest:
        rows.append(("latest_snapshot_utc", latest["timestamp_utc"]))
    if span:
        rows.append(("first_recorded", span[0][:10]))
        rows.append(("last_recorded", span[1][:10]))

    rows.append(("current_14d_clones", latest["clone_count_14d"] if latest else ""))
    rows.append(("current_14d_unique_cloners", latest["clone_uniques_14d"] if latest else ""))
    rows.append(("current_14d_views", latest["view_count_14d"] if latest else ""))
    rows.append(("current_14d_unique_viewers", latest["view_uniques_14d"] if latest else ""))

    rows.append(("archived_clone_events", totals["clone_events"]))
    rows.append(("archived_view_events", totals["view_events"]))
    rows.append(("archived_sum_of_daily_clone_uniques_observation_only",
                 totals["clone_unique_observations"]))
    rows.append(("archived_sum_of_daily_view_uniques_observation_only",
                 totals["view_unique_observations"]))
    rows.append(("unique_cloners_note",
                 "GitHub does not expose stable identities; summed daily uniques are "
                 "not a true lifetime unique-user count."))
    return ["field", "value"], [list(r) for r in rows]


def export(db: TrafficDB, export_dir: Path) -> list[Path]:
    """Write all CSV files into ``export_dir`` and return their paths."""
    export_dir.mkdir(parents=True, exist_ok=True)
    written: list[Path] = []

    header, rows = _summary_rows(db)
    p = export_dir / "summary.csv"
    _write_csv(p, rows, header)
    written.append(p)

    for name, columns, fetcher in (
        ("clone_daily.csv", ["date", "count", "uniques", "first_seen", "last_seen"],
         lambda: [(r["date"], r["count"], r["uniques"], r["first_seen"], r["last_seen"])
                  for r in db.daily_clone_rows()]),
        ("view_daily.csv", ["date", "count", "uniques", "first_seen", "last_seen"],
         lambda: [(r["date"], r["count"], r["uniques"], r["first_seen"], r["last_seen"])
                  for r in db.daily_view_rows()]),
        ("referrers.csv", ["snapshot_utc", "referrer", "count", "uniques"],
         lambda: [(r["timestamp_utc"], r["referrer"], r["count"], r["uniques"])
                  for r in db.all_referrers()]),
        ("popular_paths.csv", ["snapshot_utc", "path", "title", "count", "uniques"],
         lambda: [(r["timestamp_utc"], r["path"], r["title"] or "", r["count"], r["uniques"])
                  for r in db.all_paths()]),
    ):
        q = export_dir / name
        _write_csv(q, list(fetcher()), columns)
        written.append(q)

    return written
