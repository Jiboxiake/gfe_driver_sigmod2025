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

#include "aging2_result.hpp"

#include <cassert>

#include "common/database.hpp"
#include "common/error.hpp"
#include "details/latency.hpp"
#include "aging2_experiment.hpp"

using namespace common;
using namespace std;

namespace gfe::experiment {

Aging2Result::Aging2Result(const Aging2Experiment& parameters) : m_num_threads(parameters.m_num_threads), m_worker_granularity(parameters.m_worker_granularity){

}

Aging2Result::~Aging2Result(){

}

void Aging2Result::save(common::Database* handle) {
    assert(handle != nullptr && "Null pointer");
    if(handle == nullptr) INVALID_ARGUMENT("The handle to the database is a nullptr");

    auto db = handle->add("aging");
    db.add("granularity", m_worker_granularity);
    db.add("num_threads", m_num_threads);
    db.add("num_updates", m_num_operations_total);
    db.add("num_artificial_vertices", m_num_artificial_vertices);
    db.add("num_vertices_load", m_num_vertices_load);
    db.add("num_vertices_final", m_num_vertices_final_graph);
    db.add("num_edges_load", m_num_edges_load);
    db.add("num_edges_final", m_num_edges_final_graph);
    db.add("num_build_invocations", m_num_build_invocations);
    db.add("num_snapshots_created", m_num_levels_created);
    db.add("completion_time", m_completion_time); // microseconds
    db.add("has_terminated_for_timeout", (int64_t) m_timeout_hit);
    db.add("has_terminated_for_memfp", (int64_t) m_memfp_threshold_passed);
    db.add("has_terminated_deadlocked", (int64_t) m_thread_deadlocked);
    db.add("has_terminated_deadlocked_in_library", (int64_t) m_in_library_code);

    for(int i = 0, sz = m_reported_times.size(); i < sz; i++){
      if(m_reported_times[i] == 0) continue; // missing??
      auto db = handle->add("aging_intermediate_throughput");
      db.add("aging_coeff", (int64_t) i +1); // 1, 2, 3...
      db.add("completion_time", m_reported_times[i]); // microseconds
    }

  for(uint64_t i = 0, sz = m_progress.size(); i < sz; i++){
    auto db = handle->add("aging_intermediate_throughput3");
    db.add("second", (int64_t) i +1); // 1, 2, 3...
    db.add("num_operations", m_progress[i]);
  }

  for(auto record: m_memory_footprint){
    auto db = handle->add("aging_intermediate_memory_usage_v2");
    db.add("tick", record.m_tick );
    db.add("memfp_process", record.m_memory_process );
    db.add("memfp_driver", record.m_memory_driver );
    db.add("cooloff", (int64_t) record.m_is_cooloff);
  }

    if(m_latency_stats.get() != nullptr){
        m_latency_stats[0].save("inserts");
        m_latency_stats[1].save("deletes");
        m_latency_stats[2].save("updates");
    }
}

void Aging2Result::save(std::shared_ptr<common::Database> db){
    save(db.get());
}

} // namespace
