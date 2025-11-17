# jwt_validator.cpp

Path: `src/auth/jwt_validator.cpp`

Purpose: Validate and parse JWT tokens; extract claims for authentication/authorization.

Public functions / symbols:
- `if (claims.sub == encryption_context) {`
- `for (const auto& group : claims.groups) {`
- `if (group == encryption_context) {`
- `BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);`
- `BIO_free_all(bio);`
- `std::stringstream ss(jwt);`

Notes / TODOs:
- Document accepted signing algorithms, key rotation, JWKS support.
