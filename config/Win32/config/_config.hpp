// Copyright (c) 2010-2022, Lawrence Livermore National Security, LLC. Produced
// at the Lawrence Livermore National Laboratory. All Rights reserved. See files
// LICENSE and NOTICE for details. LLNL-CODE-806117.
//
// This file is part of the MFEM library. For more information and source code
// availability visit https://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the BSD-3 license. We welcome feedback and contributions, see file
// CONTRIBUTING.md for details.

#ifndef MFEM_CONFIG_HEADER
#define MFEM_CONFIG_HEADER

// MFEM version: integer of the form: (major*100 + minor)*100 + patch.
#define MFEM_VERSION 40400

// MFEM version string of the form "3.3" or "3.3.1".
#define MFEM_VERSION_STRING "4.4.0"

// MFEM version type, see the MFEM_VERSION_TYPE_* constants below.
#define MFEM_VERSION_TYPE ((MFEM_VERSION)%2)

// MFEM version type constants.
#define MFEM_VERSION_TYPE_RELEASE 1
#define MFEM_VERSION_TYPE_DEVELOPMENT 0

// Separate MFEM version numbers for major, minor, and patch.
#define MFEM_VERSION_MAJOR ((MFEM_VERSION)/10000)
#define MFEM_VERSION_MINOR (((MFEM_VERSION)/100)%100)
#define MFEM_VERSION_PATCH ((MFEM_VERSION)%100)

// Description of the git commit used to build MFEM.
#define MFEM_GIT_STRING "a1f6902ed72552f3e680d1489f1aa6ade2e0d3b2"

// Build the parallel MFEM library.
// Requires an MPI compiler, and the libraries HYPRE and METIS.
/* #define MFEM_USE_MPI */

// Enable debug checks in MFEM.
/* #undef MFEM_DEBUG */

// Throw an exception on errors.
/* #undef MFEM_USE_EXCEPTIONS */

// Enable gzstream in MFEM.
#define MFEM_USE_GZSTREAM 

// Enable backtraces for mfem_error through libunwind.
/* #undef MFEM_USE_LIBUNWIND */

// Enable MFEM features that use the METIS library (parallel MFEM).
/* #undef MFEM_USE_METIS */

// Enable this option if linking with METIS version 5 (parallel MFEM).
/* #undef MFEM_USE_METIS_5 */

// Use LAPACK routines for various dense linear algebra operations.
#define MFEM_USE_LAPACK

// Use thread-safe implementation. This comes at the cost of extra memory
// allocation and de-allocation.
/* #undef MFEM_THREAD_SAFE */

// Enable experimental OpenMP support. Requires MFEM_THREAD_SAFE.
/* #undef MFEM_USE_OPENMP */

// Enable MFEM functionality based on the Mesquite library.
/* #undef MFEM_USE_MESQUITE */

// Enable MFEM functionality based on the SuiteSparse library.
/* #undef MFEM_USE_SUITESPARSE */

// Enable MFEM functionality based on the SuperLU_DIST library.
#ifdef MFEM_USE_MPI
#define MFEM_USE_SUPERLU
#endif

// Enable MFEM functionality based on the STRUMPACK library.
/* #undef MFEM_USE_STRUMPACK */

// Internal MFEM option: enable group/batch allocation for some small objects.
#define MFEM_USE_MEMALLOC

// Enable functionality based on the Gecko library
/* #undef MFEM_USE_GECKO */

// Enable MFEM functionality based on the GnuTLS library
/* #undef MFEM_USE_GNUTLS */

// Enable MFEM functionality based on the NetCDF library
/* #undef MFEM_USE_NETCDF */

// Enable MFEM functionality based on the PETSc library
/* #undef MFEM_USE_PETSC */

// Enable MFEM functionality based on the Sidre library
/* #undef MFEM_USE_SIDRE */

// Enable MFEM functionality based on Conduit
#define MFEM_USE_CONDUIT

// Enable MFEM functionality based on the PUMI library
/* #undef MFEM_USE_PUMI */

// Which library functions to use in class StopWatch for measuring time.
// For a list of the available options, see INSTALL.
// If not defined, an option is selected automatically.
#define MFEM_TIMER_TYPE 3

// Enable MFEM functionality based on the SUNDIALS libraries.
/* #undef MFEM_USE_SUNDIALS */

// Windows specific options
// Macro needed to get defines like M_PI from <cmath>. (Visual Studio C++ only?)
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif

// Version of HYPRE used for building MFEM.
#define MFEM_HYPRE_VERSION 22500

// Macro defined when PUMI is built with support for the Simmetrix SimModSuite
// library.
/* #undef MFEM_USE_SIMMETRIX */

#endif // MFEM_CONFIG_HEADER