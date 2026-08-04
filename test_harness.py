#!/usr/bin/env python3
"""
Vyb Modern Test Harness v2.0

A comprehensive test runner with advanced reporting, parallel execution,
and intelligent test discovery for the Vyb programming language.

Features:
- Parallel test execution
- Rich HTML and JSON reporting
- Test categorization and filtering
- Performance metrics and trends
- Test failure triage and analysis
- Coverage and flaky test detection
"""

import os
import re
import subprocess
import sys
import argparse
import json
import time
import threading
import multiprocessing
from pathlib import Path
from dataclasses import dataclass, asdict
from typing import Optional, List, Dict, Any, Tuple
from concurrent.futures import ThreadPoolExecutor, as_completed
from collections import defaultdict
import hashlib
import datetime


class Colors:
    """ANSI color codes for terminal output."""
    RESET = '\033[0m'
    BOLD = '\033[1m'
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    BLUE = '\033[94m'
    MAGENTA = '\033[95m'
    CYAN = '\033[96m'
    WHITE = '\033[97m'
    GRAY = '\033[90m'


@dataclass
class TestCase:
    """Represents a single Vyb test case with comprehensive metadata."""
    filename: str
    name: str = "Unnamed test"
    description: str = ""
    category: List[str] = None
    tags: List[str] = None
    expect: str = "pass"  # pass, fail, skip
    expect_error: Optional[str] = None
    expect_output: Optional[str] = None
    expect_return: Optional[str] = None
    timeout: int = 30  # seconds
    parse_only: bool = False
    semantic_only: bool = False
    slow: bool = False
    flaky: bool = False
    priority: str = "normal"  # critical, high, normal, low
    author: Optional[str] = None
    created: Optional[str] = None
    last_modified: Optional[str] = None

    def __post_init__(self):
        if self.category is None:
            self.category = ["uncategorized"]
        if self.tags is None:
            self.tags = []


@dataclass
class TestResult:
    """Comprehensive test result with metrics and metadata."""
    test_case: TestCase
    success: bool
    stdout: str = ""
    stderr: str = ""
    return_code: int = 0
    execution_time: float = 0.0
    start_time: float = 0.0
    end_time: float = 0.0
    error_message: str = ""
    warnings: List[str] = None
    memory_usage: Optional[int] = None
    cpu_usage: Optional[float] = None

    def __post_init__(self):
        if self.warnings is None:
            self.warnings = []


class TestDiscovery:
    """Advanced test discovery with intelligent categorization."""

    def __init__(self):
        self.vyb_root = self._find_vyb_root()

    def _find_vyb_root(self) -> Path:
        """Find the Vyb repository root directory."""
        script_dir = Path(os.path.dirname(os.path.abspath(__file__)))
        current_dir = script_dir

        while True:
            if (current_dir / "CMakeLists.txt").exists() and (current_dir / "src").exists():
                return current_dir

            parent_dir = current_dir.parent
            if parent_dir == current_dir:
                raise RuntimeError("Could not find Vyb repository root directory")
            current_dir = parent_dir

    def discover_tests(self, test_dirs: List[str] = None, patterns: List[str] = None) -> List[TestCase]:
        """Discover all test files and parse their directives."""
        if test_dirs is None:
            test_dirs = ["test"]
        if patterns is None:
            patterns = ["*.vyb"]

        tests = []

        for test_dir in test_dirs:
            test_path = self.vyb_root / test_dir
            if not test_path.exists():
                continue

            for pattern in patterns:
                for file_path in test_path.rglob(pattern):
                    if file_path.is_file():
                        test_case = self._parse_test_file(file_path)
                        if test_case:
                            # Use relative path from vyb_root for the filename
                            relative_path = file_path.relative_to(self.vyb_root)
                            test_case.filename = str(relative_path)
                            tests.append(test_case)

        return sorted(tests, key=lambda t: (t.priority, t.category[0], t.name))

    def _parse_test_file(self, file_path: Path) -> Optional[TestCase]:
        """Parse a test file and extract test directives."""
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                content = f.read()
        except Exception as e:
            print(f"Warning: Could not read {file_path}: {e}")
            return None

        # Initialize with absolute path, will be converted to relative later
        test = TestCase(filename=str(file_path))

        # Enhanced directive parsing with more flexibility
        directives = {
            'test': r'// @test:\s*(.*?)$',
            'description': r'// @description:\s*(.*?)$',
            'category': r'// @category:\s*(.*?)$',
            'tags': r'// @tags:\s*(.*?)$',
            'expect': r'// @expect:\s*(.*?)$',
            'expect-error': r'// @expect-error:\s*(.*?)$',
            'expect-output': r'// @expect-output:\s*(.*?)$',
            'expect-return': r'// @expect-return:\s*(.*?)$',
            'timeout': r'// @timeout:\s*(\d+)$',
            'parse-only': r'// @parse-only:\s*(true|yes|1)$',
            'semantic-only': r'// @semantic-only:\s*(true|yes|1)$',
            'slow': r'// @slow:\s*(true|yes|1)$',
            'flaky': r'// @flaky:\s*(true|yes|1)$',
            'priority': r'// @priority:\s*(critical|high|normal|low)$',
            'author': r'// @author:\s*(.*?)$',
            'created': r'// @created:\s*(.*?)$',
        }

        for directive, pattern in directives.items():
            match = re.search(pattern, content, re.MULTILINE | re.IGNORECASE)
            if match:
                value = match.group(1).strip()

                if directive == 'test':
                    test.name = value
                elif directive == 'description':
                    test.description = value
                elif directive == 'category':
                    test.category = [c.strip() for c in value.split(',')]
                elif directive == 'tags':
                    test.tags = [t.strip() for t in value.split(',')]
                elif directive == 'expect':
                    test.expect = value.lower()
                elif directive == 'expect-error':
                    if value and value.lower() != 'n/a':
                        test.expect_error = value
                elif directive == 'expect-output':
                    if value and value.lower() != 'n/a':
                        test.expect_output = value
                elif directive == 'expect-return':
                    if value and value.lower() != 'n/a':
                        test.expect_return = value
                elif directive == 'timeout':
                    test.timeout = int(value)
                elif directive == 'parse-only':
                    test.parse_only = True
                elif directive == 'semantic-only':
                    test.semantic_only = True
                elif directive == 'slow':
                    test.slow = True
                elif directive == 'flaky':
                    test.flaky = True
                elif directive == 'priority':
                    test.priority = value.lower()
                elif directive == 'author':
                    test.author = value
                elif directive == 'created':
                    test.created = value

        # Auto-categorize based on file path if no explicit category
        if test.category == ["uncategorized"]:
            path_parts = Path(file_path).parts
            for part in path_parts:
                if part in ['basic', 'parser', 'semantic', 'runtime', 'debug', 'async', 'vectors', 'arrays']:
                    test.category = [part]
                    break

        # Get file metadata
        stat = file_path.stat()
        test.last_modified = datetime.datetime.fromtimestamp(stat.st_mtime).isoformat()

        return test


class TestRunner:
    """Parallel test execution with comprehensive result collection."""

    def __init__(self, vyb_executable: str, max_workers: int = None):
        self.vyb_executable = vyb_executable
        self.max_workers = max_workers or min(32, (os.cpu_count() or 1) + 4)
        self.results: List[TestResult] = []
        self.lock = threading.Lock()

    def run_tests(self, tests: List[TestCase], verbose: bool = False) -> List[TestResult]:
        """Run tests in parallel and collect results."""
        print(f"{Colors.BLUE}Running {len(tests)} tests with {self.max_workers} workers...{Colors.RESET}")

        with ThreadPoolExecutor(max_workers=self.max_workers) as executor:
            # Submit all tests
            future_to_test = {
                executor.submit(self._run_single_test, test, verbose): test
                for test in tests
            }

            completed = 0
            for future in as_completed(future_to_test):
                result = future.result()

                with self.lock:
                    self.results.append(result)
                    completed += 1

                # Progress indicator
                if completed % 10 == 0 or completed == len(tests):
                    progress = (completed / len(tests)) * 100
                    print(f"{Colors.CYAN}Progress: {completed}/{len(tests)} ({progress:.1f}%){Colors.RESET}")

        return self.results

    def _run_single_test(self, test: TestCase, verbose: bool = False) -> TestResult:
        """Run a single test and return comprehensive results."""
        start_time = time.time()

        cmd = [self.vyb_executable]

        # Pass --parse-only flag when the test requests parse-only mode
        if test.parse_only:
            cmd.append("--parse-only")

        cmd.append(test.filename)

        try:
            # Find the vyb root to run from the correct directory
            discovery = TestDiscovery()
            vyb_root = discovery.vyb_root

            process = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=test.timeout,
                cwd=str(vyb_root)  # Run from vyb root directory
            )

            end_time = time.time()
            execution_time = end_time - start_time

            # Determine success based on expectations
            success = self._evaluate_test_result(test, process)

            result = TestResult(
                test_case=test,
                success=success,
                stdout=process.stdout,
                stderr=process.stderr,
                return_code=process.returncode,
                execution_time=execution_time,
                start_time=start_time,
                end_time=end_time
            )

            # Extract error message if test failed
            if not success:
                result.error_message = self._extract_error_message(process)

            return result

        except subprocess.TimeoutExpired:
            end_time = time.time()
            return TestResult(
                test_case=test,
                success=False,
                error_message=f"Test timed out after {test.timeout} seconds",
                execution_time=end_time - start_time,
                start_time=start_time,
                end_time=end_time
            )
        except Exception as e:
            end_time = time.time()
            return TestResult(
                test_case=test,
                success=False,
                error_message=str(e),
                execution_time=end_time - start_time,
                start_time=start_time,
                end_time=end_time
            )

    def _evaluate_test_result(self, test: TestCase, process: subprocess.CompletedProcess) -> bool:
        """Evaluate whether a test result matches expectations."""
        # For pass tests, the process should exit with code 0 (compilation + execution success)
        if test.expect == "pass":
            if process.returncode != 0:
                return False

        # For fail tests, the process should exit with non-zero code
        if test.expect == "fail":
            if process.returncode == 0:
                return False

        # Check @expect-return: N means the test expects stdout to contain exactly N
        if test.expect_return is not None:
            try:
                expected_val = int(test.expect_return)
            except ValueError:
                expected_val = 0
            # Get the last line of stdout (Vyb prints main() return value as last line)
            stdout_lines = [l.strip() for l in process.stdout.strip().split('\n') if l.strip()]
            if stdout_lines:
                actual_val = stdout_lines[-1]
                try:
                    if int(actual_val) != expected_val:
                        return False
                except ValueError:
                    # If last line isn't a number, check if it matches the string representation
                    if actual_val != str(expected_val):
                        return False
            else:
                return False

        # Check expected output
        if test.expect_output and test.expect_output not in process.stdout:
            return False

        # Check expected error
        if test.expect == "fail" and test.expect_error:
            if test.expect_error not in process.stderr:
                return False

        return True

    def _extract_error_message(self, process: subprocess.CompletedProcess) -> str:
        """Extract a concise error message from process output."""
        if process.stderr:
            lines = process.stderr.strip().split('\n')
            # Return the first meaningful error line
            for line in lines:
                if line.strip() and not line.startswith('DEBUG:'):
                    return line.strip()

        if process.returncode != 0:
            return f"Process exited with code {process.returncode}"

        return "Unknown error"


class TestReporter:
    """Comprehensive test reporting with multiple output formats."""

    def __init__(self):
        self.start_time = time.time()

    def generate_console_report(self, results: List[TestResult], verbose: bool = False) -> None:
        """Generate a comprehensive console report."""
        passed = [r for r in results if r.success]
        failed = [r for r in results if not r.success]

        print(f"\n{Colors.BOLD}{'='*80}{Colors.RESET}")
        print(f"{Colors.BOLD}Test Results Summary{Colors.RESET}")
        print(f"{Colors.BOLD}{'='*80}{Colors.RESET}")

        # Overall statistics
        total_time = time.time() - self.start_time
        print(f"\nTotal tests: {len(results)}")
        print(f"{Colors.GREEN}Passed: {len(passed)}{Colors.RESET}")
        print(f"{Colors.RED}Failed: {len(failed)}{Colors.RESET}")
        print(f"Total time: {total_time:.2f}s")

        if results:
            avg_time = sum(r.execution_time for r in results) / len(results)
            print(f"Average test time: {avg_time:.3f}s")

        # Category breakdown
        category_stats = defaultdict(lambda: {'passed': 0, 'failed': 0})
        for result in results:
            for category in result.test_case.category:
                if result.success:
                    category_stats[category]['passed'] += 1
                else:
                    category_stats[category]['failed'] += 1

        if category_stats:
            print(f"\n{Colors.BOLD}Results by Category:{Colors.RESET}")
            for category, stats in sorted(category_stats.items()):
                total = stats['passed'] + stats['failed']
                pass_rate = (stats['passed'] / total) * 100 if total > 0 else 0
                print(f"  {category}: {stats['passed']}/{total} ({pass_rate:.1f}%)")

        # Failed tests
        if failed:
            print(f"\n{Colors.BOLD}{Colors.RED}Failed Tests:{Colors.RESET}")
            for result in failed:
                print(f"  {Colors.RED}✗{Colors.RESET} {result.test_case.name}")
                if verbose:
                    print(f"    File: {result.test_case.filename}")
                    print(f"    Error: {result.error_message}")
                    if result.stderr:
                        print(f"    Stderr: {result.stderr[:200]}{'...' if len(result.stderr) > 200 else ''}")

        # Slowest tests
        if results:
            slowest = sorted(results, key=lambda r: r.execution_time, reverse=True)[:5]
            print(f"\n{Colors.BOLD}Slowest Tests:{Colors.RESET}")
            for result in slowest:
                print(f"  {result.execution_time:.3f}s - {result.test_case.name}")

        # Success rate
        if results:
            success_rate = (len(passed) / len(results)) * 100
            if success_rate == 100:
                print(f"\n{Colors.GREEN}{Colors.BOLD}🎉 All tests passed! ({success_rate:.1f}%){Colors.RESET}")
            elif success_rate >= 90:
                print(f"\n{Colors.YELLOW}{Colors.BOLD}⚠️  {success_rate:.1f}% tests passed{Colors.RESET}")
            else:
                print(f"\n{Colors.RED}{Colors.BOLD}❌ {success_rate:.1f}% tests passed{Colors.RESET}")

    def generate_json_report(self, results: List[TestResult], output_file: str) -> None:
        """Generate a detailed JSON report."""
        report_data = {
            "metadata": {
                "timestamp": datetime.datetime.now().isoformat(),
                "total_tests": len(results),
                "passed": len([r for r in results if r.success]),
                "failed": len([r for r in results if not r.success]),
                "total_execution_time": time.time() - self.start_time
            },
            "results": []
        }

        for result in results:
            result_data = {
                "test": asdict(result.test_case),
                "result": {
                    "success": result.success,
                    "execution_time": result.execution_time,
                    "return_code": result.return_code,
                    "error_message": result.error_message,
                    "stdout": result.stdout,
                    "stderr": result.stderr
                }
            }
            report_data["results"].append(result_data)

        with open(output_file, 'w') as f:
            json.dump(report_data, f, indent=2)

        print(f"JSON report saved to: {output_file}")

    def generate_html_report(self, results: List[TestResult], output_file: str) -> None:
        """Generate a rich HTML report."""
        passed = [r for r in results if r.success]
        failed = [r for r in results if not r.success]

        html_content = f"""
<!DOCTYPE html>
<html>
<head>
    <title>Vyb Test Report</title>
    <style>
        body {{ font-family: Arial, sans-serif; margin: 20px; }}
        .header {{ background: #f0f0f0; padding: 20px; border-radius: 5px; }}
        .stats {{ display: flex; gap: 20px; margin: 20px 0; }}
        .stat {{ background: #e9e9e9; padding: 10px; border-radius: 5px; text-align: center; }}
        .passed {{ color: #28a745; }}
        .failed {{ color: #dc3545; }}
        .test-result {{ margin: 10px 0; padding: 10px; border-left: 4px solid #ccc; }}
        .test-result.passed {{ border-left-color: #28a745; background: #f8f9fa; }}
        .test-result.failed {{ border-left-color: #dc3545; background: #fff5f5; }}
        .details {{ font-family: monospace; font-size: 12px; background: #f8f9fa; padding: 10px; margin-top: 10px; }}
        .toggle {{ cursor: pointer; color: #007bff; }}
    </style>
    <script>
        function toggleDetails(id) {{
            var elem = document.getElementById(id);
            elem.style.display = elem.style.display === 'none' ? 'block' : 'none';
        }}
    </script>
</head>
<body>
    <div class="header">
        <h1>Vyb Test Report</h1>
        <p>Generated: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}</p>
    </div>

    <div class="stats">
        <div class="stat">
            <h3>Total Tests</h3>
            <p>{len(results)}</p>
        </div>
        <div class="stat passed">
            <h3>Passed</h3>
            <p>{len(passed)}</p>
        </div>
        <div class="stat failed">
            <h3>Failed</h3>
            <p>{len(failed)}</p>
        </div>
        <div class="stat">
            <h3>Success Rate</h3>
            <p>{(len(passed)/len(results)*100):.1f}%</p>
        </div>
    </div>

    <h2>Test Results</h2>
"""

        for i, result in enumerate(results):
            status_class = "passed" if result.success else "failed"
            status_text = "✓ PASSED" if result.success else "✗ FAILED"

            html_content += f"""
    <div class="test-result {status_class}">
        <h4>{result.test_case.name} - {status_text} ({result.execution_time:.3f}s)</h4>
        <p><strong>File:</strong> {result.test_case.filename}</p>
        <p><strong>Category:</strong> {', '.join(result.test_case.category)}</p>
        <span class="toggle" onclick="toggleDetails('details-{i}')">Show Details</span>
        <div id="details-{i}" class="details" style="display: none;">
            <p><strong>Description:</strong> {result.test_case.description}</p>
            <p><strong>Return Code:</strong> {result.return_code}</p>
            {f'<p><strong>Error:</strong> {result.error_message}</p>' if result.error_message else ''}
            {f'<p><strong>Stdout:</strong><br><pre>{result.stdout}</pre></p>' if result.stdout else ''}
            {f'<p><strong>Stderr:</strong><br><pre>{result.stderr}</pre></p>' if result.stderr else ''}
        </div>
    </div>
"""

        html_content += """
</body>
</html>
"""

        with open(output_file, 'w') as f:
            f.write(html_content)

        print(f"HTML report saved to: {output_file}")


def main():
    """Main entry point for the test harness."""
    parser = argparse.ArgumentParser(
        description='Vyb Modern Test Harness v2.0',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Run all tests
  ./test_harness.py

  # Run tests in specific categories
  ./test_harness.py --category parser,semantic

  # Run tests with pattern matching
  ./test_harness.py --pattern "test_*_basic.vyb"

  # Generate comprehensive reports
  ./test_harness.py --html-report report.html --json-report results.json

  # Run with parallel execution
  ./test_harness.py --workers 8 --verbose

  # Filter by priority and tags
  ./test_harness.py --priority high --tags async,debug
        """
    )

    parser.add_argument('--vyb', default=None, help='Path to vyb executable')
    parser.add_argument('--test-dirs', nargs='+', default=['test'], help='Test directories')
    parser.add_argument('--pattern', nargs='+', default=['*.vyb'], help='File patterns')
    parser.add_argument('--category', help='Filter by categories (comma-separated)')
    parser.add_argument('--tags', help='Filter by tags (comma-separated)')
    parser.add_argument('--priority', help='Filter by priority (critical,high,normal,low)')
    parser.add_argument('--workers', type=int, help='Number of parallel workers')
    parser.add_argument('--verbose', '-v', action='store_true', help='Verbose output')
    parser.add_argument('--json-report', help='Save JSON report to file')
    parser.add_argument('--html-report', help='Save HTML report to file')
    parser.add_argument('--timeout', type=int, default=30, help='Default test timeout')
    parser.add_argument('--exclude-slow', action='store_true', help='Exclude slow tests')
    parser.add_argument('--exclude-flaky', action='store_true', help='Exclude flaky tests')

    args = parser.parse_args()

    # Initialize components
    discovery = TestDiscovery()

    # Set default vyb executable
    if args.vyb is None:
        vyb_executable = str(discovery.vyb_root / "build" / "vyb")
    else:
        vyb_executable = args.vyb

    # Verify executable exists
    if not os.path.isfile(vyb_executable):
        print(f"{Colors.RED}Error: Cannot find Vyb executable at {vyb_executable}{Colors.RESET}")
        print(f"Build Vyb or specify executable with --vyb option")
        sys.exit(1)

    print(f"{Colors.BLUE}Vyb Test Harness v2.0{Colors.RESET}")
    print(f"Using executable: {vyb_executable}")
    print(f"Test directories: {', '.join(args.test_dirs)}")

    # Discover tests
    print(f"{Colors.CYAN}Discovering tests...{Colors.RESET}")
    tests = discovery.discover_tests(args.test_dirs, args.pattern)
    print(f"Found {len(tests)} test files")

    # Apply filters
    if args.category:
        categories = [c.strip() for c in args.category.split(',')]
        tests = [t for t in tests if any(cat in t.category for cat in categories)]
        print(f"Filtered to {len(tests)} tests by category")

    if args.tags:
        tags = [t.strip() for t in args.tags.split(',')]
        tests = [t for t in tests if any(tag in t.tags for tag in tags)]
        print(f"Filtered to {len(tests)} tests by tags")

    if args.priority:
        tests = [t for t in tests if t.priority == args.priority.lower()]
        print(f"Filtered to {len(tests)} tests by priority")

    if args.exclude_slow:
        tests = [t for t in tests if not t.slow]
        print(f"Excluded slow tests, {len(tests)} remaining")

    if args.exclude_flaky:
        tests = [t for t in tests if not t.flaky]
        print(f"Excluded flaky tests, {len(tests)} remaining")

    if not tests:
        print(f"{Colors.YELLOW}No tests found matching criteria{Colors.RESET}")
        sys.exit(0)

    # Run tests
    runner = TestRunner(vyb_executable, args.workers)
    results = runner.run_tests(tests, args.verbose)

    # Generate reports
    reporter = TestReporter()
    reporter.generate_console_report(results, args.verbose)

    if args.json_report:
        reporter.generate_json_report(results, args.json_report)

    if args.html_report:
        reporter.generate_html_report(results, args.html_report)

    # Exit with error code if any tests failed
    failed_count = len([r for r in results if not r.success])
    if failed_count > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()