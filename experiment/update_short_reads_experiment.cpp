#include "update_short_reads_experiment.hpp"
#include <iostream>
#include <mutex>
#include <thread>

#include "configuration.hpp"
#include "common/error.hpp"
//#include "details/aging2_master.hpp"
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

namespace gfe::experiment {

UpdatesShortReadsExperiment::UpdatesShortReadsExperiment():m_master(nullptr) {
}

UpdatesShortReadsExperiment::~UpdatesShortReadsExperiment() {
    if (m_master != nullptr) {
        delete m_master;
        m_master = nullptr;
    }
}

void UpdatesShortReadsExperiment::set_library(std::shared_ptr<gfe::library::UpdateInterface> library) {
    m_library = library;
}

void UpdatesShortReadsExperiment::set_log(const std::string& path){
    m_path_log = path;
}

void UpdatesShortReadsExperiment::set_max_weight(double value){
    if(value <= 0){ INVALID_ARGUMENT("value <= 0: " << value); }
    m_max_weight = value;
}

void UpdatesShortReadsExperiment::set_parallelism_degree(uint64_t num_threads) {
    if(num_threads < 1){ INVALID_ARGUMENT("num_threads < 1: " << num_threads); }
    m_num_threads = num_threads;
}

void UpdatesShortReadsExperiment::set_build_frequency(std::chrono::milliseconds millisecs){
    m_build_frequency = millisecs;
}

void UpdatesShortReadsExperiment::set_release_memory(bool value){
    m_release_driver_memory = value;
}

void UpdatesShortReadsExperiment::set_report_progress(bool value){
    m_report_progress = value;
}

void UpdatesShortReadsExperiment::set_report_memory_footprint(bool value) {
    m_report_memory_footprint = value;
}

void UpdatesShortReadsExperiment::set_num_reports_per_ops(uint64_t value){
    if(value <= 0){ INVALID_ARGUMENT("The value specified must be >= 1, given: " << value); }
    m_num_reports_per_operations = value;
}

void UpdatesShortReadsExperiment::set_measure_latency(bool value){
    m_measure_latency = value;
}

void UpdatesShortReadsExperiment::set_timeout(std::chrono::seconds secs){
    m_timeout = secs;
}

void UpdatesShortReadsExperiment::set_worker_granularity(uint64_t value){
    if(value < 1){ INVALID_ARGUMENT("value < 1: " << value); }
    m_worker_granularity = value;
}

void UpdatesShortReadsExperiment::set_cooloff(std::chrono::seconds secs){
    m_cooloff = secs;
}

void UpdatesShortReadsExperiment::set_memfp(bool value){
    m_memfp = value;
}

void UpdatesShortReadsExperiment::set_memfp_physical(bool value){
    m_memfp_physical = value;
}

void UpdatesShortReadsExperiment::set_memfp_threshold(uint64_t value) {
    m_memfp_threshold = value;
}

UpdatesReadsMixedWorkloadResult UpdatesShortReadsExperiment::execute(){
    if(m_library.get() == nullptr) ERROR("Library not set. Use #set_library to set it.");
    if(m_path_log.empty()) ERROR("Path to the log file not set. Use #set_log to set it.")
    m_master = new details::Aging2Master(*this);
    auto result = m_master->execute();

    // Master should be deleted here to ensure the same thread that called the constructor it also calls the destructor
    // So, the on_thread_init matches the on_thread_destroy.
    delete m_master;
    m_master = nullptr;
    return result;
}

double UpdatesShortReadsExperiment::progress_so_far() {
    if (m_master == nullptr) {
        return 0.0;
    } else {
        return m_master->progress_so_far();
    }
}
}//namespace