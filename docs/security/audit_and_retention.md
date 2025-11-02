# Audit & Retention

Audit-Trails und Datenaufbewahrung sind Kernbausteine der Sicherheit & Compliance.

## Changefeed (Audit‑Trail)
- Append‑only Log aller Mutationen (PUT/DELETE)
- Endpunkte:
  - GET /changefeed – Events listen (Parameter: from_seq, limit, long_poll_ms, key_prefix)
  - GET /changefeed/stats – Gesamtereignisse, letzte Sequence, Größe
  - POST /changefeed/retention – Löschung bis Sequence: { "before_sequence": <uint64> }
  - GET /changefeed/stream – Server‑Sent Events (siehe APIs/SSE)
- Einsatzzwecke: Echtzeit‑Sync, Audit, Event Processing

Details: siehe docs/change_data_capture.md

## Retention‑Policies
- Changefeed: Sequenz‑basierte Bereinigung via /changefeed/retention
- Time‑Series: RetentionManager (per‑Metric Policies) – siehe docs/time_series.md
- Content/Entities: Fachliche Policies (z. B. DSGVO Art. 17) außerhalb des Changefeeds umsetzen

## Empfehlungen
- Minimale Aufbewahrungszeit für Audit‑Zwecke definieren (rechtlich/organisatorisch)
- Automatisierte Bereinigung (Cron/Jobs) etablieren; Metriken überwachen
- Export/Archivierung vor Löschung (WORM‑Storage optional)

Weiterlesen:
- change_data_capture.md
- compliance_audit.md, compliance_governance_strategy.md
