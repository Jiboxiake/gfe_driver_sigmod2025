//
// Created by per on 29.09.21.
//

#include "mixed_workload.hpp"

#include <future>
#include <chrono>

#if defined(HAVE_OPENMP)
  #include "omp.h"
#endif

#include "graphalytics.hpp"
#include "aging2_experiment.hpp"
#include "mixed_workload_result.hpp"

namespace gfe::experiment {

    using namespace std;

    MixedWorkloadResult MixedWorkload::execute() {
      auto aging_result_future = std::async(std::launch::async, &Aging2Experiment::execute, &m_aging_experiment);

      chrono::seconds progress_check_interval( 1 );
      this_thread::sleep_for( progress_check_interval );  // Poor mans synchronization to ensure AgingExperiment was able to setup the master etc
      while (true) {
        if (m_aging_experiment.progress_so_far() > 0.1 || aging_result_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) { // The graph reached its final size
          break;
        }
        this_thread::sleep_for( progress_check_interval ) ;
      }
      m_graphalytics.mixed_workload_finished_loading();
      cout << "Graph reached final size, executing graphaltyics now" << endl;
      // TODO change this to also work for LCC, generally this solution is very ugly
#if defined(HAVE_OPENMP)
      if(m_read_threads != 0 ){
                        cout << "[driver] OpenMP, number of threads for the Graphalytics suite: " << m_read_threads << endl;
                        omp_set_num_threads(m_read_threads);
                    }
#endif

#if HAVE_LIVEGRAPH
      while (m_aging_experiment.progress_so_far() < 0.16 && aging_result_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        m_graphalytics.execute();
      }
#else
      while (m_aging_experiment.progress_so_far() < 0.9 && aging_result_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        m_graphalytics.execute();
      }
#endif
      m_graphalytics.mixed_workload_read_finish();
      cout << "Waiting for aging experiment to finish" << endl;
      aging_result_future.wait();
      cout << "Getting aging experiment results" << endl;
      auto aging_result = aging_result_future.get();

      return MixedWorkloadResult { aging_result, m_graphalytics };
    }

    void MixedWorkload::report_graphalytics() {
        m_graphalytics.report(false);
    }
}