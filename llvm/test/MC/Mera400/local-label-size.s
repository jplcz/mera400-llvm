# RUN: llvm-mc -triple=mera400-unknown-none -filetype=obj %s -o - | llvm-readobj --symbols - | FileCheck %s

# Local labels may be used in generic directives such as .size.
f:
  aw r1, r2
  uj r4
.Lend:
  .size f, .Lend-f

# CHECK: Name: f
# CHECK: Size: 4
