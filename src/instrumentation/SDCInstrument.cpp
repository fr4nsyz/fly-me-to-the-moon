#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>
#include <string>

using namespace llvm;

namespace {

struct SDCInstrumentPass : public PassInfoMixin<SDCInstrumentPass> {
    static bool isRequired() { return true; }
    
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
        LLVMContext &Ctx = M.getContext();
        FunctionCallee addHook = M.getOrInsertFunction("sdc_runtime_f64_add_hook", 
            Type::getDoubleTy(Ctx), 
            Type::getInt32Ty(Ctx),
            Type::getDoubleTy(Ctx), 
            Type::getDoubleTy(Ctx));
        FunctionCallee mulHook = M.getOrInsertFunction("sdc_runtime_f64_mul_hook", 
            Type::getDoubleTy(Ctx), 
            Type::getInt32Ty(Ctx),
            Type::getDoubleTy(Ctx), 
            Type::getDoubleTy(Ctx));
        
        unsigned opId = 0;
        
        for (Function &F : M) {
            if (F.isDeclaration()) continue;
            for (BasicBlock &BB : F) {
                for (Instruction &I : BB) {
                    if (auto *BO = dyn_cast<BinaryOperator>(&I)) {
                        if (BO->getType()->isDoubleTy()) {
                            if (BO->hasAllowReassoc() || BO->hasNoNaNs() || 
                                BO->hasNoInfs() || BO->hasNoSignedZeros() || 
                                BO->hasApproxFunc()) {
                                errs() << "Error: fast-math flags detected on FP instruction\n";
                                continue;
                            }
                            
                            if (BO->getOpcode() == Instruction::FAdd) {
                                IRBuilder<> Builder(BO);
                                Value *OpIdVal = ConstantInt::get(Type::getInt32Ty(Ctx), ++opId);
                                Value *LHS = BO->getOperand(0);
                                Value *RHS = BO->getOperand(1);
                                CallInst *Call = Builder.CreateCall(addHook, {OpIdVal, LHS, RHS});
                                BO->replaceAllUsesWith(Call);
                                BO->eraseFromParent();
                            } else if (BO->getOpcode() == Instruction::FMul) {
                                IRBuilder<> Builder(BO);
                                Value *OpIdVal = ConstantInt::get(Type::getInt32Ty(Ctx), ++opId);
                                Value *LHS = BO->getOperand(0);
                                Value *RHS = BO->getOperand(1);
                                CallInst *Call = Builder.CreateCall(mulHook, {OpIdVal, LHS, RHS});
                                BO->replaceAllUsesWith(Call);
                                BO->eraseFromParent();
                            }
                        }
                    }
                }
            }
        }
        
        std::error_code EC;
        raw_fd_ostream OS("ops.json", EC);
        if (!EC) {
            OS << "[]\n";
        }
        
        return PreservedAnalyses::all();
    }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION, "SDCInstrument", LLVM_VERSION_STRING,
        [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                    if (Name == "sdc-instrument") {
                        MPM.addPass(SDCInstrumentPass());
                        return true;
                    }
                    return false;
                });
        }
    };
}
