# Inkrementelle Backups & WAL-Archiving

Ziel: Implementiere inkrementelle Backups und WAL-Archiving/Restore für ThemisDB.

Aufwandsschätzung: 2–3 Tage

DoD:
- CLI/HTTP-Schnittstelle zum Triggern von inkrementellen Backups
- WAL-Archivierung (rotierende Archive), Wiederherstellungs-Workflow getestet
- Automatisierte Tests: Backup → WAL-Append → Restore → Datenintegrität geprüft
- Dokumentation mit Beispielbefehlen und Troubleshooting

Vorgehen:
1. Prüfen: Implementierungsdetails von `src/storage/rocksdb_wrapper.cpp` und vorhandene Snapshot/Checkpoint-Funktionen.
2. Design: API (backup/start, backup/status, restore/start), Speicherort für Archive (S3/FS), Retention-Policy.
3. Implementierung: Lokales FS-Backend + optional S3-Upload-Adapter.
4. Tests: Unit + integration (small DB instance, insert data, rotate WAL, restore).
5. Doku: `docs/development/wal-archiving.md` mit Usage & Restore-Examples.

Risiken:
- Restore-Prozess kann DB-Format/Version-sensitiv sein; teste mit Version-Matrix.
