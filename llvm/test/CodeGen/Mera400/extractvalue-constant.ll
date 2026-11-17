; RUN: llc -mtriple=mera400-unknown-none -O0 < %s | FileCheck %s
; RUN: llc -mtriple=mera400-unknown-none -target-abi=large -O0 < %s | FileCheck %s

; Clang at -O0 leaves extractvalue of a constant aggregate whose element is a
; pointer-to-integer constant expression (a pointer to member function). That
; used to abort with "do not know how to expand the result of this operator".

declare i16 @fn() addrspace(0)

define i16 @a() {
; CHECK-LABEL: a:
; CHECK:       lw {{r[0-9]}}, fn
  %r = extractvalue { i16, i16 } { i16 ptrtoint (ptr @fn to i16), i16 0 }, 0
  ret i16 %r
}

define i32 @b() {
; CHECK-LABEL: b:
; CHECK:       lw {{r[0-9]}}, fn
  %r = extractvalue { i32, i32 } { i32 ptrtoint (ptr addrspace(1) addrspacecast (ptr @fn to ptr addrspace(1)) to i32), i32 0 }, 0
  ret i32 %r
}

; Splitting a store of the aggregate creates the extractvalue.
define void @c(ptr %p) {
; CHECK-LABEL: c:
; CHECK:       lw {{r[0-9]}}, fn
  store { i32, i32 } { i32 ptrtoint (ptr addrspace(1) addrspacecast (ptr @fn to ptr addrspace(1)) to i32), i32 0 }, ptr %p, align 2
  ret void
}
