; RUN: opt -mtriple=mera400-unknown-none -passes=verify -S < %s | FileCheck %s

; Word pointers are in address space 0, 16-bit byte pointers in address space 1.
; CHECK: target datalayout = "E-m:e-p:16:16-p1:16:16-i8:8-i16:16-i32:16-i64:16-f32:16-f64:16-a:0:16-n16-S16"
define void @f() {
  ret void
}
