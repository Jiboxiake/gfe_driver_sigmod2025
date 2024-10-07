//
// Created by zhou822 on 8/6/23.
//

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include"mixed_workload_result.hpp"

// forward declarations
namespace gfe::graph { class WeightedEdgeStream; }
namespace gfe::experiment { class UpdatesShortReadsExperiment; }
namespace gfe::experiment::details { class UpdateReadMaster; }
namespace gfe::experiment::details { class Aging2Worker; }
namespace gfe::experiment::details { class ReadWorker; }
namespace gfe::library { class UpdateInterface; }

namespace gfe::experiment{
    class UpdatesShortReadsExperiment {
        //friend class Aging2Result;
        friend class details::UpdateReadMaster;
        friend class details::Aging2Worker;
        friend class details::ReadWorker;

        std::shared_ptr<gfe::library::UpdateInterface> m_library; // the library to evaluate
        std::string m_path_log; // the path to the log file [graphlog] with the sequence of updates to perform
        uint64_t m_num_threads = 1; // set the number of threads to use
        uint64_t m_worker_granularity = 1024; // the granularity of a task for a worker, that is the number of contiguous operations (inserts/deletes) performed inside the threads between each invocation to the scheduler.
        double m_max_weight = 1.0; // set the max weight for the edges to create
        std::chrono::milliseconds m_build_frequency {0}; // the frequency to create a new delta/snapshot, that is invoking the method #build()
        bool m_memfp = false; // whether to measure the memory footprint
        bool m_memfp_physical = false; // whether to consider the physical or the virtual memory in the memory footprint
        uint64_t m_memfp_threshold = 0; // forcedly stop the execution of the experiment when the readings of the memory footprint are above this threshold (0 = infinite)
        bool m_release_driver_memory = true; // whether to release the driver's memory as the experiment proceeds. Otherwise, it's only released at the end of the experiment
        bool m_report_memory_footprint = false; // whether to print to stdout the measurements observed for the memory footprint
        bool m_report_progress = false; // whether to report the current progress
        uint64_t m_num_reports_per_operations = 1; // how often to save in the database progress done
        bool m_measure_latency = false; // whether to measure the latency of updates
        std::chrono::seconds m_timeout {0}; // max time to run the simulation (excl. cool-off time)
        std::chrono::seconds m_cooloff {0}; // number of seconds to wait after the experiment terminates, to check the effectiveness of the GC

        details::UpdateReadMaster* m_master;
    public:
        UpdatesShortReadsExperiment();
        ~UpdatesShortReadsExperiment();

        // Set the library to evaluate
        void set_library(std::shared_ptr<gfe::library::UpdateInterface> library);

        // Set the path to the log file with all updates
        void set_log(const std::string& path_log);

        // Set the max weight for the edges created
        void set_max_weight(double value);

        // Set the number of client threads to use in the experiment, that is, the parallelism degree
        void set_parallelism_degree(uint64_t num_threads);

        // Set how frequently create a new snapshot/delta in the library (0 = do not create new snapshots)
        void set_build_frequency(std::chrono::milliseconds millisecs);

        // whether to release the driver's memory as the experiment proceeds. Otherwise, it's only released at the end of the experiment
        void set_release_memory(bool value);

        // Whether to print to stdout the current progress of the experiment
        void set_report_progress(bool value);

        // Whether to print to stdout the measurements observed for the memory footprint
        void set_report_memory_footprint(bool value);

        // Set how often to save in the database the progress done. The minimum value is 1.
        // A value of N, implies that there will N reports every `num_edges' operations. For instance:
        // with N = 1, it will save the progress after 1x, 2x, 3x, 4x, ..., 9x, 10x operations
        // with N = 2, it will save the progress after 0.5x, 1x, 1.5x, 2x, ..., 9x, 9.5x, 10x operations
        // with N = 4, it will save the progress after 0.25x, 0.5x, 0.75, 1x, 1.25x, ... operations
        void set_num_reports_per_ops(uint64_t value);

        // Measure the latency of updates?
        void set_measure_latency(bool value);

        // Set the max time to run the experiment
        void set_timeout(std::chrono::seconds secs);

        // Cool-off period. Number of seconds to wait idle after the simulation terminated, measuring the memory footprint
        void set_cooloff(std::chrono::seconds secs);

        // Measure the memory footprint?
        void set_memfp(bool value);
        void set_measure_memfp(bool value){ set_memfp(value); }

        // Whether to measure the phyical memory or the virtual memory in the memory footprint
        void set_memfp_physical(bool value);

        // Forcedly stop the execution of the experiment when the readings of the memory footprint are above this threshold (0 = infinite)
        void set_memfp_threshold(uint64_t value);

        // [Internal parameter]
        // Set the granularity of a task for a worker thread. This is the number of contiguos operations (inserts/deletes) done
        // by each worker thread between each invocation to the scheduler.
        void set_worker_granularity(uint64_t value);

        // Execute the experiment with the given configuration
        // @param reset_graph if true, release the contained graph before running the experiment, to save some memory
        UpdatesReadsMixedWorkloadResult execute();

        double progress_so_far();
    };
}//namespace



