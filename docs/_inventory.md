# Doku-Inventar (Stand: 2025-11-02)

Dieser Überblick listet vorhandene Inhalte, offene Dubletten/Kollisionen und Quick-Wins zur Konsolidierung.

## Wurzel (docs/)
- Architektur & Modell: `architecture.md`, `base_entity.md`, `property_graph_model.md`, `path_constraints.md`
- Query/AQL: `aql_syntax.md`, `aql_explain_profile.md`, `cursor_pagination.md`, `recursive_path_queries.md`, `temporal_graphs.md`, `temporal_time_range_queries.md`, `semantic_cache.md`
- Storage & MVCC: `time_series.md`, `mvcc_design.md`, `transactions.md`, `memory_tuning.md`, `compression_benchmarks.md`, `compression_strategy.md`, `chain_of_thought_storage.md`
- Indexe & Stats: `indexes.md`, `index_stats_maintenance.md`
- Content: `content_architecture.md`
- Sicherheit & Compliance: `encryption_strategy.md`, `encryption_deployment.md`, `column_encryption.md`, `pii_detection_engines.md`, `pii_engine_signing.md`, `security_hardening_guide.md`, `security_audit_checklist.md`, `compliance_audit.md`, `compliance_governance_strategy.md`, `compliance_integration.md`, `governance_usage.md`, `EXTENDED_COMPLIANCE_FEATURES.md`
- Deployment & Ops: `deployment.md`, `tracing.md`
- Admin-Tools: `admin_tools_*.md`
- Sonstiges: `sprint_a_plan.md`, `vector_ops.md`, `gnn_embeddings.md`, `openapi.yaml`

## Unterordner
- `content/`: `ingestion.md`, `geo_processor_design.md`, `image_processor_design.md`
- `ingestion/`: `json_ingestion_spec.md`
- `search/`: `hybrid_search_design.md`, `pagination_benchmarks.md`
- `storage/`: `geo_relational_schema.md`
- `security/`: (leer)

## Mögliche Dubletten/Kollisionen
- Change Data Capture: `cdc.md` vs. `change_data_capture.md` → zusammenführen (eine Datei, Kurzform alias im Navi)
- Compliance/ Governance: `compliance_audit.md`, `compliance_governance_strategy.md`, `compliance_integration.md`, `EXTENDED_COMPLIANCE_FEATURES.md`, `governance_usage.md` → bündeln in Kapitel mit Unterseiten; Duplizite Abschnitte prüfen
- Encryption: `encryption_strategy.md` vs. `encryption_deployment.md` vs. `column_encryption.md` → klare Abgrenzung (Strategie vs. Deployment vs. Feature)
- Time-Series: `time_series.md` referenziert alten API-Stand? → mit TSStore/API aktualisieren

## Quick-Wins (empfohlen zuerst)
1) `cdc.md` und `change_data_capture.md` konsolidieren → `change_data_capture.md` behalten, altes umleiten/verlinken
2) `time_series.md` um TSStore API, Aggregationen und Limitierungen ergänzen
3) `openapi.yaml` um Keys/Classification/Reports erweitern (siehe `docs/apis/openapi.md`)
4) `security/`-Ordner mit passenden Inhalten füllen oder entfernen (derzeit leer)
5) Admin-Tools: `admin_tools_*` um Screens/Flows aktualisieren

## Hinweise zur Navigation (mkdocs.yml)
- Navi ist erstellt; nach Konsolidierung Dateipfade ggf. anpassen
- Für OpenAPI-Render (Swagger/Redoc) später Plugin ergänzen
