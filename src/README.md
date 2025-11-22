# ThemisDB Source Code

This directory contains the core implementation of ThemisDB's multi-model database system.

## Directory Structure

### Core Components

- **acceleration/** - GPU and hardware acceleration implementations (CUDA, Vulkan)
- **api/** - HTTP API server implementation
- **auth/** - Authentication and authorization components (JWT, RBAC)
- **cache/** - Semantic caching and query result caching
- **cdc/** - Change Data Capture (CDC) and changefeed implementation
- **content/** - Content management, ingestion, and processing pipelines
- **exporters/** - Data export functionality (JSONL, LLM formats)
- **geo/** - Geospatial query processing and indexing
- **governance/** - Policy engine and compliance governance
- **importers/** - Data import functionality (PostgreSQL, etc.)
- **index/** - Index implementations (vector, graph, adaptive, secondary)
- **llm/** - LLM interaction storage and chain-of-thought features
- **plugins/** - Plugin system infrastructure
- **query/** - AQL query parser, optimizer, and execution engine
- **security/** - Encryption, key management, and PKI integration
- **server/** - Main server components and API handlers
- **sharding/** - Horizontal scaling and sharding implementation
- **storage/** - RocksDB wrapper and storage layer abstractions
- **timeseries/** - Time series data management and compression
- **transaction/** - SAGA pattern and transaction management
- **utils/** - Utility functions and shared components

## Building

See the main [README.md](../README.md) for build instructions.

## Architecture

For detailed architecture documentation, see:
- [Architecture Overview](../docs/architecture.md)
- [Source Code Documentation](../docs/src/README.md)
