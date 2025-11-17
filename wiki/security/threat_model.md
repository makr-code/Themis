# Threat Model (light)

Ziel: Risiken sichtbar machen und mit pragmatischen Kontrollen adressieren.

## Assets
- Datenbankinhalte (Dokumente, Inhalte, Vektoren, Zeitreihen)
- Schlüsselmaterial (LEK/KEK/DEK)
- Audit‑Trails (Changefeed)

## Akteure
- Admin/Operator (berechtigt)
- Anwendung/Service (technisch)
- Angreifer extern/intern (unberechtigt/teilberechtigt)

## Vertrauensgrenzen
- Client ↔ Reverse‑Proxy ↔ Themis‑Server ↔ Storage (RocksDB)
- Externe Schlüsselverwaltung (Vault o. ä.)

## Hauptrisiken (Auszug)
- Unautorisierte Schlüsselrotation/Schlüsselabgriff
- Datenexfiltration über Admin‑APIs
- PII‑Leakage in Logs/Exports
- Manipulation Audit‑Trail

## Gegenmaßnahmen
- RBAC/Netzwerk‑Kontrollen vor Admin‑APIs (/keys/rotate, /changefeed/retention)
- TLS‑Terminations‑Proxy, mTLS optional
- Secrets‑Management (kein Klartext im Repo/Config)
- Minimierte Logs; Pseudonymisierung sensibler Werte
- Regelmäßige Rotation, Least‑Privilege, Vier‑Augen‑Prinzip bei kritischen Aktionen
- Backup/Restore mit Integritätsprüfungen

## Beobachtbarkeit
- Health/Metrics Endpunkte überwachen (/health, /metrics)
- Alarme für Schlüsselablauf, Retention‑Fehler, Anomalien im Changefeed

Weiterlesen:
- security/key_management.md, security/audit_and_retention.md
- encryption_strategy.md, security_hardening_guide.md, security_audit_checklist.md
