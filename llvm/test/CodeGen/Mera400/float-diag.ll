; RUN: not llc -mtriple=mera400-unknown-none %s -o /dev/null 2>&1 | FileCheck %s
; RUN: not llc -mtriple=mera400-unknown-none -mattr=+awp %s -o /dev/null 2>&1 | FileCheck %s

; The MERA-400 has one floating point format, mapped to `double`. The other
; floating point types, vectors of them, constrained (strict) intrinsics and
; constants that the format cannot hold are reported as errors that name the
; function (or the global variable); nothing is silently miscompiled.

declare double @llvm.experimental.constrained.fadd.f64(double, double, metadata, metadata)
declare double @llvm.fma.f64(double, double, double)
declare double @llvm.sin.f64(double)
declare double @llvm.fmuladd.f64(double, double, double)
declare double @llvm.minnum.f64(double, double)
declare i32 @llvm.lrint.i32.f64(double)

; CHECK: in function f32 {{.*}}the float type (use double: the MERA-400 has one floating point format) is not supported
define float @f32(float %a) {
  ret float %a
}

; CHECK: in function h {{.*}}this floating point type (only double is supported) is not supported
define half @h(half %a) {
  ret half %a
}

; CHECK: in function b {{.*}}this floating point type (only double is supported) is not supported
define bfloat @b(bfloat %a) {
  ret bfloat %a
}

; CHECK: in function x {{.*}}this floating point type (only double is supported) is not supported
define x86_fp80 @x(x86_fp80 %a) {
  ret x86_fp80 %a
}

; CHECK: in function q {{.*}}this floating point type (only double is supported) is not supported
define fp128 @q(fp128 %a) {
  ret fp128 %a
}

; CHECK: in function v {{.*}}vectors of floating point values is not supported
define <2 x double> @v(<2 x double> %a) {
  ret <2 x double> %a
}

; A float inside an array or a structure is found as well.
; CHECK: in function agg {{.*}}the float type (use double: the MERA-400 has one floating point format) is not supported
define void @agg(ptr %p) {
  %v = load [2 x float], ptr %p
  ret void
}

; CHECK: in function strict {{.*}}constrained floating point intrinsics are not supported
define double @strict(double %a, double %b) strictfp {
  %r = call double @llvm.experimental.constrained.fadd.f64(double %a, double %b, metadata !"round.dynamic", metadata !"fpexcept.strict") strictfp
  ret double %r
}

; Intrinsics without a library function counterpart are not supported.
; CHECK: in function lrint {{.*}}is not supported
define i32 @lrint(double %a) {
  %r = call i32 @llvm.lrint.i32.f64(double %a)
  ret i32 %r
}

; CHECK: in function big {{.*}}a floating point constant is not representable in the MERA-400 floating point format
define double @big() {
  ret double 1.000000e+300
}

; CHECK: in function inf {{.*}}a floating point constant is not representable
define double @inf() {
  ret double 0x7FF0000000000000
}

; CHECK: in function nan {{.*}}a floating point constant is not representable
define double @nan() {
  ret double 0x7FF8000000000000
}

; CHECK: in function wide {{.*}}integers wider than 64 bits are not supported
define i128 @wide(double %a) {
  %r = fptosi double %a to i128
  ret i128 %r
}

; The constant of an operation is converted during instruction selection, so
; this one is reported after the errors of the IR checks above.
; CHECK: in function bigarg {{.*}}the floating point constant -1.0000000000000001E+300 is not representable
define double @bigarg(double %a) {
  %r = fadd double %a, -1.000000e+300
  ret double %r
}

; Global variables are diagnosed after all the functions.
; CHECK: a floating point constant in a global variable is not representable
@gbig = global double 1.000000e+300

; CHECK: a floating point constant in a global variable is not representable
@ginf = global [2 x double] [double 1.000000e+00, double 0x7FF0000000000000]
