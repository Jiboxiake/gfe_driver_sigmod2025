//
// Created by per on 03.02.21.
//

#include "sortledton_driver.hpp"

#include <chrono>
#include <optional>
#include <omp.h>

#include "third-party/gapbs/gapbs.hpp"

#include "common/timer.hpp"
#include "utility/timeout_service.hpp"

#include "not_implemented.hpp"

#include "data-structure/data_types.h"
#include "algorithms/SSSP.h"
#include "algorithms/PageRank.h"
#include "algorithms/WCC.h"
#include "algorithms/CDLP.h"
#include "algorithms/LCC.h"
#include "algorithms/GAPBSAlgorithms.h"
#include "data-structure/EdgeDoesNotExistsPrecondition.h"
#include "data-structure/VersionedBlockedEdgeIterator.h"
#include "data-structure/VersionedBlockedPropertyEdgeIterator.h"

using namespace gapbs;
using namespace common;
#define USING_NIGHBOR_SIZE true
namespace gfe
{
  extern mutex _log_mutex [[maybe_unused]];
}

namespace gfe::library
{

  SortledtonDriver::SortledtonDriver(bool is_graph_directed, size_t properties_size, int block_size) : tm(1),
                                                                                                       m_is_directed(
                                                                                                           is_graph_directed)
  {
    if (is_graph_directed == true)
    {
      throw std::invalid_argument("Only undirected graphs are currently supported by the front-end");
    }
    ds = new VersioningBlockedSkipListAdjacencyList(block_size, properties_size, tm);
  }

  SortledtonDriver::~SortledtonDriver()
  {
    delete ds;
    ds = nullptr;
  }

  void SortledtonDriver::on_main_init(int num_threads)
  {
    tm.reset_max_threads(num_threads);
  }

  void SortledtonDriver::on_thread_init(int thread_id)
  {
    tm.register_thread(thread_id);
  }

  void SortledtonDriver::on_thread_destroy(int thread_id)
  {
    tm.deregister_thread(thread_id);
  }

  void SortledtonDriver::dump_ostream(std::ostream &out) const
  {
    throw NotImplemented();
  }

  uint64_t SortledtonDriver::num_edges() const
  {
    SortledtonDriver *non_const_this = const_cast<SortledtonDriver *>(this);
    SnapshotTransaction tx = non_const_this->tm.getSnapshotTransaction(ds, false);
    auto num_edges = tx.edge_count() / 2;
    non_const_this->tm.transactionCompleted(tx);
    return num_edges;
  }

  uint64_t SortledtonDriver::num_vertices() const
  {
    SortledtonDriver *non_const_this = const_cast<SortledtonDriver *>(this);
    SnapshotTransaction tx = non_const_this->tm.getSnapshotTransaction(ds, false);
    auto num_vertices = tx.vertex_count();
    non_const_this->tm.transactionCompleted(tx);
    return num_vertices;
  }

  /**
   * Returns true if the given vertex is present, false otherwise
   */
  bool SortledtonDriver::has_vertex(uint64_t vertex_id) const
  {
    SortledtonDriver *non_const_this = const_cast<SortledtonDriver *>(this);
    SnapshotTransaction tx = non_const_this->tm.getSnapshotTransaction(ds,
                                                                       false); // TODO weights currently not supported
    auto has_vertex = tx.has_vertex(vertex_id);
    non_const_this->tm.transactionCompleted(tx);
    return has_vertex;
  }

  /**
   * Returns the weight of the given edge is the edge is present, or NaN otherwise
   */
  double SortledtonDriver::get_weight(uint64_t source, uint64_t destination) const
  {
    SortledtonDriver *non_const_this = const_cast<SortledtonDriver *>(this);
    SnapshotTransaction tx = non_const_this->tm.getSnapshotTransaction(ds, false);

    if (!tx.has_vertex(source) || !tx.has_vertex(destination))
    {
      return nan("");
    }
    weight_t w;
    auto has_edge = tx.get_weight({static_cast<dst_t>(source), static_cast<dst_t>(destination)}, (char *)&w);
    non_const_this->tm.transactionCompleted(tx);
    return has_edge ? w : nan("");
  }

  /**
   * Check whether the graph is directed
   */
  bool SortledtonDriver::is_directed() const
  {
    return m_is_directed;
  }

  /**
   * Impose a timeout on each graph computation. A computation that does not terminate by the given seconds will raise a TimeoutError.
   */
  void SortledtonDriver::set_timeout(uint64_t seconds)
  {
    m_timeout = chrono::seconds{seconds};
  }

  /**
   * Add the given vertex to the graph
   * @return true if the vertex has been inserted, false otherwise (that is, the vertex already exists)
   */
  bool SortledtonDriver::add_vertex(uint64_t vertex_id)
  {
    SnapshotTransaction tx = tm.getSnapshotTransaction(ds, true);
    bool inserted = true;
    try
    {
      tx.insert_vertex(vertex_id);
      tx.execute();
    }
    catch (exception &e)
    {
      inserted = false;
    }
    tm.transactionCompleted(tx);
    return inserted;
  }

  /**
   * Remove the mapping for a given vertex. The actual internal vertex is not removed from the adjacency list.
   * @param vertex_id the vertex to remove
   * @return true if a mapping for that vertex existed, false otherwise
   */
  bool SortledtonDriver::remove_vertex(uint64_t vertex_id)
  {
    throw NotImplemented();
  }

  /**
   * Adds a given edge to the graph if both vertices exists already
   */
  bool SortledtonDriver::add_edge(gfe::graph::WeightedEdge e)
  {
    assert(!m_is_directed);
    edge_t internal_edge{static_cast<dst_t>(e.source()), static_cast<dst_t>(e.destination())};

    thread_local optional<SnapshotTransaction> tx_o = nullopt;
    if (tx_o.has_value())
    {
      tm.getSnapshotTransaction(ds, true, *tx_o);
    }
    else
    {
      tx_o = tm.getSnapshotTransaction(ds, true);
    }
    auto tx = *tx_o;

    VertexExistsPrecondition pre_v1(internal_edge.src);
    tx.register_precondition(&pre_v1);
    VertexExistsPrecondition pre_v2(internal_edge.dst);
    tx.register_precondition(&pre_v2);
    // Even in the undirected case, we need to check only for the existence of one edge direction to ensure consistency.
    EdgeDoesNotExistsPrecondition pre_e(internal_edge);
    tx.register_precondition(&pre_e);

    // test
    bool inserted = true;
    try
    {
      tx.insert_edge(internal_edge, (char *)&e.m_weight, sizeof(e.m_weight));
      tx.insert_edge({internal_edge.dst, internal_edge.src}, (char *)&e.m_weight, sizeof(e.m_weight));
      inserted &= tx.execute();
    }
    catch (VertexDoesNotExistsException &e)
    {
      inserted = false;
    }
    catch (EdgeExistsException &e)
    {
      inserted = false;
    }
    tm.transactionCompleted(tx);
    return inserted;
  }

  bool SortledtonDriver::add_edge_v2(gfe::graph::WeightedEdge e)
  {
    assert(!m_is_directed);

    thread_local optional<SnapshotTransaction> tx_o = nullopt;
    edge_t internal_edge{static_cast<dst_t>(e.source()), static_cast<dst_t>(e.destination())};

    //      bool insertion = true;
    //      if (tx_o.has_value()) {
    //        tm.getSnapshotTransaction(ds, false, *tx_o);
    //        auto tx = *tx_o;
    //
    //        bool exists = tx.has_edge(internal_edge);
    //        bool exists_reverse = tx.has_edge({internal_edge.dst, internal_edge.src});
    //        if (exists != exists_reverse) {
    //          cout << "Edge existed in only one direction" << endl;
    //        }
    //        if (exists) {
    //          insertion = false;
    //        }
    //        tm.transactionCompleted(tx);
    //      }

    if (tx_o.has_value())
    {
      tm.getSnapshotTransaction(ds, true, *tx_o);
    }
    else
    {
      tx_o = tm.getSnapshotTransaction(ds, true);
    }
    auto tx = *tx_o;

    tx.use_vertex_does_not_exists_semantics();

    tx.insert_vertex(internal_edge.src);
    tx.insert_vertex(internal_edge.dst);

    tx.insert_edge(internal_edge, (char *)&e.m_weight, sizeof(e.m_weight));
    tx.insert_edge({internal_edge.dst, internal_edge.src}, (char *)&e.m_weight, sizeof(e.m_weight)); // changed back to consistent insert order

    tx.execute();

    tm.transactionCompleted(tx);

    //      tm.getSnapshotTransaction(ds, false, *tx_o);
    //      tx = *tx_o;
    //      double out;
    //      double out_reverse;
    //      bool exists = tx.get_weight(internal_edge, (char*) &out);
    //      bool exists_reverse = tx.get_weight({internal_edge.dst, internal_edge.src}, (char*) &out_reverse);
    //      if (!exists) {
    //        cout << "Forward edge does not exist." << endl;
    //      }
    //      if (!exists_reverse) {
    //        cout << "Backward edge does not exist." << endl;
    //      }
    //      if (out != out_reverse) {
    //        cout << "Edge sites have unequal weight: " << out << " " << out_reverse << endl;
    //        cout << "This was an insertion: " << insertion << endl;
    //        cout << "In neighbourhood: " << tx.neighbourhood_size(internal_edge.src) <<
    //        " " << tx.neighbourhood_size(internal_edge.dst) << endl;
    //      }
    //      if (out != e.m_weight) {
    //        cout << "Weight incorrect: " << e.m_weight << " " << out << endl;
    //        cout << "This was an insertion: " << insertion << endl;
    //      }
    //      tm.transactionCompleted(tx);
    return true;
  }

  bool SortledtonDriver::add_edge_v3(gfe::graph::WeightedEdge e)
  {
    assert(!m_is_directed);

    thread_local optional<SnapshotTransaction> tx_o = nullopt;
    edge_t internal_edge{static_cast<dst_t>(e.source()), static_cast<dst_t>(e.destination())};

    //      bool insertion = true;
    //      if (tx_o.has_value()) {
    //        tm.getSnapshotTransaction(ds, false, *tx_o);
    //        auto tx = *tx_o;
    //
    //        bool exists = tx.has_edge(internal_edge);
    //        bool exists_reverse = tx.has_edge({internal_edge.dst, internal_edge.src});
    //        if (exists != exists_reverse) {
    //          cout << "Edge existed in only one direction" << endl;
    //        }
    //        if (exists) {
    //          insertion = false;
    //        }
    //        tm.transactionCompleted(tx);
    //      }

    if (tx_o.has_value())
    {
      tm.getSnapshotTransaction(ds, true, *tx_o);
    }
    else
    {
      tx_o = tm.getSnapshotTransaction(ds, true);
    }
    auto tx = *tx_o;

    tx.use_vertex_does_not_exists_semantics();

    tx.insert_vertex(internal_edge.src);
    tx.insert_vertex(internal_edge.dst);

    tx.insert_or_update_edge(internal_edge, (char *)&e.m_weight, sizeof(e.m_weight));
    tx.insert_or_update_edge({internal_edge.dst, internal_edge.src}, (char *)&e.m_weight, sizeof(e.m_weight)); // changed back to consistent insert order

    tx.execute();

    tm.transactionCompleted(tx);

    //      tm.getSnapshotTransaction(ds, false, *tx_o);
    //      tx = *tx_o;
    //      double out;
    //      double out_reverse;
    //      bool exists = tx.get_weight(internal_edge, (char*) &out);
    //      bool exists_reverse = tx.get_weight({internal_edge.dst, internal_edge.src}, (char*) &out_reverse);
    //      if (!exists) {
    //        cout << "Forward edge does not exist." << endl;
    //      }
    //      if (!exists_reverse) {
    //        cout << "Backward edge does not exist." << endl;
    //      }
    //      if (out != out_reverse) {
    //        cout << "Edge sites have unequal weight: " << out << " " << out_reverse << endl;
    //        cout << "This was an insertion: " << insertion << endl;
    //        cout << "In neighbourhood: " << tx.neighbourhood_size(internal_edge.src) <<
    //        " " << tx.neighbourhood_size(internal_edge.dst) << endl;
    //      }
    //      if (out != e.m_weight) {
    //        cout << "Weight incorrect: " << e.m_weight << " " << out << endl;
    //        cout << "This was an insertion: " << insertion << endl;
    //      }
    //      tm.transactionCompleted(tx);
    return true;
  }

  bool SortledtonDriver::update_edge_v1(gfe::graph::WeightedEdge e)
  {
    SnapshotTransaction r_tx = tm.getSnapshotTransaction(ds, false);

    if (r_tx.has_vertex(e.source()) && r_tx.has_vertex(e.destination()))
    {
      weight_t w;
      auto has_edge = r_tx.get_weight({static_cast<dst_t>(e.source()), static_cast<dst_t>(e.destination())},
                                      (char *)&w);
      if (has_edge)
      {
        e.m_weight += w;
      }
    }
    tm.transactionCompleted(r_tx);
    assert(!m_is_directed);

    thread_local optional<SnapshotTransaction> tx_o = nullopt;
    edge_t internal_edge{static_cast<dst_t>(e.source()), static_cast<dst_t>(e.destination())};

    //      bool insertion = true;
    //      if (tx_o.has_value()) {
    //        tm.getSnapshotTransaction(ds, false, *tx_o);
    //        auto tx = *tx_o;
    //
    //        bool exists = tx.has_edge(internal_edge);
    //        bool exists_reverse = tx.has_edge({internal_edge.dst, internal_edge.src});
    //        if (exists != exists_reverse) {
    //          cout << "Edge existed in only one direction" << endl;
    //        }
    //        if (exists) {
    //          insertion = false;
    //        }
    //        tm.transactionCompleted(tx);
    //      }

    if (tx_o.has_value())
    {
      tm.getSnapshotTransaction(ds, true, *tx_o);
    }
    else
    {
      tx_o = tm.getSnapshotTransaction(ds, true);
    }
    auto tx = *tx_o;

    tx.use_vertex_does_not_exists_semantics();

    tx.insert_vertex(internal_edge.src);
    tx.insert_vertex(internal_edge.dst);

    tx.insert_or_update_edge({internal_edge.dst, internal_edge.src}, (char *)&e.m_weight, sizeof(e.m_weight)); // changed back to consistent insert order
    tx.insert_or_update_edge(internal_edge, (char *)&e.m_weight, sizeof(e.m_weight));
    tx.execute();

    tm.transactionCompleted(tx);

    //      tm.getSnapshotTransaction(ds, false, *tx_o);
    //      tx = *tx_o;
    //      double out;
    //      double out_reverse;
    //      bool exists = tx.get_weight(internal_edge, (char*) &out);
    //      bool exists_reverse = tx.get_weight({internal_edge.dst, internal_edge.src}, (char*) &out_reverse);
    //      if (!exists) {
    //        cout << "Forward edge does not exist." << endl;
    //      }
    //      if (!exists_reverse) {
    //        cout << "Backward edge does not exist." << endl;
    //      }
    //      if (out != out_reverse) {
    //        cout << "Edge sites have unequal weight: " << out << " " << out_reverse << endl;
    //        cout << "This was an insertion: " << insertion << endl;
    //        cout << "In neighbourhood: " << tx.neighbourhood_size(internal_edge.src) <<
    //        " " << tx.neighbourhood_size(internal_edge.dst) << endl;
    //      }
    //      if (out != e.m_weight) {
    //        cout << "Weight incorrect: " << e.m_weight << " " << out << endl;
    //        cout << "This was an insertion: " << insertion << endl;
    //      }
    //      tm.transactionCompleted(tx);
    return true;
  }

  bool SortledtonDriver::remove_edge(gfe::graph::Edge e)
  {
    assert(!m_is_directed);

    thread_local optional<SnapshotTransaction> tx_o = nullopt;
    if (tx_o.has_value())
    {
      tm.getSnapshotTransaction(ds, true, *tx_o);
    }
    else
    {
      tx_o = tm.getSnapshotTransaction(ds, true);
    }
    auto tx = *tx_o;

    edge_t internal_edge{static_cast<dst_t>(e.source()), static_cast<dst_t>(e.destination())};

    tx.delete_edge(internal_edge);
    tx.delete_edge({internal_edge.dst, internal_edge.src});

    bool removed = true;
    removed &= tx.execute();

    tm.transactionCompleted(tx);
    return removed;
  }

  void SortledtonDriver::run_gc()
  {
    if (!gced)
    {
      Timer t;
      t.start();
      ds->gc_all();
      gced = true;
      cout << "Running GC took: " << t;
    }
  }

  static void save_bfs(vector<pair<uint64_t, uint>> &result, const char *dump2file)
  {
    assert(dump2file != nullptr);
    COUT_DEBUG("save the results to: " << dump2file)

    fstream handle(dump2file, ios_base::out);
    if (!handle.good())
      ERROR("Cannot save the result to `" << dump2file << "'");

    for (const auto &p : result)
    {
      handle << p.first << " ";

      // if  the vertex was not reached, the algorithm sets its distance to < 0
      if (p.second == numeric_limits<uint>::max())
      {
        handle << numeric_limits<int64_t>::max();
      }
      else
      {
        handle << (int64_t)p.second;
      }
      handle << "\n";
    }
    handle.close();
  }

  static vector<pair<uint64_t, uint>> translate_bfs(SnapshotTransaction &tx, pvector<int64_t> &values)
  {
    auto N = values.size();

    vector<pair<vertex_id_t, uint>> logical_result(N);

#pragma omp parallel for
    for (uint v = 0; v < N; v++)
    {
      if (tx.has_vertex_p(v))
      {
        if (values[v] >= 0)
        {
          logical_result[v] = make_pair(tx.logical_id(v), values[v]);
        }
        else
        {
          logical_result[v] = make_pair(tx.logical_id(v), numeric_limits<uint>::max());
        }
      }
      else
      {
        logical_result[v] = make_pair(v, numeric_limits<uint>::max());
      }
    }
    return logical_result;
  }

  void SortledtonDriver::bfs(uint64_t source_vertex_id, const char *dump2file)
  {
    tm.register_thread(0);
    SnapshotTransaction tx = tm.getSnapshotTransaction(ds, false);

    // run_gc();

    auto physical_src = tx.physical_id(source_vertex_id);

    // Timer t;
    // t.start();
    auto distances = GAPBSAlgorithms::bfs(tx, physical_src, false);

    // cout << "BFS took " << t << endl;
    auto external_ids = translate_bfs(tx, distances);
    // cout << "Translation took " << t << endl;
    tm.transactionCompleted(tx);

    if (dump2file != nullptr)
    {
      save_bfs(external_ids, dump2file);
    }
    tm.deregister_thread(0);
  }

  void SortledtonDriver::pagerank(uint64_t num_iterations, double damping_factor, const char *dump2file)
  {
    tm.register_thread(0);
    SnapshotTransaction tx = tm.getSnapshotTransaction(ds, false);

    // run_gc();

    auto pr = PageRank::page_rank_bs(tx, num_iterations, damping_factor);
    ;
    auto external_ids = translate<double>(tx, pr);

    tm.transactionCompleted(tx);

    if (dump2file != nullptr)
    {
      save_result<double>(external_ids, dump2file);
    }
    tm.deregister_thread(0);
  }

  void SortledtonDriver::wcc(const char *dump2file)
  {
    tm.register_thread(0);
    /*SnapshotTransaction tx = tm.getSnapshotTransaction(ds, false);

    run_gc();


    auto clusters = WCC::gapbs_wcc(tx);
    auto external_ids = translate<uint64_t>(tx, clusters);

    tm.transactionCompleted(tx);

    if (dump2file != nullptr) {
      save_result<uint64_t>(external_ids, dump2file);
    }*/
    do_weight_scan();
    tm.deregister_thread(0);
  }
  void SortledtonDriver::do_topology_scan()
  {
    SortledtonDriver *non_const_this = const_cast<SortledtonDriver *>(this);
    SnapshotTransaction tx = non_const_this->tm.getSnapshotTransaction(ds, false);
    const uint64_t max_physical_vertices = ds->max_physical_vertex();
    uint64_t total_num_edges = 0;
    uint64_t total_vid_sum = 0;
    /*if (!tx.has_vertex(source) || !tx.has_vertex(destination)) {
        return nan("");
    }
    weight_t w;
    auto has_edge = tx.get_weight({static_cast<dst_t>(source), static_cast<dst_t>(destination)}, (char *) &w);*/
#pragma omp parallel
    {
      uint64_t local_edge_num = 0;
      uint64_t local_vid_sum = 0;
#pragma omp for
      for (uint64_t v = 0; v < max_physical_vertices; v++)
      {
        VersionedBlockedEdgeIterator _iter = tx.neighbourhood_blocked_p(v);
        while (_iter.has_next_block())
        {
          auto [_versioned, _bs, _be] = _iter.next_block();
          if (_versioned)
          {
            while (_iter.has_next_edge())
            {
              auto dst = _iter.next();
              local_vid_sum += dst;
              local_edge_num++;
            }
          }
          else
          {
            for (auto _i = _bs; _i < _be; _i++)
            {
              auto dst = *_i;
              local_vid_sum += dst;
              local_edge_num++;
            }
          }
        }
      }
#pragma omp critical
      {
        total_num_edges += local_edge_num;
        total_vid_sum += local_vid_sum;
      }
    }
    // VersionedBlockedEdgeIterator _iter = tx.neighbourhood_blocked_p(src);
    non_const_this->tm.transactionCompleted(tx);
    std::cout << "total edge number is " << total_num_edges << std::endl;
    std::cout << "total vids sum is " << total_vid_sum << std::endl;
  }

  void SortledtonDriver::cdlp(uint64_t max_iterations, const char *dump2file)
  {
    tm.register_thread(0);
    /*SnapshotTransaction tx = tm.getSnapshotTransaction(ds, false);

      run_gc();

      auto clusters = CDLP::teseo_cdlp(tx, max_iterations);
      auto external_ids = translate<uint64_t>(tx, clusters);

      tm.transactionCompleted(tx);

      if (dump2file != nullptr) {
        save_result<uint64_t>(external_ids, dump2file);
      }*/
    // run_gc();
    do_topology_scan();
    tm.deregister_thread(0);
  }

  void SortledtonDriver::sssp(uint64_t source_vertex_id, const char *dump2file)
  {
    tm.register_thread(0);
    SnapshotTransaction tx = tm.getSnapshotTransaction(ds, false);

    run_gc();

    auto physical_src = tx.physical_id(source_vertex_id);

    auto distances = SSSP::gabbs_sssp(tx, physical_src, 2.0);

    auto external_ids = translate<double>(tx, distances);

    tm.transactionCompleted(tx);

    if (dump2file != nullptr)
    {
      save_result<double>(external_ids, dump2file);
    }
    tm.deregister_thread(0);
  }

  bool SortledtonDriver::can_be_validated() const
  {
    return true;
  }

  /*****************************************************************************
   *                                                                           *
   *  LCC, sort-merge implementation, taken from Teseo, adapted to Sortledton  *
   *                                                                           *
   *****************************************************************************/
  /**
   * Algorithm parameters
   */
  static const uint64_t LCC_NUM_WORKERS = thread::hardware_concurrency(); // number of workers / logical threads to use
                                                                          // static const uint64_t LCC_NUM_WORKERS = 1; // number of workers / logical threads to use
  static constexpr uint64_t LCC_TASK_SIZE = 1ull << 10;                   // number of vertices processed in each task

  namespace
  {
    class Master
    {
      SnapshotTransaction &ds;           // CSR data structure
      atomic<uint64_t> *m_num_triangles; // number of triangles counted so far for the given vertex, array of num_vertices
      std::atomic<uint64_t> m_next;      // counter to select the next task among the workers

      // Reserve the space in the hash maps m_score and m_state so that they can be operated concurrently by each thread/worker
      void initialise();

      // Compute the final scores
      void compute_scores(vector<double> &scores);

    public:
      // Constructor
      Master(SnapshotTransaction &ds);

      // Destructor
      ~Master();

      // Execute the algorithm
      vector<double> execute();

      // Select the next window to process, in the form [vertex_start, vertex_end);
      // Return true if a window/task has been fetched, false if there are no more tasks to process
      bool next_task(uint64_t *output_vtx_start /* inclusive */, uint64_t *output_vtx_end /* exclusive */);

      // Retrieve the number of triangles associated to the given vertex
      std::atomic<uint64_t> &num_triangles(uint64_t vertex_id);
    };

    class Worker
    {
      TopologyInterface &ds;         // handle to the CSR instance
      Master *m_master;              // handle to the master instance
      thread m_handle;               // underlying thread
      vector<uint64_t> m_neighbours; // neighbours of the vertex to be processed, internal state

      // Process the given vertex
      void process_vertex(uint64_t vertex_id);

    public:
      // Init
      Worker(TopologyInterface &ds, Master *master);

      // Destructor
      ~Worker();

      // Main thread
      void execute();

      // Wait for the worker's thread to terminate
      void join();
    };

    class GFELCC
    {
    public:
      static vector<double> execute(SnapshotTransaction &tx);
    };

    vector<double> GFELCC::execute(SnapshotTransaction &tx)
    {
      Master algorithm(tx);
      return algorithm.execute();
    }

    //        friend class Master;
    //
    //        friend class Worker;

    /*****************************************************************************
     *                                                                           *
     *  LCC_Master                                                               *
     *                                                                           *
     *****************************************************************************/
    Master::Master(SnapshotTransaction &ds) : ds(ds), m_num_triangles(nullptr), m_next(0)
    {
    }

    Master::~Master()
    {
      delete[] m_num_triangles;
      m_num_triangles = nullptr;
    }

    void Master::initialise()
    {
      assert(m_num_triangles == nullptr && "Already initialised");
      m_num_triangles = new atomic<uint64_t>[ds.vertex_count()](); // init to 0;
    }

    void Master::compute_scores(vector<double> &scores)
    {
      for (uint64_t i = 0, N = ds.max_physical_vertex(); i < N; i++)
      {
        if (ds.has_vertex_p(i))
        {
          uint64_t num_triangles = m_num_triangles[i];
          if (num_triangles > 0)
          {
            uint64_t degree = ds.neighbourhood_size_p(i);
            uint64_t max_num_edges = degree * (degree - 1);
            double score = static_cast<double>(num_triangles) / max_num_edges;
            scores[i] = score;
          } // else m_scores[i] = 0 (default value)
        }
      }
    }

    vector<double> Master::execute()
    {
      // init the state and the side information for each vertex
      initialise();

      // start the workers
      assert(LCC_NUM_WORKERS >= 1 && "At least one worker should be set");
      vector<Worker *> workers;
      workers.reserve(LCC_NUM_WORKERS);
      for (uint64_t worker_id = 0; worker_id < LCC_NUM_WORKERS; worker_id++)
      {
        workers.push_back(new Worker(ds, this));
      }

      // wait for the workers to terminate ...
      for (uint64_t worker_id = 0; worker_id < workers.size(); worker_id++)
      {
        workers[worker_id]->join();
        delete workers[worker_id];
        workers[worker_id] = nullptr;
      }

      vector<double> scores;
      scores.resize(ds.vertex_count());
      compute_scores(scores);
      return scores;
    }

    bool Master::next_task(uint64_t *output_vtx_start /* inclusive */,
                           uint64_t *output_vtx_end /* exclusive */)
    {
      uint64_t logical_start = m_next.fetch_add(LCC_TASK_SIZE); /* return the previous value of m_next */
      uint64_t num_vertices = ds.vertex_count();
      if (logical_start >= num_vertices)
      {
        return false;
      }
      else
      {
        uint64_t logical_end = std::min(logical_start + LCC_TASK_SIZE, num_vertices);

        *output_vtx_start = logical_start;
        *output_vtx_end = logical_end;

        return true;
      }
    }

    atomic<uint64_t> &Master::num_triangles(uint64_t vertex_id)
    {
      return m_num_triangles[vertex_id];
    }

    Worker::Worker(TopologyInterface &ds, Master *master) : ds(ds), m_master(master)
    {
      m_handle = thread{&Worker::execute, this};
    }

    Worker::~Worker() {}

    void Worker::execute()
    {
      uint64_t v_start, v_end;
      while (m_master->next_task(&v_start, &v_end))
      {
        for (uint64_t v = v_start; v < v_end; v++)
        {
          process_vertex(v);
        }
      }
    }

    void Worker::join()
    {
      m_handle.join();
    }

    void Worker::process_vertex(uint64_t n1)
    {
      uint64_t num_triangles = 0; // current number of triangles found for `n1'
      m_neighbours.clear();

      SORTLEDTON_ITERATE_NAMED(ds, n1, n2, end_1, {
        if (n2 > n1)
          goto end_1; // we're done with n1

        m_neighbours.push_back(n2);
        uint64_t marker = 0; // current position in the neighbours vector, to merge shared neighbours

        SORTLEDTON_ITERATE_NAMED(ds, n2, n3, end_2, {
          if (n3 > n2)
            goto end_2; // we're done with n2
          assert(n1 > n2 &&
                 n2 > n3); // we're looking for triangles of the kind c - b - a, with c > b && b > a

          if (n3 > m_neighbours[marker])
          { // merge with m_neighbours
            do
            {
              marker++;
            } while (marker < m_neighbours.size() && n3 > m_neighbours[marker]);
            if (marker >= m_neighbours.size())
              break; // there is nothing left to merge
          }

          if (n3 == m_neighbours[marker])
          {                     // match !
            num_triangles += 2; // we've discovered both n1 - n2 - n3 and n1 - n3 - n2; with n1 > n2 > n3

            // increase the contribution for n2
            m_master->num_triangles(n2) += 2;
            // increase the contribution for n3
            m_master->num_triangles(n3) += 2;

            marker++;
            if (marker >= m_neighbours.size())
              goto end_2; // there is nothing left to merge
          }
        });
      });

      if (num_triangles != 0)
      {
        m_master->num_triangles(n1) += num_triangles;
      }
    }
  }

  void SortledtonDriver::lcc(const char *dump2file)
  {
    tm.register_thread(0);
    SnapshotTransaction tx = tm.getSnapshotTransaction(ds, false);

    run_gc();

    auto lcc_values = GFELCC::execute(tx);

    //      auto lcc_values = LCC::lcc_merge_sort(tx);
    auto external_ids = translate<double>(tx, lcc_values);

    tm.transactionCompleted(tx);

    if (dump2file != nullptr)
    {
      save_result<double>(external_ids, dump2file);
    }
    tm.deregister_thread(0);
  }
  void SortledtonDriver::do_weight_scan()
  {
    SortledtonDriver *non_const_this = const_cast<SortledtonDriver *>(this);
    SnapshotTransaction tx = non_const_this->tm.getSnapshotTransaction(ds, false);
    const uint64_t max_physical_vertices = ds->max_physical_vertex();
    uint64_t total_num_edges = 0;
    uint64_t total_vid_sum = 0;
    double total_weight = 0;
#pragma omp parallel
    {
      uint64_t local_edge_num = 0;
      uint64_t local_vid_sum = 0;
      double local_weight = 0;
#pragma omp for
      for (uint64_t v = 0; v < max_physical_vertices; v++)
      {

        VersionedBlockedPropertyEdgeIterator _iter = tx.neighbourhood_with_properties_blocked_p(v);
        while (_iter.has_next_block())
        {
          auto [_versioned, _bs, _be, _ws, _we] = _iter.next_block_with_properties();
          if (_versioned)
          {
            while (_iter.has_next_edge())
            {
              auto [edge_name, properties_name] = _iter.next_with_properties();
              local_edge_num++;
              local_vid_sum += edge_name;
              local_weight += properties_name;
            }
          }
          else
          {
            auto _p = _ws;
            for (auto _i = _bs; _i < _be; _i++)
            {
              auto edge_name = *_i;
              auto properties_name = *_p;
              _p++;
              local_edge_num++;
              local_vid_sum += edge_name;
              local_weight += properties_name;
            }
          }
        }
      }
#pragma omp critical
      {
        total_num_edges += local_edge_num;
        total_vid_sum += local_vid_sum;
        total_weight += local_weight;
      }
    }
    non_const_this->tm.transactionCompleted(tx);
    std::cout << "total edge number is " << total_num_edges << std::endl;
    std::cout << "total vids sum is " << total_vid_sum << std::endl;
    std::cout << "total weight is " << total_weight << std::endl;
  }

  // two hop neighbors
  void SortledtonDriver::generate_two_hops_neighbor_candidates(std::vector<uint64_t> &vertices)
  {
    vertices.resize(two_hop_neighbor_size);
    const uint64_t max_physical_vertices = ds->max_physical_vertex();
    std::unordered_set<uint64_t> unique_vertices;
    for (uint64_t i = 0; i < two_hop_neighbor_size; i++)
    {
      uint64_t vid = rand() % max_physical_vertices;
      auto result = unique_vertices.emplace(vid);
      if (result.second)
      {
        vertices[i] = vid;
      }
      else
      {
        i--;
      }
    }
    std::sort(vertices.begin(), vertices.end());
  }

  void SortledtonDriver::one_hop_neighbors(std::vector<uint64_t> &vertices){
       tm.register_thread(0);
    std::unordered_map<uint64_t, std::vector<uint64_t>> results;
    results.reserve(two_hop_neighbor_size);
    SortledtonDriver *non_const_this = const_cast<SortledtonDriver *>(this);
    SnapshotTransaction tx = non_const_this->tm.getSnapshotTransaction(ds, false);
    for (auto source : vertices)
    {
      results[source];
    }
#pragma omp parallel for
    for (auto source : vertices)
    {
      uint64_t neighbor_size = tx.neighbourhood_size_p(source);
      auto &neighbors = results[source];
      if(neighbor_size > neighbors.max_size())
        continue;
      neighbors.reserve(neighbor_size);
      // neighbors.reserve();
      {
        VersionedBlockedEdgeIterator _iter = tx.neighbourhood_blocked_p(source);
        // source neighbor
        while (_iter.has_next_block())
        {
          auto [_versioned, _bs, _be] = _iter.next_block();
          if (_versioned)
          {
            while (_iter.has_next_edge())
            {
              auto dst = _iter.next();
              //neighbor_size+=tx.neighbourhood_size_p(dst);
              neighbors.emplace_back(dst);
            }
          }
          else
          {
            for (auto _i = _bs; _i < _be; _i++)
            {
               auto dst = *_i;
              //neighbor_size+=tx.neighbourhood_size_p(dst);
              neighbors.emplace_back(dst);
            }
          }
        }
      }
    }
    non_const_this->tm.transactionCompleted(tx);
    tm.deregister_thread(0);
  }

  void SortledtonDriver::two_hop_neighbors(std::vector<uint64_t> &vertices)
  {
    tm.register_thread(0);
    std::unordered_map<uint64_t, std::vector<uint64_t>> results;
    results.reserve(two_hop_neighbor_size);
    SortledtonDriver *non_const_this = const_cast<SortledtonDriver *>(this);
    SnapshotTransaction tx = non_const_this->tm.getSnapshotTransaction(ds, false);
    for (auto source : vertices)
    {
      results[source];
    }
#pragma omp parallel for
    for (auto source : vertices)
    {
      uint64_t neighbor_size = tx.neighbourhood_size_p(source);
      auto &neighbors = results[source];
      neighbors.reserve(neighbor_size);
      std::vector<uint64_t> hop_1_neighbors;
      // neighbors.reserve();
      {
        VersionedBlockedEdgeIterator _iter = tx.neighbourhood_blocked_p(source);
        // source neighbor
        while (_iter.has_next_block())
        {
          auto [_versioned, _bs, _be] = _iter.next_block();
          if (_versioned)
          {
            while (_iter.has_next_edge())
            {
              auto dst = _iter.next();
              //neighbor_size+=tx.neighbourhood_size_p(dst);
              neighbors.emplace_back(dst);
              hop_1_neighbors.emplace_back(dst);
            }
          }
          else
          {
            for (auto _i = _bs; _i < _be; _i++)
            {
               auto dst = *_i;
              //neighbor_size+=tx.neighbourhood_size_p(dst);
              neighbors.emplace_back(dst);
              hop_1_neighbors.emplace_back(dst);
            }
          }
        }
      }
      #if USING_NIGHBOR_SIZE
      //has to do it outside to avoid deadlocks
      for(auto hop1_neighbor : hop_1_neighbors){
        neighbor_size+=tx.neighbourhood_size_p(hop1_neighbor);
      }
      neighbors.reserve(neighbor_size);
      #endif
      // std::sort(hop_1_neighbors.begin(), hop_1_neighbors.end());
      //  hop1 neighbors' neighbors
      for (auto hop1_neighbor : hop_1_neighbors)
      {
        VersionedBlockedEdgeIterator neighbor_iter = tx.neighbourhood_blocked_p(hop1_neighbor);
        while (neighbor_iter.has_next_block())
        {
          auto [_versioned, _bs, _be] = neighbor_iter.next_block();
          if (_versioned)
          {
            while (neighbor_iter.has_next_edge())
            {
              auto dst = neighbor_iter.next();
              if (dst != source)
                neighbors.emplace_back(dst);
            }
          }
          else
          {
            for (auto _i = _bs; _i < _be; _i++)
            {
              auto dst = *_i;
              if (dst != source)
                neighbors.emplace_back(dst);
            }
          }
        }
      }
    }

    non_const_this->tm.transactionCompleted(tx);
    tm.deregister_thread(0);
    // omp ends
    //std::cout << "two hop neighbors finished" << std::endl;
  }
}