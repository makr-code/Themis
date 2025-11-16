Project: Themis (Database System)
Language: C++

Purpose:
- High-performance C++ vector database with RocksDB integration, AQL and MVCC.

What Copilot should help with:
- Generate idiomatic C++ code for core algorithms, concurrency control, and memory-safe structures.
- Suggest tests for correctness and performance; avoid unsafe patterns.

Coding style and constraints:
- Follow project C++ style; prefer modern C++ (C++17/20) features and RAII patterns.
- Document threading and locking choices in `docs/architecture.md`.

Documentation duties (./docs):
- Add `docs/design.md` describing AQL semantics, indexing strategy and storage layout.

Todo.md continuation:
- Add clear development tasks with benchmark targets and unit test coverage goals.

Examples for Copilot prompts:
- "Implement MVCC snapshot read path with minimal locking and add unit tests simulating concurrent writers."

Testing & CI:
- Provide unit tests and a micro-benchmark harness; CI should run static analysis and unit tests.
