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
#include "stinger.hpp"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <mutex>
#include <queue>
#include <thread>

#include "common/system.hpp"
#include "common/timer.hpp"
#include "stinger_core/stinger.h"
#include "stinger_core/xmalloc.h"
#include "utility/timeout_service.hpp"
#include "stinger_error.hpp"

using namespace libcuckoo;
using namespace std;

// Macros
#define STINGER reinterpret_cast<struct stinger*>(m_stinger_graph)
#undef CURRENT_ERROR_TYPE
#define CURRENT_ERROR_TYPE ::gfe::library::StingerError

/******************************************************************************
 *                                                                            *
 *  Debug                                                                     *
 *                                                                            *
 *****************************************************************************/
//#define DEBUG
extern mutex _log_mutex [[maybe_unused]];
#define COUT_DEBUG_FORCE(msg) { scoped_lock<mutex> lock(::_log_mutex); std::cout << "[Stinger::" << __FUNCTION__ << "] [" << common::concurrency::get_thread_id() << "] " << msg << std::endl; }
#if defined(DEBUG)
    #define COUT_DEBUG(msg) COUT_DEBUG_FORCE(msg)
#else
    #define COUT_DEBUG(msg)
#endif


/******************************************************************************
 *                                                                            *
 *  Utility functions                                                         *
 *                                                                            *
 ******************************************************************************/
// dump the content to the given file
static void save(vector<pair<uint64_t, double>>& result, const char* dump2file){
    if(dump2file == nullptr) return; // nop
    COUT_DEBUG("save the results to: " << dump2file)

    fstream handle(dump2file, ios_base::out);
    if(!handle.good()) ERROR("Cannot save the result to `" << dump2file << "'");

    for(auto p : result){
        handle << p.first << " " << p.second << "\n";
    }

    handle.close();
}

// Copy & paste from stinger_alg/src/clustering.c
static int compare(const void* a, const void* b){
    return ( *(int64_t*)a - *(int64_t*)b );
}


/******************************************************************************
 *                                                                            *
 *  LCC                                                                       *
 *                                                                            *
 ******************************************************************************/
namespace gfe::library {
// Implementation based on stinger_alg/src/clustering.c

void Stinger::lcc(const char* dump2file){
    auto tcheck = make_unique<utility::TimeoutService>( chrono::seconds{ m_timeout } );
    common::Timer timer; timer.start();
//    uint64_t nv = stinger_max_active_vertex(STINGER); // it doesn't register the coefficient if the last vertices are isolated
    uint64_t nv = get_max_num_mappings(); // number of mappings

    vector<pair<uint64_t, double>> result (nv);

    #pragma omp parallel for schedule(dynamic, 64)
    for(uint64_t v = 0; v < nv; v++){
        // timeout check
        if(tcheck->is_timeout()) continue; // do not do any additional processing
        if(stinger_vtype_get(STINGER, v) == 1) continue; // node marked for deletion

        double coeff = 0.0; // by definition, if the vertex has less than two neighbours, its clustering coefficient is zero
        uint64_t degree = stinger_degree_get(STINGER, v);
        if(degree >= 2){
            uint64_t num_triangles = lcc_count_triangles(v);
            uint64_t max_num_edges = degree * (degree -1);
            coeff = static_cast<double>(num_triangles) / max_num_edges;
        }

        result[v] = make_pair(get_external_id(v), coeff);
    }

    timer.stop();
    if(tcheck->is_timeout()){
        RAISE_EXCEPTION(TimeoutError, "Timeout occurred after " << timer);
    }

    save(result, dump2file);
}

uint64_t Stinger::lcc_count_triangles(int64_t vertex_id){
    assert(stinger_vtype_get(STINGER, vertex_id) == 0 && "Node marked for deletion");
    uint64_t out = 0; // final number of triangles
    int64_t degree = stinger_degree_get(STINGER, vertex_id);

    // sorted list of neighbours (from both incoming and outgoing edges)
    int64_t* neighbours = (int64_t*) xmalloc(degree * sizeof(int64_t));
    size_t returned_degree { 0 };
    stinger_gather_neighbors(STINGER, vertex_id, &returned_degree, neighbours, /* optional fields weights, timestamps 0 and 1, type */ nullptr, nullptr, nullptr, nullptr, degree);
    assert(returned_degree == static_cast<size_t>(degree) && "Degree mismatch");
    qsort(neighbours, degree, sizeof(int64_t), compare);

    // consider both outgoing and incoming edges. Stinger ensures that a neighbour in a directed
    // graph that has both an incoming and outgoing edge is visited only once
    STINGER_FORALL_EDGES_OF_VTX_BEGIN(STINGER, vertex_id) {
      if (STINGER_EDGE_DEST != vertex_id) { // ignore self loops
        out += lcc_count_intersections (vertex_id, STINGER_EDGE_DEST, neighbours, degree);
      }
    } STINGER_FORALL_EDGES_OF_VTX_END();

    free(neighbours);

    return out;
}

uint64_t Stinger::lcc_count_intersections (int64_t vertex1, int64_t vertex2, int64_t* vertex1_neighbours, int64_t vertex1_neighbours_sz){
    assert(vertex1_neighbours_sz > 0 && "If this vertex1 doesn't have any neighbours, then vertex2 cannot be a neighbour itself");

    uint64_t out { 0 };

    auto binsearch = [&, vertex1_neighbours, vertex1_neighbours_sz](int64_t vertex3){
        int64_t first = 0;
        int64_t last = vertex1_neighbours_sz -1;
        int64_t middle = (first + last)/2;

        while (first <= last) {
            if (vertex1_neighbours[middle] < vertex3) {
                first = middle + 1;
            } else if (vertex1_neighbours[middle] == vertex3) {
                COUT_DEBUG("Triangle " << get_external_id(vertex1) << " - " << get_external_id(vertex2) << " - " << get_external_id(vertex3));
                return true;
            } else {
                last = middle - 1;
            }

            middle = (first + last)/2;
        }

        return false;
    };

    /*
     * According to the Graphalytics spec. v1.0 of this algorithm, for both undirected & directed graphs,
     * we only account the outgoing edges at this step, that is for the connection vertex2 -> vertex3.
     * Whereas for the link vertex1 <-> vertex2 we need to consider both incoming & outgoing edges for
     * directed graphs.
     */
    STINGER_FORALL_OUT_EDGES_OF_VTX_BEGIN(STINGER, vertex2) {
      if (STINGER_EDGE_DEST != vertex1) {
          out += ( binsearch(STINGER_EDGE_DEST) == true );
      }
    } STINGER_FORALL_OUT_EDGES_OF_VTX_END();

    return out;
}


} // namespace
