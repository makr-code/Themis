#include "index/graph_analytics.h"
#include "utils/logger.h"
#include <cmath>
#include <algorithm>
#include <queue>
#include <set>

namespace themis {

GraphAnalytics::GraphAnalytics(GraphIndexManager& graphMgr)
    : graphMgr_(graphMgr) {}

// Helper: Build adjacency structure from GraphIndexManager
std::pair<GraphAnalytics::Status, GraphAnalytics::GraphTopology>
GraphAnalytics::buildTopology(const std::vector<std::string>& node_pks) const {
    GraphTopology topo;
    
    for (const auto& pk : node_pks) {
        // Get outgoing neighbors
        auto [st_out, out_neighbors] = graphMgr_.outNeighbors(pk);
        if (!st_out.ok) {
            return {Status::Error("Failed to get out-neighbors for " + std::string(pk) + ": " + st_out.message), {}};
        }
        topo.outgoing[pk] = std::move(out_neighbors);
        
        // Get incoming neighbors
        auto [st_in, in_neighbors] = graphMgr_.inNeighbors(pk);
        if (!st_in.ok) {
            return {Status::Error("Failed to get in-neighbors for " + std::string(pk) + ": " + st_in.message), {}};
        }
        topo.incoming[pk] = std::move(in_neighbors);
    }
    
    return {Status::OK(), std::move(topo)};
}

// Degree Centrality: Count in/out edges for each node
std::pair<GraphAnalytics::Status, std::map<std::string, GraphAnalytics::DegreeResult>>
GraphAnalytics::degreeCentrality(const std::vector<std::string>& node_pks) const {
    
    if (node_pks.empty()) {
        return {Status::Error("Empty node list provided"), {}};
    }
    
    auto [st, topo] = buildTopology(node_pks);
    if (!st.ok) {
        return {st, {}};
    }
    
    std::map<std::string, DegreeResult> results;
    
    for (const auto& pk : node_pks) {
        DegreeResult dr;
        
        auto out_it = topo.outgoing.find(pk);
        if (out_it != topo.outgoing.end()) {
            dr.out_degree = static_cast<int>(out_it->second.size());
        }
        
        auto in_it = topo.incoming.find(pk);
        if (in_it != topo.incoming.end()) {
            dr.in_degree = static_cast<int>(in_it->second.size());
        }
        
        dr.total_degree = dr.in_degree + dr.out_degree;
        results[pk] = dr;
    }
    
    return {Status::OK(), std::move(results)};
}

// PageRank: Iterative power method
std::pair<GraphAnalytics::Status, std::map<std::string, double>>
GraphAnalytics::pageRank(
    const std::vector<std::string>& node_pks,
    double damping,
    int max_iterations,
    double tolerance
) const {
    
    if (node_pks.empty()) {
        return {Status::Error("Empty node list provided"), {}};
    }
    
    if (damping < 0.0 || damping > 1.0) {
        return {Status::Error("Damping factor must be in [0, 1]"), {}};
    }
    
    if (max_iterations <= 0) {
        return {Status::Error("Max iterations must be positive"), {}};
    }
    
    // Build topology
    auto [st, topo] = buildTopology(node_pks);
    if (!st.ok) {
        return {st, {}};
    }
    
    const size_t n = node_pks.size();
    const double initial_rank = 1.0 / n;
    const double random_jump = (1.0 - damping) / n;
    
    // Initialize ranks
    std::map<std::string, double> ranks;
    std::map<std::string, double> new_ranks;
    
    for (const auto& pk : node_pks) {
        ranks[pk] = initial_rank;
        new_ranks[pk] = 0.0;
    }
    
    // Iterative computation
    for (int iter = 0; iter < max_iterations; ++iter) {
        // Reset new_ranks
        for (const auto& pk : node_pks) {
            new_ranks[pk] = random_jump;
        }
        
        // Distribute rank from each node to its outgoing neighbors
        for (const auto& pk : node_pks) {
            const double rank = ranks[pk];
            
            auto out_it = topo.outgoing.find(pk);
            if (out_it == topo.outgoing.end() || out_it->second.empty()) {
                // No outgoing edges: distribute rank equally to all nodes (random jump)
                const double distributed = rank * damping / n;
                for (const auto& target : node_pks) {
                    new_ranks[target] += distributed;
                }
            } else {
                // Distribute rank to outgoing neighbors
                const size_t out_degree = out_it->second.size();
                const double distributed = rank * damping / out_degree;
                
                for (const auto& neighbor : out_it->second) {
                    auto it = new_ranks.find(neighbor);
                    if (it != new_ranks.end()) {
                        it->second += distributed;
                    }
                }
            }
        }
        
        // Check convergence
        double delta = 0.0;
        for (const auto& pk : node_pks) {
            delta += std::abs(new_ranks[pk] - ranks[pk]);
        }
        
        // Swap ranks
        ranks.swap(new_ranks);
        
        if (delta < tolerance) {
            THEMIS_INFO("PageRank converged after {} iterations (delta: {})", iter + 1, delta);
            break;
        }
        
        if (iter == max_iterations - 1) {
            THEMIS_WARN("PageRank did not converge after {} iterations (delta: {})", max_iterations, delta);
        }
    }
    
    return {Status::OK(), std::move(ranks)};
}

// Betweenness Centrality: Brandes Algorithm
// Measures how often a node lies on shortest paths between other nodes
std::pair<GraphAnalytics::Status, std::map<std::string, double>>
GraphAnalytics::betweennessCentrality(const std::vector<std::string>& node_pks) const {
    
    if (node_pks.empty()) {
        return {Status::Error("Empty node list provided"), {}};
    }
    
    // Build topology
    auto [st, topo] = buildTopology(node_pks);
    if (!st.ok) {
        return {st, {}};
    }
    
    // Initialize betweenness scores
    std::map<std::string, double> betweenness;
    for (const auto& pk : node_pks) {
        betweenness[pk] = 0.0;
    }
    
    // Brandes algorithm: Compute betweenness for each source node
    for (const auto& source : node_pks) {
        // BFS from source
        std::queue<std::string> q;
        std::map<std::string, std::vector<std::string>> predecessors; // predecessors on shortest paths
        std::map<std::string, int> distance;
        std::map<std::string, int> sigma; // number of shortest paths
        std::map<std::string, double> delta; // dependency
        
        for (const auto& pk : node_pks) {
            distance[pk] = -1;
            sigma[pk] = 0;
            delta[pk] = 0.0;
        }
        
        distance[source] = 0;
        sigma[source] = 1;
        q.push(source);
        
        std::vector<std::string> stack; // nodes in order of discovery (for backtracking)
        
        // Forward BFS
        while (!q.empty()) {
            std::string v = q.front();
            q.pop();
            stack.push_back(v);
            
            auto out_it = topo.outgoing.find(v);
            if (out_it == topo.outgoing.end()) continue;
            
            for (const auto& w : out_it->second) {
                // First time we see w?
                if (distance[w] < 0) {
                    distance[w] = distance[v] + 1;
                    q.push(w);
                }
                
                // Shortest path to w via v?
                if (distance[w] == distance[v] + 1) {
                    sigma[w] += sigma[v];
                    predecessors[w].push_back(v);
                }
            }
        }
        
        // Backward accumulation of dependencies
        while (!stack.empty()) {
            std::string w = stack.back();
            stack.pop_back();
            
            if (predecessors.count(w)) {
                for (const auto& v : predecessors[w]) {
                    delta[v] += (static_cast<double>(sigma[v]) / sigma[w]) * (1.0 + delta[w]);
                }
            }
            
            if (w != source) {
                betweenness[w] += delta[w];
            }
        }
    }
    
    // For undirected graphs, divide by 2 (we count each path twice)
    // For directed graphs, no division needed
    // Since we're working with directed graphs, keep as is
    
    return {Status::OK(), std::move(betweenness)};
}

// Closeness Centrality: Average shortest path distance
// Measures how close a node is to all other nodes
std::pair<GraphAnalytics::Status, std::map<std::string, double>>
GraphAnalytics::closenessCentrality(const std::vector<std::string>& node_pks) const {
    
    if (node_pks.empty()) {
        return {Status::Error("Empty node list provided"), {}};
    }
    
    // Build topology
    auto [st, topo] = buildTopology(node_pks);
    if (!st.ok) {
        return {st, {}};
    }
    
    std::map<std::string, double> closeness;
    
    // For each node, compute average distance to all other reachable nodes
    for (const auto& source : node_pks) {
        // BFS from source to find distances
        std::queue<std::string> q;
        std::map<std::string, int> distance;
        
        for (const auto& pk : node_pks) {
            distance[pk] = -1;
        }
        
        distance[source] = 0;
        q.push(source);
        
        while (!q.empty()) {
            std::string v = q.front();
            q.pop();
            
            auto out_it = topo.outgoing.find(v);
            if (out_it == topo.outgoing.end()) continue;
            
            for (const auto& w : out_it->second) {
                if (distance[w] < 0) {
                    distance[w] = distance[v] + 1;
                    q.push(w);
                }
            }
        }
        
        // Compute closeness: inverse of average distance
        int total_distance = 0;
        int reachable_count = 0;
        
        for (const auto& pk : node_pks) {
            if (pk != source && distance[pk] >= 0) {
                total_distance += distance[pk];
                reachable_count++;
            }
        }
        
        if (reachable_count > 0) {
            // Closeness = (n-1) / sum(distances)
            // Where n = number of reachable nodes
            closeness[source] = static_cast<double>(reachable_count) / total_distance;
        } else {
            // Isolated node: no closeness
            closeness[source] = 0.0;
        }
    }
    
    return {Status::OK(), std::move(closeness)};
}

// ============================================================================
// Community Detection - Louvain Algorithm
// ============================================================================

std::pair<GraphAnalytics::Status, std::map<std::string, int>> 
GraphAnalytics::louvainCommunities(
    const std::vector<std::string>& node_pks,
    double min_modularity_gain
) const {
    if (node_pks.empty()) {
        return {Status::OK(), {}};
    }

    // Build topology
    auto [st, topo] = buildTopology(node_pks);
    if (!st.ok) return {st, {}};

    // Initialize: each node in its own community
    std::map<std::string, int> node_to_comm;
    int next_comm_id = 0;
    for (const auto& pk : node_pks) {
        node_to_comm[pk] = next_comm_id++;
    }

    // Count total edges (bidirectional edges count once)
    std::set<std::pair<std::string, std::string>> unique_edges;
    for (const auto& [src, neighbors] : topo.outgoing) {
        for (const auto& dst : neighbors) {
            auto edge_pair = std::minmax(src, dst);
            unique_edges.insert(edge_pair);
        }
    }
    double m = static_cast<double>(unique_edges.size());
    if (m == 0.0) m = 1.0;  // Avoid division by zero

    // Compute node degrees (total degree)
    std::map<std::string, double> node_degree;
    for (const auto& pk : node_pks) {
        double deg = 0.0;
        if (topo.outgoing.count(pk)) deg += topo.outgoing.at(pk).size();
        if (topo.incoming.count(pk)) deg += topo.incoming.at(pk).size();
        node_degree[pk] = deg;
    }

    // Louvain optimization - multiple passes
    bool improved = true;
    int iteration = 0;
    const int MAX_ITERATIONS = 100;

    while (improved && iteration < MAX_ITERATIONS) {
        improved = false;
        iteration++;

        // Phase 1: Local moves
        for (const auto& node : node_pks) {
            int current_comm = node_to_comm[node];
            
            // Collect neighboring communities and edge counts
            std::map<int, double> comm_edges;  // edges from node to each community
            
            if (topo.outgoing.count(node)) {
                for (const auto& nb : topo.outgoing.at(node)) {
                    comm_edges[node_to_comm[nb]] += 1.0;
                }
            }
            if (topo.incoming.count(node)) {
                for (const auto& nb : topo.incoming.at(node)) {
                    comm_edges[node_to_comm[nb]] += 1.0;
                }
            }

            if (comm_edges.empty()) continue;  // Isolated node

            // Try each neighboring community
            int best_comm = current_comm;
            double best_delta_q = 0.0;

            for (const auto& [candidate_comm, edges_to_comm] : comm_edges) {
                if (candidate_comm == current_comm) continue;

                // Calculate modularity change (simplified):
                // Delta Q = (edges_to_comm / m) - (k_i * Sigma_comm / (2*m*m))
                // For simplicity, we focus on maximizing edges within community
                double delta_q = edges_to_comm / m;

                if (delta_q > best_delta_q) {
                    best_delta_q = delta_q;
                    best_comm = candidate_comm;
                }
            }

            // Move if improvement found
            if (best_delta_q > min_modularity_gain && best_comm != current_comm) {
                node_to_comm[node] = best_comm;
                improved = true;
            }
        }
    }

    // Renumber communities contiguously
    std::map<int, int> old_to_new;
    int new_id = 0;
    std::map<std::string, int> result;
    
    for (const auto& [pk, old_comm] : node_to_comm) {
        if (!old_to_new.count(old_comm)) {
            old_to_new[old_comm] = new_id++;
        }
        result[pk] = old_to_new[old_comm];
    }

    return {Status::OK(), std::move(result)};
}

// ============================================================================
// Community Detection - Label Propagation
// ============================================================================

std::pair<GraphAnalytics::Status, std::map<std::string, int>> 
GraphAnalytics::labelPropagationCommunities(
    const std::vector<std::string>& node_pks,
    int max_iterations
) const {
    if (node_pks.empty()) {
        return {Status::OK(), {}};
    }

    // Build topology
    auto [st, topo] = buildTopology(node_pks);
    if (!st.ok) return {st, {}};

    // Initialize: each node gets unique label (community ID)
    std::map<std::string, int> labels;
    int next_label = 0;
    for (const auto& pk : node_pks) {
        labels[pk] = next_label++;
    }

    // Iterative label propagation
    bool changed = true;
    int iteration = 0;

    while (changed && iteration < max_iterations) {
        changed = false;
        iteration++;

        // Process nodes in random-like order (use pk string order for determinism in tests)
        std::vector<std::string> nodes_shuffled = node_pks;
        
        for (const auto& node : nodes_shuffled) {
            // Count labels among neighbors
            std::map<int, int> label_count;
            
            // Outgoing neighbors
            if (topo.outgoing.count(node)) {
                for (const auto& neighbor : topo.outgoing.at(node)) {
                    label_count[labels[neighbor]]++;
                }
            }
            
            // Incoming neighbors
            if (topo.incoming.count(node)) {
                for (const auto& neighbor : topo.incoming.at(node)) {
                    label_count[labels[neighbor]]++;
                }
            }

            if (label_count.empty()) continue;  // Isolated node

            // Find most frequent label
            int best_label = labels[node];
            int best_count = 0;
            
            for (const auto& [label, count] : label_count) {
                if (count > best_count) {
                    best_count = count;
                    best_label = label;
                }
            }

            // Update label if changed
            if (best_label != labels[node]) {
                labels[node] = best_label;
                changed = true;
            }
        }
    }

    return {Status::OK(), std::move(labels)};
}

} // namespace themis
