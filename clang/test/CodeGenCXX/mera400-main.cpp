// RUN: %clang_cc1 -triple mera400-unknown-none -emit-llvm -o - %s | FileCheck %s --check-prefix=HOSTED
// RUN: %clang_cc1 -triple mera400-unknown-none -ffreestanding -emit-llvm -o - %s | FileCheck %s --check-prefix=FREESTANDING
// RUN: %clang -target mera400-unknown-none -### -c %s 2>&1 | FileCheck %s --check-prefix=DRIVER

// The target is hosted, so as for every Clang target C++ "main" is not
// mangled by default. With -ffreestanding (generic Clang behaviour, the
// language no longer gives main a special meaning) it is an ordinary function
// and is mangled; declare it extern "C" in that case. The driver does not add
// -ffreestanding by itself.
// HOSTED: define {{.*}}@main()
// FREESTANDING: define {{.*}}@_Z4mainv()
// DRIVER-NOT: -ffreestanding
int main() { return 0; }
