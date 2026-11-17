// RUN: %clang_cc1 -E -dM -ffreestanding -fno-signed-char -triple=mera400-unknown-none \
// RUN:   -x c /dev/null | FileCheck -check-prefix=FLOAT %s
// RUN: %clang_cc1 -E -dM -ffreestanding -fno-signed-char -triple=mera400-unknown-none \
// RUN:   -x c /dev/null | FileCheck -match-full-lines -check-prefix=SMALL %s
// RUN: %clang_cc1 -E -dM -ffreestanding -fno-signed-char -triple=mera400-unknown-none \
// RUN:   -target-abi large -target-feature +large-memory \
// RUN:   -x c /dev/null | FileCheck -match-full-lines -check-prefix=LARGE %s
// RUN: %clang_cc1 -E -dM -triple=mera400-unknown-none -target-feature +awp \
// RUN:   -target-cpu mx16 -x c /dev/null \
// RUN:   | FileCheck -check-prefixes=FEATURES %s

// SMALL: #define __BIGGEST_ALIGNMENT__ 2
// SMALL: #define __BYTE_ORDER__ __ORDER_BIG_ENDIAN__
// SMALL: #define __CHAR_BIT__ 8
// SMALL: #define __CHAR_UNSIGNED__ 1
// SMALL: #define __INTPTR_TYPE__ int
// SMALL: #define __INT_MAX__ 32767
// SMALL: #define __LONG_LONG_MAX__ 9223372036854775807LL
// SMALL: #define __LONG_MAX__ 2147483647L
// SMALL: #define __MERA400__ 1
// SMALL-NOT: #define __MERA400_LARGE__
// SMALL: #define __POINTER_WIDTH__ 16
// SMALL: #define __PTRDIFF_TYPE__ int
// SMALL: #define __SIZEOF_DOUBLE__ 8
// SMALL: #define __SIZEOF_FLOAT__ 8
// SMALL: #define __SIZEOF_INT__ 2
// SMALL: #define __SIZEOF_LONG_DOUBLE__ 8
// SMALL: #define __SIZEOF_LONG_LONG__ 8
// SMALL: #define __SIZEOF_LONG__ 4
// SMALL: #define __SIZEOF_POINTER__ 2
// SMALL: #define __SIZEOF_SHORT__ 2
// SMALL: #define __SIZE_TYPE__ unsigned int
// SMALL: #define __mera400__ 1

// LARGE: #define __MERA400_LARGE__ 1
// LARGE: #define __POINTER_WIDTH__ 32
// LARGE: #define __PTRDIFF_TYPE__ long int
// LARGE: #define __SIZEOF_POINTER__ 4
// LARGE: #define __SIZE_TYPE__ long unsigned int

// FEATURES: #define __MERA400_AWP__ 1
// FEATURES: #define __MERA400_MX16__ 1

// The floating point macros describe the machine's native format (39-bit
// mantissa, 8-bit exponent, no infinity, NaN or denormals; the values of the
// C library's <float.h>), not the IEEE double the front end uses as a
// stand-in.
// FLOAT-DAG: #define __DBL_DECIMAL_DIG__ 13
// FLOAT-DAG: #define __DBL_DENORM_MIN__ 1.46936794e-39
// FLOAT-DAG: #define __DBL_DIG__ 11
// FLOAT-DAG: #define __DBL_EPSILON__ 3.63797881e-12
// FLOAT-DAG: #define __DBL_HAS_DENORM__ 0
// FLOAT-DAG: #define __DBL_HAS_INFINITY__ 0
// FLOAT-DAG: #define __DBL_HAS_QUIET_NAN__ 0
// FLOAT-DAG: #define __DBL_MANT_DIG__ 39
// FLOAT-DAG: #define __DBL_MAX_10_EXP__ 38
// FLOAT-DAG: #define __DBL_MAX_EXP__ 127
// FLOAT-DAG: #define __DBL_MAX__ 1.70141183e+38
// FLOAT-DAG: #define __DBL_MIN_10_EXP__ (-38)
// FLOAT-DAG: #define __DBL_MIN_EXP__ (-128)
// FLOAT-DAG: #define __DBL_MIN__ 1.46936794e-39
// FLOAT-DAG: #define __FLT_EPSILON__ 3.63797881e-12F
// FLOAT-DAG: #define __FLT_MANT_DIG__ 39
// FLOAT-DAG: #define __FLT_MAX__ 1.70141183e+38F
// FLOAT-DAG: #define __LDBL_EPSILON__ 3.63797881e-12L
// FLOAT-DAG: #define __LDBL_MANT_DIG__ 39
// FLOAT-DAG: #define __LDBL_MAX__ 1.70141183e+38L
// FLOAT-DAG: #define __DECIMAL_DIG__ 13
// FLOAT-NOT: 1.797693
// FLOAT-NOT: 2.2204460
