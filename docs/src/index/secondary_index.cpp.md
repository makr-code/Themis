# secondary_index.cpp

Path: `src/index/secondary_index.cpp`

Purpose: Secondary index implementation for non‑primary key attributes.

Public functions / symbols:
- `inline std::vector<uint8_t> toBytes(std::string_view sv) {`
- `if (c == ':' || c == '%') {`
- `for (const auto& s : configJson["stopwords"]) {`
- `catch (...) { THEMIS_WARN("put(tx): alte Entity für PK={} nicht deserialisierbar", pk); }`
- ``
- `catch (...) { THEMIS_WARN("erase(tx): alte Entity für PK={} nicht deserialisierbar", pk); }`
- `if (lastColon != std::string_view::npos) {`
- `if (existingPK != pk) {`
- `if (conflict) {`
- `if (pos == std::string::npos) {`
- `for (const auto& c : columns) {`
- `if (!maybe) {`
- `for (const auto& t : tokens) { if (!t.empty()) tf[t]++; }`
- `for (const auto& [token, count] : tf) {`
- `for (const auto& col : indexedCols) {`
- `if (extractedPK == pk) {`
- `for (const auto& rcol : rangeCols) {`
- `if (existingPK == pk) {`
- `for (const auto& scol : sparseCols) {`
- `for (const auto& gcol : geoCols) {`
- `for (const auto& tcol : ttlCols) {`
- `for (const auto& fcol : fulltextCols) {`
- `for (const auto& token : uniqueTokens) {`
- `if (!blob) {`
- `if (count >= maxProbe) {`
- `if (!includeLower) {`
- `if (includeUpper) {`
- `for (const auto& pk : sameValuePks) {`
- `if (pk > anchorPk) {`
- `if (*it < anchorPk) {`
- `if (!reversed) {`
- `for (const auto& pk : more) {`
- `if (lat >= minLat && lat <= maxLat && lon >= minLon && lon <= maxLon) {`
- `if (dist <= radiusKm) {`
- `if (st.ok) {`
- `if (c == '"') {`
- `if (in_quotes) {`
- `for (const auto& pk : intersectionSet) {`
- `for (auto ph : phrases) {`
- `for (auto& token : tokens) {`
- `if (firstColon != std::string::npos) {`
- `if (secondColon != std::string::npos) {`
- `if (thirdColon != std::string::npos) {`
- `if (!maybeVal) { if (!advance()) { aborted = true; return false; } return true; }`
- `for (const auto& token : tokens) {`
- `if (!maybeLat || !maybeLon) { if (!advance()) { aborted = true; return false; } return true; }`
- `for (const auto& col : columns) {`
- `catch (...) {`
- `if (extractedPK != pk) {`
- `THEMIS_WARN("put: Konnte alte Entity für PK={} nicht deserialisieren", pk);`

