````markdown
---
title: Changefeed OpenAPI - Auth Examples
---

Zweck: Beispiele für die Ergänzung der Changefeed OpenAPI‑Snippets um Security Schemes (JWT Bearer, API Key).

1) Security Schemes Snippet

```yaml
components:
  securitySchemes:
    bearerAuth:
      type: http
      scheme: bearer
      bearerFormat: JWT
    apiKeyAuth:
      type: apiKey
      in: header
      name: X-API-Key

security:
  - bearerAuth: []

```

2) Endpoint example with security requirement

```yaml
/changefeed:
  get:
    security:
      - bearerAuth: []
    summary: List changefeed events (requires JWT)
    ...
```

3) Notes

- Use `bearerAuth` for user‑scoped access and `apiKeyAuth` for service/system clients.
- For SSE endpoints ensure auth is validated at connection time and that token refresh is possible for long‑running clients (reconnect and resume by seq).

````
