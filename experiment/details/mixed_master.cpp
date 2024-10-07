//
// Created by zhou822 on 8/6/23.
//
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>

#include "common/quantity.hpp"
#include "common/system.hpp"
#include "common/timer.hpp"
#include "mixed_master.hpp"
#include "experiment/update_short_reads_experiment.hpp"
#include "experiment/mixed_workload_result.hpp"
#include "reader/graphlog_reader.hpp"
#include "library/interface.hpp"
#include "utility/memory_usage.hpp"
#include "build_thread.hpp"
#include "configuration.hpp"
#include "latency.hpp"

using namespace common;
using namespace std;

extern mutex _log_mutex [[maybe_unused]];
//#define DEBUG

#define COUT_DEBUG_FORCE(msg) { scoped_lock<mutex> lock(_log_mutex); cout << "[MixedMaster::" << __FUNCTION__ << "] [" << concurrency::get_thread_id() << "] " << msg << endl; }
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
    UpdateShortReadsMaster::UpdateShortReadsMaster(const gfe::experiment::UpdatesShortReadsExperiment &parameters):
    m_parameters(parameters),
    m_is_directed(m_parameters.m_library->is_directed()),
    m_results(parameters) {
        auto properties = reader::graphlog::parse_properties(parameters.m_path_log);
        m_results.m_num_artificial_vertices = stoull(properties["internal.vertices.temporary.cardinality"]);
        m_results.m_num_vertices_load = stoull(properties["internal.vertices.final.cardinality"]);
        m_results.m_num_edges_load = stoull(properties["internal.edges.final"]);
        m_results.m_num_operations_total = stoull(properties["internal.edges.cardinality"]);
        // 1024 is a hack to avoid issues with small graphs
        m_reported_times = new uint64_t[static_cast<uint64_t>( m_parameters.m_num_reports_per_operations * ::ceil( static_cast<double>(num_operations_total())/num_edges_final_graph()) + 1 )]();
        m_parameters.m_library->on_main_init(m_parameters.m_num_threads + /* this + builder service */ 2 + /* plus potentially an analytics runner (mixed epxeriment) */ 1);

        init_workers();
        m_parameters.m_library->on_thread_init(m_parameters.m_num_threads + 1);
    }
    UpdateShortReadsMaster::~UpdateShortReadsMaster() {
        for(auto w : update_workers){
            delete w;
        }
        for(auto w: read_workers){
            delete w;
        }
        update_workers.clear();
        read_workers.clear();
        m_parameters.m_library->on_thread_destroy(m_parameters.m_num_threads + 1);
        m_parameters.m_library->on_main_destroy();

        delete[] m_reported_times; m_reported_times = nullptr;

        delete[] m_latencies; m_latencies = nullptr;
    }
}//namespace