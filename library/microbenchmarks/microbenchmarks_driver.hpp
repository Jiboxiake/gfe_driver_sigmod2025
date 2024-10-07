//
// Created by per on 12.06.21.
//

#pragma once

#include <string>
#include <atomic>
#include <cassert>
#include <fstream>
#include <unordered_set>
#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_vector.h>

#include "library/interface.hpp"

#include <TopologyInterface.h>

namespace gfe::library {
    using namespace microbenchmarks;
    using namespace tbb;

    typedef concurrent_hash_map<uint64_t, uint64_t> vertex_mapping;

    class MicroBenchmarksDriver : public virtual UpdateInterface, public virtual GraphalyticsInterface {
        MicroBenchmarksDriver(const MicroBenchmarksDriver &) = delete;
        MicroBenchmarksDriver &operator=(const MicroBenchmarksDriver &) = delete;

        std::atomic<uint64_t> next_vertex_id = 0ul;
        vertex_mapping external_2_internal;
        mutex growing_vector_mutex;
        concurrent_vector<uint64_t> internal_2_external = concurrent_vector<uint64_t>(1024, numeric_limits<uint64_t>::max());
        mutex vertex_add_mutex;

        unordered_set<uint64_t> registered_vertices;

        void grow_vector_if_smaller(concurrent_vector<uint64_t> &v, size_t s) {
          if (v.capacity() <= s) {  // Only synchronize with other threads if potentially necessary
            scoped_lock<mutex> l(growing_vector_mutex);
            if (v.capacity() <= s) {
              v.grow_to_at_least(v.capacity() * 2, numeric_limits<uint64_t>::max());
            }

          }
        }

    protected:
        std::unique_ptr<microbenchmarks::TopologyInterface> ds;
        // the budget to complete each of the algorithms in the Graphalytics suite
        std::chrono::seconds m_timeout{0};

        template<typename T>
        vector <pair<uint64_t, T>> translate(vector <T> &values) {
          size_t N = values.size();

          vector <pair<vertex_id_t, T>> logical_result(N);

#pragma omp parallel for
          for (uint v = 0; v < N; v++) {
            if (ds->has_vertex(v)) {
              logical_result[v] = make_pair(internal_2_external[v], values[v]);
            } else {
              logical_result[v] = make_pair(v, numeric_limits<T>::max());
            }
          }
          return logical_result;
        }

        template<typename T>
        void save_result(vector <pair<uint64_t, T>> &result, const char *dump2file) {
          assert(dump2file != nullptr);
//          COUT_DEBUG("save the results to: " << dump2file)

          fstream handle(dump2file, ios_base::out);
          if (!handle.good()) ERROR("Cannot save the result to `" << dump2file << "'");

          for (const auto &p : result) {
            handle << p.first << " ";
            handle << p.second;
            handle << "\n";
          }
          handle.close();
        }

    public:

        MicroBenchmarksDriver(bool is_graph_directed, const string& name);

        /**
         * Destructor
         */
        virtual ~MicroBenchmarksDriver();

        void dump_ostream(std::ostream &out) const override;

        /**
         * Get the number of edges contained in the graph
         */
        virtual uint64_t num_edges() const;

        /**
         * Get the number of nodes stored in the graph
         */
        virtual uint64_t num_vertices() const;

        /**
         * Returns true if the given vertex is present, false otherwise
         */
        virtual bool has_vertex(uint64_t vertex_id) const;

        virtual bool has_edge(uint64_t source, uint64_t destination) const override;

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
         * @return true if the vertex has been inserted, false otherwise (that is, the vertex already exists)
         */
        virtual bool add_vertex(uint64_t vertex_id);

        /**
         * Remove the mapping for a given vertex. The actual internal vertex is not removed from the adjacency list.
         * @param vertex_id the vertex to remove
         * @return true if a mapping for that vertex existed, false otherwise
         */
        virtual bool remove_vertex(uint64_t vertex_id);

        /**
        * Add the given edge in the graph
        * @return true if the edge has been inserted, false if this edge already exists or one of the referred
        *    vertices does not exist.
        */
        virtual bool add_edge(gfe::graph::WeightedEdge e);

        /**
        * Add the given edge in the graph. Implicitly create the referred vertices if they do not already exist
        * @return true if the edge has been inserted, false otherwise (e.g. this edge already exists)
        */
        virtual bool add_edge_v2(gfe::graph::WeightedEdge e);

        /**
         * Remove the given edge from the graph. There is no way to check whether the operation actually succeeded
         * in this implementation of GraphOne. Attempting to remove an edge that does not exist may result in a crash.
         * @return always true when both the source & the destination vertices already exist, false otherwise
         */
        virtual bool remove_edge(gfe::graph::Edge e);

        virtual void on_main_init(int num_threads);

        /**
         * Callback, invoked when a thread is created
         */
        virtual void on_thread_init(int thread_id);

        /**
         * Callback, invoked when a thread is going to be removed
         */
        virtual void on_thread_destroy(int thread_id);

        /**
         * Perform a BFS from source_vertex_id to all the other vertices in the graph.
         * @param source_vertex_id the vertex where to start the search
         * @param dump2file if not null, dump the result in the given path, following the format expected by the benchmark specification
         */
        virtual void bfs(uint64_t source_vertex_id, const char *dump2file = nullptr);

        /**
         * Execute the PageRank algorithm for the specified number of iterations.
         *
         * @param num_iterations the number of iterations to execute the algorithm
         * @param damping_factor weight for the PageRank algorithm, it affects the score associated to the sink nodes in the graphs
         * @param dump2file if not null, dump the result in the given path, following the format expected by the benchmark specification
         */
        virtual void
        pagerank(uint64_t num_iterations, double damping_factor = 0.85, const char *dump2file = nullptr);

        /**
         * Weakly connected components (WCC), associate each node to a connected component of the graph
         * @param dump2file if not null, dump the result in the given path, following the format expected by the benchmark specification
         */
        virtual void wcc(const char *dump2file = nullptr);

        /**
         * Community Detection using Label-Propagation. Associate a label to each vertex of the graph, according to its neighbours.
         * @param max_iterations max number of iterations to perform
         * @param dump2file if not null, dump the result in the given path, following the format expected by the benchmark specification
         */
        virtual void cdlp(uint64_t max_iterations, const char *dump2file = nullptr);

        /**
         * Local clustering coefficient. Associate to each vertex the ratio between the number of its outgoing edges and the number of
         * possible remaining edges.
         * @param dump2file if not null, dump the result in the given path, following the format expected by the benchmark specification
         */
        virtual void lcc(const char *dump2file = nullptr);

        /**
         * Single-source shortest paths. Compute the weight related to the shortest path from the source to any other vertex in the graph.
         * @param source_vertex_id the vertex where to start the search
         * @param dump2file if not null, dump the result in the given path, following the format expected by the benchmark specification
         */
        virtual void sssp(uint64_t source_vertex_id, const char *dump2file = nullptr);

        virtual bool can_be_validated() const;

        virtual bool has_weights() const;

        virtual void updates_stop() override;
    };

}
