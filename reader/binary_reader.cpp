//
// Created by per on 08.02.21.
//

#include "binary_reader.hpp"

#include <fstream>
#include "graph/edge.hpp"
#include "configuration.hpp"



namespace gfe::reader {

    BinaryReader::BinaryReader(const std::string &path) : m_random_generator(configuration().seed() + 12908478) {
      ifstream f(path, ifstream::in | ifstream::binary);

      size_t edge_count;
      f.read((char *) &edge_count, sizeof(edge_count));

      edges.clear();
      edges.resize(edge_count);

      f.read((char *) edges.data(), edge_count * sizeof(edge_t));

      f.close();
    }

    BinaryReader::~BinaryReader() {

    }

    bool BinaryReader::read(graph::WeightedEdge &edge) {
      if (pos < edges.size()) {
        edge_t e = edges[pos];
        pos += 1;
        edge.m_source = e.src;
        edge.m_destination = e.dst;

        uniform_real_distribution<double> distribution{0, configuration().max_weight()}; // in [0, max_weight)
        auto w = distribution(m_random_generator);
        if(w == 0.0) {
          w = configuration().max_weight(); // in (0, max_weight]
        }

        edge.m_weight = w;
        return true;
      } else {
        return false;
      }
    }

    bool BinaryReader::is_directed() const {
      return false;
    }
}

