// RUN: %clang_cc1 -triple mera400-unknown-none -fno-signed-char -O0 -discard-value-names -emit-llvm %s -o - \
// RUN:   | FileCheck %s
// RUN: %clang_cc1 -triple mera400-unknown-none -fno-signed-char -O0 -discard-value-names -emit-llvm %s -o - \
// RUN:   | FileCheck -check-prefix=GLOBALS %s
// RUN: %clang_cc1 -triple mera400-unknown-none -target-abi large -discard-value-names \
// RUN:   -target-feature +large-memory -fno-signed-char -O0 -emit-llvm %s -o - \
// RUN:   | FileCheck -check-prefix=LARGE %s

// The data layout must equal the LLVM target's, or the back end would reject
// the module.
// CHECK: target datalayout = "E-m:e-p:16:16-p1:16:16-i8:8-i16:16-i32:16-i64:16-f32:16-f64:16-a:0:16-n16-S16"
// GLOBALS-DAG: @g = {{.*}}global i16 0
// GLOBALS-DAG: @.str = private unnamed_addr constant [3 x i8] c"hi\00"
// GLOBALS-DAG: @fp = {{.*}}global ptr null
// LARGE: target datalayout = "E-m:e-P1-G1-p:16:16-p1:32:16-i8:8-i16:16-i32:16-i64:16-f32:16-f64:16-a:0:16-n16-S16"

struct S { int a; char b; long c; };

// Aggregates are passed by a pointer to a copy (no byval) and returned
// through a byte pointer.
// CHECK: define dso_local void @mk(ptr addrspace(1) {{.*}}sret(%struct.S) align 2 %0, ptr {{.*}}dead_on_return{{.*}} %1)
struct S mk(struct S s) { s.a++; return s; }

// Small integers are extended by the callee's declaration; char is unsigned.
// CHECK: define dso_local i16 @ex(i8 noundef zeroext %0, i8 noundef zeroext %1, i16 noundef signext %2)
int ex(char c, unsigned char u, short s) { return c + u + s; }

// CHECK: define dso_local i64 @ll(i64 noundef %0)
long long ll(long long x) { return x; }

// CHECK: define dso_local double @dd(double noundef %0)
double dd(double x) { return x; }

// Globals and string literals are word-addressed (address space 0) objects;
// C sees them through a byte pointer, hence the casts.
int g;

// CHECK: define dso_local ptr addrspace(1) @gp()
// CHECK: ret ptr addrspace(1) addrspacecast (ptr @g to ptr addrspace(1))
int *gp(void) { return &g; }

// CHECK: define dso_local ptr addrspace(1) @str()
// CHECK: ret ptr addrspace(1) addrspacecast (ptr @.str to ptr addrspace(1))
const char *str(void) { return "hi"; }

// Stack slots are address space 0 allocas, cast when their address is taken.
// CHECK: define dso_local i16 @local()
// CHECK: %[[X:.*]] = alloca i16
// CHECK: addrspacecast ptr %[[X]] to ptr addrspace(1)
int local(void) { int x = 3; int *p = &x; return *p; }

// Function pointers are 16-bit word addresses in both models.
int (*fp)(int);

// LARGE: define dso_local i16 @rd(ptr addrspace(1) noundef %0)
int rd(int *p) { return *p; }
