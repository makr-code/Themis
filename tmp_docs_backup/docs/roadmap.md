# Roadmap

Diese Roadmap skizziert priorisierte Vorhaben für ThemisDB. Zeitpläne sind indikativ; Änderungen ergeben sich aus Feedback und Prioritäten.

## Kurzfristig (0–1 Quartal)

- RBAC/Policies für Admin- und Datenendpunkte (Scopes, API-Keys)
- VectorIndex: HNSW Persistenz/Recovery-Härtung, Warmstart-Optimierungen
- CDC/SSE: Skalierung und Backpressure (Proxy/Ingress-Guidelines), Reconnect-Strategien
- Backup/Restore: Inkrementelle Backups und Automatisierung (systemd/K8s CronJobs)
- CI: clang-tidy/cppcheck Gates, Coverage-Reporting, Secrets-Scanning (gitleaks)

## Mittelfristig (1–3 Quartale)

- Query Engine: Join-Optimierungen, Kostenmodell verfeinern, Statistiken/Histograms
- Indexe: Kompakte Fulltext-Indexierung, Geo-Verbesserungen, progressives Reindexing
- Sicherheit: Externe KMS-Integration (Vault/AWS KMS), Key-Rotation APIs
- Speicherformate: Binary-Format Spezifikation stabilisieren, Zero-Copy-Reads ausbauen
- Observability: Mehr Metriken (Abfrage-Latenzen pro Typ), Trace-Sampling Regeln

## Langfristig (3+ Quartale)

- Verteilte Replikation (Leader/Follower), Konsistenzmodi, Leseskalierung
- Multi-Tenancy mit Quotas/Isolation
- GNN/Hybrid-Search Pipelines (Online/Offline) inkl. Feature Store Hooks
- Policy-Engine (ABAC) und Compliance-Vorlagen (GDPR/ISO)

## Angenommene Risiken und Gegenmaßnahmen

- Performance-Regressionen: Regelmäßige Benchmarks, Budget für Optimierungssprints
- Sicherheit: Security Reviews pro Release, Pen-Tests bei größeren Änderungen
- Komplexität: Modulare Architektur, klare Verantwortlichkeiten, Dokumentation aktuell halten
