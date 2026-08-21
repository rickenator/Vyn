# String Type Tests

This directory contains comprehensive tests for Vyb's String type implementation.

## Test Files

### Basic Tests
- **`string_test.vyb`** - Basic String functionality
  - `from_bytes()` constructor
  - `len()` method
  - `+` operator concatenation
  - String creation and manipulation

### Simple Method Tests
- **`string_simple_test.vyb`** - Core method validation
  - All 11 String methods in a single test
  - Basic functionality verification
  - Quick sanity check for String operations

### Comprehensive Method Tests
- **`string_methods_test.vyb`** - Detailed method testing
  - Individual test functions for each method
  - Edge cases and bounds checking
  - Expected output validation
  - Methods tested:
    - `substring(start, end)`
    - `char_at(index)`
    - `starts_with(prefix)`
    - `ends_with(suffix)`
    - `contains(substring)`
    - `to_upper()`
    - `to_lower()`

### String Literal Tests
- **`string_literal_test.vyb`** - String literal syntax
  - Direct literal assignment: `data<String> = "Hello"`
  - Literal concatenation: `"this" + "that"`
  - Method calls on literals: `"Hello".len()`
  - Natural string syntax validation

- **`string_literal_simple.vyb`** - Simple literal tests
  - Basic literal concatenation
  - Length verification

## Running Tests

### Run Individual Tests
```bash
# Basic functionality
build/vyb test/string/string_test.vyb

# Quick method check
build/vyb test/string/string_simple_test.vyb

# Comprehensive method tests
build/vyb test/string/string_methods_test.vyb

# String literals
build/vyb test/string/string_literal_test.vyb
```

### Run All String Tests
```bash
# Using test harness
python3 test/run_tests.py --vyb build/vyb --test-dir test/string --execute-jit

# Manual loop
for f in test/string/string_*.vyb; do
    echo "Testing $f..."
    build/vyb "$f"
done
```

## Expected Output

### string_test.vyb
```
Starting String tests...
Created string from bytes
String length: 5
Combined string length: 11
String tests completed
```

### string_simple_test.vyb
```
Testing String methods...
substring length: 5
char_at(0): 72
starts_with: true
ends_with: true
contains: true
to_upper first char: 72
to_lower first char: 104
All tests completed!
```

### string_methods_test.vyb
```
=== String::substring() ===
substring(0, 5) length: 5
substring(6, 11) length: 5
substring(6) length: 5
=== String::char_at() ===
char_at(0) = 72
char_at(4) = 111
char_at(10) out of bounds = 0
[... more output ...]
```

### string_literal_test.vyb
```
String literal assigned: 5
Concatenated length: 11
First char: 72
Starts with Hello: true
```

## Test Coverage

### Methods Tested ✅
1. `String::from_bytes(ptr, len)` - Constructor
2. `len()` - Get string length
3. `substring(start, end)` - Extract substring
4. `char_at(index)` - Character access
5. `starts_with(prefix)` - Prefix check
6. `ends_with(suffix)` - Suffix check
7. `contains(substring)` - Substring search
8. `to_upper()` - Uppercase conversion
9. `to_lower()` - Lowercase conversion
10. `+ operator` - String concatenation
11. String literals - Natural syntax

### Edge Cases Tested ✅
- Empty strings
- Out of bounds access
- Null termination
- Memory allocation
- Bounds checking
- ASCII case conversion

### Future Test Ideas
- [ ] Unicode/UTF-8 handling
- [ ] Large string performance
- [ ] Memory leak detection
- [ ] Concurrent string operations
- [ ] String formatting/interpolation
