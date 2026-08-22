"""Runtime configuration, driven primarily by environment variables.

All settings have sensible defaults so the utility works with only ``GITHUB_TOKEN``
exported. Paths default under ``~/.local/share/vyb-traffic`` per the XDG base
directory convention.
"""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path

from .errors import ConfigError

DEFAULT_OWNER = "rickenator"
DEFAULT_REPO = "Vyb"

ENV_TOKEN = "GITHUB_TOKEN"
ENV_OWNER = "VYB_TRAFFIC_OWNER"
ENV_REPO = "VYB_TRAFFIC_REPO"
ENV_DB = "VYB_TRAFFIC_DB"
ENV_EXPORT_DIR = "VYB_TRAFFIC_EXPORT_DIR"
ENV_GRAPHS_DIR = "VYB_TRAFFIC_GRAPHS_DIR"


def _data_root() -> Path:
    """Return the configured per-user data directory without side effects."""
    base = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share"))
    return base / "vyb-traffic"


def default_db_path() -> Path:
    override = os.environ.get(ENV_DB)
    if override:
        return Path(override).expanduser()
    return _data_root() / "traffic.db"


def default_export_dir() -> Path:
    override = os.environ.get(ENV_EXPORT_DIR)
    if override:
        return Path(override).expanduser()
    return _data_root() / "export"


def default_graphs_dir() -> Path:
    override = os.environ.get(ENV_GRAPHS_DIR)
    if override:
        return Path(override).expanduser()
    return _data_root() / "graphs"


@dataclass(frozen=True)
class Config:
    """Resolved configuration for the collections/reporting pipeline."""

    token: str = ""
    owner: str = DEFAULT_OWNER
    repo: str = DEFAULT_REPO
    db_path: Path = field(default_factory=default_db_path)
    export_dir: Path = field(default_factory=default_export_dir)
    graphs_dir: Path = field(default_factory=default_graphs_dir)
    timeout_seconds: float = 20.0

    @property
    def repo_slug(self) -> str:
        return f"{self.owner}/{self.repo}"

    def require_token(self) -> "Config":
        if not self.token:
            raise ConfigError(
                "GITHUB_TOKEN is not set. Set it before running 'vyb-traffic collect'."
            )
        return self


def load_config(
    token: str | None = None,
    owner: str | None = None,
    repo: str | None = None,
    db_path: str | None = None,
    export_dir: str | None = None,
    graphs_dir: str | None = None,
    timeout_seconds: float = 20.0,
) -> Config:
    """Build a :class:`Config` from explicit args, falling back to the environment.

    Command-line values win over the environment, which wins over defaults.
    The token is never logged and never written to disk by this package.
    """
    resolved_token = token if token is not None else os.environ.get(ENV_TOKEN, "")
    resolved_owner = owner or os.environ.get(ENV_OWNER, DEFAULT_OWNER)
    resolved_repo = repo or os.environ.get(ENV_REPO, DEFAULT_REPO)

    db = Path(db_path).expanduser() if db_path else default_db_path()
    exp = Path(export_dir).expanduser() if export_dir else default_export_dir()
    gr = Path(graphs_dir).expanduser() if graphs_dir else default_graphs_dir()

    if not resolved_owner or not resolved_repo:
        raise ConfigError("owner and repo must be non-empty.")

    return Config(
        token=resolved_token,
        owner=resolved_owner,
        repo=resolved_repo,
        db_path=db,
        export_dir=exp,
        graphs_dir=gr,
        timeout_seconds=timeout_seconds,
    )


def init_paths(config: Config) -> None:
    """Create the database parent; exports/graphs create their own directories."""
    try:
        config.db_path.parent.mkdir(parents=True, exist_ok=True)
    except OSError as exc:  # pragma: no cover - depends on environment
        raise ConfigError(f"cannot create data directory {config.db_path.parent}: {exc}")
