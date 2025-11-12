#include "utils/normalizer.h"
// Secondary index implementation

#include "index/secondary_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/key_schema.h"
#include "storage/base_entity.h"
#include "utils/logger.h"
#include "utils/stemmer.h"
#include "utils/stopwords.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <chrono>

namespace themis {

namespace {
// Convert bytes to vector<uint8_t>
inline std::vector<uint8_t> toBytes(std::string_view sv) {
	return std::vector<uint8_t>(sv.begin(), sv.end());
}
} // namespace
// static
std::string SecondaryIndexManager::makeFulltextTFKey(std::string_view table, std::string_view column, std::string_view token, std::string_view pk) {
	std::string key = "fttf:";
	key += std::string(table);
	key += ":";
	key += std::string(column);
	key += ":";
	key += std::string(token);
	key += ":";
	key += std::string(pk);
	return key;
}

// static
std::string SecondaryIndexManager::makeFulltextDocLenKey(std::string_view table, std::string_view column, std::string_view pk) {
	std::string key = "ftdlen:";
	key += std::string(table);
	key += ":";
	key += std::string(column);
	key += ":";
	key += std::string(pk);
	return key;
}

// static
std::string SecondaryIndexManager::makeFulltextDocLenPrefix(std::string_view table, std::string_view column) {
	std::string key = "ftdlen:";
	key += std::string(table);
	key += ":";
	key += std::string(column);
	key += ":";
	return key;
}

SecondaryIndexManager::SecondaryIndexManager(RocksDBWrapper& db) : db_(db) {}

// static
std::string SecondaryIndexManager::makeIndexMetaKey(std::string_view table, std::string_view column) {
	return std::string("idxmeta:") + std::string(table) + ":" + std::string(column);
}

// static
std::string SecondaryIndexManager::makeCompositeIndexMetaKey(std::string_view table, const std::vector<std::string>& columns) {
	std::string key = std::string("idxmeta:") + std::string(table) + ":";
	for (size_t i = 0; i < columns.size(); ++i) {
		if (i > 0) key += "+";
		key += columns[i];
	}
	return key;
}

// static
std::string SecondaryIndexManager::makeIndexKey(std::string_view table, std::string_view column, std::string_view value, std::string_view pk) {
	return KeySchema::makeSecondaryIndexKey(table, column, value, pk);
}

// static
std::string SecondaryIndexManager::makeCompositeIndexKey(std::string_view table, const std::vector<std::string>& columns, const std::vector<std::string>& values, std::string_view pk) {
	// Format: idx:table:col1+col2:val1:val2:PK
	std::string key = "idx:" + std::string(table) + ":";
	for (size_t i = 0; i < columns.size(); ++i) {
		if (i > 0) key += "+";
		key += columns[i];
	}
	key += ":";
	for (size_t i = 0; i < values.size(); ++i) {
		key += encodeKeyComponent(values[i]);
		key += ":";
	}
	key += std::string(pk);
	return key;
}

// static
std::string SecondaryIndexManager::makeCompositeIndexPrefix(std::string_view table, const std::vector<std::string>& columns, const std::vector<std::string>& values) {
	// Gleich wie makeCompositeIndexKey aber ohne PK am Ende
	std::string key = "idx:" + std::string(table) + ":";
	for (size_t i = 0; i < columns.size(); ++i) {
		if (i > 0) key += "+";
		key += columns[i];
	}
	key += ":";
	for (size_t i = 0; i < values.size(); ++i) {
		key += encodeKeyComponent(values[i]);
		key += ":";
	}
	return key;
}

// static
std::string SecondaryIndexManager::encodeKeyComponent(std::string_view raw) {
	std::string out;
	out.reserve(raw.size());
	for (unsigned char c : raw) {
		if (c == ':' || c == '%') {
			char buf[4];
			snprintf(buf, sizeof(buf), "%%%02X", c);
			out.append(buf);
		} else {
			out.push_back(static_cast<char>(c));
		}
	}
	return out;
}

// static
std::string SecondaryIndexManager::makeRangeIndexMetaKey(std::string_view table, std::string_view column) {
	return std::string("ridxmeta:") + std::string(table) + ":" + std::string(column);
}

// static
std::string SecondaryIndexManager::makeSparseIndexMetaKey(std::string_view table, std::string_view column) {
	return std::string("sidxmeta:") + std::string(table) + ":" + std::string(column);
}

// static
std::string SecondaryIndexManager::makeGeoIndexMetaKey(std::string_view table, std::string_view column) {
	return std::string("gidxmeta:") + std::string(table) + ":" + std::string(column);
}

// static
std::string SecondaryIndexManager::makeTTLIndexMetaKey(std::string_view table, std::string_view column) {
	return std::string("ttlidxmeta:") + std::string(table) + ":" + std::string(column);
}

// static
std::string SecondaryIndexManager::makeFulltextIndexMetaKey(std::string_view table, std::string_view column) {
	return std::string("ftidxmeta:") + std::string(table) + ":" + std::string(column);
}

// static
std::string SecondaryIndexManager::makeRangeIndexKey(std::string_view table, std::string_view column, std::string_view value, std::string_view pk) {
	std::string key = "ridx:" + std::string(table) + ":" + std::string(column) + ":";
	key += encodeKeyComponent(value);
	key += ":";
	key += std::string(pk);
	return key;
}

// static
std::string SecondaryIndexManager::makeRangeIndexPrefix(std::string_view table, std::string_view column, std::string_view valuePrefix) {
	std::string key = "ridx:" + std::string(table) + ":" + std::string(column) + ":";
	if (!valuePrefix.empty()) {
		key += encodeKeyComponent(valuePrefix);
		key += ":";
	}
	return key;
}

// static
std::string SecondaryIndexManager::makeSparseIndexKey(std::string_view table, std::string_view column, std::string_view value, std::string_view pk) {
	std::string key = "sidx:" + std::string(table) + ":" + std::string(column) + ":";
	key += encodeKeyComponent(value);
	key += ":";
	key += std::string(pk);
	return key;
}

// static
std::string SecondaryIndexManager::makeGeoIndexKey(std::string_view table, std::string_view column, std::string_view geohash, std::string_view pk) {
	std::string key = "gidx:" + std::string(table) + ":" + std::string(column) + ":";
	key += std::string(geohash);
	key += ":";
	key += std::string(pk);
	return key;
}

// static
std::string SecondaryIndexManager::makeGeoIndexPrefix(std::string_view table, std::string_view column, std::string_view geohashPrefix) {
	std::string key = "gidx:" + std::string(table) + ":" + std::string(column) + ":";
	if (!geohashPrefix.empty()) {
		key += std::string(geohashPrefix);
	}
	return key;
}

// static
std::string SecondaryIndexManager::makeTTLIndexKey(std::string_view table, std::string_view column, int64_t expireTimestamp, std::string_view pk) {
	// Format: ttlidx:table:column:timestamp:PK
	// timestamp wird mit führenden Nullen auf 20 Zeichen padded für lexikografische Sortierung
	char buf[32];
	snprintf(buf, sizeof(buf), "%020lld", (long long)expireTimestamp);
	std::string key = "ttlidx:" + std::string(table) + ":" + std::string(column) + ":" + buf + ":" + std::string(pk);
	return key;
}

// static
std::string SecondaryIndexManager::makeTTLIndexPrefix(std::string_view table, std::string_view column) {
	return "ttlidx:" + std::string(table) + ":" + std::string(column) + ":";
}

// static
std::string SecondaryIndexManager::makeFulltextIndexKey(std::string_view table, std::string_view column, std::string_view token, std::string_view pk) {
	std::string key = "ftidx:" + std::string(table) + ":" + std::string(column) + ":";
	key += encodeKeyComponent(token);
	key += ":";
	key += std::string(pk);
	return key;
}

// static
std::string SecondaryIndexManager::makeFulltextIndexPrefix(std::string_view table, std::string_view column, std::string_view token) {
	std::string key = "ftidx:" + std::string(table) + ":" + std::string(column) + ":";
	if (!token.empty()) {
		key += encodeKeyComponent(token) + ":";
	}
	return key;
}

SecondaryIndexManager::Status SecondaryIndexManager::createIndex(std::string_view table, std::string_view column, bool unique) {
	if (table.empty() || column.empty()) {
		return Status::Error("createIndex: table/column darf nicht leer sein");
	}
	if (std::string(table).find(':') != std::string::npos || std::string(column).find(':') != std::string::npos) {
		return Status::Error("createIndex: ':' ist in table/column nicht erlaubt");
	}

	std::string metaKey = makeIndexMetaKey(table, column);
	std::string metaValue = unique ? "unique" : "";
	std::vector<uint8_t> marker(metaValue.begin(), metaValue.end());
	if (!db_.put(metaKey, marker)) {
		return Status::Error("createIndex: Schreiben des Metaschlüssels fehlgeschlagen: " + metaKey);
	}
	THEMIS_INFO("Index erstellt: {}.{} (unique={})", table, column, unique);
	return Status::OK();
}

SecondaryIndexManager::Status SecondaryIndexManager::createCompositeIndex(std::string_view table, const std::vector<std::string>& columns, bool unique) {
	if (table.empty() || columns.empty()) {
		return Status::Error("createCompositeIndex: table/columns darf nicht leer sein");
	}
	if (columns.size() < 2) {
		return Status::Error("createCompositeIndex: mindestens 2 Spalten erforderlich (nutze createIndex für Single-Column)");
	}
	for (const auto& col : columns) {
		if (col.empty() || col.find(':') != std::string::npos || col.find('+') != std::string::npos) {
			return Status::Error("createCompositeIndex: ungültiger Spaltenname: " + col);
		}
	}
	
	std::string metaKey = makeCompositeIndexMetaKey(table, columns);
	std::string metaValue = unique ? "unique" : "";
	std::vector<uint8_t> marker(metaValue.begin(), metaValue.end());
	if (!db_.put(metaKey, marker)) {
		return Status::Error("createCompositeIndex: Schreiben des Metaschlüssels fehlgeschlagen: " + metaKey);
	}
	std::string colList;
	for (size_t i = 0; i < columns.size(); ++i) {
		if (i > 0) colList += ", ";
		colList += columns[i];
	}
	THEMIS_INFO("Composite Index erstellt: {}.{{{}}} (unique={})", table, colList, unique);
	return Status::OK();
}

SecondaryIndexManager::Status SecondaryIndexManager::dropIndex(std::string_view table, std::string_view column) {
	if (table.empty() || column.empty()) {
		return Status::Error("dropIndex: table/column darf nicht leer sein");
	}
	std::string metaKey = makeIndexMetaKey(table, column);
	if (!db_.del(metaKey)) {
		return Status::Error("dropIndex: Löschen des Metaschlüssels fehlgeschlagen: " + metaKey);
	}
	THEMIS_INFO("Index gelöscht: {}.{}", table, column);
	return Status::OK();
}

SecondaryIndexManager::Status SecondaryIndexManager::dropCompositeIndex(std::string_view table, const std::vector<std::string>& columns) {
	if (table.empty() || columns.empty()) {
		return Status::Error("dropCompositeIndex: table/columns darf nicht leer sein");
	}
	std::string metaKey = makeCompositeIndexMetaKey(table, columns);
	if (!db_.del(metaKey)) {
		return Status::Error("dropCompositeIndex: Löschen des Metaschlüssels fehlgeschlagen: " + metaKey);
	}
	std::string colList;
	for (size_t i = 0; i < columns.size(); ++i) {
		if (i > 0) colList += ", ";
		colList += columns[i];
	}
	THEMIS_INFO("Composite Index gelöscht: {}.{{{}}}", table, colList);
	return Status::OK();
}

bool SecondaryIndexManager::hasIndex(std::string_view table, std::string_view column) const {
	std::string metaKey = makeIndexMetaKey(table, column);
	return db_.get(metaKey).has_value();
}

bool SecondaryIndexManager::hasCompositeIndex(std::string_view table, const std::vector<std::string>& columns) const {
	std::string metaKey = makeCompositeIndexMetaKey(table, columns);
	return db_.get(metaKey).has_value();
}

SecondaryIndexManager::Status SecondaryIndexManager::createRangeIndex(std::string_view table, std::string_view column) {
	if (table.empty() || column.empty()) return Status::Error("createRangeIndex: table/column darf nicht leer sein");
	if (std::string(table).find(':') != std::string::npos || std::string(column).find(':') != std::string::npos) {
		return Status::Error("createRangeIndex: ':' ist in table/column nicht erlaubt");
	}
	std::string metaKey = makeRangeIndexMetaKey(table, column);
	std::vector<uint8_t> marker = {1};
	if (!db_.put(metaKey, marker)) return Status::Error("createRangeIndex: Schreiben des Metaschlüssels fehlgeschlagen: " + metaKey);
	THEMIS_INFO("Range Index erstellt: {}.{}", table, column);
	return Status::OK();
}

SecondaryIndexManager::Status SecondaryIndexManager::dropRangeIndex(std::string_view table, std::string_view column) {
	if (table.empty() || column.empty()) return Status::Error("dropRangeIndex: table/column darf nicht leer sein");
	std::string metaKey = makeRangeIndexMetaKey(table, column);
	if (!db_.del(metaKey)) return Status::Error("dropRangeIndex: Löschen des Metaschlüssels fehlgeschlagen: " + metaKey);
	THEMIS_INFO("Range Index gelöscht: {}.{}", table, column);
	return Status::OK();
}

bool SecondaryIndexManager::hasRangeIndex(std::string_view table, std::string_view column) const {
	std::string metaKey = makeRangeIndexMetaKey(table, column);
	return db_.get(metaKey).has_value();
}

// ────────────────────────────────────────────────────────────────────────────
// Sparse-Index: überspringt NULL/leere Werte
// ────────────────────────────────────────────────────────────────────────────

SecondaryIndexManager::Status SecondaryIndexManager::createSparseIndex(std::string_view table, std::string_view column, bool unique) {
	if (table.empty() || column.empty()) return Status::Error("createSparseIndex: table/column darf nicht leer sein");
	if (table.find(':') != std::string::npos || column.find(':') != std::string::npos) {
		return Status::Error("createSparseIndex: ':' ist in table/column nicht erlaubt");
	}
	std::string metaKey = makeSparseIndexMetaKey(table, column);
	std::string marker = unique ? "unique" : "";
	std::vector<uint8_t> markerBytes(marker.begin(), marker.end());
	if (!db_.put(metaKey, markerBytes)) return Status::Error("createSparseIndex: Schreiben des Metaschlüssels fehlgeschlagen: " + metaKey);
	THEMIS_INFO("Sparse Index erstellt: {}.{} (unique={})", table, column, unique);
	return Status::OK();
}

SecondaryIndexManager::Status SecondaryIndexManager::dropSparseIndex(std::string_view table, std::string_view column) {
	if (table.empty() || column.empty()) return Status::Error("dropSparseIndex: table/column darf nicht leer sein");
	std::string metaKey = makeSparseIndexMetaKey(table, column);
	if (!db_.del(metaKey)) return Status::Error("dropSparseIndex: Löschen des Metaschlüssels fehlgeschlagen: " + metaKey);
	THEMIS_INFO("Sparse Index gelöscht: {}.{}", table, column);
	return Status::OK();
}

bool SecondaryIndexManager::hasSparseIndex(std::string_view table, std::string_view column) const {
	std::string metaKey = makeSparseIndexMetaKey(table, column);
	return db_.get(metaKey).has_value();
}

// ────────────────────────────────────────────────────────────────────────────
// Geo-Index: GeoJSON-Punkt-Speicherung mit Geohash
// ────────────────────────────────────────────────────────────────────────────

SecondaryIndexManager::Status SecondaryIndexManager::createGeoIndex(std::string_view table, std::string_view column) {
	if (table.empty() || column.empty()) return Status::Error("createGeoIndex: table/column darf nicht leer sein");
	if (table.find(':') != std::string::npos || column.find(':') != std::string::npos) {
		return Status::Error("createGeoIndex: ':' ist in table/column nicht erlaubt");
	}
	std::string metaKey = makeGeoIndexMetaKey(table, column);
	std::string marker = "geo";
	std::vector<uint8_t> markerBytes(marker.begin(), marker.end());
	if (!db_.put(metaKey, markerBytes)) return Status::Error("createGeoIndex: Schreiben des Metaschlüssels fehlgeschlagen: " + metaKey);
	THEMIS_INFO("Geo Index erstellt: {}.{}", table, column);
	return Status::OK();
}

SecondaryIndexManager::Status SecondaryIndexManager::dropGeoIndex(std::string_view table, std::string_view column) {
	if (table.empty() || column.empty()) return Status::Error("dropGeoIndex: table/column darf nicht leer sein");
	std::string metaKey = makeGeoIndexMetaKey(table, column);
	if (!db_.del(metaKey)) return Status::Error("dropGeoIndex: Löschen des Metaschlüssels fehlgeschlagen: " + metaKey);
	THEMIS_INFO("Geo Index gelöscht: {}.{}", table, column);
	return Status::OK();
}

bool SecondaryIndexManager::hasGeoIndex(std::string_view table, std::string_view column) const {
	std::string metaKey = makeGeoIndexMetaKey(table, column);
	return db_.get(metaKey).has_value();
}

// ────────────────────────────────────────────────────────────────────────────
// TTL-Index
// ────────────────────────────────────────────────────────────────────────────

SecondaryIndexManager::Status SecondaryIndexManager::createTTLIndex(std::string_view table, std::string_view column, int64_t ttl_seconds) {
	if (table.empty() || column.empty()) return Status::Error("createTTLIndex: table/column darf nicht leer sein");
	if (ttl_seconds <= 0) return Status::Error("createTTLIndex: ttl_seconds muss > 0 sein");
	if (table.find(':') != std::string::npos || column.find(':') != std::string::npos) {
		return Status::Error("createTTLIndex: ':' ist in table/column nicht erlaubt");
	}
	std::string metaKey = makeTTLIndexMetaKey(table, column);
	std::string ttlValue = std::to_string(ttl_seconds);
	std::vector<uint8_t> ttlBytes(ttlValue.begin(), ttlValue.end());
	if (!db_.put(metaKey, ttlBytes)) return Status::Error("createTTLIndex: Schreiben des Metaschlüssels fehlgeschlagen: " + metaKey);
	THEMIS_INFO("TTL Index erstellt: {}.{} (TTL={}s)", table, column, ttl_seconds);
	return Status::OK();
}

SecondaryIndexManager::Status SecondaryIndexManager::dropTTLIndex(std::string_view table, std::string_view column) {
	if (table.empty() || column.empty()) return Status::Error("dropTTLIndex: table/column darf nicht leer sein");
	std::string metaKey = makeTTLIndexMetaKey(table, column);
	if (!db_.del(metaKey)) return Status::Error("dropTTLIndex: Löschen des Metaschlüssels fehlgeschlagen: " + metaKey);
	THEMIS_INFO("TTL Index gelöscht: {}.{}", table, column);
	return Status::OK();
}

bool SecondaryIndexManager::hasTTLIndex(std::string_view table, std::string_view column) const {
	std::string metaKey = makeTTLIndexMetaKey(table, column);
	return db_.get(metaKey).has_value();
}

// ────────────────────────────────────────────────────────────────────────────
// Fulltext-Index
// ────────────────────────────────────────────────────────────────────────────

SecondaryIndexManager::Status SecondaryIndexManager::createFulltextIndex(
	std::string_view table, 
	std::string_view column,
	const FulltextConfig& config
) {
	if (table.empty() || column.empty()) return Status::Error("createFulltextIndex: table/column darf nicht leer sein");
	if (table.find(':') != std::string::npos || column.find(':') != std::string::npos) {
		return Status::Error("createFulltextIndex: ':' ist in table/column nicht erlaubt");
	}
	
	// Serialize config to JSON
	nlohmann::json configJson = {
		{"type", "fulltext"},
		{"stemming_enabled", config.stemming_enabled},
		{"language", config.language},
		{"stopwords_enabled", config.stopwords_enabled},
		{"stopwords", config.stopwords},
		{"normalize_umlauts", config.normalize_umlauts}
	};
	std::string configStr = configJson.dump();
	std::vector<uint8_t> configBytes(configStr.begin(), configStr.end());
	
	std::string metaKey = makeFulltextIndexMetaKey(table, column);
	if (!db_.put(metaKey, configBytes)) {
		return Status::Error("createFulltextIndex: Schreiben des Metaschlüssels fehlgeschlagen: " + metaKey);
	}
	
		THEMIS_INFO("Fulltext Index erstellt: {}.{} (stemming={}, lang={}, stopwords_enabled={}, stopwords={}, normalize_umlauts={})", 
			table, column, config.stemming_enabled, config.language, config.stopwords_enabled, config.stopwords.size(), config.normalize_umlauts);
	return Status::OK();
}

// Overload that uses default config
SecondaryIndexManager::Status SecondaryIndexManager::createFulltextIndex(std::string_view table, std::string_view column) {
	return createFulltextIndex(table, column, FulltextConfig{});
}

SecondaryIndexManager::Status SecondaryIndexManager::dropFulltextIndex(std::string_view table, std::string_view column) {
	if (table.empty() || column.empty()) return Status::Error("dropFulltextIndex: table/column darf nicht leer sein");
	std::string metaKey = makeFulltextIndexMetaKey(table, column);
	if (!db_.del(metaKey)) return Status::Error("dropFulltextIndex: Löschen des Metaschlüssels fehlgeschlagen: " + metaKey);
	THEMIS_INFO("Fulltext Index gelöscht: {}.{}", table, column);
	return Status::OK();
}

bool SecondaryIndexManager::hasFulltextIndex(std::string_view table, std::string_view column) const {
	std::string metaKey = makeFulltextIndexMetaKey(table, column);
	return db_.get(metaKey).has_value();
}

std::optional<SecondaryIndexManager::FulltextConfig> 
SecondaryIndexManager::getFulltextConfig(std::string_view table, std::string_view column) const {
	std::string metaKey = makeFulltextIndexMetaKey(table, column);
	auto val = db_.get(metaKey);
	if (!val) return std::nullopt;
	
	try {
		std::string configStr(val->begin(), val->end());
		auto configJson = nlohmann::json::parse(configStr);
		
		FulltextConfig config;
		config.stemming_enabled = configJson.value("stemming_enabled", false);
		config.language = configJson.value("language", std::string("none"));
		config.stopwords_enabled = configJson.value("stopwords_enabled", false);
		if (configJson.contains("stopwords") && configJson["stopwords"].is_array()) {
			config.stopwords.clear();
			for (const auto& s : configJson["stopwords"]) {
				if (s.is_string()) config.stopwords.push_back(s.get<std::string>());
			}
		}
		config.normalize_umlauts = configJson.value("normalize_umlauts", false);
		return config;
	} catch (...) {
		// Legacy format (just "fulltext" marker) - return default config
		return FulltextConfig{};
	}
}

// Lädt alle Spalten, die für eine Tabelle indiziert sind, aus den Metaschlüsseln
std::unordered_set<std::string> SecondaryIndexManager::loadIndexedColumns_(std::string_view table) const {
	std::unordered_set<std::string> cols;
	const std::string prefix = std::string("idxmeta:") + std::string(table) + ":";
	db_.scanPrefix(prefix, [&cols, &prefix](std::string_view key, std::string_view /*value*/) {
		// key = idxmeta:<table>:<column> oder idxmeta:<table>:col1+col2+...
		std::string_view rest = key.substr(prefix.size());
		cols.insert(std::string(rest));
		return true;
	});
	return cols;
}

std::unordered_set<std::string> SecondaryIndexManager::loadRangeIndexedColumns_(std::string_view table) const {
	std::unordered_set<std::string> cols;
	const std::string prefix = std::string("ridxmeta:") + std::string(table) + ":";
	db_.scanPrefix(prefix, [&cols, &prefix](std::string_view key, std::string_view /*value*/) {
		std::string_view rest = key.substr(prefix.size());
		cols.insert(std::string(rest));
		return true;
	});
	return cols;
}

std::unordered_set<std::string> SecondaryIndexManager::loadSparseIndexedColumns_(std::string_view table) const {
	std::unordered_set<std::string> cols;
	const std::string prefix = std::string("sidxmeta:") + std::string(table) + ":";
	db_.scanPrefix(prefix, [&cols, &prefix](std::string_view key, std::string_view /*value*/) {
		std::string_view rest = key.substr(prefix.size());
		cols.insert(std::string(rest));
		return true;
	});
	return cols;
}

std::unordered_set<std::string> SecondaryIndexManager::loadGeoIndexedColumns_(std::string_view table) const {
	std::unordered_set<std::string> cols;
	const std::string prefix = std::string("gidxmeta:") + std::string(table) + ":";
	db_.scanPrefix(prefix, [&cols, &prefix](std::string_view key, std::string_view /*value*/) {
		std::string_view rest = key.substr(prefix.size());
		cols.insert(std::string(rest));
		return true;
	});
	return cols;
}

std::unordered_set<std::string> SecondaryIndexManager::loadTTLIndexedColumns_(std::string_view table) const {
	std::unordered_set<std::string> cols;
	const std::string prefix = std::string("ttlidxmeta:") + std::string(table) + ":";
	db_.scanPrefix(prefix, [&cols, &prefix](std::string_view key, std::string_view /*value*/) {
		std::string_view rest = key.substr(prefix.size());
		cols.insert(std::string(rest));
		return true;
	});
	return cols;
}

std::unordered_set<std::string> SecondaryIndexManager::loadFulltextIndexedColumns_(std::string_view table) const {
	std::unordered_set<std::string> cols;
	const std::string prefix = std::string("ftidxmeta:") + std::string(table) + ":";
	db_.scanPrefix(prefix, [&cols, &prefix](std::string_view key, std::string_view /*value*/) {
		std::string_view rest = key.substr(prefix.size());
		cols.insert(std::string(rest));
		return true;
	});
	return cols;
}

int64_t SecondaryIndexManager::getTTLSeconds_(std::string_view table, std::string_view column) const {
	std::string metaKey = makeTTLIndexMetaKey(table, column);
	auto val = db_.get(metaKey);
	if (!val) return 0;
	std::string ttlStr(val->begin(), val->end());
	try {
		return std::stoll(ttlStr);
	} catch (...) {
		return 0;
	}
}

bool SecondaryIndexManager::isUniqueIndex_(std::string_view table, std::string_view column) const {
	std::string metaKey = makeIndexMetaKey(table, column);
	auto val = db_.get(metaKey);
	if (!val) return false;
	std::string metaValue(val->begin(), val->end());
	return metaValue == "unique";
}

bool SecondaryIndexManager::isUniqueCompositeIndex_(std::string_view table, const std::vector<std::string>& columns) const {
	std::string metaKey = makeCompositeIndexMetaKey(table, columns);
	auto val = db_.get(metaKey);
	if (!val) return false;
	std::string metaValue(val->begin(), val->end());
	return metaValue == "unique";
}

bool SecondaryIndexManager::isSparseIndexUnique_(std::string_view table, std::string_view column) const {
	std::string metaKey = makeSparseIndexMetaKey(table, column);
	auto val = db_.get(metaKey);
	if (!val) return false;
	std::string metaValue(val->begin(), val->end());
	return metaValue == "unique";
}

SecondaryIndexManager::Status SecondaryIndexManager::put(std::string_view table, const BaseEntity& entity) {
	if (table.empty()) return Status::Error("put: table darf nicht leer sein");
	const std::string& pk = entity.getPrimaryKey();
	if (pk.empty()) return Status::Error("put: Entity hat keinen Primary Key");
	if (!db_.isOpen()) return Status::Error("put: Datenbank ist nicht geöffnet");

	// Bestehende Entity laden, um alte Indexeinträge bereinigen zu können
	const std::string relKey = KeySchema::makeRelationalKey(table, pk);
	std::optional<std::vector<uint8_t>> oldBlob = db_.get(relKey);
	std::unique_ptr<BaseEntity> oldEntity;
	if (oldBlob) {
		try {
			oldEntity = std::make_unique<BaseEntity>(BaseEntity::deserialize(pk, *oldBlob));
		} catch (...) {
			// Wenn Deserialisierung fehlschlägt, loggen und fahren fort (wir überschreiben anyway)
			THEMIS_WARN("put: Konnte alte Entity für PK={} nicht deserialisieren", pk);
		}
	}

	// Atomare Batch-Operation
	auto batch = db_.createWriteBatch();
	if (!batch) return Status::Error("put: Konnte WriteBatch nicht erstellen");
	auto st = put(table, entity, *batch);
	if (!st.ok) { batch->rollback(); return st; }
	if (!batch->commit()) return Status::Error("put: Commit des Batches fehlgeschlagen (atomare Aktualisierung)");
	return Status::OK();
}

SecondaryIndexManager::Status SecondaryIndexManager::erase(std::string_view table, std::string_view pk) {
	if (table.empty()) return Status::Error("erase: table darf nicht leer sein");
	if (pk.empty()) return Status::Error("erase: pk darf nicht leer sein");
	if (!db_.isOpen()) return Status::Error("erase: Datenbank ist nicht geöffnet");

	const std::string relKey = KeySchema::makeRelationalKey(table, pk);
	std::optional<std::vector<uint8_t>> oldBlob = db_.get(relKey);
	std::unique_ptr<BaseEntity> oldEntity;
	if (oldBlob) {
		try {
			oldEntity = std::make_unique<BaseEntity>(BaseEntity::deserialize(std::string(pk), *oldBlob));
		} catch (...) {
			THEMIS_WARN("erase: Konnte alte Entity für PK={} nicht deserialisieren", pk);
		}
	}

	auto batch = db_.createWriteBatch();
	if (!batch) return Status::Error("erase: Konnte WriteBatch nicht erstellen");
	auto st = erase(table, pk, *batch);
	if (!st.ok) { batch->rollback(); return st; }
	if (!batch->commit()) return Status::Error("erase: Commit des Batches fehlgeschlagen (atomare Löschung)");
	return Status::OK();
}

SecondaryIndexManager::Status SecondaryIndexManager::put(std::string_view table, const BaseEntity& entity, RocksDBWrapper::WriteBatchWrapper& batch) {
	if (table.empty()) return Status::Error("put(tx): table darf nicht leer sein");
	const std::string& pk = entity.getPrimaryKey();
	if (pk.empty()) return Status::Error("put(tx): Entity hat keinen Primary Key");
	if (!db_.isOpen()) return Status::Error("put(tx): Datenbank ist nicht geöffnet");

	const std::string relKey = KeySchema::makeRelationalKey(table, pk);
	std::optional<std::vector<uint8_t>> oldBlob = db_.get(relKey);
	std::unique_ptr<BaseEntity> oldEntity;
	if (oldBlob) {
		try { oldEntity = std::make_unique<BaseEntity>(BaseEntity::deserialize(pk, *oldBlob)); }
		catch (...) { THEMIS_WARN("put(tx): alte Entity für PK={} nicht deserialisierbar", pk); }
	}

	batch.put(relKey, entity.serialize());

	if (oldEntity) {
		auto st = updateIndexesForDelete_(table, pk, oldEntity.get(), batch);
		if (!st.ok) return st;
	}
	return updateIndexesForPut_(table, pk, entity, batch);
}

SecondaryIndexManager::Status SecondaryIndexManager::erase(std::string_view table, std::string_view pk, RocksDBWrapper::WriteBatchWrapper& batch) {
	if (table.empty()) return Status::Error("erase(tx): table darf nicht leer sein");
	if (pk.empty()) return Status::Error("erase(tx): pk darf nicht leer sein");
	if (!db_.isOpen()) return Status::Error("erase(tx): Datenbank ist nicht geöffnet");

	const std::string relKey = KeySchema::makeRelationalKey(table, pk);
	std::optional<std::vector<uint8_t>> oldBlob = db_.get(relKey);
	std::unique_ptr<BaseEntity> oldEntity;
	if (oldBlob) {
		try { oldEntity = std::make_unique<BaseEntity>(BaseEntity::deserialize(std::string(pk), *oldBlob)); }
		catch (...) { THEMIS_WARN("erase(tx): alte Entity für PK={} nicht deserialisierbar", pk); }
	}

	batch.del(relKey);
	return updateIndexesForDelete_(table, pk, oldEntity.get(), batch);
}

SecondaryIndexManager::Status SecondaryIndexManager::updateIndexesForPut_(std::string_view table,
																		  std::string_view pk,
																		  const BaseEntity& newEntity,
																		  RocksDBWrapper::WriteBatchWrapper& batch) {
	// Alle für diese Tabelle vorhandenen Indexspalten laden (Single + Composite)
	auto indexedCols = loadIndexedColumns_(table);
	// Zusätzlich: Range-Index-Spalten laden (können unabhängig von Equality-Indizes existieren)
	auto rangeCols = loadRangeIndexedColumns_(table);

	// Trennen: Single-Column vs. Composite (enthält '+')
	for (const auto& col : indexedCols) {
		if (col.find('+') == std::string::npos) {
			// Single-Column
			auto maybe = newEntity.extractField(col);
			if (!maybe) continue;
			const std::string encodedVal = encodeKeyComponent(*maybe);
			
			// Unique-Constraint prüfen
			if (isUniqueIndex_(table, col)) {
				// Prüfe ob bereits ein anderer PK mit diesem Wert existiert
				std::string prefix = std::string("idx:") + std::string(table) + ":" + col + ":" + encodedVal + ":";
				bool conflict = false;
				db_.scanPrefix(prefix, [&pk, &conflict](std::string_view key, std::string_view /*val*/) {
					// Extrahiere PK aus key: idx:table:column:value:PK
					size_t lastColon = key.rfind(':');
					if (lastColon != std::string_view::npos) {
						std::string_view existingPK = key.substr(lastColon + 1);
						if (existingPK != pk) {
							conflict = true;
							return false; // Stop scan
						}
					}
					return true;
				});
				if (conflict) {
					return Status::Error("Unique constraint violation: " + std::string(table) + "." + col + " = " + *maybe);
				}
			}
			
			const std::string idxKey = KeySchema::makeSecondaryIndexKey(table, col, encodedVal, pk);
			std::vector<uint8_t> pkBytes = toBytes(pk);
			batch.put(idxKey, pkBytes);
			
			// Falls Range-Index für diese Spalte existiert, ebenfalls pflegen
			if (rangeCols.find(col) != rangeCols.end()) {
				const std::string rkey = makeRangeIndexKey(table, col, *maybe, pk);
				batch.put(rkey, pkBytes);
			}
		} else {
			// Composite: col = "col1+col2+..."
			// Parse columns
			std::vector<std::string> columns;
			size_t start = 0;
			while (start < col.size()) {
				size_t pos = col.find('+', start);
				if (pos == std::string::npos) {
					columns.push_back(col.substr(start));
					break;
				}
				columns.push_back(col.substr(start, pos - start));
				start = pos + 1;
			}
			
			// Extract values
			std::vector<std::string> values;
			values.reserve(columns.size());
			bool allPresent = true;
			for (const auto& c : columns) {
				auto maybe = newEntity.extractField(c);
				if (!maybe) {
					allPresent = false;
					break;
				}
				values.push_back(*maybe);
			}
			
			if (!allPresent) continue; // Skip wenn nicht alle Felder vorhanden
			
			// Unique-Constraint prüfen für Composite Index
			if (isUniqueCompositeIndex_(table, columns)) {
				// Prüfe ob bereits ein anderer PK mit dieser Wertekombination existiert
				std::string prefix = makeCompositeIndexPrefix(table, columns, values);
				bool conflict = false;
				db_.scanPrefix(prefix, [&pk, &conflict](std::string_view key, std::string_view /*val*/) {
					// Extrahiere PK aus key (letztes Segment nach ':')
					size_t lastColon = key.rfind(':');
					if (lastColon != std::string_view::npos) {
						std::string_view existingPK = key.substr(lastColon + 1);
						if (existingPK != pk) {
							conflict = true;
							return false; // Stop scan
						}
					}
					return true;
				});
				if (conflict) {
					std::string valueStr;
					for (size_t i = 0; i < values.size(); ++i) {
						if (i > 0) valueStr += ", ";
						valueStr += columns[i] + "=" + values[i];
					}
					return Status::Error("Unique constraint violation: " + std::string(table) + ".{" + valueStr + "}");
				}
			}
			
			const std::string idxKey = makeCompositeIndexKey(table, columns, values, pk);
			std::vector<uint8_t> pkBytes = toBytes(pk);
			batch.put(idxKey, pkBytes);
		}
	}

	// Zusätzlich: Range-Indizes pflegen, die keine Equality-Indizes haben
	for (const auto& rcol : rangeCols) {
		// Wenn diese Spalte bereits im obigen Loop gepflegt wurde (weil Equality-Index existiert), überspringen
		if (indexedCols.find(rcol) != indexedCols.end()) continue;
		// Nur Single-Column Range-Indizes unterstützen (Composite-Range-Indizes sind nicht implementiert)
		auto maybe = newEntity.extractField(rcol);
		if (!maybe) continue;
		std::vector<uint8_t> pkBytes = toBytes(pk);
		const std::string rkey = makeRangeIndexKey(table, rcol, *maybe, pk);
		batch.put(rkey, pkBytes);
	}

	// Sparse-Indizes pflegen
	auto sparseCols = loadSparseIndexedColumns_(table);
	for (const auto& scol : sparseCols) {
		auto maybe = newEntity.extractField(scol);
		if (!maybe || isNullOrEmpty_(*maybe)) continue; // Skip NULL/empty values
		
		const std::string encodedVal = encodeKeyComponent(*maybe);
		
		// Unique-Constraint prüfen für Sparse Index
		if (isSparseIndexUnique_(table, scol)) {
			std::string prefix = makeSparseIndexKey(table, scol, encodedVal, "");
			bool conflict = false;
			db_.scanPrefix(prefix, [&pk, &conflict](std::string_view key, std::string_view /*val*/) {
				size_t lastColon = key.rfind(':');
				if (lastColon != std::string_view::npos) {
					std::string_view existingPK = key.substr(lastColon + 1);
					if (existingPK != pk) {
						conflict = true;
						return false;
					}
				}
				return true;
			});
			if (conflict) {
				return Status::Error("Sparse unique constraint violation: " + std::string(table) + "." + scol + " = " + *maybe);
			}
		}
		
		const std::string sidxKey = makeSparseIndexKey(table, scol, encodedVal, pk);
		std::vector<uint8_t> pkBytes = toBytes(pk);
		batch.put(sidxKey, pkBytes);
	}

	// Geo-Indizes pflegen
	auto geoCols = loadGeoIndexedColumns_(table);
	for (const auto& gcol : geoCols) {
		// Geo-Index erwartet zwei Felder: gcol_lat und gcol_lon (oder einfach lat/lon)
		// Konvention: Spaltenname ist z.B. "location", dann Felder "location_lat" und "location_lon"
		std::string latField = gcol + "_lat";
		std::string lonField = gcol + "_lon";
		
		auto maybeLat = newEntity.extractField(latField);
		auto maybeLon = newEntity.extractField(lonField);
		
		if (!maybeLat || !maybeLon) continue; // Skip wenn Koordinaten fehlen
		
		try {
			double lat = std::stod(*maybeLat);
			double lon = std::stod(*maybeLon);
			
			std::string geohash = encodeGeohash(lat, lon);
			const std::string gidxKey = makeGeoIndexKey(table, gcol, geohash, pk);
			std::vector<uint8_t> pkBytes = toBytes(pk);
			batch.put(gidxKey, pkBytes);
		} catch (...) {
			THEMIS_WARN("updateIndexesForPut_: Ungültige Geo-Koordinaten für {}.{}: lat={}, lon={}", 
					   table, gcol, *maybeLat, *maybeLon);
			continue;
		}
	}

	// TTL-Indizes pflegen
	auto ttlCols = loadTTLIndexedColumns_(table);
	for (const auto& tcol : ttlCols) {
		auto maybeValue = newEntity.extractField(tcol);
		if (!maybeValue) continue;
		
		// Calculate expire timestamp: now + TTL seconds
		auto now = std::chrono::system_clock::now();
		auto epoch = now.time_since_epoch();
		int64_t currentTimestamp = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
		int64_t ttlSeconds = getTTLSeconds_(table, tcol);
		if (ttlSeconds <= 0) continue;
		
		int64_t expireTimestamp = currentTimestamp + ttlSeconds;
		const std::string ttlKey = makeTTLIndexKey(table, tcol, expireTimestamp, pk);
		std::vector<uint8_t> pkBytes = toBytes(pk);
		batch.put(ttlKey, pkBytes);
	}

	// Fulltext-Indizes pflegen
	auto fulltextCols = loadFulltextIndexedColumns_(table);
	for (const auto& fcol : fulltextCols) {
		auto maybeText = newEntity.extractField(fcol);
		if (!maybeText || isNullOrEmpty_(maybeText)) continue;
		
		// Get index config and tokenize with stemming if enabled
		auto config = getFulltextConfig(table, fcol).value_or(FulltextConfig{});
		auto tokens = tokenize(*maybeText, config);
		
		std::unordered_map<std::string, uint32_t> tf;
		for (const auto& t : tokens) { if (!t.empty()) tf[t]++; }
		const std::string dkey = makeFulltextDocLenKey(table, fcol, pk);
		{
			std::string s = std::to_string(tokens.size());
			std::vector<uint8_t> val(s.begin(), s.end());
			batch.put(dkey, val);
		}
		std::vector<uint8_t> pkBytes = toBytes(pk);
		for (const auto& [token, count] : tf) {
			const std::string ftKey = makeFulltextIndexKey(table, fcol, token, pk);
			batch.put(ftKey, pkBytes);
			const std::string tfKey = makeFulltextTFKey(table, fcol, token, pk);
			std::string s = std::to_string(count);
			std::vector<uint8_t> tfVal(s.begin(), s.end());
			batch.put(tfKey, tfVal);
		}
	}

	return Status::OK();
}

SecondaryIndexManager::Status SecondaryIndexManager::updateIndexesForDelete_(std::string_view table,
																			 std::string_view pk,
																			 const BaseEntity* oldEntityOpt,
																			 RocksDBWrapper::WriteBatchWrapper& batch) {
	if (!oldEntityOpt) {
		// Falls keine alte Entity, können wir die spezifischen Index-Keys nicht sicher bestimmen.
		// Defensive strategy: alle Index-Prefixe für diesen PK löschen via Scan.
		auto indexedCols = loadIndexedColumns_(table);
		for (const auto& col : indexedCols) {
			std::string prefix;
			if (col.find('+') == std::string::npos) {
				// Single
				prefix = std::string("idx:") + std::string(table) + ":" + col + ":";
			} else {
				// Composite
				prefix = std::string("idx:") + std::string(table) + ":" + col + ":";
			}
			db_.scanPrefix(prefix, [this, &pk, &batch](std::string_view key, std::string_view /*val*/){
				// Prüfen, ob PK am Ende passt (endet mit :PK)
				std::string_view keyView(key);
				size_t lastColon = keyView.rfind(':');
				if (lastColon != std::string_view::npos) {
					std::string_view extractedPK = keyView.substr(lastColon + 1);
					if (extractedPK == pk) {
						batch.del(std::string(key));
					}
				}
				return true;
			});
		}
		// Auch alle Range-Index-Einträge mit diesem PK für diese Tabelle entfernen
		auto rangeCols = loadRangeIndexedColumns_(table);
		for (const auto& rcol : rangeCols) {
			std::string rprefix = std::string("ridx:") + std::string(table) + ":" + rcol + ":";
			db_.scanPrefix(rprefix, [&pk, &batch](std::string_view key, std::string_view /*val*/){
				size_t lastColon = key.rfind(':');
				if (lastColon != std::string_view::npos) {
					std::string_view existingPK = key.substr(lastColon + 1);
					if (existingPK == pk) {
						batch.del(std::string(key));
					}
				}
				return true;
			});
		}
		return Status::OK();
	}

	// Zielgerichtet löschen basierend auf alten Feldwerten
	auto indexedCols = loadIndexedColumns_(table);
	for (const auto& col : indexedCols) {
		if (col.find('+') == std::string::npos) {
			// Single-Column
			auto maybe = oldEntityOpt->extractField(col);
			if (!maybe) continue;
			const std::string encodedVal = encodeKeyComponent(*maybe);
			const std::string idxKey = KeySchema::makeSecondaryIndexKey(table, col, encodedVal, pk);
			batch.del(idxKey);
			// Auch Range-Index-Eintrag löschen, falls vorhanden
			auto rangeCols = loadRangeIndexedColumns_(table);
			if (rangeCols.find(col) != rangeCols.end()) {
				const std::string rkey = makeRangeIndexKey(table, col, *maybe, pk);
				batch.del(rkey);
			}
		} else {
			// Composite
			std::vector<std::string> columns;
			size_t start = 0;
			while (start < col.size()) {
				size_t pos = col.find('+', start);
				if (pos == std::string::npos) {
					columns.push_back(col.substr(start));
					break;
				}
				columns.push_back(col.substr(start, pos - start));
				start = pos + 1;
			}
			
			std::vector<std::string> values;
			values.reserve(columns.size());
			bool allPresent = true;
			for (const auto& c : columns) {
				auto maybe = oldEntityOpt->extractField(c);
				if (!maybe) {
					allPresent = false;
					break;
				}
				values.push_back(*maybe);
			}
			
			if (!allPresent) continue;
			
			const std::string idxKey = makeCompositeIndexKey(table, columns, values, pk);
			batch.del(idxKey);
		}
	}

	// Zusätzlich: Range-Indizes löschen, die keine passenden Equality-Indizes haben
	{
		auto rangeCols = loadRangeIndexedColumns_(table);
		for (const auto& rcol : rangeCols) {
			if (indexedCols.find(rcol) != indexedCols.end()) continue; // bereits oben behandelt
			auto maybe = oldEntityOpt->extractField(rcol);
			if (!maybe) continue;
			const std::string rkey = makeRangeIndexKey(table, rcol, *maybe, pk);
			batch.del(rkey);
		}
	}

	// Sparse-Indizes löschen
	{
		auto sparseCols = loadSparseIndexedColumns_(table);
		for (const auto& scol : sparseCols) {
			auto maybe = oldEntityOpt->extractField(scol);
			if (!maybe || isNullOrEmpty_(*maybe)) continue; // War nicht im Index
			const std::string encodedVal = encodeKeyComponent(*maybe);
			const std::string sidxKey = makeSparseIndexKey(table, scol, encodedVal, pk);
			batch.del(sidxKey);
		}
	}

	// Geo-Indizes löschen
	{
		auto geoCols = loadGeoIndexedColumns_(table);
		for (const auto& gcol : geoCols) {
			std::string latField = gcol + "_lat";
			std::string lonField = gcol + "_lon";
			
			auto maybeLat = oldEntityOpt->extractField(latField);
			auto maybeLon = oldEntityOpt->extractField(lonField);
			
			if (!maybeLat || !maybeLon) continue;
			
			try {
				double lat = std::stod(*maybeLat);
				double lon = std::stod(*maybeLon);
				
				std::string geohash = encodeGeohash(lat, lon);
				const std::string gidxKey = makeGeoIndexKey(table, gcol, geohash, pk);
				batch.del(gidxKey);
			} catch (...) {
				// Koordinaten waren ungültig, wahrscheinlich war kein Index-Eintrag vorhanden
				continue;
			}
		}
	}

	// TTL-Indizes löschen
	{
		auto ttlCols = loadTTLIndexedColumns_(table);
		for (const auto& tcol : ttlCols) {
			auto maybeValue = oldEntityOpt->extractField(tcol);
			if (!maybeValue) continue;
			
			// We need to find the TTL index entry, but we don't know the exact timestamp
			// Scan the TTL index prefix and delete matching PKs
			std::string prefix = makeTTLIndexPrefix(table, tcol);
			db_.scanPrefix(prefix, [&pk, &batch](std::string_view key, std::string_view /*val*/) {
				// Extract PK from ttlidx:table:column:timestamp:PK
				size_t lastColon = key.rfind(':');
				if (lastColon != std::string_view::npos) {
					std::string_view extractedPK = key.substr(lastColon + 1);
					if (extractedPK == pk) {
						batch.del(std::string(key));
						return false; // Stop after finding the matching entry
					}
				}
				return true;
			});
		}
	}

	// Fulltext-Indizes löschen
	{
		auto fulltextCols = loadFulltextIndexedColumns_(table);
		for (const auto& fcol : fulltextCols) {
			auto maybeText = oldEntityOpt->extractField(fcol);
			if (!maybeText || isNullOrEmpty_(maybeText)) continue;
			
			// Get index config and tokenize with same settings as index
			auto config = getFulltextConfig(table, fcol).value_or(FulltextConfig{});
			auto tokens = tokenize(*maybeText, config);
			
			std::unordered_set<std::string> uniqueTokens(tokens.begin(), tokens.end());
			for (const auto& token : uniqueTokens) {
				if (token.empty()) continue;
				const std::string ftKey = makeFulltextIndexKey(table, fcol, token, pk);
				batch.del(ftKey);
				const std::string tfKey = makeFulltextTFKey(table, fcol, token, pk);
				batch.del(tfKey);
			}
			// DocLength löschen
			const std::string dkey = makeFulltextDocLenKey(table, fcol, pk);
			batch.del(dkey);
		}
	}

	return Status::OK();
}

std::pair<SecondaryIndexManager::Status, std::vector<std::string>>
SecondaryIndexManager::scanKeysEqual(std::string_view table,
									 std::string_view column,
									 std::string_view value) const {
	// Check for regular index or sparse index
	bool hasRegularIndex = hasIndex(table, column);
	bool hasSparse = hasSparseIndex(table, column);
	
	if (!hasRegularIndex && !hasSparse) {
		return {Status::Error("scanKeysEqual: kein Index für " + std::string(table) + "." + std::string(column)), {}};
	}

	const std::string encodedVal = encodeKeyComponent(value);
	std::vector<std::string> pks;
	
	// Scan regular index if exists
	if (hasRegularIndex) {
		const std::string prefix = KeySchema::makeSecondaryIndexKey(table, column, encodedVal, "");
		db_.scanPrefix(prefix, [&pks](std::string_view key, std::string_view /*val*/){
			pks.emplace_back(KeySchema::extractPrimaryKey(key));
			return true;
		});
	}
	
	// Scan sparse index if exists (and no regular index, or as fallback)
	if (hasSparse && !hasRegularIndex) {
		const std::string prefix = makeSparseIndexKey(table, column, encodedVal, "");
		db_.scanPrefix(prefix, [&pks](std::string_view key, std::string_view /*val*/){
			// Extract PK from sidx:table:column:value:PK
			size_t lastColon = key.rfind(':');
			if (lastColon != std::string_view::npos) {
				pks.emplace_back(std::string(key.substr(lastColon + 1)));
			}
			return true;
		});
	}
	
	return {Status::OK(), std::move(pks)};
}

std::pair<SecondaryIndexManager::Status, std::vector<BaseEntity>>
SecondaryIndexManager::scanEntitiesEqual(std::string_view table,
										 std::string_view column,
										 std::string_view value) const {
	auto [st, keys] = scanKeysEqual(table, column, value);
	if (!st.ok) return {st, {}};

	std::vector<BaseEntity> out;
	out.reserve(keys.size());
	for (const auto& pk : keys) {
		const std::string relKey = KeySchema::makeRelationalKey(table, pk);
		auto blob = db_.get(relKey);
		if (!blob) {
			THEMIS_WARN("scanEntitiesEqual: Entity nicht gefunden für PK={} (inkonsistenter Index?)", pk);
			continue;
		}
		try {
			out.emplace_back(BaseEntity::deserialize(pk, *blob));
		} catch (...) {
			THEMIS_ERROR("scanEntitiesEqual: Deserialisierung fehlgeschlagen für PK={}", pk);
		}
	}
	return {Status::OK(), std::move(out)};
}

size_t SecondaryIndexManager::estimateCountEqual(std::string_view table,
												 std::string_view column,
												 std::string_view value,
												 size_t maxProbe,
												 bool* capped) const {
	if (capped) *capped = false;
	if (!hasIndex(table, column)) return 0;
	const std::string encodedVal = encodeKeyComponent(value);
	const std::string prefix = KeySchema::makeSecondaryIndexKey(table, column, encodedVal, "");
	size_t count = 0;
	db_.scanPrefix(prefix, [&](std::string_view /*key*/, std::string_view /*val*/){
		++count;
		if (count >= maxProbe) {
			if (capped) *capped = true;
			return false; // stop early
		}
		return true;
	});
	return count;
}

std::pair<SecondaryIndexManager::Status, std::vector<std::string>>
SecondaryIndexManager::scanKeysEqualComposite(std::string_view table,
											  const std::vector<std::string>& columns,
											  const std::vector<std::string>& values) const {
	if (columns.size() != values.size()) {
		return {Status::Error("scanKeysEqualComposite: Anzahl Spalten und Werte stimmt nicht überein"), {}};
	}
	if (!hasCompositeIndex(table, columns)) {
		std::string colList;
		for (size_t i = 0; i < columns.size(); ++i) {
			if (i > 0) colList += ", ";
			colList += columns[i];
		}
		return {Status::Error("scanKeysEqualComposite: kein Composite Index für " + std::string(table) + ".{" + colList + "}"), {}};
	}
	
	const std::string prefix = makeCompositeIndexPrefix(table, columns, values);
	std::vector<std::string> pks;
	db_.scanPrefix(prefix, [&pks, &prefix](std::string_view key, std::string_view /*val*/){
		// key format: idx:table:col1+col2:val1:val2:PK
		// Der PK folgt nach dem letzten ':'
		std::string_view rest = key.substr(prefix.size());
		pks.emplace_back(rest);
		return true;
	});
	return {Status::OK(), std::move(pks)};
}

std::pair<SecondaryIndexManager::Status, std::vector<BaseEntity>>
SecondaryIndexManager::scanEntitiesEqualComposite(std::string_view table,
												  const std::vector<std::string>& columns,
												  const std::vector<std::string>& values) const {
	auto [st, keys] = scanKeysEqualComposite(table, columns, values);
	if (!st.ok) return {st, {}};
	
	std::vector<BaseEntity> out;
	out.reserve(keys.size());
	for (const auto& pk : keys) {
		const std::string relKey = KeySchema::makeRelationalKey(table, pk);
		auto blob = db_.get(relKey);
		if (!blob) {
			THEMIS_WARN("scanEntitiesEqualComposite: Entity nicht gefunden für PK={} (inkonsistenter Index?)", pk);
			continue;
		}
		try {
			out.emplace_back(BaseEntity::deserialize(pk, *blob));
		} catch (...) {
			THEMIS_ERROR("scanEntitiesEqualComposite: Deserialisierung fehlgeschlagen für PK={}", pk);
		}
	}
	return {Status::OK(), std::move(out)};
}


size_t SecondaryIndexManager::estimateCountEqualComposite(std::string_view table,
											  const std::vector<std::string>& columns,
											  const std::vector<std::string>& values,
											  size_t maxProbe,
											  bool* capped) const {
	if (capped) *capped = false;
	if (columns.size() != values.size()) return 0;
	if (!hasCompositeIndex(table, columns)) return 0;
	
	const std::string prefix = makeCompositeIndexPrefix(table, columns, values);
	size_t count = 0;
	db_.scanPrefix(prefix, [&](std::string_view /*key*/, std::string_view /*val*/){
		++count;
		if (count >= maxProbe) {
			if (capped) *capped = true;
			return false;
		}
		return true;
	});
	return count;
}

std::pair<SecondaryIndexManager::Status, std::vector<std::string>> SecondaryIndexManager::scanKeysRange(
    std::string_view table,
    std::string_view column,
    const std::optional<std::string>& lower,
    const std::optional<std::string>& upper,
    bool includeLower,
    bool includeUpper,
    size_t limit,
    bool reversed
) const {
    if (table.empty() || column.empty()) return {Status::Error("scanKeysRange: table/column darf nicht leer sein"), {}};
    if (!hasRangeIndex(table, column)) return {Status::Error("scanKeysRange: kein Range-Index vorhanden für " + std::string(table) + "." + std::string(column)), {}};

    std::vector<std::string> result;
    std::string startKey, endKey;

    // RocksDB scanRange is [start, end) - exclusive on end
    if (lower.has_value()) {
        startKey = makeRangeIndexPrefix(table, column, *lower);
        // If lower is exclusive, seek past all keys with this value
        if (!includeLower) {
            startKey += '\xFF'; // Skip to next value
        }
    } else {
        startKey = std::string("ridx:") + std::string(table) + ":" + std::string(column) + ":";
    }

    if (upper.has_value()) {
        endKey = makeRangeIndexPrefix(table, column, *upper);
        // If upper is inclusive, extend range to include all keys with this value
        if (includeUpper) {
            endKey += '\xFF';
        }
        // else: scanRange is already exclusive on end, so we're good
    } else {
        endKey = std::string("ridx:") + std::string(table) + ":" + std::string(column) + ":\xFF";
    }

	uint64_t steps = 0;
	if (!reversed) {
		db_.scanRange(startKey, endKey, [&result, limit, &steps](std::string_view key, std::string_view /*value*/){
            if (result.size() >= limit) return false;
            size_t lastColon = key.rfind(':');
            if (lastColon != std::string_view::npos) {
                result.emplace_back(std::string(key.substr(lastColon+1)));
            }
			++steps;
            return true;
        });
    } else {
        std::vector<std::string> tmp;
		db_.scanRange(startKey, endKey, [&tmp, &steps](std::string_view key, std::string_view /*value*/){
            size_t lastColon = key.rfind(':');
            if (lastColon != std::string_view::npos) tmp.emplace_back(std::string(key.substr(lastColon+1)));
			++steps;
            return true;
        });
        std::reverse(tmp.begin(), tmp.end());
        if (tmp.size() > limit) tmp.resize(limit);
        result = std::move(tmp);
    }

	// Update range scan steps metric
	query_metrics_.range_scan_steps_total.fetch_add(steps, std::memory_order_relaxed);

    return {Status::OK(), result};
}

	std::pair<SecondaryIndexManager::Status, std::vector<std::string>>
	SecondaryIndexManager::scanKeysRangeAnchored(
		std::string_view table,
		std::string_view column,
		const std::optional<std::string>& lowerValue,
		const std::optional<std::string>& upperValue,
		bool includeLowerValue,
		bool includeUpperValue,
		size_t limit,
		bool reversed,
		const std::optional<std::pair<std::string, std::string>>& anchor
	) const {
		// Fallback auf normalen Range-Scan, wenn kein Anchor übergeben
		if (!anchor.has_value()) {
			return scanKeysRange(table, column, lowerValue, upperValue, includeLowerValue, includeUpperValue, limit, reversed);
		}

		if (table.empty() || column.empty()) return {Status::Error("scanKeysRangeAnchored: table/column darf nicht leer sein"), {}};
		if (!hasRangeIndex(table, column)) return {Status::Error("scanKeysRangeAnchored: kein Range-Index vorhanden für " + std::string(table) + "." + std::string(column)), {}};

		const std::string& anchorValue = anchor->first;
		const std::string& anchorPk = anchor->second;

		// Count anchor usage
		query_metrics_.cursor_anchor_hits_total.fetch_add(1, std::memory_order_relaxed);

		std::vector<std::string> out;
		out.reserve(limit);

		// 1) Innerhalb des gleichen Wertes selektieren
		//    Für asc: PKs > anchorPk, für desc: PKs < anchorPk
		{
			std::string prefix = makeRangeIndexPrefix(table, column, anchorValue);
			std::vector<std::string> sameValuePks;
			sameValuePks.reserve(limit);
			db_.scanPrefix(prefix, [&sameValuePks](std::string_view key, std::string_view /*val*/){
				size_t lastColon = key.rfind(':');
				if (lastColon != std::string_view::npos) {
					sameValuePks.emplace_back(std::string(key.substr(lastColon+1)));
				}
				return true;
			});

			// Add steps equal to scanned keys on the anchor value
			query_metrics_.range_scan_steps_total.fetch_add(static_cast<uint64_t>(sameValuePks.size()), std::memory_order_relaxed);

			if (!reversed) {
				// ascending: > anchorPk
				for (const auto& pk : sameValuePks) {
					if (pk > anchorPk) {
						out.push_back(pk);
						if (out.size() >= limit) return {Status::OK(), std::move(out)};
					}
				}
			} else {
				// descending: < anchorPk, in umgekehrter Ordnung
				// sameValuePks ist aktuell aufsteigend; wir iterieren rückwärts
				for (auto it = sameValuePks.rbegin(); it != sameValuePks.rend(); ++it) {
					if (*it < anchorPk) {
						out.push_back(*it);
						if (out.size() >= limit) return {Status::OK(), std::move(out)};
					}
				}
			}
		}

		// 2) Über die benachbarten Werte hinaus
		//    Für asc: Werte > anchorValue
		//    Für desc: Werte < anchorValue
		{
			std::optional<std::string> lb = lowerValue;
			std::optional<std::string> ub = upperValue;
			bool il = includeLowerValue;
			bool iu = includeUpperValue;

			// An Bounds anpassen, sodass wir nicht außerhalb des gewünschten Bereichs scannen
			if (!reversed) {
				// Start nach anchorValue
				// Falls upperBound < anchorValue, gibt es nichts mehr
				if (ub.has_value() && *ub <= anchorValue && !iu) {
					return {Status::OK(), std::move(out)};
				}
				// setze lb auf anchorValue und überspringe gleiche Werte
				lb = anchorValue;
				il = false; // exklusiv: Werte NACH anchorValue
			} else {
				// descending: Ende vor anchorValue
				if (lb.has_value() && *lb >= anchorValue && !il) {
					return {Status::OK(), std::move(out)};
				}
				// setze ub auf anchorValue und überspringe gleiche Werte
				ub = anchorValue;
				iu = false; // exklusiv: Werte VOR anchorValue
			}

			// Rest auffüllen
			auto [st2, more] = scanKeysRange(table, column, lb, ub, il, iu, limit - out.size(), reversed);
			if (!st2.ok) return {st2, {}};

			// Anhängen
			for (const auto& pk : more) {
				out.push_back(pk);
				if (out.size() >= limit) break;
			}
		}

		return {Status::OK(), std::move(out)};
	}

// ────────────────────────────────────────────────────────────────────────────
// Geo-Index: Geohash-Encoding und Geo-Queries
// ────────────────────────────────────────────────────────────────────────────

std::string SecondaryIndexManager::encodeGeohash(double lat, double lon, int /*precision*/) {
	// Normalize to [0, 1]
	double lat_norm = (lat + 90.0) / 180.0;
	double lon_norm = (lon + 180.0) / 360.0;
	
	// Clamp
	lat_norm = std::max(0.0, std::min(1.0, lat_norm));
	lon_norm = std::max(0.0, std::min(1.0, lon_norm));
	
	// Convert to integer coordinates (use full 32 bits for better precision)
	uint64_t lat_bits = static_cast<uint64_t>(lat_norm * 0xFFFFFFFFULL);
	uint64_t lon_bits = static_cast<uint64_t>(lon_norm * 0xFFFFFFFFULL);
	
	// Interleave bits (Z-Order / Morton Code)
	uint64_t morton = 0;
	for (int i = 0; i < 32; ++i) {
		morton |= ((lat_bits & (1ULL << i)) << i) | ((lon_bits & (1ULL << i)) << (i + 1));
	}
	
	// Convert to hex string
	char buf[32];
	snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)morton);
	return std::string(buf);
}

std::pair<double, double> SecondaryIndexManager::decodeGeohash(std::string_view geohash) {
	uint64_t morton = std::strtoull(std::string(geohash).c_str(), nullptr, 16);
	
	uint64_t lat_bits = 0, lon_bits = 0;
	for (int i = 0; i < 32; ++i) {
		if (morton & (1ULL << (i * 2))) lat_bits |= (1ULL << i);
		if (morton & (1ULL << (i * 2 + 1))) lon_bits |= (1ULL << i);
	}
	
	double lat_norm = static_cast<double>(lat_bits) / 0xFFFFFFFFULL;
	double lon_norm = static_cast<double>(lon_bits) / 0xFFFFFFFFULL;
	
	double lat = lat_norm * 180.0 - 90.0;
	double lon = lon_norm * 360.0 - 180.0;
	
	return {lat, lon};
}

double SecondaryIndexManager::haversineDistance(double lat1, double lon1, double lat2, double lon2) {
	constexpr double R = 6371.0; // Earth radius in km
	constexpr double PI = 3.14159265358979323846;
	
	double dLat = (lat2 - lat1) * PI / 180.0;
	double dLon = (lon2 - lon1) * PI / 180.0;
	
	double a = std::sin(dLat / 2.0) * std::sin(dLat / 2.0) +
	           std::cos(lat1 * PI / 180.0) * std::cos(lat2 * PI / 180.0) *
	           std::sin(dLon / 2.0) * std::sin(dLon / 2.0);
	
	double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
	
	return R * c;
}

std::pair<SecondaryIndexManager::Status, std::vector<std::string>>
SecondaryIndexManager::scanGeoBox(
	std::string_view table,
	std::string_view column,
	double minLat, double maxLat,
	double minLon, double maxLon,
	size_t limit) const {
	
	if (!hasGeoIndex(table, column)) {
		return {Status::Error("scanGeoBox: Kein Geo-Index für " + std::string(table) + "." + std::string(column)), {}};
	}
	
	// Generate geohash range for bounding box
	std::string minHash = encodeGeohash(minLat, minLon);
	std::string maxHash = encodeGeohash(maxLat, maxLon);
	
	std::vector<std::string> results;
	const std::string prefix = std::string("gidx:") + std::string(table) + ":" + std::string(column) + ":";
	
	size_t count = 0;
	db_.scanPrefix(prefix, [&](std::string_view key, std::string_view /*value*/) {
		if (count >= limit) return false;
		
		// Extract PK from key: gidx:table:column:geohash:PK
		size_t lastColon = key.rfind(':');
		if (lastColon == std::string_view::npos) return true;
		
		size_t secondLastColon = key.rfind(':', lastColon - 1);
		if (secondLastColon == std::string_view::npos) return true;
		
		std::string_view geohash = key.substr(secondLastColon + 1, lastColon - secondLastColon - 1);
		auto [lat, lon] = decodeGeohash(geohash);
		
		// Check if in bounding box
		if (lat >= minLat && lat <= maxLat && lon >= minLon && lon <= maxLon) {
			results.emplace_back(key.substr(lastColon + 1));
			count++;
		}
		return true;
	});
	
	return {Status::OK(), std::move(results)};
}

std::pair<SecondaryIndexManager::Status, std::vector<std::string>>
SecondaryIndexManager::scanGeoRadius(
	std::string_view table,
	std::string_view column,
	double centerLat, double centerLon,
	double radiusKm,
	size_t limit) const {
	
	if (!hasGeoIndex(table, column)) {
		return {Status::Error("scanGeoRadius: Kein Geo-Index für " + std::string(table) + "." + std::string(column)), {}};
	}
	
	// Approximate bounding box (1 degree ≈ 111 km at equator)
	double latDelta = radiusKm / 111.0;
	double lonDelta = radiusKm / (111.0 * std::cos(centerLat * 3.14159265358979323846 / 180.0));
	
	double minLat = centerLat - latDelta;
	double maxLat = centerLat + latDelta;
	double minLon = centerLon - lonDelta;
	double maxLon = centerLon + lonDelta;
	
	std::vector<std::string> results;
	const std::string prefix = std::string("gidx:") + std::string(table) + ":" + std::string(column) + ":";
	
	size_t count = 0;
	db_.scanPrefix(prefix, [&](std::string_view key, std::string_view /*value*/) {
		if (count >= limit) return false;
		
		// Extract geohash and PK
		size_t lastColon = key.rfind(':');
		if (lastColon == std::string_view::npos) return true;
		
		size_t secondLastColon = key.rfind(':', lastColon - 1);
		if (secondLastColon == std::string_view::npos) return true;
		
		std::string_view geohash = key.substr(secondLastColon + 1, lastColon - secondLastColon - 1);
		auto [lat, lon] = decodeGeohash(geohash);
		
		// Check if in bounding box first (fast filter)
		if (lat < minLat || lat > maxLat || lon < minLon || lon > maxLon) return true;
		
		// Precise distance check
		double dist = haversineDistance(centerLat, centerLon, lat, lon);
		if (dist <= radiusKm) {
			results.emplace_back(key.substr(lastColon + 1));
			count++;
		}
		return true;
	});
	
	return {Status::OK(), std::move(results)};
}

// ────────────────────────────────────────────────────────────────────────────
// TTL Cleanup
// ────────────────────────────────────────────────────────────────────────────

std::pair<SecondaryIndexManager::Status, size_t> 
SecondaryIndexManager::cleanupExpiredEntities(std::string_view table, std::string_view column) {
	if (!hasTTLIndex(table, column)) {
		return {Status::Error("cleanupExpiredEntities: Kein TTL-Index für " + std::string(table) + "." + std::string(column)), 0};
	}
	
	// Current timestamp
	auto now = std::chrono::system_clock::now();
	auto epoch = now.time_since_epoch();
	int64_t currentTimestamp = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
	
	// Scan TTL index for expired entries: ttlidx:table:column:0 bis ttlidx:table:column:{currentTimestamp}
	std::string prefix = makeTTLIndexPrefix(table, column);
	char maxBuf[32];
	snprintf(maxBuf, sizeof(maxBuf), "%020lld", (long long)currentTimestamp);
	std::string upperBound = prefix + maxBuf;
	
	std::vector<std::string> expiredPKs;
	std::vector<std::string> ttlKeys;
	
	db_.scanPrefix(prefix, [&](std::string_view key, std::string_view /*val*/) {
		// Stop if key > upperBound
		if (key > upperBound) return false;
		
		// Extract PK from ttlidx:table:column:timestamp:PK
		size_t lastColon = key.rfind(':');
		if (lastColon != std::string_view::npos) {
			std::string pk(key.substr(lastColon + 1));
			expiredPKs.push_back(pk);
			ttlKeys.push_back(std::string(key));
		}
		return true;
	});
	
	// Delete expired entities
	size_t deletedCount = 0;
	for (size_t i = 0; i < expiredPKs.size(); ++i) {
		auto st = erase(table, expiredPKs[i]);
		if (st.ok) {
			deletedCount++;
			// Also remove TTL index entry
			db_.del(ttlKeys[i]);
		} else {
			THEMIS_WARN("cleanupExpiredEntities: Fehler beim Löschen von PK={}: {}", expiredPKs[i], st.message);
		}
	}
	
	if (deletedCount > 0) {
		THEMIS_INFO("TTL Cleanup: {} abgelaufene Entities gelöscht aus {}.{}", deletedCount, table, column);
	}
	
	return {Status::OK(), deletedCount};
}

// ────────────────────────────────────────────────────────────────────────────
// Fulltext Scan
// ────────────────────────────────────────────────────────────────────────────

// Internal helper: computes BM25 scores for fulltext queries
std::pair<SecondaryIndexManager::Status, std::vector<SecondaryIndexManager::FulltextResult>>
SecondaryIndexManager::computeBM25Scores_(
	std::string_view table,
	std::string_view column,
	std::string_view query,
	size_t limit
) const {
	if (!hasFulltextIndex(table, column)) {
		return {Status::Error("computeBM25Scores_: Kein Fulltext-Index für " + std::string(table) + "." + std::string(column)), {}};
	}
	
	// Get index config and parse phrases; tokenize query without quoted phrases
	auto config = getFulltextConfig(table, column).value_or(FulltextConfig{});
	auto parsePhrases = [](std::string_view q) {
		std::vector<std::string> phrases;
		std::string cleaned;
		cleaned.reserve(q.size());
		bool in_quotes = false;
		std::string current;
		for (size_t i = 0; i < q.size(); ++i) {
			char c = q[i];
			if (c == '"') {
				if (in_quotes) {
					if (!current.empty()) { phrases.push_back(current); current.clear(); }
					in_quotes = false;
				} else {
					in_quotes = true;
				}
				continue;
			}
			if (in_quotes) current.push_back(c); else cleaned.push_back(c);
		}
		return std::pair{phrases, cleaned};
	};
	auto [phrases, cleanedQuery] = parsePhrases(query);
	auto tokens = tokenize(cleanedQuery, config);
	if (tokens.empty() && !phrases.empty()) {
		// Fallback: use tokens from phrases to generate candidates
		std::string concat;
		for (size_t i = 0; i < phrases.size(); ++i) {
			if (i) concat.push_back(' ');
			concat += phrases[i];
		}
		tokens = tokenize(concat, config);
	}
	
	if (tokens.empty()) {
		return {Status::OK(), {}};
	}
	
	// For each token, get PKs from inverted index (doc frequency sets)
	std::vector<std::unordered_set<std::string>> tokenResults;
	for (const auto& token : tokens) {
		std::string prefix = makeFulltextIndexPrefix(table, column, token);
		std::unordered_set<std::string> pks;
		
		db_.scanPrefix(prefix, [&pks](std::string_view key, std::string_view /*val*/) {
			// Extract PK from ftidx:table:column:token:PK
			size_t lastColon = key.rfind(':');
			if (lastColon != std::string_view::npos) {
				pks.insert(std::string(key.substr(lastColon + 1)));
			}
			return true;
		});
		
		tokenResults.push_back(std::move(pks));
	}
	
	// Intersect all sets (AND logic)
	if (tokenResults.empty()) {
		return {Status::OK(), {}};
	}
	
	std::unordered_set<std::string> intersectionSet = tokenResults[0];
	for (size_t i = 1; i < tokenResults.size(); ++i) {
		std::unordered_set<std::string> intersection;
		for (const auto& pk : intersectionSet) {
			if (tokenResults[i].count(pk)) {
				intersection.insert(pk);
			}
		}
		intersectionSet = std::move(intersection);
	}

	// Optional phrase verification on original field text (no positions stored)
	if (!phrases.empty()) {
		std::vector<std::string> toErase;
		for (const auto& pk : intersectionSet) {
			auto pkey = KeySchema::makeRelationalKey(table, pk);
			auto blob = db_.get(pkey);
			bool keep = false;
			if (blob && !blob->empty()) {
				try {
					// Use BaseEntity deserialization to reliably access field values
					BaseEntity::Blob beBlob(blob->begin(), blob->end());
					BaseEntity entity = BaseEntity::deserialize(pk, beBlob);
					auto maybeVal = entity.extractField(column);
					if (maybeVal.has_value()) {
						std::string field = *maybeVal;
						if (config.normalize_umlauts) field = utils::Normalizer::normalizeUmlauts(field);
						std::transform(field.begin(), field.end(), field.begin(), [](unsigned char c){ return std::tolower(c); });
						bool allFound = true;
						for (auto ph : phrases) {
							if (config.normalize_umlauts) ph = utils::Normalizer::normalizeUmlauts(ph);
							std::transform(ph.begin(), ph.end(), ph.begin(), [](unsigned char c){ return std::tolower(c); });
							if (field.find(ph) == std::string::npos) { allFound = false; break; }
						}
						keep = allFound;
					}
				} catch (...) {
					keep = false;
				}
			}
			if (!keep) toErase.push_back(pk);
		}
		for (const auto& pk : toErase) intersectionSet.erase(pk);
		if (intersectionSet.empty()) return {Status::OK(), {}};
	}

	// BM25 Ranking über die Schnittmenge berechnen
	// Annahmen (v1 minimal):
	// - N ~ Anzahl Dokumente im Kandidaten-Universum (Vereinigung aller Token-Sets)
	// - avgdl ~ durchschnittliche DocLength über die Kandidaten (Vereinigung)
	std::unordered_set<std::string> universe;
	for (const auto& s : tokenResults) {
		for (const auto& pk : s) universe.insert(pk);
	}
	const double N = static_cast<double>(std::max<size_t>(1, universe.size()));

	// DocLength laden für Kandidaten (für avgdl)
	std::unordered_map<std::string, double> docLen;
	double totalLen = 0.0;
	for (const auto& pk : universe) {
		const std::string dkey = makeFulltextDocLenKey(table, column, pk);
		auto v = db_.get(dkey);
		double dl = 0.0;
		if (v && !v->empty()) {
			std::string s(reinterpret_cast<const char*>(v->data()), v->size());
			try { dl = static_cast<double>(std::stoull(s)); } catch (...) { dl = 0.0; }
		}
		docLen.emplace(pk, dl);
		totalLen += dl;
	}
	const double avgdl = (universe.empty() ? 1.0 : std::max(1.0, totalLen / static_cast<double>(universe.size())));

	// Vorbereiten: df je Token
	std::vector<double> dfs;
	dfs.reserve(tokens.size());
	for (const auto& s : tokenResults) dfs.push_back(static_cast<double>(s.size()));

	// BM25 Parameter
	const double k1 = 1.2;
	const double b = 0.75;

	// Score für jede PK in der Schnittmenge berechnen
	std::vector<FulltextResult> scored;
	for (const auto& pk : intersectionSet) {
		double dl = docLen.count(pk) ? docLen[pk] : 0.0;
		double s = 0.0;
		for (size_t i = 0; i < tokens.size(); ++i) {
			const auto& token = tokens[i];
			const double df = std::max(1.0, dfs[i]);
			// IDF wie BM25 mit +1 Stabilisierung
			double idf = std::log((N - df + 0.5) / (df + 0.5) + 1.0);
			// tf laden
			const std::string tfKey = makeFulltextTFKey(table, column, token, pk);
			auto tfv = db_.get(tfKey);
			double tf = 0.0;
			if (tfv && !tfv->empty()) {
				std::string sTF(reinterpret_cast<const char*>(tfv->data()), tfv->size());
				try { tf = static_cast<double>(std::stoul(sTF)); } catch (...) { tf = 1.0; }
			} else {
				// Fallback: wenn kein TF gespeichert ist, minimal 1
				tf = 1.0;
			}
			double denom = tf + k1 * (1.0 - b + b * (dl / avgdl));
			if (denom <= 0.0) denom = tf + k1; // Guard
			double term = idf * ((tf * (k1 + 1.0)) / denom);
			s += term;
		}
		scored.push_back({pk, s});
	}

	// Sortieren nach Score absteigend
	std::sort(scored.begin(), scored.end(), [](const FulltextResult& a, const FulltextResult& b){
		return a.score > b.score;
	});

	// Top-k Ergebnisse extrahieren
	std::vector<FulltextResult> finalResults;
	finalResults.reserve(std::min(scored.size(), limit));
	for (size_t i = 0; i < scored.size() && i < limit; ++i) {
		finalResults.push_back(scored[i]);
	}

	return {Status::OK(), std::move(finalResults)};
}

// Public API: returns PKs only (deprecated, use scanFulltextWithScores for scores)
std::pair<SecondaryIndexManager::Status, std::vector<std::string>>
SecondaryIndexManager::scanFulltext(
	std::string_view table,
	std::string_view column,
	std::string_view query,
	size_t limit
) const {
	auto [status, results] = computeBM25Scores_(table, column, query, limit);
	if (!status.ok) {
		return {status, {}};
	}
	
	std::vector<std::string> pks;
	pks.reserve(results.size());
	for (const auto& result : results) {
		pks.push_back(result.pk);
	}
	
	return {Status::OK(), std::move(pks)};
}

// Public API: returns PKs with BM25 scores
std::pair<SecondaryIndexManager::Status, std::vector<SecondaryIndexManager::FulltextResult>>
SecondaryIndexManager::scanFulltextWithScores(
	std::string_view table,
	std::string_view column,
	std::string_view query,
	size_t limit
) const {
	return computeBM25Scores_(table, column, query, limit);
}

bool SecondaryIndexManager::isNullOrEmpty_(const std::optional<std::string>& value) {
	return !value.has_value() || value->empty() || *value == "null";
}

// Tokenizer: Whitespace-based, converts to lowercase
std::vector<std::string> SecondaryIndexManager::tokenize(std::string_view text) {
	std::vector<std::string> tokens;
	std::string current;
	
	for (char c : text) {
		if (std::isspace(static_cast<unsigned char>(c)) || std::ispunct(static_cast<unsigned char>(c))) {
			if (!current.empty()) {
				// Convert to lowercase
				std::transform(current.begin(), current.end(), current.begin(),
					[](unsigned char c) { return std::tolower(c); });
				tokens.push_back(std::move(current));
				current.clear();
			}
		} else {
			current += c;
		}
	}
	
	// Don't forget last token
	if (!current.empty()) {
		std::transform(current.begin(), current.end(), current.begin(),
			[](unsigned char c) { return std::tolower(c); });
		tokens.push_back(std::move(current));
	}
	
	return tokens;
}

// Tokenizer with Stemming support
std::vector<std::string> SecondaryIndexManager::tokenize(std::string_view text, const FulltextConfig& config) {
	// Optional: normalize umlauts/ß for German-like content before tokenization
	std::string normalized;
	if (config.normalize_umlauts) {
		normalized = utils::Normalizer::normalizeUmlauts(text);
	}
	// First tokenize normally (lowercase + whitespace split)
	std::vector<std::string> tokens = tokenize(normalized.empty() ? text : std::string_view(normalized));
	
	// Remove stopwords if enabled
	if (config.stopwords_enabled) {
		auto base = utils::Stopwords::defaults(config.language);
		auto sw = utils::Stopwords::merge(base, config.stopwords);
		tokens.erase(std::remove_if(tokens.begin(), tokens.end(), [&](const std::string& t){
			return sw.find(t) != sw.end();
		}), tokens.end());
	}

	// Apply stemming if enabled
	if (config.stemming_enabled) {
		auto lang = utils::Stemmer::parseLanguage(config.language);
		for (auto& token : tokens) {
			token = utils::Stemmer::stem(token, lang);
		}
	}
	
	return tokens;
}

// =============================================================================
// Index Statistics & Maintenance
// =============================================================================

std::vector<SecondaryIndexManager::IndexStats> SecondaryIndexManager::getAllIndexStats(const std::string& table) {
	std::vector<SecondaryIndexManager::IndexStats> allStats;
	std::unordered_set<std::string> processedColumns;
	
	// Scan all meta-key prefixes and collect unique table:column combinations
	auto scanMetaPrefix = [&](const std::string& metaPrefix) {
		std::string prefix = metaPrefix + table + ":";
		db_.scanPrefix(prefix, [&](std::string_view key, std::string_view /*val*/) {
			// Extract column from key (format: "prefix:table:column")
			std::string keyStr(key);
			size_t firstColon = keyStr.find(':');
			if (firstColon != std::string::npos) {
				size_t secondColon = keyStr.find(':', firstColon + 1);
				if (secondColon != std::string::npos) {
					std::string column = keyStr.substr(secondColon + 1);
					
					// Remove any trailing parts (e.g., composite column suffixes)
					size_t thirdColon = column.find(':');
					if (thirdColon != std::string::npos) {
						column = column.substr(0, thirdColon);
					}
					
					processedColumns.insert(column);
				}
			}
			return true;
		});
	};
	
	// Scan all index meta-key types
	scanMetaPrefix("idxmeta:");
	scanMetaPrefix("ridxmeta:");
	scanMetaPrefix("sidxmeta:");
	scanMetaPrefix("gidxmeta:");
	scanMetaPrefix("ttlidxmeta:");
	scanMetaPrefix("ftidxmeta:");
	scanMetaPrefix("cidxmeta:");
	
	// Get stats for each unique column
	for (const auto& column : processedColumns) {
		allStats.push_back(getIndexStats(table, column));
	}
	
	return allStats;
}

void SecondaryIndexManager::rebuildIndex(const std::string& table, const std::string& column) {
    // Delegiert auf Overload mit optionalem Progress-Callback
    rebuildIndex(table, column, nullptr);
}

void SecondaryIndexManager::rebuildIndex(const std::string& table, const std::string& column,
										 std::function<bool(size_t,size_t)> progress) {
	// Start metrics tracking
	auto start_time = std::chrono::steady_clock::now();
	
	// Helper to write entries
	auto writeIndexEntry = [&](const std::string& key, const std::string& pk) {
		std::vector<uint8_t> pkBytes(pk.begin(), pk.end());
		db_.put(key, pkBytes);
	};

	// Determine index type
	std::string indexType;
	std::string indexPrefix;

	if (db_.get(makeTTLIndexMetaKey(table, column)).has_value()) {
		indexType = "ttl";
		indexPrefix = std::string("ttlidx:") + table + ":" + column + ":";
	} else if (db_.get(makeFulltextIndexMetaKey(table, column)).has_value()) {
		indexType = "fulltext";
		indexPrefix = std::string("ftidx:") + table + ":" + column + ":";
	} else if (db_.get(makeGeoIndexMetaKey(table, column)).has_value()) {
		indexType = "geo";
		indexPrefix = std::string("gidx:") + table + ":" + column + ":";
	} else if (db_.get(makeSparseIndexMetaKey(table, column)).has_value()) {
		indexType = "sparse";
		indexPrefix = std::string("sidx:") + table + ":" + column + ":";
	} else if (db_.get(makeRangeIndexMetaKey(table, column)).has_value()) {
		indexType = "range";
		indexPrefix = std::string("ridx:") + table + ":" + column + ":";
	} else if (column.find('+') != std::string::npos) {
		// Composite index
		std::vector<std::string> cols;
		size_t pos = 0;
		while (pos < column.size()) {
			size_t p = column.find('+', pos);
			if (p == std::string::npos) p = column.size();
			cols.push_back(column.substr(pos, p - pos));
			pos = p + 1;
		}
		if (db_.get(makeCompositeIndexMetaKey(table, cols)).has_value()) {
			indexType = "composite";
			indexPrefix = std::string("idx:") + table + ":" + column + ":";
		} else {
			return; // No index
		}
	} else if (db_.get(makeIndexMetaKey(table, column)).has_value()) {
		indexType = "regular";
		indexPrefix = std::string("idx:") + table + ":" + column + ":";
	} else {
		return; // No index found
	}

	// Step 2: Delete all existing index entries
	std::vector<std::string> keysToDelete;
	db_.scanPrefix(indexPrefix, [&keysToDelete](std::string_view key, std::string_view) {
		keysToDelete.push_back(std::string(key));
		return true;
	});

	for (const auto& key : keysToDelete) {
		db_.del(key);
	}

	// Step 3.0: Total entities under <table>:
	const std::string entityPrefix = table + ":";
	size_t total = 0;
	db_.scanPrefix(entityPrefix, [&total](std::string_view /*k*/, std::string_view /*v*/) {
		++total;
		return true;
	});
	size_t done = 0;
	auto advance = [&]() -> bool {
		++done;
		if (progress) return progress(done, total);
		return true;
	};

	// Step 3: Scan entities and rebuild
	if (indexType == "ttl") {
		int64_t ttl_sec = getTTLSeconds_(table, column);
		auto now = std::chrono::system_clock::now();
		auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

		bool aborted = false;
		db_.scanPrefix(entityPrefix, [&](std::string_view key, std::string_view val) {
			size_t lastColon = key.rfind(':');
			if (lastColon == std::string::npos) return true;
			std::string pk(key.substr(lastColon + 1));

			BaseEntity::Blob blob(val.begin(), val.end());
			BaseEntity entity = BaseEntity::deserialize(pk, blob);
			auto maybeVal = entity.extractField(column);
			if (!maybeVal || isNullOrEmpty_(maybeVal)) { if (!advance()) { aborted = true; return false; } return true; }

			int64_t expire_ts = now_ts + ttl_sec;
			std::string ttlKey = makeTTLIndexKey(table, column, expire_ts, pk);
			writeIndexEntry(ttlKey, pk);
			if (!advance()) { aborted = true; return false; }
			return true;
		});
		if (aborted) return;
	} else if (indexType == "fulltext") {
		// Get index config for consistent tokenization
		auto config = getFulltextConfig(table, column).value_or(FulltextConfig{});
		
		bool aborted = false;
		db_.scanPrefix(entityPrefix, [&](std::string_view key, std::string_view val) {
			size_t lastColon = key.rfind(':');
			if (lastColon == std::string::npos) return true;
			std::string pk(key.substr(lastColon + 1));

			BaseEntity::Blob blob(val.begin(), val.end());
			BaseEntity entity = BaseEntity::deserialize(pk, blob);
			auto maybeVal = entity.extractField(column);
			if (!maybeVal) { if (!advance()) { aborted = true; return false; } return true; }

			auto tokens = tokenize(*maybeVal, config);
			for (const auto& token : tokens) {
				std::string ftKey = makeFulltextIndexKey(table, column, token, pk);
				writeIndexEntry(ftKey, pk);
			}
			if (!advance()) { aborted = true; return false; }
			return true;
		});
		if (aborted) return;
	} else if (indexType == "geo") {
		bool aborted = false;
		db_.scanPrefix(entityPrefix, [&](std::string_view key, std::string_view val) {
			size_t lastColon = key.rfind(':');
			if (lastColon == std::string::npos) return true;
			std::string pk(key.substr(lastColon + 1));

			BaseEntity::Blob blob(val.begin(), val.end());
			BaseEntity entity = BaseEntity::deserialize(pk, blob);

			std::string latField = column + "_lat";
			std::string lonField = column + "_lon";
			auto maybeLat = entity.extractField(latField);
			auto maybeLon = entity.extractField(lonField);
			if (!maybeLat || !maybeLon) { if (!advance()) { aborted = true; return false; } return true; }

			try {
				double lat = std::stod(*maybeLat);
				double lon = std::stod(*maybeLon);
				std::string geohash = encodeGeohash(lat, lon, 12);
				std::string gkey = makeGeoIndexKey(table, column, geohash, pk);
				writeIndexEntry(gkey, pk);
			} catch (...) {
				// skip invalid
			}
			if (!advance()) { aborted = true; return false; }
			return true;
		});
		if (aborted) return;
	} else if (indexType == "sparse") {
		bool aborted = false;
		db_.scanPrefix(entityPrefix, [&](std::string_view key, std::string_view val) {
			size_t lastColon = key.rfind(':');
			if (lastColon == std::string::npos) return true;
			std::string pk(key.substr(lastColon + 1));

			BaseEntity::Blob blob(val.begin(), val.end());
			BaseEntity entity = BaseEntity::deserialize(pk, blob);
			auto maybeVal = entity.extractField(column);
			if (!maybeVal || isNullOrEmpty_(maybeVal)) { if (!advance()) { aborted = true; return false; } return true; }

			std::string encoded = encodeKeyComponent(*maybeVal);
			std::string skey = makeSparseIndexKey(table, column, encoded, pk);
			writeIndexEntry(skey, pk);
			if (!advance()) { aborted = true; return false; }
			return true;
		});
		if (aborted) return;
	} else if (indexType == "range") {
		bool aborted = false;
		db_.scanPrefix(entityPrefix, [&](std::string_view key, std::string_view val) {
			size_t lastColon = key.rfind(':');
			if (lastColon == std::string::npos) return true;
			std::string pk(key.substr(lastColon + 1));

			BaseEntity::Blob blob(val.begin(), val.end());
			BaseEntity entity = BaseEntity::deserialize(pk, blob);
			auto maybeVal = entity.extractField(column);
			if (!maybeVal) { if (!advance()) { aborted = true; return false; } return true; }

			std::string rkey = makeRangeIndexKey(table, column, *maybeVal, pk);
			writeIndexEntry(rkey, pk);
			if (!advance()) { aborted = true; return false; }
			return true;
		});
		if (aborted) return;
	} else if (indexType == "composite") {
		// Parse columns
		std::vector<std::string> columns;
		size_t pos = 0;
		while (pos < column.size()) {
			size_t p = column.find('+', pos);
			if (p == std::string::npos) p = column.size();
			columns.push_back(column.substr(pos, p - pos));
			pos = p + 1;
		}

		bool aborted = false;
		db_.scanPrefix(entityPrefix, [&](std::string_view key, std::string_view val) {
			size_t lastColon = key.rfind(':');
			if (lastColon == std::string::npos) return true;
			std::string pk(key.substr(lastColon + 1));

			BaseEntity::Blob blob(val.begin(), val.end());
			BaseEntity entity = BaseEntity::deserialize(pk, blob);

			std::vector<std::string> values;
			for (const auto& col : columns) {
				auto maybeVal = entity.extractField(col);
				if (!maybeVal) { if (!advance()) { aborted = true; return false; } return true; }
				values.push_back(*maybeVal);
			}

			std::string ckey = makeCompositeIndexKey(table, columns, values, pk);
			writeIndexEntry(ckey, pk);
			if (!advance()) { aborted = true; return false; }
			return true;
		});
		if (aborted) return;
	} else if (indexType == "regular") {
		bool aborted = false;
		db_.scanPrefix(entityPrefix, [&](std::string_view key, std::string_view val) {
			size_t lastColon = key.rfind(':');
			if (lastColon == std::string::npos) return true;
			std::string pk(key.substr(lastColon + 1));

			BaseEntity::Blob blob(val.begin(), val.end());
			BaseEntity entity = BaseEntity::deserialize(pk, blob);
			auto maybeVal = entity.extractField(column);
			if (!maybeVal) { if (!advance()) { aborted = true; return false; } return true; }

			std::string encoded = encodeKeyComponent(*maybeVal);
			std::string idxKey = KeySchema::makeSecondaryIndexKey(table, column, encoded, pk);
			writeIndexEntry(idxKey, pk);
			if (!advance()) { aborted = true; return false; }
			return true;
		});
		if (aborted) return;
	}
	
	// Update metrics at the end
	auto end_time = std::chrono::steady_clock::now();
	auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
	
	rebuild_metrics_.rebuild_count.fetch_add(1, std::memory_order_relaxed);
	rebuild_metrics_.rebuild_duration_ms.fetch_add(duration_ms, std::memory_order_relaxed);
	rebuild_metrics_.rebuild_entities_processed.fetch_add(done, std::memory_order_relaxed);
}

SecondaryIndexManager::IndexStats
SecondaryIndexManager::getIndexStats(std::string_view table, std::string_view column) const {
	IndexStats stats;
	const std::string tableStr(table);
	const std::string columnStr(column);
	stats.table = tableStr;
	stats.column = columnStr;
	stats.entry_count = 0;
	stats.estimated_size_bytes = 0;
	stats.unique = false;

	// Helper to read meta value using RocksDBWrapper::get()
	auto readMeta = [&](const std::string& key)->std::optional<std::string> {
		auto opt = db_.get(key);
		if (!opt) return std::nullopt;
		return std::string(opt->begin(), opt->end());
	};

	bool found = false;

	// Composite index detection: column contains '+'
	if (columnStr.find('+') != std::string::npos && !found) {
		std::vector<std::string> cols;
		size_t pos = 0;
		while (pos < columnStr.size()) {
			size_t p = columnStr.find('+', pos);
			if (p == std::string::npos) p = columnStr.size();
			cols.push_back(columnStr.substr(pos, p - pos));
			pos = p + 1;
		}
		std::string metaKey = makeCompositeIndexMetaKey(table, cols);
		if (auto mv = readMeta(metaKey)) {
		stats.type = "composite";
		stats.unique = (mv->find("unique") != std::string::npos);
		// additional_info ist die Spaltenliste
		std::string colList;
		for (size_t i = 0; i < cols.size(); ++i) {
			if (i > 0) colList += ", ";
			colList += cols[i];
		}
		stats.additional_info = colList;			std::string prefix = std::string("idx:") + tableStr + ":" + columnStr + ":";
			db_.scanPrefix(prefix, [&stats](std::string_view /*k*/, std::string_view /*v*/) {
				stats.entry_count++;
				return true;
			});
			found = true;
		}
	}

	// TTL
	if (!found) {
		std::string metaKey = makeTTLIndexMetaKey(table, column);
		if (auto mv = readMeta(metaKey)) {
			stats.type = "ttl";
			stats.unique = false;
			stats.additional_info = std::string("ttl_seconds=") + *mv;

			std::string prefix = std::string("ttlidx:") + tableStr + ":" + columnStr + ":";
			db_.scanPrefix(prefix, [&stats](std::string_view /*k*/, std::string_view /*v*/) {
				stats.entry_count++;
				return true;
			});
			found = true;
		}
	}

	// Fulltext
	if (!found) {
		std::string metaKey = makeFulltextIndexMetaKey(table, column);
		if (auto mv = readMeta(metaKey)) {
			stats.type = "fulltext";
			stats.unique = false;
			stats.additional_info = "inverted_index";

			std::string prefix = std::string("ftidx:") + tableStr + ":" + columnStr + ":";
			db_.scanPrefix(prefix, [&stats](std::string_view /*k*/, std::string_view /*v*/) {
				stats.entry_count++;
				return true;
			});
			found = true;
		}
	}

	// Geo
	if (!found) {
		std::string metaKey = makeGeoIndexMetaKey(table, column);
		if (auto mv = readMeta(metaKey)) {
			stats.type = "geo";
			stats.unique = false;
			stats.additional_info = "geohash";

			std::string prefix = std::string("gidx:") + tableStr + ":" + columnStr + ":";
			db_.scanPrefix(prefix, [&stats](std::string_view /*k*/, std::string_view /*v*/) {
				stats.entry_count++;
				return true;
			});
			found = true;
		}
	}

	// Sparse
	if (!found) {
		std::string metaKey = makeSparseIndexMetaKey(table, column);
		if (auto mv = readMeta(metaKey)) {
			stats.type = "sparse";
			stats.unique = (*mv == "unique");
			stats.additional_info = *mv;

			std::string prefix = std::string("sidx:") + tableStr + ":" + columnStr + ":";
			db_.scanPrefix(prefix, [&stats](std::string_view /*k*/, std::string_view /*v*/) {
				stats.entry_count++;
				return true;
			});
			found = true;
		}
	}

	// Range
	if (!found) {
		std::string metaKey = makeRangeIndexMetaKey(table, column);
		if (auto mv = readMeta(metaKey)) {
			stats.type = "range";
			stats.unique = false;
			stats.additional_info = "sorted";

			std::string prefix = std::string("ridx:") + tableStr + ":" + columnStr + ":";
			db_.scanPrefix(prefix, [&stats](std::string_view /*k*/, std::string_view /*v*/) {
				stats.entry_count++;
				return true;
			});
			found = true;
		}
	}

	// Regular
	if (!found) {
		std::string metaKey = makeIndexMetaKey(table, column);
		if (auto mv = readMeta(metaKey)) {
			stats.type = "regular";
			stats.unique = (*mv == "unique");
			stats.additional_info = *mv;

			std::string prefix = std::string("idx:") + tableStr + ":" + columnStr + ":";
			db_.scanPrefix(prefix, [&stats](std::string_view /*k*/, std::string_view /*v*/) {
				stats.entry_count++;
				return true;
			});
			found = true;
		}
	}

	stats.estimated_size_bytes = stats.entry_count * 100;
	return stats;
}

void SecondaryIndexManager::reindexTable(const std::string& table) {
	std::unordered_set<std::string> columns;
	
	auto scanMetaPrefix = [&](const std::string& metaPrefix) {
		std::string prefix = metaPrefix + table + ":";
		db_.scanPrefix(prefix, [&](std::string_view key, std::string_view /*val*/) {
			std::string keyStr(key);
			size_t firstColon = keyStr.find(':');
			if (firstColon != std::string::npos) {
				size_t secondColon = keyStr.find(':', firstColon + 1);
				if (secondColon != std::string::npos) {
					std::string column = keyStr.substr(secondColon + 1);
					size_t thirdColon = column.find(':');
					if (thirdColon != std::string::npos) {
						column = column.substr(0, thirdColon);
					}
					columns.insert(column);
				}
			}
			return true;
		});
	};
	
	scanMetaPrefix("idxmeta:");
	scanMetaPrefix("ridxmeta:");
	scanMetaPrefix("sidxmeta:");
	scanMetaPrefix("gidxmeta:");
	scanMetaPrefix("ttlidxmeta:");
	scanMetaPrefix("ftidxmeta:");
	scanMetaPrefix("cidxmeta:");
	
	for (const auto& column : columns) {
		rebuildIndex(table, column);
	}
}

// ============================================================================
// MVCC Transaction Variants
// ============================================================================

SecondaryIndexManager::Status SecondaryIndexManager::put(
	std::string_view table, 
	const BaseEntity& entity, 
	RocksDBWrapper::TransactionWrapper& txn) {
	
	if (table.empty()) return Status::Error("put(mvcc): table darf nicht leer sein");
	if (entity.getPrimaryKey().empty()) return Status::Error("put(mvcc): pk darf nicht leer sein");
	if (!db_.isOpen()) return Status::Error("put(mvcc): Datenbank ist nicht geöffnet");
	if (!txn.isActive()) return Status::Error("put(mvcc): Transaction ist nicht aktiv");

	const std::string& pk = entity.getPrimaryKey();
	const std::string relKey = KeySchema::makeRelationalKey(table, pk);

	// Alte Entity lesen (mit MVCC Snapshot)
	std::optional<std::vector<uint8_t>> oldBlob = txn.get(relKey);
	std::unique_ptr<BaseEntity> oldEntity;
	if (oldBlob) {
		try { 
			oldEntity = std::make_unique<BaseEntity>(BaseEntity::deserialize(pk, *oldBlob)); 
		}
		catch (...) { 
			THEMIS_WARN("put(mvcc): alte Entity für PK={} nicht deserialisierbar", pk); 
		}
	}

	// Neue Entity schreiben
	txn.put(relKey, entity.serialize());

	// Indexes aktualisieren
	if (oldEntity) {
		auto delStatus = updateIndexesForDelete_(table, pk, oldEntity.get(), txn);
		if (!delStatus.ok) return delStatus;
	}
	return updateIndexesForPut_(table, pk, entity, txn);
}

SecondaryIndexManager::Status SecondaryIndexManager::erase(
	std::string_view table, 
	std::string_view pk, 
	RocksDBWrapper::TransactionWrapper& txn) {
	
	if (table.empty()) return Status::Error("erase(mvcc): table darf nicht leer sein");
	if (pk.empty()) return Status::Error("erase(mvcc): pk darf nicht leer sein");
	if (!db_.isOpen()) return Status::Error("erase(mvcc): Datenbank ist nicht geöffnet");
	if (!txn.isActive()) return Status::Error("erase(mvcc): Transaction ist nicht aktiv");

	const std::string relKey = KeySchema::makeRelationalKey(table, pk);
	
	// Alte Entity lesen (mit MVCC Snapshot)
	std::optional<std::vector<uint8_t>> oldBlob = txn.get(relKey);
	std::unique_ptr<BaseEntity> oldEntity;
	if (oldBlob) {
		try { 
			oldEntity = std::make_unique<BaseEntity>(BaseEntity::deserialize(std::string(pk), *oldBlob)); 
		}
		catch (...) { 
			THEMIS_WARN("erase(mvcc): alte Entity für PK={} nicht deserialisierbar", pk); 
		}
	}

	txn.del(relKey);
	return updateIndexesForDelete_(table, pk, oldEntity.get(), txn);
}

// ============================================================================
// MVCC Helper Methods
// ============================================================================

SecondaryIndexManager::Status SecondaryIndexManager::updateIndexesForPut_(
	std::string_view table,
	std::string_view pk,
	const BaseEntity& newEntity,
	RocksDBWrapper::TransactionWrapper& txn) {
	
	// Alle für diese Tabelle vorhandenen Indexspalten laden (Single + Composite)
	auto indexedCols = loadIndexedColumns_(table);
	// Zusätzlich: Range-Index-Spalten laden (können unabhängig von Equality-Indizes existieren)
	auto rangeCols = loadRangeIndexedColumns_(table);

	// Trennen: Single-Column vs. Composite (enthält '+')
	for (const auto& col : indexedCols) {
		if (col.find('+') == std::string::npos) {
			// Single-Column
			auto maybe = newEntity.extractField(col);
			if (!maybe) continue;
			const std::string encodedVal = encodeKeyComponent(*maybe);
			
			// Unique-Constraint prüfen
			if (isUniqueIndex_(table, col)) {
				// Prüfe ob bereits ein anderer PK mit diesem Wert existiert
				std::string prefix = std::string("idx:") + std::string(table) + ":" + col + ":" + encodedVal + ":";
				bool conflict = false;
				db_.scanPrefix(prefix, [&pk, &conflict](std::string_view key, std::string_view /*val*/) {
					// Extrahiere PK aus key: idx:table:column:value:PK
					size_t lastColon = key.rfind(':');
					if (lastColon != std::string_view::npos) {
						std::string_view existingPK = key.substr(lastColon + 1);
						if (existingPK != pk) {
							conflict = true;
							return false; // Stop scan
						}
					}
					return true;
				});
				if (conflict) {
					return Status::Error("Unique constraint violation: " + std::string(table) + "." + col + " = " + *maybe);
				}
			}
			
			const std::string idxKey = KeySchema::makeSecondaryIndexKey(table, col, encodedVal, pk);
			std::vector<uint8_t> pkBytes = toBytes(pk);
			txn.put(idxKey, pkBytes);
			
			// Falls Range-Index für diese Spalte existiert, ebenfalls pflegen
			if (rangeCols.find(col) != rangeCols.end()) {
				const std::string rkey = makeRangeIndexKey(table, col, *maybe, pk);
				txn.put(rkey, pkBytes);
			}
		} else {
			// Composite: col = "col1+col2+..."
			// Parse columns
			std::vector<std::string> columns;
			size_t start = 0;
			while (start < col.size()) {
				size_t pos = col.find('+', start);
				if (pos == std::string::npos) {
					columns.push_back(col.substr(start));
					break;
				}
				columns.push_back(col.substr(start, pos - start));
				start = pos + 1;
			}
			
			// Extract values
			std::vector<std::string> values;
			values.reserve(columns.size());
			bool allPresent = true;
			for (const auto& c : columns) {
				auto maybe = newEntity.extractField(c);
				if (!maybe) {
					allPresent = false;
					break;
				}
				values.push_back(*maybe);
			}
			
			if (!allPresent) continue; // Skip wenn nicht alle Felder vorhanden
			
			// Unique-Constraint prüfen für Composite Index
			if (isUniqueCompositeIndex_(table, columns)) {
				// Prüfe ob bereits ein anderer PK mit dieser Wertekombination existiert
				std::string prefix = makeCompositeIndexPrefix(table, columns, values);
				bool conflict = false;
				db_.scanPrefix(prefix, [&pk, &conflict](std::string_view key, std::string_view /*val*/) {
					// Extrahiere PK aus key (letztes Segment nach ':')
					size_t lastColon = key.rfind(':');
					if (lastColon != std::string_view::npos) {
						std::string_view existingPK = key.substr(lastColon + 1);
						if (existingPK != pk) {
							conflict = true;
							return false; // Stop scan
						}
					}
					return true;
				});
				if (conflict) {
					std::string valueStr;
					for (size_t i = 0; i < values.size(); ++i) {
						if (i > 0) valueStr += ", ";
						valueStr += columns[i] + "=" + values[i];
					}
					return Status::Error("Unique constraint violation: " + std::string(table) + ".{" + valueStr + "}");
				}
			}
			
			const std::string idxKey = makeCompositeIndexKey(table, columns, values, pk);
			std::vector<uint8_t> pkBytes = toBytes(pk);
			txn.put(idxKey, pkBytes);
		}
	}

	// Zusätzlich: Range-Indizes pflegen, die keine Equality-Indizes haben
	for (const auto& rcol : rangeCols) {
		// Wenn diese Spalte bereits im obigen Loop gepflegt wurde (weil Equality-Index existiert), überspringen
		if (indexedCols.find(rcol) != indexedCols.end()) continue;
		// Nur Single-Column Range-Indizes unterstützen (Composite-Range-Indizes sind nicht implementiert)
		auto maybe = newEntity.extractField(rcol);
		if (!maybe) continue;
		std::vector<uint8_t> pkBytes = toBytes(pk);
		const std::string rkey = makeRangeIndexKey(table, rcol, *maybe, pk);
		txn.put(rkey, pkBytes);
	}

	// Sparse-Indizes pflegen
	auto sparseCols = loadSparseIndexedColumns_(table);
	for (const auto& scol : sparseCols) {
		auto maybe = newEntity.extractField(scol);
		if (!maybe || isNullOrEmpty_(*maybe)) continue; // Skip NULL/empty values
		
		const std::string encodedVal = encodeKeyComponent(*maybe);
		
		// Unique-Constraint prüfen für Sparse Index
		if (isSparseIndexUnique_(table, scol)) {
			std::string prefix = makeSparseIndexKey(table, scol, encodedVal, "");
			bool conflict = false;
			db_.scanPrefix(prefix, [&pk, &conflict](std::string_view key, std::string_view /*val*/) {
				size_t lastColon = key.rfind(':');
				if (lastColon != std::string_view::npos) {
					std::string_view extractedPK = key.substr(lastColon + 1);
					if (extractedPK != pk) {
						conflict = true;
						return false;
					}
				}
				return true;
			});
			if (conflict) {
				return Status::Error("Sparse unique constraint violation: " + std::string(table) + "." + scol + " = " + *maybe);
			}
		}
		
		const std::string sidxKey = makeSparseIndexKey(table, scol, encodedVal, pk);
		std::vector<uint8_t> pkBytes = toBytes(pk);
		txn.put(sidxKey, pkBytes);
	}

	// Geo-Indizes pflegen
	auto geoCols = loadGeoIndexedColumns_(table);
	for (const auto& gcol : geoCols) {
		// Geo-Index erwartet zwei Felder: gcol_lat und gcol_lon (oder einfach lat/lon)
		// Konvention: Spaltenname ist z.B. "location", dann Felder "location_lat" und "location_lon"
		std::string latField = gcol + "_lat";
		std::string lonField = gcol + "_lon";
		
		auto maybeLat = newEntity.extractField(latField);
		auto maybeLon = newEntity.extractField(lonField);
		
		if (!maybeLat || !maybeLon) continue; // Skip wenn Koordinaten fehlen
		
		try {
			double lat = std::stod(*maybeLat);
			double lon = std::stod(*maybeLon);
			
			std::string geohash = encodeGeohash(lat, lon);
			const std::string gidxKey = makeGeoIndexKey(table, gcol, geohash, pk);
			std::vector<uint8_t> pkBytes = toBytes(pk);
			txn.put(gidxKey, pkBytes);
		} catch (...) {
			THEMIS_WARN("updateIndexesForPut_(mvcc): Ungültige Geo-Koordinaten für {}.{}: lat={}, lon={}", 
					   table, gcol, *maybeLat, *maybeLon);
			continue;
		}
	}

	// TTL-Indizes pflegen
	auto ttlCols = loadTTLIndexedColumns_(table);
	for (const auto& tcol : ttlCols) {
		auto maybeValue = newEntity.extractField(tcol);
		if (!maybeValue) continue;
		
		// Calculate expire timestamp: now + TTL seconds
		auto now = std::chrono::system_clock::now();
		auto epoch = now.time_since_epoch();
		int64_t currentTimestamp = std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
		int64_t ttlSeconds = getTTLSeconds_(table, tcol);
		if (ttlSeconds <= 0) continue;
		
		int64_t expireTimestamp = currentTimestamp + ttlSeconds;
		const std::string ttlKey = makeTTLIndexKey(table, tcol, expireTimestamp, pk);
		std::vector<uint8_t> pkBytes = toBytes(pk);
		txn.put(ttlKey, pkBytes);
	}

	// Fulltext-Indizes pflegen
	auto fulltextCols = loadFulltextIndexedColumns_(table);
	for (const auto& fcol : fulltextCols) {
		auto maybeText = newEntity.extractField(fcol);
		if (!maybeText || isNullOrEmpty_(maybeText)) continue;
		
		// Get index config and tokenize with stemming if enabled
		auto config = getFulltextConfig(table, fcol).value_or(FulltextConfig{});
		auto tokens = tokenize(*maybeText, config);
		
		std::unordered_map<std::string, uint32_t> tf;
		for (const auto& t : tokens) { if (!t.empty()) tf[t]++; }
		const std::string dkey = makeFulltextDocLenKey(table, fcol, pk);
		{
			std::string s = std::to_string(tokens.size());
			std::vector<uint8_t> val(s.begin(), s.end());
			txn.put(dkey, val);
		}
		std::vector<uint8_t> pkBytes = toBytes(pk);
		for (const auto& [token, count] : tf) {
			const std::string ftKey = makeFulltextIndexKey(table, fcol, token, pk);
			txn.put(ftKey, pkBytes);
			const std::string tfKey = makeFulltextTFKey(table, fcol, token, pk);
			std::string s = std::to_string(count);
			std::vector<uint8_t> tfVal(s.begin(), s.end());
			txn.put(tfKey, tfVal);
		}
	}

	return Status::OK();
}

SecondaryIndexManager::Status SecondaryIndexManager::updateIndexesForDelete_(
	std::string_view table,
	std::string_view pk,
	const BaseEntity* oldEntityOpt,
	RocksDBWrapper::TransactionWrapper& txn) {
	
	if (!oldEntityOpt) {
		// Falls keine alte Entity, können wir die spezifischen Index-Keys nicht sicher bestimmen.
		// Defensive strategy: alle Index-Prefixe für diesen PK löschen via Scan.
		auto indexedCols = loadIndexedColumns_(table);
		for (const auto& col : indexedCols) {
			std::string prefix;
			if (col.find('+') == std::string::npos) {
				// Single
				prefix = std::string("idx:") + std::string(table) + ":" + col + ":";
			} else {
				// Composite
				prefix = std::string("idx:") + std::string(table) + ":" + col + ":";
			}
			db_.scanPrefix(prefix, [this, &pk, &txn](std::string_view key, std::string_view /*val*/){
				// Prüfen, ob PK am Ende passt (endet mit :PK)
				std::string_view keyView(key);
				size_t lastColon = keyView.rfind(':');
				if (lastColon != std::string_view::npos) {
					std::string_view extractedPK = keyView.substr(lastColon + 1);
					if (extractedPK == pk) {
						txn.del(std::string(key));
					}
				}
				return true;
			});
		}
		// Auch alle Range-Index-Einträge mit diesem PK für diese Tabelle entfernen
		auto rangeCols = loadRangeIndexedColumns_(table);
		for (const auto& rcol : rangeCols) {
			std::string rprefix = std::string("ridx:") + std::string(table) + ":" + rcol + ":";
			db_.scanPrefix(rprefix, [&pk, &txn](std::string_view key, std::string_view /*val*/){
				size_t lastColon = key.rfind(':');
				if (lastColon != std::string_view::npos) {
					std::string_view existingPK = key.substr(lastColon + 1);
					if (existingPK == pk) {
						txn.del(std::string(key));
					}
				}
				return true;
			});
		}
		return Status::OK();
	}

	// Zielgerichtet löschen basierend auf alten Feldwerten
	auto indexedCols = loadIndexedColumns_(table);
	for (const auto& col : indexedCols) {
		if (col.find('+') == std::string::npos) {
			// Single-Column
			auto maybe = oldEntityOpt->extractField(col);
			if (!maybe) continue;
			const std::string encodedVal = encodeKeyComponent(*maybe);
			const std::string idxKey = KeySchema::makeSecondaryIndexKey(table, col, encodedVal, pk);
			txn.del(idxKey);
			// Auch Range-Index-Eintrag löschen, falls vorhanden
			auto rangeCols = loadRangeIndexedColumns_(table);
			if (rangeCols.find(col) != rangeCols.end()) {
				const std::string rkey = makeRangeIndexKey(table, col, *maybe, pk);
				txn.del(rkey);
			}
		} else {
			// Composite
			std::vector<std::string> columns;
			size_t start = 0;
			while (start < col.size()) {
				size_t pos = col.find('+', start);
				if (pos == std::string::npos) {
					columns.push_back(col.substr(start));
					break;
				}
				columns.push_back(col.substr(start, pos - start));
				start = pos + 1;
			}
			
			std::vector<std::string> values;
			values.reserve(columns.size());
			bool allPresent = true;
			for (const auto& c : columns) {
				auto maybe = oldEntityOpt->extractField(c);
				if (!maybe) {
					allPresent = false;
					break;
				}
				values.push_back(*maybe);
			}
			
			if (!allPresent) continue;
			
			const std::string idxKey = makeCompositeIndexKey(table, columns, values, pk);
			txn.del(idxKey);
		}
	}

	// Zusätzlich: Range-Indizes löschen, die keine passenden Equality-Indizes haben
	{
		auto rangeCols = loadRangeIndexedColumns_(table);
		for (const auto& rcol : rangeCols) {
			if (indexedCols.find(rcol) != indexedCols.end()) continue; // bereits oben behandelt
			auto maybe = oldEntityOpt->extractField(rcol);
			if (!maybe) continue;
			const std::string rkey = makeRangeIndexKey(table, rcol, *maybe, pk);
			txn.del(rkey);
		}
	}

	// Sparse-Indizes löschen
	{
		auto sparseCols = loadSparseIndexedColumns_(table);
		for (const auto& scol : sparseCols) {
			auto maybe = oldEntityOpt->extractField(scol);
			if (!maybe || isNullOrEmpty_(*maybe)) continue; // War nicht im Index
			const std::string encodedVal = encodeKeyComponent(*maybe);
			const std::string sidxKey = makeSparseIndexKey(table, scol, encodedVal, pk);
			txn.del(sidxKey);
		}
	}

	// Geo-Indizes löschen
	{
		auto geoCols = loadGeoIndexedColumns_(table);
		for (const auto& gcol : geoCols) {
			std::string latField = gcol + "_lat";
			std::string lonField = gcol + "_lon";
			
			auto maybeLat = oldEntityOpt->extractField(latField);
			auto maybeLon = oldEntityOpt->extractField(lonField);
			
			if (!maybeLat || !maybeLon) continue;
			
			try {
				double lat = std::stod(*maybeLat);
				double lon = std::stod(*maybeLon);
				
				std::string geohash = encodeGeohash(lat, lon);
				const std::string gidxKey = makeGeoIndexKey(table, gcol, geohash, pk);
				txn.del(gidxKey);
			} catch (...) {
				// Koordinaten waren ungültig, wahrscheinlich war kein Index-Eintrag vorhanden
				continue;
			}
		}
	}

	// TTL-Indizes löschen
	{
		auto ttlCols = loadTTLIndexedColumns_(table);
		for (const auto& tcol : ttlCols) {
			auto maybeValue = oldEntityOpt->extractField(tcol);
			if (!maybeValue) continue;
			
			// We need to find the TTL index entry, but we don't know the exact timestamp
			// Scan the TTL index prefix and delete matching PKs
			std::string prefix = makeTTLIndexPrefix(table, tcol);
			db_.scanPrefix(prefix, [&pk, &txn](std::string_view key, std::string_view /*val*/) {
				// Extract PK from ttlidx:table:column:timestamp:PK
				size_t lastColon = key.rfind(':');
				if (lastColon != std::string_view::npos) {
					std::string_view extractedPK = key.substr(lastColon + 1);
					if (extractedPK == pk) {
						txn.del(std::string(key));
						return false; // Stop after finding the matching entry
					}
				}
				return true;
			});
		}
	}

	// Fulltext-Indizes löschen
	{
		auto fulltextCols = loadFulltextIndexedColumns_(table);
		for (const auto& fcol : fulltextCols) {
			auto maybeText = oldEntityOpt->extractField(fcol);
			if (!maybeText || isNullOrEmpty_(maybeText)) continue;
			
			// Get index config and tokenize with same settings as index
			auto config = getFulltextConfig(table, fcol).value_or(FulltextConfig{});
			auto tokens = tokenize(*maybeText, config);
			
			std::unordered_set<std::string> uniqueTokens(tokens.begin(), tokens.end());
			for (const auto& token : uniqueTokens) {
				if (token.empty()) continue;
				const std::string ftKey = makeFulltextIndexKey(table, fcol, token, pk);
				txn.del(ftKey);
				const std::string tfKey = makeFulltextTFKey(table, fcol, token, pk);
				txn.del(tfKey);
			}
			// DocLength löschen
			const std::string dkey = makeFulltextDocLenKey(table, fcol, pk);
			txn.del(dkey);
		}
	}

	return Status::OK();
}

} // namespace themis