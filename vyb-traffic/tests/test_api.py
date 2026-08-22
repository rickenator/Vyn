"""API parsing and transport tests; no real network access."""

from __future__ import annotations

import urllib.error
from unittest.mock import patch

import pytest

from conftest import FakeResponse, payload_bytes
from vyb_traffic.api import GitHubClient, parse_clones, parse_views
from vyb_traffic.errors import (
    AuthError,
    ForbiddenError,
    MalformedResponseError,
    RateLimitError,
    TransportError,
)


def _client(**kwargs):
    values = {"token": "test-token", "owner": "rickenator", "repo": "Vyb"}
    values.update(kwargs)
    return GitHubClient(**values)


def test_parse_clone_response(clone_payload):
    parsed = parse_clones(clone_payload)
    assert parsed["count"] == 1129
    assert parsed["uniques"] == 82
    assert parsed["clones"][0] == {
        "timestamp": "2026-08-22T00:00:00Z", "count": 55, "uniques": 3,
    }


def test_parse_view_response(view_payload):
    parsed = parse_views(view_payload)
    assert parsed["count"] == 5012
    assert parsed["uniques"] == 402
    assert parsed["views"][1]["count"] == 250


@pytest.mark.parametrize(
    "payload",
    [
        [],
        {"count": 1, "uniques": 1, "clones": "wrong"},
        {"count": -1, "uniques": 1, "clones": []},
        {"count": 1, "uniques": 1, "clones": [{"timestamp": "bad", "count": 1, "uniques": 1}]},
    ],
)
def test_parse_clone_rejects_malformed_payload(payload):
    with pytest.raises(MalformedResponseError):
        parse_clones(payload)


def test_all_endpoint_shapes(clone_payload, view_payload, referrer_payload, path_payload):
    responses = iter([clone_payload, view_payload, referrer_payload, path_payload])

    def fake_open(_url):
        return FakeResponse(payload_bytes(next(responses)))

    client = _client()
    with patch.object(client, "_urlopen", side_effect=fake_open):
        assert client.get_clones()["count"] == 1129
        assert client.get_views()["count"] == 5012
        assert client.get_referrers()[0]["referrer"] == "google.com"
        assert client.get_paths()[0]["path"] == "/"


def test_token_required():
    with pytest.raises(AuthError):
        GitHubClient(token="", owner="o", repo="r")


def test_request_headers_and_url_are_correct(clone_payload):
    captured = {}

    def fake_open(request, timeout=None):
        captured["headers"] = dict(request.headers)
        captured["url"] = request.full_url
        captured["timeout"] = timeout
        return FakeResponse(payload_bytes(clone_payload))

    with patch("vyb_traffic.api.urllib.request.urlopen", side_effect=fake_open):
        _client(timeout_seconds=12.5).get_clones()

    headers = {key.lower(): value for key, value in captured["headers"].items()}
    assert headers["accept"] == "application/vnd.github+json"
    assert headers["x-github-api-version"] == "2022-11-28"
    assert headers["authorization"] == "Bearer test-token"
    assert captured["url"] == "https://api.github.com/repos/rickenator/Vyb/traffic/clones"
    assert captured["timeout"] == 12.5


@pytest.mark.parametrize(
    ("code", "headers", "exception"),
    [
        (401, {}, AuthError),
        (403, {"X-RateLimit-Remaining": "0", "X-RateLimit-Reset": "999"}, RateLimitError),
        (403, {"X-RateLimit-Remaining": "10"}, ForbiddenError),
        (429, {}, RateLimitError),
        (404, {}, ForbiddenError),
    ],
)
def test_http_errors(code, headers, exception):
    error = urllib.error.HTTPError("u", code, "error", headers, None)
    with patch("vyb_traffic.api.urllib.request.urlopen", side_effect=error):
        with pytest.raises(exception):
            _client().get_views()


@pytest.mark.parametrize(
    "error", [urllib.error.URLError("offline"), TimeoutError("timed out")]
)
def test_network_errors(error):
    with patch("vyb_traffic.api.urllib.request.urlopen", side_effect=error):
        with pytest.raises(TransportError):
            _client().get_clones()


def test_malformed_json():
    with patch.object(
        GitHubClient, "_urlopen", return_value=FakeResponse(b"{ not json")
    ):
        with pytest.raises(MalformedResponseError):
            _client().get_clones()


def test_referrers_must_be_list():
    with patch.object(
        GitHubClient, "_urlopen", return_value=FakeResponse(payload_bytes({"bad": 1}))
    ):
        with pytest.raises(MalformedResponseError):
            _client().get_referrers()
