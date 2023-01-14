# Copyright (C) 2022-2023 Adam Lugowski. All rights reserved.
# Use of this source code is governed by the BSD 2-clause license found in the LICENSE.txt file.
import warnings
from io import BytesIO, StringIO
from pathlib import Path
import unittest

import numpy as np
import scipy.io

import fast_matrix_market as fmm

matrices = Path("matrices")


class TestSciPy(unittest.TestCase):
    """
    Test compatibility with SciPy
    """

    def assertMatrixEqual(self, lhs, rhs, types=True):
        """
        Assert matrices are equal. Can be dense numpy arrays or sparse scipy matrices.
        """
        self.assertEqual(lhs.shape, rhs.shape)
        if isinstance(lhs, np.ndarray):
            self.assertEqual(type(lhs), type(rhs))
            if types:
                self.assertEqual(lhs.dtype, rhs.dtype)
            np.testing.assert_almost_equal(lhs, rhs)
            return

        # Sparse.
        # Avoid failing on different ordering by using a CSC with sorted indices
        lhs_csc = lhs.tocsc().sorted_indices()
        rhs_csc = rhs.tocsc().sorted_indices()

        if types:
            self.assertEqual(lhs_csc.indptr.dtype, rhs_csc.indptr.dtype)
            self.assertEqual(lhs_csc.indices.dtype, rhs_csc.indices.dtype)
            self.assertEqual(lhs_csc.data.dtype, rhs_csc.data.dtype)
        np.testing.assert_almost_equal(lhs_csc.indptr, rhs_csc.indptr)
        np.testing.assert_almost_equal(lhs_csc.indices, rhs_csc.indices)
        np.testing.assert_almost_equal(lhs_csc.data, rhs_csc.data)

    def test_read(self):
        for mtx in sorted(list(matrices.glob("*.mtx*"))):
            mtx_name = str(mtx.stem)
            with self.subTest(msg=mtx_name):
                if str(mtx).endswith(".gz"):
                    # TODO: fmm does not handle GZip files yet
                    # import gzip
                    # stream = gzip.open(filespec, mode)
                    continue
                    # self.skipTest("no gz")
                if str(mtx).endswith(".bz2"):
                    # TODO: fmm does not handle BZ2 files yet
                    # import bz2
                    # stream = bz2.BZ2File(filespec, 'rb')
                    continue
                    # self.skipTest("no bz2")

                m = scipy.io.mmread(mtx)
                header = fmm.read_header(mtx)
                m_fmm = fmm.read_scipy(mtx)
                self.assertEqual(m.shape, header.shape)
                self.assertEqual(m.shape, m_fmm.shape)

                self.assertMatrixEqual(m, m_fmm)

    def test_scipy_crashes(self):
        for mtx in sorted(list((matrices / "scipy_crashes").glob("*.mtx*"))):
            mtx_name = str(mtx.stem)
            with self.subTest(msg=mtx_name):
                # Verify SciPy has not been updated to handle this file
                # noinspection PyTypeChecker
                with self.assertRaises((ValueError, TypeError)), warnings.catch_warnings():
                    warnings.filterwarnings('ignore')
                    _ = scipy.io.mmread(mtx)

                # Verify fast_matrix_market can read the file
                m_fmm = fmm.read_scipy(mtx)
                self.assertGreater(m_fmm.shape[0], 0)
                self.assertGreater(m_fmm.shape[1], 0)

    def test_write(self):
        for mtx in sorted(list(matrices.glob("*.mtx"))):
            mtx_header = fmm.read_header(mtx)
            with self.subTest(msg=mtx.stem):
                m = scipy.io.mmread(mtx)
                m_fmm = fmm.read_scipy(mtx)
                fmms = fmm.write_scipy(None, m_fmm, field=("pattern" if mtx_header.field == "pattern" else None))

                if mtx_header.field == "pattern":
                    # Make sure pattern header is written
                    self.assertIn("pattern", fmms)

                m2 = scipy.io.mmread(StringIO(fmms))

                self.assertMatrixEqual(m, m2)

    def test_write_formats(self):
        for mtx in sorted(list(matrices.glob("*.mtx"))):
            mtx_header = fmm.read_header(mtx)
            if mtx_header.format != "coordinate":
                continue

            m = scipy.io.mmread(mtx)
            m_fmm = fmm.read_scipy(mtx)

            formats = {
                "coo": m_fmm.tocoo(),
                "csc": m_fmm.tocsc(),
                "csr": m_fmm.tocsr(),
                "bsr": m_fmm.tobsr(),
                "dok": m_fmm.todok(),
                "dia": m_fmm.todia(),
                "lil": m_fmm.tolil(),
            }
            for name, m_fmm in formats.items():
                with self.subTest(msg=f"{mtx.stem} - {name}"):
                    fmms = fmm.write_scipy(None, m_fmm, field=("pattern" if mtx_header.field == "pattern" else None))

                    if mtx_header.field == "pattern":
                        # Make sure pattern header is written
                        self.assertIn("pattern", fmms)

                    m2 = scipy.io.mmread(StringIO(fmms))

                    self.assertMatrixEqual(m, m2)

    def test_write_fields(self):
        for mtx in sorted(list(matrices.glob("*.mtx"))):
            mtx_header = fmm.read_header(mtx)
            mat = fmm.read_scipy(mtx)

            for field in ["integer", "real", "complex", "pattern"]:
                if mtx_header.format == "array" and field == "pattern":
                    continue

                with self.subTest(msg=f"{mtx.stem} to field={field}"), warnings.catch_warnings():
                    # Converting complex to real raises a warning
                    warnings.simplefilter('ignore', np.ComplexWarning)

                    # write scipy with this field
                    try:
                        sio = BytesIO()
                        scipy.io.mmwrite(sio, mat, field=field)
                        scipys = sio.getvalue().decode("latin1")
                    except OverflowError:
                        if mtx_header.field != "integer" and field == "integer":
                            continue

                    # Write FMM with this field
                    fmms = fmm.write_scipy(None, mat, field=field)

                    # verify the reads come up with the same types
                    m = scipy.io.mmread(StringIO(scipys))

                    m2 = fmm.read_scipy(fmms)

                    self.assertMatrixEqual(m, m2)

    def test_long_types(self):
        # verify that this platform's numpy is built with longdouble and longcomplex support
        long_val = "1E310"  # Value just slightly out of range of a 64-bit float
        with warnings.catch_warnings():
            warnings.filterwarnings('ignore')
            d = np.array([long_val], dtype="double")
            ld = np.array([long_val], dtype="longdouble")

        if d[0] == ld[0]:
            self.skipTest("Numpy does not have longdouble support on this platform.")

        for mtx in sorted(list((matrices / "long_double").glob("*.mtx*"))):
            # assert no exception
            # fast_matrix_market throws if value is out of range
            _ = fmm.read_scipy(mtx, long_type=True)

    def test_invalid(self):
        # use the invalid matrices from the C++ tests
        for mtx in sorted(list((matrices / ".." / ".." / ".." / "tests" / "matrices" / "invalid").glob("*.mtx"))):
            mtx_name = str(mtx.stem)
            with self.subTest(msg=mtx_name):
                with self.assertRaises(ValueError):
                    fmm.read_scipy(mtx)


if __name__ == '__main__':
    unittest.main()
