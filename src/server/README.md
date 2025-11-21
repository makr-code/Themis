# Server Module

Main server components and API handlers for ThemisDB.

## Components

- HTTP server implementation
- API handlers (audit, classification, keys, PII, policy, reports, retention, SAGA)
- Authentication middleware
- SSE connection manager
- Ranger adapter

## Features

- RESTful API endpoints
- Server-Sent Events (SSE) for changefeeds
- JWT authentication middleware
- Integration with Apache Ranger
- API request/response handling

## Documentation

For server documentation, see:
- [HTTP Server](../../docs/src/server/http_server.cpp.md)
- [Audit API Handler](../../docs/src/server/audit_api_handler.cpp.md)
- [Auth Middleware](../../docs/src/server/auth_middleware.cpp.md)
- [Classification API Handler](../../docs/src/server/classification_api_handler.cpp.md)
- [Keys API Handler](../../docs/src/server/keys_api_handler.cpp.md)
- [PII API Handler](../../docs/src/server/pii_api_handler.cpp.md)
- [Policy Engine](../../docs/src/server/policy_engine.cpp.md)
- [SAGA API Handler](../../docs/src/server/saga_api_handler.cpp.md)
- [SSE Connection Manager](../../docs/src/server/sse_connection_manager.cpp.md)
