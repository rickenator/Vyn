"""Single collection run: fetch all traffic endpoints and persist them.

Each endpoint is fetched independently so a transient failure of one endpoint
does not lose the data GitHub returned for the others. Whatever succeeds is
written in one atomic transaction (see :meth:`TrafficDB.upsert_traffic`), so a
partial failure never corrupts previously stored history.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Callable

from .api import GitHubClient
from .config import Config
from .db import TrafficDB, utc_now_iso
from .errors import ConfigError, VybTrafficError


@dataclass
class CollectOutcome:
    """Summary of one collection run."""

    snapshot_id: int
    timestamp_utc: str
    stored: set[str] = field(default_factory=set)
    failures: list[tuple[str, str]] = field(default_factory=list)

    @property
    def partial(self) -> bool:
        return bool(self.failures)

    @property
    def ok(self) -> bool:
        return not self.failures


def _safe_fetch(
    name: str,
    fn: Callable[[], Any],
    outcome: CollectOutcome,
    errors_log: list[tuple[str, str]],
):
    """Fetch one endpoint; record either its data key or an error message."""
    try:
        payload = fn()
    except VybTrafficError as exc:
        errors_log.append((name, str(exc)))
        return None
    outcome.stored.add(name)
    return payload


def collect(
    config: Config,
    *,
    client: GitHubClient | None = None,
    timestamp_utc: str | None = None,
) -> CollectOutcome:
    """Run a full collection against GitHub and store the results.

    ``config`` must already include a token (call ``require_token`` upstream or
    rely on this function raising a clear error when it is missing).

    Returns a :class:`CollectOutcome`. Raises :class:`DBError` if storage fails.
    """
    token = config.token
    if not token:
        raise ConfigError("GITHUB_TOKEN is not set; cannot collect traffic data.")

    client = client or GitHubClient(
        config.token, config.owner, config.repo, config.timeout_seconds
    )
    timestamp_utc = timestamp_utc or utc_now_iso()
    errors_log: list[tuple[str, str]] = []
    outcome = CollectOutcome(snapshot_id=-1, timestamp_utc=timestamp_utc)

    clones = _safe_fetch("clones", client.get_clones, outcome, errors_log)
    views = _safe_fetch("views", client.get_views, outcome, errors_log)
    referrers = _safe_fetch("referrers", client.get_referrers, outcome, errors_log)
    paths = _safe_fetch("paths", client.get_paths, outcome, errors_log)

    outcome.failures = errors_log

    with TrafficDB(config.db_path) as db:
        outcome.snapshot_id = db.upsert_traffic(
            timestamp_utc=timestamp_utc,
            clones=clones,
            views=views,
            referrers=referrers,
            paths=paths,
        )

    return outcome
