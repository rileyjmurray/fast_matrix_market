// Copyright (C) 2022 Adam Lugowski. All rights reserved.
// Use of this source code is governed by the BSD 2-clause license found in the LICENSE.txt file.

#pragma once

#include <algorithm>

#include "fast_matrix_market.hpp"

namespace fast_matrix_market {

    template <typename T, bool is_value, typename std::enable_if<is_value, int>::type = 0>
    std::string column_to_str(const T& c) {
        return value_to_string(c);
    }

    template <typename T, bool is_value, typename std::enable_if<!is_value, int>::type = 0>
    std::string column_to_str(const T& c) {
        return int_to_string(c + 1);
    }

    /**
     * Format row, column, value vectors.
     *
     * Value range may be empty (i.e. val_begin == val_end) to omit writing values at all. Useful for pattern matrices.
     *
     * @tparam A_ITER
     * @tparam B_ITER
     * @tparam C_ITER Must be a valid iterator, but if begin==end then the values are not written.
     * @tparam COLUMN_IS_VALUE if true then the B_ITER values are not incremented.
     */
    template<typename A_ITER, typename B_ITER, typename C_ITER, bool COLUMN_IS_VALUE = false>
    class triplet_formatter {
    public:
        explicit triplet_formatter(const A_ITER row_begin, const A_ITER row_end,
                                   const B_ITER col_begin, const B_ITER col_end,
                                   const C_ITER val_begin, const C_ITER val_end) :
                                   row_iter(row_begin), row_end(row_end),
                                   col_iter(col_begin),
                                   val_iter(val_begin), val_end(val_end) {
            if (row_end - row_begin != col_end - col_begin ||
                    (row_end - row_begin != val_end - val_begin && val_end != val_begin)) {
                throw invalid_argument("Row, column, and value ranges must have equal length.");
            }
        }

        [[nodiscard]] bool has_next() const {
            return row_iter != row_end;
        }

        class chunk {
        public:
            explicit chunk(const A_ITER row_begin, const A_ITER row_end,
                           const B_ITER col_begin,
                           const C_ITER val_begin, const C_ITER val_end) :
                    row_iter(row_begin), row_end(row_end),
                    col_iter(col_begin),
                    val_iter(val_begin), val_end(val_end) {}

            std::string operator()() {
                std::string chunk;
                chunk.reserve((row_end - row_iter)*25);

                for (; row_iter != row_end; ++row_iter, ++col_iter) {
                    chunk += int_to_string(*row_iter + 1);
                    chunk += kSpace;
                    chunk += column_to_str<decltype(*col_iter), COLUMN_IS_VALUE>(*col_iter);

                    if (val_iter != val_end) {
                        chunk += kSpace;
                        chunk += value_to_string(*val_iter);
                        ++val_iter;
                    }
                    chunk += kNewline;
                }

                return chunk;
            }

            A_ITER row_iter, row_end;
            B_ITER col_iter;
            C_ITER val_iter, val_end;
        };

        chunk next_chunk(const write_options& options) {
            auto chunk_size = std::min(options.chunk_size_values, (int64_t)(row_end - row_iter));
            A_ITER row_chunk_end = row_iter + chunk_size;
            B_ITER col_chunk_end = col_iter + chunk_size;
            C_ITER val_chunk_end = (val_iter != val_end) ? val_iter + chunk_size: val_end;

            chunk c(row_iter, row_chunk_end,
                    col_iter,
                    val_iter, val_chunk_end);

            row_iter = row_chunk_end;
            col_iter = col_chunk_end;
            val_iter = val_chunk_end;

            return c;
        }

    protected:
        A_ITER row_iter, row_end;
        B_ITER col_iter;
        C_ITER val_iter, val_end;
    };

    /**
     * Format CSC structures.
     */
    template<typename PTR_ITER, typename IND_ITER, typename VAL_ITER>
    class csc_formatter {
    public:
        explicit csc_formatter(const PTR_ITER ptr_begin, const PTR_ITER ptr_end,
                               const IND_ITER ind_begin, const IND_ITER ind_end,
                               const VAL_ITER val_begin, const VAL_ITER val_end,
                               bool transpose = false) :
                ptr_begin(ptr_begin), ptr_iter(ptr_begin), ptr_end(ptr_end),
                ind_begin(ind_begin),
                val_begin(val_begin), val_end(val_end),
                transpose(transpose) {
            if (ind_end - ind_begin != val_end - val_begin && val_end != val_begin) {
                throw invalid_argument("Index and value ranges must have equal length.");
            }

            auto num_columns = (ptr_end - ptr_iter);
            auto nnz = (ind_end - ind_begin);
            nnz_per_column = ((double)nnz) / num_columns;
        }

        [[nodiscard]] bool has_next() const {
            return ptr_iter != ptr_end;
        }

        class chunk {
        public:
            explicit chunk(const PTR_ITER ptr_begin, const PTR_ITER ptr_iter, const PTR_ITER ptr_end,
                           const IND_ITER ind_begin,
                           const VAL_ITER val_begin, const VAL_ITER val_end,
                           bool transpose) :
                    ptr_begin(ptr_begin), ptr_iter(ptr_iter), ptr_end(ptr_end),
                    ind_begin(ind_begin),
                    val_begin(val_begin), val_end(val_end),
                    transpose(transpose) {}

            std::string operator()() {
                std::string chunk;
                chunk.reserve((ptr_end - ptr_iter)*250);

                // emit the columns [ptr_iter, ptr_end)

                // iterate over assigned columns
                for (; ptr_iter != ptr_end; ++ptr_iter) {
                    auto column_number = (int64_t)(ptr_iter - ptr_begin);
                    std::string col = int_to_string(column_number + 1);

                    // iterate over rows in column
                    IND_ITER row_end = ind_begin + *(ptr_iter+1);
                    IND_ITER row_iter = ind_begin + *ptr_iter;
                    VAL_ITER val_iter = val_begin;
                    if (val_begin != val_end) {
                        val_iter = val_begin + *ptr_iter;
                    }
                    for (; row_iter != row_end; ++row_iter) {
                        auto row = int_to_string(*row_iter + 1);

                        if (transpose) {
                            chunk += col;
                            chunk += kSpace;
                            chunk += row;
                        } else {
                            chunk += row;
                            chunk += kSpace;
                            chunk += col;
                        }

                        if (val_iter != val_end) {
                            chunk += kSpace;
                            chunk += value_to_string(*val_iter);
                            ++val_iter;
                        }
                        chunk += kNewline;
                    }
                }

                return chunk;
            }

            PTR_ITER ptr_begin, ptr_iter, ptr_end;
            IND_ITER ind_begin;
            VAL_ITER val_begin, val_end;
            bool transpose;
        };

        chunk next_chunk(const write_options& options) {
            auto num_columns = (int64_t)(nnz_per_column * (double)options.chunk_size_values + 1);

            num_columns = std::min(num_columns, (int64_t)(ptr_end - ptr_iter));
            PTR_ITER ptr_chunk_end = ptr_iter + num_columns;

            chunk c(ptr_begin, ptr_iter, ptr_chunk_end,
                    ind_begin,
                    val_begin, val_end,
                    transpose);

            ptr_iter = ptr_chunk_end;

            return c;
        }

    protected:
        PTR_ITER ptr_begin, ptr_iter, ptr_end;
        IND_ITER ind_begin;
        VAL_ITER val_begin, val_end;
        bool transpose;
        double nnz_per_column;
    };

    /**
     * Format dense row-major arrays.
     */
    template<typename VT>
    class row_major_array_formatter {
    public:
        explicit row_major_array_formatter(const std::vector<VT> &values, int64_t nrows) : values(values), nrows(nrows), ncols(values.size() / nrows) {}

        [[nodiscard]] bool has_next() const {
            return cur_row != nrows;
        }

        class chunk {
        public:
            explicit chunk(const std::vector<VT> &values, int64_t nrows, int64_t ncols, int64_t cur_row) :
            values(values), nrows(nrows), ncols(ncols), cur_row(cur_row) {}

            std::string operator()() {
                std::string c;
                c.reserve(ncols * 15);

                for (int64_t i = 0; i < ncols; ++i) {
                    c += value_to_string(values[(cur_row * ncols) + i]);
                    c += kNewline;
                }

                return c;
            }

            const std::vector<VT>& values;
            int64_t nrows, ncols;
            int64_t cur_row;
        };

        chunk next_chunk([[maybe_unused]] const write_options& options) {
            return chunk(values, nrows, ncols, cur_row++);
        }

    protected:
        const std::vector<VT>& values;
        int64_t nrows, ncols;
        int64_t cur_row = 0;
    };
}
