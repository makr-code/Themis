# Code Quality Pipeline

ThemisDB uses a comprehensive code quality pipeline to ensure high standards across the codebase.

## Overview

The CI pipeline includes:

- **Static Analysis** (clang-tidy): Detects bugs, code smells, and enforces modern C++17 best practices
- **Linting** (cppcheck): Additional C++ linting for potential issues
- **Code Coverage** (gcov/lcov): Measures test coverage and generates reports
- **Secret Scanning** (Gitleaks): Prevents accidental commits of API keys, passwords, and secrets

## Quick Start

### Local Testing

**Linux/macOS:**
```bash
# Run all checks
./scripts/check-quality.sh

# Skip specific checks
./scripts/check-quality.sh --skip-tidy --skip-tests

# Auto-fix issues
./scripts/check-quality.sh --fix
```

**Windows:**
```powershell
# Run all checks
.\scripts\check-quality.ps1

# Skip specific checks
.\scripts\check-quality.ps1 -SkipTidy -SkipTests

# Auto-fix issues
.\scripts\check-quality.ps1 -Fix
```

### Prerequisites

**Linux (Ubuntu/Debian):**
```bash
sudo apt-get update
sudo apt-get install -y \
  cmake \
  ninja-build \
  clang-tidy \
  clang-tools \
  cppcheck \
  lcov \
  gcovr

# Install gitleaks
wget https://github.com/gitleaks/gitleaks/releases/download/v8.18.4/gitleaks_8.18.4_linux_x64.tar.gz
tar -xzf gitleaks_8.18.4_linux_x64.tar.gz
sudo mv gitleaks /usr/local/bin/
```

**macOS:**
```bash
brew install cmake ninja llvm cppcheck lcov gitleaks
```

**Windows:**
```powershell
# Using Chocolatey
choco install cmake llvm cppcheck gitleaks

# Using Scoop
scoop install cmake llvm cppcheck gitleaks
```

## GitHub Actions Workflows

### Code Quality Workflow

**File:** `.github/workflows/code-quality.yml`

**Triggers:**
- Push to `main` or `develop` branches
- Pull requests to `main` or `develop`

**Jobs:**

1. **clang-tidy**: Static analysis with modern C++ checks
   - Runs on Ubuntu latest
   - Uses `.clang-tidy` configuration
   - Generates `clang-tidy-report.txt`
   - Uploads artifact for review

2. **cppcheck**: C++ linting
   - Runs on Ubuntu latest
   - Uses `.cppcheck-suppressions` for known false positives
   - Generates XML and text reports
   - Uploads artifacts

3. **coverage**: Code coverage analysis
   - Builds with `--coverage` flags
   - Runs full test suite
   - Generates lcov reports and HTML output
   - Comments on PRs with coverage summary
   - Uploads coverage artifacts

4. **gitleaks**: Secret scanning
   - Scans full repository history
   - Uses `.gitleaks.toml` configuration
   - **Fails build** if secrets detected
   - Uploads report for review

5. **quality-summary**: Aggregates all job results
   - Fails if gitleaks finds secrets
   - Reports overall status

### Coverage Badge Workflow

**File:** `.github/workflows/coverage-badge.yml`

**Triggers:**
- Push to `main` branch
- Manual workflow dispatch

**Purpose:**
- Generates coverage percentage badge
- Deploys HTML coverage report to GitHub Pages
- Updates coverage badge in README

**Setup Required:**
1. Create a GitHub Gist for badge storage
2. Add `GIST_SECRET` to repository secrets (Personal Access Token with `gist` scope)
3. Update `gistID` in workflow file
4. Enable GitHub Pages in repository settings

## Configuration Files

### `.clang-tidy`

Configures clang-tidy checks for modern C++17:

```yaml
Checks: >
  bugprone-*,
  clang-analyzer-*,
  cppcoreguidelines-*,
  modernize-*,
  performance-*,
  readability-*,
  concurrency-*
```

**Disabled checks:**
- `modernize-use-trailing-return-type`: Unnecessary for our style
- `readability-magic-numbers`: Too noisy, use sparingly
- `cppcoreguidelines-pro-bounds-pointer-arithmetic`: RocksDB integration requires this

**Naming conventions:**
- Namespaces: `lower_case`
- Classes/Structs: `CamelCase`
- Functions: `camelCase`
- Variables: `lower_case`
- Private members: `lower_case_`
- Constants: `UPPER_CASE`

### `.cppcheck-suppressions`

Suppresses known false positives:

```
# System headers
missingIncludeSystem
unmatchedSuppression

# Third-party code
*:vcpkg_installed/*
*:*/crow/*
*:*/rocksdb/*

# Test files (more flexible)
unusedFunction:tests/*
```

### `.gitleaks.toml`

Configures secret detection rules:

**Custom rules:**
- ThemisDB API keys
- Database connection strings with credentials
- JWT secrets
- Encryption keys
- AWS credentials
- GitHub tokens
- Slack tokens

**Allowlists:**
- Test files: `tests/**`
- Documentation: `docs/**`
- Example configs: `*.example.*`, `*.template.*`

## CI Integration

### Pull Request Checks

When you open a PR, the code-quality workflow runs automatically:

1. **clang-tidy** analyzes code for bugs and style issues
2. **cppcheck** performs additional linting
3. **coverage** measures test coverage and comments on PR
4. **gitleaks** scans for secrets (blocks merge if found)

### Required Status Checks

Configure branch protection rules to require:
- `clang-tidy` (recommended)
- `cppcheck` (recommended)
- `coverage` (optional, for metrics)
- **`gitleaks`** (mandatory, blocks secrets)

### Artifacts

Each workflow uploads artifacts:
- **clang-tidy-report** (30 days retention)
- **cppcheck-xml-report** (30 days)
- **cppcheck-text-report** (30 days)
- **coverage-reports** (30 days, includes HTML)
- **gitleaks-report** (30 days, JSON + summary)

Download from GitHub Actions UI: Actions → Workflow Run → Artifacts

## Coverage Reporting

### Viewing Coverage

**Local:**
```bash
# Generate coverage locally
./scripts/check-quality.sh

# Generate HTML report
mkdir -p coverage
lcov --capture --directory build --output-file coverage/coverage.info --rc lcov_branch_coverage=1
lcov --remove coverage/coverage.info '/usr/*' '*/vcpkg_installed/*' '*/tests/*' \
  --output-file coverage/coverage-filtered.info --rc lcov_branch_coverage=1
genhtml coverage/coverage-filtered.info --output-directory coverage/html

# Open in browser
xdg-open coverage/html/index.html  # Linux
open coverage/html/index.html      # macOS
start coverage/html/index.html     # Windows
```

**GitHub Actions:**
- Coverage report comments on PRs
- HTML report deployed to GitHub Pages: `https://<org>.github.io/<repo>/coverage/`
- Badge in README (after setup)

### Coverage Goals

- **Overall:** Target 80%+ line coverage
- **Critical paths:** 90%+ coverage
  - Storage engine
  - Transaction logic
  - Query engine
- **Nice-to-have:** 70%+ coverage
  - HTTP handlers
  - Utility functions

## Best Practices

### Before Committing

1. **Run local checks:**
   ```bash
   ./scripts/check-quality.sh
   ```

2. **Fix clang-tidy warnings:**
   ```bash
   ./scripts/check-quality.sh --fix
   ```

3. **Review cppcheck output:**
   - Suppress known false positives in `.cppcheck-suppressions`
   - Use `// cppcheck-suppress <error_id>` inline for one-off cases

4. **Check coverage:**
   - Add tests for new code
   - Aim for >80% coverage on modified files

5. **Scan for secrets:**
   - Review gitleaks output carefully
   - **Never commit** real API keys or passwords
   - Use `.env.example` for templates

### Handling False Positives

**clang-tidy:**
```cpp
// Disable specific check for one line
// NOLINTNEXTLINE(check-name)
auto ptr = reinterpret_cast<void*>(addr);

// Disable for block
// NOLINTBEGIN(check-name)
// ... code ...
// NOLINTEND(check-name)
```

**cppcheck:**
```cpp
// Inline suppression
// cppcheck-suppress unusedFunction
void helperFunction() { }
```

**gitleaks:**
Add to `.gitleaks.toml` allowlist:
```toml
[rules.allowlist]
paths = [
  '''tests/fixtures/test_keys.json'''
]
regexes = [
  '''(?i)test[-_]?api[-_]?key'''
]
```

## Troubleshooting

### Issue: clang-tidy too slow

**Solution:**
```bash
# Run on changed files only
git diff --name-only main | grep -E '\.(cpp|h)$' | \
  xargs clang-tidy -p build
```

### Issue: cppcheck false positives

**Solution:**
Add to `.cppcheck-suppressions`:
```
specificError:path/to/file.cpp:123
```

### Issue: Gitleaks flagging test data

**Solution:**
Update `.gitleaks.toml`:
```toml
[allowlist]
paths = [
  '''tests/test_data/.*'''
]
```

### Issue: Coverage report incomplete

**Solution:**
Ensure all tests run before generating report:
```bash
cd build
ctest --output-on-failure
cd ..
lcov --capture --directory build ...
```

## Resources

- [Clang-Tidy Checks](https://clang.llvm.org/extra/clang-tidy/checks/list.html)
- [Cppcheck Manual](http://cppcheck.sourceforge.net/manual.pdf)
- [LCOV Documentation](http://ltp.sourceforge.net/coverage/lcov.php)
- [Gitleaks Configuration](https://github.com/gitleaks/gitleaks#configuration)
- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/)

## Metrics Dashboard (Future)

Planned integrations:
- **SonarQube**: Comprehensive code quality dashboard
- **Codecov**: Advanced coverage tracking
- **CodeClimate**: Maintainability scoring
- **Snyk**: Dependency vulnerability scanning
