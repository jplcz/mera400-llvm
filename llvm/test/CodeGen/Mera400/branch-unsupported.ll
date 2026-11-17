; RUN: not llc -mtriple=mera400-unknown-none %s -o /dev/null 2>&1 | FileCheck %s
; RUN: not llc -mtriple=mera400-unknown-none -enable-new-pm=0 %s -o /dev/null 2>&1 | FileCheck %s

; There are no indirect jumps through a register, so the address of a basic
; block cannot be used.

; CHECK: error: {{.*}}indirect_branch{{.*}}indirect branches
define void @indirect_branch(ptr %p) {
  indirectbr ptr %p, [label %l]
l:
  ret void
}

; CHECK: error: {{.*}}block_address{{.*}}blockaddress
define ptr @block_address() {
entry:
  br label %l
l:
  ret ptr blockaddress(@block_address, %l)
}
