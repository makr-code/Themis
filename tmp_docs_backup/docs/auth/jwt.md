# JWT / Auth Middleware

This document gives a short guide how the JWT validator and AuthMiddleware work in Themis, an example JWKS file, and how to test the middleware with curl/PowerShell.

## Overview

- `auth::JWTValidator` verifies RS256-signed JWTs against a JWKS endpoint or an injected JWKS. It validates signature (kid -> JWK), and standard claims: `exp`, `nbf`, `iss`, `aud`.
- `AuthMiddleware` uses `JWTValidator` to guard HTTP endpoints and expose the parsed claims to downstream handlers.

## Example JWKS

A minimal JWKS (file: `docs/auth/jwks_example.json`) looks like:

```json
{
  "keys": [
    {
      "kty": "RSA",
      "kid": "example-key-1",
      "use": "sig",
      "alg": "RS256",
      "n": "...base64url modulus...",
      "e": "AQAB"
    }
  ]
}
```

Replace `n` with the base64url-encoded RSA modulus of your public key. `e` is usually `AQAB`.

## Configuration snippet (C++)

JWT config is provided using `JWTValidatorConfig` (see `include/auth/jwt_validator.h`). Example:

- `jwks_url` (string): remote JWKS endpoint (optional during tests where JWKS are injected)
- `expected_issuer` (string)
- `expected_audience` (string)
- `jwks_cache_ttl` (seconds)
- `clock_skew` (seconds)

Example initializer:

```cpp
JWTValidatorConfig cfg;
cfg.jwks_url = "https://pki.example.com/.well-known/jwks.json";
cfg.expected_issuer = "https://auth.example.com";
cfg.expected_audience = "themis-api";
cfg.jwks_cache_ttl = std::chrono::seconds(300);
cfg.clock_skew = std::chrono::seconds(60);
JWTValidator validator(cfg);
```

## Testing locally

1) Unit tests

- The repo contains `tests/test_jwt_validator.cpp` with unit tests that generate RSA keys on the fly and inject JWKS via `setJWKSForTesting(...)`.
- Build and run only JWT tests:

```powershell
cmake --build C:\VCC\themis\build --config Release --target themis_tests
C:\VCC\themis\build\Release\themis_tests.exe --gtest_filter=JWTValidatorTest.*
```

2) Manual curl test (using a pre-generated JWT and JWKS hosted at `http://localhost:8000/jwks`):

- Start an HTTP server to serve `docs/auth/jwks_example.json` (for example `python -m http.server 8000` in that directory).
- Request to protected endpoint (example):

```bash
curl -H "Authorization: Bearer <JWT>" http://localhost:8080/api/protected
```

If the token is valid, the middleware forwards the request; otherwise it returns 401.

## Common gotchas

- The JWKS must include the correct `kid` that matches the JWT header.
- Use base64url (no padding) for `n` and `e` fields.
- `exp`/`nbf` are validated with a configurable clock skew (default 60s in tests).

## Next steps / TODO

- Add examples for JWKS rotation and multiple `kid` entries.
- Add an integration test that runs a small local server and verifies middleware with a real HTTP request.

