//===- FlattenHelpers.h - Helpers for flattening CFG ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/Operation.h"

#ifndef DIALECT_CIR_TRANSFORMS_FLATTENHELPERS_H_
#define DIALECT_CIR_TRANSFORMS_FLATTENHELPERS_H_

namespace cir {

[[nodiscard]] mlir::LogicalResult flattenCFGForOperation(mlir::Operation *op);

} // namespace cir

#endif
