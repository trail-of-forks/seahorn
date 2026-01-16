#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

#include "boost/range.hpp"
#include "seahorn/Support/SeaDebug.h"

using namespace llvm;

namespace {
class PromoteMalloc : public FunctionPass {
public:
  static char ID;

  PromoteMalloc() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override {
    if (F.empty())
      return false;

    // -- only promote mallocs in top level functions
    if (F.getName() != "main")
      return false;

    bool changed = false;

    SmallVector<Instruction *, 16> kill;

    for (auto &I : llvm::make_range(inst_begin(F), inst_end(F))) {
      Value *v = I.stripPointerCasts();
      if (!isa<CallInst>(v))
        continue;

      auto &CI = cast<CallInst>(*v);

      const Function *fn = CI.getCalledFunction();
      if (!fn && CI.getCalledOperand())
        fn = dyn_cast<const Function>(CI.getCalledOperand()->stripPointerCasts());

      if (fn &&
          (fn->getName() == "malloc" || fn->getName() == "_Znwj" /* new */ ||
           fn->getName() == "_Znaj" /* new[] */)) {

        unsigned addrSpace = 0;
        Value *nv = nullptr;
        if (auto *ci = dyn_cast<Constant>(CI.getOperand(0))) {
          // malloc(0) == nullptr
          if (fn->getName() == "malloc" && ci->isZeroValue()) {
            nv = Constant::getNullValue(CI.getType());
          }
        }

        if (!nv) {
          // With LLVM 20 opaque pointers, malloc returns ptr (opaque)
          // Allocate i8 array matching malloc's byte-level allocation semantics
          auto ai = new AllocaInst(Type::getInt8Ty(F.getContext()),
                                   addrSpace, CI.getOperand(0), "malloc", &I);
          // -- set alignment based on stack, not alignment of the type
          // Handle MaybeAlign → Align conversion
          if (MaybeAlign StackAlign = F.getParent()->getDataLayout().getStackAlignment()) {
            ai->setAlignment(*StackAlign);
          }
          nv = ai;
        }
        v->replaceAllUsesWith(nv);

        changed = true;
      } else if (fn && (fn->getName() == "free" ||
                        fn->getName() == "_ZdlPv" /* delete */ ||
                        fn->getName() == "_ZdaPv" /* delete[] */))
        kill.push_back(&I);
    }

    // -- remove all calls to free(). This is too much, but ensures
    // -- that all promoted mallocs() are not free'ed by mistake
    for (auto *I : kill)
      I->eraseFromParent();

    return changed;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  virtual StringRef getPassName() const override { return "PromoteMalloc"; }
};

char PromoteMalloc::ID = 0;
} // namespace

namespace seahorn {
Pass *createPromoteMallocPass() { return new PromoteMalloc(); }
} // namespace seahorn

static llvm::RegisterPass<PromoteMalloc>
    X("promote-malloc", "Promote top-level malloc calls to alloca");
