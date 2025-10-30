// Graph adjacency index implementation

#include "index/graph_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/key_schema.h"
#include "storage/base_entity.h"
#include "utils/logger.h"

#include <queue>
#include <unordered_set>
#include <algorithm>
#include <chrono>

namespace themis {

// static
std::vector<uint8_t> GraphIndexManager::toBytes(std::string_view sv) {
	return std::vector<uint8_t>(sv.begin(), sv.end());
}

GraphIndexManager::GraphIndexManager(RocksDBWrapper& db) : db_(db) {}

GraphIndexManager::Status GraphIndexManager::addEdge(const BaseEntity& edge) {
	if (!db_.isOpen()) return Status::Error("addEdge: Datenbank ist nicht geöffnet");

	auto eidOpt = edge.getFieldAsString("id");
	auto fromOpt = edge.getFieldAsString("_from");
	auto toOpt = edge.getFieldAsString("_to");
	if (!eidOpt || !fromOpt || !toOpt) {
		return Status::Error("addEdge: Felder 'id', '_from' und '_to' sind erforderlich");
	}

	// Variablen verwendet in addEdge-Überladung
	[[maybe_unused]] const std::string& eid = *eidOpt;
	[[maybe_unused]] const std::string& from = *fromOpt;
	[[maybe_unused]] const std::string& to = *toOpt;

	auto batch = db_.createWriteBatch();
	if (!batch) return Status::Error("addEdge: Konnte WriteBatch nicht erstellen");
	auto st = addEdge(edge, *batch);
	if (!st.ok) { batch->rollback(); return st; }
	if (!batch->commit()) return Status::Error("addEdge: Commit des Batches fehlgeschlagen");
	return Status::OK();
}

GraphIndexManager::Status GraphIndexManager::deleteEdge(std::string_view edgeId) {
	if (!db_.isOpen()) return Status::Error("deleteEdge: Datenbank ist nicht geöffnet");
	if (edgeId.empty()) return Status::Error("deleteEdge: edgeId darf nicht leer sein");

	// Edge laden, um _from/_to zu ermitteln
	const std::string edgeKey = KeySchema::makeGraphEdgeKey(edgeId);
	auto blob = db_.get(edgeKey);
	if (!blob) {
		// Idempotent: nichts zu löschen
		return Status::OK();
	}

	BaseEntity e = BaseEntity::deserialize(std::string(edgeId), *blob);
	auto fromOpt = e.getFieldAsString("_from");
	auto toOpt = e.getFieldAsString("_to");
	if (!fromOpt || !toOpt) {
		// Inkonsistente Kante, löschen wir zumindest den Edge-Key
		db_.del(edgeKey);
		return Status::Error("deleteEdge: Edge hat keine _from/_to Felder (inkonsistent)");
	}

	auto batch = db_.createWriteBatch();
	if (!batch) return Status::Error("deleteEdge: Konnte WriteBatch nicht erstellen");
	auto st = deleteEdge(edgeId, *batch);
	if (!st.ok) { batch->rollback(); return st; }
	if (!batch->commit()) return Status::Error("deleteEdge: Commit des Batches fehlgeschlagen");
	return Status::OK();
}

GraphIndexManager::Status GraphIndexManager::addEdge(const BaseEntity& edge, RocksDBWrapper::WriteBatchWrapper& batch) {
	auto eidOpt = edge.getFieldAsString("id");
	auto fromOpt = edge.getFieldAsString("_from");
	auto toOpt = edge.getFieldAsString("_to");
	if (!eidOpt || !fromOpt || !toOpt) return Status::Error("addEdge(tx): Felder 'id', '_from', '_to' fehlen");
	const std::string& eid = *eidOpt; const std::string& from = *fromOpt; const std::string& to = *toOpt;
	// Edge speichern (Primärspeicher)
	batch.put(KeySchema::makeGraphEdgeKey(eid), edge.serialize());
	// Adjazenz-Indizes
	batch.put(KeySchema::makeGraphOutdexKey(from, eid), toBytes(to));
	batch.put(KeySchema::makeGraphIndexKey(to, eid), toBytes(from));

	// Update in-memory topology if loaded
	if (topologyLoaded_) {
		addEdgeToTopology_(eid, from, to);
	}

	return Status::OK();
}

GraphIndexManager::Status GraphIndexManager::deleteEdge(std::string_view edgeId, RocksDBWrapper::WriteBatchWrapper& batch) {
	if (edgeId.empty()) return Status::Error("deleteEdge(tx): edgeId leer");
	const std::string edgeKey = KeySchema::makeGraphEdgeKey(edgeId);
	auto blob = db_.get(edgeKey);
	if (!blob) return Status::OK();
	BaseEntity e = BaseEntity::deserialize(std::string(edgeId), *blob);
	auto fromOpt = e.getFieldAsString("_from");
	auto toOpt = e.getFieldAsString("_to");
	batch.del(edgeKey);
	if (fromOpt && toOpt) {
		batch.del(KeySchema::makeGraphOutdexKey(*fromOpt, std::string(edgeId)));
		batch.del(KeySchema::makeGraphIndexKey(*toOpt, std::string(edgeId)));

		// Update in-memory topology if loaded
		if (topologyLoaded_) {
			removeEdgeFromTopology_(std::string(edgeId), *fromOpt, *toOpt);
		}

		return Status::OK();
	}
	return Status::Error("deleteEdge(tx): _from/_to fehlen (inkonsistent)");
}

std::pair<GraphIndexManager::Status, std::vector<std::string>>
GraphIndexManager::outNeighbors(std::string_view fromPk) const {
	if (!db_.isOpen()) return {Status::Error("outNeighbors: Datenbank ist nicht geöffnet"), {}};

	// Use in-memory topology if available (O(1) lookup)
	if (topologyLoaded_) {
		std::lock_guard<std::mutex> lock(topology_mutex_);
		std::vector<std::string> result;
		auto it = outEdges_.find(std::string(fromPk));
		if (it != outEdges_.end()) {
			result.reserve(it->second.size());
			for (const auto& adj : it->second) {
				result.push_back(adj.targetPk);
			}
		}
		return {Status::OK(), std::move(result)};
	}

	// Fallback to RocksDB scan (O(log N))
	std::vector<std::string> result;
	const std::string prefix = std::string("graph:out:") + std::string(fromPk) + ":";
	db_.scanPrefix(prefix, [&result](std::string_view /*key*/, std::string_view val){
		result.emplace_back(std::string(val));
		return true;
	});
	return {Status::OK(), std::move(result)};
}

std::pair<GraphIndexManager::Status, std::vector<std::string>>
GraphIndexManager::inNeighbors(std::string_view toPk) const {
	if (!db_.isOpen()) return {Status::Error("inNeighbors: Datenbank ist nicht geöffnet"), {}};

	// Use in-memory topology if available (O(1) lookup)
	if (topologyLoaded_) {
		std::lock_guard<std::mutex> lock(topology_mutex_);
		std::vector<std::string> result;
		auto it = inEdges_.find(std::string(toPk));
		if (it != inEdges_.end()) {
			result.reserve(it->second.size());
			for (const auto& adj : it->second) {
				result.push_back(adj.targetPk);
			}
		}
		return {Status::OK(), std::move(result)};
	}

	// Fallback to RocksDB scan (O(log N))
	std::vector<std::string> result;
	const std::string prefix = std::string("graph:in:") + std::string(toPk) + ":";
	db_.scanPrefix(prefix, [&result](std::string_view /*key*/, std::string_view val){
		result.emplace_back(std::string(val));
		return true;
	});
	return {Status::OK(), std::move(result)};
}

std::pair<GraphIndexManager::Status, std::vector<GraphIndexManager::AdjacencyInfo>>
GraphIndexManager::outAdjacency(std::string_view fromPk) const {
	if (!db_.isOpen()) return {Status::Error("outAdjacency: Datenbank ist nicht geöffnet"), {}};

	// In-Memory schnellpfad
	if (topologyLoaded_) {
		std::lock_guard<std::mutex> lock(topology_mutex_);
		std::vector<AdjacencyInfo> result;
		auto it = outEdges_.find(std::string(fromPk));
		if (it != outEdges_.end()) {
			result = it->second; // Kopie bewusst
		}
		return {Status::OK(), std::move(result)};
	}

	// Fallback: RocksDB-Scan – EdgeId aus Key extrahieren
	std::vector<AdjacencyInfo> result;
	const std::string prefix = std::string("graph:out:") + std::string(fromPk) + ":";
	db_.scanPrefix(prefix, [&result](std::string_view key, std::string_view val){
		std::string keyStr(key);
		size_t lastColon = keyStr.rfind(':');
		if (lastColon != std::string::npos) {
			std::string edgeId = keyStr.substr(lastColon + 1);
			result.push_back({edgeId, std::string(val)});
		}
		return true;
	});
	return {Status::OK(), std::move(result)};
}

std::pair<GraphIndexManager::Status, std::vector<GraphIndexManager::AdjacencyInfo>>
GraphIndexManager::inAdjacency(std::string_view toPk) const {
	if (!db_.isOpen()) return {Status::Error("inAdjacency: Datenbank ist nicht geöffnet"), {}};

	if (topologyLoaded_) {
		std::lock_guard<std::mutex> lock(topology_mutex_);
		std::vector<AdjacencyInfo> result;
		auto it = inEdges_.find(std::string(toPk));
		if (it != inEdges_.end()) {
			result = it->second; // Kopie bewusst (edgeId, fromPk)
		}
		return {Status::OK(), std::move(result)};
	}

	// Fallback: RocksDB-Scan – EdgeId aus Key extrahieren
	std::vector<AdjacencyInfo> result;
	const std::string prefix = std::string("graph:in:") + std::string(toPk) + ":";
	db_.scanPrefix(prefix, [&result](std::string_view key, std::string_view val){
		std::string keyStr(key);
		size_t lastColon = keyStr.rfind(':');
		if (lastColon != std::string::npos) {
			std::string edgeId = keyStr.substr(lastColon + 1);
			// hier ist val = fromPk
			result.push_back({edgeId, std::string(val)});
		}
		return true;
	});
	return {Status::OK(), std::move(result)};
}

std::pair<GraphIndexManager::Status, std::vector<std::string>>
GraphIndexManager::bfs(std::string_view startPk, int maxDepth) const {
	if (!db_.isOpen()) return {Status::Error("bfs: Datenbank ist nicht geöffnet"), {}};
	if (startPk.empty()) return {Status::Error("bfs: startPk darf nicht leer sein"), {}};
	if (maxDepth < 0) return {Status::Error("bfs: maxDepth muss >= 0 sein"), {}};

	std::vector<std::string> order;
	std::unordered_set<std::string> visited;
	std::queue<std::pair<std::string,int>> q;

	q.emplace(std::string(startPk), 0);
	visited.insert(std::string(startPk));

	// Use in-memory topology for faster BFS if available
	if (topologyLoaded_) {
		while (!q.empty()) {
			auto [node, depth] = q.front();
			q.pop();

			order.push_back(node);
			if (depth == maxDepth) continue;

			std::lock_guard<std::mutex> lock(topology_mutex_);
			auto it = outEdges_.find(node);
			if (it != outEdges_.end()) {
				for (const auto& adj : it->second) {
					if (!visited.count(adj.targetPk)) {
						visited.insert(adj.targetPk);
						q.emplace(adj.targetPk, depth + 1);
					}
				}
			}
		}
		return {Status::OK(), std::move(order)};
	}

	// Fallback to RocksDB scan-based BFS
	while (!q.empty()) {
		auto [node, depth] = q.front();
		q.pop();

		order.push_back(node);
		if (depth == maxDepth) continue;

		const std::string prefix = std::string("graph:out:") + node + ":";
		db_.scanPrefix(prefix, [&](std::string_view /*key*/, std::string_view val){
			std::string neigh(val);
			if (!visited.count(neigh)) {
				visited.insert(neigh);
				q.emplace(neigh, depth + 1);
			}
			return true;
		});
	}
	return {Status::OK(), std::move(order)};
}

// ────────────────────────────────────────────────────────────────────────────
// In-Memory Topology Management
// ────────────────────────────────────────────────────────────────────────────

GraphIndexManager::Status GraphIndexManager::rebuildTopology() {
	if (!db_.isOpen()) return Status::Error("rebuildTopology: Datenbank ist nicht geöffnet");

	std::lock_guard<std::mutex> lock(topology_mutex_);

	// Clear existing topology
	outEdges_.clear();
	inEdges_.clear();

	// Scan all outgoing edges: graph:out:fromPk:edgeId -> toPk
	db_.scanPrefix("graph:out:", [this](std::string_view key, std::string_view val) {
		// Parse key: graph:out:<fromPk>:<edgeId>
		std::string keyStr(key);
		// Remove "graph:out:" prefix
		size_t pos = keyStr.find("graph:out:");
		if (pos == std::string::npos) return true;
		keyStr = keyStr.substr(10); // skip "graph:out:"
		
		// Split at last ':' to separate fromPk and edgeId
		size_t lastColon = keyStr.rfind(':');
		if (lastColon == std::string::npos) return true;

		std::string fromPk = keyStr.substr(0, lastColon);
		std::string edgeId = keyStr.substr(lastColon + 1);
		std::string toPk(val);

		// Add to outEdges_
		outEdges_[fromPk].push_back({edgeId, toPk});
		return true;
	});

	// Scan all incoming edges: graph:in:toPk:edgeId -> fromPk
	db_.scanPrefix("graph:in:", [this](std::string_view key, std::string_view val) {
		// Parse key: graph:in:<toPk>:<edgeId>
		std::string keyStr(key);
		size_t pos = keyStr.find("graph:in:");
		if (pos == std::string::npos) return true;
		keyStr = keyStr.substr(9); // skip "graph:in:"
		
		size_t lastColon = keyStr.rfind(':');
		if (lastColon == std::string::npos) return true;

		std::string toPk = keyStr.substr(0, lastColon);
		std::string edgeId = keyStr.substr(lastColon + 1);
		std::string fromPk(val);

		// Add to inEdges_
		inEdges_[toPk].push_back({edgeId, fromPk});
		return true;
	});

	topologyLoaded_ = true;
	return Status::OK();
}

void GraphIndexManager::addEdgeToTopology_(const std::string& edgeId, const std::string& fromPk, const std::string& toPk) {
	std::lock_guard<std::mutex> lock(topology_mutex_);
	outEdges_[fromPk].push_back({edgeId, toPk});
	inEdges_[toPk].push_back({edgeId, fromPk});
}

void GraphIndexManager::removeEdgeFromTopology_(const std::string& edgeId, const std::string& fromPk, const std::string& toPk) {
	std::lock_guard<std::mutex> lock(topology_mutex_);

	// Remove from outEdges_
	auto outIt = outEdges_.find(fromPk);
	if (outIt != outEdges_.end()) {
		auto& vec = outIt->second;
		vec.erase(std::remove_if(vec.begin(), vec.end(),
			[&edgeId](const AdjacencyInfo& info) { return info.edgeId == edgeId; }),
			vec.end());
		if (vec.empty()) outEdges_.erase(outIt);
	}

	// Remove from inEdges_
	auto inIt = inEdges_.find(toPk);
	if (inIt != inEdges_.end()) {
		auto& vec = inIt->second;
		vec.erase(std::remove_if(vec.begin(), vec.end(),
			[&edgeId](const AdjacencyInfo& info) { return info.edgeId == edgeId; }),
			vec.end());
		if (vec.empty()) inEdges_.erase(inIt);
	}
}

size_t GraphIndexManager::getTopologyNodeCount() const {
	std::lock_guard<std::mutex> lock(topology_mutex_);
	std::unordered_set<std::string> nodes;
	for (const auto& [node, _] : outEdges_) nodes.insert(node);
	for (const auto& [node, _] : inEdges_) nodes.insert(node);
	return nodes.size();
}

size_t GraphIndexManager::getTopologyEdgeCount() const {
	std::lock_guard<std::mutex> lock(topology_mutex_);
	size_t total = 0;
	for (const auto& [_, edges] : outEdges_) {
		total += edges.size();
	}
	return total;
}

// ────────────────────────────────────────────────────────────────────────────
// Shortest-Path-Algorithmen
// ────────────────────────────────────────────────────────────────────────────

double GraphIndexManager::getEdgeWeight_(std::string_view edgeId) const {
	const std::string edgeKey = KeySchema::makeGraphEdgeKey(edgeId);
	auto blob = db_.get(edgeKey);
	if (!blob) return 1.0; // Default weight
	
	BaseEntity edge = BaseEntity::deserialize(std::string(edgeId), *blob);
	auto weightOpt = edge.getFieldAsDouble("_weight");
	return weightOpt.value_or(1.0);
}

std::pair<GraphIndexManager::Status, GraphIndexManager::PathResult>
GraphIndexManager::dijkstra(std::string_view startPk, std::string_view targetPk) const {
	if (!db_.isOpen()) return {Status::Error("dijkstra: Datenbank ist nicht geöffnet"), {}};
	if (startPk.empty() || targetPk.empty()) {
		return {Status::Error("dijkstra: Start und Ziel dürfen nicht leer sein"), {}};
	}

	std::string start(startPk);
	std::string target(targetPk);

	// Priority Queue: (cost, node)
	using QueueItem = std::pair<double, std::string>;
	auto cmp = [](const QueueItem& a, const QueueItem& b) { return a.first > b.first; };
	std::priority_queue<QueueItem, std::vector<QueueItem>, decltype(cmp)> pq(cmp);

	std::unordered_map<std::string, double> dist;
	std::unordered_map<std::string, std::string> prev;
	std::unordered_set<std::string> visited;

	dist[start] = 0.0;
	pq.emplace(0.0, start);

	while (!pq.empty()) {
		auto [cost, node] = pq.top();
		pq.pop();

		if (visited.count(node)) continue;
		visited.insert(node);

		// Ziel erreicht?
		if (node == target) break;

		// Nachbarn holen (In-Memory falls verfügbar)
		std::vector<std::string> neighbors;
		if (topologyLoaded_) {
			std::lock_guard<std::mutex> lock(topology_mutex_);
			auto it = outEdges_.find(node);
			if (it != outEdges_.end()) {
				for (const auto& adj : it->second) {
					neighbors.push_back(adj.targetPk);
					// Edge-Weight ermitteln
					double weight = getEdgeWeight_(adj.edgeId);
					double newCost = dist[node] + weight;
					
					if (!dist.count(adj.targetPk) || newCost < dist[adj.targetPk]) {
						dist[adj.targetPk] = newCost;
						prev[adj.targetPk] = node;
						pq.emplace(newCost, adj.targetPk);
					}
				}
			}
		} else {
			// Fallback: RocksDB scan
			const std::string prefix = std::string("graph:out:") + node + ":";
			db_.scanPrefix(prefix, [&](std::string_view key, std::string_view val) {
				std::string keyStr(key);
				size_t lastColon = keyStr.rfind(':');
				if (lastColon == std::string::npos) return true;
				
				std::string edgeId = keyStr.substr(lastColon + 1);
				std::string neighbor(val);
				
				double weight = getEdgeWeight_(edgeId);
				double newCost = dist[node] + weight;
				
				if (!dist.count(neighbor) || newCost < dist[neighbor]) {
					dist[neighbor] = newCost;
					prev[neighbor] = node;
					pq.emplace(newCost, neighbor);
				}
				return true;
			});
		}
	}

	// Pfad rekonstruieren
	if (!prev.count(target) && target != start) {
		return {Status::Error("dijkstra: Kein Pfad gefunden"), {}};
	}

	PathResult result;
	result.totalCost = dist[target];
	
	std::vector<std::string> path;
	std::string current = target;
	while (current != start) {
		path.push_back(current);
		auto it = prev.find(current);
		if (it == prev.end()) break;
		current = it->second;
	}
	path.push_back(start);
	std::reverse(path.begin(), path.end());
	result.path = std::move(path);

	return {Status::OK(), std::move(result)};
}

std::pair<GraphIndexManager::Status, GraphIndexManager::PathResult>
GraphIndexManager::aStar(std::string_view startPk, std::string_view targetPk, HeuristicFunc heuristic) const {
	if (!db_.isOpen()) return {Status::Error("aStar: Datenbank ist nicht geöffnet"), {}};
	if (startPk.empty() || targetPk.empty()) {
		return {Status::Error("aStar: Start und Ziel dürfen nicht leer sein"), {}};
	}

	std::string start(startPk);
	std::string target(targetPk);

	// Wenn keine Heuristik angegeben, verwende konstante 0 (= Dijkstra)
	auto h = heuristic ? heuristic : [](const std::string&) { return 0.0; };

	// Priority Queue: (f_score, node) wobei f = g + h
	using QueueItem = std::pair<double, std::string>;
	auto cmp = [](const QueueItem& a, const QueueItem& b) { return a.first > b.first; };
	std::priority_queue<QueueItem, std::vector<QueueItem>, decltype(cmp)> pq(cmp);

	std::unordered_map<std::string, double> g_score; // Tatsächliche Kosten vom Start
	std::unordered_map<std::string, double> f_score; // g + h (geschätzte Gesamtkosten)
	std::unordered_map<std::string, std::string> prev;
	std::unordered_set<std::string> visited;

	g_score[start] = 0.0;
	f_score[start] = h(start);
	pq.emplace(f_score[start], start);

	while (!pq.empty()) {
		auto [_, node] = pq.top();
		pq.pop();

		if (visited.count(node)) continue;
		visited.insert(node);

		// Ziel erreicht?
		if (node == target) break;

		// Nachbarn holen
		if (topologyLoaded_) {
			std::lock_guard<std::mutex> lock(topology_mutex_);
			auto it = outEdges_.find(node);
			if (it != outEdges_.end()) {
				for (const auto& adj : it->second) {
					if (visited.count(adj.targetPk)) continue;

					double weight = getEdgeWeight_(adj.edgeId);
					double tentative_g = g_score[node] + weight;

					if (!g_score.count(adj.targetPk) || tentative_g < g_score[adj.targetPk]) {
						prev[adj.targetPk] = node;
						g_score[adj.targetPk] = tentative_g;
						f_score[adj.targetPk] = tentative_g + h(adj.targetPk);
						pq.emplace(f_score[adj.targetPk], adj.targetPk);
					}
				}
			}
		} else {
			// Fallback: RocksDB scan
			const std::string prefix = std::string("graph:out:") + node + ":";
			db_.scanPrefix(prefix, [&](std::string_view key, std::string_view val) {
				std::string keyStr(key);
				size_t lastColon = keyStr.rfind(':');
				if (lastColon == std::string::npos) return true;
				
				std::string edgeId = keyStr.substr(lastColon + 1);
				std::string neighbor(val);

				if (visited.count(neighbor)) return true;

				double weight = getEdgeWeight_(edgeId);
				double tentative_g = g_score[node] + weight;

				if (!g_score.count(neighbor) || tentative_g < g_score[neighbor]) {
					prev[neighbor] = node;
					g_score[neighbor] = tentative_g;
					f_score[neighbor] = tentative_g + h(neighbor);
					pq.emplace(f_score[neighbor], neighbor);
				}
				return true;
			});
		}
	}

	// Pfad rekonstruieren
	if (!prev.count(target) && target != start) {
		return {Status::Error("aStar: Kein Pfad gefunden"), {}};
	}

	PathResult result;
	result.totalCost = g_score[target];
	
	std::vector<std::string> path;
	std::string current = target;
	while (current != start) {
		path.push_back(current);
		auto it = prev.find(current);
		if (it == prev.end()) break;
		current = it->second;
	}
	path.push_back(start);
	std::reverse(path.begin(), path.end());
	result.path = std::move(path);

	return {Status::OK(), std::move(result)};
}

// ============================================================================
// MVCC Transaction Variants
// ============================================================================

GraphIndexManager::Status GraphIndexManager::addEdge(const BaseEntity& edge, RocksDBWrapper::TransactionWrapper& txn) {
	if (!txn.isActive()) return Status::Error("addEdge(mvcc): Transaction ist nicht aktiv");
	
	auto eidOpt = edge.getFieldAsString("id");
	auto fromOpt = edge.getFieldAsString("_from");
	auto toOpt = edge.getFieldAsString("_to");
	if (!eidOpt || !fromOpt || !toOpt) return Status::Error("addEdge(mvcc): Felder 'id', '_from', '_to' fehlen");
	
	const std::string& eid = *eidOpt; 
	const std::string& from = *fromOpt; 
	const std::string& to = *toOpt;
	
	// Edge speichern (Primärspeicher)
	txn.put(KeySchema::makeGraphEdgeKey(eid), edge.serialize());
	
	// Adjazenz-Indizes
	txn.put(KeySchema::makeGraphOutdexKey(from, eid), toBytes(to));
	txn.put(KeySchema::makeGraphIndexKey(to, eid), toBytes(from));

	// Update in-memory topology if loaded
	if (topologyLoaded_) {
		addEdgeToTopology_(eid, from, to);
	}

	return Status::OK();
}

GraphIndexManager::Status GraphIndexManager::deleteEdge(std::string_view edgeId, RocksDBWrapper::TransactionWrapper& txn) {
	if (!txn.isActive()) return Status::Error("deleteEdge(mvcc): Transaction ist nicht aktiv");
	if (edgeId.empty()) return Status::Error("deleteEdge(mvcc): edgeId leer");
	
	const std::string edgeKey = KeySchema::makeGraphEdgeKey(edgeId);
	
	// Read edge from MVCC snapshot
	auto blob = txn.get(edgeKey);
	if (!blob) return Status::OK();
	
	BaseEntity e = BaseEntity::deserialize(std::string(edgeId), *blob);
	auto fromOpt = e.getFieldAsString("_from");
	auto toOpt = e.getFieldAsString("_to");
	
	txn.del(edgeKey);
	
	if (fromOpt && toOpt) {
		txn.del(KeySchema::makeGraphOutdexKey(*fromOpt, std::string(edgeId)));
		txn.del(KeySchema::makeGraphIndexKey(*toOpt, std::string(edgeId)));

		// Update in-memory topology if loaded
		if (topologyLoaded_) {
			removeEdgeFromTopology_(std::string(edgeId), *fromOpt, *toOpt);
		}

		return Status::OK();
	}
	
	return Status::Error("deleteEdge(mvcc): _from/_to fehlen (inkonsistent)");
}

// ===== Sprint B: Temporal Graph Traversal =====

std::pair<GraphIndexManager::Status, std::vector<std::string>>
GraphIndexManager::bfsAtTime(std::string_view startPk, int64_t timestamp_ms, int maxDepth) const {
	if (!db_.isOpen()) return {Status::Error("bfsAtTime: Datenbank ist nicht geöffnet"), {}};
	if (startPk.empty()) return {Status::Error("bfsAtTime: startPk darf nicht leer sein"), {}};
	if (maxDepth < 0) return {Status::Error("bfsAtTime: maxDepth muss >= 0 sein"), {}};

	TemporalFilter filter = TemporalFilter::at(timestamp_ms);
	
	std::vector<std::string> order;
	std::unordered_set<std::string> visited;
	std::queue<std::pair<std::string,int>> q;

	q.emplace(std::string(startPk), 0);
	visited.insert(std::string(startPk));

	while (!q.empty()) {
		auto [node, depth] = q.front();
		q.pop();
		order.push_back(node);

		if (depth >= maxDepth) continue;

		// Get outgoing edges with adjacency info
		auto [st, adj] = outAdjacency(node);
		if (!st.ok) continue;

		for (const auto& info : adj) {
			// Load edge to check temporal validity
			std::string edgeKey = KeySchema::makeGraphEdgeKey(info.edgeId);
			auto blob = db_.get(edgeKey);
			if (!blob) continue;

			BaseEntity edge = BaseEntity::deserialize(info.edgeId, *blob);
			
			// Check temporal validity
			std::optional<int64_t> valid_from = edge.getFieldAsInt("valid_from");
			std::optional<int64_t> valid_to = edge.getFieldAsInt("valid_to");
			
			if (!filter.isValid(valid_from, valid_to)) {
				continue; // Skip edge - not valid at query time
			}

			// Include neighbor if valid
			if (visited.find(info.targetPk) == visited.end()) {
				visited.insert(info.targetPk);
				q.emplace(info.targetPk, depth + 1);
			}
		}
	}

	return {Status::OK(), std::move(order)};
}

std::pair<GraphIndexManager::Status, GraphIndexManager::PathResult>
GraphIndexManager::dijkstraAtTime(std::string_view startPk, std::string_view targetPk, int64_t timestamp_ms) const {
	if (!db_.isOpen()) return {Status::Error("dijkstraAtTime: Datenbank ist nicht geöffnet"), {}};
	if (startPk.empty() || targetPk.empty()) {
		return {Status::Error("dijkstraAtTime: start/target dürfen nicht leer sein"), {}};
	}

	TemporalFilter filter = TemporalFilter::at(timestamp_ms);
	
	std::unordered_map<std::string, double> dist;
	std::unordered_map<std::string, std::string> prev;
	
	using PQElem = std::pair<double, std::string>;
	std::priority_queue<PQElem, std::vector<PQElem>, std::greater<>> pq;

	std::string start(startPk);
	std::string target(targetPk);

	dist[start] = 0.0;
	pq.emplace(0.0, start);

	while (!pq.empty()) {
		auto [d, u] = pq.top();
		pq.pop();

		if (u == target) break;
		if (dist.count(u) && d > dist[u]) continue;

		auto [st, adj] = outAdjacency(u);
		if (!st.ok) continue;

		for (const auto& info : adj) {
			// Load edge to check temporal validity and weight
			std::string edgeKey = KeySchema::makeGraphEdgeKey(info.edgeId);
			auto blob = db_.get(edgeKey);
			if (!blob) continue;

			BaseEntity edge = BaseEntity::deserialize(info.edgeId, *blob);
			
			// Check temporal validity
			std::optional<int64_t> valid_from = edge.getFieldAsInt("valid_from");
			std::optional<int64_t> valid_to = edge.getFieldAsInt("valid_to");
			
			if (!filter.isValid(valid_from, valid_to)) {
				continue; // Skip edge - not valid at query time
			}

			// Get edge weight
			double weight = 1.0;
			if (auto w = edge.getFieldAsDouble("_weight")) {
				weight = *w;
			}

			const std::string& v = info.targetPk;
			double alt = d + weight;

			if (!dist.count(v) || alt < dist[v]) {
				dist[v] = alt;
				prev[v] = u;
				pq.emplace(alt, v);
			}
		}
	}

	// Reconstruct path
	PathResult result;
	if (!dist.count(target)) {
		return {Status::Error("dijkstraAtTime: Kein Pfad gefunden"), result};
	}

	result.totalCost = dist[target];
	std::string curr = target;
	while (curr != start) {
		result.path.push_back(curr);
		if (!prev.count(curr)) break;
		curr = prev[curr];
	}
	result.path.push_back(start);
	std::reverse(result.path.begin(), result.path.end());

	return {Status::OK(), result};
}

} // namespace themis