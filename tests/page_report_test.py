#!/usr/bin/env python3
import datetime
import runpy
import unittest
from pathlib import Path

report = runpy.run_path(str(Path(__file__).resolve().parents[1] / "scripts/page-report.py"))


def line(date, target, status=200, agent="Mozilla/5.0 Chrome/140.0"):
    return f'192.0.2.1 - - [{date}:10:00:00 +0000] "GET {target} HTTP/1.1" {status} 42 "-" "{agent}"'


class PageReportTest(unittest.TestCase):
    def test_counts_only_successful_pages_and_separates_bots(self):
        lines = [
            line("07/Nov/2026", "/kosmos/"),
            line("07/Nov/2026", "/kosmos/index.html?from=link", 304),
            line("07/Nov/2026", "/kosmos/manual.html", agent="Googlebot"),
            line("07/Nov/2026", "/kosmos/report/"),
            line("07/Nov/2026", "/kosmos/css/site.css"),
            line("07/Nov/2026", "/kosmos/download/game.zip"),
            line("07/Nov/2026", "/kosmos/../other.html"),
            line("07/Nov/2026", "/kosmos/%2e%2e/other.html"),
            line("07/Nov/2026", "/kosmos/missing.html", 404),
            line("08/Nov/2026", "/kosmos/manual.html"),
        ]
        found, days = report["parse"](lines)
        self.assertEqual(days, {"2026-11-07", "2026-11-08"})
        self.assertEqual(dict(found["2026-11-07"]["human"]),
                         {"/kosmos/index.html": 2, "/kosmos/report/index.html": 1})
        self.assertEqual(dict(found["2026-11-07"]["bot"]), {"/kosmos/manual.html": 1})
        state = report["merge"]({}, found, days, datetime.date(2026, 11, 8))
        self.assertNotIn("2026-11-08", state)
        self.assertIn("/kosmos/manual.html", report["render"](state, "test", datetime.date(2026, 11, 8)))

    def test_oldest_partial_day_does_not_replace_saved_history(self):
        saved = {"2026-11-07": {"human": {"/kosmos/index.html": 10}, "bot": {}}}
        found, days = report["parse"]([line("07/Nov/2026", "/kosmos/"), line("08/Nov/2026", "/other/")])
        report["merge"](saved, found, days, datetime.date(2026, 11, 9))
        self.assertEqual(saved["2026-11-07"]["human"]["/kosmos/index.html"], 10)
        self.assertEqual(saved["2026-11-08"], {"human": {}, "bot": {}})

    def test_missing_logs_are_an_error(self):
        with self.assertRaises(ValueError):
            report["merge"]({}, {}, set(), datetime.date(2026, 11, 8))


if __name__ == "__main__":
    unittest.main()
