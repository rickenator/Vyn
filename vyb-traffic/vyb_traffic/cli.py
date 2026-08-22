"""Command-line interface for vyb-traffic.

Exit codes
----------
0  success
1  fatal error (configuration, storage, or unexpected)
2  partial success (collection stored some endpoints but at least one failed)
"""

from __future__ import annotations

import argparse
import sys

from . import __version__
from .collector import collect
from .config import init_paths, load_config
from .db import TrafficDB
from .errors import VybTrafficError
from .export import export
from .graph import render_graphs
from .report import render_report

EXIT_OK = 0
EXIT_ERROR = 1
EXIT_PARTIAL = 2


def _add_connection_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--owner", default=None, help="GitHub owner (default: rickenator)")
    parser.add_argument("--repo", default=None, help="GitHub repo (default: Vyb)")
    parser.add_argument("--db", default=None, help="Path to the SQLite database")
    parser.add_argument("--export-dir", default=None, help="Directory for CSV exports")
    parser.add_argument("--graphs-dir", default=None, help="Directory for PNG graphs")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="vyb-traffic",
        description="Archive GitHub repository traffic beyond GitHub's 14-day window.",
    )
    parser.add_argument("--version", action="version", version=f"vyb-traffic {__version__}")
    _add_connection_args(parser)

    sub = parser.add_subparsers(dest="command", required=True, metavar="COMMAND")

    p_collect = sub.add_parser("collect", help="Fetch all traffic endpoints and store them")
    p_collect.set_defaults(func=cmd_collect)

    p_report = sub.add_parser("report", help="Print a concise traffic report")
    p_report.add_argument("--recent", type=int, default=14,
                          help="Days of recent daily history to show (default: 14)")
    p_report.add_argument("--no-uniques", action="store_true",
                          help="Hide the daily unique-observations section")
    p_report.set_defaults(func=cmd_report)

    p_export = sub.add_parser("export", help="Export CSV files (read-only views of the DB)")
    p_export.set_defaults(func=cmd_export)

    p_graph = sub.add_parser("graph", help="Generate PNG graphs (requires matplotlib)")
    p_graph.add_argument("--no-uniques", action="store_true",
                         help="Do not overlay daily unique observations")
    p_graph.set_defaults(func=cmd_graph)

    p_init = sub.add_parser("init", help="Create the database schema and data dirs")
    p_init.set_defaults(func=cmd_init)

    return parser


def cmd_collect(config, args) -> int:
    config.require_token()
    init_paths(config)
    outcome = collect(config)
    print(f"vyb-traffic: snapshot {outcome.snapshot_id} @ {outcome.timestamp_utc} "
          f"stored: {', '.join(sorted(outcome.stored)) or '(none)'}")
    if outcome.ok:
        return EXIT_OK
    for name, msg in outcome.failures:
        print(f"vyb-traffic: warning: {name}: {msg}", file=sys.stderr)
    return EXIT_PARTIAL


def cmd_report(config, args) -> int:
    with TrafficDB(config.db_path) as db:
        text = render_report(db, config.owner, config.repo,
                             recent_days=max(1, args.recent),
                             include_uniques=not args.no_uniques)
    print(text)
    return EXIT_OK


def cmd_export(config, args) -> int:
    init_paths(config)
    with TrafficDB(config.db_path) as db:
        written = export(db, config.export_dir)
    for path in written:
        print(path)
    return EXIT_OK


def cmd_graph(config, args) -> int:
    with TrafficDB(config.db_path) as db:
        written = render_graphs(db, config.graphs_dir,
                                include_uniques=not args.no_uniques)
    for path in written:
        print(path)
    return EXIT_OK


def cmd_init(config, args) -> int:
    init_paths(config)
    with TrafficDB(config.db_path) as db:
        pass  # connect() creates schema
    print(f"vyb-traffic: initialized database at {config.db_path}")
    return EXIT_OK


def main(argv=None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        config = load_config(
            owner=args.owner, repo=args.repo,
            db_path=args.db, export_dir=args.export_dir, graphs_dir=args.graphs_dir,
        )
    except VybTrafficError as exc:
        parser.error(str(exc))
        return EXIT_ERROR

    try:
        return args.func(config, args)
    except VybTrafficError as exc:
        print(f"vyb-traffic: error: {exc}", file=sys.stderr)
        return EXIT_ERROR
    except KeyboardInterrupt:
        print("vyb-traffic: interrupted", file=sys.stderr)
        return 130
    except Exception as exc:  # last-resort CLI boundary; never prints configuration
        print(f"vyb-traffic: unexpected error: {exc}", file=sys.stderr)
        return EXIT_ERROR


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
