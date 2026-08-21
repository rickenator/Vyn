# Vyb Test Harness and Analysis System

Vyb's canonical test runner is `test/run_tests.py` (wired into CTest as `run-tests`; currently **1061 `.vyb` tests, all passing**). This guide documents the auxiliary parallel harness (`test_harness.py`) and its richer reporting/triage workflow.

## Overview

The Vyb test system consists of:
- **1061 Tests, All Passing**: The canonical `test/run_tests.py` suite covers all language features
- **Modern Test Harness**: Parallel execution with intelligent categorization
- **Rich Reporting**: HTML, JSON, and console output formats
- **Failure Triage**: Automated analysis and actionable recommendations
- **Performance Tracking**: Execution time analysis and optimization insights

## Test Harness Architecture

### Core Components

#### 1. TestDiscovery
- **Intelligent test discovery** across multiple directories
- **Automatic categorization** based on file paths and directives
- **Metadata extraction** from test file comments
- **Pattern-based filtering** with glob support

#### 2. TestRunner
- **Parallel execution** with configurable worker threads
- **Timeout management** per test with customizable limits
- **Comprehensive result collection** with detailed metrics
- **Progress tracking** with real-time updates

#### 3. TestReporter
- **Multi-format output**: Console, HTML, and JSON reports
- **Statistical analysis**: Pass rates, category breakdowns, performance metrics
- **Visual reporting**: Rich HTML with interactive elements
- **Export capabilities**: JSON for further analysis

#### 4. TriageAnalyzer
- **Pattern recognition** for common failure types
- **Priority-based categorization** of test failures
- **Actionable recommendations** for fixing issues
- **Related test grouping** for efficient debugging

## Test File Format

### Test Directives

Vyb tests use comment-based directives for comprehensive metadata:

```vyb
// @test: Short descriptive name
// @description: Detailed description of what this test validates
// @category: parser, semantic, runtime, async
// @tags: memory, performance, edge-case
// @expect: pass
// @expect-output: Expected output text
// @expect-error: Expected error pattern
// @timeout: 30
// @priority: high
// @author: Developer Name
// @slow: true
// @flaky: false

async test_function()<Future<Int>> -> {
    // Test implementation
    return 42
}
```

### Supported Directives

| Directive | Purpose | Example |
|-----------|---------|---------|
| `@test` | Short test name | `@test: Vector Push Operation` |
| `@description` | Detailed explanation | `@description: Tests Vec<T> push method with type safety` |
| `@category` | Test category | `@category: collections, runtime` |
| `@tags` | Additional tags | `@tags: memory, performance` |
| `@expect` | Expected result | `@expect: pass` or `@expect: fail` |
| `@expect-output` | Expected stdout | `@expect-output: Hello, World!` |
| `@expect-error` | Expected error pattern | `@expect-error: Type mismatch` |
| `@timeout` | Timeout in seconds | `@timeout: 60` |
| `@priority` | Test priority | `@priority: critical` |
| `@author` | Test author | `@author: Jane Developer` |
| `@slow` | Slow test marker | `@slow: true` |
| `@flaky` | Flaky test marker | `@flaky: true` |
| `@parse-only` | Parse-only test | `@parse-only: true` |
| `@semantic-only` | Semantic analysis only | `@semantic-only: true` |

## Usage Guide

### Basic Test Execution

```bash
# Run all tests with default settings
./test_harness.py

# Run with increased parallelism
./test_harness.py --workers 16

# Run with verbose output
./test_harness.py --verbose
```

### Filtering and Selection

```bash
# Run specific categories
./test_harness.py --category parser,semantic

# Run by priority
./test_harness.py --priority critical,high

# Run by tags
./test_harness.py --tags async,memory

# Exclude slow/flaky tests
./test_harness.py --exclude-slow --exclude-flaky

# Pattern matching
./test_harness.py --pattern "test_async_*.vyb"
```

### Report Generation

```bash
# Generate HTML report
./test_harness.py --html-report test_report.html

# Generate JSON results
./test_harness.py --json-report results.json

# Combined execution with reports
./test_harness.py --html-report report.html --json-report results.json --workers 8
```

### Advanced Usage

```bash
# Custom test directories
./test_harness.py --test-dirs test/async test/debug --pattern "*.vyb"

# Custom executable
./test_harness.py --vyb /path/to/custom/vyb

# Timeout override
./test_harness.py --timeout 60
```

## Failure Analysis and Triage

### Triage Tool

The triage tool analyzes test failures and creates actionable plans:

```bash
# Basic triage analysis
./triage_tool.py results.json

# Generate markdown report
./triage_tool.py results.json --format markdown --output triage.md

# Focus on high-priority failures
./triage_tool.py results.json --priority critical,high
```

### Failure Pattern Recognition

The triage system recognizes common failure patterns:

#### Parser Issues
- **Pattern**: `parse error|syntax error|unexpected token`
- **Priority**: High
- **Action**: Fix parser grammar or syntax handling
- **Effort**: Medium

#### Type System Issues
- **Pattern**: `type mismatch|type error|cannot convert`
- **Priority**: High
- **Action**: Review type system and type checking
- **Effort**: Medium

#### Memory Safety Issues
- **Pattern**: `segmentation fault|segfault|signal 11`
- **Priority**: Critical
- **Action**: Debug memory safety issue - use valgrind/asan
- **Effort**: High

#### Performance Issues
- **Pattern**: `timeout|timed out`
- **Priority**: Medium
- **Action**: Optimize performance or increase timeout
- **Effort**: Medium

### Triage Report Structure

#### Phase-Based Organization
1. **Phase 1 - Critical Issues**: Segfaults, crashes, blocking bugs
2. **Phase 2 - High Priority**: Type errors, parser failures
3. **Phase 3 - Medium Priority**: Performance issues, edge cases
4. **Phase 4 - Low Priority**: Minor issues, cleanup tasks

#### Actionable Recommendations
- **Pass rate analysis**: Identifies overall system health
- **Category breakdown**: Highlights problematic subsystems
- **Pattern detection**: Groups related failures for batch fixing
- **Effort estimation**: Helps prioritize development work

## Test Categories

### Core Language Tests
- **basic/**: Fundamental language features
- **parser/**: Syntax parsing and grammar tests
- **semantic/**: Type checking and semantic analysis
- **runtime/**: Execution and runtime behavior

### Feature-Specific Tests
- **async/**: Asynchronous programming and debugging
- **vectors/**: Vec<T> collection operations
- **arrays/**: Fixed-size array functionality
- **debug/**: Debug information and metadata

### Specialized Tests
- **template/**: Template and generic system tests
- **units/**: Unit-level functionality tests
- **performance/**: Performance and optimization tests

## Report Formats

### Console Report

```
================================================================================
Test Results Summary
================================================================================

Total tests: 391
Passed: 345
Failed: 46
Total time: 23.45s
Average test time: 0.060s

Results by Category:
  parser: 89/92 (96.7%)
  semantic: 76/84 (90.5%)
  runtime: 123/135 (91.1%)
  async: 57/80 (71.3%)

Failed Tests:
  ✗ Async State Machine Debug
  ✗ Complex Future Types
  ...

Slowest Tests:
  0.234s - Complex Template Instantiation
  0.189s - Large Vector Operations
  ...
```

### HTML Report

Rich HTML reports include:
- **Interactive test results** with expand/collapse details
- **Statistical dashboards** with visual charts
- **Filterable tables** for easy navigation
- **Detailed error information** with syntax highlighting
- **Performance metrics** with trend analysis

### JSON Report

Structured JSON output for programmatic analysis:

```json
{
  "metadata": {
    "timestamp": "2025-10-17T15:30:00",
    "total_tests": 391,
    "passed": 345,
    "failed": 46,
    "total_execution_time": 23.45
  },
  "results": [
    {
      "test": {
        "filename": "test/async/debug_test.vyb",
        "name": "Async State Machine Debug",
        "category": ["async", "debug"],
        "priority": "high"
      },
      "result": {
        "success": false,
        "execution_time": 0.123,
        "error_message": "Suspension point creation failed",
        "return_code": 1
      }
    }
  ]
}
```

## Performance Analysis

### Execution Metrics

The test harness tracks comprehensive performance data:
- **Individual test timing**: Identifies slow tests
- **Category performance**: Compares subsystem speeds
- **Parallel efficiency**: Measures threading benefits
- **Resource utilization**: Memory and CPU usage patterns

### Optimization Insights

Performance analysis reveals:
- **Hot paths**: Most time-consuming test categories
- **Bottlenecks**: Tests that consistently run slowly
- **Regression detection**: Performance degradations over time
- **Scaling characteristics**: Behavior with different worker counts

## Integration with Development Workflow

### Continuous Integration

```bash
# CI-friendly test execution
./test_harness.py --workers 4 --json-report ci-results.json --exclude-flaky

# Failure analysis for CI
./triage_tool.py ci-results.json --priority critical,high --output ci-triage.md
```

### Development Testing

```bash
# Quick smoke test during development
./test_harness.py --category basic --exclude-slow

# Feature-specific testing
./test_harness.py --category async --tags debug --verbose

# Performance regression check
./test_harness.py --category performance --html-report perf-report.html
```

### Release Validation

```bash
# Comprehensive release testing
./test_harness.py --workers 8 --html-report release-report.html --json-report release-results.json

# Release triage analysis
./triage_tool.py release-results.json --format markdown --output release-triage.md
```

## Advanced Features

### Parallel Execution Optimization

The test harness optimizes parallel execution:
- **Intelligent work distribution**: Balances load across workers
- **Memory-aware scheduling**: Prevents memory contention
- **Timeout handling**: Prevents hung tests from blocking others
- **Resource monitoring**: Tracks system resource usage

### Smart Test Discovery

Advanced discovery features:
- **Recursive directory scanning**: Finds tests in nested directories
- **Multiple pattern support**: Flexible file matching
- **Metadata caching**: Speeds up repeated discovery
- **Incremental updates**: Only processes changed files

### Extensible Reporting

The reporting system supports:
- **Custom report formats**: Easy to add new output types
- **Template-based generation**: Customizable report layouts
- **Plugin architecture**: Extensible analysis capabilities
- **Data export**: Integration with external analysis tools

## Best Practices

### Writing Maintainable Tests

1. **Use descriptive test names** that explain the test purpose
2. **Include comprehensive metadata** with appropriate directives
3. **Categorize tests appropriately** for efficient filtering
4. **Set realistic timeouts** based on test complexity
5. **Mark slow/flaky tests** to avoid blocking development

### Effective Test Organization

1. **Group related tests** in logical directory structures
2. **Use consistent naming conventions** for easy discovery
3. **Maintain test documentation** within the test files
4. **Regular test maintenance** to remove obsolete tests
5. **Performance monitoring** to catch regressions early

### Debugging Test Failures

1. **Use verbose output** for detailed failure analysis
2. **Run individual tests** to isolate issues
3. **Analyze triage reports** for systematic fixing
4. **Check related test failures** for pattern recognition
5. **Monitor resource usage** for performance issues

## Future Enhancements

### Planned Features

- **Test coverage analysis**: Integration with LLVM coverage tools
- **Regression tracking**: Historical failure pattern analysis
- **Automated bisection**: Finding failure introduction points
- **Performance benchmarking**: Comparative performance analysis
- **Test generation**: Automated test case creation

### Integration Improvements

- **IDE integration**: Test runner plugins for popular editors
- **CI/CD enhancements**: Better integration with build systems
- **Monitoring dashboards**: Real-time test health monitoring
- **Notification systems**: Alert mechanisms for critical failures

## Conclusion

Vyb's test harness represents a modern approach to language testing, combining comprehensive coverage with intelligent analysis. The system's 1061+ tests provide excellent coverage of language features, while the advanced reporting and triage capabilities enable efficient development and maintenance workflows.

The combination of parallel execution, rich reporting, and automated failure analysis makes the Vyb test harness a powerful tool for ensuring language quality and developer productivity.