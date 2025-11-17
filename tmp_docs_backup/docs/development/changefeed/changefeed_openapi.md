```markdown
---
title: Changefeed OpenAPI Snippets
---

Zweck: Minimaler OpenAPI‑Ausschnitt zur Spezifikation der Changefeed‑MVP Endpoints. Diese Snippets sind für die Dokumentation und zum Einfügen in `openapi.yaml` gedacht.

1) GET /changefeed

paths:
  /changefeed:
    get:
      summary: Liste von Changefeed‑Events
      parameters:
        - name: from_seq
          in: query
          schema:
            type: integer
        - name: limit
          in: query
          schema:
            type: integer
            default: 100
        - name: long_poll_ms
          in: query
          schema:
            type: integer
            default: 0
        - name: key_prefix
          in: query
          schema:
            type: string
      responses:
        '200':
          description: Array of change events
          content:
            application/json:
              schema:
                type: object
                properties:
                  events:
                    type: array
                    items:
                      $ref: '#/components/schemas/ChangeEvent'
                  last_seq:
                    type: integer

2) GET /changefeed/stream (SSE)

  /changefeed/stream:
    get:
      summary: Server‑Sent Events stream of changes
      parameters:
        - name: from_seq
          in: query
          schema:
            type: integer
      responses:
        '200':
          description: text/event-stream (SSE)
          content:
            text/event-stream:
              schema:
                type: string

3) GET /changefeed/stats

  /changefeed/stats:
    get:
      summary: Changefeed statistics
      responses:
        '200':
          description: Stats
          content:
            application/json:
              schema:
                type: object
                properties:
                  last_seq:
                    type: integer
                  total_events:
                    type: integer

4) POST /changefeed/retention

  /changefeed/retention:
    post:
      summary: Admin: delete events up to seq
      requestBody:
        required: true
        content:
          application/json:
            schema:
              type: object
              properties:
                up_to_seq:
                  type: integer
      responses:
        '204':
          description: Deleted

Components:

components:
  schemas:
    ChangeEvent:
      type: object
      properties:
        seq:
          type: integer
        ts:
          type: string
          format: date-time
        key:
          type: string
        op:
          type: string
          description: one of 'insert' | 'update' | 'delete'
        value:
          type: object

Hinweis: Die OpenAPI‑Snippets sind minimal — Werte für Auth/Zugriffssteuerung sollten projektintern ergänzt werden.

```
