// Vector ANN index implementation

#include "index/vector_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/key_schema.h"
#include "storage/base_entity.h"
#include "utils/logger.h"

#ifdef THEMIS_HNSW_ENABLED
#include <hnswlib/hnswlib.h>
#endif

#include <algorithm>
#include <queue>
#include <limits>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace themis {

VectorIndexManager::VectorIndexManager(RocksDBWrapper& db) : db_(db) {}

float VectorIndexManager::l2(const std::vector<float>& a, const std::vector<float>& b) {
	float s = 0.0f;
	for (size_t i = 0; i < a.size(); ++i) {
		float d = a[i] - b[i];
		s += d * d;
	}
	return s;
}

float VectorIndexManager::cosineOneMinus(const std::vector<float>& a, const std::vector<float>& b) {
	float dot = 0.0f, na = 0.0f, nb = 0.0f;
	for (size_t i = 0; i < a.size(); ++i) {
		dot += a[i] * b[i];
		na += a[i] * a[i];
		nb += b[i] * b[i];
	}
	float denom = std::sqrt(std::max(na * nb, 1e-12f));
	float cosv = denom > 0 ? (dot / denom) : 0.0f;
	return 1.0f - cosv;
}

void VectorIndexManager::normalizeL2(std::vector<float>& v) {
	float n2 = 0.0f;
	for (float x : v) n2 += x * x;
	float n = std::sqrt(std::max(n2, 1e-12f));
	if (n > 0.f) {
		for (float& x : v) x /= n;
	}
}

float VectorIndexManager::distance(const std::vector<float>& a, const std::vector<float>& b) const {
	return (metric_ == Metric::L2) ? l2(a, b) : cosineOneMinus(a, b);
}

std::string VectorIndexManager::makeObjectKey(std::string_view pk) const {
	return KeySchema::makeVectorKey(objectName_, pk);
}

VectorIndexManager::Status VectorIndexManager::init(std::string_view objectName, int dim, Metric metric,
													int M, int efConstruction, int efSearch) {
	if (objectName.empty()) return Status::Error("init: objectName darf nicht leer sein");
	if (dim <= 0) return Status::Error("init: dim muss > 0 sein");
	objectName_ = std::string(objectName);
	dim_ = dim;
	metric_ = metric;
	efSearch_ = efSearch;
    m_ = M;
    efConstruction_ = efConstruction;

#ifdef THEMIS_HNSW_ENABLED
	try {
		hnswlib::SpaceInterface<float>* space = nullptr;
	if (metric == Metric::L2) space = new hnswlib::L2Space(dim);
	else space = new hnswlib::InnerProductSpace(dim); // Cosine via normalisierte Vektoren (InnerProduct)
	auto* appr = new hnswlib::HierarchicalNSW<float>(space, 1000 /*initial*/, M, efConstruction);
		appr->ef_ = efSearch;
		hnswIndex_ = static_cast<void*>(appr);
		useHnsw_ = true;
	} catch (...) {
		useHnsw_ = false;
		THEMIS_WARN("init: HNSW initialisierung fehlgeschlagen, Fallback auf Brute-Force");
	}
#else
	useHnsw_ = false;
#endif
	return Status::OK();
}

	VectorIndexManager::Status VectorIndexManager::setEfSearch(int efSearch) {
		if (efSearch <= 0) return Status::Error("setEfSearch: efSearch muss > 0 sein");
		efSearch_ = efSearch;
	#ifdef THEMIS_HNSW_ENABLED
		if (useHnsw_) {
			try {
				auto* appr = static_cast<hnswlib::HierarchicalNSW<float>*>(hnswIndex_);
				appr->ef_ = efSearch_;
			} catch (...) {
				return Status::Error("setEfSearch: HNSW ef_-Update fehlgeschlagen");
			}
		}
	#endif
		return Status::OK();
	}

VectorIndexManager::Status VectorIndexManager::rebuildFromStorage() {
	if (objectName_.empty() || dim_ <= 0) return Status::Error("rebuildFromStorage: Manager nicht initialisiert");
	cache_.clear();
	pkToId_.clear();
	idToPk_.clear();

	const std::string prefix = objectName_ + ":"; // KeySchema::makeVectorKey(object, pk) = object:pk
	size_t nextId = 0;
	db_.scanPrefix(prefix, [&](std::string_view key, std::string_view value) {
		std::string pk = KeySchema::extractPrimaryKey(key);
		std::vector<uint8_t> bytes(value.begin(), value.end());
		try {
			BaseEntity e = BaseEntity::deserialize(pk, bytes);
			auto vecOpt = e.extractVector("embedding");
			if (!vecOpt || vecOpt->size() != static_cast<size_t>(dim_)) return true;
			std::vector<float> v = *vecOpt;
			if (metric_ == Metric::COSINE) normalizeL2(v);
			cache_[pk] = v;
			if (useHnsw_) {
#ifdef THEMIS_HNSW_ENABLED
				auto* appr = static_cast<hnswlib::HierarchicalNSW<float>*>(hnswIndex_);
				if (pkToId_.count(pk) == 0) {
					pkToId_[pk] = nextId;
					idToPk_.push_back(pk);
					appr->addPoint(v.data(), nextId);
					++nextId;
				}
#endif
			} else {
				// Fallback: nur Cache
			}
		} catch (...) {
			THEMIS_WARN("rebuildFromStorage: Deserialisierung fehlgeschlagen für PK={}", pk);
		}
		return true;
	});
	return Status::OK();
}

VectorIndexManager::Status VectorIndexManager::addEntity(const BaseEntity& e, std::string_view vectorField) {
	if (objectName_.empty()) return Status::Error("addEntity: Manager nicht initialisiert");
	const std::string& pk = e.getPrimaryKey();
	auto v = e.extractVector(vectorField);
	if (!v) return Status::Error("addEntity: Vektor-Feld fehlt oder hat falsches Format");
	if (v->size() != static_cast<size_t>(dim_)) return Status::Error("addEntity: Vektordimension passt nicht");

	// Persistenz in RocksDB
	std::string key = makeObjectKey(pk);
	std::vector<uint8_t> serialized = e.serialize();
	if (!db_.put(key, serialized)) {
		return Status::Error("addEntity: RocksDB put fehlgeschlagen");
	}

	// In-Memory Cache aktualisieren (bei COSINE mit L2-Normalisierung)
	std::vector<float> vv = *v;
	if (metric_ == Metric::COSINE) normalizeL2(vv);
	cache_[pk] = vv;
#ifdef THEMIS_HNSW_ENABLED
	if (useHnsw_) {
		auto* appr = static_cast<hnswlib::HierarchicalNSW<float>*>(hnswIndex_);
		size_t id;
		auto it = pkToId_.find(pk);
		if (it == pkToId_.end()) {
			id = idToPk_.size();
			pkToId_[pk] = id;
			idToPk_.push_back(pk);
		} else {
			id = it->second;
		}
		try { appr->addPoint(cache_[pk].data(), id); } catch (...) { /* evtl. schon vorhanden */ }
	}
#endif
	return Status::OK();
}

VectorIndexManager::Status VectorIndexManager::addEntity(const BaseEntity& e, RocksDBWrapper::WriteBatchWrapper& batch,
                                                          std::string_view vectorField) {
	if (objectName_.empty()) return Status::Error("addEntity: Manager nicht initialisiert");
	const std::string& pk = e.getPrimaryKey();
	auto v = e.extractVector(vectorField);
	if (!v) return Status::Error("addEntity: Vektor-Feld fehlt oder hat falsches Format");
	if (v->size() != static_cast<size_t>(dim_)) return Status::Error("addEntity: Vektordimension passt nicht");

	// Persistenz via WriteBatch (für Transaktionen)
	std::string key = makeObjectKey(pk);
	std::vector<uint8_t> serialized = e.serialize();
	batch.put(key, serialized);

	// In-Memory Cache aktualisieren (optimistisch); bei COSINE normalisieren
	std::vector<float> vv = *v;
	if (metric_ == Metric::COSINE) normalizeL2(vv);
	cache_[pk] = vv;
#ifdef THEMIS_HNSW_ENABLED
	if (useHnsw_) {
		auto* appr = static_cast<hnswlib::HierarchicalNSW<float>*>(hnswIndex_);
		size_t id;
		auto it = pkToId_.find(pk);
		if (it == pkToId_.end()) {
			id = idToPk_.size();
			pkToId_[pk] = id;
			idToPk_.push_back(pk);
		} else {
			id = it->second;
		}
		try { appr->addPoint(cache_[pk].data(), id); } catch (...) { /* evtl. schon vorhanden */ }
	}
#endif
	return Status::OK();
}

VectorIndexManager::Status VectorIndexManager::updateEntity(const BaseEntity& e, std::string_view vectorField) {
	// einfache Strategie: remove + add
	auto r = removeByPk(e.getPrimaryKey());
	if (!r.ok) THEMIS_WARN("updateEntity: remove warn: {}", r.message);
	return addEntity(e, vectorField);
}

VectorIndexManager::Status VectorIndexManager::updateEntity(const BaseEntity& e, RocksDBWrapper::WriteBatchWrapper& batch,
                                                             std::string_view vectorField) {
	// einfache Strategie: remove + add (beide via Batch)
	auto r = removeByPk(e.getPrimaryKey(), batch);
	if (!r.ok) THEMIS_WARN("updateEntity: remove warn: {}", r.message);
	return addEntity(e, batch, vectorField);
}

VectorIndexManager::Status VectorIndexManager::removeByPk(std::string_view pk) {
	// RocksDB löschen
	std::string key = makeObjectKey(pk);
	if (!db_.del(key)) {
		THEMIS_WARN("removeByPk: RocksDB delete fehlgeschlagen für key={}", key);
	}

	// In-Memory Cache löschen
	cache_.erase(std::string(pk));
#ifdef THEMIS_HNSW_ENABLED
	if (useHnsw_) {
		auto* appr = static_cast<hnswlib::HierarchicalNSW<float>*>(hnswIndex_);
		auto it = pkToId_.find(std::string(pk));
		if (it != pkToId_.end()) {
			try { appr->markDelete(it->second); } catch (...) {}
		}
	}
#endif
	return Status::OK();
}

VectorIndexManager::Status VectorIndexManager::removeByPk(std::string_view pk, RocksDBWrapper::WriteBatchWrapper& batch) {
	// RocksDB löschen via WriteBatch
	std::string key = makeObjectKey(pk);
	batch.del(key);

	// In-Memory Cache löschen
	cache_.erase(std::string(pk));
#ifdef THEMIS_HNSW_ENABLED
	if (useHnsw_) {
		auto* appr = static_cast<hnswlib::HierarchicalNSW<float>*>(hnswIndex_);
		auto it = pkToId_.find(std::string(pk));
		if (it != pkToId_.end()) {
			try { appr->markDelete(it->second); } catch (...) {}
		}
	}
#endif
	return Status::OK();
}

std::vector<VectorIndexManager::Result>
VectorIndexManager::bruteForceSearch_(const std::vector<float>& query, size_t k,
									  const std::vector<std::string>* whitelist) const {
	std::vector<Result> results;
	results.reserve(k);

	auto consider = [&](const std::string& pk, const std::vector<float>& vec) {
		float dist = distance(query, vec);
		if (results.size() < k) {
			results.push_back({pk, dist});
			if (results.size() == k) {
				std::sort(results.begin(), results.end(), [](const Result& a, const Result& b){return a.distance < b.distance;});
			}
		} else if (dist < results.back().distance) {
			results.back() = {pk, dist};
			std::sort(results.begin(), results.end(), [](const Result& a, const Result& b){return a.distance < b.distance;});
		}
	};

	if (whitelist && !whitelist->empty()) {
		for (const auto& pk : *whitelist) {
			auto it = cache_.find(pk);
			if (it != cache_.end() && it->second.size() == static_cast<size_t>(dim_)) {
				consider(pk, it->second);
			} else {
				// Lade aus Storage on-demand
				auto blob = db_.get(makeObjectKey(pk));
				if (!blob) continue;
				try {
					BaseEntity e = BaseEntity::deserialize(pk, *blob);
					auto vec = e.extractVector("embedding");
					if (vec && vec->size() == static_cast<size_t>(dim_)) {
						consider(pk, *vec);
					}
				} catch (...) {}
			}
		}
	} else {
		for (const auto& [pk, vec] : cache_) {
			if (vec.size() == static_cast<size_t>(dim_)) consider(pk, vec);
		}
	}
	return results;
}

std::pair<VectorIndexManager::Status, std::vector<VectorIndexManager::Result>>
VectorIndexManager::searchKnn(const std::vector<float>& query, size_t k, const std::vector<std::string>* whitelist) const {
	if (query.size() != static_cast<size_t>(dim_)) {
		return {Status::Error("searchKnn: Query-Dimension passt nicht"), {}};
	}

#ifdef THEMIS_HNSW_ENABLED
	if (useHnsw_ && (!whitelist || whitelist->empty())) {
		try {
			auto* appr = static_cast<hnswlib::HierarchicalNSW<float>*>(hnswIndex_);
			std::vector<float> q = query;
			if (metric_ == Metric::COSINE) normalizeL2(q);
			auto topk = appr->searchKnn(q.data(), static_cast<size_t>(k));
			std::vector<Result> out;
			out.reserve(topk.size());
			while (!topk.empty()) {
				auto p = topk.top();
				topk.pop();
				size_t id = p.second;
				float d = p.first;
				if (id < idToPk_.size()) out.push_back({idToPk_[id], d});
			}
			std::reverse(out.begin(), out.end()); // kleinste Distanz zuerst
			return {Status::OK(), std::move(out)};
		} catch (...) {
			THEMIS_WARN("searchKnn: HNSW-Suche fehlgeschlagen, Fallback auf Brute-Force");
		}
	}
#endif
	// Fallback oder Whitelist-Fall: Brute-Force
	return {Status::OK(), bruteForceSearch_(query, k, whitelist)};
}

	// =============================================================================
	// Persistenz: saveIndex / loadIndex (HNSW + Mapping + Meta)
	// =============================================================================

	VectorIndexManager::Status VectorIndexManager::saveIndex(const std::string& directory) const {
		namespace fs = std::filesystem;
		try {
			fs::create_directories(directory);
			// Speichere Mapping (id -> pk)
			{
				std::ofstream mapFile(fs::path(directory) / "labels.txt", std::ios::binary | std::ios::trunc);
				if (!mapFile) return Status::Error("saveIndex: labels.txt nicht schreibbar");
				for (const auto& pk : idToPk_) {
					mapFile << pk << "\n";
				}
			}
			// Speichere Meta
			{
				std::ofstream metaFile(fs::path(directory) / "meta.txt", std::ios::binary | std::ios::trunc);
				if (!metaFile) return Status::Error("saveIndex: meta.txt nicht schreibbar");
				metaFile << objectName_ << "\n" << dim_ << "\n" << (metric_ == Metric::L2 ? "L2" : "COSINE")
						 << "\n" << efSearch_ << "\n" << m_ << "\n" << efConstruction_ << "\n";
			}
	#ifdef THEMIS_HNSW_ENABLED
			if (useHnsw_) {
				auto* appr = static_cast<hnswlib::HierarchicalNSW<float>*>(hnswIndex_);
				std::string indexPath = (fs::path(directory) / "index.bin").string();
				appr->saveIndex(indexPath);
			}
	#endif
		} catch (const std::exception& ex) {
			return Status::Error(std::string("saveIndex: ") + ex.what());
		} catch (...) {
			return Status::Error("saveIndex: unbekannter Fehler");
		}
		return Status::OK();
	}

	VectorIndexManager::Status VectorIndexManager::loadIndex(const std::string& directory) {
		namespace fs = std::filesystem;
		try {
			// Lade Meta
			std::ifstream metaFile(fs::path(directory) / "meta.txt", std::ios::binary);
			if (!metaFile) return Status::Error("loadIndex: meta.txt nicht lesbar");
			std::string obj; std::string metricStr; int dim; int ef, m, efc;
			std::getline(metaFile, obj);
			metaFile >> dim; metaFile.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
			std::getline(metaFile, metricStr);
			metaFile >> ef; metaFile.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
			metaFile >> m; metaFile.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
			metaFile >> efc; metaFile.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

			if (obj != objectName_) return Status::Error("loadIndex: objectName passt nicht zum Manager");
			if (dim_ != 0 && dim_ != dim) return Status::Error("loadIndex: Dimension passt nicht zum Manager");
			dim_ = dim;
			metric_ = (metricStr == "L2") ? Metric::L2 : Metric::COSINE;
			efSearch_ = ef; m_ = m; efConstruction_ = efc;

	#ifdef THEMIS_HNSW_ENABLED
			// Initialisiere Space und Index und lade
			hnswlib::SpaceInterface<float>* space = nullptr;
			if (metric_ == Metric::L2) space = new hnswlib::L2Space(dim_);
			else space = new hnswlib::InnerProductSpace(dim_);

			auto* appr = new hnswlib::HierarchicalNSW<float>(space, (fs::path(directory) / "index.bin").string(), false);
			appr->ef_ = efSearch_;
			hnswIndex_ = static_cast<void*>(appr);
			useHnsw_ = true;
	#else
			useHnsw_ = false;
	#endif
			// Lade Mapping
			pkToId_.clear(); idToPk_.clear();
			{
				std::ifstream mapFile(fs::path(directory) / "labels.txt", std::ios::binary);
				if (!mapFile) return Status::Error("loadIndex: labels.txt nicht lesbar");
				std::string line; size_t id = 0;
				while (std::getline(mapFile, line)) {
					if (line.empty()) { ++id; continue; }
					pkToId_[line] = id;
					idToPk_.push_back(line);
					++id;
				}
			}

			// Cache ggf. leer lassen; rebuildFromStorage() kann separat genutzt werden
		} catch (const std::exception& ex) {
			return Status::Error(std::string("loadIndex: ") + ex.what());
		} catch (...) {
			return Status::Error("loadIndex: unbekannter Fehler");
		}
		return Status::OK();
	}

// ============================================================================
// MVCC Transaction Variants
// ============================================================================

VectorIndexManager::Status VectorIndexManager::addEntity(const BaseEntity& e, RocksDBWrapper::TransactionWrapper& txn,
                                                          std::string_view vectorField) {
	if (objectName_.empty()) return Status::Error("addEntity(mvcc): Manager nicht initialisiert");
	if (!txn.isActive()) return Status::Error("addEntity(mvcc): Transaction ist nicht aktiv");
	
	const std::string& pk = e.getPrimaryKey();
	auto v = e.extractVector(vectorField);
	if (!v) return Status::Error("addEntity(mvcc): Vektor-Feld fehlt oder hat falsches Format");
	if (v->size() != static_cast<size_t>(dim_)) return Status::Error("addEntity(mvcc): Vektordimension passt nicht");

	// Persistenz via MVCC Transaction
	std::string key = makeObjectKey(pk);
	std::vector<uint8_t> serialized = e.serialize();
	txn.put(key, serialized);

	// In-Memory Cache aktualisieren (bei COSINE mit L2-Normalisierung)
	std::vector<float> vv = *v;
	if (metric_ == Metric::COSINE) normalizeL2(vv);
	cache_[pk] = vv;
#ifdef THEMIS_HNSW_ENABLED
	if (useHnsw_) {
		auto* appr = static_cast<hnswlib::HierarchicalNSW<float>*>(hnswIndex_);
		size_t id;
		auto it = pkToId_.find(pk);
		if (it == pkToId_.end()) {
			id = idToPk_.size();
			pkToId_[pk] = id;
			idToPk_.push_back(pk);
		} else {
			id = it->second;
		}
		try { appr->addPoint(cache_[pk].data(), id); } catch (...) { /* evtl. schon vorhanden */ }
	}
#endif
	return Status::OK();
}

VectorIndexManager::Status VectorIndexManager::updateEntity(const BaseEntity& e, RocksDBWrapper::TransactionWrapper& txn,
                                                             std::string_view vectorField) {
	// einfache Strategie: remove + add (beide via Transaction)
	auto r = removeByPk(e.getPrimaryKey(), txn);
	if (!r.ok) THEMIS_WARN("updateEntity(mvcc): remove warn: {}", r.message);
	return addEntity(e, txn, vectorField);
}

VectorIndexManager::Status VectorIndexManager::removeByPk(std::string_view pk, RocksDBWrapper::TransactionWrapper& txn) {
	if (!txn.isActive()) return Status::Error("removeByPk(mvcc): Transaction ist nicht aktiv");
	
	// RocksDB löschen via Transaction
	std::string key = makeObjectKey(pk);
	txn.del(key);

	// In-Memory Cache löschen
	cache_.erase(std::string(pk));
#ifdef THEMIS_HNSW_ENABLED
	if (useHnsw_) {
		auto* appr = static_cast<hnswlib::HierarchicalNSW<float>*>(hnswIndex_);
		auto it = pkToId_.find(std::string(pk));
		if (it != pkToId_.end()) {
			try { appr->markDelete(it->second); } catch (...) {}
		}
	}
#endif
	return Status::OK();
}

} // namespace themis