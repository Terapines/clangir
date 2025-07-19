// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fno-clangir-direct-lowering -emit-mlir=core %s -o %t.mlir
// RUN: FileCheck --input-file=%t.mlir %s

void nonsimple() {
  int i = 0;
  switch (i) {
  case 2:
    if (i) {
      i++;
  case 3:
      i++;
      break;
  case 4:
      i++;
    }
  }
}

// Note that some dead blocks still retain `cir.br` rather than `cf.br`.
// This is expected.

// CHECK: memref.alloca_scope  {
// CHECK:   scf.execute_region {
// CHECK:     %[[I:.+]] = memref.load %alloca[]
// CHECK:     cf.br ^bb1
// CHECK:   ^bb1:
// CHECK:     cf.switch %[[I]]
// CHECK:       default: ^bb14,
// CHECK:       2: ^bb3,
// CHECK:       3: ^bb6,
// CHECK:       4: ^bb9
// CHECK:   ^bb2:
// CHECK:     cir.br ^bb3
// CHECK:   ^bb3:
// CHECK:     cf.br ^bb4
// CHECK:   ^bb4:
// CHECK:     %[[I:.+]] = memref.load %alloca[]
// CHECK:     %[[V0:.+]] = arith.constant 0
// CHECK:     %[[I2:.+]] = arith.cmpi ne, %[[I]], %[[V0]]
// CHECK:     cf.cond_br %[[I2]], ^bb5, ^bb11
// CHECK:   ^bb5:
// CHECK:     %[[I:.+]] = memref.load %alloca[]
// CHECK:     %[[V1:.+]] = arith.constant 1
// CHECK:     %[[I2:.+]] = arith.addi %[[I]], %[[V1]]
// CHECK:     memref.store %[[I2]], %alloca[]
// CHECK:     cf.br ^bb6
// CHECK:   ^bb6:
// CHECK:     %[[I:.+]] = memref.load %alloca[]
// CHECK:     %[[V1:.+]] = arith.constant 1
// CHECK:     %[[I2:.+]] = arith.addi %[[I]], %[[V1]]
// CHECK:     memref.store %[[I2]], %alloca[]
// CHECK:     cf.br ^bb7
// CHECK:   ^bb7:
// CHECK:     cf.br ^bb14
// CHECK:   ^bb8:
// CHECK:     cir.br ^bb9
// CHECK:   ^bb9:
// CHECK:     %[[I:.+]] = memref.load %alloca[]
// CHECK:     %[[V1:.+]] = arith.constant 1
// CHECK:     %[[I2:.+]] = arith.addi %[[I]], %[[V1]]
// CHECK:     memref.store %[[I2]], %alloca[]
// CHECK:     cf.br ^bb10
// CHECK:   ^bb10:
// CHECK:     cf.br ^bb11
// CHECK:   ^bb11:
// CHECK:     cf.br ^bb12
// CHECK:   ^bb12:
// CHECK:     cf.br ^bb13
// CHECK:   ^bb13:
// CHECK:     cf.br ^bb14
// CHECK:   ^bb14:
// CHECK:     scf.yield
// CHECK:   }
// CHECK: }