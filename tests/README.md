# ThemisDB Tests

This directory contains the test suite for ThemisDB.

## Test Categories

### Unit Tests
Tests for individual components and functions.

### Integration Tests
Tests for component interactions and API endpoints.

### Geospatial Tests
**Path:** `geo/`
Tests for geospatial query processing and indexing.

### Configuration Tests
**Path:** `config/`
Configuration file validation and parsing tests.

## Running Tests

To run all tests:

```bash
# Linux/macOS
./build.sh && ctest --test-dir build

# Windows
.\build.ps1
cd build
ctest
```

To run specific test suites, see the build documentation in the main [README.md](../README.md).

## Test Data

Test data files are located in the `data/` directory at the project root.

## Writing Tests

When adding new tests:
1. Follow the existing test structure and naming conventions
2. Use the Catch2 framework for C++ tests
3. Include both positive and negative test cases
4. Document test requirements and setup
5. Ensure tests are deterministic and can run in isolation

## Documentation

For more information on testing and quality assurance, see:
- [Quality Assurance](../docs/quality_assurance.md)
- [Code Quality](../docs/code_quality.md)
