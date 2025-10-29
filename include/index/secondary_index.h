#pragma once

#include "storage/rocksdb_wrapper.h"
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <functional>
#include <utility>
#include <unordered_set>

namespace themis {

class BaseEntity;

/// SecondaryIndexManager
/// - Gleichheitsbasierte Sekundärindizes pro Tabelle/Spalte(n)
/// - Single-Column Key-Schema: idx:table:column:value:PK
/// - Composite Key-Schema: idx:table:col1+col2:val1:val2:PK
/// - Atomare Pflege via RocksDB WriteBatch (Put/Delete + Index-Updates)
/// - Keine Exceptions im API: Status-Objekt mit klaren Fehlermeldungen
class SecondaryIndexManager {
public:
    struct Status {
        bool ok = true;
        std::string message;
        static Status OK() { return {}; }
        static Status Error(std::string msg) { return Status{false, std::move(msg)}; }
    };

    explicit SecondaryIndexManager(RocksDBWrapper& db);

    // Index-Lifecycle
    Status createIndex(std::string_view table, std::string_view column, bool unique = false);
    Status createCompositeIndex(std::string_view table, const std::vector<std::string>& columns, bool unique = false);
    Status dropIndex(std::string_view table, std::string_view column);
    Status dropCompositeIndex(std::string_view table, const std::vector<std::string>& columns);
    bool hasIndex(std::string_view table, std::string_view column) const;
    bool hasCompositeIndex(std::string_view table, const std::vector<std::string>& columns) const;

    // Range-/Sort-Index (lexikografisch über String-Encoding)
    Status createRangeIndex(std::string_view table, std::string_view column);
    Status dropRangeIndex(std::string_view table, std::string_view column);
    bool hasRangeIndex(std::string_view table, std::string_view column) const;

    // Sparse-Index: überspringt NULL/fehlende Werte (reduziert Index-Größe)
    Status createSparseIndex(std::string_view table, std::string_view column, bool unique = false);
    Status dropSparseIndex(std::string_view table, std::string_view column);
    bool hasSparseIndex(std::string_view table, std::string_view column) const;

    // Geo-Index: GeoJSON-Punkt-Speicherung mit Bounding-Box und Radius-Queries
    // Erwartet Felder: "lat" (double) und "lon" (double) oder GeoJSON "geometry"
    Status createGeoIndex(std::string_view table, std::string_view column);
    Status dropGeoIndex(std::string_view table, std::string_view column);
    bool hasGeoIndex(std::string_view table, std::string_view column) const;

    // TTL-Index: Time-To-Live für automatisches Löschen nach Ablauf
    // ttl_seconds: Lebensdauer in Sekunden
    Status createTTLIndex(std::string_view table, std::string_view column, int64_t ttl_seconds);
    Status dropTTLIndex(std::string_view table, std::string_view column);
    bool hasTTLIndex(std::string_view table, std::string_view column) const;
    
    // TTL-Cleanup: Löscht abgelaufene Entities (periodisch aufrufen)
    std::pair<Status, size_t> cleanupExpiredEntities(std::string_view table, std::string_view column);

    // Fulltext-Index: Inverted Index für Textsuche
    Status createFulltextIndex(std::string_view table, std::string_view column);
    Status dropFulltextIndex(std::string_view table, std::string_view column);
    bool hasFulltextIndex(std::string_view table, std::string_view column) const;
    
    // Fulltext-Suche: AND-Logik für alle Tokens
    std::pair<Status, std::vector<std::string>> scanFulltext(
        std::string_view table,
        std::string_view column,
        std::string_view query,
        size_t limit = 1000
    ) const;

    // Geo-Queries: Bounding Box [minLat, maxLat] x [minLon, maxLon]
    std::pair<Status, std::vector<std::string>> scanGeoBox(
        std::string_view table,
        std::string_view column,
        double minLat, double maxLat,
        double minLon, double maxLon,
        size_t limit = 1000
    ) const;

    // Geo-Queries: Radius-Search um (centerLat, centerLon) mit radiusKm
    std::pair<Status, std::vector<std::string>> scanGeoRadius(
        std::string_view table,
        std::string_view column,
        double centerLat, double centerLon,
        double radiusKm,
        size_t limit = 1000
    ) const;

    // Range-Scan: [lower, upper] je nach Inclusives; leeres optional = ungebunden
    std::pair<Status, std::vector<std::string>> scanKeysRange(
        std::string_view table,
        std::string_view column,
        const std::optional<std::string>& lower,
        const std::optional<std::string>& upper,
        bool includeLower,
        bool includeUpper,
        size_t limit = 1000,
        bool reversed = false
    ) const;

    // Datenpflege (atomar, inkl. Indizes)
    Status put(std::string_view table, const BaseEntity& entity);
    Status erase(std::string_view table, std::string_view pk);

    // Varianten für Transaktionen: nutzen bestehende WriteBatch
    Status put(std::string_view table, const BaseEntity& entity, RocksDBWrapper::WriteBatchWrapper& batch);
    Status erase(std::string_view table, std::string_view pk, RocksDBWrapper::WriteBatchWrapper& batch);
    
    // MVCC Transaction variants
    Status put(std::string_view table, const BaseEntity& entity, RocksDBWrapper::TransactionWrapper& txn);
    Status erase(std::string_view table, std::string_view pk, RocksDBWrapper::TransactionWrapper& txn);

    // Abfragen über Index = Gleichheit
    // Liefert primäre Schlüssel
    std::pair<Status, std::vector<std::string>> scanKeysEqual(
        std::string_view table,
        std::string_view column,
        std::string_view value
    ) const;
    
    // Composite index scan: alle Spalten müssen exakt matchen
    std::pair<Status, std::vector<std::string>> scanKeysEqualComposite(
        std::string_view table,
        const std::vector<std::string>& columns,
        const std::vector<std::string>& values
    ) const;

    // Liefert komplett deserialisierte Entities
    std::pair<Status, std::vector<BaseEntity>> scanEntitiesEqual(
        std::string_view table,
        std::string_view column,
        std::string_view value
    ) const;
    
    std::pair<Status, std::vector<BaseEntity>> scanEntitiesEqualComposite(
        std::string_view table,
        const std::vector<std::string>& columns,
        const std::vector<std::string>& values
    ) const;

    // Schätzung der Trefferanzahl (nur Zählen bis maxProbe; capped=true wenn abgeschnitten)
    size_t estimateCountEqual(
        std::string_view table,
        std::string_view column,
        std::string_view value,
        size_t maxProbe = 1000,
        bool* capped = nullptr
    ) const;
    
    size_t estimateCountEqualComposite(
        std::string_view table,
        const std::vector<std::string>& columns,
        const std::vector<std::string>& values,
        size_t maxProbe = 1000,
        bool* capped = nullptr
    ) const;

    // Utility: Geohash-Encoding (Lat/Lon -> Morton-Code mit konfigurierbarer Precision)
    static std::string encodeGeohash(double lat, double lon, int precision = 12);
    static std::pair<double, double> decodeGeohash(std::string_view geohash);
    
    // Utility: Haversine-Distanz in Kilometern
    static double haversineDistance(double lat1, double lon1, double lat2, double lon2);
    
    // Utility: Fulltext-Tokenizer (Whitespace + Lowercase)
    static std::vector<std::string> tokenize(std::string_view text);

    // Index-Statistiken und Wartung
    struct IndexStats {
        std::string type;              // "regular", "composite", "range", "sparse", "geo", "ttl", "fulltext"
        std::string table;
        std::string column;            // oder col1+col2+... für Composite
        size_t entry_count = 0;        // Anzahl Index-Einträge
        size_t estimated_size_bytes = 0; // Geschätzte Größe (Keys + Values)
        bool unique = false;           // Unique-Constraint
        std::string additional_info;   // Typ-spezifische Infos
    };
    
    // Liefert Statistiken für einen bestimmten Index (erkennt Typ automatisch)
    IndexStats getIndexStats(std::string_view table, std::string_view column) const;
    
    // Liefert Statistiken für alle Indizes einer Tabelle
    std::vector<IndexStats> getAllIndexStats(const std::string& table);
    
    // Rebuild eines Index (nützlich bei Inkonsistenzen)
    void rebuildIndex(const std::string& table, const std::string& column);
    // Rebuild mit Fortschritts-Callback: progress(done,total) -> true=weiter, false=abbrechen
    void rebuildIndex(const std::string& table, const std::string& column,
                      std::function<bool(size_t,size_t)> progress);
    
    // Rebuild aller Indizes einer Tabelle
    void reindexTable(const std::string& table);

    // Metriken für Rebuild-Operationen
    struct RebuildMetrics {
        std::atomic<uint64_t> rebuild_count{0};           // Anzahl durchgeführter Rebuilds
        std::atomic<uint64_t> rebuild_duration_ms{0};     // Gesamtdauer aller Rebuilds in ms
        std::atomic<uint64_t> rebuild_entities_processed{0}; // Anzahl verarbeiteter Entities
    };
    
    RebuildMetrics& getRebuildMetrics() { return rebuild_metrics_; }
    const RebuildMetrics& getRebuildMetrics() const { return rebuild_metrics_; }

private:
    RocksDBWrapper& db_;
    RebuildMetrics rebuild_metrics_;

    // Meta-Key für vorhandene Indizes: idxmeta:<table>:<column>
    // Composite: idxmeta:<table>:col1+col2+col3
    // Meta-Value: "unique" oder "" (leer = nicht-unique)
    static std::string makeIndexMetaKey(std::string_view table, std::string_view column);
    static std::string makeCompositeIndexMetaKey(std::string_view table, const std::vector<std::string>& columns);

    // Range-Index-Metadaten: ridxmeta:<table>:<column>
    static std::string makeRangeIndexMetaKey(std::string_view table, std::string_view column);

    // Sparse-Index-Metadaten: sidxmeta:<table>:<column>
    static std::string makeSparseIndexMetaKey(std::string_view table, std::string_view column);

    // Geo-Index-Metadaten: gidxmeta:<table>:<column>
    static std::string makeGeoIndexMetaKey(std::string_view table, std::string_view column);
    
    // TTL-Index-Metadaten: ttlidxmeta:<table>:<column> -> Value: TTL-Sekunden als String
    static std::string makeTTLIndexMetaKey(std::string_view table, std::string_view column);
    
    // Fulltext-Index-Metadaten: ftidxmeta:<table>:<column>
    static std::string makeFulltextIndexMetaKey(std::string_view table, std::string_view column);
    
    // Index-Key-Builder
    // Single: idx:table:column:value:PK
    // Composite: idx:table:col1+col2:val1:val2:PK
    static std::string makeIndexKey(std::string_view table, std::string_view column, std::string_view value, std::string_view pk);
    static std::string makeCompositeIndexKey(std::string_view table, const std::vector<std::string>& columns, const std::vector<std::string>& values, std::string_view pk);
    static std::string makeCompositeIndexPrefix(std::string_view table, const std::vector<std::string>& columns, const std::vector<std::string>& values);

    // Range-Index-Key-Builder: ridx:table:column:value:PK und Prefix ridx:table:column:value:
    static std::string makeRangeIndexKey(std::string_view table, std::string_view column, std::string_view value, std::string_view pk);
    static std::string makeRangeIndexPrefix(std::string_view table, std::string_view column, std::string_view valuePrefix);

    // Sparse-Index-Key-Builder: sidx:table:column:value:PK (wie idx, aber NULL-Werte werden übersprungen)
    static std::string makeSparseIndexKey(std::string_view table, std::string_view column, std::string_view value, std::string_view pk);

    // Geo-Index-Key-Builder: gidx:table:column:geohash:PK
    // Geohash: Z-Order-Curve (Morton-Code) für räumliche Lokalität
    static std::string makeGeoIndexKey(std::string_view table, std::string_view column, std::string_view geohash, std::string_view pk);
    static std::string makeGeoIndexPrefix(std::string_view table, std::string_view column, std::string_view geohashPrefix);

    // TTL-Index-Key-Builder: ttlidx:table:column:timestamp:PK
    // timestamp: Unix-Timestamp (Sekunden) als Expire-Time
    static std::string makeTTLIndexKey(std::string_view table, std::string_view column, int64_t expireTimestamp, std::string_view pk);
    static std::string makeTTLIndexPrefix(std::string_view table, std::string_view column);
    
    // Fulltext-Index-Key-Builder: ftidx:table:column:token:PK
    // token: Einzelnes Wort aus tokenisiertem Text (lowercase)
    static std::string makeFulltextIndexKey(std::string_view table, std::string_view column, std::string_view token, std::string_view pk);
    static std::string makeFulltextIndexPrefix(std::string_view table, std::string_view column, std::string_view token);

    // Prüft ob Index unique ist
    bool isUniqueIndex_(std::string_view table, std::string_view column) const;
    bool isUniqueCompositeIndex_(std::string_view table, const std::vector<std::string>& columns) const;
    bool isSparseIndexUnique_(std::string_view table, std::string_view column) const;

    // Sichere Kodierung für Key-Komponenten (':' und '%' werden percent-encodiert)
    static std::string encodeKeyComponent(std::string_view raw);

    // Hilfsfunktionen
    Status updateIndexesForPut_(std::string_view table,
                                std::string_view pk,
                                const BaseEntity& newEntity,
                                RocksDBWrapper::WriteBatchWrapper& batch);
    Status updateIndexesForDelete_(std::string_view table,
                                   std::string_view pk,
                                   const BaseEntity* oldEntityOpt,
                                   RocksDBWrapper::WriteBatchWrapper& batch);
    
    // MVCC Transaction Varianten
    Status updateIndexesForPut_(std::string_view table,
                                std::string_view pk,
                                const BaseEntity& newEntity,
                                RocksDBWrapper::TransactionWrapper& txn);
    Status updateIndexesForDelete_(std::string_view table,
                                   std::string_view pk,
                                   const BaseEntity* oldEntityOpt,
                                   RocksDBWrapper::TransactionWrapper& txn);
    
    std::unordered_set<std::string> loadIndexedColumns_(std::string_view table) const;
    std::unordered_set<std::string> loadRangeIndexedColumns_(std::string_view table) const;
    std::unordered_set<std::string> loadSparseIndexedColumns_(std::string_view table) const;
    std::unordered_set<std::string> loadGeoIndexedColumns_(std::string_view table) const;
    std::unordered_set<std::string> loadTTLIndexedColumns_(std::string_view table) const;
    std::unordered_set<std::string> loadFulltextIndexedColumns_(std::string_view table) const;
    
    // TTL-Helpers
    int64_t getTTLSeconds_(std::string_view table, std::string_view column) const;
    
    // Prüft ob Feld-Wert NULL/leer ist (für Sparse-Index)
    static bool isNullOrEmpty_(const std::optional<std::string>& value);
};

} // namespace themis
