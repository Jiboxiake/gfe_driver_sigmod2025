#ifndef GFE_DRIVER_MIXED_WORKLOAD_RESULT_H
#define GFE_DRIVER_MIXED_WORKLOAD_RESULT_H

#include "aging2_result.hpp"
namespace gfe::experiment { class GraphalyticsSequential; }
namespace common { class Database; }

namespace gfe::experiment {

    class MixedWorkloadResult {
    public:
        MixedWorkloadResult(Aging2Result aging_result, GraphalyticsSequential& analytics);

        void save(common::Database* db);

    private:
        Aging2Result m_aging_result;
        GraphalyticsSequential& m_graphalytics;
    };

    class UpdatesReadsMixedWorkloadResult {
    public:
       // UpdatesReadsMixedWorkloadResult(Aging2Result aging_result, GraphalyticsSequential& analytics);

        void save(common::Database* db);

    private:
        Aging2Result m_aging_result;
        //GraphalyticsSequential& m_graphalytics;
    };

}

#endif //GFE_DRIVER_MIXED_WORKLOAD_RESULT_H
