# graph_index.cpp

Path: `src/index/graph_index.cpp`

Purpose: Graph index implementation for relationship queries and traversal support.

Public functions / symbols:
- `if (topologyLoaded_) {`
- `for (const auto& adj : it->second) {`
- `if (lastColon != std::string::npos) {`
- ``
- `addEdgeToTopology_(eid, from, to);`
- `std::lock_guard<std::mutex> lock(topology_mutex_);`
- `std::string keyStr(key);`
- `std::string neigh(val);`
- `std::string toPk(val);`
- `std::string fromPk(val);`
- `std::string target(targetPk);`
- `std::string neighbor(val);`

