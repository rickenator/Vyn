"""Collection orchestration tests with a fully mocked client."""

from __future__ import annotations

from vyb_traffic.collector import collect
from vyb_traffic.config import Config
from vyb_traffic.db import TrafficDB
from vyb_traffic.errors import TransportError


class PartialClient:
    def __init__(self, clone_payload, referrer_payload, path_payload):
        self.clone_payload = clone_payload
        self.referrer_payload = referrer_payload
        self.path_payload = path_payload

    def get_clones(self):
        return self.clone_payload

    def get_views(self):
        raise TransportError("timed out")

    def get_referrers(self):
        return self.referrer_payload

    def get_paths(self):
        return self.path_payload


def test_partial_api_failure_stores_successful_endpoints(
    tmp_path, clone_payload, referrer_payload, path_payload
):
    db_path = tmp_path / "traffic.db"
    config = Config(token="secret", db_path=db_path)
    outcome = collect(
        config,
        client=PartialClient(clone_payload, referrer_payload, path_payload),
        timestamp_utc="2026-08-22T10:15:00Z",
    )
    assert outcome.partial
    assert outcome.stored == {"clones", "referrers", "paths"}
    assert outcome.failures == [("views", "timed out")]

    with TrafficDB(db_path) as db:
        snapshot = db.latest_snapshot()
        assert snapshot["clone_count_14d"] == 1129
        assert snapshot["view_count_14d"] is None
        assert db.archived_totals()["clone_events"] == 99
        assert db.all_referrers()[0]["referrer"] == "google.com"


def test_duplicate_collect_run_is_idempotent(
    tmp_path, clone_payload, view_payload, referrer_payload, path_payload
):
    class Client:
        get_clones = lambda self: clone_payload
        get_views = lambda self: view_payload
        get_referrers = lambda self: referrer_payload
        get_paths = lambda self: path_payload

    config = Config(token="secret", db_path=tmp_path / "traffic.db")
    for _ in range(2):
        collect(config, client=Client(), timestamp_utc="2026-08-22T10:15:00Z")
    with TrafficDB(config.db_path) as db:
        assert db.snapshot_count() == 1
        assert len(db.daily_clone_rows()) == 2
