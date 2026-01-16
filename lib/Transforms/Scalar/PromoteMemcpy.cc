/**  The transformation we do here looks roughly like this:
        memcpy(Dst, Source, sizeof(BufferTy))
          goes to
      for each field_id in fields(BufferTy):
        *GEP(Dst, field_id) = *GEP(Src, field_id)
*/
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BuildLibCalls.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/SimplifyLibCalls.h"

#include "seahorn/Support/SeaDebug.h"
#include "seahorn/Support/SeaLog.hh"

#define PMCPY_LOG(...) LOG("promote-memcpy", __VA_ARGS__)
#define PMCPY_DBG_LOG(...) LOG("promote-memcpy.dbg", __VA_ARGS__)

using namespace llvm;

namespace {

// Helper to extract element type from a pointer value by tracing its origin
// Needed for LLVM 20 opaque pointers where type info isn't in pointer types
static Type *getPointerElementTypeFromValue(Value *Ptr) {
  // Try to get type from the value's origin
  if (AllocaInst *AI = dyn_cast<AllocaInst>(Ptr)) {
    return AI->getAllocatedType();
  }
  if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Ptr)) {
    return GEP->getSourceElementType();
  }
  // For bitcast, recurse on operand (though bitcasts are rare with opaque pointers)
  if (BitCastInst *BC = dyn_cast<BitCastInst>(Ptr)) {
    return getPointerElementTypeFromValue(BC->getOperand(0));
  }
  // If we can't determine, return nullptr
  return nullptr;
}

class PromoteMemcpy : public FunctionPass {
public:
  static char ID;

  PromoteMemcpy() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<llvm::DominatorTreeWrapperPass>();
    AU.addRequired<AssumptionCacheTracker>();
  }

  StringRef getPassName() const override { return "PromoteMemcpy"; }

private:
  Module *m_M = nullptr;
  LLVMContext *m_Ctx = nullptr;
  const DataLayout *m_DL = nullptr;
  DominatorTree *m_DT = nullptr;
  AssumptionCache *m_AC = nullptr;

  bool simplifyMemCpy(MemCpyInst *MCpy);
};

char PromoteMemcpy::ID = 0;

bool PromoteMemcpy::simplifyMemCpy(MemCpyInst *MI) {
  assert(MI);
#if 0

  auto DstAlign = getKnownAlignment(MI->getDest(), *m_DL, MI, m_AC, m_DT);
  auto SrcAlign = getKnownAlignment(MI->getSource(), *m_DL, MI, m_AC, m_DT);

  // -- alignment on memcpy should be trusted, alignment on arguments is not important
  // -- at most should check that alignment of src and dst is the same
  if (MI->getSourceAlignment() != SrcAlign) {
    PMCPY_LOG(WARN << "unhandled SOURCE alignment. Skipping memcpy: " << *MI;);
    return false;
  }
  else if (MI->getDestAlignment() != DstAlign) {
    PMCPY_LOG(WARN << "unhandled DEST alignment. Skipping memcpy: " << *MI;);
    return false;
  }
#endif

  // skip non-constant length memcpy()
  ConstantInt *MemOpLength = dyn_cast<ConstantInt>(MI->getLength());
  if (!MemOpLength) {
    return false;
  }

  // Source and destination pointer types are always "i8*" for intrinsic.  See
  // if the size is something we can handle with a single primitive load/store.
  // A single load+store correctly handles overlapping memory in the memmove
  // case.
  uint64_t Size = MemOpLength->getLimitedValue();
  if (Size == 0) {
    PMCPY_LOG(WARN << "unexpected 0 length memcpy: " << *MI;);
    return false;
  }
  auto *SrcPtr = MI->getSource();
  auto *DstPtr = MI->getDest();

  unsigned SrcAddrSp = cast<PointerType>(SrcPtr->getType())->getAddressSpace();
  unsigned DstAddrSp = cast<PointerType>(DstPtr->getType())->getAddressSpace();

  if (SrcAddrSp != DstAddrSp) {
    llvm_unreachable("unexpected");
    return false;
  }

  auto *SrcPtrTy = cast<PointerType>(SrcPtr->getType());
  auto *DstPtrTy = cast<PointerType>(DstPtr->getType());

  if (SrcPtrTy != DstPtrTy) {
    PMCPY_LOG(WARN << "memcpy between different types: " << *MI;);
    return false;
  }

  // With LLVM 20 opaque pointers, trace pointer origin to find element type
  Type *SrcElemTy = getPointerElementTypeFromValue(SrcPtr);
  if (!SrcElemTy || !SrcElemTy->isFirstClassType()) {
    PMCPY_LOG(WARN << "Not a first class type! " << *MI;);
    return false;
  }

  PMCPY_DBG_LOG(errs() << "\nSrc:\t"; SrcPtr->print(errs());
                SrcPtrTy->print(errs() << "\t"); errs() << "\nDst:\t";
                DstPtr->print(errs()); DstPtrTy->print(errs() << "\t");
                errs() << "\n"; errs().flush());

  auto *BufferTy = dyn_cast<StructType>(SrcElemTy);
  // require src to be a struct
  if (!BufferTy) {
    PMCPY_LOG(WARN << "memcpy on non-struct types: " << *MI;);
    return false;
  }
  // require constant length argument equal to struct size
  if (m_DL->getTypeStoreSize(BufferTy) != Size) {
    PMCPY_LOG(WARN << "memcpy length not equal to struct size: " << *MI;);
    return false;
  }

  IRBuilder<> Builder(MI);
  auto *I64Ty = IntegerType::getInt64Ty(*m_Ctx);
  auto *NullInt = Constant::getNullValue(I64Ty);
  auto *I32Ty = IntegerType::getInt32Ty(*m_Ctx);

  // Perform field-wise copy. Note that this doesn't recurse and only explores
  // the immediately visible fields.
  //
  // The transformation we do here looks roughly like this:
  //   memcpy(Dst, Source, sizeof(BufferTy))
  //    ||
  //    V
  // for each field_id in fields(BufferTy):
  //   *GEP(Dst, field_id) = *GEP(Src, field_id)
  //

  // Track types explicitly for LLVM 20 opaque pointers
  using Transfer = std::tuple<Value *, Value *, Type *>;
  SmallVector<Transfer, 4> ToLower = {std::make_tuple(SrcPtr, DstPtr, BufferTy)};
  while (!ToLower.empty()) {
    Value *TrSrc, *TrDst;
    Type *TrType;
    std::tie(TrSrc, TrDst, TrType) = ToLower.pop_back_val();
    assert(TrSrc->getType()->isPointerTy());
    assert(TrDst->getType()->isPointerTy());

    if (!TrType->isStructTy()) {
      // Perform load/store with explicit type for opaque pointers
      auto *NewLoad = Builder.CreateLoad(TrType, TrSrc, SrcPtr->getName() + ".pmcpy");
      auto *NewStore = Builder.CreateStore(NewLoad, TrDst);

      PMCPY_DBG_LOG(errs() << "New load-store:\n\t"; NewLoad->print(errs());
                    errs() << "\n\t"; NewStore->print(errs()); errs() << "\n");
      continue;
    }

    // For struct types, create GEPs for each field with explicit type
    auto *StructTy = cast<StructType>(TrType);
    SmallVector<Transfer, 8> TmpBuff;
    for (unsigned i = 0, e = StructTy->getNumElements(); i != e; ++i) {
      auto *Idx = Constant::getIntegerValue(I32Ty, APInt(32, i));
      auto *SrcGEP = Builder.CreateInBoundsGEP(StructTy, TrSrc, {NullInt, Idx},
                                               "src.gep.pmcpy");
      auto *DstGEP = Builder.CreateInBoundsGEP(StructTy, TrDst, {NullInt, Idx},
                                               "dst.gep.pmcpy");
      Type *FieldTy = StructTy->getElementType(i);
      TmpBuff.push_back({SrcGEP, DstGEP, FieldTy});
    }

    for (auto &P : llvm::reverse(TmpBuff))
      ToLower.push_back(P);

    PMCPY_DBG_LOG(errs() << "\tSecond level\n");
  }
  return true;
}

bool PromoteMemcpy::runOnFunction(Function &F) {
  if (F.empty())
    return false;

  m_DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  m_M = F.getParent();
  m_Ctx = &m_M->getContext();
  m_DL = &m_M->getDataLayout();
  m_AC = &getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);

  bool Changed = false;
  SmallVector<MemCpyInst *, 8> ToDeleteQueue;
  PMCPY_DBG_LOG(
      errs() << "\n############## Start Promote Memcpy ###################\n");

  for (auto &BB : F)
    for (auto &I : BB)
      if (auto *MCpy = dyn_cast<MemCpyInst>(&I)) {
        PMCPY_DBG_LOG(MCpy->print(errs() << "Visiting: \n"); errs() << "\n");

        if (!simplifyMemCpy(MCpy))
          continue;

        ToDeleteQueue.push_back(MCpy);
        Changed = true;
      }

  PMCPY_DBG_LOG(errs() << "Removing dead memcpys in " << F.getName() << ":");
  for (auto *MCpy : ToDeleteQueue) {
    PMCPY_DBG_LOG(MCpy->print(errs() << "\n\t"));

    // Using getArgOperand API to avoid looking through casts.
    auto *SrcPtr = dyn_cast<BitCastInst>(MCpy->getArgOperand(1));
    auto *DstPtr = dyn_cast<BitCastInst>(MCpy->getArgOperand(0));

    MCpy->eraseFromParent();
    if (SrcPtr && SrcPtr->hasNUses(0)) {
      PMCPY_DBG_LOG(SrcPtr->print(errs() << "\n\t\tdeleting:\t"));
      SrcPtr->eraseFromParent();
    }
    if (DstPtr && DstPtr->hasNUses(0)) {
      PMCPY_DBG_LOG(DstPtr->print(errs() << "\n\t\tdeleting:\t"));
      DstPtr->eraseFromParent();
    }
  }

  PMCPY_DBG_LOG(
      errs() << "\n############## End Promote Memcpy ###################\n";
      errs().flush());
  return Changed;
}

} // namespace

namespace seahorn {
llvm::FunctionPass *createPromoteMemcpyPass() { return new PromoteMemcpy(); }
} // namespace seahorn

static llvm::RegisterPass<PromoteMemcpy>
    X("promote-memcpy", "Promote memcpy to field-wise stores");
