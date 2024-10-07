/**
 * Copyright (C) 2019 Dean De Leo, email: dleo[at]cwi.nl
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

// Whether to use libcuckoo or tbb as a dictionary to store the vertex IDs. With libcuckoo we
// try to keep track whether vertex ids are should only be visible in the write-store. With tbb there
// is only a global view, resembling the same behaviour of GraphOne.
// #define LLAMA_HASHMAP_WITH_TBB /* set by configure.ac */

#include <atomic>
#include <chrono>

#include "common/spinlock.hpp"
#include "common/timer.hpp"
#include "library/interface.hpp"
#if defined(LLAMA_HASHMAP_WITH_TBB)
#include "tbb/concurrent_hash_map.h"
#else
#include "third-party/libcuckoo/cuckoohash_map.hh"
#endif
#if defined(LLAMA_FAIR_SHARED_MUTEX)
#include "llama_mutex.hpp"
#else
#include <shared_mutex>
#endif

class ll_database; // forward declaration
class ll_mlcsr_ro_graph; // forward declaration
namespace gfe::utility { class TimeoutService; } // forward declaration
[[maybe_unused]] static void _bm_run_llama(); // bm tool
[[maybe_unused]] static void _test_perf_run_llama(); // Performance test

namespace gfe::library {

/**
 * Adaptor for the LLAMA library.
 *
 * All methods except dump() & family are thread safe.
 */
class LLAMAClass : public virtual UpdateInterface, public virtual GraphalyticsInterface {
    friend void ::_bm_run_llama(); // bm tool
    friend void ::_test_perf_run_llama(); // performance test
protected:
    LLAMAClass(const LLAMAClass&) = delete;
    LLAMAClass& operator=(const LLAMAClass&) = delete;
#if defined(LLAMA_FAIR_SHARED_MUTEX)
    using shared_mutex_t = llama_details::FairSharedMutex;
#else
    using shared_mutex_t = std::shared_mutex;
#endif

    const bool m_is_directed; // is the graph directed
    ll_database* m_db { nullptr };
    uint64_t m_num_edges { 0 }; // the current number of edges contained
    std::chrono::seconds m_timeout { 0 }; // the budget to complete each of the algorithms in the Graphalytics suite
    mutable shared_mutex_t m_lock_checkpoint; // invoking #build(), that is creating a new snapshot, must be done without any other interference from other writers

#if defined(LLAMA_HASHMAP_WITH_TBB)
    tbb::concurrent_hash_map<uint64_t, /* node_t */ int64_t> m_vmap; // vertex dictionary, from external vertex ID to internal vertex ID
#else
    // A vertex mapping (vmap) is a hash table where the keys are external node ids (e.g. user_id = 2184128047 ) and the mapped values
    // are corresponding logical node ids in the llama library (e.g. logical_id = 3 ).
    // In this we have three vmaps:
    // m_vmap_read_only is for the vertices in the last read-only level (delta) of
    // m_vmap_removed is a list of vertices that exist in m_vmap_read_only but not anymore in the newest snapshot m_vmap_write
    // m_vmap_write is for the newer vertices introduces in the current write based store
    // To translate an external node id into a logical id:
    // 1- Acquire the shared lock to m_lock_checkpoint
    // 2- Check the association in `m_vmap_write'
    // 3- If it is not present, check the vertex has not been marked in `v_map_removed'
    // 4- If it is not present, check `m_vmap_read_only'
    cuckoohash_map<uint64_t, int64_t> m_vmap_updated;
    cuckoohash_map<uint64_t, int64_t> m_vmap_removed;
    cuckoohash_map<uint64_t, int64_t> m_vmap_read_only;

    mutable common::SpinLock* m_vmap_locks {nullptr}; // array of locks, to sync m_vertex_mappings and operations in m_db
    const uint64_t m_num_vmap_locks = /* arbitrary number */ 4096 ; // size of the array `m_vmap_locks'


    // Retrieve the logical vertex_id for the given external vertex_id, starting from the write store. This method is not thread-safe.
    // The method throws std::out_of_range in case the vertex is not present to mimic the behaviour of cuckoohash_map#find
    // @return the logical id of the vertex
    // throws std::out_of_range in case of failure
    int64_t vmap_write_store_find(uint64_t external_vertex_id) const;

    // Check whether the given vertex exists in the write store
    bool vmap_write_store_contains(uint64_t external_vertex_id) const;
#endif
    // Retrieve the internal vertex id (only looking into the read store with libcuckoo) for the given external vertex ID. It raises an exception if the vertex does not exist.
    int64_t get_internal_vertex_id(uint64_t external_vertex_id) const;

    // Retrieve the internal vertex id for the given external vertex id. Create a new vertex if the vertex id is not already present.
    int64_t get_or_create_vertex_id(uint64_t external_vertex_id);

    // Retrieve the outgoing degree (# outgoing edges) for the given logical vertex_id, starting from the write store
    uint64_t get_write_store_outdegree(int64_t llama_vertex_id) const;

    // Retrieve the outgoing degree (# outgoing edges) for the given logical vertex id in the given snapshot
    uint64_t get_read_store_outdegree(ll_mlcsr_ro_graph& snapshot, int64_t llama_vertex_id) const;

    // Actual routine to internally create a new edge
    bool add_edge0(int64_t llama_source_id, int64_t llama_destination_id, double weight);

    // The actual implementation for dump. The parameter T can either be ll_mlcsr_ro_graph or ll_writable_graph
    template<typename T>
    void dump_impl(std::ostream& out, T& graph) const;

    // Print the given snapshot. Meant to be used inside a debugger.
    //This method is not thread safe.
    void dump_snapshot(ll_mlcsr_ro_graph& graph) const;

    // Retrieve the last snapshot/delta of the read-only graph. The caller is expected to hold the shared lock m_lock_checkpoint
    ll_mlcsr_ro_graph get_snapshot() const;

    // Internal implementation of the PageRank algorithm.
    void pagerank_impl(utility::TimeoutService& timer, ll_mlcsr_ro_graph& graph, uint64_t num_vertices, uint64_t num_iterations, double damping_factor, /* output array, already allocated */ double* rank);

    // Internal implementation of the CDLP algorithm
    std::unique_ptr<uint64_t[]> cdlp_impl(utility::TimeoutService& timer, ll_mlcsr_ro_graph& graph, uint64_t max_iterations);

    // Helper for Graphalytics: translate the logical IDs into external IDs
    template <typename T>
    std::vector<std::pair<uint64_t, T>> translate(ll_mlcsr_ro_graph& graph, const T* __restrict data);

    // Helper, save the content of the vector to the given output file
    template <typename T, bool negative_scores = true>
    void save_results(const std::vector<std::pair<uint64_t, T>>& result, const char* dump2file);
public:
    /**
     * Constructor
     * @param is_directed true if the underlying graph is directed, false otherwise
     */
    LLAMAClass(bool is_directed);

    /**
     * Destructor
     */
    virtual ~LLAMAClass();

    /**
     * Dump the content of the graph to given stream.
     */
    virtual void dump_ostream(std::ostream& out) const;

    /**
     * Dump the content of the graph to given stream, up to the given level/snapshot/delta
     * @param level the snapshot to dump
     */
    virtual void dump_ostream(std::ostream& out, int level) const;

    /**
     * Get the number of edges contained in the latest read-only snapshot (excluding the write-based store)
     */
    virtual uint64_t num_edges() const;

    /**
     * Get the number of nodes in the latest read-only snapshot (excluding the write-based store)
     */
    virtual uint64_t num_vertices() const;

    /**
     * Get the number of read-only levels (or snapshots, or deltas). Every invocation to #build creates a new read-only level
     * where the changes from the write store are frozen and moved into the read-only store.
     */
    virtual uint64_t num_levels() const;

    /**
     * Returns true if the given vertex is present, false otherwise
     */
    virtual bool has_vertex(uint64_t vertex_id) const;

    /**
     * Returns the weight of the given edge is the edge is present, or NaN otherwise
     */
    virtual double get_weight(uint64_t source, uint64_t destination) const;

    /**
     * Check whether the graph is directed
     */
    virtual bool is_directed() const;

    /**
     * Impose a timeout on each graph computation. A computation that does not terminate by the given seconds will raise a TimeoutError.
     */
    virtual void set_timeout(uint64_t seconds);

    /**
     * Add the given vertex to the graph
     * @return true if the vertex has been inserted, false otherwise (e.g. the vertex already exists)
     */
    virtual bool add_vertex(uint64_t vertex_id);

    /**
     * Remove the given vertex and all its attached edges (both incoming and outgoing) from the graph
     * @param vertex_id the vertex to remove
     * @return true in case of success, false if the given vertex does not exist.
     */
    virtual bool remove_vertex(uint64_t vertex_id);

    /**
     * Add the given edge in the graph
     * @return true if the edge has been inserted, false otherwise (e.g. this edge already exists)
     */
    virtual bool add_edge(graph::WeightedEdge e);

    /**
     * Add the given edge in the graph. Implicitly create the referred vertices if they do not already exist.
     * @return true if the edge has been inserted, false otherwise (e.g. this edge already exists)
     */
    virtual bool add_edge_v2(gfe::graph::WeightedEdge e);

    /**
     * Remove the given edge from the graph
     * @return true if the given edge has been removed, false otherwise (e.g. this edge does not exist)
     */
    virtual bool remove_edge(graph::Edge e);

    /**
     * Create a new delta, or a level, in LLAMA's parlance
     */
    virtual void build();

    /**
     * Perform a BFS from source_vertex_id to all the other vertices in the graph.
     * @param source_vertex_id the vertex where to start the search
     * @param dump2file if not null, dump the result in the given path, following the format expected by the benchmark specification
     */
    virtual void bfs(uint64_t source_vertex_id, const char* dump2file = nullptr);

    /**
     * Execute the PageRank algorithm for the specified number of iterations.
     *
     * @param num_iterations the number of iterations to execute the algorithm
     * @param damping_factor weight for the PageRank algorithm, it affects the score associated to the sink nodes in the graphs
     * @param dump2file if not null, dump the result in the given path, following the format expected by the benchmark specification
     */
    virtual void pagerank(uint64_t num_iterations, double damping_factor = 0.85, const char* dump2file = nullptr);

    /**
     * Weakly connected components (WCC), associate each node to a connected component of the graph
     * @param dump2file if not null, dump the result in the given path, following the format expected by the benchmark specification
     */
    virtual void wcc(const char* dump2file = nullptr);

    /**
     * Community Detection using Label-Propagation. Associate a label to each vertex of the graph, according to its neighbours.
     * @param max_iterations max number of iterations to perform
     * @param dump2file if not null, dump the result in the given path, following the format expected by the benchmark specification
     */
    virtual void cdlp(uint64_t max_iterations, const char* dump2file = nullptr);

    /**
     * Local clustering coefficient. Associate to each vertex the ratio between the number of its outgoing edges and the number of
     * possible remaining edges.
     * @param dump2file if not null, dump the result in the given path, following the format expected by the benchmark specification
     */
    virtual void lcc(const char* dump2file = nullptr);

    /**
     * Single-source shortest paths. Compute the weight related to the shortest path from the source to any other vertex in the graph.
     * @param source_vertex_id the vertex where to start the search
     * @param dump2file if not null, dump the result in the given path, following the format expected by the benchmark specification
     */
    virtual void sssp(uint64_t source_vertex_id, const char* dump2file = nullptr);

#if defined(LL_PROFILE_UPDATES)
    // Overhead to create new delta level
    virtual void updates_start();
    virtual void updates_stop();
#endif
};

} // namespace
