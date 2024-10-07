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

#include "aging2_experiment.hpp"

#include <iostream>
#include <mutex>
#include <thread>

#include "configuration.hpp"
#include "common/error.hpp"
#include "details/aging2_master.hpp"
#include "graph/edge_stream.hpp"
#include "library/interface.hpp"

using namespace std;

/*****************************************************************************
 *                                                                           *
 * Debug                                                                     *
 *                                                                           *
 *****************************************************************************/
extern mutex _log_mutex [[maybe_unused]];
//#define DEBUG
#define COUT_DEBUG_FORCE(msg) { scoped_lock<mutex> lock(_log_mutex); cout << "[Aging2Experiment::" << __FUNCTION__ << "] [" << concurrency::get_thread_id() << "] " << msg << endl; }
#if defined(DEBUG)
    #define COUT_DEBUG(msg) COUT_DEBUG_FORCE(msg)
#else
    #define COUT_DEBUG(msg)
#endif

/*****************************************************************************
 *                                                                           *
 * Aging2Experiment                                                          *
 *                                                                           *
 *****************************************************************************/

namespace gfe::experiment {

Aging2Experiment::Aging2Experiment() : m_master(nullptr) {
}

Aging2Experiment::~Aging2Experiment() {
  if (m_master != nullptr) {
    delete m_master;
    m_master = nullptr;
  }
}

void Aging2Experiment::set_library(std::shared_ptr<library::UpdateInterface> library) {
    m_library = library;
}

void Aging2Experiment::set_log(const std::string& path){
    m_path_log = path;
}

void Aging2Experiment::set_max_weight(double value){
    if(value <= 0){ INVALID_ARGUMENT("value <= 0: " << value); }
    m_max_weight = value;
}

void Aging2Experiment::set_parallelism_degree(uint64_t num_threads){
    if(num_threads < 1){ INVALID_ARGUMENT("num_threads < 1: " << num_threads); }
    m_num_threads = num_threads;
}

void Aging2Experiment::set_build_frequency(std::chrono::milliseconds millisecs){
    m_build_frequency = millisecs;
}

void Aging2Experiment::set_release_memory(bool value){
    m_release_driver_memory = value;
}

void Aging2Experiment::set_report_progress(bool value){
    m_report_progress = value;
}

void Aging2Experiment::set_report_memory_footprint(bool value){
    m_report_memory_footprint = value;
}

void Aging2Experiment::set_num_reports_per_ops(uint64_t value){
    if(value <= 0){ INVALID_ARGUMENT("The value specified must be >= 1, given: " << value); }
    m_num_reports_per_operations = value;
}

void Aging2Experiment::set_measure_latency(bool value){
    m_measure_latency = value;
}

void Aging2Experiment::set_timeout(std::chrono::seconds secs){
    m_timeout = secs;
}

void Aging2Experiment::set_worker_granularity(uint64_t value){
    if(value < 1){ INVALID_ARGUMENT("value < 1: " << value); }
    m_worker_granularity = value;
}

void Aging2Experiment::set_cooloff(std::chrono::seconds secs){
    m_cooloff = secs;
}

void Aging2Experiment::set_memfp(bool value){
    m_memfp = value;
}

void Aging2Experiment::set_memfp_physical(bool value){
    m_memfp_physical = value;
}

void Aging2Experiment::set_memfp_threshold(uint64_t value) {
    m_memfp_threshold = value;
}

Aging2Result Aging2Experiment::execute(){
    if(m_library.get() == nullptr) ERROR("Library not set. Use #set_library to set it.");
    if(m_path_log.empty()) ERROR("Path to the log file not set. Use #set_log to set it.")
#if HAVE_GTX
   // m_library.get()->set_worker_thread_num(m_num_threads);
#endif
    m_master = new details::Aging2Master(*this);
    //auto result = m_master->execute();
    //auto result = m_master->execute_synchronized(5);
    auto result = m_master->execute_synchronized_small_batch();
    //auto result = m_master->execute_pure_update_small_batch();
    //auto result = m_master->execute_synchronized_small_batch_even_partition();
    //auto result = m_master->execute_synchronized_evenly_partition(5);
    // Master should be deleted here to ensure the same thread that called the constructor it also calls the destructor
    // So, the on_thread_init matches the on_thread_destroy.
    delete m_master;
    m_master = nullptr;
    return result;
}

double Aging2Experiment::progress_so_far() {
  if (m_master == nullptr) {
    return 0.0;
  } else {
    return m_master->progress_so_far();
  }
}
} // namespace
