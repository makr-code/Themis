# PII‑Erkennung & Klassifizierung

ThemisDB nutzt eine regelbasierte PII‑Erkennung (PIIDetector) zur Klassifizierung von Inhalten.

## Komponenten
- PIIDetector (Regex‑Engine): erkennt Entitäten wie EMAIL, PHONE, IBAN etc.
- Classification API: stellt Regeln bereit und ermöglicht Test‑Klassifikationen

## Server‑APIs
- GET /classification/rules – Liste aktiver Regeln
- POST /classification/test – Test einer Probe
  - Body: { "text": "...", "metadata": { ... } }
  - Response (Beispiel): { "classification": "CONFIDENTIAL", "confidence": 0.92, "detected_entities": [ { "type": "EMAIL", "value": "a@b.c" } ] }

Fehlerfälle:
- 400 Missing JSON body – Eingabe erforderlich
- 503 Classification API not available – PIIDetector nicht initialisiert

## Grenzen & Hinweise
- Regex‑basiert: Heuristiken, false positives/negatives möglich
- Kontextabhängige Klassifikation kann ergänzende Logik/Modelle benötigen
- Protokollierung sensibler Treffer nur pseudonymisiert/aggregiert ablegen

Weiterlesen:
- pii_detection_engines.md
- compliance_integration.md
