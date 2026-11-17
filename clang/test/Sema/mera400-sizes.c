// RUN: %clang_cc1 -triple mera400-unknown-none -fno-signed-char -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple mera400-unknown-none -target-abi large \
// RUN:   -target-feature +large-memory -fno-signed-char -fsyntax-only -verify=large %s
// expected-no-diagnostics
// large-no-diagnostics

_Static_assert(sizeof(char) == 1, "");
_Static_assert(sizeof(short) == 2, "");
_Static_assert(sizeof(int) == 2, "");
_Static_assert(sizeof(long) == 4, "");
_Static_assert(sizeof(long long) == 8, "");
_Static_assert(sizeof(float) == 8, "");
_Static_assert(sizeof(double) == 8, "");
_Static_assert(sizeof(long double) == 8, "");
_Static_assert(sizeof(__WCHAR_TYPE__) == 2, "");

// Plain char is unsigned (the machine's byte load zero-extends).
_Static_assert((char)-1 > 0, "");

// Every type is at most 2-byte aligned: the memory is addressed in words.
_Static_assert(_Alignof(long long) == 2, "");
_Static_assert(_Alignof(double) == 2, "");
_Static_assert(_Alignof(long) == 2, "");
_Static_assert(__BIGGEST_ALIGNMENT__ == 2, "");

#ifdef __MERA400_LARGE__
_Static_assert(sizeof(void *) == 4, "");
_Static_assert(sizeof(__SIZE_TYPE__) == 4, "");
#else
_Static_assert(sizeof(void *) == 2, "");
_Static_assert(sizeof(__SIZE_TYPE__) == 2, "");
#endif
