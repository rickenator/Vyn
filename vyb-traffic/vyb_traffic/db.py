"""SQLite storage for durable GitHub traffic history."""

from __future__ import annotations

import sqlite3
from contextlib import contextmanager
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterator

from .errors import DBError

SCHEMA = """
CREATE TABLE IF NOT EXISTS snapshots (
    id                 INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp_utc      TEXT NOT NULL UNIQUE,
    clone_count_14d    INTEGER,
    clone_uniques_14d  INTEGER,
    view_count_14d     INTEGER,
    view_uniques_14d   INTEGER,
    clones_ok          INTEGER NOT NULL DEFAULT 0 CHECK (clones_ok IN (0, 1)),
    views_ok           INTEGER NOT NULL DEFAULT 0 CHECK (views_ok IN (0, 1)),
    referrers_ok       INTEGER NOT NULL DEFAULT 0 CHECK (referrers_ok IN (0, 1)),
    paths_ok           INTEGER NOT NULL DEFAULT 0 CHECK (paths_ok IN (0, 1))
);

CREATE TABLE IF NOT EXISTS clone_daily (
    date       TEXT PRIMARY KEY,
    count      INTEGER NOT NULL CHECK (count >= 0),
    uniques    INTEGER NOT NULL CHECK (uniques >= 0),
    first_seen TEXT NOT NULL,
    last_seen  TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS view_daily (
    date       TEXT PRIMARY KEY,
    count      INTEGER NOT NULL CHECK (count >= 0),
    uniques    INTEGER NOT NULL CHECK (uniques >= 0),
    first_seen TEXT NOT NULL,
    last_seen  TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS referrers (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    snapshot_id   INTEGER NOT NULL REFERENCES snapshots(id) ON DELETE CASCADE,
    timestamp_utc TEXT NOT NULL,
    referrer      TEXT NOT NULL,
    count         INTEGER NOT NULL CHECK (count >= 0),
    uniques       INTEGER NOT NULL CHECK (uniques >= 0),
    UNIQUE (snapshot_id, referrer)
);
CREATE INDEX IF NOT EXISTS idx_referrers_timestamp ON referrers(timestamp_utc);

CREATE TABLE IF NOT EXISTS popular_paths (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    snapshot_id   INTEGER NOT NULL REFERENCES snapshots(id) ON DELETE CASCADE,
    timestamp_utc TEXT NOT NULL,
    path          TEXT NOT NULL,
    title         TEXT,
    count         INTEGER NOT NULL CHECK (count >= 0),
    uniques       INTEGER NOT NULL CHECK (uniques >= 0),
    UNIQUE (snapshot_id, path)
);
CREATE INDEX IF NOT EXISTS idx_paths_timestamp ON popular_paths(timestamp_utc);
"""


def utc_now_iso() -> str:
    return (
        datetime.now(timezone.utc)
        .isoformat(timespec="microseconds")
        .replace("+00:00", "Z")
    )


def _bucket_date(bucket: dict[str, Any]) -> str:
    return str(bucket["timestamp"])[:10]


class TrafficDB:
    """Own a SQLite connection and expose traffic-specific operations."""

    def __init__(self, db_path: str | Path):
        self.db_path = Path(db_path).expanduser()
        self._conn: sqlite3.Connection | None = None

    @property
    def connection(self) -> sqlite3.Connection:
        if self._conn is None:
            raise DBError("database is not connected")
        return self._conn

    def connect(self) -> "TrafficDB":
        try:
            self.db_path.parent.mkdir(parents=True, exist_ok=True)
            self._conn = sqlite3.connect(str(self.db_path), timeout=30.0)
            self._conn.row_factory = sqlite3.Row
            self._conn.execute("PRAGMA foreign_keys=ON")
            self._conn.execute("PRAGMA journal_mode=WAL")
            self._conn.execute("PRAGMA synchronous=NORMAL")
            self._conn.executescript(SCHEMA)
        except (OSError, sqlite3.Error) as exc:
            self.close()
            raise DBError(f"failed to initialize database {self.db_path}: {exc}") from exc
        return self

    def close(self) -> None:
        if self._conn is not None:
            self._conn.close()
            self._conn = None

    def __enter__(self) -> "TrafficDB":
        return self.connect()

    def __exit__(self, *_exc_info) -> None:
        self.close()

    @contextmanager
    def transaction(self) -> Iterator[sqlite3.Connection]:
        conn = self.connection
        try:
            conn.execute("BEGIN IMMEDIATE")
            yield conn
            conn.commit()
        except sqlite3.Error as exc:
            conn.rollback()
            raise DBError(f"SQLite transaction failed: {exc}") from exc
        except Exception:
            conn.rollback()
            raise

    def upsert_traffic(
        self,
        *,
        timestamp_utc: str,
        clones: dict[str, Any] | None = None,
        views: dict[str, Any] | None = None,
        referrers: list[dict[str, Any]] | None = None,
        paths: list[dict[str, Any]] | None = None,
    ) -> int:
        """Atomically persist every successful endpoint from one collection run.

        Missing endpoints are represented by ``NULL`` rolling values and a false
        status flag, never by a misleading zero. On an idempotent re-run with the
        same timestamp, a failed endpoint cannot erase data that was already
        stored successfully.
        """
        with self.transaction() as conn:
            conn.execute(
                """
                INSERT INTO snapshots (
                    timestamp_utc, clone_count_14d, clone_uniques_14d,
                    view_count_14d, view_uniques_14d,
                    clones_ok, views_ok, referrers_ok, paths_ok
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                ON CONFLICT(timestamp_utc) DO UPDATE SET
                    clone_count_14d = COALESCE(excluded.clone_count_14d, snapshots.clone_count_14d),
                    clone_uniques_14d = COALESCE(excluded.clone_uniques_14d, snapshots.clone_uniques_14d),
                    view_count_14d = COALESCE(excluded.view_count_14d, snapshots.view_count_14d),
                    view_uniques_14d = COALESCE(excluded.view_uniques_14d, snapshots.view_uniques_14d),
                    clones_ok = MAX(snapshots.clones_ok, excluded.clones_ok),
                    views_ok = MAX(snapshots.views_ok, excluded.views_ok),
                    referrers_ok = MAX(snapshots.referrers_ok, excluded.referrers_ok),
                    paths_ok = MAX(snapshots.paths_ok, excluded.paths_ok)
                """,
                (
                    timestamp_utc,
                    clones["count"] if clones is not None else None,
                    clones["uniques"] if clones is not None else None,
                    views["count"] if views is not None else None,
                    views["uniques"] if views is not None else None,
                    int(clones is not None),
                    int(views is not None),
                    int(referrers is not None),
                    int(paths is not None),
                ),
            )
            snapshot_id = int(
                conn.execute(
                    "SELECT id FROM snapshots WHERE timestamp_utc = ?", (timestamp_utc,)
                ).fetchone()["id"]
            )

            if clones is not None:
                for bucket in clones["clones"]:
                    self._upsert_daily(
                        conn, "clone_daily", _bucket_date(bucket), bucket["count"],
                        bucket["uniques"], timestamp_utc,
                    )
            if views is not None:
                for bucket in views["views"]:
                    self._upsert_daily(
                        conn, "view_daily", _bucket_date(bucket), bucket["count"],
                        bucket["uniques"], timestamp_utc,
                    )
            if referrers is not None:
                self._replace_referrers(conn, snapshot_id, timestamp_utc, referrers)
            if paths is not None:
                self._replace_paths(conn, snapshot_id, timestamp_utc, paths)
        return snapshot_id

    @staticmethod
    def _upsert_daily(conn, table, date, count, uniques, timestamp_utc) -> None:
        if table not in {"clone_daily", "view_daily"}:
            raise ValueError("invalid daily table")
        conn.execute(
            f"""
            INSERT INTO {table} (date, count, uniques, first_seen, last_seen)
            VALUES (?, ?, ?, ?, ?)
            ON CONFLICT(date) DO UPDATE SET
                count = excluded.count,
                uniques = excluded.uniques,
                last_seen = excluded.last_seen
            """,
            (date, count, uniques, timestamp_utc, timestamp_utc),
        )

    @staticmethod
    def _replace_referrers(conn, snapshot_id, timestamp_utc, rows) -> None:
        conn.execute("DELETE FROM referrers WHERE snapshot_id = ?", (snapshot_id,))
        conn.executemany(
            """
            INSERT INTO referrers
                (snapshot_id, timestamp_utc, referrer, count, uniques)
            VALUES (?, ?, ?, ?, ?)
            """,
            [
                (snapshot_id, timestamp_utc, row["referrer"], row["count"], row["uniques"])
                for row in rows
            ],
        )

    @staticmethod
    def _replace_paths(conn, snapshot_id, timestamp_utc, rows) -> None:
        conn.execute("DELETE FROM popular_paths WHERE snapshot_id = ?", (snapshot_id,))
        conn.executemany(
            """
            INSERT INTO popular_paths
                (snapshot_id, timestamp_utc, path, title, count, uniques)
            VALUES (?, ?, ?, ?, ?, ?)
            """,
            [
                (
                    snapshot_id, timestamp_utc, row["path"], row.get("title"),
                    row["count"], row["uniques"],
                )
                for row in rows
            ],
        )

    def _query_one(self, sql: str, params=()) -> dict[str, Any] | None:
        try:
            row = self.connection.execute(sql, params).fetchone()
        except sqlite3.Error as exc:
            raise DBError(f"SQLite query failed: {exc}") from exc
        return dict(row) if row is not None else None

    def _query_all(self, sql: str, params=()) -> list[dict[str, Any]]:
        try:
            return [dict(row) for row in self.connection.execute(sql, params).fetchall()]
        except sqlite3.Error as exc:
            raise DBError(f"SQLite query failed: {exc}") from exc

    def latest_snapshot(self) -> dict[str, Any] | None:
        return self._query_one("SELECT * FROM snapshots ORDER BY timestamp_utc DESC LIMIT 1")

    def snapshot_range(self) -> tuple[str, str] | None:
        row = self._query_one(
            "SELECT MIN(timestamp_utc) AS first_ts, MAX(timestamp_utc) AS last_ts FROM snapshots"
        )
        if row is None or row["first_ts"] is None:
            return None
        return row["first_ts"], row["last_ts"]

    def snapshot_count(self) -> int:
        return int(self._query_one("SELECT COUNT(*) AS n FROM snapshots")["n"])

    def archived_totals(self) -> dict[str, int]:
        def total(table: str, column: str) -> int:
            row = self._query_one(f"SELECT COALESCE(SUM({column}), 0) AS n FROM {table}")
            return int(row["n"])

        return {
            "clone_events": total("clone_daily", "count"),
            "view_events": total("view_daily", "count"),
            "clone_unique_observations": total("clone_daily", "uniques"),
            "view_unique_observations": total("view_daily", "uniques"),
        }

    def daily_clone_rows(self) -> list[dict[str, Any]]:
        return self._query_all(
            "SELECT date, count, uniques, first_seen, last_seen FROM clone_daily ORDER BY date"
        )

    def daily_view_rows(self) -> list[dict[str, Any]]:
        return self._query_all(
            "SELECT date, count, uniques, first_seen, last_seen FROM view_daily ORDER BY date"
        )

    def all_referrers(self) -> list[dict[str, Any]]:
        return self._query_all(
            "SELECT timestamp_utc, referrer, count, uniques FROM referrers "
            "ORDER BY timestamp_utc, count DESC, referrer"
        )

    def all_paths(self) -> list[dict[str, Any]]:
        return self._query_all(
            "SELECT timestamp_utc, path, title, count, uniques FROM popular_paths "
            "ORDER BY timestamp_utc, count DESC, path"
        )
