# Merge conflict report for branch: feature/complete-database-capabilities

**Generated on**: 2025-11-17T17:13:32.2995755+01:00

## Merge summary
Exit code: 2

```
error: Your local changes to the following files would be overwritten by merge:
  CHANGELOG.md CMakeLists.txt MANUAL_CHANGES_REQUIRED.txt README.md SECURITY_SPRINT_SUMMARY.md config/schemas/aql_request.json config/schemas/content_import.json config/schemas/pki_sign.json config/schemas/pki_verify.json docs/AUDIT_LOGGING.md docs/CERTIFICATE_PINNING.md docs/DATABASE_CAPABILITIES_ROADMAP.md docs/ENTERPRISE_INGESTION_INTERFACE.md docs/GEO_ARCHITECTURE.md docs/RBAC.md docs/SECURITY_IMPLEMENTATION_SUMMARY.md docs/THEMIS_IMPLEMENTATION_SUMMARY.md docs/TLS_SETUP.md docs/development/NEXT_STEPS_ANALYSIS.md docs/development/sprint_summary_2025-11-17.md docs/development/todo.md docs/eidas_qualified_signatures.md docs/encryption_metrics.md docs/hsm_integration.md docs/pki_integration_architecture.md docs/pki_signatures.md docs/security_hardening_guide.md include/index/graph_index.h include/index/spatial_index.h include/query/cte_subquery.h include/query/let_evaluator.h include/query/statistical_aggregator.h include/query/window_evaluator.h include/security/encryption.h include/security/hsm_provider.h include/security/rbac.h include/security/timestamp_authority.h include/server/http_server.h include/server/pki_api_handler.h include/server/rate_limiter.h include/storage/backup_manager.h include/storage/base_entity.h include/utils/audit_logger.h include/utils/geo/ewkb.h include/utils/input_validator.h include/utils/pki_client.h scripts/generate_test_certs.sh src/index/graph_index.cpp src/index/spatial_index.cpp src/query/aql_translator.cpp src/query/cte_subquery.cpp src/query/let_evaluator.cpp src/query/query_engine.cpp src/query/statistical_aggregator.cpp src/query/window_evaluator.cpp src/security/field_encryption.cpp src/security/hsm_provider.cpp src/security/rbac.cpp src/security/timestamp_authority.cpp src/server/http_server.cpp src/server/pki_api_handler.cpp src/server/rate_limiter.cpp src/storage/backup_manager.cpp src/storage/base_entity.cpp src/utils/audit_logger.cpp src/utils/geo/ewkb.cpp src/utils/input_validator.cpp src/utils/pki_client.cpp tests/geo/test_geo_ewkb.cpp tests/geo/test_spatial_index.cpp tests/test_aql_let.cpp tests/test_aql_or_not.cpp tests/test_hsm_provider.cpp tests/test_input_validator.cpp tests/test_lazy_reencryption.cpp tests/test_rate_limiter.cpp tests/test_schema_encryption.cpp tests/test_statistical_aggregations.cpp tests/test_timestamp_authority.cpp tests/test_vector_metadata_encryption_edge_cases.cpp tests/test_wal_backup_manager.cpp tests/test_window_functions.cpp
<stdin>:211: trailing whitespace.
    
<stdin>:214: trailing whitespace.
    
<stdin>:228: trailing whitespace.
        
<stdin>:322: trailing whitespace.
   
<stdin>:377: trailing whitespace.
- **New Implementation:** 
warning: squelched 1978 whitespace errors
warning: 1983 lines add whitespace errors.
Merge with strategy ort failed.
```
\n## Conflicting files
(Could not list conflicted files or none found)
\n## Git status (unmerged entries)
```

```
\n## Instructions for author
- Please rebase your branch onto main or resolve conflicts shown above.
- After resolving, push your branch and open a PR targeting main.
