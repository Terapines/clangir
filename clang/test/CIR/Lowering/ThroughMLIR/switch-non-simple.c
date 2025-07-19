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
