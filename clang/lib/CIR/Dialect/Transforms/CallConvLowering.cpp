//===- CallConvLowering.cpp - Rewrites functions according to call convs --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TargetLowering/LowerModule.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/MissingFeatures.h"

#define GEN_PASS_DEF_CALLCONVLOWERING
#include "clang/CIR/Dialect/Passes.h.inc"

#include "llvm/Support/TimeProfiler.h"

namespace mlir {
namespace cir {

//===----------------------------------------------------------------------===//
// Rewrite Patterns
//===----------------------------------------------------------------------===//

struct CallConvLoweringPattern : public OpRewritePattern<FuncOp> {
  using OpRewritePattern<FuncOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(FuncOp op,
                                PatternRewriter &rewriter) const final {
    llvm::TimeTraceScope scope("Call Conv Lowering Pass", op.getSymName().str());

    const auto module = op->getParentOfType<mlir::ModuleOp>();

    auto modOp = op->getParentOfType<ModuleOp>();
    std::unique_ptr<LowerModule> lowerModule =
        createLowerModule(modOp, rewriter);

    // Rewrite function calls before definitions. This should be done before
    // lowering the definition.
    auto calls = op.getSymbolUses(module);
    if (calls.has_value()) {
      for (auto call : calls.value()) {
        // FIXME(cir): Function pointers are ignored.
        if (isa<GetGlobalOp>(call.getUser())) {
          cir_cconv_assert_or_abort(!::cir::MissingFeatures::ABIFuncPtr(),
                                    "NYI");
          continue;
        }

        auto callOp = dyn_cast_or_null<CallOp>(call.getUser());
        if (!callOp)
          cir_cconv_unreachable("NYI empty callOp");
        if (lowerModule->rewriteFunctionCall(callOp, op).failed())
          return failure();
      }
    }

    // TODO(cir): Instead of re-emmiting every load and store, bitcast arguments
    // and return values to their ABI-specific counterparts when possible.
    if (lowerModule->rewriteFunctionDefinition(op).failed())
      return failure();

    return success();
  }
};

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct CallConvLoweringPass
    : ::impl::CallConvLoweringBase<CallConvLoweringPass> {
  using CallConvLoweringBase::CallConvLoweringBase;

  void runOnOperation() override;
  StringRef getArgument() const override { return "cir-call-conv-lowering"; };
};

void populateCallConvLoweringPassPatterns(RewritePatternSet &patterns) {
  patterns.add<CallConvLoweringPattern>(patterns.getContext());
}

void CallConvLoweringPass::runOnOperation() {

  // Collect rewrite patterns.
  RewritePatternSet patterns(&getContext());
  populateCallConvLoweringPassPatterns(patterns);

  // Collect operations to be considered by the pass.
  SmallVector<Operation *, 16> ops;
  getOperation()->walk([&](FuncOp op) { ops.push_back(op); });

  // Configure rewrite to ignore new ops created during the pass.
  GreedyRewriteConfig config;
  config.strictMode = GreedyRewriteStrictness::ExistingOps;

  // Apply patterns.
  if (failed(applyOpPatternsAndFold(ops, std::move(patterns), config)))
    signalPassFailure();
}

} // namespace cir

std::unique_ptr<Pass> createCallConvLoweringPass() {
  return std::make_unique<cir::CallConvLoweringPass>();
}

} // namespace mlir
