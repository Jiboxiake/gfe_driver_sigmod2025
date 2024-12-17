//
// Created by zhou822 on 6/23/23.
//

//
// Created by zhou822 on 6/21/23.
//

#include "gtx_driver.hpp"

#include <atomic>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <mutex>
#include <limits>
#include <sstream>
#include <thread>
#include <unordered_set>

#include "../../third-party/libcommon/include/lib/common/system.hpp"
#include "../../third-party/libcommon/include/lib/common/timer.hpp"
#include "tbb/concurrent_hash_map.h"
#include "../../third-party/gapbs/gapbs.hpp"
#include "../../third-party/libcuckoo/cuckoohash_map.hh"
#include "GTX.hpp"
#include "../../utility/timeout_service.hpp"

using namespace common;
using namespace libcuckoo;
using namespace std;

#define GTX reinterpret_cast<gt::Graph*>(m_pImpl)
using vertex_dictionary_t = tbb::concurrent_hash_map<uint64_t, gt::vertex_t>;
#define VertexDictionary reinterpret_cast<vertex_dictionary_t*>(m_pHashMap)

/*****************************************************************************
 *                                                                           *
 *  Debug                                                                    *
 *                                                                           *
 *****************************************************************************/
//#define DEBUG
namespace gfe { extern mutex _log_mutex [[maybe_unused]]; }
#define COUT_DEBUG_FORCE(msg) { std::scoped_lock<std::mutex> lock{::gfe::_log_mutex}; std::cout << "[GTXDriver::" << __FUNCTION__ << "] [Thread #" << common::concurrency::get_thread_id() << "] " << msg << std::endl; }
#if defined(DEBUG)
//#define COUT_DEBUG(msg) COUT_DEBUG_FORCE(msg)
#else
#define COUT_DEBUG(msg)
#endif

namespace gfe::library {
    GTXDriver::GTXDriver(bool is_directed, bool read_only):m_pImpl(nullptr), m_pHashMap(nullptr), m_is_directed(is_directed),m_read_only(read_only) {
        m_pImpl = new gt::Graph();
        m_pHashMap = new tbb::concurrent_hash_map<uint64_t, /* vertex_t */ uint64_t>();
    }

    GTXDriver::~GTXDriver() noexcept {
        delete GTX; m_pImpl = nullptr;
        delete VertexDictionary; m_pHashMap = nullptr;
    }

    void GTXDriver::set_worker_thread_num(uint64_t new_num) {

        GTX->set_worker_thread_num(new_num);
        std::cout<<"set GTX worker thread num "<<new_num<<std::endl;
    }
    void GTXDriver::on_edge_writes_finish(){
        GTX->print_garbage_queue_status();
    }
    void GTXDriver::thread_exit(){
        GTX->thread_exit();
    }
    void GTXDriver::on_openmp_workloads_finish() {
        GTX->on_openmp_workloads_finish();
    }
    void GTXDriver::configure_distinct_reader_and_writer_threads(uint64_t reader_num, uint64_t writer_num) {
        GTX->configure_distinct_readers_and_writers(reader_num,writer_num);
    }

    void GTXDriver::mixed_workload_finish_loading(){
        GTX->on_finish_loading();
    }

    void GTXDriver::print_and_clear_txn_stats() {
        GTX->print_and_clear_txn_stats();
    }

    bool GTXDriver::is_directed() const {
        return m_is_directed;
    }

    uint64_t GTXDriver::num_edges() const {
        return m_num_edges;
    }

    uint64_t GTXDriver::num_vertices() const {
        return m_num_vertices;
    }

    void GTXDriver::set_timeout(uint64_t seconds) {
        m_timeout = chrono::seconds{ seconds };
    }

    void* GTXDriver::gtx(){
        return m_pImpl;
    }

    void* GTXDriver::vertex_dictionary() {
        return m_pHashMap;
    }

    void GTXDriver::analytical_workload_end(){
       // GTX->on_read_workload_finishing
    }

    void GTXDriver::finish_loading() {
        GTX->whole_label_graph_eager_consolidation(1);
    }
    uint64_t GTXDriver::ext2int(uint64_t external_vertex_id) const {
        vertex_dictionary_t::const_accessor accessor;
        if ( VertexDictionary->find(accessor, external_vertex_id ) ){
            return accessor->second;
        } else {
            std::cout<<"unable to find the vertex "<<external_vertex_id<<std::endl;
            //ERROR("The given vertex does not exist: " << external_vertex_id);
            throw std::runtime_error("force crash");
            return 0;
        }
    }
    //todo: check what should we do to support both read only and rw transaction here
    uint64_t GTXDriver::int2ext(void* opaque_transaction, uint64_t internal_vertex_id) const {
        //todo: if read_only, else
        if(m_read_only){
            auto transaction = reinterpret_cast<gt::SharedROTransaction*>(opaque_transaction);
            string_view payload = transaction->get_vertex(internal_vertex_id);//they store external vid in the vertex data for experiments
            if(payload.empty()){ // the vertex does not exist
                return numeric_limits<uint64_t>::max();
            } else {
                return *(reinterpret_cast<const uint64_t*>(payload.data()));
            }
        }else{
            //auto transaction = reinterpret_cast<gt::RWTransaction*>(opaque_transaction);
            //todo:: libin changed it here, just always use this one for our experiment?
            auto transaction = reinterpret_cast<gt::SharedROTransaction*>(opaque_transaction);
            string_view payload = transaction->get_vertex(internal_vertex_id);//they store external vid in the vertex data for experiments
            if(payload.empty()){ // the vertex does not exist
                return numeric_limits<uint64_t>::max();
            } else {
                return *(reinterpret_cast<const uint64_t*>(payload.data()));
            }
        }
    }

    uint64_t GTXDriver::int2ext_openmp(void *opaque_transaction, uint64_t internal_vertex_id, uint8_t thread_id) const {
        auto transaction = reinterpret_cast<gt::SharedROTransaction*>(opaque_transaction);
        string_view payload = transaction->get_vertex(internal_vertex_id,thread_id);//they store external vid in the vertex data for experiments
        if(payload.empty()){ // the vertex does not exist
            return numeric_limits<uint64_t>::max();
        } else {
            return *(reinterpret_cast<const uint64_t*>(payload.data()));
        }
    }
    bool GTXDriver::add_vertex(uint64_t external_id) {
        vertex_dictionary_t::accessor accessor; // xlock
        bool inserted = VertexDictionary->insert(accessor, external_id);
        if ( inserted ){
            gt::vertex_t internal_id = 0;
            bool done = false;
            do {
                auto tx = GTX->begin_read_write_transaction();
                try {
                    internal_id = tx.new_vertex();
                    string_view data { (char*) &external_id, sizeof(external_id) };
                    tx.put_vertex(internal_id, data);
                    tx.commit();
                    done = true;
                } catch(gt::RollbackExcept& e){
                    tx.abort();
                    COUT_DEBUG("Rollback, vertex id: " << external_id);
                    // retry ...
                }
            } while(!done);

            accessor->second = internal_id;
            m_num_vertices++;
        }
        return inserted;
    }
    //todo:: currently gtx did not implement delete vertex, it should be much more complicated
    bool GTXDriver::remove_vertex(uint64_t vertex_id) {
        m_num_vertices --;
        return true;
    }

    bool GTXDriver::has_vertex(uint64_t vertex_id) const {
        vertex_dictionary_t::const_accessor accessor;
        return VertexDictionary->find(accessor, vertex_id);
    }

    bool GTXDriver::add_edge(gfe::graph::WeightedEdge e) {
        vertex_dictionary_t::const_accessor accessor1, accessor2;  // shared lock on the dictionary
        if(!VertexDictionary->find(accessor1, e.source())){ return false; }
        if(!VertexDictionary->find(accessor2, e.destination())) { return false; }
        gt::vertex_t internal_source_id = accessor1->second;
        gt::vertex_t internal_destination_id = accessor2->second;

        bool done = false;
        do {
            auto tx = GTX->begin_read_write_transaction();
            try {

                // insert the new edge only if it doesn't already exist
                auto bg_weight = tx.get_edge(internal_source_id, 1, internal_destination_id);
                if(bg_weight.size() > 0){ // the edge already exists
                    tx.abort();
                    return false;
                }

                string_view weight { (char*) &e.m_weight, sizeof(e.weight()) };
                //gtx label is at least 1
                tx.put_edge(internal_source_id, /* label */ 1, internal_destination_id, weight);
                if(!m_is_directed){ // undirected graph
                    // We follow the same convention given by G. Feng, author of the LiveGraph paper,
                    // for his experiments in the LDBC SNB Person knows Person: undirected edges
                    // are added twice as a -> b and b -> a
                    tx.put_edge(internal_destination_id, /* label */ 1, internal_source_id, weight);
                }

                if(tx.commit()){
                    m_num_edges++;
                    done = true;
                }
            } catch (gt::RollbackExcept& exc){
                tx.abort();
                COUT_DEBUG("Rollback, edge: " << e);
                // retry ...
            }
        } while(!done);

        return true;
    }

    bool GTXDriver::add_edge_v2(gfe::graph::WeightedEdge edge){
        //for the experiment comparing with sortledton and teseo, we just assume it has to be undirected.
        uint64_t internal_source_id = numeric_limits<uint64_t>::max();
        uint64_t internal_destination_id = 0;
        bool insert_source = false;
        bool insert_destination = false;
        vertex_dictionary_t::const_accessor slock1, slock2;
        vertex_dictionary_t::accessor xlock1, xlock2;
        bool result = false;
        if(VertexDictionary->find(slock1, edge.m_source)){ // insert the vertex e.m_source
            internal_source_id = slock1->second;
        } else {
            slock1.release();
            if ( VertexDictionary->insert(xlock1, edge.m_source) ) {
                insert_source = true;
            } else {
                internal_source_id = xlock1->second;
            }
        }

        if(VertexDictionary->find(slock2, edge.m_destination)){ // insert the vertex e.m_destination
            internal_destination_id = slock2->second;
        } else {
            slock2.release();
            if( VertexDictionary->insert(xlock2, edge.m_destination) ) {
                insert_destination = true;
            } else {
                internal_destination_id = xlock2->second;
            }
        }

        bool done = false;
        do {
            auto tx = GTX->begin_read_write_transaction();
            try {
                // create the vertices in GTX
                if(insert_source){
                    internal_source_id = tx.new_vertex();
                    string_view data { (char*) &edge.m_source, sizeof(edge.m_source) };
                    tx.put_vertex(internal_source_id, data);
                }
                if(insert_destination){
                    internal_destination_id = tx.new_vertex();
                    string_view data { (char*) &edge.m_destination, sizeof(edge.m_destination) };
                    tx.put_vertex(internal_destination_id, data);
                }

                // insert the edge
                string_view weight { (char*) &edge.m_weight, sizeof(edge.m_weight) };
                //string weight = "weight";//todo:: change this back
                //tx.put_edge(internal_source_id, /* label */ 1, internal_destination_id, weight);
                result = tx.checked_put_edge(internal_source_id, /* label */ 1, internal_destination_id, weight);

                //commented because checked_put_edge covers both directions already
                //if(!m_is_directed&&result){
                    // a) In directed graphs, we register the incoming edges with label 1
                    // b) In undirected graphs, we follow the same convention given by G. Feng, author
                    // of the LiveGraph paper, for his experiments in the LDBC SNB Person knows Person:
                    // undirected edges are added twice as a -> b and b -> a
                    //gt::label_t label = 1;
                    //tx.put_edge(internal_destination_id, /* label */ 1, internal_source_id, weight);
                //    result&=tx.checked_put_edge(internal_destination_id, /* label */ 1, internal_source_id, weight);
                //}
                if(tx.commit()){
                    if(result)
                        m_num_edges++;
                    done = true;
                }
            } catch (gt::RollbackExcept& e){
                tx.abort();
                // retry ...
            }
        } while(!done);

        if(insert_source){
            //assert(internal_source_id != numeric_limits<uint64_t>::max());
            xlock1->second = internal_source_id;
            m_num_vertices++;
        }
        if(insert_destination){
            //assert(internal_destination_id != numeric_limits<uint64_t>::max());
            xlock2->second = internal_destination_id;
            m_num_vertices++;
        }

        return result;
    }
/*
 * gtx version is the same as v2
 */
    bool GTXDriver::add_edge_v3(gfe::graph::WeightedEdge edge){
        uint64_t internal_source_id = numeric_limits<uint64_t>::max();
        uint64_t internal_destination_id = 0;
        bool insert_source = false;
        bool insert_destination = false;
        vertex_dictionary_t::const_accessor slock1, slock2;
        vertex_dictionary_t::accessor xlock1, xlock2;
        bool result = false;
        if(VertexDictionary->find(slock1, edge.m_source)){ // insert the vertex e.m_source
            internal_source_id = slock1->second;
        } else {
            slock1.release();
            if ( VertexDictionary->insert(xlock1, edge.m_source) ) {
                insert_source = true;
            } else {
                internal_source_id = xlock1->second;
            }
        }

        if(VertexDictionary->find(slock2, edge.m_destination)){ // insert the vertex e.m_destination
            internal_destination_id = slock2->second;
        } else {
            slock2.release();
            if( VertexDictionary->insert(xlock2, edge.m_destination) ) {
                insert_destination = true;
            } else {
                internal_destination_id = xlock2->second;
            }
        }

        bool done = false;
        do {
            auto tx = GTX->begin_read_write_transaction();
            try {
                // create the vertices in GTX
                if(insert_source){
                    internal_source_id = tx.new_vertex();
                    string_view data { (char*) &edge.m_source, sizeof(edge.m_source) };
                    tx.put_vertex(internal_source_id, data);
                }
                if(insert_destination){
                    internal_destination_id = tx.new_vertex();
                    string_view data { (char*) &edge.m_destination, sizeof(edge.m_destination) };
                    tx.put_vertex(internal_destination_id, data);
                }

                // insert the edge
                string_view weight { (char*) &edge.m_weight, sizeof(edge.m_weight) };
                //string weight = "weight";//todo:: change this back
                //tx.put_edge(internal_source_id, /* label */ 1, internal_destination_id, weight);
                result = tx.checked_put_edge(internal_source_id, /* label */ 1, internal_destination_id, weight);

                if(!m_is_directed&&result){
                    // a) In directed graphs, we register the incoming edges with label 1
                    // b) In undirected graphs, we follow the same convention given by G. Feng, author
                    // of the LiveGraph paper, for his experiments in the LDBC SNB Person knows Person:
                    // undirected edges are added twice as a -> b and b -> a
                    //gt::label_t label = 1;
                    //tx.put_edge(internal_destination_id, /* label */ 1, internal_source_id, weight);
                    result&=tx.checked_put_edge(internal_destination_id, /* label */ 1, internal_source_id, weight);
                }
                if(tx.commit()){
                    if(result)
                        m_num_edges++;
                    done = true;
                }
            } catch (gt::RollbackExcept& e){
                tx.abort();
                // retry ...
            }
        } while(!done);

        if(insert_source){
            assert(internal_source_id != numeric_limits<uint64_t>::max());
            xlock1->second = internal_source_id;
            m_num_vertices++;
        }
        if(insert_destination){
            assert(internal_destination_id != numeric_limits<uint64_t>::max());
            xlock2->second = internal_destination_id;
            m_num_vertices++;
        }

        return true;
    }

    /*
     * lookup the edge, and then insert/update
     */
    bool GTXDriver::update_edge_v1(gfe::graph::WeightedEdge edge) {
        uint64_t internal_source_id = numeric_limits<uint64_t>::max();
        uint64_t internal_destination_id = 0;
        bool insert_source = false;
        bool insert_destination = false;
        vertex_dictionary_t::const_accessor slock1, slock2;
        vertex_dictionary_t::accessor xlock1, xlock2;
        bool result = false;
        if(VertexDictionary->find(slock1, edge.m_source)){ // insert the vertex e.m_source
            internal_source_id = slock1->second;
        } else {
            slock1.release();
            if ( VertexDictionary->insert(xlock1, edge.m_source) ) {
                insert_source = true;
            } else {
                internal_source_id = xlock1->second;
            }
        }

        if(VertexDictionary->find(slock2, edge.m_destination)){ // insert the vertex e.m_destination
            internal_destination_id = slock2->second;
        } else {
            slock2.release();
            if( VertexDictionary->insert(xlock2, edge.m_destination) ) {
                insert_destination = true;
            } else {
                internal_destination_id = xlock2->second;
            }
        }

        bool done = false;
        bool need_check = true;
        do {
            auto tx = GTX->begin_read_write_transaction();
            double update_weight = edge.m_weight;
            try {
                // create the vertices in GTX
                if(insert_source){
                    internal_source_id = tx.new_vertex();
                    string_view data { (char*) &edge.m_source, sizeof(edge.m_source) };
                    tx.put_vertex(internal_source_id, data);
                    need_check= false;
                }
                if(insert_destination){
                    internal_destination_id = tx.new_vertex();
                    string_view data { (char*) &edge.m_destination, sizeof(edge.m_destination) };
                    tx.put_vertex(internal_destination_id, data);
                    need_check= false;
                }
                if(need_check){
                    std::string_view read_result = tx.get_edge(internal_source_id,internal_destination_id,1);
                    if(!read_result.empty()){
                        double w = *reinterpret_cast<const double*>(read_result.data());
                        update_weight+= w;
                    }
                }
                // insert the edge
                string_view weight { (char*) &update_weight, sizeof(update_weight) };
                //string weight = "weight";//todo:: change this back
                //tx.put_edge(internal_source_id, /* label */ 1, internal_destination_id, weight);
                result = tx.checked_put_edge(internal_source_id, /* label */ 1, internal_destination_id, weight);

                if(!m_is_directed&&result){
                    // a) In directed graphs, we register the incoming edges with label 1
                    // b) In undirected graphs, we follow the same convention given by G. Feng, author
                    // of the LiveGraph paper, for his experiments in the LDBC SNB Person knows Person:
                    // undirected edges are added twice as a -> b and b -> a
                    //gt::label_t label = 1;
                    //tx.put_edge(internal_destination_id, /* label */ 1, internal_source_id, weight);
                    result&=tx.checked_put_edge(internal_destination_id, /* label */ 1, internal_source_id, weight);
                }
                if(tx.commit()){
                    if(result)
                        m_num_edges++;
                    done = true;
                }
            } catch (gt::RollbackExcept& e){
                tx.abort();
                // retry ...
            }
        } while(!done);

        if(insert_source){
            assert(internal_source_id != numeric_limits<uint64_t>::max());
            xlock1->second = internal_source_id;
            m_num_vertices++;
        }
        if(insert_destination){
            assert(internal_destination_id != numeric_limits<uint64_t>::max());
            xlock2->second = internal_destination_id;
            m_num_vertices++;
        }
        return true;
    }

    bool GTXDriver::remove_edge(gfe::graph::Edge e){
        vertex_dictionary_t::const_accessor slock1, slock2;
        if(!VertexDictionary->find(slock1, e.source())){ return false; }
        if(!VertexDictionary->find(slock2, e.destination())){ return false; }
        gt::vertex_t internal_source_id = slock1->second;
        gt::vertex_t internal_destination_id = slock2->second;

        while(true){
            auto tx = GTX->begin_read_write_transaction();
            try {
                ///*bool removed =*/ tx.delete_edge(internal_source_id, /* label */ 1, internal_destination_id);
                //if(/*removed &&*/ !m_is_directed){ // undirected graph
                //    tx.delete_edge(internal_destination_id, /* label */ 1, internal_source_id);
                //}
                bool removed = tx.checked_delete_edge(internal_source_id, /* label */ 1, internal_destination_id);

                //checked_delete_edge covers both directions already
                //if(removed && !m_is_directed){ // undirected graph
                //    removed&=tx.checked_delete_edge(internal_destination_id, /* label */ 1, internal_source_id);
                //}
                if(tx.commit()){
                   if(removed){
                       m_num_edges--;
                       return true;
                   }else{
                       return false;
                   }
                }
            } catch(gt::RollbackExcept& e){
                tx.abort();
                // retry ...
            }
        }
    }

    double GTXDriver::get_weight(uint64_t source, uint64_t destination) const {
        // check whether the referred vertices exist
        vertex_dictionary_t::const_accessor slock1, slock2;
        if(!VertexDictionary->find(slock1, source)){ return numeric_limits<double>::signaling_NaN(); }
        if(!VertexDictionary->find(slock2, destination)){ return numeric_limits<double>::signaling_NaN(); }
        gt::vertex_t internal_source_id = slock1->second;
        gt::vertex_t internal_destination_id = slock2->second;

        auto tx = GTX->begin_read_only_transaction();
       /*string_view bg_weight = tx.get_edge(internal_source_id, internal_destination_id, 1);
        double weight = numeric_limits<double>::signaling_NaN();
        if(bg_weight.size() > 0){ // the edge exists
            weight = *(reinterpret_cast<const double*>(bg_weight.data()));
        }*/
        auto weight = tx.get_edge_weight(internal_source_id, internal_destination_id, 1);
        tx.commit(); //read-only txn should not abort in gtx
        return weight;
    }

    /*****************************************************************************
    *                                                                           *
    *  Dump                                                                     *
    *                                                                           *
    *****************************************************************************/
    void GTXDriver::dump_ostream(std::ostream& out) const {
        out << "[LiveGraph] num vertices: " << m_num_vertices << ", num edges: " << m_num_edges << ", "
                                                                                                   "directed graph: " << boolalpha << is_directed() << ", read only txn for graphalytics: " << m_read_only << endl;
        auto tx = GTX->begin_read_only_transaction();
        const uint64_t max_vertex_id = GTX->get_max_allocated_vid();
        for(uint64_t internal_source_id = 1; internal_source_id <= max_vertex_id; internal_source_id++){
            auto bg_external_id = tx.get_vertex(internal_source_id);
            if(bg_external_id.size() == 0) continue; // the vertex has been deleted
            uint64_t external_id = *((uint64_t*) bg_external_id.data() );
            out << "[" << internal_source_id << ", external_id: " << external_id << "]";
            { // outgoing edges
                out << " outgoing edges: ";
                auto it = tx.get_edges(internal_source_id, 1);
                bool first = true;
                while(it.valid()){
                    if(first){ first = false; } else { out << ", "; }
                    uint64_t internal_destination_id = it.dst_id();
                    auto bg_weight = it.edge_delta_data();
                    double weight = * ((double*) bg_weight.data());
                    out << "<" << internal_destination_id << " [external: " << int2ext(&tx, internal_destination_id) << "], " << weight << ">";
                    it.next();
                }
            }
            out << endl;
        }
        tx.commit(); // commit() fires the exception `The transaction is read-only without cache.'
    }

    /*****************************************************************************
    *                                                                           *
    *  Graphalytics Helpers                                                     *
    *                                                                           *
    *****************************************************************************/
    template <typename T>
    vector<pair<uint64_t, T>> GTXDriver::translate(void* /* transaction object */ opaque_transaction, const T* __restrict data, uint64_t data_sz) {
        assert(opaque_transaction != nullptr && "Transaction object not specified");
        vector<pair<uint64_t, T>> output(data_sz);
        auto transaction = reinterpret_cast<gt::SharedROTransaction*>(opaque_transaction);
       /* for(uint64_t logical_id = 1; logical_id <= data_sz; logical_id++){
            uint64_t external_id = int2ext(transaction, logical_id);
            if(external_id == numeric_limits<uint64_t>::max()) { // the vertex does not exist
                output[logical_id-1] = make_pair(numeric_limits<uint64_t>::max(), numeric_limits<T>::max()); // special marker
            } else {
                output[logical_id-1] = make_pair(external_id, data[logical_id-1]);
            }
        }*/
        auto graph = transaction->get_graph();
#pragma omp parallel
        {
            uint8_t thread_id = graph->get_openmp_worker_thread_id();
#pragma omp for
            for(uint64_t logical_id = 1; logical_id <= data_sz; logical_id++){
                string_view payload = transaction->get_vertex(logical_id,thread_id);//they store external vid in the vertex data for experiments
                if(payload.empty()){ // the vertex does not exist
                    output[logical_id-1] = make_pair(numeric_limits<uint64_t>::max(), numeric_limits<T>::max());
                } else {
                    output[logical_id-1] = make_pair(*(reinterpret_cast<const uint64_t*>(payload.data())), data[logical_id-1]);
                }
            }
            transaction->thread_on_openmp_section_finish(thread_id);
        }
        graph->on_openmp_section_finishing();
        return output;
    }

    template<typename T>
    std::vector<std::pair<uint64_t, T>>
    GTXDriver::static_translate(void *opaque_transaction, const T *__restrict data, uint64_t data_sz) {
        assert(opaque_transaction != nullptr && "Transaction object not specified");
        vector<pair<uint64_t, T>> output(data_sz);
        auto transaction = reinterpret_cast<gt::SharedROTransaction*>(opaque_transaction);
        /* for(uint64_t logical_id = 1; logical_id <= data_sz; logical_id++){
             uint64_t external_id = int2ext(transaction, logical_id);
             if(external_id == numeric_limits<uint64_t>::max()) { // the vertex does not exist
                 output[logical_id-1] = make_pair(numeric_limits<uint64_t>::max(), numeric_limits<T>::max()); // special marker
             } else {
                 output[logical_id-1] = make_pair(external_id, data[logical_id-1]);
             }
         }*/
#pragma omp parallel
        {
#pragma omp for
            for(uint64_t logical_id = 1; logical_id <= data_sz; logical_id++){
                string_view payload = transaction->static_get_vertex(logical_id);//they store external vid in the vertex data for experiments
                if(payload.empty()){ // the vertex does not exist
                    output[logical_id-1] = make_pair(numeric_limits<uint64_t>::max(), numeric_limits<T>::max());
                } else {
                    output[logical_id-1] = make_pair(*(reinterpret_cast<const uint64_t*>(payload.data())), data[logical_id-1]);
                }
            }
        }
        return output;
    }

    template <typename T, bool negative_scores>
    void GTXDriver::save_results(const vector<pair<uint64_t, T>>& result, const char* dump2file) {
        assert(dump2file != nullptr);
        COUT_DEBUG("save the results to: " << dump2file);

        fstream handle(dump2file, ios_base::out);
        if (!handle.good()) ERROR("Cannot save the result to `" << dump2file << "'");

        for (const auto &p : result) {
            if(p.first == numeric_limits<uint64_t>::max()) continue; // invalid node

            handle << p.first << " ";

            if(!negative_scores && p.second < 0){
                handle << numeric_limits<T>::max();
            } else {
                handle << p.second;
            }

            handle << "\n";
        }

        handle.close();
    }

    /*****************************************************************************
     *                                                                           *
     *  BFS                                                                      *
     *                                                                           *
     *****************************************************************************/
    // Implementation based on the reference BFS for the GAP Benchmark Suite
    // https://github.com/sbeamer/gapbs
    // The reference implementation has been written by Scott Beamer
    //
    // Copyright (c) 2015, The Regents of the University of California (Regents)
    // All Rights Reserved.
    //
    // Redistribution and use in source and binary forms, with or without
    // modification, are permitted provided that the following conditions are met:
    // 1. Redistributions of source code must retain the above copyright
    //    notice, this list of conditions and the following disclaimer.
    // 2. Redistributions in binary form must reproduce the above copyright
    //    notice, this list of conditions and the following disclaimer in the
    //    documentation and/or other materials provided with the distribution.
    // 3. Neither the name of the Regents nor the
    //    names of its contributors may be used to endorse or promote products
    //    derived from this software without specific prior written permission.
    //
    // THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
    // ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    // WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    // DISCLAIMED. IN NO EVENT SHALL REGENTS BE LIABLE FOR ANY
    // DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    // (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    // LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    // ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    // (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    // SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    /*

    Will return parent array for a BFS traversal from a source vertex
    This BFS implementation makes use of the Direction-Optimizing approach [1].
    It uses the alpha and beta parameters to determine whether to switch search
    directions. For representing the frontier, it uses a SlidingQueue for the
    top-down approach and a Bitmap for the bottom-up approach. To reduce
    false-sharing for the top-down approach, thread-local QueueBuffer's are used.
    To save time computing the number of edges exiting the frontier, this
    implementation precomputes the degrees in bulk at the beginning by storing
    them in parent array as negative numbers. Thus the encoding of parent is:
      parent[x] < 0 implies x is unvisited and parent[x] = -out_degree(x)
      parent[x] >= 0 implies x been visited
    [1] Scott Beamer, Krste Asanović, and David Patterson. "Direction-Optimizing
        Breadth-First Search." International Conference on High Performance
        Computing, Networking, Storage and Analysis (SC), Salt Lake City, Utah,
        November 2012.

    */
    //#define DEBUG_BFS
#if defined(DEBUG_BFS)
#define COUT_DEBUG_BFS(msg) COUT_DEBUG(msg)
#else
#define COUT_DEBUG_BFS(msg)
#endif

    static
    int64_t do_bfs_BUStep(gt::SharedROTransaction& transaction, uint64_t max_vertex_id, int64_t* distances, int64_t distance, gapbs::Bitmap &front, gapbs::Bitmap &next) {
        int64_t awake_count = 0;
        next.reset();
        auto graph = transaction.get_graph();
//#pragma omp parallel for schedule(dynamic, 1024) reduction(+ : awake_count)
#pragma omp parallel reduction(+ : awake_count)
        {
            uint8_t thread_id = graph->get_openmp_worker_thread_id();
            auto iterator = transaction.generate_edge_delta_iterator(thread_id);
#pragma omp for schedule(dynamic, 1024)
            for (uint64_t u = 1; u <= max_vertex_id; u++) {
                if (distances[u - 1] == numeric_limits<int64_t>::max()) continue; // the vertex does not exist
                COUT_DEBUG_BFS("explore: " << u << ", distance: " << distances[u]);

                if (distances[u - 1] < 0) { // the node has not been visited yet
                    //auto iterator = transaction.simple_get_edges(u, /* label */1,thread_id); // fixme: incoming edges for directed graphs
                    transaction.simple_get_edges(u,1,thread_id,iterator);
                    //for debug:
                    //uint64_t counter = 0;
                    while (iterator.valid()) {
                        uint64_t dst = iterator.dst_id();
                        COUT_DEBUG_BFS("\tincoming edge: " << dst);
                        //counter++;
                        if (front.get_bit(dst - 1)) {
                            COUT_DEBUG_BFS("\t-> distance updated to " << distance << " via vertex #" << dst);
                            distances[u - 1] = distance; // on each BUStep, all nodes will have the same distance
                            awake_count++;
                            next.set_bit(u - 1);
                            break;
                        }

                        //iterator.next();
                    }
                    iterator.close();
                }
            }
            transaction.thread_on_openmp_section_finish(thread_id);
        }
        graph->on_openmp_section_finishing();
        return awake_count;
    }
    static
    int64_t static_do_bfs_BUStep(gt::SharedROTransaction& transaction, uint64_t max_vertex_id, int64_t* distances, int64_t distance, gapbs::Bitmap &front, gapbs::Bitmap &next) {
        int64_t awake_count = 0;
        next.reset();
//#pragma omp parallel for schedule(dynamic, 1024) reduction(+ : awake_count)
#pragma omp parallel reduction(+ : awake_count)
        {
            auto iterator = transaction.generate_static_edge_delta_iterator();
#pragma omp for schedule(dynamic, 1024)
            for (uint64_t u = 1; u <= max_vertex_id; u++) {
                if (distances[u - 1] == numeric_limits<int64_t>::max()) continue; // the vertex does not exist
                COUT_DEBUG_BFS("explore: " << u << ", distance: " << distances[u]);

                if (distances[u - 1] < 0) { // the node has not been visited yet
                    //auto iterator = transaction.static_get_edges(u, 1); 
                    transaction.static_get_edges(u,1,iterator);

                    while (iterator.valid()) {
                        uint64_t dst = iterator.dst_id();
                        COUT_DEBUG_BFS("\tincoming edge: " << dst);

                        if (front.get_bit(dst - 1)) {
                            COUT_DEBUG_BFS("\t-> distance updated to " << distance << " via vertex #" << dst);
                            distances[u - 1] = distance; // on each BUStep, all nodes will have the same distance
                            awake_count++;
                            next.set_bit(u - 1);
                            break;
                        }
                    }
                }
            }
        }
        return awake_count;
    }
    static
    int64_t do_bfs_TDStep(gt::SharedROTransaction& transaction, uint64_t max_vertex_id, int64_t* distances, int64_t distance, gapbs::SlidingQueue<int64_t>& queue) {
        int64_t scout_count = 0;
        auto graph = transaction.get_graph();
#pragma omp parallel reduction(+ : scout_count)
        {
            uint8_t thread_id = graph->get_openmp_worker_thread_id();
            auto iterator = transaction.generate_edge_delta_iterator(thread_id);
          /*  if(thread_id>64){
                std::cout<<static_cast<uint32_t>(thread_id)<<std::endl;
                throw std::runtime_error("error, bad thread ID");
            }*/
            gapbs::QueueBuffer<int64_t> lqueue(queue);

#pragma omp for schedule(dynamic, 64)
            for (auto q_iter = queue.begin(); q_iter < queue.end(); q_iter++) {
                int64_t u = *q_iter;
                COUT_DEBUG_BFS("explore: " << u);
                //auto iterator = transaction.simple_get_edges(u, /* label */ 1, thread_id);
                transaction.simple_get_edges(u,1,thread_id,iterator);
                while(iterator.valid()){
                    uint64_t dst = iterator.dst_id();
                    COUT_DEBUG_BFS("\toutgoing edge: " << dst);

                    int64_t curr_val = distances[dst-1];
                    if (curr_val < 0 && gapbs::compare_and_swap(distances[dst-1], curr_val, distance)) {
                        COUT_DEBUG_BFS("\t-> distance updated to " << distance << " via vertex #" << dst);
                        lqueue.push_back(dst);
                        scout_count += -curr_val;
                    }

                   // iterator.next();
                }
                iterator.close();
            }

            lqueue.flush();
            transaction.thread_on_openmp_section_finish(thread_id);
        }
        graph->on_openmp_section_finishing();
        return scout_count;
    }
    static
    int64_t static_do_bfs_TDStep(gt::SharedROTransaction& transaction, uint64_t max_vertex_id, int64_t* distances, int64_t distance, gapbs::SlidingQueue<int64_t>& queue) {
        int64_t scout_count = 0;
#pragma omp parallel reduction(+ : scout_count)
        {
            gapbs::QueueBuffer<int64_t> lqueue(queue);
            auto iterator = transaction.generate_static_edge_delta_iterator();
#pragma omp for schedule(dynamic, 64)
            for (auto q_iter = queue.begin(); q_iter < queue.end(); q_iter++) {
                int64_t u = *q_iter;
                COUT_DEBUG_BFS("explore: " << u);
                //auto iterator = transaction.static_get_edges(u, /* label */ 1);
                transaction.static_get_edges(u,1,iterator);
                while(iterator.valid()){
                    uint64_t dst = iterator.dst_id();
                    COUT_DEBUG_BFS("\toutgoing edge: " << dst);

                    int64_t curr_val = distances[dst-1];
                    if (curr_val < 0 && gapbs::compare_and_swap(distances[dst-1], curr_val, distance)) {
                        COUT_DEBUG_BFS("\t-> distance updated to " << distance << " via vertex #" << dst);
                        lqueue.push_back(dst);
                        scout_count += -curr_val;
                    }

                    // iterator.next();
                }
            }

            lqueue.flush();
        }
        return scout_count;
    }
    static
    void do_bfs_QueueToBitmap(gt::SharedROTransaction& transaction, uint64_t max_vertex_id, const gapbs::SlidingQueue<int64_t> &queue, gapbs::Bitmap &bm) {
#pragma omp parallel for
        for (auto q_iter = queue.begin(); q_iter < queue.end(); q_iter++) {
            int64_t u = *q_iter;
            bm.set_bit_atomic(u-1);
        }
    }

    static
    void do_bfs_BitmapToQueue(gt::SharedROTransaction& transaction, uint64_t max_vertex_id, const gapbs::Bitmap &bm, gapbs::SlidingQueue<int64_t> &queue) {
#pragma omp parallel
        {
            gapbs::QueueBuffer<int64_t> lqueue(queue);
#pragma omp for
            for (uint64_t n=1; n <= max_vertex_id; n++)
                if (bm.get_bit(n-1))
                    lqueue.push_back(n);
            lqueue.flush();
        }
        queue.slide_window();
    }
    static
    unique_ptr<int64_t[]> do_bfs_init_distances_init_edge_num(gt::SharedROTransaction& transaction, uint64_t max_vertex_id, int64_t& edge_num){
        unique_ptr<int64_t[]> distances{ new int64_t[max_vertex_id] };
        //reset thread ID's
        auto graph = transaction.get_graph();
        std::atomic_uint64_t total_num = 0;
        //graph->on_openmp_section_finishing();
#pragma omp parallel
        {
            uint8_t thread_id = graph->get_openmp_worker_thread_id();
            auto iterator = transaction.generate_edge_delta_iterator(thread_id);
            uint64_t local_num = 0;
#pragma omp for
            for (uint64_t n = 1; n <= max_vertex_id; n++) {
                if (transaction.get_vertex(n, thread_id).empty()) { // the vertex does not exist
                    distances[n - 1] = numeric_limits<int64_t>::max();
                } else { // the vertex exists
                    // Retrieve the out degree for the vertex n
                    /*uint64_t out_degree = 0;
                    auto iterator = transaction.simple_get_edges(n,  1, thread_id);
                    while (iterator.valid()) {
                        out_degree++;
                        // iterator.next();
                    }
                    iterator.close();*/
                    transaction.simple_get_edges(n,1,thread_id,iterator);
                    uint64_t out_degree = iterator.get_vertex_degree();
                    local_num+= out_degree;
                    distances[n - 1] = out_degree != 0 ? -out_degree : -1;
                }
            }
            total_num.fetch_add(local_num,std::memory_order_acq_rel);
            transaction.thread_on_openmp_section_finish(thread_id);
        }
        edge_num = total_num.load(std::memory_order_relaxed)/2;
        graph->on_openmp_section_finishing();
        return distances;
    }
    static
    unique_ptr<int64_t[]> do_bfs_init_distances(gt::SharedROTransaction& transaction, uint64_t max_vertex_id) {
        unique_ptr<int64_t[]> distances{ new int64_t[max_vertex_id] };
        //reset thread ID's
        auto graph = transaction.get_graph();
        //graph->on_openmp_section_finishing();
#pragma omp parallel
        {
            uint8_t thread_id = graph->get_openmp_worker_thread_id();
            auto iterator = transaction.generate_edge_delta_iterator(thread_id);
#pragma omp for
            for (uint64_t n = 1; n <= max_vertex_id; n++) {
                if (transaction.get_vertex(n, thread_id).empty()) { // the vertex does not exist
                    distances[n - 1] = numeric_limits<int64_t>::max();
                } else { // the vertex exists
                    // Retrieve the out degree for the vertex n
                    /*uint64_t out_degree = 0;
                    auto iterator = transaction.simple_get_edges(n,  1, thread_id);
                    while (iterator.valid()) {
                        out_degree++;
                        // iterator.next();
                    }
                    iterator.close();*/
                    transaction.simple_get_edges(n,1,thread_id,iterator);
                    uint64_t out_degree = iterator.get_vertex_degree();
                    distances[n - 1] = out_degree != 0 ? -out_degree : -1;
                }
            }
            transaction.thread_on_openmp_section_finish(thread_id);
        }
        graph->on_openmp_section_finishing();
        return distances;
    }
    static
    unique_ptr<int64_t[]> static_do_bfs_init_distances(gt::SharedROTransaction& transaction, uint64_t max_vertex_id) {
        unique_ptr<int64_t[]> distances{ new int64_t[max_vertex_id] };

#pragma omp parallel
        {
            auto iterator = transaction.generate_static_edge_delta_iterator();
#pragma omp for
            for (uint64_t n = 1; n <= max_vertex_id; n++) {
                //todo::add static get vertex
                if (transaction.static_get_vertex(n).empty()) { // the vertex does not exist
                    distances[n - 1] = numeric_limits<int64_t>::max();
                } else { // the vertex exists
                    // Retrieve the out degree for the vertex n

                   /*uint64_t out_degree = 0;
                   auto iterator = transaction.static_get_edges(n,  1);
                    while (iterator.valid()) {
                        out_degree++;
                        // iterator.next();
                    }*/
                    transaction.static_get_edges(n,1,iterator);
                    uint64_t out_degree = static_cast<uint64_t>(iterator.vertex_degree());
                    distances[n - 1] = out_degree != 0 ? -out_degree : -1;
                }
            }
        }
        return distances;
    }
    static
    unique_ptr<int64_t[]> do_bfs(gt::SharedROTransaction& transaction, uint64_t num_vertices, uint64_t max_vertex_id, uint64_t root, utility::TimeoutService& timer, int alpha = 15, int beta = 18) {
        // The implementation from GAP BS reports the parent (which indeed it should make more sense), while the one required by
        // Graphalytics only returns the distance
        //transaction.print_debug_info();
       // std::cout<<"correct bfs"<<std::endl;
       int64_t edges_to_check = 0;
        unique_ptr<int64_t[]> ptr_distances = do_bfs_init_distances_init_edge_num(transaction,max_vertex_id,edges_to_check);//do_bfs_init_distances(transaction, max_vertex_id);
        int64_t* __restrict distances = ptr_distances.get();
        distances[root-1] = 0;

        gapbs::SlidingQueue<int64_t> queue(max_vertex_id);
        queue.push_back(root);
        queue.slide_window();
        gapbs::Bitmap curr(max_vertex_id);
        curr.reset();
        gapbs::Bitmap front(max_vertex_id);
        front.reset();
        //int64_t edges_to_check = num_edges; //g.num_edges_directed();

        int64_t scout_count = 0;
        { // retrieve the out degree of the root
            auto graph = transaction.get_graph();
            uint8_t thread_id = graph->get_openmp_worker_thread_id();
            auto iterator = transaction.simple_get_edges(root, 1,thread_id);
            /*while(iterator.valid()){ scout_count++; //iterator.next();
             }
            iterator.close();*/
            scout_count = iterator.get_vertex_degree();
            graph->on_openmp_section_finishing();
        }
        int64_t distance = 1; // current distance
        while (!timer.is_timeout() && !queue.empty()) {

            if (scout_count > edges_to_check / alpha) {
                int64_t awake_count, old_awake_count;
                do_bfs_QueueToBitmap(transaction, max_vertex_id, queue, front);
                awake_count = queue.size();
                queue.slide_window();
                do {
                    old_awake_count = awake_count;
                    awake_count = do_bfs_BUStep(transaction, max_vertex_id, distances, distance, front, curr);
                    front.swap(curr);
                    distance++;
                } while ((awake_count >= old_awake_count) || (awake_count > (int64_t) num_vertices / beta));
                do_bfs_BitmapToQueue(transaction, max_vertex_id, front, queue);
                scout_count = 1;
            } else {
                edges_to_check -= scout_count;
                scout_count = do_bfs_TDStep(transaction, max_vertex_id, distances, distance, queue);
                queue.slide_window();
                distance++;
            }
        }

        return ptr_distances;
    }
    static
    unique_ptr<int64_t[]> do_bfs(gt::SharedROTransaction& transaction, uint64_t num_vertices, uint64_t num_edges, uint64_t max_vertex_id, uint64_t root, utility::TimeoutService& timer, int alpha = 15, int beta = 18) {
        // The implementation from GAP BS reports the parent (which indeed it should make more sense), while the one required by
        // Graphalytics only returns the distance
        //transaction.print_debug_info();
        unique_ptr<int64_t[]> ptr_distances = do_bfs_init_distances(transaction, max_vertex_id);
        int64_t* __restrict distances = ptr_distances.get();
        distances[root-1] = 0;

        gapbs::SlidingQueue<int64_t> queue(max_vertex_id);
        queue.push_back(root);
        queue.slide_window();
        gapbs::Bitmap curr(max_vertex_id);
        curr.reset();
        gapbs::Bitmap front(max_vertex_id);
        front.reset();
        int64_t edges_to_check = num_edges; //g.num_edges_directed();

        int64_t scout_count = 0;
        { // retrieve the out degree of the root
            auto graph = transaction.get_graph();
            uint8_t thread_id = graph->get_openmp_worker_thread_id();
            auto iterator = transaction.simple_get_edges(root, 1,thread_id);
            /*while(iterator.valid()){ scout_count++; //iterator.next();
             }
            iterator.close();*/
            scout_count = iterator.get_vertex_degree();
            graph->on_openmp_section_finishing();
        }
        int64_t distance = 1; // current distance
        while (!timer.is_timeout() && !queue.empty()) {

            if (scout_count > edges_to_check / alpha) {
                int64_t awake_count, old_awake_count;
                do_bfs_QueueToBitmap(transaction, max_vertex_id, queue, front);
                awake_count = queue.size();
                queue.slide_window();
                do {
                    old_awake_count = awake_count;
                    awake_count = do_bfs_BUStep(transaction, max_vertex_id, distances, distance, front, curr);
                    front.swap(curr);
                    distance++;
                } while ((awake_count >= old_awake_count) || (awake_count > (int64_t) num_vertices / beta));
                do_bfs_BitmapToQueue(transaction, max_vertex_id, front, queue);
                scout_count = 1;
            } else {
                edges_to_check -= scout_count;
                scout_count = do_bfs_TDStep(transaction, max_vertex_id, distances, distance, queue);
                queue.slide_window();
                distance++;
            }
        }

        return ptr_distances;
    }
    static
    unique_ptr<int64_t[]> static_do_bfs(gt::SharedROTransaction& transaction, uint64_t num_vertices, uint64_t num_edges, uint64_t max_vertex_id, uint64_t root, utility::TimeoutService& timer, int alpha = 15, int beta = 18) {
        // The implementation from GAP BS reports the parent (which indeed it should make more sense), while the one required by
        // Graphalytics only returns the distance
        //transaction.print_debug_info();
        unique_ptr<int64_t[]> ptr_distances = static_do_bfs_init_distances(transaction, max_vertex_id);
        int64_t* __restrict distances = ptr_distances.get();
        distances[root-1] = 0;

        gapbs::SlidingQueue<int64_t> queue(max_vertex_id);
        queue.push_back(root);
        queue.slide_window();
        gapbs::Bitmap curr(max_vertex_id);
        curr.reset();
        gapbs::Bitmap front(max_vertex_id);
        front.reset();
        int64_t edges_to_check = num_edges; //g.num_edges_directed();

        int64_t scout_count = 0;
        { // retrieve the out degree of the root
            auto iterator = transaction.static_get_edges(root, 1);
            /*while(iterator.valid()){ scout_count++; //iterator.next();
            }*/
            scout_count = iterator.vertex_degree();
        }
        int64_t distance = 1; // current distance

        while (!timer.is_timeout() && !queue.empty()) {

            if (scout_count > edges_to_check / alpha) {
                int64_t awake_count, old_awake_count;
                do_bfs_QueueToBitmap(transaction, max_vertex_id, queue, front);
                awake_count = queue.size();
                queue.slide_window();
                do {
                    old_awake_count = awake_count;
                    awake_count = static_do_bfs_BUStep(transaction, max_vertex_id, distances, distance, front, curr);
                    front.swap(curr);
                    distance++;
                } while ((awake_count >= old_awake_count) || (awake_count > (int64_t) num_vertices / beta));
                do_bfs_BitmapToQueue(transaction, max_vertex_id, front, queue);
                scout_count = 1;
            } else {
                edges_to_check -= scout_count;
                scout_count = static_do_bfs_TDStep(transaction, max_vertex_id, distances, distance, queue);
                queue.slide_window();
                distance++;
            }
        }

        return ptr_distances;
    }
    void GTXDriver::bfs(uint64_t external_source_id, const char* dump2file) {
        //std::cout<<"from source "<<external_source_id<<std::endl;
        if(m_is_directed) { ERROR("This implementation of the BFS does not support directed graphs"); }

        // Init
        utility::TimeoutService timeout { m_timeout };
        Timer timer; timer.start();
        gt::SharedROTransaction transaction =  GTX->begin_shared_read_only_transaction();
        uint64_t max_vertex_id = GTX->get_max_allocated_vid();
        uint64_t num_vertices = m_num_vertices;
        //uint64_t num_edges = m_num_edges;
        //transaction.print_debug_info();
        uint64_t root = ext2int(external_source_id);
        //COUT_DEBUG_BFS("root: " << root << " [external vertex: " << external_source_id << "]");
        /*auto handler =  GTX->get_bfs_handler(num_vertices);
        handler.compute(root);
        if(dump2file != nullptr)
            save_results<int64_t, false>(*handler.get_result(),dump2file);*/
        //Timer t;
        //t.start();
        // Run the BFS algorithm
        unique_ptr<int64_t[]> ptr_result = do_bfs(transaction, num_vertices /*, num_edges*/, max_vertex_id, root, timeout);
        //unique_ptr<int64_t[]> ptr_result = static_do_bfs(transaction, num_vertices, num_edges, max_vertex_id, root, timeout);
        //cout << "BFS took " << t << endl;
        if(timeout.is_timeout()){
            transaction.commit(); // in gtx it is necessary
            RAISE_EXCEPTION(TimeoutError, "Timeout occurred after " << timer);
        }

        // translate the logical vertex IDs into the external vertex IDs
        auto external_ids = translate(&transaction, ptr_result.get(), max_vertex_id);
        //auto external_ids = static_translate(&transaction, ptr_result.get(), max_vertex_id);
        //cout << "Translation took " << t << endl;
        transaction.commit(); // not sure if strictly necessary
        if(timeout.is_timeout()){
            RAISE_EXCEPTION(TimeoutError, "Timeout occurred after " << timer);
        }
        //std::cout<<dump2file<<std::endl;
        if(dump2file != nullptr) // store the results in the given file
            save_results<int64_t, false>(external_ids, dump2file);
        //std::cout<<"bfs over"<<std::endl;
    }
/*****************************************************************************
 *                                                                           *
 *  PageRank                                                                 *
 *                                                                           *
 *****************************************************************************/
    //#define DEBUG_PAGERANK
#if defined(DEBUG_PAGERANK)
#define COUT_DEBUG_PAGERANK(msg) COUT_DEBUG(msg)
#else
#define COUT_DEBUG_PAGERANK(msg)
#endif

// Implementation based on the reference PageRank for the GAP Benchmark Suite
// https://github.com/sbeamer/gapbs
// The reference implementation has been written by Scott Beamer
//
// Copyright (c) 2015, The Regents of the University of California (Regents)
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. Neither the name of the Regents nor the
//    names of its contributors may be used to endorse or promote products
//    derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL REGENTS BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

/*
GAP Benchmark Suite
Kernel: PageRank (PR)
Author: Scott Beamer

Will return pagerank scores for all vertices once total change < epsilon

This PR implementation uses the traditional iterative approach. This is done
to ease comparisons to other implementations (often use same algorithm), but
it is not necessarily the fastest way to implement it. It does perform the
updates in the pull direction to remove the need for atomics.
*/
    static
    unique_ptr<double[]> do_pagerank(gt::SharedROTransaction& transaction, uint64_t num_vertices, uint64_t max_vertex_id, uint64_t num_iterations, double damping_factor, utility::TimeoutService& timer) {
        const double init_score = 1.0 / num_vertices;
        const double base_score = (1.0 - damping_factor) / num_vertices;

        unique_ptr<double[]> ptr_scores{ new double[max_vertex_id]() }; // avoid memory leaks
        unique_ptr<uint64_t[]> ptr_degrees{ new uint64_t[max_vertex_id]() }; // avoid memory leaks
        double* scores = ptr_scores.get();
        uint64_t* __restrict degrees = ptr_degrees.get();
        auto graph = transaction.get_graph();

#pragma omp parallel
        {
            uint8_t thread_ud = graph->get_openmp_worker_thread_id();
            auto iterator = transaction.generate_edge_delta_iterator(thread_ud);
#pragma omp for
            for (uint64_t v = 1; v <= max_vertex_id; v++) {
                scores[v - 1] = init_score;

                // compute the outdegree of the vertex
                if (!transaction.get_vertex(v, thread_ud).empty()) { // check the vertex exists
                    /*uint64_t degree = 0;
                    //auto iterator = transaction.simple_get_edges(v, 1,thread_ud);
                    transaction.simple_get_edges(v,1,thread_ud,iterator);
                    while (iterator.valid()) {
                        degree++; //iterator.next();
                    }
                    iterator.close();
                    degrees[v - 1] = degree;*/
                    
                    transaction.simple_get_edges(v,1,thread_ud,iterator);
                    degrees[v - 1] = iterator.get_vertex_degree();
                } else {
                    degrees[v - 1] = numeric_limits<uint64_t>::max();
                }
            }
            transaction.thread_on_openmp_section_finish(thread_ud);
        }
        graph->on_openmp_section_finishing();
        gapbs::pvector<double> outgoing_contrib(max_vertex_id, 0.0);
        #if false
        {
            double dangling_sum = 0.0;

            // for each node, precompute its contribution to all of its outgoing neighbours and, if it's a sink,
            // add its rank to the `dangling sum' (to be added to all nodes).
#pragma omp parallel for reduction(+:dangling_sum)
            for(uint64_t v = 1; v <= max_vertex_id; v++){
                uint64_t out_degree = degrees[v-1];
                if(out_degree == numeric_limits<uint64_t>::max()){
                    continue; // the vertex does not exist
                } else if (out_degree == 0){ // this is a sink
                    dangling_sum += scores[v-1];
                } else {
                    outgoing_contrib[v-1] = scores[v-1] / out_degree;
                }
            }

            dangling_sum /= num_vertices;

            // compute the new score for each node in the graph
            //fixme: currently our txn does not support being executed by mutiple threads. It requires fetching thread_id for each operation execution from the table.
//#pragma omp parallel for schedule(dynamic, 64)
#pragma omp parallel
            {
                uint8_t thread_id = graph->get_openmp_worker_thread_id();
                auto iterator = transaction.generate_edge_delta_iterator(thread_id);
#pragma omp for schedule(dynamic, 64)
                for (uint64_t v = 1; v <= max_vertex_id; v++) {
                    if (degrees[v-1] == numeric_limits<uint64_t>::max()) { continue; } // the vertex does not exist

                    double incoming_total = 0;
                    //auto iterator = transaction.simple_get_edges(v, /* label ? */1,thread_id); // fixme: incoming edges for directed graphs
                    transaction.simple_get_edges(v, /* label ? */1,thread_id,iterator);
                    while (iterator.valid()) {
                        uint64_t u = iterator.dst_id();
                        incoming_total += outgoing_contrib[u-1];
                        //iterator.next();
                    }
                    iterator.close();
                    // update the score
                    scores[v-1] = base_score + damping_factor * (incoming_total + dangling_sum);
                }
                transaction.thread_on_openmp_section_finish(thread_id);
            }
            graph->on_openmp_section_finishing();
        }
        #endif
        // pagerank iterations
        for(uint64_t iteration = 0; iteration < num_iterations && !timer.is_timeout(); iteration++){
            double dangling_sum = 0.0;

            // for each node, precompute its contribution to all of its outgoing neighbours and, if it's a sink,
            // add its rank to the `dangling sum' (to be added to all nodes).
#pragma omp parallel for reduction(+:dangling_sum)
            for(uint64_t v = 1; v <= max_vertex_id; v++){
                uint64_t out_degree = degrees[v-1];
                if(out_degree == numeric_limits<uint64_t>::max()){
                    continue; // the vertex does not exist
                } else if (out_degree == 0){ // this is a sink
                    dangling_sum += scores[v-1];
                } else {
                    outgoing_contrib[v-1] = scores[v-1] / out_degree;
                }
            }

            dangling_sum /= num_vertices;

            // compute the new score for each node in the graph
            //fixme: currently our txn does not support being executed by mutiple threads. It requires fetching thread_id for each operation execution from the table.
//#pragma omp parallel for schedule(dynamic, 64)
#pragma omp parallel
            {
                uint8_t thread_id = graph->get_openmp_worker_thread_id();
                auto iterator = transaction.generate_edge_delta_iterator(thread_id);
#pragma omp for schedule(dynamic, 64)
                for (uint64_t v = 1; v <= max_vertex_id; v++) {
                    if (degrees[v-1] == numeric_limits<uint64_t>::max()) { continue; } // the vertex does not exist

                    double incoming_total = 0;
                    //auto iterator = transaction.simple_get_edges(v, /* label ? */1,thread_id); // fixme: incoming edges for directed graphs
                    transaction.simple_get_edges(v, /* label ? */1,thread_id,iterator);
                    while (iterator.valid()) {
                        uint64_t u = iterator.dst_id();
                        incoming_total += outgoing_contrib[u-1];
                        //iterator.next();
                    }
                    iterator.close();
                    // update the score
                    scores[v-1] = base_score + damping_factor * (incoming_total + dangling_sum);
                }
                transaction.thread_on_openmp_section_finish(thread_id);
            }
            graph->on_openmp_section_finishing();
        }

        return ptr_scores;
    }
    static
    unique_ptr<double[]> do_static_pagerank(gt::SharedROTransaction& transaction, uint64_t num_vertices, uint64_t max_vertex_id, uint64_t num_iterations, double damping_factor, utility::TimeoutService& timer) {
        const double init_score = 1.0 / num_vertices;
        const double base_score = (1.0 - damping_factor) / num_vertices;

        unique_ptr<double[]> ptr_scores{ new double[max_vertex_id]() }; // avoid memory leaks
        unique_ptr<uint64_t[]> ptr_degrees{ new uint64_t[max_vertex_id]() }; // avoid memory leaks
        double* scores = ptr_scores.get();
        uint64_t* __restrict degrees = ptr_degrees.get();
        //auto graph = transaction.get_graph();

#pragma omp parallel
        {
            //uint8_t thread_ud = graph->get_openmp_worker_thread_id();
            auto iterator = transaction.generate_static_edge_delta_iterator();
#pragma omp for
            for (uint64_t v = 1; v <= max_vertex_id; v++) {
                scores[v - 1] = init_score;

                // compute the outdegree of the vertex
                if (!transaction.static_get_vertex(v).empty()) { // check the vertex exists
                    //uint64_t degree = 0;
                    //auto iterator = transaction.static_get_edges(v, /* label ? */ 1);
                    transaction.static_get_edges(v,1, iterator);
                    /*while (iterator.valid()) {
                        degree++; //iterator.next();
                    }*/
                    //iterator.close();
                    //degrees[v - 1] = degree;
                    degrees[v-1]=iterator.vertex_degree();
                } else {
                    degrees[v - 1] = numeric_limits<uint64_t>::max();
                }
            }
        }
        //graph->on_openmp_section_finishing();
        gapbs::pvector<double> outgoing_contrib(max_vertex_id, 0.0);

        // pagerank iterations
        for(uint64_t iteration = 0; iteration < num_iterations && !timer.is_timeout(); iteration++){
            double dangling_sum = 0.0;

            // for each node, precompute its contribution to all of its outgoing neighbours and, if it's a sink,
            // add its rank to the `dangling sum' (to be added to all nodes).
#pragma omp parallel for reduction(+:dangling_sum)
            for(uint64_t v = 1; v <= max_vertex_id; v++){
                uint64_t out_degree = degrees[v-1];
                if(out_degree == numeric_limits<uint64_t>::max()){
                    continue; // the vertex does not exist
                } else if (out_degree == 0){ // this is a sink
                    dangling_sum += scores[v-1];
                } else {
                    outgoing_contrib[v-1] = scores[v-1] / out_degree;
                }
            }

            dangling_sum /= num_vertices;

            // compute the new score for each node in the graph
            //fixme: currently our txn does not support being executed by mutiple threads. It requires fetching thread_id for each operation execution from the table.
//#pragma omp parallel for schedule(dynamic, 64)
#pragma omp parallel
            {
                //uint8_t thread_id = graph->get_openmp_worker_thread_id();
                auto iterator = transaction.generate_static_edge_delta_iterator();
#pragma omp for schedule(dynamic, 64)
                for (uint64_t v = 1; v <= max_vertex_id; v++) {
                    if (degrees[v-1] == numeric_limits<uint64_t>::max()) { continue; } // the vertex does not exist

                    double incoming_total = 0;
                    /*auto iterator = transaction.static_get_edges(v, 
                                                                 1); // fixme: incoming edges for directed graphs*/
                    transaction.static_get_edges(v,1,iterator);
                    while (iterator.valid()) {
                        uint64_t u = iterator.dst_id();
                        incoming_total += outgoing_contrib[u-1];
                        //iterator.next();
                    }
                    //iterator.close();
                    // update the score
                    scores[v-1] = base_score + damping_factor * (incoming_total + dangling_sum);
                }
            }
            //graph->on_openmp_section_finishing();
        }

        return ptr_scores;
    }

    void GTXDriver::pagerank(uint64_t num_iterations, double damping_factor, const char* dump2file) {
        if(m_is_directed) { ERROR("This implementation of PageRank does not support directed graphs"); }

        // Init
        utility::TimeoutService timeout { m_timeout };
        Timer timer; timer.start();
        //gt::SharedROTransaction transaction = GTX->begin_shared_read_only_transaction();
        uint64_t num_vertices = m_num_vertices;
        //reuse data structures
       /*auto result = GTX->compute_pagerank(num_vertices,num_iterations,damping_factor);
        if(dump2file != nullptr)
            save_results(*result,dump2file);*/
        //direct access method
        auto handler =  GTX->get_pagerank_handler(num_vertices);
        handler.compute(num_iterations,damping_factor);
        if(dump2file != nullptr)
            save_results(*handler.get_result(),dump2file);

        /*uint64_t max_vertex_id = GTX->get_max_allocated_vid();
        // Run the PageRank algorithm
        unique_ptr<double[]> ptr_result = do_pagerank(transaction, num_vertices, max_vertex_id, num_iterations, damping_factor, timeout);
        //unique_ptr<double[]> ptr_result = do_sstatic_pagerank(transaction, num_vertices, max_vertex_id, num_iterations, damping_factor, timeout);
        if(timeout.is_timeout()){ transaction.commit(); RAISE_EXCEPTION(TimeoutError, "Timeout occurred after " << timer);  }

        // Retrieve the external node ids
        auto external_ids = translate(&transaction, ptr_result.get(), max_vertex_id);
        //auto external_ids = static_translate(&transaction, ptr_result.get(), max_vertex_id);
        transaction.commit(); // read-only transaction, abort == commit
        if(timeout.is_timeout()){ RAISE_EXCEPTION(TimeoutError, "Timeout occurred after " << timer); }

        // Store the results in the given file
        if(dump2file != nullptr)
            save_results(external_ids, dump2file);*/
    }
/*****************************************************************************
 *                                                                           *
 *  WCC                                                                      *
 *                                                                           *
 *****************************************************************************/
// Implementation based on the reference WCC for the GAP Benchmark Suite
// https://github.com/sbeamer/gapbs
// The reference implementation has been written by Scott Beamer
//
// Copyright (c) 2015, The Regents of the University of California (Regents)
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. Neither the name of the Regents nor the
//    names of its contributors may be used to endorse or promote products
//    derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL REGENTS BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#define DEBUG_WCC
#if defined(DEBUG_WCC)
#define COUT_DEBUG_WCC(msg) COUT_DEBUG(msg)
#else
#define COUT_DEBUG_WCC(msg)
#endif
/*
GAP Benchmark Suite
Kernel: Connected Components (CC)
Author: Scott Beamer

Will return comp array labelling each vertex with a connected component ID

This CC implementation makes use of the Shiloach-Vishkin [2] algorithm with
implementation optimizations from Bader et al. [1]. Michael Sutton contributed
a fix for directed graphs using the min-max swap from [3], and it also produces
more consistent performance for undirected graphs.

[1] David A Bader, Guojing Cong, and John Feo. "On the architectural
    requirements for efficient execution of graph algorithms." International
    Conference on Parallel Processing, Jul 2005.

[2] Yossi Shiloach and Uzi Vishkin. "An o(logn) parallel connectivity algorithm"
    Journal of Algorithms, 3(1):57–67, 1982.

[3] Kishore Kothapalli, Jyothish Soman, and P. J. Narayanan. "Fast GPU
    algorithms for graph connectivity." Workshop on Large Scale Parallel
    Processing, 2010.
*/

// The hooking condition (comp_u < comp_v) may not coincide with the edge's
// direction, so we use a min-max swap such that lower component IDs propagate
// independent of the edge's direction.
/*
 * Libin: gtx read-txn not thread safe, so these analytical functions can only be done in read-only environment
 */
    static
    unique_ptr<uint64_t[]> do_wcc(gt::SharedROTransaction& transaction, uint64_t max_vertex_id, utility::TimeoutService& timer) {
        // init
        COUT_DEBUG_WCC("max_vertex_id: " << max_vertex_id);
        unique_ptr<uint64_t[]> ptr_components { new uint64_t[max_vertex_id] };
        uint64_t* comp = ptr_components.get();

#pragma omp parallel for
        for (uint64_t n = 1; n <= max_vertex_id; n++){
            if(transaction.get_vertex(n).empty()){ // the vertex does not exist
                COUT_DEBUG_WCC("Vertex #" << n << " does not exist");
                comp[n] = numeric_limits<uint64_t>::max();
            } else {
                comp[n] = n;
            }
        }

        bool change = true;
        while (change && !timer.is_timeout()) {
            change = false;

#pragma omp parallel for schedule(dynamic, 64)
            for (uint64_t u = 1; u <= max_vertex_id; u++){
                if(comp[u] == numeric_limits<uint64_t>::max()) continue; // the vertex does not exist

                auto iterator = transaction.simple_get_edges(u, 1);
                while(iterator.valid()){
                    uint64_t v = iterator.dst_id();

                    uint64_t comp_u = comp[u];
                    uint64_t comp_v = comp[v];
                    if (comp_u != comp_v) {
                        // Hooking condition so lower component ID wins independent of direction
                        uint64_t high_comp = std::max(comp_u, comp_v);
                        uint64_t low_comp = std::min(comp_u, comp_v);
                        if (high_comp == comp[high_comp]) {
                            change = true;
                            COUT_DEBUG_WCC("comp[" << high_comp << "] = " << low_comp);
                            comp[high_comp] = low_comp;
                        }
                    }

                    //iterator.next();
                }
                iterator.close();
            }

#pragma omp parallel for schedule(dynamic, 64)
            for (uint64_t n = 1; n <= max_vertex_id; n++){
                if(comp[n] == numeric_limits<uint64_t>::max()) continue; // the vertex does not exist

                while (comp[n] != comp[comp[n]]) {
                    comp[n] = comp[comp[n]];
                }
            }


            COUT_DEBUG_WCC("change: " << change);
        }

        return ptr_components;
    }
    static void do_weighted_scan(gt::SharedROTransaction& transaction, uint64_t max_vertex_id){
        uint64_t total_edge_num = 0;
        uint64_t total_vid_sum = 0;
        double total_weight = 0;
        auto graph = transaction.get_graph();
#pragma omp parallel
        {
            uint8_t thread_id = graph->get_openmp_worker_thread_id();
            auto iterator = transaction.generate_edge_delta_iterator(thread_id);
            uint64_t local_degree = 0;
            uint64_t  local_vid = 0;
            double local_weight = 0;
#pragma omp for
            for (uint64_t v = 1; v <= max_vertex_id; v++) {
                     // compute the outdegree of the vertex
                //if (!transaction.get_vertex(v, thread_id).empty()) { // check the vertex exists
                    //auto iterator = transaction.simple_get_edges(v, /* label ? */ 1,thread_id);
                    transaction.simple_get_edges(v,1,thread_id,iterator);
                    while (iterator.valid()) {
                        local_degree++; //iterator.next();
                        local_vid+=iterator.dst_id();
                        //string_view payload = iterator.edge_delta_data();
                        //double w = *reinterpret_cast<const double*>(payload.data());
                        double w = iterator.edge_delta_weight();
                        local_weight +=w;
                    }
                    iterator.close();
               // } 
            }
#pragma omp critical
{
                total_edge_num += local_degree;
                total_vid_sum += local_vid;
                total_weight+=local_weight;
}
            transaction.thread_on_openmp_section_finish(thread_id);
        }
        graph->on_openmp_section_finishing();
        std::cout<<"total degree is "<<total_edge_num<<std::endl;
        std::cout<<"total vid sum is "<<total_vid_sum<<std::endl;
        std::cout<<"total weight is "<<total_weight<<std::endl;
    }
    void GTXDriver::wcc(const char* dump2file) {
        /*utility::TimeoutService timeout { m_timeout };
        Timer timer; timer.start();
        auto transaction =GTX->begin_shared_read_only_transaction();
        uint64_t max_vertex_id = GTX->get_max_allocated_vid();

        // run wcc
        unique_ptr<uint64_t[]> ptr_components = do_wcc(transaction, max_vertex_id, timeout);
        if(timeout.is_timeout()){ transaction.commit(); RAISE_EXCEPTION(TimeoutError, "Timeout occurred after " << timer); }

        // translate the vertex IDs
        auto external_ids = translate(&transaction, ptr_components.get(), max_vertex_id);
        transaction.commit(); // read-only transaction, abort == commit
        if(timeout.is_timeout()){ RAISE_EXCEPTION(TimeoutError, "Timeout occurred after " << timer); }

        // store the results in the given file
        if(dump2file != nullptr)
            save_results(external_ids, dump2file);*/
        auto transaction =GTX->begin_shared_read_only_transaction();
        uint64_t max_vertex_id = GTX->get_max_allocated_vid();
        do_weighted_scan(transaction,max_vertex_id);
    }
/*****************************************************************************
 *                                                                           *
 *  CDLP                                                                     *
 *                                                                           *
 *****************************************************************************/
// same impl~ as the one done for llama
    static
    unique_ptr<uint64_t[]> do_cdlp(gt::SharedROTransaction& transaction, uint64_t max_vertex_id, bool is_graph_directed, uint64_t max_iterations, utility::TimeoutService& timer) {
        unique_ptr<uint64_t[]> ptr_labels0 { new uint64_t[max_vertex_id] };
        unique_ptr<uint64_t[]> ptr_labels1 { new uint64_t[max_vertex_id] };
        uint64_t* labels0 = ptr_labels0.get(); // current labels
        uint64_t* labels1 = ptr_labels1.get(); // labels for the next iteration

        // initialisation
#pragma omp parallel for
        for(uint64_t v = 1; v <= max_vertex_id; v++){
            string_view payload = transaction.get_vertex(v);
            if(payload.empty()){ // the vertex does not exist
                labels0[v] = labels1[v] = numeric_limits<uint64_t>::max();
            } else {
                labels0[v] = *reinterpret_cast<const uint64_t*>(payload.data());
            }
        }

        // algorithm pass
        bool change = true;
        uint64_t current_iteration = 0;
        while(current_iteration < max_iterations && change && !timer.is_timeout()){
            change = false; // reset the flag

#pragma omp parallel for schedule(dynamic, 64) shared(change)
            for(uint64_t v = 1; v <= max_vertex_id; v++){
                if(labels0[v] == numeric_limits<uint64_t>::max()) continue; // the vertex does not exist

                unordered_map<uint64_t, uint64_t> histogram;

                // compute the histogram from both the outgoing & incoming edges. The aim is to find the number of each label
                // is shared among the neighbours of node_id
                auto iterator = transaction.simple_get_edges(v, 1); // out edges
                while(iterator.valid()){
                    uint64_t u = iterator.dst_id();
                    histogram[labels0[u]]++;
                   // iterator.next();
                }
                iterator.close();
                // get the max label
                uint64_t label_max = numeric_limits<int64_t>::max();
                uint64_t count_max = 0;
                for(const auto pair : histogram){
                    if(pair.second > count_max || (pair.second == count_max && pair.first < label_max)){
                        label_max = pair.first;
                        count_max = pair.second;
                    }
                }

                labels1[v] = label_max;
                change |= (labels0[v] != labels1[v]);
            }

            std::swap(labels0, labels1); // next iteration
            current_iteration++;
        }

        if(labels0 == ptr_labels0.get()){
            return ptr_labels0;
        } else {
            return ptr_labels1;
        }
    }
    static void do_topology_scan(gt::SharedROTransaction& transaction, uint64_t max_vertex_id){
        uint64_t total_edge_num = 0;
        uint64_t total_vid_sum = 0;
        auto graph = transaction.get_graph();
#pragma omp parallel
        {
            uint8_t thread_id = graph->get_openmp_worker_thread_id();
            auto iterator = transaction.generate_edge_delta_iterator(thread_id);
            uint64_t local_degree = 0;
            uint64_t  local_vid = 0;
#pragma omp for
            for (uint64_t v = 1; v <= max_vertex_id; v++) {
                     // compute the outdegree of the vertex
                //if (!transaction.get_vertex(v, thread_id).empty()) { // check the vertex exists
                    //auto iterator = transaction.simple_get_edges(v, /* label ? */ 1,thread_id);
                    transaction.simple_get_edges(v,1,thread_id,iterator);
                    //auto iterator = transaction.early_stop_get_edges(v, /* label ? */ 1,thread_id);
                    while (iterator.valid()) {
                        local_degree++; //iterator.next();
                        local_vid+=iterator.dst_id();
                    }
                    iterator.close();
                //} 
            }
#pragma omp critical
{
                total_edge_num += local_degree;
                total_vid_sum += local_vid;
}
            transaction.thread_on_openmp_section_finish(thread_id);
        }
        graph->on_openmp_section_finishing();
        std::cout<<"total degree is "<<total_edge_num<<std::endl;
        std::cout<<"total vid sum is "<<total_vid_sum<<std::endl;
    }
    
    void GTXDriver::generate_two_hops_neighbor_candidates(std::vector<uint64_t>&vertices){
        vertices.resize(two_hop_neighbor_size);
        uint64_t max_vertex_id = GTX->get_max_allocated_vid();
        std::unordered_set<uint64_t> unique_vertices;
        for(uint64_t i=0; i<two_hop_neighbor_size;i++){
            uint64_t vid = rand()%max_vertex_id+1;
            auto result = unique_vertices.emplace(vid);
            if(result.second){
                vertices[i]=vid;
            }else{
                i--;
            }
        }
    }
    void GTXDriver::one_hop_neighbors(std::vector<uint64_t>&vertices){
        auto handler = GTX->get_one_hop_neighbors_handler();
        handler.compute(vertices);
    }

    void GTXDriver::two_hop_neighbors(std::vector<uint64_t>&vertices){
        auto handler = GTX->get_two_hop_neighbors_handler();
        handler.compute(vertices);
    }
    //todo:: fix iterator
    void GTXDriver::cdlp(uint64_t max_iterations, const char* dump2file) {
         /*std::vector<uint64_t> vertices(two_hop_neighbor_size);
         std::unordered_set<uint64_t> unique_vertices;
         for(uint64_t i=0; i<two_hop_neighbor_size; i++){
            vertices[i]= rand()%max_vertex_id+1;
         }
         auto handler = GTX->get_two_hop_neighbors_handler();
         handler.compute(vertices);*/
        /*auto transaction =GTX->begin_shared_read_only_transaction();
        uint64_t max_vertex_id = GTX->get_max_allocated_vid();
        do_topology_scan(transaction,max_vertex_id);*/
     /*   if(m_is_directed) { ERROR("This implementation of the CDLP does not support directed graphs"); }

        utility::TimeoutService timeout { m_timeout };
        Timer timer; timer.start();
        auto transaction =GTX->begin_shared_read_only_transaction();
        uint64_t max_vertex_id = GTX->get_max_allocated_vid();

        // Run the CDLP algorithm
        unique_ptr<uint64_t[]> labels = do_cdlp(transaction, max_vertex_id, is_directed(), max_iterations, timeout);
        if(timeout.is_timeout()){ transaction.commit(); RAISE_EXCEPTION(TimeoutError, "Timeout occurred after " << timer);  }

        // Translate the vertex IDs
        auto external_ids = translate(&transaction, labels.get(), max_vertex_id);
        transaction.commit(); // read-only transaction, abort == commit
        if(timeout.is_timeout()){ RAISE_EXCEPTION(TimeoutError, "Timeout occurred after " << timer); }

        // Store the results in the given file
        if(dump2file != nullptr)
            save_results(external_ids, dump2file);*/
        
    }
/*****************************************************************************
 *                                                                           *
 *  LCC                                                                      *
 *                                                                           *
 *****************************************************************************/
//#define DEBUG_LCC
#if defined(DEBUG_LCC)
#define COUT_DEBUG_LCC(msg) COUT_DEBUG(msg)
#else
#define COUT_DEBUG_LCC(msg)
#endif
// loosely based on the impl~ made for GraphOne
    static
    unique_ptr<double[]> do_lcc_undirected(gt::SharedROTransaction& transaction, uint64_t max_vertex_id, utility::TimeoutService& timer) {
        unique_ptr<double[]> ptr_lcc { new double[max_vertex_id] };
        double* lcc = ptr_lcc.get();
        unique_ptr<uint32_t[]> ptr_degrees_out { new uint32_t[max_vertex_id] };
        uint32_t* __restrict degrees_out = ptr_degrees_out.get();

        // precompute the degrees of the vertices
#pragma omp parallel for schedule(dynamic, 4096)
        for(uint64_t v = 1; v <= max_vertex_id; v++){
            bool vertex_exists = !transaction.get_vertex(v).empty();
            if(!vertex_exists){
                lcc[v] = numeric_limits<double>::signaling_NaN();
            } else {
                { // out degree, restrict the scope
                    uint32_t count = 0;
                    auto iterator = transaction.simple_get_edges(v, 1);
                    while(iterator.valid()){ count ++; //iterator.next();
                    }
                    iterator.close();
                    degrees_out[v] = count;
                }
            }
        }


#pragma omp parallel for schedule(dynamic, 64)
        for(uint64_t v = 1; v <= max_vertex_id; v++){
            if(degrees_out[v] == numeric_limits<uint32_t>::max()) continue; // the vertex does not exist

            COUT_DEBUG_LCC("> Node " << v);
            if(timer.is_timeout()) continue; // exhausted the budget of available time
            lcc[v] = 0.0;
            uint64_t num_triangles = 0; // number of triangles found so far for the node v

            // Cfr. Spec v.0.9.0 pp. 15: "If the number of neighbors of a vertex is less than two, its coefficient is defined as zero"
            uint64_t v_degree_out = degrees_out[v];
            if(v_degree_out < 2) continue;

            // Build the list of neighbours of v
            unordered_set<uint64_t> neighbours;

            { // Fetch the list of neighbours of v
                auto iterator1 = transaction.simple_get_edges(v, 1);
                while(iterator1.valid()){
                    uint64_t u = iterator1.dst_id();
                    neighbours.insert(u);
                    iterator1.next();
                }
                iterator1.close();
            }

            // again, visit all neighbours of v
            // for directed graphs, edges1 contains the intersection of both the incoming and the outgoing edges
            auto iterator1 = transaction.simple_get_edges(v, /* label */ 1);
            while(iterator1.valid()){
                uint64_t u = iterator1.dst_id();
                COUT_DEBUG_LCC("[" << i << "/" << edges.size() << "] neighbour: " << u);
                assert(neighbours.count(u) == 1 && "The set `neighbours' should contain all neighbours of v");

                // For the Graphalytics spec v 0.9.0, only consider the outgoing edges for the neighbours u
                auto iterator2 = transaction.simple_get_edges(u, /* label */ 1);
                while(iterator2.valid()){
                    uint64_t w = iterator2.dst_id();

                    COUT_DEBUG_LCC("---> [" << j << "/" << /* degree */ (u_out_interval.second - u_out_interval.first) << "] neighbour: " << w);
                    // check whether it's also a neighbour of v
                    if(neighbours.count(w) == 1){
                        COUT_DEBUG_LCC("Triangle found " << v << " - " << u << " - " << w);
                        num_triangles++;
                    }

                    iterator2.next();
                }
                iterator2.close();
                iterator1.next();
            }
            iterator1.close();

            // register the final score
            uint64_t max_num_edges = v_degree_out * (v_degree_out -1);
            lcc[v] = static_cast<double>(num_triangles) / max_num_edges;
            COUT_DEBUG_LCC("Score computed: " << (num_triangles) << "/" << max_num_edges << " = " << lcc[v]);
        }

        return ptr_lcc;
    }
    void GTXDriver::lcc(const char* dump2file) {
        if(m_is_directed) { ERROR("Implementation of LCC supports only undirected graphs"); }

        utility::TimeoutService timeout { m_timeout };
        Timer timer; timer.start();
        gt::SharedROTransaction transaction = GTX->begin_shared_read_only_transaction();
        uint64_t max_vertex_id = GTX->get_max_allocated_vid();

        // Run the LCC algorithm
        unique_ptr<double[]> scores = do_lcc_undirected(transaction, max_vertex_id, timeout);
        if(timeout.is_timeout()){ transaction.commit(); RAISE_EXCEPTION(TimeoutError, "Timeout occurred after " << timer);  }

        // Translate the vertex IDs
        auto external_ids = translate(&transaction, scores.get(), max_vertex_id);
        transaction.commit(); // read-only transaction, abort == commit
        if(timeout.is_timeout()){ RAISE_EXCEPTION(TimeoutError, "Timeout occurred after " << timer); }

        // Store the results in the given file
        if(dump2file != nullptr)
            save_results(external_ids, dump2file);
    }
/*****************************************************************************
 *                                                                           *
 *  SSSP                                                                     *
 *                                                                           *
 *****************************************************************************/
// Implementation based on the reference SSSP for the GAP Benchmark Suite
// https://github.com/sbeamer/gapbs
// The reference implementation has been written by Scott Beamer
//
// Copyright (c) 2015, The Regents of the University of California (Regents)
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. Neither the name of the Regents nor the
//    names of its contributors may be used to endorse or promote products
//    derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL REGENTS BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    using NodeID = uint64_t;
    using WeightT = double;
    static const size_t kMaxBin = numeric_limits<size_t>::max()/2;
    static
    gapbs::pvector<WeightT> do_sssp(gt::SharedROTransaction& transaction, uint64_t num_edges, uint64_t max_vertex_id, uint64_t source, double delta, utility::TimeoutService& timer) {
        // Init
        gapbs::pvector<WeightT> dist(max_vertex_id, numeric_limits<WeightT>::infinity());
        dist[source-1] = 0;
        gapbs::pvector<NodeID> frontier(num_edges);
        // two element arrays for double buffering curr=iter&1, next=(iter+1)&1
        size_t shared_indexes[2] = {0, kMaxBin};
        size_t frontier_tails[2] = {1, 0};
        frontier[0] = source;
         auto graph = transaction.get_graph();
#pragma omp parallel
        {
            uint8_t thread_id = graph->get_openmp_worker_thread_id();
            auto iterator = transaction.generate_edge_delta_iterator(thread_id);
            vector<vector<NodeID> > local_bins(0);
            size_t iter = 0;

            while (shared_indexes[iter&1] != kMaxBin) {
                size_t &curr_bin_index = shared_indexes[iter&1];
                size_t &next_bin_index = shared_indexes[(iter+1)&1];
                size_t &curr_frontier_tail = frontier_tails[iter&1];
                size_t &next_frontier_tail = frontier_tails[(iter+1)&1];
#pragma omp for nowait schedule(dynamic, 64)
                for (size_t i=0; i < curr_frontier_tail; i++) {
                    NodeID u = frontier[i];
                    if (dist[u-1] >= delta * static_cast<WeightT>(curr_bin_index)) {
                        //auto iterator = transaction.simple_get_edges(u, /* label */ 1,thread_id);
                        transaction.simple_get_edges(u,1,thread_id,iterator);
                        while(iterator.valid()){
                            uint64_t v = iterator.dst_id();
                           // string_view payload = iterator.edge_delta_data();
                           // double w = *reinterpret_cast<const double*>(payload.data());
                            double w = iterator.edge_delta_weight();
                            WeightT old_dist = dist[v-1];
                            WeightT new_dist = dist[u-1] + w;
                            if (new_dist < old_dist) {
                                bool changed_dist = true;
                                while (!gapbs::compare_and_swap(dist[v-1], old_dist, new_dist)) {
                                    old_dist = dist[v-1];
                                    if (old_dist <= new_dist) {
                                        changed_dist = false;
                                        break;
                                    }
                                }
                                if (changed_dist) {
                                    size_t dest_bin = new_dist/delta;
                                    if (dest_bin >= local_bins.size()) {
                                        local_bins.resize(dest_bin+1);
                                    }
                                    local_bins[dest_bin].push_back(v);
                                }
                            }

                            //iterator.next();
                        }
                        //iterator.close();
                    }
                }
                iterator.close();
                for (size_t i=curr_bin_index; i < local_bins.size(); i++) {
                    if (!local_bins[i].empty()) {
#pragma omp critical
                        next_bin_index = min(next_bin_index, i);
                        break;
                    }
                }

#pragma omp barrier
#pragma omp single nowait
                {
                    curr_bin_index = kMaxBin;
                    curr_frontier_tail = 0;
                }

                if (next_bin_index < local_bins.size()) {
                    size_t copy_start = gapbs::fetch_and_add(next_frontier_tail, local_bins[next_bin_index].size());
                    copy(local_bins[next_bin_index].begin(), local_bins[next_bin_index].end(), frontier.data() + copy_start);
                    local_bins[next_bin_index].resize(0);
                }

                iter++;
#pragma omp barrier
            }

#if defined(DEBUG)
            #pragma omp single
        COUT_DEBUG("took " << iter << " iterations");
#endif
            transaction.thread_on_openmp_section_finish(thread_id);
        }
        graph->on_openmp_section_finishing();

        return dist;
    }

    static
    gapbs::pvector<WeightT> do_static_sssp(gt::SharedROTransaction& transaction, uint64_t num_edges, uint64_t max_vertex_id, uint64_t source, double delta, utility::TimeoutService& timer) {
        // Init
        gapbs::pvector<WeightT> dist(max_vertex_id, numeric_limits<WeightT>::infinity());
        dist[source-1] = 0;
        gapbs::pvector<NodeID> frontier(num_edges);
        // two element arrays for double buffering curr=iter&1, next=(iter+1)&1
        size_t shared_indexes[2] = {0, kMaxBin};
        size_t frontier_tails[2] = {1, 0};
        frontier[0] = source;
         //auto graph = transaction.get_graph();
#pragma omp parallel
        {
            //uint8_t thread_id = graph->get_openmp_worker_thread_id();
            auto iterator = transaction.generate_static_edge_delta_iterator();
            vector<vector<NodeID> > local_bins(0);
            size_t iter = 0;

            while (shared_indexes[iter&1] != kMaxBin) {
                size_t &curr_bin_index = shared_indexes[iter&1];
                size_t &next_bin_index = shared_indexes[(iter+1)&1];
                size_t &curr_frontier_tail = frontier_tails[iter&1];
                size_t &next_frontier_tail = frontier_tails[(iter+1)&1];
#pragma omp for nowait schedule(dynamic, 64)
                for (size_t i=0; i < curr_frontier_tail; i++) {
                    NodeID u = frontier[i];
                    if (dist[u-1] >= delta * static_cast<WeightT>(curr_bin_index)) {
                        //auto iterator = transaction.static_get_edges(u, /* label */ 1);
                        transaction.static_get_edges(u,1,iterator);
                        while(iterator.valid()){
                            uint64_t v = iterator.dst_id();
                            /*string_view payload = iterator.edge_delta_data();
                            double w = *reinterpret_cast<const double*>(payload.data());*/
                            double w = iterator.get_weight();
                            WeightT old_dist = dist[v-1];
                            WeightT new_dist = dist[u-1] + w;
                            if (new_dist < old_dist) {
                                bool changed_dist = true;
                                while (!gapbs::compare_and_swap(dist[v-1], old_dist, new_dist)) {
                                    old_dist = dist[v-1];
                                    if (old_dist <= new_dist) {
                                        changed_dist = false;
                                        break;
                                    }
                                }
                                if (changed_dist) {
                                    size_t dest_bin = new_dist/delta;
                                    if (dest_bin >= local_bins.size()) {
                                        local_bins.resize(dest_bin+1);
                                    }
                                    local_bins[dest_bin].push_back(v);
                                }
                            }

                            //iterator.next();
                        }
                        //iterator.close();
                    }
                }

                for (size_t i=curr_bin_index; i < local_bins.size(); i++) {
                    if (!local_bins[i].empty()) {
#pragma omp critical
                        next_bin_index = min(next_bin_index, i);
                        break;
                    }
                }

#pragma omp barrier
#pragma omp single nowait
                {
                    curr_bin_index = kMaxBin;
                    curr_frontier_tail = 0;
                }

                if (next_bin_index < local_bins.size()) {
                    size_t copy_start = gapbs::fetch_and_add(next_frontier_tail, local_bins[next_bin_index].size());
                    copy(local_bins[next_bin_index].begin(), local_bins[next_bin_index].end(), frontier.data() + copy_start);
                    local_bins[next_bin_index].resize(0);
                }

                iter++;
#pragma omp barrier
            }

#if defined(DEBUG)
            #pragma omp single
        COUT_DEBUG("took " << iter << " iterations");
#endif
            //transaction.thread_on_openmp_section_finish(thread_id);
        }
        //graph->on_openmp_section_finishing();

        return dist;
    }

    void GTXDriver::sssp(uint64_t source_vertex_id, const char* dump2file) {
        //std::cout<<"sssp running"<<std::endl;
        //uint64_t max_vertex_id = GTX->get_max_allocated_vid()+100000;
        uint64_t root = ext2int(source_vertex_id);
        //uint64_t root = 128;
       auto handler = GTX->get_sssp_handler(/*max_vertex_id*/);
        double delta = 2.0;
        //std::cout<<"before entering compute"<<std::endl;
        handler.compute(root,delta);
        if(dump2file != nullptr)
            save_results<double, false>(*handler.get_result(),dump2file);
        //std::cout<<"sssp ran"<<std::endl;
       /*utility::TimeoutService timeout { m_timeout };
        Timer timer; timer.start();
        gt::SharedROTransaction transaction = GTX->begin_shared_read_only_transaction();
        uint64_t num_edges = m_num_edges;
        uint64_t max_vertex_id = GTX->get_max_allocated_vid();
        uint64_t root = ext2int(source_vertex_id);

        // Run the SSSP algorithm
        double delta = 2.0; // same value used in the GAPBS, at least for most graphs
        //auto distances = do_sssp(transaction, num_edges, max_vertex_id, root, delta, timeout);
        auto distances = do_static_sssp(transaction, num_edges, max_vertex_id, root, delta, timeout);
        if(timeout.is_timeout()){ transaction.commit(); RAISE_EXCEPTION(TimeoutError, "Timeout occurred after " << timer);  }

        // Translate the vertex IDs
        //auto external_ids = translate(&transaction, distances.data(), max_vertex_id);
        auto external_ids = static_translate(&transaction, distances.data(), max_vertex_id);
        transaction.commit(); // read-only transaction, abort == commit
        if(timeout.is_timeout()){ RAISE_EXCEPTION(TimeoutError, "Timeout occurred after " << timer); }

        // Store the results in the given file
        if(dump2file != nullptr)
            save_results(external_ids, dump2file);*/
        //std::cout<<"sssp ran"<<std::endl;
    }
}//namespace gfe::library
