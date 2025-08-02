// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fno-clangir-direct-lowering -emit-mlir=core %s -o %t.mlir
// RUN: FileCheck --input-file=%t.mlir %s

void jump() {
  int i = 1;
label:
  i++;
  if (i < 10)
    goto label;

  // CHECK: func.func @jump() {
  // CHECK:   %[[ALLOCA:.+]] = memref.alloca()
  // CHECK:   %[[ONE:.+]] = arith.constant 1
  // CHECK:   memref.store %[[ONE]], %[[ALLOCA]][]
  // CHECK:   cf.br ^bb1
  // CHECK: ^bb1:
  // CHECK:   cir.label "label"
  // CHECK:   %[[V0:.+]] = memref.load %[[ALLOCA]][]
  // CHECK:   %[[ONE:.+]] = arith.constant 1
  // CHECK:   %[[V1:.+]] = arith.addi %[[V0]], %[[ONE]]
  // CHECK:   memref.store %[[V1]], %[[ALLOCA]][]
  // CHECK:   cf.br ^bb2
  // CHECK: ^bb2:
  // CHECK:   %[[V2:.+]] = memref.load %[[ALLOCA]][]
  // CHECK:   %[[TEN:.+]] = arith.constant 10
  // CHECK:   %[[COND:.+]] = arith.cmpi slt, %[[V2]], %[[TEN]]
  // CHECK:   cf.cond_br %[[COND]], ^bb3, ^bb4
  // CHECK: ^bb3:
  // CHECK:   cir.goto "label"
  // CHECK: ^bb4:
  // CHECK:   cf.br ^bb5
  // CHECK: ^bb5:
  // CHECK:   return
  // CHECK: }
}
