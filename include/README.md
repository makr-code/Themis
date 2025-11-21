# ThemisDB Header Files

This directory contains the public header files and interfaces for ThemisDB components.

## Directory Structure

The headers are organized to mirror the source code structure:

- **acceleration/** - GPU and hardware acceleration interfaces
- **api/** - API server interfaces and HTTP request/response structures
- **auth/** - Authentication and authorization interfaces
- **cache/** - Caching interfaces and abstractions
- **cdc/** - Change Data Capture interfaces
- **content/** - Content management interfaces
- **document/** - Document storage interfaces
- **exporters/** - Data export interfaces
- **geo/** - Geospatial processing interfaces
- **governance/** - Policy engine and governance interfaces
- **importers/** - Data import interfaces
- **index/** - Index interfaces (vector, graph, secondary)
- **llm/** - LLM interaction interfaces
- **plugins/** - Plugin system interfaces
- **query/** - Query engine and AQL parser interfaces
- **security/** - Security, encryption, and key management interfaces
- **server/** - Server component interfaces
- **sharding/** - Sharding and horizontal scaling interfaces
- **storage/** - Storage layer interfaces
- **timeseries/** - Time series data interfaces
- **transaction/** - Transaction and SAGA interfaces
- **utils/** - Utility interfaces and helpers

## Usage

Include headers using the `#include <themisdb/component/header.hpp>` pattern.

## Documentation

For detailed API documentation, see the [API documentation](../docs/api/).
