# Contributing to ThemisDB

Thank you for your interest in contributing to ThemisDB! This document provides guidelines and instructions for contributing.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Workflow](#development-workflow)
- [Code Quality Standards](#code-quality-standards)
- [Pull Request Process](#pull-request-process)
- [Reporting Bugs](#reporting-bugs)
- [Feature Requests](#feature-requests)

## Code of Conduct

This project adheres to a code of conduct. By participating, you are expected to uphold this code. Please be respectful and constructive in all interactions.

## Getting Started

### Prerequisites

- **C++ Compiler**: GCC 10+, Clang 12+, or MSVC 2019+
- **CMake**: 3.20 or higher
- **vcpkg**: Package manager for dependencies
- **Git**: For version control

### Development Environment Setup

**1. Clone the repository:**

```bash
git clone https://github.com/makr-code/ThemisDB.git
cd ThemisDB
```

**2. Install dependencies:**

**Linux/macOS:**
```bash
./setup.sh
```

**Windows:**
```powershell
.\setup.ps1
```

**3. Build the project:**

**Linux/macOS:**
```bash
./build.sh
```

**Windows:**
```powershell
.\build.ps1
```

**4. Run tests:**

```bash
cd build
ctest --output-on-failure
```

**Running tests under Windows / WSL (developer tips)**

- If you build under WSL the default build output used by repository helper scripts is `build-wsl/` (e.g. `build-wsl/themis_tests` and `build-wsl/themis_server`). Helper scripts (such as `.tools/vault_dev_run.ps1`) rely on this layout.
- To run the GoogleTest binary directly in WSL and export JUnit XML to the Windows host:

```powershell
# from PowerShell (host):
wsl bash -lc "cd /mnt/c/VCC/themis; ./build-wsl/themis_tests --gtest_output=xml:/mnt/c/Temp/themis_tests.xml"
```

- Vault integration tests expect a reachable Vault at `VAULT_ADDR` and a valid token in `VAULT_TOKEN`. The repository includes a small helper `.tools/vault_dev_run.ps1` which:
   - starts a local Vault dev container, enables KV v2 at mount `themis/`, writes a 32‑byte base64 key to `themis/keys/test_key`, and runs the Vault tests from WSL (writing XML output to `C:\Temp`).
   - Preconditions: Docker available locally and a WSL installation with repo mounted under `/mnt/c/VCC/themis`.


### Install Code Quality Tools

**Linux (Ubuntu/Debian):**
```bash
sudo apt-get install -y clang-tidy cppcheck lcov gcovr

# Install gitleaks
wget https://github.com/gitleaks/gitleaks/releases/download/v8.18.4/gitleaks_8.18.4_linux_x64.tar.gz
tar -xzf gitleaks_8.18.4_linux_x64.tar.gz
sudo mv gitleaks /usr/local/bin/
```

**macOS:**
```bash
brew install llvm cppcheck lcov gitleaks
```

**Windows:**
```powershell
choco install llvm cppcheck gitleaks
```

## Development Workflow

### 1. Create a Feature Branch

```bash
git checkout -b feature/your-feature-name
# OR for bug fixes
git checkout -b fix/bug-description
```

**Branch naming conventions:**
- `feature/` - New features
- `fix/` - Bug fixes
- `docs/` - Documentation updates
- `refactor/` - Code refactoring
- `test/` - Test additions/improvements
- `perf/` - Performance improvements

### 2. Make Your Changes

- Write clean, maintainable code following C++17 best practices
- Add tests for new functionality
- Update documentation as needed
- Keep commits focused and atomic

### 3. Run Code Quality Checks

**Before committing, run local quality checks:**

```bash
# Linux/macOS
./scripts/check-quality.sh

# Windows
.\scripts\check-quality.ps1
```

**Auto-fix issues where possible:**

```bash
# Linux/macOS
./scripts/check-quality.sh --fix

# Windows
.\scripts\check-quality.ps1 -Fix
```

### 4. Commit Your Changes

**Write clear, descriptive commit messages:**

```
<type>(<scope>): <subject>

<body>

<footer>
```

**Examples:**

```
feat(storage): Add checkpoint-based incremental backups

- Implement RocksDB checkpoint API integration
- Add WAL archiving with retention policies
- Create cross-platform backup scripts (PowerShell + Bash)
- Add systemd and Kubernetes automation examples

Closes #123
```

```
fix(query): Correct off-by-one error in pagination

The cursor offset calculation was incorrect for empty result sets,
causing the next page to skip the first result.

Fixes #456
```

**Commit types:**
- `feat` - New feature
- `fix` - Bug fix
- `docs` - Documentation changes
- `style` - Code style changes (formatting, no logic change)
- `refactor` - Code refactoring
- `perf` - Performance improvements
- `test` - Test additions/improvements
- `chore` - Build/tooling changes

### 5. Push and Create Pull Request

```bash
git push origin feature/your-feature-name
```

Then create a pull request on GitHub.

## Code Quality Standards

ThemisDB enforces strict code quality standards through automated CI checks:

### Static Analysis (clang-tidy)

- **Enabled checks**: `bugprone-*`, `clang-analyzer-*`, `cppcoreguidelines-*`, `modernize-*`, `performance-*`, `readability-*`
- **Configuration**: `.clang-tidy`
- **Fix automatically**: `./scripts/check-quality.sh --fix`

**Common issues to avoid:**
- Magic numbers (use named constants)
- Unnecessary copies (use `const&` or `std::move`)
- Missing `override` on virtual methods
- C-style casts (use `static_cast`, `dynamic_cast`)

### Linting (cppcheck)

- **Enabled checks**: All (with suppressions for known false positives)
- **Configuration**: `.cppcheck-suppressions`

### Code Coverage

- **Target**: 80%+ line coverage overall
- **Critical paths**: 90%+ coverage (storage, transactions, query engine)
- **Tool**: gcov/lcov

**Generate local coverage report:**

```bash
# After running tests with coverage
mkdir -p coverage
lcov --capture --directory build --output-file coverage/coverage.info
lcov --remove coverage/coverage.info '/usr/*' '*/vcpkg_installed/*' '*/tests/*' \
  --output-file coverage/coverage-filtered.info
genhtml coverage/coverage-filtered.info --output-directory coverage/html

# Open report
xdg-open coverage/html/index.html  # Linux
open coverage/html/index.html      # macOS
start coverage/html/index.html     # Windows
```

### Secret Scanning (Gitleaks)

- **Tool**: Gitleaks
- **Configuration**: `.gitleaks.toml`
- **Enforcement**: CI fails if secrets detected

**Avoid committing:**
- API keys
- Passwords
- Private keys
- Database credentials

**Use instead:**
- Environment variables
- `.env.example` templates (not `.env`)
- Configuration placeholders

### Code Style

**Naming conventions:**
- **Namespaces**: `lower_case` (e.g., `vccdb`, `themis`)
- **Classes/Structs**: `CamelCase` (e.g., `StorageEngine`, `HttpServer`)
- **Functions**: `camelCase` (e.g., `getValue`, `processQuery`)
- **Variables**: `lower_case` (e.g., `table_name`, `max_size`)
- **Private members**: `lower_case_` (e.g., `db_path_`, `cache_size_`)
- **Constants**: `UPPER_CASE` (e.g., `MAX_CONNECTIONS`, `DEFAULT_PORT`)

**File structure:**
- Header files: `include/<module>/<name>.h`
- Source files: `src/<module>/<name>.cpp`
- Tests: `tests/test_<module>_<feature>.cpp`

**Comments:**
- Use `//` for single-line comments
- Use `/** ... */` for documentation comments (Doxygen style)
- Explain **why**, not **what** (code should be self-documenting)

## Pull Request Process

### Before Submitting

1. **Run all checks locally:**
   ```bash
   ./scripts/check-quality.sh
   ```

2. **Ensure all tests pass:**
   ```bash
   cd build
   ctest --output-on-failure
   ```

3. **Update documentation:**
   - Add/update relevant `.md` files in `docs/`
   - Update `README.md` if needed
   - Add docstrings for new public APIs

4. **Add tests:**
   - Unit tests for new functions/classes
   - Integration tests for new features
   - Update existing tests if behavior changes

### PR Description Template

```markdown
## Description
Brief description of changes

## Type of Change
- [ ] Bug fix (non-breaking change which fixes an issue)
- [ ] New feature (non-breaking change which adds functionality)
- [ ] Breaking change (fix or feature that would cause existing functionality to not work as expected)
- [ ] Documentation update

## How Has This Been Tested?
Describe the tests you ran to verify your changes.

## Checklist
- [ ] My code follows the code style of this project
- [ ] I have performed a self-review of my own code
- [ ] I have commented my code, particularly in hard-to-understand areas
- [ ] I have made corresponding changes to the documentation
- [ ] My changes generate no new warnings
- [ ] I have added tests that prove my fix is effective or that my feature works
- [ ] New and existing unit tests pass locally with my changes
- [ ] Any dependent changes have been merged and published
- [ ] I have run `./scripts/check-quality.sh` and fixed all issues
```

### Review Process

1. **Automated checks** run on all PRs:
   - Build (Linux + Windows)
   - Tests
   - Clang-tidy static analysis
   - Cppcheck linting
   - Code coverage
   - Gitleaks secret scanning

2. **Maintainer review**:
   - Code quality and style
   - Test coverage
   - Documentation
   - Architecture fit

3. **Address feedback**:
   - Make requested changes
   - Push updates to the same branch
   - Request re-review

4. **Merge**:
   - Once approved and all checks pass
   - Maintainer will merge (squash or merge commit)

## Reporting Bugs

**Before submitting a bug report:**

1. **Check existing issues**: Search for similar reports
2. **Verify it's a bug**: Ensure it's not expected behavior
3. **Test on latest version**: Bug may already be fixed

**Bug report template:**

```markdown
## Description
Clear description of the bug

## Steps to Reproduce
1. Step one
2. Step two
3. ...

## Expected Behavior
What you expected to happen

## Actual Behavior
What actually happened

## Environment
- OS: [e.g., Ubuntu 22.04, Windows 11]
- Compiler: [e.g., GCC 11.2, MSVC 2022]
- ThemisDB version/commit: [e.g., v1.0.0 or commit hash]

## Additional Context
Logs, screenshots, or other relevant information
```

## Feature Requests

**Feature request template:**

```markdown
## Feature Description
Clear description of the proposed feature

## Motivation
Why is this feature needed? What problem does it solve?

## Proposed Solution
How should this feature work?

## Alternatives Considered
Other approaches you've thought about

## Additional Context
Any other relevant information
```

## Documentation

- **Architecture docs**: `docs/architecture.md`, `docs/design/`
- **API docs**: Generated from code comments, `docs/apis/`
- **User guides**: `docs/*.md`
- **Deployment**: `docs/deployment.md`

**Updating documentation:**
- Keep docs in sync with code changes
- Use clear, concise language
- Include code examples where appropriate
- Update `mkdocs.yml` if adding new pages

## Questions?

- **GitHub Discussions**: For general questions and discussions
- **GitHub Issues**: For bug reports and feature requests
- **Documentation**: Check `docs/` for detailed guides

## License

By contributing to ThemisDB, you agree that your contributions will be licensed under the MIT License.

---

Thank you for contributing to ThemisDB! 🚀
