//
// Created by per on 29.09.21.
//

#include "mixed_workload_result.hpp"

#include "common/database.hpp"
#include "aging2_result.hpp"
#include "graphalytics.hpp"
#include "iostream"

namespace gfe::experiment {
    using namespace std;

    MixedWorkloadResult::MixedWorkloadResult(Aging2Result aging_result, GraphalyticsSequential& analytics)
      : m_aging_result(aging_result), m_graphalytics(analytics) {

    }

    void MixedWorkloadResult::save(common::Database* db) {
      cout << "Start saving results" << endl;
      m_graphalytics.report(true);
      cout << "Saved graphalytics" << endl;
      m_aging_result.save(db);
      cout << "Saved aging" << endl;
      cout << "Saved aging" << endl;
    }
}