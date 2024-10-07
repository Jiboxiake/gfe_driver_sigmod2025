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

#include "aging2_master.hpp"

#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>
#include <unordered_set>

#include "common/error.hpp"
#include "common/quantity.hpp"
#include "common/system.hpp"
#include "common/timer.hpp"
#include "experiment/aging2_experiment.hpp"
#include "experiment/aging2_result.hpp"
#include "reader/graphlog_reader.hpp"
#include "library/interface.hpp"
#include "utility/memory_usage.hpp"
#include "aging2_worker.hpp"
#include "build_thread.hpp"
#include "configuration.hpp"
#include "latency.hpp"

using namespace common;
using namespace std;

/*****************************************************************************
 *                                                                           *
 * Debug                                                                     *
 *                                                                           *
 *****************************************************************************/
extern mutex _log_mutex [[maybe_unused]];
//#define DEBUG

#define COUT_DEBUG_FORCE(msg) { scoped_lock<mutex> lock(_log_mutex); cout << "[Aging2Master::" << __FUNCTION__ << "] [" << concurrency::get_thread_id() << "] " << msg << endl; }
#if defined(DEBUG)
#define COUT_DEBUG(msg) COUT_DEBUG_FORCE(msg)
#else
#define COUT_DEBUG(msg)
#endif

/*****************************************************************************
 *                                                                           *
 * Init                                                                      *
 *                                                                           *
 *****************************************************************************/
namespace gfe::experiment::details {
    //https://www.geeksforgeeks.org/how-to-create-an-unordered_map-of-pairs-in-c/
    struct hash_edge {
        size_t operator()(const pair <uint64_t, uint64_t> &p) const {
            auto hash1 = std::hash<uint64_t>()(p.first);
            auto hash2 = std::hash<uint64_t>()(p.second);
            if (hash1 != hash2)[[likely]] {
                return hash1 ^ hash2;
            }
            // If hash1 == hash2, their XOR is zero.
            return hash1;
        }
    };

    Aging2Master::Aging2Master(const Aging2Experiment &parameters) :
            m_parameters(parameters),
            m_is_directed(m_parameters.m_library->is_directed()),
            m_results(parameters) {

        auto properties = reader::graphlog::parse_properties(parameters.m_path_log);
        m_results.m_num_artificial_vertices = stoull(properties["internal.vertices.temporary.cardinality"]);
        m_results.m_num_vertices_load = stoull(properties["internal.vertices.final.cardinality"]);
        m_results.m_num_edges_load = stoull(properties["internal.edges.final"]);
        m_results.m_num_operations_total = stoull(properties["internal.edges.cardinality"]);

        // 1024 is a hack to avoid issues with small graphs
        m_reported_times = new uint64_t[static_cast<uint64_t>(m_parameters.m_num_reports_per_operations *
                                                              ::ceil(static_cast<double>(num_operations_total()) /
                                                                     num_edges_final_graph()) + 1 )]();
        m_parameters.m_library->on_main_init(m_parameters.m_num_threads + /* this + builder service */ 2 +
                                             /* plus potentially an analytics runner (mixed epxeriment) */ 1);

        init_workers();
        m_parameters.m_library->on_thread_init(m_parameters.m_num_threads + 1);
    }

    Aging2Master::~Aging2Master() {
        for (auto w: m_workers) { delete w; }
        m_workers.clear();
        m_parameters.m_library->on_thread_destroy(m_parameters.m_num_threads + 1);
        m_parameters.m_library->on_main_destroy();

        delete[] m_reported_times;
        m_reported_times = nullptr;

        delete[] m_latencies;
        m_latencies = nullptr;
    }

    void Aging2Master::init_workers() {
        Timer timer;
        timer.start();
        LOG("[Aging2] Initialising " << parameters().m_num_threads << " worker threads ... ");

        m_workers.reserve(parameters().m_num_threads);
        //sortledton:version
        //for(uint64_t worker_id = 1; worker_id < parameters().m_num_threads; worker_id++){
#if HAVE_SORTLEDTON
        for(uint64_t worker_id = 0; worker_id < parameters().m_num_threads; worker_id++){
#else
        for (uint64_t worker_id = 0; worker_id < parameters().m_num_threads; worker_id++) {
#endif

            m_workers.push_back(new Aging2Worker(*this, worker_id));
        }

        LOG("[Aging2] Workers initialised in " << timer);
    }

/*****************************************************************************
 *                                                                           *
 * Experiment                                                                *
 *                                                                           *
 *****************************************************************************/
    void Aging2Master::load_edges() {
        LOG("[Aging2] Loading the sequence of updates to perform from " << m_parameters.m_path_log << " ...");
        Timer timer;
        timer.start();

        fstream handle(m_parameters.m_path_log, ios_base::in | ios_base::binary);
        auto properties = reader::graphlog::parse_properties(handle);
        uint64_t array_sz = stoull(properties["internal.edges.block_size"]);
        unique_ptr<uint64_t[]> ptr_array1{new uint64_t[array_sz]};
        unique_ptr<uint64_t[]> ptr_array2{new uint64_t[array_sz]};
        uint64_t *array1 = ptr_array1.get();
        uint64_t *array2 = ptr_array2.get();
        reader::graphlog::set_marker(properties, handle, reader::graphlog::Section::EDGES);

        reader::graphlog::EdgeLoader loader(handle);
        uint64_t num_edges = loader.load(array1, array_sz / 3);
        //std::cout<<num_edges<<std::endl;
        uint64_t total_size = num_edges;
        //std::cout<<"system has "<<num_edges<<" edge operations"<<std::endl;
        while (num_edges > 0) {
            // partition the batch among the workers
            for (auto w: m_workers) w->load_edges(array1, num_edges);
            if (m_results.m_random_vertex_id == 0) { set_random_vertex_id(array1, num_edges); }

            // load the next batch in the meanwhile
            num_edges = loader.load(array2, array_sz / 3);
            //std::cout<<num_edges<<std::endl;
            total_size += num_edges;
            // wait for the workers to complete
            for (auto w: m_workers) w->wait();

            swap(array1, array2);
        }
        /*
        std::cout<<"worker 1"<<std::endl;
        m_workers.at(1)->print_workload(6509488+50,10);
        std::cout<<"worker 5"<<std::endl;
        m_workers.at(5)->print_workload(6509488+50,10);*/
        /*for(uint64_t x=0; x<39; x++){
            std::cout<<"worker "<<x<<std::endl;
            m_workers.at(x)->print_workload(6509488+50,10);
        }*/
        handle.close();

        timer.stop();
        LOG("[Aging2] Graphlog loaded in " << timer);
        std::cout << "system has " << total_size << " edge operations" << std::endl;

    }

/*
 * it matters whether the current batch is fully processed
 */
    void Aging2Master::load_edges_percent(reader::graphlog::EdgeLoader *loader, uint64_t *array1, uint64_t *array2,
                                          uint64_t &total_workload_size, uint64_t &read_operations_num,
                                          uint64_t synchronization_ratio, uint64_t array_sz) {

        uint64_t batch_to_read_size = total_workload_size / synchronization_ratio;
        uint64_t already_read_size = 0;
        if (read_operations_num % batch_to_read_size) {

        }
        uint64_t num_edges = loader->load(array1, array_sz / 3);
        //we assume batch size is always greater than at least 1 num_edges
        //std::cout<<"system has "<<num_edges<<" edge operations"<<std::endl;
        while (num_edges > 0) {
            // partition the batch among the workers
            if (already_read_size + num_edges <= batch_to_read_size) {
                for (auto w: m_workers) w->load_edges(array1, num_edges);
                if (m_results.m_random_vertex_id == 0) { set_random_vertex_id(array1, num_edges); }
                already_read_size += num_edges;
            } else {
                uint64_t to_read_size = batch_to_read_size - already_read_size;
                for (auto w: m_workers) w->load_edges(array1, to_read_size);
                if (m_results.m_random_vertex_id == 0) { set_random_vertex_id(array1, to_read_size); }
                break;
            }

            // load the next batch in the meanwhile
            num_edges = loader->load(array2, array_sz / 3);
            // wait for the workers to complete
            for (auto w: m_workers) w->wait();

            swap(array1, array2);
        }
        read_operations_num += already_read_size;
    }

    void Aging2Master::prepare_latencies() {
        LOG("[Aging2] Allocating space to record the latency of each update ...");
        Timer timer;
        timer.start();

        for (auto w: m_workers) {
            m_latencies_num_insertions += w->num_insertions();
            m_latencies_num_deletions += w->num_deletions();
        }
        assert(m_latencies_num_insertions + m_latencies_num_deletions == m_results.m_num_operations_total &&
               "Counting mismatch");

        m_latencies = new uint64_t[m_results.m_num_operations_total];

        uint64_t *latency_insertions = m_latencies;
        uint64_t *latency_deletions = m_latencies + m_latencies_num_insertions;
        for (auto w: m_workers) {
            w->set_latencies(latency_insertions, latency_deletions);

            // next offset
            latency_insertions += w->num_insertions();
            latency_deletions += w->num_deletions();
        }

        for (auto w: m_workers) { w->wait(); }

        timer.stop();
        LOG("[Aging2] Latency allocations done in " << timer);
    }

    void Aging2Master::do_run_experiment() {
       // LOG("[Aging2] Experiment started ...");
        m_last_progress_reported = 0;
        m_last_time_reported = 0;
        m_time_start = chrono::steady_clock::now();

        // init the build service (the one that creates the new snapshots/deltas)
       // BuildThread build_service{parameters().m_library, static_cast<int>(parameters().m_num_threads) + 2,
        //                          parameters().m_build_frequency};

        auto start_time = chrono::steady_clock::now();
        Timer timer;
        timer.start();
        m_parameters.m_library->updates_start();
        for (auto w: m_workers) w->execute_updates();
        m_experiment_running = true;
        wait_and_record();
        //build_service.stop();
        m_parameters.m_library->build(); // flush last changes
        m_parameters.m_library->updates_stop();
        timer.stop();
#if HAVE_GTX
        //m_parameters.m_library->on_edge_writes_finish();
#endif
     //   LOG("[Aging2] Experiment completed!");
     //   LOG("[Aging2] Updates performed with " << parameters().m_num_threads << " threads in " << timer);
        total_time_microseconds+=timer.microseconds();
        cooloff(start_time);
        m_results.m_completion_time = timer.microseconds();
        //m_results.m_num_build_invocations = build_service.num_invocations();
        m_results.m_num_levels_created = m_parameters.m_library->num_levels();
       // m_parameters.m_library->print_and_clear_txn_stats();
    }

    void Aging2Master::remove_vertices() {
        LOG("[Aging2] Removing the list of temporary vertices ...");
        Timer timer;
        timer.start();

        fstream handle(m_parameters.m_path_log, ios_base::in | ios_base::binary);
        auto properties = reader::graphlog::parse_properties(handle);
        uint64_t num_vertices = stoull(properties["internal.vertices.temporary.cardinality"]);
        unique_ptr<uint64_t[]> ptr_vertices{new uint64_t[num_vertices]};
        uint64_t *vertices = ptr_vertices.get();
        reader::graphlog::set_marker(properties, handle, reader::graphlog::Section::VTX_TEMP);

        reader::graphlog::VertexLoader loader{handle};
        loader.load(vertices, num_vertices);
        m_results.m_num_artificial_vertices = num_vertices;

        for (auto w: m_workers) w->remove_vertices(vertices, num_vertices);
        for (auto w: m_workers) w->wait();
        m_parameters.m_library->build();

        LOG("[Aging2] Number of extra vertices: " << m_results.m_num_artificial_vertices << ", "
                                                                                            "expansion factor: " <<
                                                  static_cast<double>(m_results.m_num_artificial_vertices +
                                                                      m_results.m_num_vertices_final_graph) /
                                                  m_results.m_num_vertices_final_graph);
        timer.stop();
        LOG("[Aging2] Temporary vertices removed in " << timer);
    }

    Aging2Result Aging2Master::execute() {
        load_edges();
        if (parameters().m_measure_latency) prepare_latencies();
        do_run_experiment();
        remove_vertices();

        store_results();
        log_num_vtx_edges();

        return m_results;
    }

/*****************************************************************************
 *                                                                           *
 * Utility methods                                                           *
 *                                                                           *
 *****************************************************************************/
    uint64_t Aging2Master::num_operations_total() const {
        return m_results.m_num_operations_total;
    }

    uint64_t Aging2Master::num_operations_sofar() const {
        uint64_t total = 0;
        for (auto w: m_workers) total += w->num_operations();
        return total;
    }

    uint64_t Aging2Master::num_edges_final_graph() const {
        return m_results.m_num_edges_load;
    }

    void Aging2Master::wait_and_record() {
        bool done = false;
        m_results.m_progress.clear();
        const bool measure_memfp = parameters().m_memfp;
        const bool measure_physical_memory = parameters().m_memfp_physical;
        const bool report_memfp = parameters().m_report_memory_footprint;

        chrono::steady_clock::time_point now = chrono::steady_clock::now();
        chrono::steady_clock::time_point last_memory_footprint_recording = now;
        chrono::steady_clock::time_point timeout =
                now + (m_parameters.m_timeout == 0s ? /* 1 month */ 31 * 24h : m_parameters.m_timeout);

        do {
            auto tp = now + 1s;

            done = true;
            uint64_t i = 0, num_workers = m_workers.size();
            while (done && i < num_workers) {
                done &= m_workers[i]->wait(tp);
                i++;
            }
            now = tp;

            if (!done) {
                m_results.m_progress.push_back(num_operations_sofar());

                if (measure_memfp && (/* first tick */ (m_results.m_progress.size() == 1) ||
                                                       tp - last_memory_footprint_recording >= 10s)) {
                    uint64_t tick = m_results.m_progress.size(); // 1, 10, 20, 30, 40, 50, 60, ...
                    uint64_t memfp_process = measure_physical_memory ? common::get_memory_footprint() : max<int64_t>(
                            utility::MemoryUsage::memory_footprint(), 0);
                    uint64_t memfp_driver = memory_footprint();
                    Aging2Result::MemoryFootprint memfp{tick, memfp_process, memfp_driver, /* cool off ? */ false};
                    m_results.m_memory_footprint.push_back(memfp);;
                    if (report_memfp) {
                        LOG("Memory footprint after " << DurationQuantity(tick) << ": "
                                                      << ComputerQuantity(memfp_process - memfp_driver,
                                                                          true));
                    }
                    if (m_results.m_progress.size() >
                        1) { last_memory_footprint_recording = tp; } // beyond the first tick

                    if (parameters().m_memfp_threshold > 0 && memfp_process >= parameters().m_memfp_threshold) {
                        m_stop_experiment = true;
                        LOG("MEMORY THRESHOLD PASSED ( " << ComputerQuantity(parameters().m_memfp_threshold, true)
                                                         << " ). Terminating the experiment ...");
                        m_stop_reason = StopReason::MEMORY_FOOTPRINT;
                    }
                }

                if (now >= timeout) {
                    m_stop_experiment = true;
                    LOG("TIMEOUT HIT, Terminating the experiment ... ");
                    m_stop_reason = StopReason::TIMEOUT_HIT;
                }
            }
        } while (!done && !m_stop_experiment);

        if (m_stop_experiment) { // wait the workers to terminate
            for (auto i = 0; i < m_workers.size(); i++) {
                auto &w = m_workers[i];
                auto until = chrono::steady_clock::now() + 5 * 60s;
                auto idle = w->wait(until);
                if (!idle) {
                    cerr << "Worker " << i << " not idle and it is " << w->is_in_library_code() << " library code."
                         << endl;
                    m_results.m_thread_deadlocked = true;
                    m_results.m_in_library_code = w->is_in_library_code();
                }
            }
        }
    }

    void Aging2Master::cooloff(std::chrono::steady_clock::time_point start) {
        if (m_parameters.m_cooloff.count() == 0) return; // nothing to do
        const bool report_memfp = parameters().m_report_memory_footprint;
        const bool measure_memfp = parameters().m_memfp;
        const bool measure_physical_memory = parameters().m_memfp_physical;

        LOG("[Aging2] Cool-off period of " << m_parameters.m_cooloff.count() << " seconds ... ");
        auto now = chrono::steady_clock::now();
        const auto end = now + m_parameters.m_cooloff;
        chrono::seconds next_tick{chrono::duration_cast<chrono::seconds>(now - start) + 1s};
        do {
            this_thread::sleep_until(start + next_tick);
            uint64_t memfp_process = measure_physical_memory ? common::get_memory_footprint() : max<int64_t>(
                    utility::MemoryUsage::memory_footprint(), 0);
            uint64_t memfp_driver = memory_footprint();
            Aging2Result::MemoryFootprint memfp{static_cast<uint64_t>(next_tick.count()), memfp_process,
                                                memfp_driver, /* cool off ? */ true};
            if (measure_memfp) { m_results.m_memory_footprint.push_back(memfp); }
            if (report_memfp) {
                LOG(
                        "Memory footprint (cool-off) after " << DurationQuantity((uint64_t) next_tick.count()) << ": "
                                                             << ComputerQuantity(memfp_process - memfp_driver, true));
            }
            next_tick += 1s;

            now = chrono::steady_clock::now();
        } while (now < end);

        LOG("[Aging2] Cool-off period terminated");
    }

    void Aging2Master::store_results() {
        m_results.m_num_vertices_final_graph = parameters().m_library->num_vertices();
        m_results.m_num_edges_final_graph = parameters().m_library->num_edges();
        m_results.m_reported_times.reserve(m_last_time_reported);
        for (size_t i = 0, sz = m_last_time_reported; i < sz; i++) {
            m_results.m_reported_times.push_back(m_reported_times[i]);
        }

        if (parameters().m_measure_latency) {
            assert(m_latencies != nullptr);
            LOG("[Aging2] Computing the statistics for the measured latencies ...");
            Timer timer;
            timer.start();

            m_results.m_latency_stats.reset(new LatencyStatistics[3]);
            m_results.m_latency_stats[0] = LatencyStatistics::compute_statistics(m_latencies,
                                                                                 m_latencies_num_insertions); // insertions
            m_results.m_latency_stats[1] = LatencyStatistics::compute_statistics(
                    m_latencies + m_latencies_num_insertions, m_latencies_num_deletions); // deletions
            m_results.m_latency_stats[2] = LatencyStatistics::compute_statistics(m_latencies,
                                                                                 m_latencies_num_insertions +
                                                                                 m_latencies_num_deletions); // both insertions & deletions

            timer.stop();
            LOG("[Aging2] Statistics computed in " << timer);
            LOG("[Aging2] Average latency of updates: " << DurationQuantity(m_results.m_latency_stats[2].mean())
                                                        << ", 99th percentile: " << DurationQuantity(
                    m_results.m_latency_stats[2].percentile99()));

            delete[] m_latencies;
            m_latencies = nullptr; // free some memory
        }

        m_results.m_timeout_hit = (m_stop_reason == StopReason::TIMEOUT_HIT);
        m_results.m_memfp_threshold_passed = (m_stop_reason == StopReason::MEMORY_FOOTPRINT);
    }

    void Aging2Master::log_num_vtx_edges() {
        scoped_lock<mutex> lock(_log_mutex);
        cout << "[Aging2] Number of stored vertices: " << m_results.m_num_vertices_final_graph << " [match: ";
        if (m_results.m_num_vertices_load == m_results.m_num_vertices_final_graph) { cout << "yes"; }
        else {
            cout << "no, expected " << m_results.m_num_vertices_load;
        }
        cout << "], number of stored edges: " << m_results.m_num_edges_final_graph << " [match: ";
        if (m_results.m_num_edges_load == m_results.m_num_edges_final_graph) { cout << "yes"; }
        else {
            cout << "no, expected " << m_results.m_num_edges_load;
        }
        cout << "]" << endl;
    }

    void Aging2Master::set_random_vertex_id(uint64_t *edges, uint64_t num_edges) {
        uint64_t *__restrict sources = edges;
        uint64_t *__restrict destinations = sources + num_edges;
        double *__restrict weights = reinterpret_cast<double *>(destinations + num_edges);

        uint64_t i = 0;
        while (i < num_edges && weights[i] <= 0) i++;

        if (i < num_edges)
            m_results.m_random_vertex_id = sources[i];
    }

    uint64_t Aging2Master::memory_footprint() const {
        uint64_t result = 0;
        // workers
        for (auto &w: m_workers) { result += w->memory_footprint(); }

        // size of the internal vectors
        if (parameters().m_memfp_physical) { // physical memory
            //result += sizeof(uint64_t) * static_cast<uint64_t>( m_parameters.m_num_reports_per_operations * ::ceil( static_cast<double>(num_operations_total())/num_edges_final_graph()) + 1 );
            result += m_results.m_progress.size() * sizeof(m_results.m_progress[0]);
            result += m_results.m_memory_footprint.size() * sizeof(m_results.m_memory_footprint[0]);
            if (m_latencies != nullptr) {
                result += sizeof(uint64_t) * m_results.m_num_operations_total;
            }
        } else { // virtual memory
            result += utility::MemoryUsage::get_allocated_space(m_results.m_progress.data());
            result += utility::MemoryUsage::get_allocated_space(m_results.m_memory_footprint.data());
            if (m_latencies != nullptr) {
                result += utility::MemoryUsage::get_allocated_space(m_latencies);
            }
        }

        return result;
    }

    double Aging2Master::progress_so_far() const {
        if (m_experiment_running) {
            return (double) num_operations_sofar() / (double) num_operations_total();
        } else {
            return 0.0;
        }
    }

    Aging2Result Aging2Master::execute_synchronized(uint64_t synchronization_point) {
        uint64_t iterations = 100 / synchronization_point;

        //setup loader:
        uint64_t read_log_num = 0;
        uint64_t total_log_num = 2603795200;//for graph500's 10 hour log
        fstream handle(m_parameters.m_path_log, ios_base::in | ios_base::binary);
        auto properties = reader::graphlog::parse_properties(handle);
        uint64_t array_sz = stoull(properties["internal.edges.block_size"]);
        reader::graphlog::set_marker(properties, handle, reader::graphlog::Section::EDGES);
        reader::graphlog::EdgeLoader loader(handle);
        uint64_t num_edges = 0;
        //bool print = false;
        for (uint64_t i = 0; i < iterations; i++) {
            unique_ptr<uint64_t[]> ptr_array1{new uint64_t[array_sz]};
            unique_ptr<uint64_t[]> ptr_array2{new uint64_t[array_sz]};
            uint64_t *array1 = ptr_array1.get();
            uint64_t *array2 = ptr_array2.get();
            num_edges = loader.load(array1, array_sz / 3);
            uint64_t load_iterations = 1;
            uint64_t batch_size = num_edges;
            /*if(print){
                for(uint64_t j=100; j<150; j++){
                    LOG(array1[j]<<" "<<(array1+num_edges)[j]<<" "<< (reinterpret_cast<double*>(array1+2*num_edges))[j]);
                }
            }*/
            //we manually calculated it, load 16 batches
            while (num_edges > 0) {
                // partition the batch among the workers
                for (auto w: m_workers) w->load_edges(array1, num_edges);
                if (m_results.m_random_vertex_id == 0) { set_random_vertex_id(array1, num_edges); }
                load_iterations++;
                if (load_iterations < 9) {
                    // load the next batch in the meanwhile
                    num_edges = loader.load(array2, array_sz / 3);
                    //std::cout<<num_edges<<std::endl;
                    batch_size += num_edges;
                    // wait for the workers to complete
                    for (auto w: m_workers) w->wait();
                    swap(array1, array2);
                } else {
                    for (auto w: m_workers) w->wait();
                    break;
                }
            }
            /*if(print){
                for(uint64_t x=0; x<39; x++){
                    std::cout<<"worker "<<x<<std::endl;
                    m_workers.at(x)->print_workload(50,15);
                }
            }*/
            LOG("execute edge updates of batch size " << batch_size);
            if (parameters().m_measure_latency) prepare_latencies();
            do_run_experiment();
            //print = true;
            // for (auto w: m_workers) w->load_edges(array1, num_edges);
            //store_results();
            //log_num_vtx_edges();
        }
        handle.close();
        return m_results;
    }



    Aging2Result Aging2Master::execute_synchronized_evenly_partition(uint64_t synchronization_point) {
        uint64_t num_workers = m_workers.size();
        uint64_t iterations = 100 / synchronization_point;

        //setup loader:
        uint64_t read_log_num = 0;
        uint64_t total_log_num = 2603795200;//for graph500's 10 hour log
        fstream handle(m_parameters.m_path_log, ios_base::in | ios_base::binary);
        auto properties = reader::graphlog::parse_properties(handle);
        uint64_t array_sz = stoull(properties["internal.edges.block_size"]);
        reader::graphlog::set_marker(properties, handle, reader::graphlog::Section::EDGES);
        reader::graphlog::EdgeLoader loader(handle);
        uint64_t num_edges = 0;
        //bool print = false;
        for (uint64_t i = 0; i < iterations; i++) {
            unique_ptr<uint64_t[]> ptr_array1{new uint64_t[array_sz]};
            unique_ptr<uint64_t[]> ptr_array2{new uint64_t[array_sz]};
            uint64_t *array1 = ptr_array1.get();
            uint64_t *array2 = ptr_array2.get();
            num_edges = loader.load(array1, array_sz / 3);
            std::vector<bool> clock_even_distribution;
            for (auto w: m_workers) {
                clock_even_distribution.emplace_back(false);
            }
            uint64_t offset = 0;
            std::unordered_map<std::pair<uint64_t, uint64_t>, uint64_t, hash_edge> edge_to_worker_map;
            uint64_t load_iterations = 1;
            uint64_t batch_size = num_edges;
            /*if(print){
                for(uint64_t j=100; j<150; j++){
                    LOG(array1[j]<<" "<<(array1+num_edges)[j]<<" "<< (reinterpret_cast<double*>(array1+2*num_edges))[j]);
                }
            }*/
            //we manually calculated it, load 16 batches
            while (num_edges > 0) {
                // partition the batch among the workers by the master
                //array1 has tons of data, partition them into buckets
                for (uint64_t j = 0; j < num_edges; j++) {
                    std::pair<uint64_t, uint64_t> tem_edge(array1[j], (array1 +
                                                                       num_edges)[j]);//array1[j]<<" "<<(array1+num_edges)[j]<<" "<< (reinterpret_cast<double*>(array1+2*num_edges))[j]
                    double weight = (reinterpret_cast<double *>(array1 + 2 * num_edges))[j];
                    auto emplace_result = edge_to_worker_map.try_emplace(tem_edge, 0);
                    //if the edge does not have an owner yet
                    if (emplace_result.second) {
                        bool continue_clock = true;
                        while (continue_clock) {
                            if (!clock_even_distribution.at(offset)) {
                                clock_even_distribution.at(offset) = true;
                                emplace_result.first->second = offset;
                                continue_clock = false;
                                m_workers.at(offset)->load_edge(tem_edge.first, tem_edge.second, weight);
                            } else {
                                clock_even_distribution.at(offset) = false;
                            }
                            offset = (offset + 1) % num_workers;
                        }
                    } else {
                        //if it has an owner already
                        m_workers.at(emplace_result.first->second)->load_edge(tem_edge.first, tem_edge.second, weight);
                        clock_even_distribution.at(emplace_result.first->second) = true;
                    }
                }
                if (m_results.m_random_vertex_id == 0) { set_random_vertex_id(array1, num_edges); }
                load_iterations++;
                if (load_iterations < 9) {
                    // load the next batch in the meanwhile
                    num_edges = loader.load(array2, array_sz / 3);
                    //std::cout<<num_edges<<std::endl;
                    batch_size += num_edges;
                    // wait for the workers to complete
                    swap(array1, array2);
                } else {
                    break;
                }
            }
            /*if(print){
                for(uint64_t x=0; x<39; x++){
                    std::cout<<"worker "<<x<<std::endl;
                    m_workers.at(x)->print_workload(50,15);
                }
            }*/
            LOG("execute edge updates of batch size " << batch_size);
            if (parameters().m_measure_latency) prepare_latencies();
            do_run_experiment();
            //print = true;
            // for (auto w: m_workers) w->load_edges(array1, num_edges);
            //store_results();
            //log_num_vtx_edges();
        }
        handle.close();
        return m_results;
    }

    Aging2Result Aging2Master::execute_synchronized_small_batch() {
        //setup loader:
        uint64_t read_log_num = 0;
        uint64_t total_log_num = 2603795200;//for graph500's 10 hour log
        fstream handle(m_parameters.m_path_log, ios_base::in | ios_base::binary);
        auto properties = reader::graphlog::parse_properties(handle);
        uint64_t array_sz = stoull(properties["internal.edges.block_size"]);
        reader::graphlog::set_marker(properties, handle, reader::graphlog::Section::EDGES);
        reader::graphlog::EdgeLoader loader(handle);
        uint64_t num_edges = 0;
        //bool print = false;
        unique_ptr<uint64_t[]> ptr_array1{new uint64_t[array_sz]};
        unique_ptr<uint64_t[]> ptr_array2{new uint64_t[array_sz]};
        uint64_t *array1 = ptr_array1.get();
        uint64_t *array2 = ptr_array2.get();
        num_edges = loader.load(array1, array_sz / 3);
        /*if(print){
            for(uint64_t j=100; j<150; j++){
                LOG(array1[j]<<" "<<(array1+num_edges)[j]<<" "<< (reinterpret_cast<double*>(array1+2*num_edges))[j]);
            }
        }*/
        //we manually calculated it, load 16 batches
#if HAVE_LIVEGRAPH
        uint64_t executed_operations = 0;
        uint64_t total_log = 2603795200;
#endif
        while (num_edges > 0) {
#if HAVE_LIVEGRAPH
            executed_operations+=num_edges;
#endif
            // partition the batch among the workers
            for (auto w: m_workers) w->load_edges(array1, num_edges);
            if (m_results.m_random_vertex_id == 0) { set_random_vertex_id(array1, num_edges); }
           // LOG("execute edge updates of batch size " << num_edges);
            // load the next batch in the meanwhile
            num_edges = loader.load(array2, array_sz / 3);
            //std::cout<<num_edges<<std::endl;
            // wait for the workers to complete
            for (auto w: m_workers) w->wait();
            //for (auto w: m_workers) w->print_workload(0,num_edges);
            swap(array1, array2);
            if (parameters().m_measure_latency) prepare_latencies();
            do_run_experiment();
#if HAVE_LIVEGRAPH
            if(executed_operations>(2603795200/5)){
                break;
            }
#endif
        }
        /*if(print){
            for(uint64_t x=0; x<39; x++){
                std::cout<<"worker "<<x<<std::endl;
                m_workers.at(x)->print_workload(50,15);
            }
        }*/

        //print = true;
        // for (auto w: m_workers) w->load_edges(array1, num_edges);
        //store_results();
        //log_num_vtx_edges();

        handle.close();
#if HAVE_LIVEGRAPH
            LOG("LiveGraph executed "<<executed_operations<<" operations");
#endif
        LOG("total execution time is "<<total_time_microseconds<<" us");
        return m_results;
    }

    Aging2Result Aging2Master::execute_synchronized_small_batch_even_partition() {
        //setup loader:
        uint64_t read_log_num = 0;
        uint64_t total_log_num = 2603795200;//for graph500's 10 hour log
        uint64_t num_workers = m_workers.size();
        fstream handle(m_parameters.m_path_log, ios_base::in | ios_base::binary);
        auto properties = reader::graphlog::parse_properties(handle);
        uint64_t array_sz = stoull(properties["internal.edges.block_size"]);
        reader::graphlog::set_marker(properties, handle, reader::graphlog::Section::EDGES);
        reader::graphlog::EdgeLoader loader(handle);
        uint64_t num_edges = 0;
        //bool print = false;
        unique_ptr<uint64_t[]> ptr_array1{new uint64_t[array_sz]};
        unique_ptr<uint64_t[]> ptr_array2{new uint64_t[array_sz]};
        uint64_t *array1 = ptr_array1.get();
        uint64_t *array2 = ptr_array2.get();
        num_edges = loader.load(array1, array_sz / 3);
        /*if(print){
            for(uint64_t j=100; j<150; j++){
                LOG(array1[j]<<" "<<(array1+num_edges)[j]<<" "<< (reinterpret_cast<double*>(array1+2*num_edges))[j]);
            }
        }*/
        //we manually calculated it, load 16 batches
        while (num_edges > 0) {
            uint64_t offset = 0;
            std::unordered_map<std::pair<uint64_t, uint64_t>, uint64_t, hash_edge> edge_to_worker_map;
            std::vector<bool> clock_even_distribution;
            for (auto w: m_workers) {
                clock_even_distribution.emplace_back(false);
            }
            // partition the batch among the workers
            for (uint64_t j = 0; j < num_edges; j++) {
                std::pair<uint64_t, uint64_t> tem_edge(array1[j], (array1 +
                                                                   num_edges)[j]);//array1[j]<<" "<<(array1+num_edges)[j]<<" "<< (reinterpret_cast<double*>(array1+2*num_edges))[j]
                double weight = (reinterpret_cast<double *>(array1 + 2 * num_edges))[j];
                auto emplace_result = edge_to_worker_map.try_emplace(tem_edge, 0);
                //if the edge does not have an owner yet
                if (emplace_result.second) {
                    bool continue_clock = true;
                    while (continue_clock) {
                        if (!clock_even_distribution.at(offset)) {
                            clock_even_distribution.at(offset) = true;
                            emplace_result.first->second = offset;
                            continue_clock = false;
                            m_workers.at(offset)->load_edge(tem_edge.first, tem_edge.second, weight);
                        } else {
                            clock_even_distribution.at(offset) = false;
                        }
                        offset = (offset + 1) % num_workers;
                    }
                } else {
                    //if it has an owner already
                    m_workers.at(emplace_result.first->second)->load_edge(tem_edge.first, tem_edge.second, weight);
                    clock_even_distribution.at(emplace_result.first->second) = true;
                }
            }
            if (m_results.m_random_vertex_id == 0) { set_random_vertex_id(array1, num_edges); }
            LOG("execute edge updates of batch size " << num_edges);
            // load the next batch in the meanwhile
            num_edges = loader.load(array2, array_sz / 3);
            //std::cout<<num_edges<<std::endl;
            // wait for the workers to complete
            for (auto w: m_workers) w->wait();
            //for (auto w: m_workers) w->print_workload(0,num_edges);
            swap(array1, array2);
            if (parameters().m_measure_latency) prepare_latencies();
            do_run_experiment();
        }
        /*if(print){
            for(uint64_t x=0; x<39; x++){
                std::cout<<"worker "<<x<<std::endl;
                m_workers.at(x)->print_workload(50,15);
            }
        }*/

        //print = true;
        // for (auto w: m_workers) w->load_edges(array1, num_edges);
        //store_results();
        //log_num_vtx_edges();

        handle.close();
        LOG("total execution time is "<<total_time_microseconds);
        return m_results;
    }

    Aging2Result Aging2Master::execute_pure_update_small_batch() {
        m_workload_index.store(0,std::memory_order_release);
        fstream handle(m_parameters.m_path_log, ios_base::in | ios_base::binary);
        auto properties = reader::graphlog::parse_properties(handle);
        uint64_t array_sz = stoull(properties["internal.edges.block_size"]);
        reader::graphlog::set_marker(properties, handle, reader::graphlog::Section::EDGES);
        reader::graphlog::EdgeLoader loader(handle);
        uint64_t num_edges = 0;
        //bool print = false;
        unique_ptr<uint64_t[]> ptr_array1{new uint64_t[array_sz]};
        unique_ptr<uint64_t[]> ptr_array2{new uint64_t[array_sz]};
        uint64_t *array1 = ptr_array1.get();
        uint64_t *array2 = ptr_array2.get();
        num_edges = loader.load(array1, array_sz / 3);
        std::unordered_set<std::pair<uint64_t,uint64_t>,hash_edge>seen_edges;
        //do a simple test
    /*    for(uint64_t j=0; j<num_edges; j++){
            auto result = seen_edges.emplace(array1[j],(array1+num_edges)[j]);
            if(!result.second)
            LOG(array1[j]<<" "<<(array1+num_edges)[j]<<" "<< (reinterpret_cast<double*>(array1+2*num_edges))[j]);
            //LOG(array1[j]<<" "<<(array1+num_edges)[j]<<" "<< (reinterpret_cast<double*>(array1+2*num_edges))[j]);
        }*/
        uint64_t loop = 0;
#if HAVE_LIVEGRAPH
        uint64_t executed_operations = 0;
        uint64_t total_log = 2603795200;
#endif
        while (num_edges > 0) {
            loop++;
            if (parameters().m_measure_latency) prepare_latencies();
            for(uint64_t j=100; j<150; j++){
                LOG(array1[j]<<" "<<(array1+num_edges)[j]<<" "<< (reinterpret_cast<double*>(array1+2*num_edges))[j]);
            }
            //LOG(loop<<"th iteration execute edge updates of batch size " << num_edges);
            //do experiment
            Timer timer;
            timer.start();
            for (auto w: m_workers) w->execute_true_updates(array1,num_edges);
            m_experiment_running = true;
            wait_and_record();
            timer.stop();
            total_time_microseconds+= timer.microseconds();
           // LOG("batch finished in "<<timer.seconds()<<" seconds");
            m_experiment_running = false;
            // change to insert-like batch execution
            //if (m_results.m_random_vertex_id == 0) { set_random_vertex_id(array1, num_edges); }
            // load the next batch in the meanwhile
            num_edges = loader.load(array2, array_sz / 3);
            //std::cout<<num_edges<<std::endl;
            // wait for the workers to complete
            //for (auto w: m_workers) w->wait();
            //for (auto w: m_workers) w->print_workload(0,num_edges);
            swap(array1, array2);

            m_workload_index.store(0,std::memory_order_release);
            //do_run_experiment();
        }
        LOG("total update time is "<<total_time_microseconds<<" us");
        return m_results;
    }
} // namespace
