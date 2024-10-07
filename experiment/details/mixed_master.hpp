//
// Created by zhou822 on 8/6/23.
//

#pragma once

#include <memory>
#include <vector>
#include <atomic>

#include "common/static_index.hpp"
#include "experimental/mixed_workload_result.hpp"
#include "graph/edge.hpp"
#include "third-party/libcuckoo/cuckoohash_map.hh"

// forward declarations
namespace gfe::experiment { class UpdatesShortReadsExperiment; }
namespace gfe::experiment::details { class Aging2Worker; }
namespace gfe::experiment::details { class ShortReadWorker; }
namespace gfe::experiment::details { class LatencyStatistics; }

namespace gfe::experiment::details {
    class UpdateShortReadsMaster{
        friend class Aging2Worker;
        friend class ShortReadWorker;

        const UpdatesShortReadsExperiment& m_parameters;
        const bool m_is_directed;
        std::vector<Aging2Worker*> update_workers; // pool of workers
        std::vector<ShortReadWorker*> read_workers;
        std::atomic<int64_t> m_num_updates_performed = 0; // current number of operations performed so far
        std::atomic<int64_t> m_num_reads_performed = 0; // current number of operations performed so far
        std::atomic<int> m_last_progress_reported = 0; // the last progress of the experiment, reported by any of the worker threads. E.g. 1%, 2%, 3%, so on.

        // report how long it took to perform 1x, 2x, 3x, ... updates w.r.t. to the loaded graph.
        std::chrono::steady_clock::time_point m_time_start; // when the computation started
        uint64_t* m_reported_times = nullptr; // microsecs
        std::atomic<int> m_last_time_reported = 0;

        // latencies of each update
        uint64_t* m_latencies = nullptr; // nanosecs
        uint64_t m_latencies_num_insertions = 0; // total number of operations that are insertions
        uint64_t m_latencies_num_deletions = 0; // total number of operations that are deletions
        uint64_t m_latencies_num_reads_found = 0;
        uint64_t m_latencies_num_reads_missing = 0;

        // Stinger is so slow, that we stop the experiment after four hours
        std::atomic<bool> m_stop_experiment = false;
        enum class StopReason { NOT_SET, TIMEOUT_HIT, MEMORY_FOOTPRINT }; // the reason the experiment has been stopped
        StopReason m_stop_reason = StopReason::NOT_SET;

        UpdatesReadsMixedWorkloadResult m_results;

        std::atomic_bool m_experiment_running = false;

        // Initialise the set of workers
        void init_workers();

        // Load & partition the edges to insert/remove in the available workers
        void load_edges();

        // Prepare the array to record the latency of all updates
        void prepare_latencies();

        // Execute the main part of the experiment, that is the insertions/deletions in the graph with the worker threads
        void do_run_experiment();

        // Remove the vertices that do not belong to the final graph
        void remove_vertices();

        // Save the current results in `m_results'
        void store_results();

        // Retrieve the current number of operations performed so far by the workers
        uint64_t num_operations_sofar() const;

        // Wait for the workers to complete, record the throughput in the meanwhile
        void wait_and_record();

        // Wait idle for the cool-off period
        void cooloff(std::chrono::steady_clock::time_point start_time);

        // print to stdout the number of vertices/edges expected and effectively stored in the final library. The two values should be equal.
        void log_num_vtx_edges();

        // Grab the vertex id of a random (final) edge
        void set_random_vertex_id(uint64_t* edges, uint64_t num_edges);

        // Get the current memory footprint of the experiment, in bytes
        uint64_t memory_footprint() const;

    public:
        UpdateShortReadsMaster(const UpdatesShortReadsExperiment& parameters);

        ~UpdateShortReadsMaster();

        UpdatesReadsMixedWorkloadResult execute();

        // Is the graph directed?
        bool is_directed() const { return m_is_directed; }

        // Total number of operations to perform (insertions/deletions)
        uint64_t num_operations_total() const;

        // Total number of edges expected in the final graph
        uint64_t num_edges_final_graph() const;

        // Access the configuration of this experiment
        const UpdatesShortReadsExperiment& parameters() const { return m_parameters; }

        double progress_so_far() const;
    };
}//namespace