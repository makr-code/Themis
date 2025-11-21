# ThemisDB Documentation

Welcome to the ThemisDB documentation! This directory contains comprehensive documentation for all aspects of the ThemisDB multi-model database system.

## Quick Navigation

### 📋 Overview
- **[FEATURES.md](../FEATURES.md)** - **Comprehensive features list** with production-ready status indicators (✅/🔧/📋)

### 🚀 Getting Started
- [Main README](../README.md) - Quick start guide and basic usage
- [Architecture Overview](architecture.md) - High-level system architecture  
- [Deployment Guide](guides/deployment.md) - How to deploy ThemisDB
- [Operations Runbook](guides/operations_runbook.md) - Day-to-day operations

### 📖 Core Documentation

#### Query Language & Features
- **[AQL Documentation](aql/)** - ArangoDB Query Language support
  - [Syntax Reference](aql/syntax.md)
  - [Query Engine](aql/query_engine.md)
  - [Explain & Profile](aql/explain_profile.md)
  
- **[Search & Query](search/)** - Full-text and hybrid search
  - [Full-text API](search/fulltext_api.md)
  - [Hybrid Search](search/hybrid_search_design.md)
  - [Performance Tuning](search/performance_tuning.md)

- **[Features](features/)** - Database capabilities
  - [Indexes](features/indexes.md)
  - [Transactions](features/transactions.md)
  - [Time Series](features/time_series.md)
  - [Temporal Graphs](features/temporal_graphs.md)
  - [Vector Operations](features/vector_ops.md)
  - [Compliance](features/compliance.md)

#### Architecture & Design
- **[Architecture](architecture/)** - System design
  - [Ecosystem Overview](architecture/ecosystem_overview.md)
  - [Strategic Overview](architecture/strategic_overview.md)
  - [Content Architecture](architecture/content_architecture.md)
  - [MVCC Design](architecture/mvcc_design.md)

- **[Geospatial](geo/)** - Geo features
  - [Geo Architecture](geo/architecture.md)
  - [Integration Guide](geo/geo_integration_readme.md)

- **[Sharding](sharding/)** - Horizontal scaling
  - [Implementation Summary](sharding/implementation_summary.md)
  - [Horizontal Scaling Strategy](sharding/horizontal_scaling_strategy.md)

#### Security & Compliance
- **[Security](security/)** - Security features
  - [Overview](security/overview.md)
  - [Encryption](security/encryption_deployment.md)
  - [PKI Integration](security/pki_integration_architecture.md)
  - [Key Management](security/key_management.md)
  - [PII Detection](security/pii_detection.md)
  - [Audit & Retention](security/audit_and_retention.md)

### 🛠️ Development

- **[Development Guides](development/)** - For contributors
  - [Development Overview](development/README.md)
  - [Development Audit Log](development/auditlog.md)
  - [Implementation Status](development/implementation_status.md)
  - [Priorities](development/priorities.md)

- **[Source Code Documentation](src/)** - Code-level docs
  - Auto-generated documentation for all modules

### 🔧 Administration

- **[Admin Tools](admin_tools/)** - GUI administration tools
  - [User Guide](admin_tools/user_guide.md)
  - [Admin Guide](admin_tools/admin_guide.md)
  - [Feature Matrix](admin_tools/feature_matrix.md)

- **[Guides](guides/)** - Operations and setup
  - [TLS Setup](guides/tls_setup.md)
  - [Vault Integration](guides/vault.md)
  - [RBAC](guides/rbac.md)
  - [Code Quality](guides/code_quality.md)

- **[Performance](performance/)** - Performance optimization
  - [Benchmarks](performance/benchmarks.md)
  - [Memory Tuning](performance/memory_tuning.md)
  - [GPU Acceleration](performance/GPU_ACCELERATION_PLAN.md)

### 📦 Integration

- **[Client SDKs](clients/)** - Language-specific clients
  - [Python SDK](clients/python_sdk_quickstart.md)
  - [JavaScript SDK](clients/javascript_sdk_quickstart.md)
  - [Rust SDK](clients/rust_sdk_quickstart.md)

- **[API Documentation](api/)** - REST API reference
- **[Plugins](plugins/)** - Plugin development
  - [Plugin Security](plugins/PLUGIN_SECURITY.md)
  - [Plugin Migration](plugins/PLUGIN_MIGRATION.md)

- **[Exporters](exporters/)** & **[Importers](importers/)** - Data migration
  - [JSONL LLM Exporter](exporters/JSONL_LLM_EXPORTER.md)
  - [PostgreSQL Importer](importers/POSTGRES_IMPORTER.md)

### 📊 Reports & Planning

- **[Reports](reports/)** - Development reports and analysis
  - [Themis Implementation Summary](reports/themis_implementation_summary.md)
  - [Database Capabilities Roadmap](reports/database_capabilities_roadmap.md)
  - [Phase Reports](reports/) - Detailed phase completion reports
  - [Documentation Status](reports/DOCUMENTATION_SUMMARY.md)

- **[Roadmap](roadmap.md)** - Development roadmap

### 📚 Reference

- **[Glossary](glossary.md)** - Terminology reference
- **[Style Guide](guides/styleguide.md)** - Documentation standards
- **[Changelog](changelog.md)** - Version history
- **[Home](home.md)** - Documentation home

## Documentation Organization

```
docs/
├── admin_tools/          # Administration tool guides
├── aql/                  # AQL query language
├── api/                  # API documentation
├── architecture/         # System architecture
├── auth/                 # Authentication docs
├── clients/              # Client SDK guides
├── content/              # Content management
├── deployment/           # Deployment guides
├── development/          # Development documentation
├── exporters/            # Data export
├── features/             # Feature documentation
├── geo/                  # Geospatial features
├── guides/               # User and admin guides
├── importers/            # Data import
├── ingestion/            # Data ingestion
├── observability/        # Monitoring and metrics
├── performance/          # Performance tuning
├── plugins/              # Plugin development
├── query/                # Query features
├── release_notes/        # Release notes
├── reports/              # Status reports
├── search/               # Search features
├── security/             # Security documentation
├── sharding/             # Sharding and scaling
├── src/                  # Source code documentation
└── storage/              # Storage layer
```

## Contributing to Documentation

When adding or updating documentation:

1. **Follow the structure** - Place docs in the appropriate subdirectory
2. **Link properly** - Use relative links to other documentation
3. **Add to README** - Update relevant README.md files
4. **Use markdown** - Follow the [Style Guide](guides/styleguide.md)
5. **Keep it current** - Update docs when features change

## Documentation Standards

- **Format**: Markdown (.md)
- **Encoding**: UTF-8
- **Line endings**: LF (Unix-style)
- **Max line length**: None (wrap for readability)
- **Code blocks**: Always specify language
- **Links**: Use relative paths

## Building Documentation

The documentation can be built using MkDocs:

```powershell
# Install dependencies
pip install -r requirements-docs.txt

# Build documentation
.\build-docs.ps1

# Preview locally
mkdocs serve
```

Documentation is automatically deployed to GitHub Pages on merge to main.

## Getting Help

- **Issues**: [GitHub Issues](https://github.com/makr-code/ThemisDB/issues)
- **Wiki**: [GitHub Wiki](https://github.com/makr-code/ThemisDB/wiki)
- **Main README**: [Project README](../README.md)

## License

This documentation is part of ThemisDB and is licensed under the same terms. See [LICENSE](../LICENSE) for details.

---

**Last Updated**: November 2025
