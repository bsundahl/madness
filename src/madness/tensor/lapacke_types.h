/*
  This file is part of MADNESS.

  Copyright (C) 2019 Virginia Tech

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

  For more information please contact:

  Robert J. Harrison
  Oak Ridge National Laboratory
  One Bethel Valley Road
  P.O. Box 2008, MS-6367

  email: harrisonrj@ornl.gov
  tel:   865-241-3937
  fax:   865-572-0680
*/


#ifndef MADNESS_LINALG_LAPACKE_TYPES_H__INCLUDED
#define MADNESS_LINALG_LAPACKE_TYPES_H__INCLUDED

/// \file lapacke_types.h
/// \brief Define types used by LAPACKe


#include <madness/madness_config.h>
// some BLAS libraries define their own types for complex data
#ifndef HAVE_INTEL_MKL
# include <complex>
# ifndef lapack_complex_float
#  define lapack_complex_float  std::complex<float>
# else
static_assert(sizeof(std::complex<float>)==sizeof(lapack_complex_float), "sizes of lapack_complex_float and std::complex<float> do not match");
# endif
# ifndef lapack_complex_double
#  define lapack_complex_double std::complex<double>
# else
static_assert(sizeof(std::complex<double>)==sizeof(lapack_complex_double), "sizes of lapack_complex_double and std::complex<double> do not match");
# endif
#else
// if calling direct need to cast to the MKL complex types
# ifdef MKL_DIRECT_CALL
#  include <mkl_types.h>
#  ifndef lapack_complex_float
#   define lapack_complex_float MKL_Complex8
#  endif
#  ifndef lapack_complex_double
#   define lapack_complex_double MKL_Complex16
#  endif
// else can call via F77 prototypes which don't need type conversion
# else
#  include <complex>
#  ifndef lapack_complex_float
#   define lapack_complex_float  std::complex<float>
#  endif
#  ifndef lapack_complex_double
#   define lapack_complex_double std::complex<double>
#  endif
# endif
#endif

#include <type_traits>

namespace madness {
namespace lapacke {

    /////////// legalized conversions between C++ and LAPACKe types //////////
    template <typename T>
    const lapack_complex_float*
    to_cptr(const T* ptr) {
      static_assert(sizeof(T)==sizeof(lapack_complex_float), "sizes of lapack_complex_float and T given to madness::cblas::to_cptr do not match");
      return reinterpret_cast<const lapack_complex_float*>(ptr);
    }
    template <typename T>
    typename std::enable_if<!std::is_const<T>::value, lapack_complex_float*>::type
    to_cptr(T* ptr) {
      static_assert(sizeof(T)==sizeof(lapack_complex_float), "sizes of lapack_complex_float and T given to madness::cblas::to_cptr do not match");
      return reinterpret_cast<lapack_complex_float*>(ptr);
    }

    template <typename T>
    const lapack_complex_double*
    to_zptr(const T* ptr) {
      static_assert(sizeof(T)==sizeof(lapack_complex_double), "sizes of lapack_complex_double and T given to madness::cblas::to_zptr do not match");
      return reinterpret_cast<const lapack_complex_double*>(ptr);
    }
    template <typename T>
    typename std::enable_if<!std::is_const<T>::value, lapack_complex_double*>::type
    to_zptr(T* ptr) {
      static_assert(sizeof(T)==sizeof(lapack_complex_double), "sizes of lapack_complex_double and T given to madness::cblas::to_zptr do not match");
      return reinterpret_cast<lapack_complex_double*>(ptr);
    }

} // namespace lapacke
} // namespace madness

#endif // MADNESS_LINALG_LAPACKE_TYPES_H__INCLUDED

