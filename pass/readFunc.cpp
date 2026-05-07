#include "llvm/IR/PassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IRReader/IRReader.h"

#include <fstream>
#include <string>
#include <algorithm>
#include <fstream>
#include <vector>
#include <filesystem>


using namespace llvm;

namespace {

static std::string getSourceFileForFunction(const Function &F) {
  if (F.getSubprogram() && F.getSubprogram()->getFilename() != "") {
    return F.getSubprogram()->getFilename().str();
  }
  return "unknown";
}

static void printFunctionPrefix(const Function &F,
                                const std::string &targetFile,
                                unsigned maxLines = 20) {
  const DISubprogram *SP = F.getSubprogram();
  if (!SP || !SP->isDefinition())
    return;

  const DIFile *File = SP->getFile();
  if (!File)
    return;

  std::string filename = File->getFilename().str();
  if (filename != targetFile)
    return;

  std::string path = File->getDirectory().str();
  if (!path.empty())
    path += "/";
  path += filename;

  unsigned startLine = SP->getLine();
  unsigned endLine = startLine + 20;

  std::ifstream src(path);
  if (!src.is_open()) {
    errs() << "Failed to open source file: " << path << "\n";
    return;
  }

  unsigned cur = 1;
  unsigned printed = 0;
  std::string line;

  while (std::getline(src, line)) {
    if (cur >= startLine && cur <= endLine) {
      outs() << line << "\n";
      printed++;
      if (printed >= maxLines)
        break;
    }
    if (cur > endLine)
      break;
    cur++;
  }
}


struct FuncInfoPass : public PassInfoMixin<FuncInfoPass> {

  FuncInfoPass() {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    std::ofstream out("functions.txt", std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
      errs() << "[FuncInfoPass] Failed to open functions.txt for writing\n";
      return PreservedAnalyses::all();
    }

    for (Function &F : M) {
      if (F.isDeclaration())
        continue;

      std::string filename = getSourceFileForFunction(F);
      std::string funcname = F.getName().str();
      unsigned numParams = F.arg_size();

      std::filesystem::path p(filename);
      auto ext = p.extension().string();
      if (ext != ".c" && ext != ".cpp" && ext != ".cc" && ext != ".cxx")
        continue;

      out << filename << "," << funcname << "," << numParams << "\n";
    }

    out.close();
    return PreservedAnalyses::all();
  }
};

} // namespace

PassPluginLibraryInfo getPassPluginInfo() {
  const auto callback = [](PassBuilder &PB) {
    PB.registerOptimizerLastEPCallback(
        [](ModulePassManager &MPM, OptimizationLevel level) {
          MPM.addPass(FuncInfoPass());
        });
  };

  return {LLVM_PLUGIN_API_VERSION, "name", "0.0.1", callback};
};

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return getPassPluginInfo();
}

int main(int argc, char **argv) {
  if (argc < 2) {
    errs() << "Usage:\n"
           << "  " << argv[0] << " <input.bc>\n"
           << "  " << argv[0] << " <input.bc> <filename> <function>\n";
    return 1;
  }

  LLVMContext Ctx;
  SMDiagnostic Err;

  std::unique_ptr<Module> M = parseIRFile(argv[1], Err, Ctx);
  if (!M) {
    Err.print(argv[0], errs());
    return 1;
  }

  // case 1: Build functions.txt
  if (argc == 2) {
    std::ofstream out("functions.txt", std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
      errs() << "Failed to open functions.txt\n";
      return 1;
    }

    for (Function &F : *M) {
      if (F.isDeclaration())
        continue;

      std::string filename = getSourceFileForFunction(F);

      std::filesystem::path p(filename);
      auto ext = p.extension().string();
      if (ext != ".c" && ext != ".cpp" && ext != ".cc" && ext != ".cxx")
        continue;

      std::string funcname = F.getName().str();
      unsigned numParams = F.arg_size();

      int recommended = 0;
      for (BasicBlock &BB : F) {
        for (Instruction &I : BB) {
          if (CallBase *callInst = dyn_cast<CallBase>(&I)) {
            if (Function *calledFunc = callInst->getCalledFunction()) {
              std::string calledFuncName = calledFunc->getName().str();
              if (calledFuncName == "read" ||
                  calledFuncName == "fread" ||
                  calledFuncName == "recv" ||
                  calledFuncName == "recvfrom") {
                recommended = 1;
                continue;
              }
            }
          }
        }
      }

      if (recommended) {
        out << filename << "," << funcname << "," << numParams << ",recommended\n";
      } else {
        out << filename << "," << funcname << "," << numParams << "\n";
      }
    }

    return 0;
  }

  // case 2: Print function.
  if (argc >= 4) {
    std::string targetFile = argv[2];
    std::string targetFunc = argv[3];

    for (Function &F : *M) {
      if (F.isDeclaration())
        continue;

      if (F.getName() == targetFunc) {
        printFunctionPrefix(F, targetFile);
        return 0;
      }
    }

    errs() << "Function not found: " << targetFunc << "\n";
    return 1;
  }

  return 0;
}