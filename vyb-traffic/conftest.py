"""Pytest configuration: make the package importable and share test fixtures."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent))

from vyb_traffic.db import TrafficDB  # noqa: E402


class FakeResponse:
    """Duck-typed stand-in for ``urllib`` response objects used by the API client."""

    def __init__(self, content: bytes, code: int = 200, headers=None):
        self._content = content
        self.status = code
        self.headers = headers or {}

    def read(self):
        return self._content

    def __enter__(self):
        return self

    def __exit__(self, *exc_info):
        return False


def payload_bytes(payload) -> bytes:
    return json.dumps(payload).encode("utf-8")


@pytest.fixture
def clone_payload():
    return {
        "count": 1129,
        "uniques": 82,
        "clones": [
            {"timestamp": "2026-08-22T00:00:00Z", "count": 55, "uniques": 3},
            {"timestamp": "2026-08-21T00:00:00Z", "count": 44, "uniques": 4},
        ],
    }


@pytest.fixture
def view_payload():
    return {
        "count": 5012,
        "uniques": 402,
        "views": [
            {"timestamp": "2026-08-22T00:00:00Z", "count": 300, "uniques": 22},
            {"timestamp": "2026-08-21T00:00:00Z", "count": 250, "uniques": 20},
        ],
    }


@pytest.fixture
def referrer_payload():
    return [
        {"referrer": "google.com", "count": 500, "uniques": 40},
        {"referrer": "github.com", "count": 120, "uniques": 15},
    ]


@pytest.fixture
def path_payload():
    return [
        {"path": "/", "title": "Vyb", "count": 800, "uniques": 60},
        {"path": "/README.md", "title": "README", "count": 300, "uniques": 25},
    ]


@pytest.fixture
def fake_response():
    def make(payload, code=200, headers=None, raw_bytes=None):
        return FakeResponse(
            raw_bytes if raw_bytes is not None else payload_bytes(payload),
            code=code, headers=headers,
        )
    return make


@pytest.fixture
def temp_db(tmp_path):
    """Yield a connected TrafficDB at a temp path, closed afterwards."""
    db = TrafficDB(tmp_path / "traffic_test.db").connect()
    yield db
    db.close()


def seed_db(db: TrafficDB, *, count=2) -> None:
    """Insert ``count`` snapshots of deterministic traffic data."""
    for i in range(count):
        day = f"2026-08-{22 - i:02d}"
        db.upsert_traffic(
            timestamp_utc=f"2026-08-{22 - i:02d}T00:00:00Z",
            clones={
                "count": 1000 + i, "uniques": 80 + i,
                "clones": [{"timestamp": f"{day}T00:00:00Z", "count": 100 + i,
                            "uniques": 10 + i}],
            },
            views={
                "count": 5000 + i, "uniques": 400 + i,
                "views": [{"timestamp": f"{day}T00:00:00Z", "count": 500 + i,
                           "uniques": 50 + i}],
            },
            referrers=[{"referrer": "google.com", "count": 50 + i, "uniques": 5 + i}],
            paths=[{"path": "/", "title": "Vyb", "count": 60 + i, "uniques": 6 + i}],
        )
