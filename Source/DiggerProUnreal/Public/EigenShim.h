// --- Eigen compatibility shim for MSVC ---
// Place this BEFORE any #include <Eigen/...>
#if defined(_MSC_VER)
  #include <cfloat>  // defines LDBL_MANT_DIG on MSVC
  // Map GCC-style long-double mantissa macro to MSVC's
  #ifndef __LDBL_MANT_DIG__
	#define __LDBL_MANT_DIG__ LDBL_MANT_DIG
  #endif

  // Define byte-order macros Eigen sometimes expects
  #ifndef __ORDER_LITTLE_ENDIAN__
	#define __ORDER_LITTLE_ENDIAN__ 1234
  #endif
  #ifndef __ORDER_BIG_ENDIAN__
	#define __ORDER_BIG_ENDIAN__ 4321
  #endif
  #ifndef __BYTE_ORDER__
	#define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__
  #endif

  // Optionally disable Eigen vectorization alignment issues on MSVC if you see alignment errors
  // #define EIGEN_DONT_ALIGN_STATICALLY
#endif
// -----------------------------------------
