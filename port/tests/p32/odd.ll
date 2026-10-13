; hand-written IR with the constant forms clang may emit for 4-byte pointers: must survive p32rebase and compile
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "arm64-apple-ios15.0.0"
@g = global [64 x i32] zeroinitializer, align 16
declare i32 @port(ptr, ptr, i32)
define i32 @odd() {
  %a = call i32 @port(ptr addrspacecast (ptr addrspace(271) getelementptr inbounds nuw (i8, ptr addrspace(271) inttoptr (i32 4096 to ptr addrspace(271)), i32 8) to ptr), ptr addrspacecast (ptr addrspace(271) getelementptr inbounds nuw (i8, ptr addrspace(271) addrspacecast (ptr @g to ptr addrspace(271)), i32 96) to ptr), i32 1)
  %b = call i32 @port(ptr addrspacecast (ptr addrspace(271) inttoptr (i32 268435456 to ptr addrspace(271)) to ptr), ptr addrspacecast (ptr addrspace(271) addrspacecast (ptr @g to ptr addrspace(271)) to ptr), i32 2)
  %c = add i32 %a, %b
  ret i32 %c
}
