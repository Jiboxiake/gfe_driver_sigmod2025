//
// Created by per on 12.06.21.
//

#include "microbenchmarks_driver.hpp"

#include <gapbs.h>
#include <factory.h>
#include <GFEDriverLCC.h>

namespace gfe::library {
    using namespace microbenchmarks;

    MicroBenchmarksDriver::MicroBenchmarksDriver(bool is_graph_directed, const string &name) {
      if (is_graph_directed) {
        throw std::invalid_argument("Only undirected graphs are currently supported by the front-end");
      }
      ds = generate_data_structure(name);
    }

    void MicroBenchmarksDriver::on_main_init(int num_threads) {}

    void MicroBenchmarksDriver::on_thread_init(int thread_id) {}

    void MicroBenchmarksDriver::on_thread_destroy(int thread_id) {}

    void MicroBenchmarksDriver::updates_stop() {
      unordered_set<uint64_t> s;
      s.reserve(internal_2_external.size());
      ds->finalize_load();
    }

    MicroBenchmarksDriver::~MicroBenchmarksDriver() {
    }

    void MicroBenchmarksDriver::dump_ostream(std::ostream &out) const {
      throw exception();
    }

    uint64_t MicroBenchmarksDriver::num_edges() const {
      return ds->edge_count() / 2;
    }

    uint64_t MicroBenchmarksDriver::num_vertices() const {
      return ds->vertex_count();
    }

    /**
     * Returns true if the given vertex is present, false otherwise
     */
    bool MicroBenchmarksDriver::has_vertex(uint64_t vertex_id) const {
      vertex_mapping::const_accessor a;
      if (external_2_internal.find(a, vertex_id)) {
        return ds->has_vertex(a->second);
      } else {
        return false;
      }
    }

    bool MicroBenchmarksDriver::has_edge(uint64_t source, uint64_t destination) const {
      vertex_mapping::const_accessor a;
      vertex_mapping::const_accessor b;
      if (external_2_internal.find(a, source) && external_2_internal.find(b, destination)) {
        edge_t internal_edge {a->second, b->second};
        return ds->has_edge(internal_edge);
      } else {
        return false;
      }
    }

    /**
     * Returns the weight of the given edge is the edge is present, or NaN otherwise
     */
    double MicroBenchmarksDriver::get_weight(uint64_t source, uint64_t destination) const {
      vertex_mapping::const_accessor a;
      vertex_mapping::const_accessor b;
      if (external_2_internal.find(a, source) && external_2_internal.find(b, destination)) {
        edge_t internal_edge {a->second, b->second};
        return ds->get_weight(internal_edge);
      } else {
        return numeric_limits<double>::quiet_NaN();
      }
    }

    /**
     * Check whether the graph is directed
     */
    bool MicroBenchmarksDriver::is_directed() const {
      return false;
    }

    /**
     * Impose a timeout on each graph computation. A computation that does not terminate by the given seconds will raise a TimeoutError.
     */
    void MicroBenchmarksDriver::set_timeout(uint64_t seconds) {
      m_timeout = chrono::seconds{seconds};
    }

    /**
     * Add the given vertex to the graph
     * @return true if the vertex has been inserted, false otherwise (that is, the vertex already exists)
     */
    bool MicroBenchmarksDriver::add_vertex(uint64_t vertex_id) {
      scoped_lock<mutex> l(vertex_add_mutex);
      vertex_mapping::accessor a;
      if (!external_2_internal.insert(a, vertex_id)) { // This does not seem to block until a is released. It seems to continue with the information that another thread is !currently! inserting this key.
        return false;
      } else {
        auto internal_id = next_vertex_id.fetch_add(1);
        if (ds->insert_vertex(internal_id)) {
          a->second = internal_id;
          grow_vector_if_smaller(internal_2_external, internal_id);

          assert(internal_2_external[internal_id] == numeric_limits<uint64_t>::max());

          internal_2_external[internal_id] = vertex_id;
          return true;
        } else {
          throw exception();
        }
      }
    }

    /**
     * Remove the mapping for a given vertex. The actual internal vertex is not removed from the adjacency list.
     * @param vertex_id the vertex to remove
     * @return true if a mapping for that vertex existed, false otherwise
     */
    bool MicroBenchmarksDriver::remove_vertex(uint64_t vertex_id) {
      throw exception();
    }


    /**
     * Adds a given edge to the graph if both vertices exists already
     */
    bool MicroBenchmarksDriver::add_edge(gfe::graph::WeightedEdge e) {
      vertex_mapping::const_accessor a;
      vertex_mapping::const_accessor b;

      if (!(external_2_internal.find(a, e.source()) && external_2_internal.find(b, e.destination()))) {
        return false;
      } else {
        edge_t internal_edge{a->second, b->second};
        edge_t opposite{b->second, a->second};
        auto ret = ds->insert_edge(internal_edge, e.weight());
        ret &= ds->insert_edge(opposite, e.weight());
        return ret;
      }
    }

    bool MicroBenchmarksDriver::add_edge_v2(gfe::graph::WeightedEdge e) {
      {
        vertex_mapping::const_accessor a;
        vertex_mapping::const_accessor b;

        if (!external_2_internal.find(a, e.source())) {
          a.release();
          add_vertex(e.source());
        }

        if (!external_2_internal.find(b, e.destination())) {
          b.release();
          add_vertex(e.destination());
        }
      }

      return add_edge(e);
    }

    bool MicroBenchmarksDriver::remove_edge(gfe::graph::Edge e) {
      throw exception();
    }

    static void save_bfs(vector <pair<uint64_t, uint>> &result, const char *dump2file) {
      assert(dump2file != nullptr);
//      COUT_DEBUG("save the results to: " << dump2file)

      fstream handle(dump2file, ios_base::out);
      if (!handle.good()) ERROR("Cannot save the result to `" << dump2file << "'");

      for (const auto &p : result) {
        handle << p.first << " ";

        // if  the vertex was not reached, the algorithm sets its distance to < 0
        if (p.second == numeric_limits<uint>::max()) {
          handle << numeric_limits<int64_t>::max();
        } else {
          handle << (int64_t) p.second;
        }
        handle << "\n";
      }
      handle.close();
    }

    void MicroBenchmarksDriver::bfs(uint64_t source_vertex_id, const char *dump2file) {
      vertex_mapping::const_accessor a;
      if (!external_2_internal.find(a, source_vertex_id)) {
        throw exception();
      }

      auto internal_source = a->second;
      a.release();

      auto distances = Gapbs::bfs(*ds, internal_source);

      size_t N = distances.size();
      vector <pair<vertex_id_t, uint>> external_ids(N);

      #pragma omp parallel for
      for (uint v = 0; v < N; v++) {
        if (ds->has_vertex(v)) {
          if (distances[v] < 0) {
            external_ids[v] = make_pair(internal_2_external[v], numeric_limits<uint>::max());
          } else {
            external_ids[v] = make_pair(internal_2_external[v], distances[v]);
          }

        } else {
          external_ids[v] = make_pair(v, numeric_limits<uint>::max());
        }
      }

      if (dump2file != nullptr) {
        save_bfs(external_ids, dump2file);
      }
    }

    void MicroBenchmarksDriver::pagerank(uint64_t num_iterations, double damping_factor, const char *dump2file) {
      auto pr = Gapbs::page_rank(*ds, num_iterations, damping_factor);
      auto external_ids = translate<double>(pr);

      if (dump2file != nullptr) {
        save_result<double>(external_ids, dump2file);
      }
    }

    void MicroBenchmarksDriver::wcc(const char *dump2file) {
      auto clusters = Gapbs::wcc(*ds);
      auto external_ids = translate<uint64_t>(clusters);

      if (dump2file != nullptr) {
        save_result<uint64_t>(external_ids, dump2file);
      }
    }

    void MicroBenchmarksDriver::cdlp(uint64_t max_iterations, const char *dump2file) {
      auto clusters = Gapbs::cdlp(*ds, [this](uint64_t v) { return this->internal_2_external[v]; },max_iterations);
      auto external_ids = translate<uint64_t>(clusters);

      if (dump2file != nullptr) {
        save_result<uint64_t>(external_ids, dump2file);
      }
    }

    void MicroBenchmarksDriver::lcc(const char *dump2file) {
      vector<double> lcc_values = gfeDriverLCC::GFEDriverLCC::lcc(*ds);
      auto external_ids = translate<double>(lcc_values);
      if (dump2file != nullptr) {
        save_result<double>(external_ids, dump2file);
      }
    }

    void MicroBenchmarksDriver::sssp(uint64_t source_vertex_id, const char *dump2file) {
      vertex_mapping::const_accessor a;
      if (!external_2_internal.find(a, source_vertex_id)) {
        throw exception();
      }

      auto internal_source = a->second;
      a.release();

      auto distances = Gapbs::sssp(*ds, internal_source, 2.0);

      auto external_ids = translate<double>(distances);
      if (dump2file != nullptr) {
        save_result<double>(external_ids, dump2file);
      }
    }

    bool MicroBenchmarksDriver::can_be_validated() const {
      return true;
    }

    bool MicroBenchmarksDriver::has_weights() const {
      return ds->has_weights();
    }

}