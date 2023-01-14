// Copyright (C) 2022-2023 Adam Lugowski. All rights reserved.
// Use of this source code is governed by the BSD 2-clause license found in the LICENSE.txt file.

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

#include <fstream>
#include <fast_matrix_market/fast_matrix_market.hpp>

namespace py = pybind11;
using namespace pybind11::literals;
namespace fmm = fast_matrix_market;

fmm::matrix_market_header read_header_file(const std::string& filename) {
    std::ifstream f(filename);
    fmm::matrix_market_header header;
    fast_matrix_market::read_header(f, header);
    return header;
}

fmm::matrix_market_header read_header_string(const std::string& str) {
    std::istringstream f(str);
    fmm::matrix_market_header header;
    fast_matrix_market::read_header(f, header);
    return header;
}

void write_header_file(const fmm::matrix_market_header& header, const std::string& filename) {
    std::ofstream f(filename);
    fast_matrix_market::write_header(f, header);
}

std::string write_header_string(const fmm::matrix_market_header& header) {
    std::ostringstream f;
    fast_matrix_market::write_header(f, header);
    return f.str();
}

std::tuple<int64_t, int64_t> get_header_shape(const fmm::matrix_market_header& header) {
    return std::make_tuple(header.nrows, header.ncols);
}

void set_header_shape(fmm::matrix_market_header& header, const std::tuple<int64_t, int64_t>& shape) {
    header.nrows = std::get<0>(shape);
    header.ncols = std::get<1>(shape);
}

std::string get_header_object(const fmm::matrix_market_header& header) {
    return fmm::object_map.at(header.object);
}
std::string get_header_format(const fmm::matrix_market_header& header) {
    return fmm::format_map.at(header.format);
}
std::string get_header_field(const fmm::matrix_market_header& header) {
    return fmm::field_map.at(header.field);
}
std::string get_header_symmetry(const fmm::matrix_market_header& header) {
    return fmm::symmetry_map.at(header.symmetry);
}

void set_header_object(fmm::matrix_market_header& header, const std::string& value) {
    header.object = fmm::parse_enum<fmm::object_type>(value, fmm::object_map);
}
void set_header_format(fmm::matrix_market_header& header, const std::string& value) {
    header.format = fmm::parse_enum<fmm::format_type>(value, fmm::format_map);
}
void set_header_field(fmm::matrix_market_header& header, const std::string& value) {
    header.field = fmm::parse_enum<fmm::field_type>(value, fmm::field_map);
}
void set_header_symmetry(fmm::matrix_market_header& header, const std::string& value) {
    header.symmetry = fmm::parse_enum<fmm::symmetry_type>(value, fmm::symmetry_map);
}

fmm::matrix_market_header create_header(const std::tuple<int64_t, int64_t>& shape, int64_t nnz,
                                        const std::string& comment,
                                        const std::string& object, const std::string& format,
                                        const std::string& field, const std::string& symmetry) {
    fmm::matrix_market_header header{};
    set_header_shape(header, shape);
    header.nnz = nnz;
    header.comment = comment;
    set_header_object(header, object);
    set_header_format(header, format);
    set_header_field(header, field);
    set_header_symmetry(header, symmetry);
    return header;
}

py::dict header_to_dict(fmm::matrix_market_header& header) {
    py::dict dict;
    dict["shape"] = py::make_tuple(header.nrows, header.ncols);
    dict["nnz"] = header.nnz;
    dict["comment"] = header.comment;
    dict["object"] = get_header_object(header);
    dict["format"] = get_header_format(header);
    dict["field"] = get_header_field(header);
    dict["symmetry"] = get_header_symmetry(header);

    return dict;
}

std::string header_repr(const fmm::matrix_market_header& header) {
    std::ostringstream oss;
    oss << "header(";
    oss << "shape=(" << header.nrows << ", " << header.ncols << "), ";
    oss << "nnz=" << header.nnz << ", ";
    oss << "comment=\"" << header.comment << "\", ";
    oss << "object=\"" << get_header_object(header) << "\", ";
    oss << "format=\"" << get_header_format(header) << "\", ";
    oss << "field=\"" << get_header_field(header) << "\", ";
    oss << "symmetry=\"" << get_header_symmetry(header) << "\"";
    oss << ")";
    return oss.str();
}


struct read_cursor {
    read_cursor(const std::string& filename): stream(std::make_unique<std::ifstream>(filename)) {}
    read_cursor(const std::string& str, bool string_source): stream(std::make_unique<std::istringstream>(str)) {}

    std::unique_ptr<std::istream> stream;

    fmm::matrix_market_header header{};
    fmm::read_options options{};
};

void open_read_rest(read_cursor& cursor) {
    // This is done later in Python to match SciPy behavior
    cursor.options.generalize_symmetry = false;

    // read header
    fmm::read_header(*cursor.stream, cursor.header);
}

read_cursor open_read_file(const std::string& filename, int num_threads) {
    read_cursor cursor(filename);
    // Set options
    cursor.options.num_threads = num_threads;

    open_read_rest(cursor);
    return cursor;
}

read_cursor open_read_string(const std::string& str, int num_threads) {
    read_cursor cursor(str, true);
    // Set options
    cursor.options.num_threads = num_threads;

    open_read_rest(cursor);
    return cursor;
}

/**
 * Read Matrix Market body into a numpy array.
 *
 * @param cursor Opened by open_read().
 * @param array NumPy array. Assumed to be the correct size and zeroed out.
 */
template <typename T>
void read_body_array(read_cursor& cursor, py::array_t<T>& array) {
    auto unchecked = array.mutable_unchecked();
    auto handler = fmm::dense_2d_call_adding_parse_handler<decltype(unchecked), int64_t, T>(unchecked);
    fmm::read_matrix_market_body(*cursor.stream, cursor.header, handler, 1, cursor.options);
}


/**
 * Triplet handler. Separate row, column, value iterators.
 */
template<typename IT, typename VT, typename IT_ARR, typename VT_ARR>
class triplet_numpy_parse_handler {
public:
    using coordinate_type = IT;
    using value_type = VT;
    static constexpr int flags = fmm::kParallelOk;

    explicit triplet_numpy_parse_handler(IT_ARR& rows,
                                         IT_ARR& cols,
                                         VT_ARR& values,
                                         int64_t offset = 0) : rows(rows), cols(cols), values(values), offset(offset) {}

    void handle(const coordinate_type row, const coordinate_type col, const value_type value) {
        rows(offset) = row;
        cols(offset) = col;
        values(offset) = value;

        ++offset;
    }

    triplet_numpy_parse_handler<IT, VT, IT_ARR, VT_ARR> get_chunk_handler(int64_t offset_from_begin) {
        return triplet_numpy_parse_handler(rows, cols, values, offset_from_begin);
    }

protected:
    IT_ARR& rows;
    IT_ARR& cols;
    VT_ARR& values;

    int64_t offset;
};


template <typename IT, typename VT>
void read_body_triplet(read_cursor& cursor, py::array_t<IT>& row, py::array_t<IT>& col, py::array_t<VT>& data) {
    if (row.size() != cursor.header.nnz || col.size() != cursor.header.nnz || data.size() != cursor.header.nnz) {
        throw std::invalid_argument("NumPy Array sizes need to equal matrix nnz");
    }
    auto row_unchecked = row.mutable_unchecked();
    auto col_unchecked = col.mutable_unchecked();
    auto data_unchecked = data.mutable_unchecked();
    auto handler = triplet_numpy_parse_handler<IT, VT, decltype(row_unchecked), decltype(data_unchecked)>(row_unchecked, col_unchecked, data_unchecked);
    fmm::read_matrix_market_body(*cursor.stream, cursor.header, handler, 1, cursor.options);
}


struct write_cursor {
    write_cursor(const std::string& filename): stream(std::make_unique<std::ofstream>(filename)) {}
    write_cursor(): stream(std::make_unique<std::ostringstream>()), is_string(true) {}

    std::unique_ptr<std::ostream> stream;
    bool is_string = false;

    fmm::matrix_market_header header{};
    fmm::write_options options{};

    std::string get_string() {
        if (!is_string) {
            return "";
        }
        return static_cast<std::ostringstream*>(stream.get())->str();
    }
};

write_cursor open_write_file(const std::string& filename, const fmm::matrix_market_header& header, int num_threads) {
    write_cursor cursor(filename);
    // Set options
    cursor.options.num_threads = num_threads;
    cursor.header = header;
    return cursor;
}

write_cursor open_write_string(fmm::matrix_market_header& header, int num_threads) {
    write_cursor cursor;
    // Set options
    cursor.options.num_threads = num_threads;
    cursor.header = header;
    return cursor;
}

void write_header_only(write_cursor& cursor) {
    fmm::write_header(*cursor.stream, cursor.header);
}

template <typename T>
void write_array(write_cursor& cursor, py::array_t<T>& array) {
    if (array.ndim() != 2) {
        throw std::invalid_argument("Only 2D arrays supported.");
    }

    cursor.header.nrows = array.shape(0);
    cursor.header.ncols = array.shape(1);

    cursor.header.object = fmm::matrix;
    cursor.header.field = fmm::get_field_type((const T*)nullptr);
    cursor.header.format = fmm::array;
    cursor.header.symmetry = fmm::general;

    fmm::write_header(*cursor.stream, cursor.header);

    auto unchecked = array.unchecked();
    auto formatter = fmm::dense_2d_call_formatter<decltype(unchecked), int64_t>(unchecked, cursor.header.nrows, cursor.header.ncols);
    fmm::write_body(*cursor.stream, formatter, cursor.options);
}


template<typename ARR, typename T>
class py_array_iterator
{
public:
    using value_type = T;
    using difference_type = int64_t;

    py_array_iterator(ARR& array) : array(array), index(0) {}
    py_array_iterator(ARR& array, int64_t index) : array(array), index(index) {}
    py_array_iterator(const py_array_iterator &rhs) : array(rhs.array), index(rhs.index) {}
    /* py_array_iterator& operator=(Type* rhs) {index = rhs; return *this;} */
    py_array_iterator& operator=(const py_array_iterator &rhs) {index = rhs.index; return *this;}
    py_array_iterator& operator+=(difference_type rhs) {index += rhs; return *this;}
    py_array_iterator& operator-=(difference_type rhs) {index -= rhs; return *this;}
    T operator*() const {return array(index);}
//    T* operator->() const {return index;}
    T operator[](difference_type rhs) const {return array(index + rhs);}

    py_array_iterator& operator++() {++index; return *this;}
    py_array_iterator& operator--() {--index; return *this;}
    py_array_iterator operator++(int) {py_array_iterator tmp(*this); ++index; return tmp;}
    py_array_iterator operator--(int) {py_array_iterator tmp(*this); --index; return tmp;}
    /* py_array_iterator operator+(const py_array_iterator& rhs) {return py_array_iterator(array, index+rhs.index);} */
    difference_type operator-(const py_array_iterator& rhs) const {return index-rhs.index;}
    py_array_iterator operator+(difference_type rhs) const {return py_array_iterator(array, index+rhs);}
    py_array_iterator operator-(difference_type rhs) const {return py_array_iterator(array, index-rhs);}
    friend py_array_iterator operator+(difference_type lhs, const py_array_iterator& rhs) {return py_array_iterator(rhs.array, lhs+rhs.index);}
    friend py_array_iterator operator-(difference_type lhs, const py_array_iterator& rhs) {return py_array_iterator(rhs.array, lhs-rhs.index);}

    bool operator==(const py_array_iterator& rhs) const {return index == rhs.index;}
    bool operator!=(const py_array_iterator& rhs) const {return index != rhs.index;}
    bool operator>(const py_array_iterator& rhs) const {return index > rhs.index;}
    bool operator<(const py_array_iterator& rhs) const {return index < rhs.index;}
    bool operator>=(const py_array_iterator& rhs) const {return index >= rhs.index;}
    bool operator<=(const py_array_iterator& rhs) const {return index <= rhs.index;}
private:
    ARR& array;
    int64_t index;
};


template <typename IT, typename VT>
void write_triplet(write_cursor& cursor, const std::tuple<int64_t, int64_t>& shape,
                   py::array_t<IT>& rows, py::array_t<IT>& cols, py::array_t<VT>& data) {
    if (rows.size() != cols.size()) {
        throw std::invalid_argument("len(row) must equal len(col).");
    }
    if (rows.size() != data.size() && data.size() != 0) {
        throw std::invalid_argument("len(row) must equal len(data).");
    }

    cursor.header.nrows = std::get<0>(shape);
    cursor.header.ncols = std::get<1>(shape);
    cursor.header.nnz = rows.size();

    cursor.header.object = fmm::matrix;
    cursor.header.field = data.size() == 0 ? fmm::pattern : fmm::get_field_type((const VT*)nullptr);
    cursor.header.format = fmm::coordinate;
    cursor.header.symmetry = fmm::general;

    fmm::write_header(*cursor.stream, cursor.header);

    auto rows_unchecked = rows.unchecked();
    auto cols_unchecked = cols.unchecked();
    auto data_unchecked = data.unchecked();
    auto formatter = fmm::triplet_formatter(py_array_iterator<decltype(rows_unchecked), IT>(rows_unchecked),
                                            py_array_iterator<decltype(rows_unchecked), IT>(rows_unchecked, rows_unchecked.size()),
                                            py_array_iterator<decltype(cols_unchecked), IT>(cols_unchecked),
                                            py_array_iterator<decltype(cols_unchecked), IT>(cols_unchecked, cols_unchecked.size()),
                                            py_array_iterator<decltype(data_unchecked), VT>(data_unchecked),
                                            py_array_iterator<decltype(data_unchecked), VT>(data_unchecked, data_unchecked.size()));
    fmm::write_body(*cursor.stream, formatter, cursor.options);
}

template <typename IT, typename VT>
void write_csc(write_cursor& cursor, const std::tuple<int64_t, int64_t>& shape,
                   py::array_t<IT>& indptr, py::array_t<IT>& indices, py::array_t<VT>& data, bool is_csr) {
    if (indptr.size() != std::get<1>(shape) + 1) {
        throw std::invalid_argument("indptr length does not match matrix shape.");
    }
    if (indices.size() != data.size() && data.size() != 0) {
        throw std::invalid_argument("len(indices) must equal len(data).");
    }

    cursor.header.nrows = std::get<0>(shape);
    cursor.header.ncols = std::get<1>(shape);
    cursor.header.nnz = indices.size();

    cursor.header.object = fmm::matrix;
    cursor.header.field = data.size() == 0 ? fmm::pattern : fmm::get_field_type((const VT*)nullptr);
    cursor.header.format = fmm::coordinate;
    cursor.header.symmetry = fmm::general;

    fmm::write_header(*cursor.stream, cursor.header);

    auto indptr_unchecked = indptr.unchecked();
    auto indices_unchecked = indices.unchecked();
    auto data_unchecked = data.unchecked();
    auto formatter = fmm::csc_formatter(py_array_iterator<decltype(indptr_unchecked), IT>(indptr_unchecked),
                                        py_array_iterator<decltype(indptr_unchecked), IT>(indptr_unchecked, indptr_unchecked.size() - 1),
                                        py_array_iterator<decltype(indices_unchecked), IT>(indices_unchecked),
                                        py_array_iterator<decltype(indices_unchecked), IT>(indices_unchecked, indices_unchecked.size()),
                                        py_array_iterator<decltype(data_unchecked), VT>(data_unchecked),
                                        py_array_iterator<decltype(data_unchecked), VT>(data_unchecked, data_unchecked.size()),
                                        is_csr);
    fmm::write_body(*cursor.stream, formatter, cursor.options);
}


PYBIND11_MODULE(_core, m) {
    m.doc() = R"pbdoc(
        fast_matrix_market
        -----------------------
    )pbdoc";

    // translate exceptions
    py::register_local_exception_translator([](std::exception_ptr p) {
        try {
            if (p) {
                std::rethrow_exception(p);
            }
        } catch (const fmm::fmm_error &e) {
            // Everything we throw maps best to ValueError
            PyErr_SetString(PyExc_ValueError, e.what());
        }
    });

    py::class_<fmm::matrix_market_header>(m, "header")
    .def(py::init<>())
    .def(py::init<int64_t, int64_t>())
    .def(py::init([](std::tuple<int64_t, int64_t> shape) { return fmm::matrix_market_header{std::get<0>(shape), std::get<1>(shape)}; }))
    .def(py::init(&create_header), py::arg("shape")=std::make_tuple(0, 0), "nnz"_a=0, "comment"_a=std::string(), "object"_a="matrix", "format"_a="coordinate", "field"_a="real", "symmetry"_a="general")
    .def_readwrite("nrows", &fmm::matrix_market_header::nrows)
    .def_readwrite("ncols", &fmm::matrix_market_header::ncols)
    .def_property("shape", &get_header_shape, &set_header_shape)
    .def_readwrite("nnz", &fmm::matrix_market_header::nnz)
    .def_readwrite("comment", &fmm::matrix_market_header::comment)
    .def_property("object", &get_header_object, &set_header_object)
    .def_property("format", &get_header_format, &set_header_format)
    .def_property("field", &get_header_field, &set_header_field)
    .def_property("symmetry", &get_header_symmetry, &set_header_symmetry)
    .def("to_dict", &header_to_dict, R"pbdoc(
        Return the values in the header as a dict.
    )pbdoc")
    .def("__repr__", [](const fmm::matrix_market_header& header) { return header_repr(header); });

    m.def("read_header_file", &read_header_file, R"pbdoc(
        Read Matrix Market header from a file.
    )pbdoc");
    m.def("read_header_string", &read_header_string, R"pbdoc(
        Read Matrix Market header from a string.
    )pbdoc");
    m.def("write_header_file", &write_header_file, R"pbdoc(
        Write Matrix Market header to a file.
    )pbdoc");
    m.def("write_header_string", &write_header_string, R"pbdoc(
        Write Matrix Market header to a string.
    )pbdoc");

    // Read methods
    py::class_<read_cursor>(m, "_read_cursor")
    .def_readonly("header", &read_cursor::header);

    m.def("open_read_file", &open_read_file, py::arg("path"), py::arg("num_threads")=0);
    m.def("open_read_string", &open_read_string, py::arg("str"), py::arg("num_threads")=0);

    m.def("read_body_array", &read_body_array<int64_t>);
    m.def("read_body_array", &read_body_array<double>);
    m.def("read_body_array", &read_body_array<std::complex<double>>);

    m.def("read_body_triplet", &read_body_triplet<int32_t, int64_t>);
    m.def("read_body_triplet", &read_body_triplet<int32_t, double>);
    m.def("read_body_triplet", &read_body_triplet<int32_t, std::complex<double>>);

    m.def("read_body_triplet", &read_body_triplet<int64_t, int64_t>);
    m.def("read_body_triplet", &read_body_triplet<int64_t, double>);
    m.def("read_body_triplet", &read_body_triplet<int64_t, std::complex<double>>);

    // Write methods
    py::class_<write_cursor>(m, "_write_cursor")
    .def_readwrite("header", &write_cursor::header)
    .def("get_string", &write_cursor::get_string);

    m.def("open_write_file", &open_write_file);
    m.def("open_write_string", &open_write_string);
    m.def("write_header_only", &write_header_only);

    // Write arrays
    m.def("write_array", &write_array<int64_t>);
    m.def("write_array", &write_array<double>);
    m.def("write_array", &write_array<long double>);
    m.def("write_array", &write_array<std::complex<double>>);
    m.def("write_array", &write_array<std::complex<long double>>);

    // Write triplets
    m.def("write_triplet", &write_triplet<int32_t, int64_t>);
    m.def("write_triplet", &write_triplet<int32_t, double>);
    m.def("write_triplet", &write_triplet<int32_t, long double>);
    m.def("write_triplet", &write_triplet<int32_t, std::complex<double>>);
    m.def("write_triplet", &write_triplet<int32_t, std::complex<long double>>);

    m.def("write_triplet", &write_triplet<int64_t, int64_t>);
    m.def("write_triplet", &write_triplet<int64_t, double>);
    m.def("write_triplet", &write_triplet<int64_t, long double>);
    m.def("write_triplet", &write_triplet<int64_t, std::complex<double>>);
    m.def("write_triplet", &write_triplet<int64_t, std::complex<long double>>);

    // Write CSC/CSR
    m.def("write_csc", &write_csc<int32_t, int64_t>);
    m.def("write_csc", &write_csc<int32_t, double>);
    m.def("write_csc", &write_csc<int32_t, long double>);
    m.def("write_csc", &write_csc<int32_t, std::complex<double>>);
    m.def("write_csc", &write_csc<int32_t, std::complex<long double>>);

    m.def("write_csc", &write_csc<int64_t, int64_t>);
    m.def("write_csc", &write_csc<int64_t, double>);
    m.def("write_csc", &write_csc<int64_t, long double>);
    m.def("write_csc", &write_csc<int64_t, std::complex<double>>);
    m.def("write_csc", &write_csc<int64_t, std::complex<long double>>);
#ifdef VERSION_INFO
#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}
