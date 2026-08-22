"""CLI exit-code and secret-handling tests."""

from vyb_traffic.cli import EXIT_ERROR, EXIT_PARTIAL, main
from vyb_traffic.collector import CollectOutcome


def test_collect_without_token_returns_fatal_error(monkeypatch, tmp_path, capsys):
    monkeypatch.delenv("GITHUB_TOKEN", raising=False)
    result = main(["--db", str(tmp_path / "traffic.db"), "collect"])
    assert result == EXIT_ERROR
    assert "GITHUB_TOKEN is not set" in capsys.readouterr().err


def test_partial_collection_returns_distinct_exit_code(monkeypatch, tmp_path, capsys):
    monkeypatch.setenv("GITHUB_TOKEN", "never-print-this-test-token")
    outcome = CollectOutcome(
        snapshot_id=7,
        timestamp_utc="2026-08-22T03:15:00Z",
        stored={"clones", "views", "paths"},
        failures=[("referrers", "timed out")],
    )
    monkeypatch.setattr("vyb_traffic.cli.collect", lambda _config: outcome)
    result = main(["--db", str(tmp_path / "traffic.db"), "collect"])
    output = capsys.readouterr()
    assert result == EXIT_PARTIAL
    assert "referrers: timed out" in output.err
    assert "never-print-this-test-token" not in output.out + output.err
