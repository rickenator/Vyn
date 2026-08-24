# Vyb Development Guide v0.4.0

A comprehensive guide for developing with and contributing to the Vyb programming language, covering the complete development ecosystem from async programming to test harness usage.

## Table of Contents

1. [Getting Started](#getting-started)
2. [Language Features](#language-features)
3. [Async Programming](#async-programming)
4. [Debug System](#debug-system)
5. [Test Harness](#test-harness)
6. [Development Workflow](#development-workflow)
7. [Contributing](#contributing)
8. [Advanced Topics](#advanced-topics)

## Getting Started

### System Requirements

- **Operating System**: Linux (primary), macOS, Windows (WSL)
- **Compiler**: GCC 9+ or Clang 10+
- **CMake**: 3.16 or later
- **LLVM**: 14+ (automatically managed by CMake)
- **Python**: 3.8+ (for test harness and tools)

### Quick Setup

```bash
# Clone the repository
git clone https://github.com/rickenator/Vyb.git
cd Vyb

# Build the compiler
mkdir -p build && cd build
cmake .. && make clean && make -j

# Verify installation
./vyb --version
echo 'main()<Int> -> return 42' > test.vyb
./vyb test.vyb
echo $?  # Should output 42
```

### Project Structure

```
Vyb/
├── src/                 # C++ source code
│   ├── main.cpp         # Entry point with LLVM JIT
│   ├── lexer.cpp        # Tokenization
│   ├── parser.cpp       # Syntax analysis
│   ├── ast.cpp          # Abstract syntax tree
│   └── vre/llvm/        # LLVM codegen and debug
├── include/vyb/         # Header files
├── test/                # 1077 Vyb test files
├── doc/                 # Comprehensive documentation
├── examples/            # Example programs
├── test_harness.py      # Auxiliary parallel test runner (canonical suite: test/run_tests.py)
├── triage_tool.py       # Failure analysis tool
└── build/               # Build output directory
```

## Language Features

### Core Language (100% Complete)

#### Variables and Types
```vyb
// Type inference
x = 42          // Int
name = "Alice"  // String
flag = true     // Bool

// Explicit typing
count<Int> = 0
message<String> = "Hello"
PI<Float> = 3.14159
```

#### Functions with Dual Parameter Syntax
```vyb
// Standard syntax
add_standard(a<Int>, b<Int>)<Int> -> a + b

// Shorthand syntax
add_shorthand(Int a, Int b)<Int> -> a + b

// Mixed syntax
mixed_params(x<Int>, Int y, const Int z)<Int> -> x + y + z
```

#### Advanced Collections
```vyb
// Dynamic vectors
numbers<Vec<Int>> = Vec::new()
numbers.push(10)
numbers.push(20)
length<Int> = numbers.len()
first<Int> = numbers.get(0)

// Fixed arrays
fixed<[Int; 5]> = [1, 2, 3, 4, 5]
element<Int> = fixed[2]  // Array indexing
```

#### Pattern Matching
```vyb
describe_number(x<Int>)<String> -> {
    match (x) {
        0 -> "zero",
        1 -> "one",
        42 -> "the answer",
        ? -> "some number"
    }
}
```

#### Control Flow
```vyb
// Conditionals
check_sign(x<Int>)<String> -> {
    if (x > 0) {
        return "positive"
    } else if (x < 0) {
        return "negative"
    } else {
        return "zero"
    }
}

// Loops with break/continue
factorial(n<Int>)<Int> -> {
    result<Int> = 1
    i<Int> = 1
    while (i <= n) {
        if (i == 0) continue
        result = result * i
        i = i + 1
        if (result > 1000) break
    }
    return result
}
```

## Async Programming

### Complete Async/Await System

Vyb v0.4.0 includes a fully implemented async programming model with comprehensive debugging support.

#### Basic Async Functions
```vyb
// Simple async function
async fetch_data()<Future<Int>> -> {
    result<Int> = 10
    println("Fetching data...")
    return result
}
```

#### Complex Async Operations
```vyb
// Multiple await expressions with state machine debugging
async process_data()<Future<Int>> -> {
    data1<Int> = await fetch_data()    // Suspension point 1
    data2<Int> = await fetch_data()    // Suspension point 2

    combined<Int> = data1 + data2
    return combined
}
```

#### Integration with Main
```vyb
main()<Int> -> {
    println("=== Async Demo ===")
    result<Future<Int>> = process_data()
    return 0
}
```

### Async Debug Features

- **Suspension Point Tracking**: Each await creates a tracked suspension point
- **State Transition Debugging**: Monitor async state machine progression
- **Continuation Debugging**: Debug async resume operations
- **Variable Scope Tracking**: Track variable lifetime across suspensions

Debug output example:
```
DEBUG: Initialized async state debug info for function 'process_data' at line 11
DEBUG: Created suspension point 1 (await_expression_1) at line 12 column 18
DEBUG: Continuation point for state 1 (await_expression_1) at line 12 column 18
DEBUG: Resuming from suspension point at line 12 column 18
```

## Debug System

### LLVM Debug Integration

Vyb provides comprehensive debug information through LLVM's debug infrastructure:

#### Debug Information Features
- **DIBuilder Integration**: Complete LLVM debug metadata generation
- **Source Location Tracking**: Precise line/column information
- **Variable Information**: Local variable and parameter tracking
- **Function Debug Info**: Complete function metadata
- **Async State Debugging**: Specialized async function debugging

#### Debug Variable Information
```vyb
// Debug information is automatically generated for all variables
async test_debug()<Future<Int>> -> {
    local_var<Int> = 42        // Debug info: DILocalVariable created
    param_value<String> = "test"  // Tracked in debug metadata

    result<Int> = await some_async_op()  // Suspension point debugging
    return result
}
```

#### Debugging Commands
```bash
# Compile with debug information
build/vyb --emit-llvm --debug-info test.vyb

# The debug output shows comprehensive information:
# - Function debug info creation
# - Variable debug declarations
# - Async state machine debugging
# - Suspension point tracking
```

## Test Harness

### Modern Parallel Test Runner

Vyb's canonical suite runner (`test/run_tests.py`) manages 1077+ test files; this section describes the auxiliary parallel harness:

#### Basic Usage
```bash
# Run the canonical suite (authoritative pass/fail count)
python3 test/run_tests.py --vyb build/vyb --test-dir test --execute-jit

# Run all tests with the auxiliary parallel harness
./test_harness.py

# Run with parallel execution
./test_harness.py --workers 8

# Generate comprehensive reports
./test_harness.py --html-report report.html --json-report results.json
```

#### Advanced Filtering
```bash
# Run specific categories
./test_harness.py --category async,debug

# Filter by priority and exclude slow tests
./test_harness.py --priority high --exclude-slow

# Pattern matching
./test_harness.py --pattern "*async*.vyb"
```

#### Test Analysis and Triage
```bash
# Analyze failures and create triage plan
./triage_tool.py results.json

# Generate actionable markdown report
./triage_tool.py results.json --format markdown --output triage.md
```

### Test Writing Guidelines

#### Test File Structure
```vyb
// @test: Descriptive Test Name
// @description: Detailed explanation of test purpose
// @category: async, debug, parser, semantic, runtime
// @tags: memory, performance, edge-case
// @expect: pass
// @priority: high
// @timeout: 30

async example_test()<Future<Int>> -> {
    // Test implementation with proper async usage
    data<Int> = await fetch_test_data()
    return data
}

main()<Int> -> {
    result<Future<Int>> = example_test()
    return 0
}
```

#### Test Categories
- **basic/**: Fundamental language features
- **async/**: Asynchronous programming tests
- **debug/**: Debug information and metadata tests
- **parser/**: Syntax parsing tests
- **semantic/**: Type checking tests
- **runtime/**: Execution behavior tests
- **vectors/**: Collection operation tests

## Development Workflow

### Daily Development

#### 1. Quick Development Cycle
```bash
# Make code changes
vim src/vre/llvm/cgen_expr.cpp

# Quick rebuild
make -C build -j

# Test specific functionality
./test_harness.py --category async --pattern "*debug*"
```

#### 2. Feature Development
```bash
# Run comprehensive tests
./test_harness.py --workers 8 --json-report results.json

# Analyze any failures
./triage_tool.py results.json --priority critical,high

# Commit changes with descriptive messages
git add .
git commit -m "Implement async state machine debugging

- Add comprehensive suspension point tracking
- Integrate with LLVM debug infrastructure
- Test with working debug output examples"
```

#### 3. Feature Testing
```bash
# Test new async functionality
build/vyb test/debug_async_test.vyb

# Run related test suite
./test_harness.py --category async --verbose
```

### Release Preparation

#### 1. Comprehensive Testing
```bash
# Full test suite with reporting
./test_harness.py --workers 8 --html-report release-report.html --json-report release-results.json

# Generate triage analysis
./triage_tool.py release-results.json --format markdown --output release-triage.md
```

#### 2. Performance Analysis
```bash
# Performance-focused testing
./test_harness.py --category performance --exclude-flaky

# Check for regressions
./test_harness.py --category runtime --verbose
```

#### 3. Documentation Updates
```bash
# Update README with new features
vim README.md

# Update technical documentation
vim doc/Async_Programming_Debug_System.md
```

## Contributing

### Code Contribution Process

#### 1. Setting Up Development Environment
```bash
# Fork and clone
git clone https://github.com/your-username/Vyb.git
cd Vyb

# Create feature branch
git checkout -b feature/new-async-debugging

# Build and test
mkdir -p build && cd build
cmake .. && make -j
cd ..
./test_harness.py --category basic
```

#### 2. Making Changes
```bash
# Implement new feature
# - Add C++ implementation in src/
# - Add header declarations in include/vyb/
# - Create tests in test/

# Test your changes
./test_harness.py --category relevant-category --verbose

# Ensure all tests pass
./test_harness.py --exclude-flaky
```

#### 3. Submission Process
```bash
# Commit with descriptive messages
git add .
git commit -m "Add comprehensive async debugging support

- Implement suspension point tracking in AsyncState
- Add debug variable information for async functions
- Create test cases for async state machine debugging
- Update documentation with async debug examples"

# Push and create pull request
git push origin feature/new-async-debugging
```

### Contribution Guidelines

#### Code Quality Standards
- **Consistent naming**: Follow existing C++ and Vyb naming conventions
- **Comprehensive testing**: Add tests for all new functionality
- **Documentation**: Update relevant documentation files
- **Debug support**: Ensure new features work with debug infrastructure

#### Testing Requirements
- **Unit tests**: Add specific tests for new functionality
- **Integration tests**: Ensure new features work with existing code
- **Performance tests**: Consider performance impact of changes
- **Debug tests**: Verify debug information generation

### Areas for Contribution

#### High Priority
- **Template system**: Implement comprehensive template/generic support
- **Standard library**: Expand built-in types and functions
- **Error messages**: Improve compiler error reporting
- **Performance**: Optimize compilation and runtime performance

#### Medium Priority
- **Module system**: Implement import/smuggle module system
- **Package manager**: Create dependency management system
- **IDE integration**: Develop language server protocol support
- **Documentation**: Expand examples and tutorials

#### Advanced Projects
- **Self-hosting**: Port compiler from C++ to Vyb
- **JIT optimization**: Enhance LLVM ORC JIT performance
- **Parallel compilation**: Add multi-threaded compilation

## Advanced Topics

### LLVM Integration

#### Understanding the Backend
```cpp
// Key LLVM components in Vyb
class VRECodegen {
    llvm::LLVMContext context;           // LLVM context
    llvm::Module* module;                // LLVM module
    llvm::IRBuilder<> builder;           // IR builder
    llvm::orc::LLJIT* jit;              // ORC JIT engine
    llvm::DIBuilder* diBuilder;          // Debug information builder
};
```

#### Debug Information Architecture
```cpp
// Debug infrastructure components
struct DebugInfo {
    llvm::DICompileUnit* compileUnit;    // Compilation unit
    llvm::DISubprogram* currentFunction; // Current function debug info
    std::stack<llvm::DIScope*> scopes;   // Lexical scope stack
    std::map<std::string, llvm::DILocalVariable*> variables; // Variable debug info
};
```

### Async Implementation Details

#### State Machine Generation
```cpp
// AsyncState structure for debug support
struct AsyncState {
    llvm::Value* stateVar;                           // Current state
    llvm::Value* futureVar;                          // Future result
    llvm::DILocalVariable* stateDebugVar;            // State debug info
    std::vector<llvm::DebugLoc> suspensionPointLocations; // Suspension points
    std::map<int, std::string> stateDescriptions;    // State names
};
```

#### Suspension Point Implementation
```cpp
// Creating suspension points for await expressions
llvm::DebugLoc createSuspensionPointDebugInfo(unsigned line, unsigned column, int stateId) {
    // Create debug location for suspension point
    // Track in async state for debugger integration
    // Associate state ID with source location
}
```

### Performance Optimization

#### JIT Optimization
- **LLVM optimization passes**: Automatic optimization of generated code
- **Lazy compilation**: Functions compiled on first execution
- **Hot code detection**: Optimize frequently executed paths
- **Memory management**: Efficient allocation and cleanup

#### Test Harness Performance
- **Parallel execution**: Optimal worker thread utilization
- **Memory efficiency**: Minimal memory footprint per test
- **I/O optimization**: Efficient file system operations
- **Resource monitoring**: System resource usage tracking

### Debugging Techniques

#### Compiler Debugging
```bash
# Enable verbose debug output
build/vyb --emit-llvm --debug-info --verbose test.vyb

# Analyze generated LLVM IR
build/vyb --emit-llvm test.vyb > output.ll
cat output.ll | grep -A 10 -B 10 "debug"
```

#### Runtime Debugging
```bash
# Use external debugging tools
gdb build/vyb
valgrind --tool=memcheck build/vyb test.vyb
```

#### Test Debugging
```bash
# Debug specific test failures
./test_harness.py --pattern "failing_test.vyb" --verbose

# Analyze test patterns
./triage_tool.py results.json --priority critical
```

## Conclusion

Vyb v0.4.0 represents a complete, production-ready systems programming language with advanced async programming capabilities and comprehensive debugging support. The canonical `test/run_tests.py` suite (1077 tests, all passing) ensures code quality, with auxiliary parallel/triage tooling for analysis.

The combination of clean syntax, powerful async/await support, comprehensive debug infrastructure, and modern development tools makes Vyb an excellent choice for systems programming where debugging, maintainability, and performance are critical.

Whether you're using Vyb for development projects or contributing to the language itself, this guide provides the foundation for effective work with the Vyb ecosystem.