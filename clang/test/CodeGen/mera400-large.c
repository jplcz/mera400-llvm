// RUN: %clang_cc1 -triple mera400-unknown-none -target-abi large -discard-value-names \
// RUN:   -target-feature +large-memory -fno-signed-char -O0 -emit-llvm %s -o - \
// RUN:   | FileCheck %s

// A large model C pointer is a 32-bit byte address (address space 1), while a
// function pointer stays a 16-bit word address.
// CHECK: target datalayout = "E-m:e-P1-G1-p:16:16-p1:32:16-
// CHECK: @sz = {{.*}}global i32 4
unsigned long sz = sizeof(char *);

// CHECK: define {{.*}}ptr addrspace(1) @next(ptr addrspace(1) noundef %0)
char *next(char *p) { return p + 1; }
