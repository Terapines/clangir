//===- MLIRCoreDialectsLoweringPrepare.cpp - CIR lowering preparation -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "FlattenHelpers.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/DialectConversion.h"
#include "clang/CIR/Dialect/Builder/CIRBaseBuilder.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"

using namespace llvm;
using namespace cir;

namespace cir {

struct MLIRLoweringPrepare
    : public mlir::PassWrapper<MLIRLoweringPrepare,
                               mlir::OperationPass<mlir::ModuleOp>> {
  // `scf.index_switch` requires that switch branches do not fall through.
  // We need to copy the next branch's body when the current `cir.case` does
  // not terminate with a break.
  void removeFallthrough(llvm::SmallVector<CaseOp> &cases);
  // When the switch is not canonical, i.e. it jumps into other regions,
  // we must flatten its content.
  void flatten(mlir::Operation *op);

  // The `isSimpleForm` function only cares about scopes.
  // Here we also need to make sure cases don't jump into other regions.
  bool isCanonicalForm(SwitchOp switchOp, llvm::SmallVector<CaseOp> &cases);

  // Finds the least common ancestor (LCA) of two basic blocks.
  // This means to find the lowest operation whose region contains both blocks.
  mlir::Operation *findLCA(mlir::Block *a, mlir::Block *b);

  void runOnOp(mlir::Operation *op);
  void runOnFunction(cir::FuncOp func);
  void runOnOperation() final;

  StringRef getDescription() const override {
    return "Rewrite CIR module to be more 'scf' dialect-friendly";
  }

  StringRef getArgument() const override { return "mlir-lowering-prepare"; }
private:
  llvm::StringMap<mlir::Block*> labels;
};

bool MLIRLoweringPrepare::isCanonicalForm(SwitchOp switchOp, llvm::SmallVector<CaseOp> &cases) {
  if (!switchOp.isSimpleForm(cases))
    return false;

  return std::all_of(cases.begin(), cases.end(), [&](CaseOp op) {
    return op->getParentOp() == switchOp;
  });
}

mlir::Operation *MLIRLoweringPrepare::findLCA(mlir::Block *a, mlir::Block *b) {
  // Collect all parent operations of `a`.
  llvm::SmallDenseSet<mlir::Operation *, 8> ancestors;
  for (auto *runner = a->getParent(); runner; runner = runner->getParentRegion()) {
    mlir::Operation *op = runner->getParentOp();
    if (!op)
      break;
    ancestors.insert(op);
  }

  // Enumerate `b`'s ancestors and look them up.
  for (auto *runner = b->getParent(); runner; runner = runner->getParentRegion()) {
    mlir::Operation *op = runner->getParentOp();
    if (!op)
      break;
    if (ancestors.contains(op))
      return op;
  }

  mlir::emitError(a->getParent()->getLoc(), "goto and label have no common ancestors!");
  return nullptr;
}

void MLIRLoweringPrepare::flatten(mlir::Operation *op) {
  // Move the switch inside a block.
  if (mlir::failed(flattenCFGForOperation(op)))
    signalPassFailure();
}

// `scf.index_switch` requires that switch branches do not fall through.
// We need to copy the next branch's body when the current `cir.case` does not
// terminate with a break.
void MLIRLoweringPrepare::removeFallthrough(llvm::SmallVector<CaseOp> &cases) {
  CIRBaseBuilderTy builder(getContext());
  // Note we enumerate in the reverse order, to facilitate the cloning.
  for (auto it = cases.rbegin(); it != cases.rend(); it++) {
    auto caseOp = *it;
    auto &region = caseOp.getRegion();
    auto &lastBlock = region.back();
    mlir::Operation &last = lastBlock.back();
    if (isa<BreakOp>(last))
      continue;

    // The last op must be a `cir.yield`. As it falls through, we copy the
    // previous case's body to this one.
    if (!isa<YieldOp>(last))
      continue;
    
    assert(isa<YieldOp>(last));

    // If there's no previous case, we can simply change the yield into a break.
    if (it == cases.rbegin()) {
      builder.setInsertionPointAfter(&last);
      builder.create<BreakOp>(last.getLoc());
      last.erase();
      continue;
    }

    auto prevIt = it;
    --prevIt;
    CaseOp &prev = *prevIt;
    auto &prevRegion = prev.getRegion();
    mlir::IRMapping mapping;
    builder.cloneRegionBefore(prevRegion, region, region.end());

    // We inline the block to the end.
    // This is required because `scf.index_switch` expects that each of its
    // region contains a single block.
    mlir::Block *cloned = lastBlock.getNextNode();
    for (auto it = cloned->begin(); it != cloned->end();) {
      auto next = it;
      next++;
      it->moveBefore(&last);
      it = next;
    }
    cloned->erase();
    last.erase();
  }
}

void MLIRLoweringPrepare::runOnOp(mlir::Operation *op) {
  if (auto switchOp = dyn_cast<SwitchOp>(op)) {
    llvm::SmallVector<CaseOp> cases;
    if (!isCanonicalForm(switchOp, cases)) {
      flatten(switchOp);
      return;
    }

    removeFallthrough(cases);
    return;
  }
  if (auto gotoOp = dyn_cast<GotoOp>(op)) {
    auto labelName = gotoOp.getLabel();
    mlir::Block *target = labels[labelName];
    mlir::Block *source = gotoOp->getBlock();
    mlir::Operation *lca = findLCA(target, source);
    // Only these operations can be flattened.
    flatten(lca);
    return;
  }
  op->emitError("unexpected op type");
}

void MLIRLoweringPrepare::runOnFunction(cir::FuncOp func) {
  labels.clear();

  llvm::SmallVector<mlir::Operation *> opsToTransform;
  func.getBody().walk([&](mlir::Operation *op) {
    if (isa<SwitchOp, GotoOp>(op))
      opsToTransform.push_back(op);
    if (auto label = dyn_cast<LabelOp>(op))
      labels.try_emplace(label.getLabel(), label->getBlock());
  });

  for (auto *op : opsToTransform)
    runOnOp(op);
}

void MLIRLoweringPrepare::runOnOperation() {
  auto module = getOperation();
  llvm::SmallVector<cir::FuncOp> functions;
  module->walk([&](cir::FuncOp op) {
    functions.push_back(op);
  });
  for (auto func : functions)
    runOnFunction(func);
}

std::unique_ptr<mlir::Pass> createMLIRCoreDialectsLoweringPreparePass() {
  return std::make_unique<MLIRLoweringPrepare>();
}

} // namespace cir
