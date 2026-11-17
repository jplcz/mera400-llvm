// RUN: not %clang --target=mera400-unknown-none -S %s -o /dev/null 2>&1 | FileCheck %s

// The machine's floating point format has no infinity or NaN and a smaller
// exponent range than IEEE double precision; Clang models the type as IEEE
// double, so the back end rejects constants that cannot be represented.
// CHECK: error: Mera400: a floating point constant is not representable
double too_big(void) { return 1e300; }
// CHECK: error: Mera400: a floating point constant is not representable
double inf(void) { return __builtin_inf(); }
