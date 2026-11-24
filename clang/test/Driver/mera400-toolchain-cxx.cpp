// RUN: %clangxx -### --target=mera400-unknown-none --sysroot=%S/Inputs/mera400_tree \
// RUN:   -c %s 2>&1 | FileCheck %s
// RUN: %clangxx -### --target=mera400-unknown-none --sysroot=%S/Inputs/mera400_tree \
// RUN:   -nostdinc++ -c %s 2>&1 | FileCheck -check-prefix=NOSTDINCXX %s
// RUN: %clang -### --target=mera400-unknown-none --sysroot=%S/Inputs/mera400_tree \
// RUN:   -x c -c %s 2>&1 | FileCheck -check-prefix=C %s

// The header-only libc++ lives in the sysroot and is searched before the C
// headers, because its wrapper headers continue the search with #include_next.
// CHECK: "-internal-isystem" "{{.*}}mera400_tree{{/|\\\\}}include{{/|\\\\}}c++{{/|\\\\}}v1"
// CHECK-SAME: "-internal-isystem" "{{.*}}mera400_tree{{/|\\\\}}include"

// NOSTDINCXX-NOT: c++{{/|\\\\}}v1
// NOSTDINCXX: "-internal-isystem" "{{.*}}mera400_tree{{/|\\\\}}include"

// C compilations do not get the C++ library headers.
// C-NOT: c++{{/|\\\\}}v1
