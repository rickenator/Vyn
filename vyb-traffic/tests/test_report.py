"""Human report accounting-label tests."""

from conftest import seed_db
from vyb_traffic.report import render_report


def test_report_labels_events_and_disclaims_unique_people(temp_db):
    seed_db(temp_db)
    report = render_report(temp_db, "rickenator", "Vyb")
    assert "Archived clone events:  201" in report
    assert "Archived view events:   1,001" in report
    assert "not unique people" in report
    assert "cannot be summed into a true lifetime unique-user count" in report
    assert report.index("2026-08-22   100") < report.index("2026-08-21   101")
