#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include <string>
#include <vector>

using namespace llvm;

namespace {

struct OpEntry {
    std::string file;
    unsigned line = 0;
    unsigned column = 0;
    std::string function;
    std::string kind;
};

const char* kind_name(unsigned opcode) {
    switch (opcode) {
        case Instruction::FAdd: return "fadd";
        case Instruction::FSub: return "fsub";
        case Instruction::FMul: return "fmul";
        case Instruction::FDiv: return "fdiv";
        case Instruction::FRem: return "frem";
        default: return "unknown";
    }
}

std::string json_escape_local(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(c); }
        else if (c == '\n') out += "\\n";
        else out.push_back(c);
    }
    return out;
}

struct SDCInstrumentPass : public PassInfoMixin<SDCInstrumentPass> {
    static bool isRequired() { return true; }

    PreservedAnalyses run(Module& M, ModuleAnalysisManager& AM) {
        LLVMContext& Ctx = M.getContext();
        Type* DoubleTy = Type::getDoubleTy(Ctx);
        Type* OpIdTy = Type::getInt32Ty(Ctx);

        FunctionCallee addHook = M.getOrInsertFunction("sdc_runtime_f64_add_hook", DoubleTy, OpIdTy, DoubleTy, DoubleTy);
        FunctionCallee subHook = M.getOrInsertFunction("sdc_runtime_f64_sub_hook", DoubleTy, OpIdTy, DoubleTy, DoubleTy);
        FunctionCallee mulHook = M.getOrInsertFunction("sdc_runtime_f64_mul_hook", DoubleTy, OpIdTy, DoubleTy, DoubleTy);
        FunctionCallee divHook = M.getOrInsertFunction("sdc_runtime_f64_div_hook", DoubleTy, OpIdTy, DoubleTy, DoubleTy);

        std::vector<OpEntry> ops;
        unsigned opId = 0;

        // Phase 1: collect candidates. Erasing an instruction during iteration
        // invalidates the iterator, so the rewrite happens in a second pass.
        struct Candidate {
            BinaryOperator* inst;
            FunctionCallee hook;
            OpEntry entry;
        };
        std::vector<Candidate> candidates;

        for (Function& F : M) {
            if (F.isDeclaration()) continue;
            for (BasicBlock& BB : F) {
                for (Instruction& I : BB) {
                    auto* BO = dyn_cast<BinaryOperator>(&I);
                    if (!BO) continue;
                    if (!BO->getType()->isDoubleTy()) continue;

                    // ADR-0002: an instruction carrying a fast-math flag changes
                    // the arithmetic, so it must not be instrumented silently.
                    //
                    // The flags split into two groups. The "unsafe" group below
                    // enables reassociation, contraction, or altered NaN/Inf
                    // handling, so the operation no longer computes what the
                    // source says. Any of them is a hard build failure.
                    //
                    // `nnan` is deliberately not in that group: clang emits it
                    // from its own value tracking in an ordinary -O2 build
                    // (for example on constants it has proven finite), and it
                    // does not change the arithmetic for finite operands.
                    // Rejecting it would make the pass unusable on normal code.
                    if (BO->hasAllowReassoc() || BO->hasNoInfs() || BO->hasNoSignedZeros() ||
                        BO->hasAllowContract() || BO->hasApproxFunc() || BO->hasAllowReciprocal()) {
                        report_fatal_error(
                            StringRef("sdc-instrument: refusing to instrument a fast-math "
                                      "instruction in function '" + F.getName().str() +
                                      "'. Rebuild with -ffp-contract=off -fno-fast-math."));
                    }

                    FunctionCallee hook;
                    switch (BO->getOpcode()) {
                        case Instruction::FAdd: hook = addHook; break;
                        case Instruction::FSub: hook = subHook; break;
                        case Instruction::FMul: hook = mulHook; break;
                        case Instruction::FDiv: hook = divHook; break;
                        default: continue;
                    }

                    OpEntry entry;
                    entry.function = F.getName().str();
                    entry.kind = kind_name(BO->getOpcode());
                    if (DebugLoc DL = BO->getDebugLoc()) {
                        entry.line = DL.getLine();
                        entry.column = DL.getCol();
                        if (auto* Scope = dyn_cast_or_null<DIScope>(DL.getScope())) {
                            entry.file = Scope->getFilename().str();
                        }
                    }

                    candidates.push_back({BO, hook, entry});
                }
            }
        }

        // Phase 2: rewrite. Op ids are assigned in module traversal order,
        // which is deterministic for identical sources, flags, and LLVM version
        // (ADR-0003).
        for (Candidate& c : candidates) {
            BinaryOperator* BO = c.inst;
            IRBuilder<> Builder(BO);
            Value* OpIdVal = ConstantInt::get(OpIdTy, ++opId);
            CallInst* Call = Builder.CreateCall(c.hook, {OpIdVal, BO->getOperand(0), BO->getOperand(1)});
            BO->replaceAllUsesWith(Call);
            BO->eraseFromParent();
            ops.push_back(c.entry);
        }

        std::error_code EC;
        raw_fd_ostream OS("ops.json", EC, sys::fs::OF_Text);
        if (EC) {
            errs() << "sdc-instrument: cannot write ops.json: " << EC.message() << "\n";
        } else {
            OS << "{\n";
            for (size_t i = 0; i < ops.size(); ++i) {
                const OpEntry& e = ops[i];
                OS << "  \"" << (i + 1) << "\": {"
                   << "\"file\": \"" << json_escape_local(e.file) << "\", "
                   << "\"line\": " << e.line << ", "
                   << "\"column\": " << e.column << ", "
                   << "\"function\": \"" << json_escape_local(e.function) << "\", "
                   << "\"kind\": \"" << e.kind << "\""
                   << "}";
                if (i + 1 < ops.size()) OS << ",";
                OS << "\n";
            }
            OS << "}\n";
        }

        errs() << "sdc-instrument: instrumented " << ops.size() << " operation(s)\n";
        return PreservedAnalyses::all();
    }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {
        LLVM_PLUGIN_API_VERSION, "SDCInstrument", LLVM_VERSION_STRING,
        [](PassBuilder& PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager& MPM, ArrayRef<PassBuilder::PipelineElement>) {
                    if (Name == "sdc-instrument") {
                        MPM.addPass(SDCInstrumentPass());
                        return true;
                    }
                    return false;
                });
        }
    };
}
