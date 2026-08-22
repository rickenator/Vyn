"""SQLite schema, idempotence, and accounting tests."""

from __future__ import annotations


def _traffic(day, clone_count=10, view_count=20):
    return (
        {
            "count": 100,
            "uniques": 8,
            "clones": [{"timestamp": f"{day}T00:00:00Z", "count": clone_count, "uniques": 3}],
        },
        {
            "count": 200,
            "uniques": 18,
            "views": [{"timestamp": f"{day}T00:00:00Z", "count": view_count, "uniques": 7}],
        },
    )


def test_database_initialization(temp_db):
    names = {
        row["name"]
        for row in temp_db.connection.execute(
            "SELECT name FROM sqlite_master WHERE type = 'table'"
        )
    }
    assert {"snapshots", "clone_daily", "view_daily", "referrers", "popular_paths"} <= names


def test_daily_bucket_upsert_updates_values_and_preserves_first_seen(temp_db):
    clones, views = _traffic("2026-08-22", clone_count=10)
    temp_db.upsert_traffic(
        timestamp_utc="2026-08-22T03:15:00Z", clones=clones, views=views
    )
    clones, views = _traffic("2026-08-22", clone_count=13)
    temp_db.upsert_traffic(
        timestamp_utc="2026-08-23T03:15:00Z", clones=clones, views=views
    )
    row = temp_db.daily_clone_rows()[0]
    assert row["count"] == 13
    assert row["first_seen"] == "2026-08-22T03:15:00Z"
    assert row["last_seen"] == "2026-08-23T03:15:00Z"


def test_duplicate_collection_timestamp_is_idempotent(temp_db):
    clones, views = _traffic("2026-08-22")
    kwargs = dict(
        timestamp_utc="2026-08-22T03:15:00Z",
        clones=clones,
        views=views,
        referrers=[{"referrer": "google.com", "count": 4, "uniques": 2}],
        paths=[{"path": "/", "title": "Vyb", "count": 8, "uniques": 3}],
    )
    first_id = temp_db.upsert_traffic(**kwargs)
    second_id = temp_db.upsert_traffic(**kwargs)
    assert first_id == second_id
    assert temp_db.snapshot_count() == 1
    assert len(temp_db.all_referrers()) == 1
    assert len(temp_db.all_paths()) == 1
    assert len(temp_db.daily_clone_rows()) == 1


def test_partial_rerun_does_not_erase_successful_endpoint_data(temp_db):
    clones, views = _traffic("2026-08-22")
    timestamp = "2026-08-22T03:15:00Z"
    temp_db.upsert_traffic(
        timestamp_utc=timestamp,
        clones=clones,
        views=views,
        referrers=[{"referrer": "google.com", "count": 4, "uniques": 2}],
        paths=[],
    )
    temp_db.upsert_traffic(timestamp_utc=timestamp, clones=None, views=None)
    snapshot = temp_db.latest_snapshot()
    assert snapshot["clone_count_14d"] == 100
    assert snapshot["view_count_14d"] == 200
    assert snapshot["clones_ok"] == 1
    assert len(temp_db.all_referrers()) == 1


def test_cumulative_totals_sum_unique_dates_once(temp_db):
    for day, clones_count, views_count in (
        ("2026-08-21", 4, 10),
        ("2026-08-22", 6, 20),
    ):
        clones, views = _traffic(day, clones_count, views_count)
        temp_db.upsert_traffic(
            timestamp_utc=f"{day}T03:15:00Z", clones=clones, views=views
        )
    totals = temp_db.archived_totals()
    assert totals["clone_events"] == 10
    assert totals["view_events"] == 30
    assert totals["clone_unique_observations"] == 6
    assert totals["view_unique_observations"] == 14


def test_partial_snapshot_uses_null_not_zero(temp_db):
    temp_db.upsert_traffic(timestamp_utc="2026-08-22T03:15:00Z")
    snapshot = temp_db.latest_snapshot()
    assert snapshot["clone_count_14d"] is None
    assert snapshot["view_count_14d"] is None
    assert snapshot["clones_ok"] == 0
