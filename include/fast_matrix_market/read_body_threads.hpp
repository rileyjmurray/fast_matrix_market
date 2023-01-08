// Copyright (C) 2022 Adam Lugowski. All rights reserved.
// Use of this source code is governed by the BSD 2-clause license found in the LICENSE.txt file.

#pragma once

#include <future>
#include <queue>

#include "fast_matrix_market.hpp"
#include "3rdparty/BS_thread_pool_light.hpp"

namespace fast_matrix_market {

    struct line_count_result {
        std::string chunk;
        int64_t chunk_line_start = -1;
        int64_t chunk_line_count = -1;
    };

    inline line_count_result count_chunk_lines(line_count_result lcr) {
        lcr.chunk_line_count = count_lines(lcr.chunk);
        return lcr;
    }

    template <typename HANDLER>
    int64_t read_body_threads(std::istream& instream, const matrix_market_header& header,
                              HANDLER& handler, const read_options& options = {}) {
        /*
         * Pipeline:
         * 1. Read chunk
         * 2. Calculate chunk's line count
         * 3. Parse chunk.
         *
         * The line count is needed for
         * 1. for array files the line number determines the row/column indices of the value
         * 2. for coordinate files the line number determines the chunk's offset into the result arrays
         * 3. for error messages
         *
         * We do the I/O reading only in the main thread. Everything else is done by tasks in a thread pool.
         *
         * The line count is fast, but we still spawn line count tasks. The futures for these tasks are saved in a
         * queue so they can be retrieved in order. This way we can easily keep track of the line number of each chunk.
         *
         * Once a line count is complete we spawn a task to parse this chunk. We also then read another chunk from
         * the input stream.
         *
         * The line count step is significantly faster than the parse step. As a form of backpressure we don't read
         * additional chunks if there are too many inflight chunks.
         */
        auto line_num = header.header_line_count;

        std::queue<std::future<line_count_result>> line_count_futures;
        BS::thread_pool_light pool(options.num_threads);

        // Number of concurrent chunks available to work on.
        // Too few may starve workers (such as due to uneven chunk splits)
        // Too many increases costs, such as storing chunk results in memory before they're written.
        const unsigned inflight_count = 10 * pool.get_thread_count();

        // Start reading chunks and counting lines.
        for (unsigned seed_i = 0; seed_i < inflight_count && instream.good(); ++seed_i) {
            line_count_result lcr;
            lcr.chunk = get_next_chunk(instream, options);

            line_count_futures.push(pool.submit(count_chunk_lines, lcr));
        }

        // Read chunks in order as they become available.
        while (!line_count_futures.empty()) {
            if (pool.get_tasks_total() < inflight_count && is_ready(line_count_futures.front())) {
                // Next chunk has finished line count.

                // Start another to replace it.
                if (instream.good()) {
                    line_count_result lcr;
                    lcr.chunk = get_next_chunk(instream, options);

                    line_count_futures.push(pool.submit(count_chunk_lines, lcr));
                }

                // Figure out where this chunk belongs
                line_count_result lcr = line_count_futures.front().get();
                line_count_futures.pop();

                lcr.chunk_line_start = line_num;
                line_num += lcr.chunk_line_count;

                // Parse it.
                auto body_line = lcr.chunk_line_start - header.header_line_count;
                auto chunk_handler = handler.get_chunk_handler(body_line);
                if (header.format == array) {
                    // compute the row/column
                    typename HANDLER::coordinate_type row = body_line % header.ncols;
                    typename HANDLER::coordinate_type col = body_line / header.ncols;

                    std::ignore = pool.submit([=]() mutable {
                        read_chunk_array(lcr.chunk, header, lcr.chunk_line_start, chunk_handler, row, col);
                    });
                } else if (header.object == matrix) {
                    std::ignore = pool.submit([=]() mutable {
                        read_chunk_matrix_coordinate(lcr.chunk, header, lcr.chunk_line_start, chunk_handler, options);
                    });
                } else {
                    std::ignore = pool.submit([=]() mutable {
                        read_chunk_vector_coordinate(lcr.chunk, header, lcr.chunk_line_start, chunk_handler);
                    });
                }
            } else {
                // Next chunk is not done. Yield CPU for it.
                std::this_thread::yield();
            }
        }

        pool.wait_for_tasks();

        return line_num;
    }
}