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

#include "configuration.hpp"

#include <algorithm>
#include <cctype> // tolower
#include <cmath>
#include <cstdlib>
#include <omp.h>
#include <random>
#include <sstream>
#include <string>
#include <unistd.h> // sysconf

#include "common/cpu_topology.hpp"
#include "common/database.hpp"
#include "common/filesystem.hpp"
#include "common/quantity.hpp"
#include "common/system.hpp"
#include "experiment/graphalytics.hpp"
#include "library/interface.hpp"
#include "reader/graphlog_reader.hpp"
#include "third-party/cxxopts/cxxopts.hpp"

using namespace common;
using namespace std;

#undef CURRENT_ERROR_TYPE
#define CURRENT_ERROR_TYPE ::gfe::ConfigurationError

namespace gfe {

/*****************************************************************************
 *                                                                           *
 *  Helpers                                                                  *
 *                                                                           *
 *****************************************************************************/
mutex _log_mutex;

static string libraries_help_screen(){
    auto libs = library::implementations();
    sort(begin(libs), end(libs), [](const auto& lib1, const auto& lib2){
       return lib1.m_name < lib2.m_name;
    });

    stringstream stream;
    stream << "The library to evaluate: ";
    bool first = true;
    for(const auto& l : libs){
        if (first) first = false; else stream << ", ";
        stream << l.m_name;
    }

    return stream.str();
}

/*****************************************************************************
 *                                                                           *
 *  Singleton                                                                *
 *                                                                           *
 *****************************************************************************/
static Configuration g_configuration;
Configuration& configuration(){ return g_configuration; }

/*****************************************************************************
 *                                                                           *
 *  Initialisation                                                           *
 *                                                                           *
 *****************************************************************************/
Configuration::Configuration(){

}

Configuration::~Configuration(){
    delete m_database; m_database = nullptr;
}

void Configuration::initialise(int argc, char* argv[]){
    using namespace cxxopts;

    Options options(argv[0], "GFE Driver");

    options.add_options("Generic")
        ("aging_cooloff", "The amount of time to wait idle after the simulation completed in the Aging2 experiment. The purpose is to measure the memory footprint of the test library when no updates are being executed", value<DurationQuantity>())
        ("aging_memfp", "Whether to measure the memory footprint", value<bool>()->default_value("false"))
        ("aging_memfp_physical", "Whether to consider the virtual or the physical memory in the memory footprint", value<bool>()->default_value("false"))
        ("aging_memfp_report", "Whether to log to stdout the memory footprint measurements observed", value<bool>()->default_value("false"))
        ("aging_memfp_threshold", "Forcedly stop the execution of the aging experiment if the memory footprint of the whole process is above this threshold", value<ComputerQuantity>())
        ("aging_release_memory", "Whether to release the memory from the driver as the experiment proceeds", value<bool>()->default_value("true"))
        ("aging_step_size", "The step of each recording for the measured progress in the Aging2 experiment. Valid values are 0.1, 0.25, 0.5 and 1.0", value<double>()->default_value("1"))
        ("aging_timeout", "Force terminating the aging experiment after the given amount of time (excl. cool-off time)", value<DurationQuantity>())
        ("blacklist", "Comma separated list of graph algorithms to blacklist and do not execute", value<string>())
        ("build_frequency", "The frequency to build a new snapshot in the aging experiment (default: disabled)", value<DurationQuantity>())
        ("d, database", "Store the current configuration value into the a sqlite3 database at the given location", value<string>())
        ("efe", "Expansion factor for the edges in the graph", value<double>()->default_value(to_string(get_ef_edges())))
        ("efv", "Expansion factor for the vertices in the graph", value<double>()->default_value(to_string(get_ef_vertices())))
        ("G, graph", "The path to the graph to load", value<string>())
        ("h, help", "Show this help menu")
        ("latency", "Measure the latency of inserts/updates, report the average, median, std. dev. and 90/95/97/99 percentiles")
        ("l, library", libraries_help_screen(), value<string>())
        ("load", "Load the graph into the library in one go")
        ("log", "Repeat the log of updates specified in the given file", value<string>())
        ("max_weight", "The maximum weight that can be assigned when reading non weighted graphs", value<double>()->default_value(to_string(max_weight())))
        ("omp", "Maximum number of threads that can be used by OpenMP (0 = do not change)", value<int>()->default_value(to_string(num_threads_omp())))
        ("R, repetitions", "The number of repetitions of the same experiment (where applicable)", value<uint64_t>()->default_value(to_string(num_repetitions())))
        ("r, readers", "The number of client threads to use for the read operations", value<int>()->default_value(to_string(num_threads(THREADS_READ))))
        ("seed", "Random seed used in various places in the experiments", value<uint64_t>()->default_value(to_string(seed())))
        ("t, threads", "The number of threads to use for both the read and write operations", value<int>()->default_value(to_string(num_threads(THREADS_TOTAL))))
        ("timeout", "Set the maximum time for an operation to complete, in seconds", value<uint64_t>()->default_value(to_string(get_timeout_graphalytics())))
        ("u, undirected", "Is the graph undirected? By default, it's considered directed.")
        ("v, validate", "Whether to validate the output results of the Graphalytics algorithms", value<string>()->implicit_value("<path>"))
        ("w, writers", "The number of client threads to use for the write operations", value<int>()->default_value(to_string(num_threads(THREADS_WRITE))))
        ("b, block_size", "The block size for Sortledton to use.", value<int>()->default_value("1024"))
        ("m, mixed_workload", "If set run updates and analytics concurrently.", value<bool>()->default_value("false"))
        ("is_timestamped", "If the graph log is sorted by external timestamps and should not be shuffled.", value<bool>()->default_value("false"))
    ;

    try {
        auto result = options.parse(argc, argv);

        if(result.count("help") > 0){
            cout << options.help({"Generic"}) << endl;
            ::exit(EXIT_SUCCESS);
        }

        if(result["log"].count() > 0){
            m_update_log = result["log"].as<string>();
            if(!common::filesystem::exists(m_update_log)){ ERROR("Option --log \"" << m_update_log << "\", the file does not exist"); }

            // verify that the properties from the log file are not also specified via command line
            if(result["efe"].count() > 0) { ERROR("Cannot specify the option --efe together with the log file"); }
            if(result["efv"].count() > 0) { ERROR("Cannot specify the option --efv together with the log file"); }
            if(result["max_weight"].count() > 0) { ERROR("Cannot specify the option --max_weight together with the log file"); }

            // read the properties from the log file
            auto log_properties = reader::graphlog::parse_properties(m_update_log);
            if(log_properties.find("aging_coeff") == log_properties.end()) { ERROR("The log file `" << m_update_log << "' does not contain the expected property 'aging_coeff'"); }
            set_coeff_aging(stod(log_properties["aging_coeff"]));
            if(log_properties.find("ef_edges") == log_properties.end()) { ERROR("The log file `" << m_update_log << "' does not contain the expected property 'ef_edges'"); }
            set_ef_edges(stod(log_properties["ef_edges"]));
            if(log_properties.find("ef_vertices") == log_properties.end()) { ERROR("The log file `" << m_update_log << "' does not contain the expected property 'ef_vertices'"); }
            set_ef_vertices(stod(log_properties["ef_vertices"]));
            if(log_properties.find("max_weight") != log_properties.end()){ // otherwise assume the default
                set_max_weight(stod(log_properties["max_weight"]));
            }

            // validate that the graph in the input log file matches the same graph specified in the command line
            auto it_path_graph = log_properties.find("input_graph");
            if(it_path_graph != log_properties.end() && result["graph"].count() > 0){
                auto name_log = common::filesystem::filename(it_path_graph->second);
                auto name_param = common::filesystem::filename(common::filesystem::absolute_path(result["graph"].as<string>()));
                if(name_log != name_param){
                    ERROR("The log file is based on the graph `" << name_log << "', while the parameter --graph refers to `" << name_param << "'");
                }
            }
        }

        set_seed( result["seed"].as<uint64_t>() );

        if( result["max_weight"].count() > 0)
            set_max_weight( result["max_weight"].as<double>() );

        if( result["database"].count() > 0 ){ set_database_path( result["database"].as<string>() ); }

        // number of threads
        if( result["threads"].count() > 0) {
            int value = result["threads"].as<int>();

            set_num_threads_write(value);
        }
        if( result["readers"].count() > 0) {
            set_num_threads_read( result["readers"].as<int>() );
        }
        if( result["writers"].count() > 0 ){
            set_num_threads_write( result["writers"].as<int>() );
        }

        // the graph to work with
        if( result["graph"].count() > 0 ){
            set_graph( result["graph"].as<string>() );
        }

        set_num_repetitions( result["repetitions"].as<uint64_t>() );

        set_timeout_graphalytics( result["timeout"].as<uint64_t>() );

        if( result["efe"].count() > 0 )
            set_ef_edges( result["efe"].as<double>() );

        if( result["efv"].count() > 0 )
            set_ef_vertices( result["efv"].as<double>() );

        if( result["build_frequency"].count() > 0 ){
            set_build_frequency( result["build_frequency"].as<DurationQuantity>().as<chrono::milliseconds>().count() );
        }

        // library to evaluate
        if( result["library"].count() == 0 ){
            ERROR("Missing mandatory argument --library. Which library do you want to evaluate??");
        } else {
            string library_name = result["library"].as<string>();
            transform(begin(library_name), end(library_name), begin(library_name), ::tolower); // make it lower case
            auto libs = library::implementations();
            auto library_found = find_if(begin(libs), end(libs), [&library_name](const auto& candidate){
                return library_name == candidate.m_name;
            });
            if(library_found == end(libs)){ ERROR("Library not recognised: `" << result["library"].as<string>() << "'"); }
            m_library_name = library_found->m_name;
            m_library_factory = library_found->m_factory;
        }

        if( result["load"].count() > 0 ){
            set_load(true);
        }

        if( result["undirected"].count() > 0 ){
            m_graph_directed = false;
        }

        if( result["validate"].count() > 0 ){
            //m_validate_inserts = true;
            m_validate_output = true;

            string path_validate_graph = result["validate"].as<string>();
            if(path_validate_graph !=  "<path>" /* implicit */){
                if(!common::filesystem::exists(path_validate_graph)){ ERROR("Option --validate=\"" << path_validate_graph << "\", the file does not exist."); }

                auto abs_path_validate = common::filesystem::absolute_path(path_validate_graph);
                auto abs_path_graph = common::filesystem::absolute_path(result["graph"].as<string>());
                if(abs_path_validate != abs_path_graph){
                    m_validate_graph = path_validate_graph;
                }
            }
        }

        if ( result["omp"].count() > 0 ){
            set_num_threads_omp( result["omp"].as<int>() );
        }

        if ( result["aging_step_size"].count() > 0 ){
            set_aging_step_size( result["aging_step_size"].as<double>() );
        }

        if( result["aging_cooloff"].count() > 0){
            set_aging_cooloff_seconds( result["aging_cooloff"].as<DurationQuantity>().as<chrono::seconds>().count() );
        }

        if(result.count("aging_memfp") > 0){
            m_aging_memfp = result["aging_memfp"].as<bool>();
        }

        if(result.count("mixed_workload") > 0){
          m_is_mixed_workload = result["mixed_workload"].as<bool>();
        }

        if( result["aging_memfp_physical"].count() > 0 ){
            m_aging_memfp_physical = result["aging_memfp_physical"].as<bool>();
        }

        if( result["aging_memfp_threshold"].count() > 0 ){
            set_aging_memfp_threshold( result["aging_memfp_threshold"].as<ComputerQuantity>() );
        }

        if(result["aging_memfp_report"].count() > 0){
            m_aging_memfp_report = result["aging_memfp_report"].as<bool>();
        }

        if(result["aging_release_memory"].count() > 0){
            m_aging_release_memory = result["aging_release_memory"].as<bool>();
        }

        if( result["blacklist"].count() > 0 ){
            string algorithm;
            stringstream ss(result["blacklist"].as<string>());
            while(getline(ss, algorithm, ',')){
                // trim
                algorithm.erase(algorithm.begin(), std::find_if(algorithm.begin(), algorithm.end(), [](int ch) { return !std::isspace(ch); }));
                algorithm.erase(std::find_if(algorithm.rbegin(), algorithm.rend(), [](int ch) { return !std::isspace(ch); }).base(), algorithm.end());

                // to lower case
                std::transform(algorithm.begin(), algorithm.end(), algorithm.begin(),[](unsigned char c){ return std::tolower(c); });

                m_blacklist.push_back(algorithm);
            }

            std::sort(m_blacklist.begin(), m_blacklist.end());
        } // blacklist

        m_measure_latency = result["latency"].count() > 0;

        if ( result["aging_timeout"].count() > 0 ){
            set_timeout_aging2( result["aging_timeout"].as<DurationQuantity>().as<chrono::seconds>().count() );
        }

        if( result["block_size"].count() > 0 ){
          set_block_size( result["block_size"].as<int>() );
        }

        if(result["is_timestamped"].count() > 0 ){
          set_is_timestamped( result["is_timestamped"].as<bool>() );
        }

    } catch ( argument_incorrect_type& e){
        ERROR(e.what());
    }
}

/*****************************************************************************
 *                                                                           *
 *  Properties                                                               *
 *                                                                           *
 *****************************************************************************/

bool Configuration::has_database() const {
    return !m_database_path.empty();
}

::common::Database* Configuration::db(){
    if(m_database == nullptr && has_database()){
        m_database = new Database{m_database_path};
        auto params = m_database->create_execution();
        // random value with no semantic, the aim is to simplify the work of ./automerge.pl
        // in recognising duplicate entries
        params.add("magic", (uint64_t) std::random_device{}());
    }
    return m_database;
}

void Configuration::set_max_weight(double value){
    if(value <= 0) ERROR("Invalid value for max weight: " << value << ". Expected a positive value");
    m_max_weight = value;
}

void Configuration::set_coeff_aging(double value){
    if(value < 0 || (value > 0 && value < 1)){
        ERROR("The parameter aging is invalid. It must be >= 1.0: " << value);
    }
    m_coeff_aging = value;
}

void Configuration::set_ef_vertices(double value){
    m_ef_vertices = value;
}

void Configuration::set_ef_edges(double value){
    m_ef_edges = value;
}

void Configuration::set_num_repetitions(uint64_t value) {
    m_num_repetitions = value; // accept 0 as value
}

void Configuration::set_num_threads_omp(int value){
    ASSERT( value >= 0 );
#if !defined(HAVE_OPENMP)
    if(value > 0) ERROR("Cannot set the maximum number of threads to use with OpenMP: the driver was not configured with support of OpenMP");
#endif

    m_num_threads_omp = value;
}

void Configuration::set_num_threads_read(int value){
    ASSERT( value >= 0 );
#if !defined(HAVE_OPENMP)
    if(value > 0) ERROR("Cannot set the maximum number of threads to use: the driver was not configured with support of OpenMP");
#endif

    m_num_threads_read = value;
}

void Configuration::set_num_threads_write(int value){
    ASSERT( value > 0 );
    m_num_threads_write = value;
}

void Configuration::set_timeout_graphalytics(uint64_t seconds) {
    m_timeout_graphalytics = seconds;
}

void Configuration::set_timeout_aging2(uint64_t seconds){
    m_timeout_aging2 = seconds;
}

void Configuration::set_graph(const std::string& graph){
    m_path_graph_to_load = graph;
}

void Configuration::set_build_frequency( uint64_t millisecs ){
    m_build_frequency = millisecs;
}

void Configuration::set_load( bool value ) {
    m_load = value;
}

void Configuration::set_aging_step_size( double value ){
    if(value <= 0 || value > 1){
        ERROR("Invalid value for the aging step size. It must be in (0, 1]. Value given: " << value);
    }
    if ( ::ceil(1.0/value) != ::floor(1.0/value) ){
        ERROR("Value for the step size currently not supported: " << value << ". Expected a value such as 1/(step size) is an integer");
    }
    m_step_size_recordings = value;
}

void Configuration::set_aging_cooloff_seconds(uint64_t value){
    m_aging_cooloff_seconds = value;
}

void Configuration::set_aging_memfp_threshold(uint64_t bytes){
    m_aging_memfp_threshold = bytes;
}

void Configuration::set_block_size(size_t block_size) {
  m_block_size = block_size;
}

void Configuration::set_is_timestamped(bool timestamped) {
  m_is_timestamped_graph = timestamped;
}

bool Configuration::is_timestamped_graph() const {
  return m_is_timestamped_graph;
}

uint64_t Configuration::get_num_recordings_per_ops() const {
    double step_size = get_aging_step_size();
    if ( ::ceil(1.0/step_size) != ::floor(1.0/step_size) ){
        ERROR("Value for the step size currently not supported: " << step_size << ". Expected a value such as 1/(step size) is an integer");
    }
    return 1.0/step_size;
}

int Configuration::num_threads(ThreadsType type) const {
    switch(type){
    case THREADS_READ:
        return m_num_threads_read;
    case THREADS_WRITE:
        return m_num_threads_write;
    case THREADS_TOTAL:
        return m_num_threads_read + m_num_threads_write;
    default:
        ERROR("Invalid thread type: " << ((int) type));
    }
}

int Configuration::num_threads_omp() const {
    return m_num_threads_omp;
}

bool Configuration::is_load() const {
    return m_load;
}

bool Configuration::is_mixed_workload() const {
    return m_is_mixed_workload;
}

std::unique_ptr<library::Interface> Configuration::generate_graph_library() {
    return m_library_factory(is_graph_directed());
}

void Configuration::do_blacklist(bool& property_enabled, const char* name) const {
    if(property_enabled){
        if(find(begin(m_blacklist), end(m_blacklist), name) != end(m_blacklist)){
            string name_upper_case = name;
            std::transform(name_upper_case.begin(), name_upper_case.end(), name_upper_case.begin(),[](unsigned char c){ return std::toupper(c); });
            LOG("> Ignore " << name_upper_case << ", algorithm blacklisted");
            property_enabled = false;
        }
    }
}

void Configuration::blacklist(gfe::experiment::GraphalyticsAlgorithms& algorithms) const {
    do_blacklist(algorithms.bfs.m_enabled, "bfs");
    do_blacklist(algorithms.cdlp.m_enabled, "cdlp");
    do_blacklist(algorithms.lcc.m_enabled, "lcc");
    do_blacklist(algorithms.pagerank.m_enabled, "pagerank");
    do_blacklist(algorithms.sssp.m_enabled, "sssp");
    do_blacklist(algorithms.wcc.m_enabled, "wcc");
}

size_t Configuration::block_size() {
  return m_block_size;
}

const std::string& Configuration::get_validation_graph() const {
    if(m_validate_graph.empty()){
        return get_path_graph();
    } else {
        return m_validate_graph;
    }
}

/*****************************************************************************
 *                                                                           *
 *  Save parameters                                                          *
 *                                                                           *
 *****************************************************************************/

// omp_proc_bind_t, translate the typedef in a string
static string omp_proc_bind_to_string(){
    switch(omp_get_proc_bind()){
    case omp_proc_bind_false: return "false"; break;
    case omp_proc_bind_true: return "true"; break;
    case omp_proc_bind_master: return "master"; break;
    case omp_proc_bind_close: return "close"; break;
    case omp_proc_bind_spread: return "spread"; break;
    default: return "unknown";
    }
}

void Configuration::save_parameters() {
    if(db() == nullptr) ERROR("Path where to store the results not set");

    using P = pair<string, string>;
    std::vector<P> params;
    params.push_back(P{"database", get_database_path()});
    params.push_back(P{"git_commit", common::git_last_commit()});
    params.push_back(P{"hostname", common::hostname()});
    params.push_back(P("max_weight", to_string(max_weight())));
    params.push_back(P{"seed", to_string(seed())});
    params.push_back(P{"aging", to_string(m_coeff_aging)});
    params.push_back(P{"aging_cooloff", to_string(get_aging_cooloff_seconds())});
    params.push_back(P{"aging_memfp", to_string(get_aging_memfp())});
    params.push_back(P{"aging_memfp_physical", to_string(get_aging_memfp_physical())});
    params.push_back(P{"aging_memfp_report", to_string(get_aging_memfp_report())});
    params.push_back(P{"aging_memfp_threshold", to_string(get_aging_memfp_threshold())});
    params.push_back(P{"aging_release_memory", to_string(get_aging_release_memory())});
    params.push_back(P{"aging_step_size", to_string(get_aging_step_size())});
    params.push_back(P{"aging_timeout", to_string(get_timeout_aging2())});
    params.push_back(P{"build_frequency", to_string(get_build_frequency())}); // milliseconds
    params.push_back(P{"ef_edges", to_string(get_ef_edges())});
    params.push_back(P{"ef_vertices", to_string(get_ef_vertices())});
    if(!get_path_graph().empty()){ params.push_back(P{"graph", get_path_graph()}); }
    params.push_back(P{"measure_latency", to_string(measure_latency())});
    params.push_back(P{"num_repetitions", to_string(num_repetitions())});
    params.push_back(P{"num_threads_omp", to_string(num_threads_omp())});
    params.push_back(P{"num_threads_read", to_string(num_threads(ThreadsType::THREADS_READ))});
    params.push_back(P{"num_threads_write", to_string(num_threads(ThreadsType::THREADS_WRITE))});
    params.push_back(P{"omp_proc_bind", omp_proc_bind_to_string()});
    params.push_back(P{"timeout", to_string(get_timeout_graphalytics())});
    params.push_back(P{"directed", to_string(is_graph_directed())});
    params.push_back(P{"library", get_library_name()});
    params.push_back(P{"load", to_string(is_load())});
    if(!get_update_log().empty()) {
        // version 1: uniform distribution
        // version 2: log file, follow the same node degree distribution of the input graph
        // version 3: use #add_edge_v2
        params.push_back(P{"aging_impl", "version_3"});
        params.push_back(P{"log", get_update_log()});
    }
    params.push_back(P{"role", "standalone"});
    params.push_back(P{"validate_inserts", to_string(validate_inserts())});
    params.push_back(P{"validate_output", to_string(validate_output())});
    params.push_back(P{"validate_output_graph", get_validation_graph()});
    params.push_back(P{"block_size", to_string(block_size())});
    params.push_back(P{"is_mixed_workload", to_string(m_is_mixed_workload)});

    if(!m_blacklist.empty()){
        stringstream ss;
        for(auto& s: m_blacklist){
            if(ss.tellp() > 0){ ss << ", "; }
            ss << s;
        }
        params.push_back(P{"blacklist", ss.str()});
    }

    CompilerInfo compiler; // detect the compiler used to build this source file
    string compiler_name = compiler.name();
    transform(begin(compiler_name), end(compiler_name), begin(compiler_name), ::tolower);
    params.push_back(P{"compiler", compiler_name});
    params.push_back(P{"compiler_major", to_string(compiler.major())});
    params.push_back(P{"compiler_minor", to_string(compiler.minor())});
    params.push_back(P{"compiler_patch", to_string(compiler.patch())});

    sort(begin(params), end(params));
    db()->store_parameters(params);
}

} // namespace
