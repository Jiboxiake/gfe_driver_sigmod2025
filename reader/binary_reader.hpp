//
// Created by per on 08.02.21.
//

#ifndef GFE_DRIVER_BINARY_READER_HPP
#define GFE_DRIVER_BINARY_READER_HPP

#include <vector>
#include <string>
#include <random>

#include "reader.hpp"

using namespace std;

namespace gfe::reader {

    class BinaryReader : public Reader {

        struct edge_t {
            uint64_t src;
            uint64_t dst;
        };

        vector <edge_t> edges;  // vector to load edges into
        size_t pos {0}; // Position in the edge vector to read next.

    public:
        /**
         * Read the edge list from the given file
         * @param path the source of the file
         */
        BinaryReader(const std::string &path);

        ~BinaryReader();

        bool read(graph::WeightedEdge &) override;

        bool is_directed() const override;

    private:
        std::mt19937_64 m_random_generator;
    };
}

#endif //GFE_DRIVER_BINARY_READER_HPP
