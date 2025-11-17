# aql_parser.cpp

Path: `src/query/aql_parser.cpp`

Purpose: Parses AQL queries into AST. Should include grammar details and handling of FULLTEXT tokens.

Public functions / symbols:
- ``
- `if (token.type != TokenType::INVALID) {`
- `if (ch == '
') {`
- `if (ch == '"' || ch == '\'') {`
- `if (quote != '"' && quote != '\'') {`
- `switch (next) {`
- `switch (ch) {`
- `for (const auto& token : tokens_) {`
- `if (token.type == TokenType::INVALID) {`
- `if (lastTraversal_) {`
- `while (true) {`
- `if constexpr (std::is_same_v<T, std::nullptr_t>) {`
- `switch (op) {`
- `for (const auto& arg : arguments) {`
- `for (const auto& elem : elements) {`
- `for (const auto& [key, value] : fields) {`
- `skipWhitespace();`
- `advance();`
- `return readString(start_line, start_column);`
- `return readNumber(start_line, start_column);`
- `return readIdentifierOrKeyword(start_line, start_column);`
- `return readOperatorOrPunctuation(start_line, start_column);`
- `advance(); // Skip opening quote`
- `advance(); // Skip closing quote`
- `advance(); advance();`
- `return Token(TokenType::INVALID, "===", line, col);`
- `return Token(TokenType::EQ, "==", line, col);`
- `return Token(TokenType::NEQ, "!=", line, col);`
- `return Token(TokenType::LTE, "<=", line, col);`
- `return Token(TokenType::GTE, ">=", line, col);`
- `expect(TokenType::END_OF_FILE, "Expected end of query");`
- `expect(TokenType::FOR, "Expected FOR");`
- `advance(); // first '.'`
- `advance(); // second '.'`
- `expect(TokenType::GRAPH, "Expected GRAPH keyword in traversal");`
- `expect(TokenType::LET, "Expected LET");`
- `expect(TokenType::ASSIGN, "Expected '=' after variable name in LET");`
- `expect(TokenType::FILTER, "Expected FILTER");`
- `expect(TokenType::SORT, "Expected SORT");`
- `expect(TokenType::COMMA, "Expected comma");`
- `expect(TokenType::LIMIT, "Expected LIMIT");`
- `expect(TokenType::RETURN, "Expected RETURN");`
- `expect(TokenType::COLLECT, "Expected COLLECT");`
- `expect(TokenType::ASSIGN, "Expected '=' after group variable in COLLECT");`
- `expect(TokenType::ASSIGN, "Expected '=' in aggregation assignment");`
- `expect(TokenType::LPAREN, "Expected '(' after aggregation function");`
- `return parseLogicalOr();`
- `expect(TokenType::COLON, "Expected ':' after object key");`
- `expect(TokenType::RBRACE, "Expected '}' at end of object");`
- `advance(); // empty object {}`

