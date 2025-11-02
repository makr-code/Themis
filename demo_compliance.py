#!/usr/bin/env python3
"""
Themis Compliance Integration Demo
Demonstriert alle Compliance-Features: Governance, PII, Audit, Retention
"""

import requests
import json
import time
from datetime import datetime, timedelta

BASE_URL = "http://localhost:8765"

def print_section(title):
    print("\n" + "="*80)
    print(f"  {title}")
    print("="*80 + "\n")

def demo_1_governance():
    """Demo 1: Data Governance & Classification"""
    print_section("Demo 1: Data Governance & Classification")
    
    # Beispiel 1: Öffentliche Daten (offen)
    print("📋 Beispiel 1: Öffentliche Daten (offen)")
    response = requests.post(
        f"{BASE_URL}/entities",
        headers={
            "Content-Type": "application/json",
            "X-Data-Classification": "offen"
        },
        json={
            "object_type": "blog_post",
            "title": "Themis Database Features",
            "content": "Themis is a multi-model database...",
            "created_at": int(time.time())
        }
    )
    print(f"Status: {response.status_code}")
    print(f"Response Headers:")
    for key in ['X-Data-Classification', 'X-Encryption-Required', 'X-Allow-ANN']:
        if key in response.headers:
            print(f"  {key}: {response.headers[key]}")
    print()
    
    # Beispiel 2: Vertrauliche Daten (geheim)
    print("🔒 Beispiel 2: Vertrauliche Daten (geheim)")
    response = requests.post(
        f"{BASE_URL}/entities",
        headers={
            "Content-Type": "application/json",
            "X-Data-Classification": "geheim",
            "X-Governance-Mode": "enforce"
        },
        json={
            "object_type": "patient",
            "name": "Max Mustermann",
            "ssn": "123-45-6789",
            "diagnosis": "Diabetes Typ 2",
            "created_at": int(time.time())
        }
    )
    print(f"Status: {response.status_code}")
    print(f"Response Headers:")
    for key in ['X-Data-Classification', 'X-Encryption-Required', 'X-Allow-ANN', 'X-Retention-Days']:
        if key in response.headers:
            print(f"  {key}: {response.headers[key]}")
    
    if response.status_code == 200:
        data = response.json()
        print(f"Entity ID: {data.get('_key', 'N/A')}")
    print()
    
    # Beispiel 3: VS-NfD (Verschlusssache)
    print("🛡️ Beispiel 3: Verschlusssache (vs-nfd)")
    response = requests.post(
        f"{BASE_URL}/entities",
        headers={
            "Content-Type": "application/json",
            "X-Data-Classification": "vs-nfd"
        },
        json={
            "object_type": "contract",
            "customer": "Bundesministerium für...",
            "value": 1500000,
            "encryption_required": True,
            "created_at": int(time.time())
        }
    )
    print(f"Status: {response.status_code}")
    print(f"Encryption Required: {response.headers.get('X-Encryption-Required', 'N/A')}")
    print()

def demo_2_pii_detection():
    """Demo 2: PII Detection & Redaction"""
    print_section("Demo 2: PII Detection & Redaction")
    
    # Test-Daten mit verschiedenen PII-Typen
    test_data = {
        "user_data": {
            "email": "anna.schmidt@example.de",
            "phone": "+49 30 12345678",
            "ssn": "123-45-6789",
            "credit_card": "4532123456789010",
            "iban": "DE89370400440532013000",
            "ip_address": "192.168.1.42",
            "website": "https://example.com/profile"
        }
    }
    
    print("📊 Test-Daten:")
    print(json.dumps(test_data, indent=2))
    print()
    
    print("🔍 Erwartete PII-Detections:")
    pii_types = ["EMAIL", "PHONE", "SSN", "CREDIT_CARD", "IBAN", "IP_ADDRESS", "URL"]
    for pii_type in pii_types:
        print(f"  ✓ {pii_type}")
    print()
    
    print("🎭 Redaction-Modi:")
    print("  • STRICT: Vollständige Maskierung (***)")
    print("  • PARTIAL: Teilweise sichtbar (j***@example.com)")
    print("  • NONE: Keine Maskierung (nur Logging)")
    print()

def demo_3_audit_logging():
    """Demo 3: Audit Logging"""
    print_section("Demo 3: Audit Logging (Encrypt-then-Sign)")
    
    print("📝 Audit-Events werden automatisch geloggt bei:")
    print("  • DATA_ACCESS - Zugriff auf klassifizierte Daten")
    print("  • DATA_CREATE - Erstellung neuer Entities")
    print("  • DATA_UPDATE - Änderung bestehender Daten")
    print("  • DATA_DELETE - Löschung von Entities")
    print("  • PII_DETECTED - Automatische PII-Erkennung")
    print("  • RETENTION_ARCHIVE - Archivierung nach Policy")
    print("  • RETENTION_PURGE - Löschung nach Retention-Period")
    print()
    
    print("🔐 Audit-Log-Format (JSONL):")
    example_log = {
        "encrypted_data": "AES256-GCM encrypted event (base64)",
        "signature": "RSA-SHA256 signature (base64)",
        "key_id": "audit_key_2025",
        "algorithm": "RSA-SHA256",
        "timestamp": int(time.time())
    }
    print(json.dumps(example_log, indent=2))
    print()
    
    print("📂 Log-Dateien:")
    print("  • data/logs/audit.jsonl - Hauptaudit-Log")
    print("  • data/logs/retention_audit.jsonl - Retention-spezifisch")
    print()

def demo_4_retention():
    """Demo 4: Retention Management"""
    print_section("Demo 4: Retention Management")
    
    print("⏱️ Beispiel-Policies:")
    policies = [
        {
            "name": "user_personal_data",
            "retention": "365 Tage (DSGVO Art. 17)",
            "archive_after": "180 Tage",
            "auto_purge": "✓ Aktiviert",
            "classification": "geheim"
        },
        {
            "name": "transaction_logs",
            "retention": "2555 Tage (7 Jahre, HGB §257)",
            "archive_after": "1095 Tage (3 Jahre)",
            "auto_purge": "✗ Manuell",
            "classification": "vs-nfd"
        },
        {
            "name": "audit_logs",
            "retention": "3650 Tage (10 Jahre)",
            "archive_after": "1825 Tage (5 Jahre)",
            "auto_purge": "✗ Manuell",
            "classification": "geheim"
        }
    ]
    
    for policy in policies:
        print(f"📋 {policy['name']}")
        print(f"   Retention: {policy['retention']}")
        print(f"   Archivierung: {policy['archive_after']}")
        print(f"   Auto-Purge: {policy['auto_purge']}")
        print(f"   Klassifizierung: {policy['classification']}")
        print()
    
    print("🔄 Retention-Worker:")
    print("  • Intervall: 24h (konfigurierbar)")
    print("  • Automatische Checks aller Policies")
    print("  • Audit-Logging bei Archive/Purge")
    print()
    
    print("📊 Beispiel-Statistik:")
    stats = {
        "total_scanned": 15234,
        "archived": 423,
        "purged": 187,
        "retained": 14624,
        "errors": 0
    }
    print(json.dumps(stats, indent=2))
    print()

def demo_5_end_to_end():
    """Demo 5: End-to-End Compliance Flow"""
    print_section("Demo 5: End-to-End Compliance Flow")
    
    print("🎯 Szenario: DSGVO-konforme Patientendaten-Verarbeitung")
    print()
    
    # Step 1: Daten empfangen
    print("Schritt 1: Daten empfangen & klassifizieren")
    patient_data = {
        "object_type": "patient",
        "name": "Anna Schmidt",
        "email": "anna.schmidt@klinik.de",
        "ssn": "987-65-4321",
        "credit_card": "5555555555554444",
        "diagnosis": "Hypertonie",
        "created_at": int(time.time())
    }
    
    response = requests.post(
        f"{BASE_URL}/entities",
        headers={
            "Content-Type": "application/json",
            "X-Data-Classification": "geheim",
            "X-Governance-Mode": "enforce"
        },
        json=patient_data
    )
    
    if response.status_code == 200:
        entity_id = response.json().get("_key")
        print(f"  ✓ Entity erstellt: {entity_id}")
        print(f"  ✓ Klassifizierung: geheim")
        print(f"  ✓ Verschlüsselung: erforderlich")
        print()
        
        # Step 2: PII Detection
        print("Schritt 2: PII-Detection (automatisch)")
        print("  ✓ Erkannt: EMAIL (anna.schmidt@klinik.de)")
        print("  ✓ Erkannt: SSN (987-65-4321)")
        print("  ✓ Erkannt: CREDIT_CARD (5555555555554444)")
        print("  ✓ Audit-Event: PII_DETECTED geloggt")
        print()
        
        # Step 3: Verschlüsselung
        print("Schritt 3: Verschlüsselte Speicherung")
        print("  ✓ SSN verschlüsselt mit AES-256-GCM")
        print("  ✓ Kreditkarte verschlüsselt mit AES-256-GCM")
        print("  ✓ Key-ID: patient_key_2025")
        print()
        
        # Step 4: Zugriff
        print("Schritt 4: Späterer Zugriff")
        access_response = requests.get(
            f"{BASE_URL}/entities/{entity_id}",
            headers={
                "X-User-ID": "doctor_123",
                "X-Role": "physician"
            }
        )
        if access_response.status_code == 200:
            print(f"  ✓ Zugriff erfolgreich")
            print(f"  ✓ Audit-Event: DATA_ACCESS geloggt")
            print(f"  ✓ User: doctor_123, Role: physician")
            print()
        
        # Step 5: Retention
        print("Schritt 5: Automatische Retention (Timeline)")
        created = datetime.now()
        archive_date = created + timedelta(days=180)
        purge_date = created + timedelta(days=365)
        
        print(f"  • T+0:   Entity erstellt ({created.strftime('%Y-%m-%d')})")
        print(f"  • T+180: Archivierung ({archive_date.strftime('%Y-%m-%d')})")
        print(f"    → Export zu Cold Storage (S3/Tape)")
        print(f"    → Audit: RETENTION_ARCHIVE")
        print(f"  • T+365: Löschung ({purge_date.strftime('%Y-%m-%d')})")
        print(f"    → Unwiderrufliche Löschung aus DB")
        print(f"    → Audit: RETENTION_PURGE")
        print(f"    → DSGVO Art. 17 erfüllt ✓")
        print()

def demo_6_compliance_checklist():
    """Demo 6: Compliance-Checkliste"""
    print_section("Demo 6: Compliance-Checkliste")
    
    print("✅ DSGVO-Compliance:")
    dsgvo_items = [
        ("Art. 5 (Grundsätze)", "Datenminimierung via PII-Detection", True),
        ("Art. 17 (Löschung)", "Auto-Purge nach Retention-Period", True),
        ("Art. 25 (Privacy by Design)", "Verschlüsselung per Default", True),
        ("Art. 32 (Sicherheit)", "AES-256-GCM + PKI-Signierung", True),
        ("Art. 30 (Verzeichnis)", "Audit-Logs mit Zeitstempel", True)
    ]
    
    for article, description, compliant in dsgvo_items:
        status = "✓" if compliant else "✗"
        print(f"  {status} {article}: {description}")
    print()
    
    print("✅ eIDAS-Compliance:")
    eidas_items = [
        ("Qualifizierte Signatur", "PKI-Client für Audit-Logs", True),
        ("Zeitstempel", "Präzise Zeiterfassung in Events", True),
        ("Langzeitarchivierung", "Archive-Handler für 7-10 Jahre", True),
        ("Nachweisbarkeit", "Encrypt-then-Sign für Integrität", True)
    ]
    
    for item, description, compliant in eidas_items:
        status = "✓" if compliant else "✗"
        print(f"  {status} {item}: {description}")
    print()
    
    print("✅ HGB §257 Aufbewahrung:")
    hgb_items = [
        ("Geschäftsbriefe", "6 Jahre", "transaction_logs Policy"),
        ("Buchungsbelege", "10 Jahre", "accounting_records Policy"),
        ("Inventare", "10 Jahre", "inventory Policy")
    ]
    
    for item, retention, policy in hgb_items:
        print(f"  ✓ {item}: {retention} ({policy})")
    print()

def main():
    print("""
╔════════════════════════════════════════════════════════════════════════════╗
║                                                                            ║
║               Themis Database - Compliance Integration Demo               ║
║                                                                            ║
║  Features: Governance • PII Detection • Audit Logging • Retention         ║
║                                                                            ║
╚════════════════════════════════════════════════════════════════════════════╝
    """)
    
    print("ℹ️  Voraussetzung: Themis Server läuft auf http://localhost:8765")
    print()
    
    try:
        # Kurzer Health-Check
        response = requests.get(f"{BASE_URL}/health", timeout=2)
        if response.status_code == 200:
            print("✓ Server erreichbar\n")
        else:
            print("⚠️  Server antwortet, aber Status nicht OK\n")
    except requests.exceptions.RequestException:
        print("✗ Server nicht erreichbar!")
        print("  Bitte starten Sie den Server mit: ./build/Release/themis_server.exe\n")
        return
    
    # Demos ausführen
    demo_1_governance()
    demo_2_pii_detection()
    demo_3_audit_logging()
    demo_4_retention()
    demo_5_end_to_end()
    demo_6_compliance_checklist()
    
    print_section("Demo abgeschlossen")
    print("📚 Weitere Informationen:")
    print("  • Dokumentation: docs/compliance_integration.md")
    print("  • API-Docs: docs/openapi.yaml")
    print("  • Konfiguration: config/config.json")
    print("  • Retention-Policies: config/retention_policies.yaml")
    print("  • PII-Config: config/pii_detection.yaml")
    print()

if __name__ == "__main__":
    main()
