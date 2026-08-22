"""Minimal, validated GitHub REST client for repository traffic data."""

from __future__ import annotations

import json
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime
from typing import Any

from .errors import (
    AuthError,
    ForbiddenError,
    MalformedResponseError,
    RateLimitError,
    TransportError,
)

API_HOST = "https://api.github.com"
ACCEPT_HEADER = "application/vnd.github+json"
API_VERSION = "2022-11-28"


def _nonnegative_int(value: Any, field: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise MalformedResponseError(f"{field} must be a non-negative integer")
    return value


def _text(value: Any, field: str, *, allow_empty: bool = False) -> str:
    if not isinstance(value, str) or (not allow_empty and not value.strip()):
        raise MalformedResponseError(f"{field} must be a non-empty string")
    return value


def _timestamp(value: Any, field: str) -> str:
    value = _text(value, field)
    try:
        datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as exc:
        raise MalformedResponseError(f"{field} is not an ISO-8601 timestamp") from exc
    return value


def _parse_traffic(payload: Any, bucket_key: str) -> dict[str, Any]:
    if not isinstance(payload, dict):
        raise MalformedResponseError(f"{bucket_key} response must be an object")
    buckets = payload.get(bucket_key)
    if not isinstance(buckets, list):
        raise MalformedResponseError(f"{bucket_key} must be a list")

    parsed_buckets = []
    for index, bucket in enumerate(buckets):
        if not isinstance(bucket, dict):
            raise MalformedResponseError(f"{bucket_key}[{index}] must be an object")
        prefix = f"{bucket_key}[{index}]"
        parsed_buckets.append(
            {
                "timestamp": _timestamp(bucket.get("timestamp"), f"{prefix}.timestamp"),
                "count": _nonnegative_int(bucket.get("count"), f"{prefix}.count"),
                "uniques": _nonnegative_int(bucket.get("uniques"), f"{prefix}.uniques"),
            }
        )

    return {
        "count": _nonnegative_int(payload.get("count"), "count"),
        "uniques": _nonnegative_int(payload.get("uniques"), "uniques"),
        bucket_key: parsed_buckets,
    }


def parse_clones(payload: Any) -> dict[str, Any]:
    """Validate and normalize a GitHub clone-traffic response."""
    return _parse_traffic(payload, "clones")


def parse_views(payload: Any) -> dict[str, Any]:
    """Validate and normalize a GitHub view-traffic response."""
    return _parse_traffic(payload, "views")


def parse_referrers(payload: Any) -> list[dict[str, Any]]:
    """Validate and normalize a GitHub popular-referrers response."""
    if not isinstance(payload, list):
        raise MalformedResponseError("popular/referrers response must be a list")
    result = []
    for index, row in enumerate(payload):
        if not isinstance(row, dict):
            raise MalformedResponseError(f"referrers[{index}] must be an object")
        prefix = f"referrers[{index}]"
        result.append(
            {
                "referrer": _text(row.get("referrer"), f"{prefix}.referrer"),
                "count": _nonnegative_int(row.get("count"), f"{prefix}.count"),
                "uniques": _nonnegative_int(row.get("uniques"), f"{prefix}.uniques"),
            }
        )
    return result


def parse_paths(payload: Any) -> list[dict[str, Any]]:
    """Validate and normalize a GitHub popular-paths response."""
    if not isinstance(payload, list):
        raise MalformedResponseError("popular/paths response must be a list")
    result = []
    for index, row in enumerate(payload):
        if not isinstance(row, dict):
            raise MalformedResponseError(f"paths[{index}] must be an object")
        prefix = f"paths[{index}]"
        title = row.get("title")
        if title is not None:
            title = _text(title, f"{prefix}.title", allow_empty=True)
        result.append(
            {
                "path": _text(row.get("path"), f"{prefix}.path"),
                "title": title,
                "count": _nonnegative_int(row.get("count"), f"{prefix}.count"),
                "uniques": _nonnegative_int(row.get("uniques"), f"{prefix}.uniques"),
            }
        )
    return result


class GitHubClient:
    """Small synchronous client for the four GitHub traffic endpoints."""

    def __init__(self, token: str, owner: str, repo: str, timeout_seconds: float = 20.0):
        if not token:
            raise AuthError("GitHub token is required")
        self._token = token
        self._owner = urllib.parse.quote(owner, safe="")
        self._repo = urllib.parse.quote(repo, safe="")
        self._timeout = timeout_seconds

    def _urlopen(self, url: str):
        request = urllib.request.Request(
            url,
            headers={
                "Accept": ACCEPT_HEADER,
                "X-GitHub-Api-Version": API_VERSION,
                "Authorization": f"Bearer {self._token}",
                "User-Agent": "vyb-traffic/1.0 (+github.com/rickenator/Vyb)",
            },
        )
        try:
            return urllib.request.urlopen(request, timeout=self._timeout)
        except urllib.error.HTTPError as exc:
            self._raise_for_status(exc)
            raise AssertionError("unreachable")
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            raise TransportError(f"network error contacting GitHub: {exc}") from exc

    @staticmethod
    def _raise_for_status(exc: urllib.error.HTTPError) -> None:
        headers = exc.headers or {}
        if exc.code == 401:
            raise AuthError(
                "GitHub returned HTTP 401; check GITHUB_TOKEN and repository access"
            ) from exc
        if exc.code == 403:
            remaining = headers.get("X-RateLimit-Remaining", "")
            reset = headers.get("X-RateLimit-Reset", "unknown")
            if remaining == "0":
                raise RateLimitError(
                    f"GitHub API rate limit reached; reset epoch is {reset}"
                ) from exc
            raise ForbiddenError(
                "GitHub returned HTTP 403; traffic data requires push access"
            ) from exc
        if exc.code == 429:
            raise RateLimitError("GitHub returned HTTP 429 (rate limited)") from exc
        if 400 <= exc.code < 600:
            raise ForbiddenError(f"GitHub returned HTTP {exc.code}") from exc
        raise TransportError(f"GitHub returned unexpected HTTP {exc.code}") from exc

    def _get_json(self, path: str) -> Any:
        url = f"{API_HOST}{path}"
        with self._urlopen(url) as response:
            body = response.read()
        try:
            return json.loads(body.decode("utf-8"))
        except (ValueError, UnicodeDecodeError) as exc:
            raise MalformedResponseError("GitHub returned malformed JSON") from exc

    def _traffic_path(self, endpoint: str) -> str:
        return f"/repos/{self._owner}/{self._repo}/traffic/{endpoint}"

    def get_clones(self) -> dict[str, Any]:
        return parse_clones(self._get_json(self._traffic_path("clones")))

    def get_views(self) -> dict[str, Any]:
        return parse_views(self._get_json(self._traffic_path("views")))

    def get_referrers(self) -> list[dict[str, Any]]:
        return parse_referrers(self._get_json(self._traffic_path("popular/referrers")))

    def get_paths(self) -> list[dict[str, Any]]:
        return parse_paths(self._get_json(self._traffic_path("popular/paths")))
