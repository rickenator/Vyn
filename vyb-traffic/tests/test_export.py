"""CSV export tests."""

from __future__ import annotations

import csv

from conftest import seed_db
from vyb_traffic.export import CSV_FILES, export


def test_csv_export_writes_all_expected_files(temp_db, tmp_path):
    seed_db(temp_db)
    export_dir = tmp_path / "export"
    written = export(temp_db, export_dir)
    assert {path.name for path in written} == set(CSV_FILES)
    assert all(path.exists() for path in written)

    with (export_dir / "clone_daily.csv").open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    assert len(rows) == 2
    assert rows[0]["date"] == "2026-08-21"

    with (export_dir / "summary.csv").open(newline="", encoding="utf-8") as handle:
        summary = {row["field"]: row["value"] for row in csv.DictReader(handle)}
    assert summary["archived_clone_events"] == "201"
    assert "not a true lifetime" in summary["unique_cloners_note"]
