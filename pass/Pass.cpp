#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/NoFolder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#define RYML_SINGLE_HDR_DEFINE_NOW
#include "ryml_all.hpp"

#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <queue>
#include <sstream>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <filesystem>

using namespace llvm;

namespace {

#define MAX_PARAM_INDEX 16

/*******************************************
 * Color Printing
 *******************************************/
#define PRINT_RED "\033[31m\033[1m"
#define PRINT_GREEN "\033[32m"
#define PRINT_YELLOW "\033[33m"
#define PRINT_BLUE "\033[34m\033[1m"
#define PRINT_MAGENTA "\033[35m\033[1m"
#define PRINT_CYAN "\033[36m"
#define PRINT_RESET "\033[0m"

/*******************************************
 * Helper Functions
 *******************************************/
static Instruction *referencePoint;
static bool use_is_referenced(Use &U) {
  if (auto *I = dyn_cast<Instruction>(U.getUser())) {
    if (I == referencePoint) {
      return false;
    }

    /* Change only forward instructions */
    if (referencePoint->getParent() == I->getParent()) {
      if (!referencePoint->comesBefore(I)) {
        return false;
      }
    } else {
      DominatorTree DT(*I->getFunction());
      if (!DT.dominates(referencePoint, I)) {
        if (auto *PHI = dyn_cast<PHINode>(I)) {
          if (PHI->getIncomingBlock(U) == referencePoint->getParent()) {
            return true;
          }
        }
        return false;
      }
    }

    bool isReferenced = false;

    if (auto *LI = dyn_cast<LoadInst>(I)) {
      isReferenced = true;
    } else if (auto *SI = dyn_cast<StoreInst>(I)) {
      isReferenced = true;
    } else if (auto *GI = dyn_cast<GetElementPtrInst>(I)) {
      isReferenced = true;
    } else if (auto *CI = dyn_cast<CmpInst>(I)) {
      isReferenced = true;
    } else if (auto *BI = dyn_cast<BranchInst>(I)) {
      isReferenced = true;
    } else if (auto *PI = dyn_cast<PHINode>(I)) {
      isReferenced = true;
    } else if (auto *CI = dyn_cast<CallInst>(I)) {
      isReferenced = true;
    } else if (I->isBinaryOp()) {
      isReferenced = true;
    } else if (I->isCast()) {
      isReferenced = true;
    } else if (I->isTerminator()) {
      isReferenced = true;
    }

#ifdef SANITIZE_DEBUG
    if (isReferenced) {
      std::cerr << PRINT_YELLOW;
    } else {
      std::cerr << PRINT_RED;
    }
    errs() << "M ";
    I->print(errs());
    errs() << "\n";
    std::cerr << PRINT_RESET;
#endif

    return isReferenced;
  }
  return false;
}

static std::vector<std::string> stdFunctions = {
    "strlen",     "strcpy",      "strncpy",     "strcat",
    "strncat",    "strcmp",      "strncmp",     "strchr",
    "strrchr",    "strstr",      "strtok",      "strtok_r",
    "strpbrk",    "strspn",      "malloc",      "calloc",
    "realloc",    "free",        "memcpy",      "memmove",
    "memcmp",     "memset",      "memchr",      "printf",
    "fprintf",    "sprintf",     "snprintf",    "vprintf",
    "vfprintf",   "vsprintf",    "vsnprintf",   "scanf",
    "fscanf",     "sscanf",      "vscanf",      "vfscanf",
    "vsscanf",    "fopen",       "freopen",     "fclose",
    "fread",      "fwrite",      "fseek",       "ftell",
    "rewind",     "fgetpos",     "fsetpos",     "feof",
    "ferror",     "clearerr",    "remove",      "rename",
    "tmpfile",    "tmpnam",      "setbuf",      "setvbuf",
    "exit",       "atexit",      "stdio_write", "stdio_read",
    "stdio_seek", "stdio_close", "stdio_flush", "stdio_error",
    "stdio_eof",  "kill",        "raise",       "abort",
    "system",     "getenv",      "putenv",      "getpid",
    "getppid",    "getuid",      "fileno",      "puts",
    "putc",       "putchar",     "fputc",       "fputs",
    "fgetc",      "fgets",       "getc",        "getchar",
    "ungetc",     "strerror",    "perror",      "strerror_r",
    "signal",     "makecontext", "swapcontext", "getcontext",
    "setcontext", "sigaction",   "sigprocmask", "__errno_location",
    "strdup",     "strndup",     "pthread_mutex_lock",
    "pthread_mutex_unlock",      "sleep",       "usleep",
    "pthread_cond_broadcast",    "pthread_cond_signal",
    "pthread_cond_wait",         "pthread_cond_timedwait",
    "pthread_exit",              "pthread_join", "pthread_create",
    "pthread_cancel",            "pthread_detach", "pthread_self",
    "__ctype_b_loc",             "__ctype_tolower_loc",
    "_set",       "_copy"
};

static bool isStdFunction(Function *F) {
  return std::find(stdFunctions.begin(), stdFunctions.end(), F->getName()) !=
         stdFunctions.end();
}

std::vector<int> *unionVectors(std::vector<int> *v1, std::vector<int> *v2) {
  std::vector<int> *result = new std::vector<int>();

  for (int index : *v1) {
    result->push_back(index);
  }

  for (int index : *v2) {
    if (std::find(result->begin(), result->end(), index) == result->end()) {
      result->push_back(index);
    }
  }

  return result;
}

std::vector<int> *intersectVectors(std::vector<int> *v1, std::vector<int> *v2) {
  std::vector<int> *result = new std::vector<int>();

  for (int index : *v1) {
    if (std::find(v2->begin(), v2->end(), index) != v2->end()) {
      result->push_back(index);
    }
  }

  return result;
}

bool isSubsetVector(std::vector<int> *v1, std::vector<int> *v2) {
  for (int index : *v1) {
    if (std::find(v2->begin(), v2->end(), index) == v2->end()) {
      return false;
    }
  }

  return true;
}

std::string getFileBaseName(Function *F) {
  std::string fileName;
  // fileName = F->getParent()->getSourceFileName();
  if (F->getSubprogram() == nullptr)
    return "unknown";
  fileName = F->getSubprogram()->getFilename().str();
  fileName = fileName.substr(fileName.find_last_of("/\\") + 1);
  fileName = fileName.substr(0, fileName.find_last_of('.'));
  return fileName;
}

// helper functions for sample_parse_file()

C4_SUPPRESS_WARNING_MSVC_WITH_PUSH(4996) // fopen: this function may be unsafe
/** load a file from disk into an existing CharContainer */
template<class CharContainer>
size_t file_get_contents(const char *filename, CharContainer *v)
{
    std::FILE *fp = std::fopen(filename, "rb");
    RYML_CHECK_MSG(fp != nullptr, "could not open file");
    std::fseek(fp, 0, SEEK_END);
    long sz = std::ftell(fp);
    v->resize(static_cast<typename CharContainer::size_type>(sz));
    if(sz)
    {
        std::rewind(fp);
        size_t ret = std::fread(&(*v)[0], 1, v->size(), fp);
        RYML_CHECK(ret == (size_t)sz);
    }
    std::fclose(fp);
    return v->size();
}

/** load a file from disk and return a newly created CharContainer */
template<class CharContainer>
CharContainer file_get_contents(const char *filename)
{
    CharContainer cc;
    file_get_contents(filename, &cc);
    return cc;
}

/*******************************************
 * Taint Configuration
 *******************************************/
/**
 * @brief The name of the taint configuration file.
 *
 * This global variable stores the name of the file that contains the taint
 * configuration settings.
 */
static std::string TaintFunctionFile;

/**
 * @brief Total list of taint functions.
 *
 * This global variable stores the total list of taint functions.
 */
static std::vector<std::string> GlobalTaintFunctionList;

/**
 * @brief Total scope
 *
 */
static std::vector<std::string> GlobalScopeList;

/**
 * @brief Total file scope
 * 
 */
static std::vector<std::string> GlobalFileScopeList;

/**
 * @brief Total target scope
 *
 */
static std::vector<std::tuple<std::string, int, int>> GlobalTargetList;

/**
 * @brief A queue of functions to be analyzed for taint propagation.
 *
 * This global variable holds a list of functions along with their respective
 * taint indices. Each element in the vector is a tuple containing:
 *  - A pointer to a Function object.
 *  - A pointer to a vector of integers representing the taint indices.
 */
static std::vector<std::tuple<Function *, std::vector<int> *>> taintFunctions;

/**
 * @brief A map of analyzed functions and their associated taint information.
 *
 * This global variable maintains a list of functions that have been analyzed
 * for taint propagation. Each entry in the map consists of:
 *  - A pointer to a Function object as the key.
 *  - A tuple as the value, containing:
 *    - A pointer to a Value object (which is a CallInst if the function is
 * called, or nullptr if the function is the entry function).
 *    - A pointer to a vector of integers representing the taint indices.
 *
 * Notes:
 *  - The Value (CallInst) is nullptr when the function is the entry function.
 *  - The taint indices may be overwritten if the function is reanalyzed.
 *  - This map holds the current taint analysis information for each function.
 */
static std::map<Function *, std::tuple<Value *, std::vector<int> *>>
    taintFunctionsList;

/**
 * @brief Connection information for the indirect call.
 * 
 */
static std::map<std::string, std::vector<std::string>> indirectCallMap;

/**
 * @brief Linkage information for the indriect calls.
 * 
 */
static std::map<std::string, std::string> indirectCallLinkage;

/**
 * @brief Number of arguments for the indirect call.
 * 
 */
static std::map<std::string, int> indirectCallArgNum;

/**
 * @brief Input index for the indirect call.
 */
static std::map<std::string, int> indirectCallInputIndex;

/**
 * @brief Length index for the indirect call.
 */
static std::map<std::string, int> indirectCallLengthIndex;

/**
 * @brief Multi-entry function list
 * 
 */
static std::map<std::string, int> multiEntryFunctionList;

/**
 * @brief Name of the entry function.
 *
 * This global variable stores the name of the entry function.
 */
static std::string entryFunctionName;

/**
 * @brief List of functions excluded from taint analysis.
 *
 * This global variable maintains a list of function names that should be
 * excluded from taint analysis.
 */
static std::vector<std::string> taintBanList;

/**
 * @brief List of functions of which the return value is tainted.
 *
 * If the function is in this list, it is assumed that the analysis is already
 * done and the return value is either tainted or not.
 */
static std::map<std::string, bool> returnIsTaintedList;

/**
 * @brief List of functions of which the parameters are tainted.
 * 
 */
static std::map<std::string, std::vector<int>> paramIsTaintedList;

/**
 * @brief True if it is cross fuzz.
 *
 */
static bool isCrossFuzz = true;

/**
 * @brief Holds indices related to fuzz data for the entry function.
 *
 * This global variable is a vector that stores specific indices used during
 * fuzz testing of the entry function.
 */
static std::vector<int> entryFunctionFuzzIndexes;

static std::vector<std::string> hookFunctionList = {};
static std::vector<std::string> assertFunctionList = {};
static std::vector<std::string> ignoreFunctionList = {};

static bool isInTaintBanList(std::string funcName) {
  return std::find(taintBanList.begin(), taintBanList.end(), funcName) !=
         taintBanList.end();
}

static bool isInTaintFunctionsList(Function *F) {
  return taintFunctionsList.find(F) != taintFunctionsList.end();
}

static bool isInGlobalScopeList(std::string scopeName) {
  return std::find(GlobalScopeList.begin(), GlobalScopeList.end(), scopeName) !=
         GlobalScopeList.end();
}

static bool isInGlobalFileScopeList(std::string scopeName) {
  // for (std::string fileScope : GlobalFileScopeList) {
  //   if (fileScope.find(scopeName) != std::string::npos) {
  //     return true;
  //   }
  // }
  return true;
}

static bool isInTargetList(std::string funcName) {
  for (auto &[name, start, end] : GlobalTargetList) {
    if (funcName == name) {
      return true;
    }
  }
  return false;
}

static int getTargetInputIndex(std::string funcName) {
  for (auto &[name, start, end] : GlobalTargetList) {
    if (funcName == name) {
      return start;
    }
  }
  return -1;
}

static int getTargetLengthIndex(std::string funcName) {
  for (auto &[name, start, end] : GlobalTargetList) {
    if (funcName == name) {
      return end;
    }
  }
  return -1;
}

static bool isInMultiEntryFunctionList(std::string funcName) {
  return multiEntryFunctionList.find(funcName) != multiEntryFunctionList.end();
}

static int getMultiEntryFunctionDepth(std::string funcName) {
  return multiEntryFunctionList[funcName];
}

static bool isFunctionAnalyzed(std::string funcName) {
  return returnIsTaintedList.find(funcName) != returnIsTaintedList.end();
}

static void addReturnIsTainted(std::string funcName, bool isTainted) {
  returnIsTaintedList[funcName] = isTainted;
}

static void updateReturnIsTainted(std::string funcName, bool isTainted) {
  if (isFunctionAnalyzed(funcName)) {
    returnIsTaintedList[funcName] = isTainted;
  }
}

static bool isReturnTainted(std::string funcName) {
  return returnIsTaintedList[funcName];
}

static void addParamIsTainted(std::string funcName,
                              std::vector<int> taintIndices) {
  paramIsTaintedList[funcName] = taintIndices;
}

static std::vector<int> *getParamIsTainted(std::string funcName) {
  if (paramIsTaintedList.find(funcName) != paramIsTaintedList.end()) {
    return &paramIsTaintedList[funcName];
  }
  return nullptr;
}

static bool isInAssertFunctionsList(std::string funcName) {
  return std::find(assertFunctionList.begin(), assertFunctionList.end(),
                   funcName) != assertFunctionList.end();
}

static bool isInIgnoreFunctionsList(std::string funcName) {
  return std::find(ignoreFunctionList.begin(), ignoreFunctionList.end(),
                   funcName) != ignoreFunctionList.end();
}

static bool hasIndirectLink(std::string funcName) {
  if (funcName.find("replicate_") != std::string::npos) {
    for (auto &[key, value] : indirectCallMap) {
      if (strstr(funcName.c_str(), key.c_str())) {
        return true;
      }
    }
  }
  return indirectCallMap.find(funcName) != indirectCallMap.end();
}

static std::vector<std::string> getIndirectLinks(std::string funcName) {
  if (funcName.find("replicate_") != std::string::npos) {
    for (auto &[key, value] : indirectCallMap) {
      if (strstr(funcName.c_str(), key.c_str())) {
        return value;
      }
    }
  }
  return indirectCallMap[funcName];
}

static bool hasIndirectLinkage(std::string funcName) {
  if (funcName.find("replicate_") != std::string::npos) {
    for (auto &[key, value] : indirectCallLinkage) {
      if (strstr(funcName.c_str(), key.c_str())) {
        return true;
      }
    }
  }
  return indirectCallLinkage.find(funcName) != indirectCallLinkage.end();
}

static std::string getIndirectLinkage(std::string funcName) {
  if (funcName.find("replicate_") != std::string::npos) {
    for (auto &[key, value] : indirectCallLinkage) {
      if (strstr(funcName.c_str(), key.c_str())) {
        return value;
      }
    }
  }
  return indirectCallLinkage[funcName];
}

static int getIndirectArgNum(std::string funcName) {
  if (funcName.find("replicate_") != std::string::npos) {
    for (auto &[key, value] : indirectCallArgNum) {
      if (strstr(funcName.c_str(), key.c_str())) {
        return value;
      }
    }
  }
  return indirectCallArgNum[funcName];
}

static int getIndirectInputIndex(std::string funcName) {
  if (funcName.find("replicate_") != std::string::npos) {
    for (auto &[key, value] : indirectCallInputIndex) {
      if (strstr(funcName.c_str(), key.c_str())) {
        return value;
      }
    }
  }
  return indirectCallInputIndex[funcName];
}

static int getIndirectLengthIndex(std::string funcName) {
  if (funcName.find("replicate_") != std::string::npos) {
    for (auto &[key, value] : indirectCallLengthIndex) {
      if (strstr(funcName.c_str(), key.c_str())) {
        return value;
      }
    }
  }
  return indirectCallLengthIndex[funcName];
}

static void addTaintFunctions(Function *F, std::vector<int> *taintIndices,
                              Value *callInst) {
  taintFunctions.push_back(std::make_tuple(F, taintIndices));
  taintFunctionsList[F] = std::make_tuple(callInst, taintIndices);
}

static std::vector<int> *getTaintIndices(Function *F) {
  if (!isInTaintFunctionsList(F)) {
    return nullptr;
  }
  return std::get<1>(taintFunctionsList[F]);
}

/*******************************************
 * Configuration Reader
 *******************************************/
static std::string createdFuncFile = "/tmp/created_functions.txt";

static bool readConfiguration(Module &M) {
  std::string contents = "";
  if (taintFunctions.empty()) {
    /* Parsing configuration yaml file */
    if (!std::getenv("TAINT_CONFIGURATION_FILE")) {
      for (const auto &entry :
           std::filesystem::directory_iterator("config/")) {
        if (entry.path().extension() == ".yaml") {
          contents = file_get_contents<std::string>(entry.path().c_str());
          break;
        }
      }

      if (contents == "") {
        errs() << PRINT_RED;
        errs()
            << "[-] TAINT_CONFIGURATION_FILE environment variable is not set or no .yaml file found in config/ directory\n";
        errs() << PRINT_RESET;
        return false;
      }
    } else {
      contents = file_get_contents<std::string>(
          std::getenv("TAINT_CONFIGURATION_FILE"));
    }

    if (std::getenv("LOW_FUZZ")) {
      isCrossFuzz = false;
    }

    ryml::Tree config = ryml::parse_in_arena(ryml::to_csubstr(contents));

#ifdef ASSERT_HOOK
    if (config["assert"].has_key()) {
      for (auto assertFunc : config["assert"]) {
        std::string funcName;
        assertFunc >> funcName;
        assertFunctionList.push_back(funcName);
      }
    }
#endif

#ifdef IGNORE_HOOK
    if (config["skip"].has_key()) {
      for (auto ignoreFunc : config["skip"]) {
        std::string funcName;
        ignoreFunc >> funcName;
        ignoreFunctionList.push_back(funcName);
      }
    }
#endif

    if (config["ban"].has_key()) {
      for (auto banFunc : config["ban"]) {
        std::string funcName;
        banFunc >> funcName;
        taintBanList.push_back(funcName);
      }
    }

    /* Record global scope */
    if (config["scope"].has_key()) {
      for (auto scopeEntry : config["scope"]) {
        std::string funcNameWithIndex;
        std::string funcName, token;
        int funcInputIndex, funcLengthIndex;

        scopeEntry >> funcNameWithIndex;
        std::stringstream ss(funcNameWithIndex);
        std::getline(ss, funcName, ',');
        std::getline(ss, token, ',');
        funcInputIndex = std::stoi(token);
        std::getline(ss, token, ',');
        funcLengthIndex = std::stoi(token);

        GlobalScopeList.push_back(funcName);

        /* Check the function is in target_list */
        if (funcInputIndex != -1) {
          GlobalTargetList.push_back(
            std::make_tuple(funcName, funcInputIndex, funcLengthIndex));
        }
      }
    }

    /* Read global file scope */
    if (config["file_scope"].has_key()) {
      for (auto scopeEntry : config["file_scope"]) {
        std::string fileName;
        scopeEntry >> fileName;
        GlobalFileScopeList.push_back(fileName);
      }
    }

    /* Parse entry function */
    if (config["entry"].has_key()) {
      config["entry"]["name"] >> entryFunctionName;
      Function *entryFunction = M.getFunction(entryFunctionName);
      std::string fileName;
      if (entryFunction && entryFunction->getSubprogram()) {
        fileName = entryFunction->getSubprogram()->getFilename().str();
      } else {
        fileName = "Unknown";
      }
      // std::string fileName = M.getSourceFileName();

      if (entryFunction && !isInTaintFunctionsList(entryFunction) &&
          !entryFunction->isDeclaration() && isInGlobalFileScopeList(fileName)) {
        std::vector<int> *taintIndices = new std::vector<int>();
        std::vector<int> controllableIndices;

        if (config["entry"]["user_controllable_index"].has_key()) {
          for (auto index : config["entry"]["user_controllable_index"]) {
            int val;
            index >> val;
            controllableIndices.push_back(val);
          }
        }

        /* Check for invalid indices */
        for (int idx : controllableIndices) {
            if (idx >= (ssize_t)entryFunction->arg_size()) {
                errs() << PRINT_RED;
                errs() << "[-] Invalid user_controllable_index " << idx << " for " << entryFunctionName << "\n";
                errs() << "Max index of " << entryFunctionName << " is "
                       << entryFunction->arg_size() << "\n";
                errs() << "Arguments might be optimized out\n";
                errs() << PRINT_RESET;
                
                entryFunctionName = "";
                 // return false; -- Keeping original behavior of not returning false immediately, though it sets entryFunctionName to empty
            }
        }

        if (entryFunctionName != "") {
             for (int i = 0; i < entryFunction->arg_size(); i++) {
                bool isControllable = false;
                for (int idx : controllableIndices) {
                    if (i == idx) {
                        isControllable = true;
                        break;
                    }
                }
                if (!isControllable) {
                    taintIndices->push_back(i);
                }
            }
            addTaintFunctions(entryFunction, taintIndices, nullptr);

            for (int idx : controllableIndices) {
                entryFunctionFuzzIndexes.push_back(idx);
            }
        }
      }
    } else {
      errs() << PRINT_RED;
      errs() << "[-] Entry function not found\n";
      errs() << PRINT_RESET;

      return false;
    }

    /* Parse return value of taint functions */
    if (config["taint"].has_key()) {
      for (auto taintEntry : config["taint"]) {
        std::string funcName;
        bool isTainted;

        taintEntry["name"] >> funcName;
        taintEntry["is_tainted"] >> isTainted;

        addReturnIsTainted(funcName, isTainted);

        /* The function taints the parameters */
        if (taintEntry["taint_params"].has_key()) {
          std::vector<int> taintIndices;
          for (auto index : taintEntry["taint_params"]) {
            int index_val;
            index >> index_val;
            taintIndices.push_back(index_val);
          }
          addParamIsTainted(funcName, taintIndices);
        }
      }
    }

    /* Read indirect links */
    if (config["indirect_links"].has_key()) {
      for (auto indirectLink : config["indirect_links"]) {
        std::string link;
        indirectLink >> link;

        std::vector<std::string> linkParts;
        std::stringstream ss(link);
        std::string part;
        while (std::getline(ss, part, ',')) {
          linkParts.push_back(part);
        }

        /*
           0: parent function name
           1: parent file name
           2: child function name
           3: child file name
           4: linkage
           5: argnum
           6: input index
           7: length index
        */
        if (!hasIndirectLink(linkParts[0])) {
          indirectCallMap[linkParts[0]] = {linkParts[2]};
        } else {
          indirectCallMap[linkParts[0]].push_back(linkParts[2]);
        }

        if (linkParts[4] == "internal") {
          // Save base name of the child file
          std::string fileName = linkParts[3];
          fileName = fileName.substr(fileName.find_last_of("/\\") + 1);
          fileName = fileName.substr(0, fileName.find_last_of('.'));
          indirectCallLinkage[linkParts[2]] = fileName;
        }

        indirectCallArgNum[linkParts[2]] = std::stoi(linkParts[5]);
        indirectCallInputIndex[linkParts[2]] = std::stoi(linkParts[6]);
        indirectCallLengthIndex[linkParts[2]] = std::stoi(linkParts[7]);
      }
    }

    /* Read multi-entry */
    if (config["multientry"].has_key()) {
      for (auto multiEntry : config["multientry"]) {
        std::string me;
        multiEntry >> me;

        std::vector<std::string> meParts;
        std::stringstream ss(me);
        std::string part;
        while (std::getline(ss, part, ',')) {
          meParts.push_back(part);
        }

        /*
        * 0: function name
        * 1: function depth
        */
        multiEntryFunctionList[meParts[0]] = std::stoi(meParts[1]);
      }
    }

        /* Read hook functions */
    if (config["hook"].has_key()) {
      for (auto hookEntry : config["hook"]) {
        std::string hookFuncName;
        hookEntry >> hookFuncName;

        hookFunctionList.push_back(hookFuncName);
      }
    }
  }

  return true;
}

static bool insertTaintConfiguration(std::string funcName, bool isTainted) {
  /* Parsing configuration yaml file */
  if (!std::getenv("TAINT_CONFIGURATION_FILE")) {
    errs() << PRINT_RED;
    errs() << "[-] TAINT_CONFIGURATION_FILE environment variable is not set\n";
    errs() << PRINT_RESET;
    return false;
  }

  std::string contents = file_get_contents<std::string>(
      std::getenv("TAINT_CONFIGURATION_FILE"));
  ryml::Tree config = ryml::parse_in_arena(ryml::to_csubstr(contents));

  /* Check exist */
  for (auto taintEntry : config["taint"]) {
    std::string taint_func_name;
    taintEntry["name"] >> taint_func_name;
    if (taint_func_name == funcName) {
      return false;
    }
  }

  int cur_child = config["taint"].num_children();
  config["taint"][cur_child] |= ryml::MAP;

  config["taint"][cur_child].append_child() << ryml::key("name") << funcName;
  config["taint"][cur_child].append_child() << ryml::key("is_tainted")
                                            << isTainted;
  config["taint"][cur_child].append_child() << ryml::key("taint_params") |=
      ryml::SEQ;

#ifdef TAINT_DEBUG
  errs() << PRINT_RED;
  errs() << "++ Inserted new taint configuration for " << funcName << "\n";
  errs() << PRINT_RESET;
#endif

  std::ofstream out(std::getenv("TAINT_CONFIGURATION_FILE"));
  out << config;

  std::FILE *fp = std::fopen(std::getenv("TAINT_CONFIGURATION_FILE"), "w");
  ryml::emit_yaml(config, fp);
  std::fclose(fp);

  return true;
}

static bool updateTaintReturnConfiguration(Function &F,
                                           std::string funcName,
                                           bool isTainted) {
  /* Parsing configuration yaml file */
  if (!std::getenv("TAINT_CONFIGURATION_FILE")) {
    errs() << PRINT_RED;
    errs() << "[-] TAINT_CONFIGURATION_FILE environment variable is not set\n";
    errs() << PRINT_RESET;
    return false;
  }

  std::string contents = file_get_contents<std::string>(
      std::getenv("TAINT_CONFIGURATION_FILE"));
  ryml::Tree config = ryml::parse_in_arena(ryml::to_csubstr(contents));

  for (auto taintEntry : config["taint"]) {
    std::string taint_func_name;
    taintEntry["name"] >> taint_func_name;
    if (taint_func_name == funcName) {
      taintEntry["is_tainted"] << isTainted;
      break;
    }
  }

  /* Create or append configuration update file */

  std::FILE *fp = std::fopen(std::getenv("TAINT_CONFIGURATION_FILE"), "w");
  ryml::emit_yaml(config, fp);
  std::fclose(fp);

  return true;
}

static bool updateTaintParamsConfiguration(Function &F,
                                           std::string funcName,
                                           std::vector<int> *taintIndices) {
  /* Parsing configuration yaml file */
  if (!std::getenv("TAINT_CONFIGURATION_FILE")) {
    errs() << PRINT_RED;
    errs() << "[-] TAINT_CONFIGURATION_FILE environment variable is not set\n";
    errs() << PRINT_RESET;
    return false;
  }

  std::string contents = file_get_contents<std::string>(
      std::getenv("TAINT_CONFIGURATION_FILE"));
  ryml::Tree config = ryml::parse_in_arena(ryml::to_csubstr(contents));

  for (auto taintEntry : config["taint"]) {
    std::string taint_func_name;
    taintEntry["name"] >> taint_func_name;
    if (taint_func_name == funcName) {
      for (int index : *taintIndices) {
        if (!taintEntry["taint_params"].has_child(index)) {
          taintEntry["taint_params"].append_child() << index;
        }
      }
    }
  }

  /* Create configuration update file */

  std::FILE *fp = std::fopen(std::getenv("TAINT_CONFIGURATION_FILE"), "w");
  ryml::emit_yaml(config, fp);
  std::fclose(fp);

  return true;
}
/*******************************************
 * Function Replication
 *******************************************/
static void insertCreatedFunction(Function *F, std::string funcName) {
  std::ofstream out(createdFuncFile, std::ios_base::app);
  out << funcName << "\n";
}

static bool isCreatedFunction(std::string funcName) {
  std::ifstream in(createdFuncFile);
  std::string line;
  while (std::getline(in, line)) {
    if (line == funcName) {
      return true;
    }
  }

  return false;
}

static void replicateSingleFunctionIndex(Function *F, int index) {
  FunctionType *FT = F->getFunctionType();
  std::vector<int> *taintIndices = new std::vector<int>();

  if (!isCreatedFunction(F->getName().str())) {
    /* Create function checking the given address is original function */
    std::string checkFuncName;
    if (F->hasLocalLinkage()) {
      std::string fileName = getFileBaseName(F);
      checkFuncName = "check_" + fileName + "_" + F->getName().str();
    } else {
      checkFuncName = "check_" + F->getName().str();
    }

    FunctionType *checkFuncType =
        FunctionType::get(Type::getInt1Ty(F->getContext()),
                          {Type::getInt64Ty(F->getContext())}, false);
    Function *CheckFunc =
        Function::Create(checkFuncType, GlobalValue::ExternalLinkage,
                        checkFuncName, F->getParent());
    BasicBlock *CheckBB = BasicBlock::Create(F->getContext(), "entry", CheckFunc);

    IRBuilder<> CheckBuilder(CheckBB);

    Instruction *Ret =
        CheckBuilder.CreateRet(ConstantInt::getFalse(F->getContext()));

    CheckBuilder.SetInsertPoint(Ret);

    Value *CheckArg = CheckFunc->arg_begin();

    Value *CmpInst = CheckBuilder.CreateICmpEQ(
      CheckBuilder.CreateBitCast(F, Type::getInt64Ty(F->getContext())),
      CheckArg);

    Instruction *ThenTerm, *ElseTerm;
    SplitBlockAndInsertIfThenElse(CmpInst, Ret, &ThenTerm,
                                  &ElseTerm, nullptr);

    CheckBuilder.SetInsertPoint(ThenTerm);
    CheckBuilder.CreateRet(ConstantInt::getTrue(F->getContext()));

    BasicBlock *ThenBB = ThenTerm->getParent();
    ThenBB->getTerminator()->eraseFromParent();

    insertCreatedFunction(F, F->getName().str());
  }

  unsigned int numArgs = F->arg_empty() ? 0 : F->arg_size();
  for (int j = 0; j < numArgs; j++) {
    if (index & (1 << j)) {
      taintIndices->push_back(j);
    }
  }

  /* Function name depends on the linkage */
  std::string funcName;
  if (F->hasLocalLinkage()) {
    std::string fileName = getFileBaseName(F);
    funcName = "replicate_" + fileName + "_" + F->getName().str() + "_" +
                std::to_string(index);
  } else {
    funcName = "replicate_" + F->getName().str() + "_" + std::to_string(index);
  }

  insertCreatedFunction(F, funcName);

  ValueToValueMapTy VMap;
  Function *cloneF = CloneFunction(F, VMap);
  cloneF->setName(funcName);
  cloneF->setLinkage(GlobalValue::ExternalLinkage);

#ifdef TAINT_DEBUG
  errs() << PRINT_MAGENTA;
  errs() << "++ Create replicated function: " << cloneF->getName() << " ++\n";
  errs() << PRINT_GREEN;
  errs() << "\n";
  errs() << PRINT_RESET;
#endif
  addTaintFunctions(cloneF, taintIndices, nullptr);
}

static void replicateSingleFunction(Function *F) {
  unsigned int numArgs = F->arg_empty() ? 0 : F->arg_size();
  if (numArgs > MAX_PARAM_INDEX) {
#ifdef TAINT_DEBUG
    errs() << PRINT_RED;
    errs() << "[-] Failed to replicate function: " << F->getName() << "\n";
    errs() << "[-] Number of arguments is too large\n";
    errs() << PRINT_RESET;
#endif
    return;
  }
  unsigned int totalReplicated = 1 << numArgs;

#ifdef TAINT_DEBUG
  errs() << PRINT_MAGENTA;
  errs() << "++ Replicate Function [" << F->getName() << "] ++\n";
  errs() << PRINT_GREEN;
  errs() << "Total number of arguments: " << numArgs << "\n";
  errs() << "Total number of replicated functions: " << totalReplicated << "\n";
  errs() << PRINT_RESET;
#endif

  /* Check the function is in Multi-entry function list */
  bool isMultiEntry = false;
  int meInputIndex = -1;
  int meLengthIndex = -1;
  int mefunctionIndex = 0;
  if (isCrossFuzz &&
      F->hasName() && 
      isInMultiEntryFunctionList(F->getName().str()) && 
      isInTargetList(F->getName().str()) &&
      (F->getName() != std::getenv("TARGET_FUNC_NAME"))) {
    isMultiEntry = true;
    meInputIndex = getTargetInputIndex(F->getName().str());
    meLengthIndex = getTargetLengthIndex(F->getName().str());
    for (int i=0; i<F->arg_size(); i++) {
      if (i == meInputIndex || i == meLengthIndex) {
        continue;
      }
      mefunctionIndex |= (1 << i);
    }
  }

  /* Create function checking the given address is original function */
  std::string checkFuncName;
  if (F->hasLocalLinkage()) {
    std::string fileName = getFileBaseName(F);
    checkFuncName = "check_" + fileName + "_" + F->getName().str();
  } else {
    checkFuncName = "check_" + F->getName().str();
  }

  FunctionType *checkFuncType =
      FunctionType::get(Type::getInt1Ty(F->getContext()),
                        {Type::getInt64Ty(F->getContext())}, false);
  Function *CheckFunc =
      Function::Create(checkFuncType, GlobalValue::ExternalLinkage,
                       checkFuncName, F->getParent());
  BasicBlock *CheckBB = BasicBlock::Create(F->getContext(), "entry", CheckFunc);

  IRBuilder<> CheckBuilder(CheckBB);

  Instruction *Ret =
      CheckBuilder.CreateRet(ConstantInt::getFalse(F->getContext()));

  CheckBuilder.SetInsertPoint(Ret);

  Value *CheckArg = CheckFunc->arg_begin();

  Value *CmpInst = CheckBuilder.CreateICmpEQ(
    CheckBuilder.CreateBitCast(F, Type::getInt64Ty(F->getContext())),
    CheckArg);

  Instruction *ThenTerm, *ElseTerm;
  SplitBlockAndInsertIfThenElse(CmpInst, Ret, &ThenTerm,
                                &ElseTerm, nullptr);

  CheckBuilder.SetInsertPoint(ThenTerm);
  CheckBuilder.CreateRet(ConstantInt::getTrue(F->getContext()));

  BasicBlock *ThenBB = ThenTerm->getParent();
  ThenBB->getTerminator()->eraseFromParent();

  /* For instance, if the number of functions' arguments is 4,
   * replication for 0000 ~ 1111 would be generated.
   * 0000: original function
   */
  for (int i = 0; i < totalReplicated; i++) {
    std::vector<int> *taintIndices = new std::vector<int>();
    for (int j = 0; j < numArgs; j++) {
      if (i & (1 << j)) {
        taintIndices->push_back(j);
      }
    }

    FunctionType *FT = F->getFunctionType();
    /* Function name depends on the linkage */
    std::string funcName;
    if (F->hasLocalLinkage()) {
      std::string fileName = getFileBaseName(F);
      funcName = "replicate_" + fileName + "_" + F->getName().str() + "_" +
                 std::to_string(i);
    } else {
      funcName = "replicate_" + F->getName().str() + "_" + std::to_string(i);
    }

    insertCreatedFunction(F, funcName);

    ValueToValueMapTy VMap;
    Function *cloneF = CloneFunction(F, VMap);
    cloneF->setName(funcName);
    cloneF->setLinkage(GlobalValue::ExternalLinkage);

    /* Multi entry */
    if (isMultiEntry && i == mefunctionIndex) {
      std::string targetFunctionName = entryFunctionName;
      std::string multiEntryFileName = "/tmp/" + targetFunctionName + "_multi_entry.txt";
      /* Record MultiEntry Function to file */
      std::FILE *fp = std::fopen(multiEntryFileName.c_str(), "a");
      std::fprintf(fp, "%s\n", funcName.c_str());
      std::fclose(fp);
    }

#ifdef TAINT_DEBUG
    errs() << PRINT_MAGENTA;
    errs() << "++ Create replicated function: " << cloneF->getName() << " ++\n";
    errs() << PRINT_GREEN;
    errs() << "Taint indices: ";
    if (taintIndices->empty()) {
      errs() << "None";
    } else {
      for (int index : *taintIndices) {
        errs() << index << " ";
      }
    }
    errs() << "\n";
    errs() << PRINT_RESET;
#endif
    addTaintFunctions(cloneF, taintIndices, nullptr);
  }
}

static void replicateFunctions(Module &M) {
  for (auto &FuncName : GlobalScopeList) {
    if (isInTaintBanList(FuncName) ||
        isInIgnoreFunctionsList(FuncName) ||
        isInAssertFunctionsList(FuncName)) {
      continue;
    }
    Function *F = M.getFunction(FuncName);
    if (F && !F->isDeclaration() && !F->isIntrinsic()) {
      replicateSingleFunction(F);
    }
  }
}

/*******************************************
 * Simple Taint Analysis
 *******************************************/
std::map<Function *, std::vector<Value *>> taintMap;

static void cleanTaintValues(Function *F) {
  if (taintMap.find(F) != taintMap.end()) {
    taintMap.erase(F);
  }
}

static void addTaintValue(Function *F, Value *V) {
  if (taintMap.find(F) == taintMap.end()) {
    taintMap[F] = std::vector<Value *>();
  }

  if (std::find(taintMap[F].begin(), taintMap[F].end(), V) ==
      taintMap[F].end()) {
    taintMap[F].push_back(V);
  }
}

static bool isTaintValue(Function *F, Value *V) {
  /* Tainted by global variable */
  if (std::find(taintMap[nullptr].begin(), taintMap[nullptr].end(), V) !=
      taintMap[nullptr].end()) {
    return true;
  }

  /* No taint values in current function */
  if (taintMap.find(F) == taintMap.end()) {
    return false;
  }

  return std::find(taintMap[F].begin(), taintMap[F].end(), V) !=
         taintMap[F].end();
}

static void singleLineTaintFlow(Module &M, Function &F, Instruction &I) {
  bool tainted = false;
  bool retNotTainted = false;

  if (auto *Store = dyn_cast<StoreInst>(&I)) {
    Value *from = Store->getValueOperand();
    Value *to = Store->getPointerOperand();

    if (isTaintValue(&F, from) && !isTaintValue(&F, to)) {
      /* A' -> B */
      addTaintValue(&F, to);
      addTaintValue(&F, Store);
      tainted = true;
      if (auto *GEP = dyn_cast<GetElementPtrInst>(to)) {
        Value *GEPFrom = GEP->getPointerOperand();
        if (!isTaintValue(&F, GEPFrom)) {
          /* From now, the base is also tainted */
          addTaintValue(&F, GEPFrom);
        }
      }
    } else if (isTaintValue(&F, from) && isTaintValue(&F, to)) {
      /* A' -> B' */
      addTaintValue(&F, to);
      addTaintValue(&F, Store);
      tainted = true;
    } else if (!isTaintValue(&F, from) && isTaintValue(&F, to)) {
      /* A -> B' */
      addTaintValue(&F, Store);
      tainted = true;
    }
  } else if (auto *Load = dyn_cast<LoadInst>(&I)) {
    Value *from = Load->getPointerOperand();
    Value *to = Load;

    if (isTaintValue(&F, from)) {
      addTaintValue(&F, to);
      tainted = true;
    }
  } else if (auto *GetElementPtr = dyn_cast<GetElementPtrInst>(&I)) {
    Value *from = GetElementPtr->getPointerOperand();
    Value *offset = GetElementPtr->getOperand(1);
    Value *to = GetElementPtr;

    if (isTaintValue(&F, from)) {
      /* A' + B = C', A' + B' = C' */
      addTaintValue(&F, to);
      tainted = true;
    } else if (!isTaintValue(&F, from) && isTaintValue(&F, offset)) {
      /* A + B' = C' */
      addTaintValue(&F, to);
      tainted = true;
    }
  } else if (auto *Phi = dyn_cast<PHINode>(&I)) {
    for (Value *arg : Phi->incoming_values()) {
      if (isTaintValue(&F, arg)) {
        tainted = true;
      }
    }
    if (tainted) {
      addTaintValue(&F, Phi);
    }
  } else if (auto *Binary = dyn_cast<BinaryOperator>(&I)) {
    for (Value *operand : Binary->operands()) {
      if (isTaintValue(&F, operand)) {
        addTaintValue(&F, Binary);
        tainted = true;
      }
    }
  } else if (auto *Conversion = dyn_cast<CastInst>(&I)) {
    Value *from = Conversion->getOperand(0);
    Value *to = Conversion;

    if (isTaintValue(&F, from)) {
      addTaintValue(&F, to);
      tainted = true;
    }
  } else if (auto *Cmp = dyn_cast<CmpInst>(&I)) {
    Value *op1 = Cmp->getOperand(0);
    Value *op2 = Cmp->getOperand(1);

    if (isTaintValue(&F, op1) || isTaintValue(&F, op2)) {
      addTaintValue(&F, Cmp);
      tainted = true;
    }
  } else if (auto *Call = dyn_cast<CallInst>(&I)) {
    /* How to know the result of the call is tainted or not? */
    Function *callee = Call->getCalledFunction();
    if (callee && isInGlobalScopeList(callee->getName().str()) &&
        !isInTaintBanList(callee->getName().str()) &&
        !isInAssertFunctionsList(callee->getName().str()) &&
        !isInIgnoreFunctionsList(callee->getName().str()) &&
        !isStdFunction(callee) && !callee->isIntrinsic() &&
        callee->arg_size() < MAX_PARAM_INDEX &&
        !callee->isVarArg()) {
      /* Replace the callee with the replicated one */
      int functionIndex = 0;
      for (int i = 0; i < Call->arg_size(); i++) {
        if (isTaintValue(&F, Call->getArgOperand(i))) {
          functionIndex |= (1 << i);
        }
      }

      /* Used for cross fuzz */
      bool needNewStart = false;
      if (isCrossFuzz && isInTargetList(callee->getName().str())) {
        /* In this case, we need to check the taint status corresponds to 
         * target information */
        int targetInputIndex = getTargetInputIndex(callee->getName().str());
        int targetLengthIndex = getTargetLengthIndex(callee->getName().str());
        for (int i = 0; i < Call->arg_size(); i++) {
          if ((i == targetInputIndex) && isTaintValue(&F, Call->getArgOperand(i))) {
            needNewStart = true;
          }
          if ((i == targetLengthIndex) && isTaintValue(&F, Call->getArgOperand(i))) {
            needNewStart = true;
          }
        }
      }

      /* Function name depends on the linkage */
      std::string funcName;
      if (callee->hasLocalLinkage()) {
        std::string fileName = getFileBaseName(callee);
        funcName = "replicate_" + fileName + "_" + callee->getName().str() +
                   "_" + std::to_string(functionIndex);
      } else {
        funcName = "replicate_" + callee->getName().str() + "_" +
                   std::to_string(functionIndex);
      }

      /* Create a new replicated function (on-demand) */
      if (!isCreatedFunction(funcName)) {
        replicateSingleFunctionIndex(callee, functionIndex);
      }

      /* Replace function to replicated one */
      Function *replicateF = M.getFunction(funcName);
      if (!replicateF) {
        replicateF = Function::Create(callee->getFunctionType(),
                                      Function::ExternalLinkage, funcName, &M);
      }

#ifdef TAINT_DEBUG
      errs() << PRINT_YELLOW;
      errs() << "- ";
      Call->print(errs());
      errs() << "\n";
      errs() << PRINT_RESET;
#endif

      /* We will append new entry functions statically */
      if (needNewStart) {
        /* Need to register new entry point */
        int targetInputIndex = getTargetInputIndex(callee->getName().str());
        int targetLengthIndex = getTargetLengthIndex(callee->getName().str());
        int targetFunctionIndex = 0;
        for (int i = 0; i < Call->arg_size(); i++) {
          if ((i == targetInputIndex) || (i == targetLengthIndex)) {
            continue;
          }
          targetFunctionIndex |= (1 << i);
        }
        std::string targetFuncName;
        if (callee->hasLocalLinkage()) {
          std::string fileName = getFileBaseName(callee);
          targetFuncName = "replicate_" + fileName + "_" + callee->getName().str() +
                    "_" + std::to_string(targetFunctionIndex);
        } else {
          targetFuncName = "replicate_" + callee->getName().str() + "_" +
                    std::to_string(targetFunctionIndex);
        }

        Function *replicateTargetF = M.getFunction(targetFuncName);
        if (!replicateTargetF) {
          replicateTargetF = Function::Create(callee->getFunctionType(),
                                      Function::ExternalLinkage, 
                                      targetFuncName, &M);
        }

        /* Call Register Entry List */
        Function *registerFunc = M.getFunction("registerEntryFunction");
        if (!registerFunc) {
          FunctionType *registerFuncType =
          FunctionType::get(Type::getVoidTy(M.getContext()),
                            {Type::getInt64Ty(M.getContext()),
                            Type::getInt32Ty(M.getContext()),
                            Type::getInt32Ty(M.getContext()),
                            Type::getInt32Ty(M.getContext())}, false);
          registerFunc = Function::Create(registerFuncType, 
                                          Function::ExternalLinkage, 
                                          "registerEntryFunction",
                                          &M);
        }
        /* Get current_test_depth */
        GlobalVariable *currentTestDepth = M.getGlobalVariable("current_test_depth");
        if (!currentTestDepth) {
          currentTestDepth = new GlobalVariable(
              M, Type::getInt32Ty(M.getContext()), false,
              GlobalValue::ExternalLinkage, nullptr, "current_test_depth");
        }
        IRBuilder<> Builder(Call);

        LoadInst *loadInst = Builder.CreateLoad(currentTestDepth->getValueType(), currentTestDepth, "load_current_test_depth");

        /* Insert register functiony */
        Builder.CreateCall(registerFunc, {Builder.CreateBitCast(replicateTargetF, Type::getInt64Ty(M.getContext())),
                                          ConstantInt::get(Type::getInt32Ty(M.getContext()), targetInputIndex),
                                          ConstantInt::get(Type::getInt32Ty(M.getContext()), targetLengthIndex),
                                          loadInst});
#ifdef TAINT_DEBUG
        errs() << PRINT_YELLOW;
        errs() << "Register new entry point " << funcName << "\n";
        errs() << PRINT_RESET;
#endif
      }

      Call->setCalledFunction(replicateF);
#ifdef TAINT_DEBUG
      errs() << PRINT_CYAN;
      errs() << "+ ";
      Call->print(errs());
      errs() << "\n";
      errs() << PRINT_RESET;
#endif

      /* Check the callee is analyzed */
      if (isFunctionAnalyzed(funcName)) {
        /* Function is already analyzed */

        /* The return value of function is tainted */
        if (isReturnTainted(funcName)) {
          addTaintValue(&F, Call);
          tainted = true;
        }

        /* The parameter is tainted in the function */
        std::vector<int> *taintIndices = getParamIsTainted(funcName);
        if (taintIndices) {
          for (int index : *taintIndices) {
            addTaintValue(&F, Call->getArgOperand(index));
          }
        }
      } else {
        /* Function is not analyzed yet */
#ifdef TAINT_DEBUG
        errs() << PRINT_BLUE;
        errs() << "[-] Function is not analyzed yet: " << funcName << "\n";
        errs() << PRINT_RESET;
#endif
        /* We first taint it and fix it in later build */
        addTaintValue(&F, Call);
        /* Taint all arguments */
        for (int i = 0; i < Call->arg_size(); i++) {
          addTaintValue(&F, Call->getArgOperand(i));
        }
        tainted = true;
      }
    } else {
      /* TODO: It is not precise */
      for (int i = 0; i < Call->arg_size(); i++) {
        if (isTaintValue(&F, Call->getArgOperand(i))) {
          /* If at least one argument is tainted, taint the call result */
          tainted = true;
        }
      }
      /* If the call is indirect call, taint it */
      if (Call->isIndirectCall()) {
        tainted = true;
      }

      if (tainted) {
        addTaintValue(&F, Call);
      }
    }
  } else if (auto *Switch = dyn_cast<SwitchInst>(&I)) {
    Value *from = Switch->getCondition();
    Value *to = Switch;

    if (isTaintValue(&F, from)) {
      addTaintValue(&F, to);
      tainted = true;
    }
  } else if (auto *Return = dyn_cast<ReturnInst>(&I)) {
    Value *ret = Return->getReturnValue();
    std::string funcName = F.getName().str();
    if (isFunctionAnalyzed(funcName)) {
      /* It is already analyzed */
      bool isTainted = isReturnTainted(funcName);
      if (isTainted != isTaintValue(&F, ret)) {
        /* TODO: dynamic taint analysis */
        if (isTainted) {
          /* Taint true --> false is allowed */
#ifdef TAINT_DEBUG
          errs() << PRINT_RED;
          errs() << "[!] Return value of " << F.getName().str()
                 << " is updated from " << isTainted << " to "
                 << isTaintValue(&F, ret) << "\n";
          errs() << PRINT_RESET;
#endif
          updateTaintReturnConfiguration(F, funcName, isTaintValue(&F, ret));

          updateReturnIsTainted(funcName, isTaintValue(&F, ret));
        } else {
          /* Taint false --> true is not allowed */
#ifdef TAINT_DEBUG
          errs() << PRINT_RED;
          errs() << "[!] Return value of " << F.getName().str()
                 << " is not updated.\n";
          errs() << PRINT_RESET;
#endif
        }
      }
    } else {
      /* Update taint configuration */
      insertTaintConfiguration(funcName, isTaintValue(&F, ret));

      /* Update local list */
      addReturnIsTainted(funcName, isTaintValue(&F, ret));
    }
  }

#ifdef TAINT_DEBUG
  if (tainted) {
    std::cerr << PRINT_GREEN;
  } else if (retNotTainted) {
    std::cerr << PRINT_YELLOW;
  }
  I.print(errs());
  errs() << "\n";
  std::cerr << PRINT_RESET;
#endif
}

static void singleBlockTaintFlow(Module &M, Function &F, BasicBlock &BB) {
#ifdef TAINT_DEBUG
  if (BB.hasName()) {
    errs() << PRINT_YELLOW;
    errs() << BB.getName() << ":\n";
    errs() << PRINT_RESET;
  }
#endif
  for (Instruction &I : BB) {
    singleLineTaintFlow(M, F, I);
  }
}

static void singleFunctionTaintFlow(Module &M, Function &F,
                                    std::vector<int> *taintIndices) {
  for (int index : *taintIndices) {
    addTaintValue(&F, F.getArg(index));
#ifdef TAINT_DEBUG
    std::cerr << PRINT_GREEN;
    std::cerr << "Taint index " << index << ": ";
    /* Print every use cases */
    F.getArg(index)->print(errs());
    errs() << "\n";
    std::cerr << PRINT_RESET;
#endif
  }

  for (BasicBlock &BB : F) {
    singleBlockTaintFlow(M, F, BB);
  }

  /* Parameter is tainted inside the function */
  std::vector<int> newTaintParams;
  for (int index = 0; index < F.arg_size(); index++) {
    if (std::find(taintIndices->begin(), taintIndices->end(), index) ==
        taintIndices->end()) {
      if (isTaintValue(&F, F.getArg(index))) {
        /* Param is tainted */
        newTaintParams.push_back(index);
      }
    }
  }
  if (!newTaintParams.empty()) {
    if (getParamIsTainted(F.getName().str()) == nullptr) {
      updateTaintParamsConfiguration(F, F.getName().str(), &newTaintParams);
    } else {
      std::vector<int> *oldTaintParams = getParamIsTainted(F.getName().str());
      // std::vector<int> *intersectTaintParams =
      //     intersectVectors(oldTaintParams, &newTaintParams);
      // if (*oldTaintParams != *intersectTaintParams) {
      //   updateTaintParamsConfiguration(F, F.getName().str(), intersectTaintParams);
      // }
      std::vector<int> *unionTaintParams = unionVectors(oldTaintParams, &newTaintParams);
      if (*oldTaintParams != *unionTaintParams) {
        updateTaintParamsConfiguration(F, F.getName().str(), unionTaintParams);
      }
    }
  }
}

/*******************************************
 * Sanitizer
 *******************************************/
static CallInst *insertSanitizeLoad(Function &F, LoadInst *Inst,
                                    Function *SanitizeFunc) {
  LLVMContext &C = F.getContext();
  IRBuilder<> Builder(C);

  Builder.SetInsertPoint(Inst);

  CallInst *Call =
      Builder.CreateCall(SanitizeFunc, {Inst->getPointerOperand()});

  /* Created call should be tainted */
  addTaintValue(&F, Call);

  return Call;
}

static CallInst *insertSanitizeStore(Function &F, StoreInst *Inst,
                                     Function *SanitizeFunc) {
  LLVMContext &C = F.getContext();
  IRBuilder<> Builder(C);

  Builder.SetInsertPoint(Inst);

  CallInst *Call =
      Builder.CreateCall(SanitizeFunc, {Inst->getPointerOperand()});

  /* Created call should be tainted */
  addTaintValue(&F, Call);

  return Call;
}

static CallInst *insertSanitizeCmp(Function &F, CmpInst *Inst,
                                   Function *SanitizeFunc) {
  LLVMContext &C = F.getContext();
  IRBuilder<llvm::NoFolder> Builder(C);

  Builder.SetInsertPoint(Inst);

  Value *op1 = Inst->getOperand(0);
  Value *op2 = Inst->getOperand(1);

  CallInst *Call = Builder.CreateCall(SanitizeFunc, {op1, op2});

  /* Created call should be tainted
   * counter example: assert(addr);
   */
  if (isTaintValue(&F, op1)) {
    addTaintValue(&F, Call);
    if (LoadInst *fromLoad = dyn_cast<LoadInst>(op1)) {
      Instruction *store =
          Builder.CreateStore(Call, fromLoad->getPointerOperand());
    }
  } else {
    addTaintValue(&F, Call);
    if (LoadInst *fromLoad = dyn_cast<LoadInst>(op2)) {
      Instruction *store =
          Builder.CreateStore(Call, fromLoad->getPointerOperand());
    }
  }

  return Call;
}

static CallInst *insertSanitizeSwitch(Function &F, SwitchInst *Inst,
                                      Function *SanitizeFunc) {
  LLVMContext &C = F.getContext();
  IRBuilder<llvm::NoFolder> Builder(C);

  Builder.SetInsertPoint(Inst);

  Value *from = Inst->getCondition();

  CallInst *Call = Builder.CreateCall(SanitizeFunc, {from});

  /* Created call should be tainted */
  addTaintValue(&F, Call);

  return Call;
}

/* Get sanitize function full suffix */
static std::string getFunctionSuffix(CallInst *Inst) {
  /* Example of sanitize function : sanitizeCall7with1.13_64, sanitizeCall72.6.13_32 */
  std::string func_suffix = "with";
  func_suffix += std::to_string(Inst->arg_size());
  for (int i=0; i<Inst->arg_size(); i++) {
    if (Inst->getArgOperand(i)->getType()->isIntegerTy()) {
      func_suffix += ".13_" + std::to_string(Inst->getArgOperand(i)->getType()->getIntegerBitWidth());
    } else {
      func_suffix += "." + std::to_string(Inst->getArgOperand(i)->getType()->getTypeID());
    }
  }
  return func_suffix;
}

static GlobalVariable *insertSanitizeDirectCall(Function &F, CallInst *Inst) {
  Module &M = *F.getParent();
  /* Create a sanitize function */
  std::vector<Type *> argsTypes;
  for (int i = 0; i < Inst->arg_size(); i++) {
    argsTypes.push_back(Inst->getArgOperand(i)->getType());
  }
  FunctionType *sanitizeFuncType = FunctionType::get(
      Inst->getType(), argsTypes, false);
  std::string sanitizeType;
  if (Inst->getType()->isIntegerTy()) {
    sanitizeType =
        "13_" + std::to_string(Inst->getType()->getIntegerBitWidth());
  } else {
    sanitizeType = std::to_string(Inst->getType()->getTypeID());
  }
  std::string sanitizeFullType = sanitizeType + getFunctionSuffix(Inst);

  /* Sanitize function */
  /* Example: sanitizeCall7with1.13_64 */
  Function *sanitizeFunc = M.getFunction("sanitizeCall" + sanitizeFullType);
  if (!sanitizeFunc) {
    sanitizeFunc =
        Function::Create(sanitizeFuncType, Function::ExternalLinkage,
                          "sanitizeCall" + sanitizeFullType, &M);
  }

  /* Real sanitize function */
  /* Example: sanitizeCall7 */
  std::vector<Type *> realArgsTypes;
  for (int i=0; i<MAX_PARAM_INDEX; i++) {
    realArgsTypes.push_back(Type::getInt64Ty(F.getContext())->getPointerTo());
  }
  FunctionType *realSanitizeFuncType = FunctionType::get(
      Inst->getType(), realArgsTypes, false);
  Function *realSanitizeFunc = M.getFunction("sanitizeCall" + sanitizeType);
  if (!realSanitizeFunc) {
    realSanitizeFunc =
        Function::Create(realSanitizeFuncType, Function::ExternalLinkage,
                        "sanitizeCall" + sanitizeType, &M);
  }

  LLVMContext &C = F.getContext();
  IRBuilder<> Builder(C);

  Builder.SetInsertPoint(Inst);

  /* Save original function pointer */
  Value *OrigFunc =
      Builder.CreateBitCast(Inst->getCalledFunction(), Builder.getPtrTy());
  GlobalVariable *GV =
      M.getGlobalVariable("sanitizeCall" + sanitizeType + "Orig");
  if (!GV) {
    GV = new GlobalVariable(M, Builder.getPtrTy(), false,
                            GlobalValue::ExternalLinkage, nullptr,
                            "sanitizeCall" + sanitizeType + "Orig");
  }
  StoreInst *Store = Builder.CreateStore(OrigFunc, GV);

#ifdef SANITIZE_DEBUG
  std::cerr << PRINT_CYAN << "+ ";
  Store->print(errs());
  errs() << "\n";
  std::cerr << PRINT_RESET;
#endif

  /* Build sanitize function */
  BasicBlock *EntryBB = BasicBlock::Create(C, "entry", sanitizeFunc);
  IRBuilder<> sanitizeBuilder(EntryBB);

  std::vector<Value *> args;
  for (int i = 0; i < sanitizeFunc->arg_size(); i++) {
    if (sanitizeFunc->getArg(i)->getType()->isIntegerTy()) {
      Value *arg = sanitizeBuilder.CreateIntToPtr(
          sanitizeFunc->getArg(i),
          Type::getInt64Ty(C)->getPointerTo()
      );
      args.push_back(arg);
      continue;
    } else {
      Value *arg = sanitizeBuilder.CreateBitCast(
          sanitizeFunc->getArg(i),
          Type::getInt64Ty(C)->getPointerTo()
      );
      args.push_back(arg);
      continue;
    }
  }
  for (int i = sanitizeFunc->arg_size(); i < MAX_PARAM_INDEX; i++) {
    args.push_back(
        ConstantPointerNull::get(Type::getInt64Ty(C)->getPointerTo())
    );
  }

  CallInst *Call = sanitizeBuilder.CreateCall(realSanitizeFunc, args);
  if (realSanitizeFunc->getReturnType()->isVoidTy()) {
      sanitizeBuilder.CreateRetVoid();
  } else {
      sanitizeBuilder.CreateRet(Call);
  }

  Inst->setCalledFunction(sanitizeFunc);

  return GV;
}

static GlobalVariable *insertSanitizeIndirectCall(Function &F, CallInst *Inst,
                                                  Function *SanitizeFunc) {
  LLVMContext &C = F.getContext();
  IRBuilder<> Builder(C);

  Builder.SetInsertPoint(Inst);

  /* TODO */
  Value *OrigFunc =
      Builder.CreateBitCast(Inst->getCalledOperand(), Builder.getPtrTy());

  Module &M = *F.getParent();
  GlobalVariable *GV =
      M.getGlobalVariable(SanitizeFunc->getName().str() + "Orig");
  if (!GV) {
    GV = new GlobalVariable(M, Builder.getPtrTy(), false,
                            GlobalValue::ExternalLinkage, nullptr,
                            SanitizeFunc->getName().str() + "Orig");
  }

  StoreInst *Store = Builder.CreateStore(OrigFunc, GV);
#ifdef SANITIZE_DEBUG
  std::cerr << PRINT_CYAN << "+ ";
  Store->print(errs());
  errs() << "\n";
  std::cerr << PRINT_RESET;
#endif

  std::vector<Value *> args;
  
  for (int i = 0; i < Inst->arg_size(); i++) {
    if (Inst->getArgOperand(i)->getType() != SanitizeFunc->getArg(i)->getType()) {
      if (Inst->getArgOperand(i)->getType()->isIntegerTy()) {
        args.push_back(Builder.CreateIntToPtr(Inst->getArgOperand(i), SanitizeFunc->getArg(i)->getType()));
      } else {
        args.push_back(Builder.CreateBitCast(Inst->getArgOperand(i), SanitizeFunc->getArg(i)->getType()));
      }
    } else {
      args.push_back(Inst->getArgOperand(i));
    }
  }
  for (int i = Inst->arg_size(); i < 8; i++) {
    args.push_back(
        ConstantPointerNull::get(Type::getInt64Ty(C)->getPointerTo())
    );
  }

  CallInst *NewIndirectCall = Builder.CreateCall(SanitizeFunc, args);

  // Remove Inst
  Inst->replaceAllUsesWith(NewIndirectCall);
  Inst->eraseFromParent();

  // Inst->setCalledFunction(SanitizeFunc);
  return GV;
}

static CallInst *
insertIndirectLinkCall(Function &F, CallInst *Inst,
                       std::vector<std::string> &IndirectLinks) {
  LLVMContext &C = F.getContext();
  IRBuilder<> Builder(C);

  Module &M = *F.getParent();

  /* Arguments are same as Inst's */
  std::vector<Value *> args;
  for (int i = 0; i < Inst->arg_size(); i++) {
    args.push_back(Inst->getArgOperand(i));
  }

  /* Indirect function pointers */
  std::vector<Function *> IndirectFuncs;
  std::map<Function *, int> IndirectInputIndex;
  std::map<Function *, int> IndirectLengthIndex;

  CallInst *insertBefore = Inst;
  CallInst *Call = Inst;
  for (std::string &IndirectLink : IndirectLinks) {
    if (isInTaintBanList(IndirectLink) || isInIgnoreFunctionsList(IndirectLink) ||
        isInAssertFunctionsList(IndirectLink)) {
      continue;
    }

    Function *IndirectFunc = M.getFunction(IndirectLink);
    if (!IndirectFunc) {
      IndirectFunc = Function::Create(
          Call->getFunctionType(), Function::ExternalLinkage, IndirectLink, M);
    }

    if (IndirectFunc->isIntrinsic() || isStdFunction(IndirectFunc)) {
      continue;
    }

    /* Check invalid analysis */
    int indirectFuncArgNum = getIndirectArgNum(IndirectLink);
    if (indirectFuncArgNum != Call->arg_size()) {
      continue;
    }

    /* Replace the callee with the replicated one */
    int functionIndex = 0;
    for (int i = 0; i < Call->arg_size(); i++) {
      if (isTaintValue(&F, Call->getArgOperand(i))) {
        functionIndex |= (1 << i);
      }
    }

    /* Function name depends on the linkage */
    std::string funcName;
    if (IndirectFunc->hasLocalLinkage()) {
      std::string fileName = getFileBaseName(IndirectFunc);
      funcName = "replicate_" + fileName + "_" + IndirectFunc->getName().str() +
                 "_" + std::to_string(functionIndex);
    } else if (hasIndirectLinkage(IndirectFunc->getName().str())) {
      // Internal linkage
      std::string fileName = getIndirectLinkage(IndirectFunc->getName().str());
      funcName = "replicate_" + fileName + "_" + IndirectFunc->getName().str() +
                 "_" + std::to_string(functionIndex);
    } else {
      funcName = "replicate_" + IndirectFunc->getName().str() + "_" +
                 std::to_string(functionIndex);
    }

    /* Replace function to replicated one */
    Function *replicateF = M.getFunction(funcName);
    if (!replicateF) {
      replicateF =
          Function::Create(IndirectFunc->getFunctionType(),
                          Function::ExternalLinkage, funcName, &M);
    }

    Builder.SetInsertPoint(insertBefore);

    /* Check call */
    std::string checkFuncName;
    if (IndirectFunc->hasLocalLinkage()) {
      std::string fileName = getFileBaseName(IndirectFunc);
      checkFuncName = "check_" + fileName + "_" + IndirectFunc->getName().str();
    } else if (hasIndirectLinkage(IndirectFunc->getName().str())) {
      std::string fileName = getIndirectLinkage(IndirectFunc->getName().str());
      checkFuncName = "check_" + fileName + "_" + IndirectFunc->getName().str();
    } else {
      checkFuncName = "check_" + IndirectFunc->getName().str();
    }
    Function *CheckFunc = M.getFunction(checkFuncName);
    if (!CheckFunc) {
      FunctionType *checkFuncType =
          FunctionType::get(Type::getInt1Ty(C), {Type::getInt64Ty(C)}, false);
      CheckFunc = Function::Create(checkFuncType, GlobalValue::ExternalLinkage,
                                   checkFuncName,
                                   F.getParent());
    }
    CallInst *CheckCall =
        Builder.CreateCall(CheckFunc, 
          {Builder.CreatePtrToInt(insertBefore->getCalledOperand(), Type::getInt64Ty(C))});
    Value *Cmp = Builder.CreateICmpEQ(CheckCall, ConstantInt::getTrue(C));

    Instruction *ThenTerm, *ElseTerm;
    SplitBlockAndInsertIfThenElse(Cmp, insertBefore, &ThenTerm, &ElseTerm,
                                  nullptr);
    
    Builder.SetInsertPoint(ThenTerm);
    Builder.CreateCall(replicateF, args);

    CallInst *CloneCall = dyn_cast<CallInst>(Call->clone());
    Call->replaceAllUsesWith(CloneCall);
    Call->eraseFromParent();
    CloneCall->insertBefore(ElseTerm);

    /* Save indirect funcs */
    IndirectFuncs.push_back(replicateF);
    IndirectInputIndex[replicateF] = getIndirectInputIndex(IndirectLink);
    IndirectLengthIndex[replicateF] = getIndirectLengthIndex(IndirectLink);

    Call = CloneCall;
    insertBefore = CloneCall;
  }

  /* Set true checkIndirectLink */
  Builder.SetInsertPoint(Call);
  GlobalVariable *GV = F.getParent()->getGlobalVariable("checkIndirectLink");
  if (!GV) {
    GV = new GlobalVariable(*F.getParent(), Builder.getInt1Ty(), false,
                            GlobalValue::ExternalLinkage,
                            0, "checkIndirectLink");
  }
  Builder.CreateStore(ConstantInt::getTrue(C), GV);

  /* Global array saving function pointers */
  ArrayType *ATy = ArrayType::get(Builder.getPtrTy(), IndirectFuncs.size() + 1);
  std::vector<Constant *> FuncPtrs;
  ArrayType *ITy = ArrayType::get(Builder.getInt32Ty(), IndirectFuncs.size() + 1);
  std::vector<int> InputIndices;
  std::vector<int> LengthIndices;

  for (Function *IndirectFunc : IndirectFuncs) {
    FuncPtrs.push_back(ConstantExpr::getBitCast(IndirectFunc, Builder.getPtrTy()));
    InputIndices.push_back(IndirectInputIndex[IndirectFunc]);
    LengthIndices.push_back(IndirectLengthIndex[IndirectFunc]);
  }
  FuncPtrs.push_back(Constant::getNullValue(Builder.getPtrTy()));
  InputIndices.push_back(-1);
  LengthIndices.push_back(-1);
  Constant *GA = ConstantArray::get(ATy, FuncPtrs);
  Constant *IA = ConstantDataArray::get(C, InputIndices);
  Constant *LA = ConstantDataArray::get(C, LengthIndices);

  GlobalVariable *GVArray = M.getGlobalVariable("IndirectLinks");
  if (!GVArray) {
    GVArray = new GlobalVariable(M, ATy, false, GlobalValue::ExternalLinkage,
                                 0, "IndirectLinks");
  }
  GlobalVariable *GVInputIndices = M.getGlobalVariable("IndirectInputIndices");
  if (!GVInputIndices) {
    GVInputIndices = new GlobalVariable(M, ITy, false, GlobalValue::ExternalLinkage,
                                 0, "IndirectInputIndices");
  }
  GlobalVariable *GVLengthIndices = M.getGlobalVariable("IndirectLengthIndices");
  if (!GVLengthIndices) {
    GVLengthIndices = new GlobalVariable(M, ITy, false, GlobalValue::ExternalLinkage,
                                 0, "IndirectLengthIndices");
  }

  Builder.CreateStore(GA, GVArray);
  Builder.CreateStore(IA, GVInputIndices);
  Builder.CreateStore(LA, GVLengthIndices);

  return Call;
}

static void registerSanitizeScope(Module &M, Function &F) {
  /* Create call at the begining of the function */
  if (F.hasName()) {
    LLVMContext &C = F.getContext();
    IRBuilder<> Builder(C);

    FunctionType *registerFuncType =
        FunctionType::get(Type::getVoidTy(C), {Builder.getPtrTy()}, false);
    Function *registerFunc = M.getFunction("registerSanitizedScope");
    if (!registerFunc) {
      registerFunc =
          Function::Create(registerFuncType, Function::ExternalLinkage,
                           "registerSanitizedScope", M);
    }

    Builder.SetInsertPoint(&F.getEntryBlock(), F.getEntryBlock().begin());

    Constant *funcName = ConstantDataArray::getString(C, F.getName());
    GlobalVariable *funcNameGV = new GlobalVariable(
        M, funcName->getType(), true, GlobalValue::InternalLinkage, funcName);

    Value *strPtr = Builder.CreatePointerCast(funcNameGV, Builder.getPtrTy());
#ifdef SANITIZE_DEBUG
    errs() << PRINT_CYAN << "+ ";
    errs() << "Register function: " << F.getName() << "\n";
    errs() << PRINT_RESET;
#endif
    Builder.CreateCall(registerFunc, {strPtr});
  }
}

static void singleFunctionSanitize(Module &M, Function &F, Value *callInst) {
  std::vector<Value *> taintValues = taintMap[&F];
  if (taintValues.empty()) {
    return;
  }

  for (Value *V : taintValues) {
    if (auto *Load = dyn_cast<LoadInst>(V)) {
      FunctionType *sanitizeFuncType =
          FunctionType::get(Load->getPointerOperand()->getType(),
                            {Load->getPointerOperand()->getType()}, false);
      Function *sanitizeFunc = M.getFunction("sanitizeLoad");
      if (!sanitizeFunc) {
        sanitizeFunc = Function::Create(
            sanitizeFuncType, Function::ExternalLinkage, "sanitizeLoad", M);
      }

      CallInst *call = insertSanitizeLoad(F, Load, sanitizeFunc);

      /* Change to use the sanitized value */
      referencePoint = call;
      Load->getOperand(0)->replaceUsesWithIf(call, use_is_referenced);

#ifdef SANITIZE_DEBUG
      std::cerr << PRINT_CYAN << "+ ";
      call->print(errs());
      errs() << "\n+ ";
      Load->print(errs());
      errs() << "\n";
      std::cerr << PRINT_RESET;
#endif
    } else if (auto *Store = dyn_cast<StoreInst>(V)) {
      Value *to = Store->getPointerOperand();
      FunctionType *sanitizeFuncType =
          FunctionType::get(to->getType(), {to->getType()}, false);
      Function *sanitizeFunc = M.getFunction("sanitizeStore");
      if (!sanitizeFunc) {
        sanitizeFunc = Function::Create(
            sanitizeFuncType, Function::ExternalLinkage, "sanitizeStore", M);
      }

      CallInst *call = insertSanitizeStore(F, Store, sanitizeFunc);
      /* Change to use the sanitized value */
      // Store->setOperand(1, call);
      referencePoint = call;
      Store->getOperand(1)->replaceUsesWithIf(call, use_is_referenced);

#ifdef SANITIZE_DEBUG
      std::cerr << PRINT_CYAN << "+ ";
      call->print(errs());
      errs() << "\n+ ";
      Store->print(errs());
      errs() << "\n";
      std::cerr << PRINT_RESET;
#endif
    } else if (auto *Call = dyn_cast<CallInst>(V)) {
      if (!Call->isIndirectCall() && Call->getCalledFunction()) {
        if (Call->getCalledFunction()->isIntrinsic() ||
            isInTaintBanList(Call->getCalledFunction()->getName().str()) ||
            isInAssertFunctionsList(
                Call->getCalledFunction()->getName().str()) ||
            isInIgnoreFunctionsList(
                Call->getCalledFunction()->getName().str()) ||
            Call->getCalledFunction()->arg_size() > MAX_PARAM_INDEX) {
          continue;
        }

        /* Encapsulate sanitizeCall if the function is not replicated */
        if (!isInGlobalScopeList(Call->getCalledFunction()->getName().str()) ||
            isInTaintBanList(Call->getCalledFunction()->getName().str()) ||  
            isStdFunction(Call->getCalledFunction()) ||
            Call->getCalledFunction()->isVarArg()) {
#ifdef SANITIZE_DEBUG
          std::cerr << PRINT_YELLOW << "- ";
          Call->print(errs());
          errs() << "\n";
          std::cerr << PRINT_RESET;
#endif
          insertSanitizeDirectCall(F, Call);
#ifdef SANITIZE_DEBUG
          std::cerr << PRINT_CYAN << "+ ";
          Call->print(errs());
          errs() << "\n";
          std::cerr << PRINT_RESET;
#endif
          continue;
        }

        /* We already change the called function during taint process */
        if (!Call->getCalledFunction()->getName().starts_with("replicate_")) {
          errs() << PRINT_RED;
          errs() << "[-] Function with taint value is not replicated["
                 << Call->getCalledFunction()->getName() << "]\n";
          errs() << PRINT_RESET;
          report_fatal_error("[-] Function with taint value is not replicated");
        }

      } else {
        if (hasIndirectLink(F.getName().str())) {
          std::vector<std::string> indirectLinks =
              getIndirectLinks(F.getName().str());
          Call = insertIndirectLinkCall(F, Call, indirectLinks);
        }

        LLVMContext &C = F.getContext();
        IRBuilder<> Builder(C);
        FunctionType *sanitizeFuncType = FunctionType::get(
            Call->getType(),
            {Builder.getPtrTy(), Builder.getPtrTy(),
             Builder.getPtrTy(), Builder.getPtrTy(),
             Builder.getPtrTy(), Builder.getPtrTy(),
             Builder.getPtrTy(), Builder.getPtrTy()},
            false);
        std::string sanitizeType;
        if (Call->getType()->isIntegerTy()) {
          sanitizeType =
              "13_" + std::to_string(Call->getType()->getIntegerBitWidth());
        } else {
          sanitizeType = std::to_string(Call->getType()->getTypeID());
        }
        Function *sanitizeFunc =
            M.getFunction("sanitizeIndirectCall" + sanitizeType);
        if (!sanitizeFunc) {
          sanitizeFunc =
              Function::Create(sanitizeFuncType, Function::ExternalLinkage,
                               "sanitizeIndirectCall" + sanitizeType, M);
        }

#ifdef SANITIZE_DEBUG
        std::cerr << PRINT_YELLOW << "- ";
        Call->print(errs());
        errs() << "\n";
        std::cerr << PRINT_RESET;
#endif

        insertSanitizeIndirectCall(F, Call, sanitizeFunc);

// #ifdef SANITIZE_DEBUG
//         std::cerr << PRINT_CYAN << "+ ";
//         Call->print(errs());
//         errs() << "\n";
//         std::cerr << PRINT_RESET;
// #endif
      }
    } else if (auto *Cmp = dyn_cast<CmpInst>(V)) {
      Value *op1 = Cmp->getOperand(0);
      Value *op2 = Cmp->getOperand(1);

      Value *sanitizedValue;
      Value *cmpValue;

      if (isTaintValue(&F, op1)) {
        sanitizedValue = op1;
        cmpValue = op2;
      } else {
        sanitizedValue = op2;
        cmpValue = op1;
      }

      FunctionType *sanitizeFuncType = FunctionType::get(
          sanitizedValue->getType(),
          {sanitizedValue->getType(), sanitizedValue->getType()}, false);
      std::string sanitizeType;
      if (sanitizedValue->getType()->isIntegerTy()) {
        sanitizeType =
            "13_" +
            std::to_string(sanitizedValue->getType()->getIntegerBitWidth());
      } else {
        sanitizeType = std::to_string(sanitizedValue->getType()->getTypeID());
      }
      Function *sanitizeFunc = M.getFunction("sanitizeCmp" + sanitizeType);
      if (!sanitizeFunc) {
        sanitizeFunc =
            Function::Create(sanitizeFuncType, Function::ExternalLinkage,
                             "sanitizeCmp" + sanitizeType, M);
      }

      CallInst *call = insertSanitizeCmp(F, Cmp, sanitizeFunc);

      referencePoint = call;
      sanitizedValue->replaceUsesWithIf(call, use_is_referenced);

#ifdef SANITIZE_DEBUG
      std::cerr << PRINT_CYAN << "+ ";
      call->print(errs());
      errs() << "\n";
      std::cerr << PRINT_RESET;
      /* Generated cmp value should be saved back to the memory */
      if (LoadInst *fromLoad = dyn_cast<LoadInst>(sanitizedValue)) {
        errs() << PRINT_CYAN << "+ ";
        call->getNextNode()->print(errs());
        errs() << "\n";
        std::cerr << PRINT_RESET;
      }
#endif
    } else if (auto *Switch = dyn_cast<SwitchInst>(V)) {
      Value *from = Switch->getCondition();

      FunctionType *sanitizeFuncType =
          FunctionType::get(from->getType(), {from->getType()}, false);
      std::string sanitizeType;
      if (from->getType()->isIntegerTy()) {
        sanitizeType =
            "13_" + std::to_string(from->getType()->getIntegerBitWidth());
      } else {
        sanitizeType = std::to_string(from->getType()->getTypeID());
      }
      Function *sanitizeFunc = M.getFunction("sanitizeSwitch" + sanitizeType);
      if (!sanitizeFunc) {
        sanitizeFunc =
            Function::Create(sanitizeFuncType, Function::ExternalLinkage,
                             "sanitizeSwitch" + sanitizeType, M);
      }

      CallInst *call = insertSanitizeSwitch(F, Switch, sanitizeFunc);

      referencePoint = call;
      from->replaceUsesWithIf(call, use_is_referenced);
#ifdef SANITIZE_DEBUG
      std::cerr << PRINT_CYAN << "+ ";
      call->print(errs());
      errs() << "\n";
      std::cerr << PRINT_RESET;
#endif
    }
  }
}

/*******************************************
 * Entry Function
 *******************************************/
static void createEntryFunction(Module &M) {
  LLVMContext &C = M.getContext();
  IRBuilder<> Builder(C);

  FunctionType *entryFunctionType = FunctionType::get(
      Type::getVoidTy(C), 
      {Builder.getPtrTy(), Builder.getPtrTy(), 
      Builder.getPtrTy(), Builder.getPtrTy(), 
      Builder.getPtrTy(), Builder.getPtrTy(), 
      Builder.getPtrTy(), Builder.getPtrTy()},
      false);
  Function *entryFunction = Function::Create(
      entryFunctionType, Function::ExternalLinkage, "fuzzEntryFunction", M);
  entryFunction->addFnAttr(Attribute::NoInline);
  entryFunction->addFnAttr(Attribute::OptimizeNone);

  BasicBlock *BB = BasicBlock::Create(C, "entry", entryFunction);
  Builder.SetInsertPoint(BB);

  Function *F = M.getFunction(entryFunctionName);
  if (!F) {
    report_fatal_error("Entry function not found");
  }

  std::vector<Value *> args;
  for (int i = 0; i < F->arg_size(); i++) {
    auto it = std::find(entryFunctionFuzzIndexes.begin(),
                        entryFunctionFuzzIndexes.end(), i);
    if (it != entryFunctionFuzzIndexes.end()) {
      args.push_back(entryFunction->getArg(
          std::distance(entryFunctionFuzzIndexes.begin(), it)));
    } else {
      args.push_back(Constant::getNullValue(F->getArg(i)->getType()));
    }
  }
  for (int i = 0; i < args.size(); i++) {
    if (args[i]->getType() != F->getArg(i)->getType()) {
      args[i] = Builder.CreatePointerCast(args[i], F->getArg(i)->getType());
    }
  }

  CallInst *defaultCall = Builder.CreateCall(F, args);

  Builder.CreateRetVoid();

  Builder.SetInsertPoint(defaultCall);

  /* Multi entry points for cross fuzz */
  FunctionType *crossEntryPointsType = FunctionType::get(
    Type::getInt1Ty(C),
    {Builder.getPtrTy(), Builder.getPtrTy(),
    Builder.getPtrTy(), Builder.getPtrTy(),
    Builder.getPtrTy(), Builder.getPtrTy(),
    Builder.getPtrTy(), Builder.getPtrTy()},
    false);
  Function *crossEntryPoints = M.getFunction("crossEntryPoints");
  if (!crossEntryPoints) {
    crossEntryPoints = Function::Create(
        crossEntryPointsType, Function::ExternalLinkage, "crossEntryPoints", M);
  }
  CallInst *crossCall = Builder.CreateCall(crossEntryPoints,
                        {entryFunction->getArg(0), entryFunction->getArg(1),
                        entryFunction->getArg(2), entryFunction->getArg(3),
                        entryFunction->getArg(4), entryFunction->getArg(5),
                        entryFunction->getArg(6), entryFunction->getArg(7)});
  Value *crossCallResult = Builder.CreateICmpEQ(crossCall, ConstantInt::getTrue(C));
  Instruction *ThenTerm, *ElseTerm;
  SplitBlockAndInsertIfThenElse(crossCallResult, defaultCall, &ThenTerm,
                                &ElseTerm, nullptr);

  Builder.SetInsertPoint(ThenTerm);
  Builder.CreateRetVoid();

  BasicBlock *ThenBB = ThenTerm->getParent();
  ThenBB->getTerminator()->eraseFromParent();

#ifdef ENTRY_DEBUG
  std::cerr << PRINT_GREEN;
  for (Instruction &I : *BB) {
    I.print(errs());
    errs() << "\n";
  }
  std::cerr << PRINT_RESET;
#endif
}

static void createTargetEntryFunction(Module &M) {
  std::string targetFunctionName = std::getenv("TARGET_FUNC_NAME");
  std::string targetFuncInputIndex = std::getenv("TARGET_FUNC_INPUT_INDEX");
  std::string targetFuncInputLengthIndex =
      std::getenv("TARGET_FUNC_LENGTH_INDEX");

  LLVMContext &C = M.getContext();
  IRBuilder<> Builder(C);

  Function *F = M.getFunction(targetFunctionName);
  if (!F) {
    report_fatal_error("Target Entry function not found");
  }

  /* Get replicated name */
  int functionIndex = 0;
  for (int i = 0; i < F->arg_size(); i++) {
    if (i != std::stoi(targetFuncInputIndex) &&
        i != std::stoi(targetFuncInputLengthIndex)) {
          functionIndex |= (1 << i);
        }
  }

  std::string targetReplicatedFunctionName;
  if (F->hasLocalLinkage()) {
    std::string fileName = getFileBaseName(F);
    targetReplicatedFunctionName = "replicate_" + fileName + "_" + targetFunctionName +
                         "_" + std::to_string(functionIndex);
  } else {
    targetReplicatedFunctionName = "replicate_" + targetFunctionName + "_" +
                         std::to_string(functionIndex);
  }
  Function *replicateF = M.getFunction(targetReplicatedFunctionName);
  if (!replicateF) {
    report_fatal_error("Replicated function not found");
  }

  FunctionType *entryFunctionType = FunctionType::get(
      Type::getVoidTy(C),
      {Builder.getPtrTy(), Builder.getPtrTy(), Builder.getPtrTy(),
       Builder.getPtrTy(), Builder.getPtrTy(), Builder.getPtrTy(),
       Builder.getPtrTy(), Builder.getPtrTy()},
      false);
  Function *entryFunction = Function::Create(
      entryFunctionType, Function::ExternalLinkage, "fuzzTargetEntryFunction", M);
  entryFunction->addFnAttr(Attribute::NoInline);
  entryFunction->addFnAttr(Attribute::OptimizeNone);

  BasicBlock *BB = BasicBlock::Create(C, "entry", entryFunction);
  Builder.SetInsertPoint(BB);

  std::vector<Value *> args;
  for (int i = 0; i < replicateF->arg_size(); i++) {
    args.push_back(entryFunction->getArg(i));
  }

  Builder.CreateCall(replicateF, args);

  Builder.CreateRetVoid();
#ifdef ENTRY_DEBUG
  std::cerr << PRINT_GREEN;
  for (Instruction &I : *BB) {
    I.print(errs());
    errs() << "\n";
  }
  std::cerr << PRINT_RESET;
#endif
}

/*******************************************
 * Hook Functions
 *******************************************/
static void hookAssertFunction(Module &M) {
  while (!assertFunctionList.empty()) {
    std::string assertFunctionName = assertFunctionList.back();
    assertFunctionList.pop_back();

    Function *F = M.getFunction(assertFunctionName);
    if (!F) {
      continue;
    }

#ifdef HOOK_DEBUG
    std::cerr << PRINT_MAGENTA;
    std::cerr << "++ assertHookFunction generation ++\n";
    std::cerr << PRINT_RESET;
#endif

    for (Function &Func : M) {
      for (BasicBlock &BB : Func) {
        for (Instruction &I : BB) {
          if (auto *CI = dyn_cast<CallInst>(&I)) {
            if (CI->getCalledFunction() == F) {
#ifdef HOOK_DEBUG
              std::cerr << PRINT_YELLOW;
              errs() << "- ";
              CI->print(errs());
              errs() << "\n";
              std::cerr << PRINT_RESET;
#endif
              LLVMContext &C = M.getContext();
              IRBuilder<> Builder(C);
              Builder.SetInsertPoint(CI);

              Value *Inst;
              if (Func.getReturnType() == Type::getVoidTy(M.getContext())) {
                Inst = Builder.CreateRetVoid();
              } else if (Func.getReturnType()->isIntegerTy()) {
                Inst = Builder.CreateRet(
                    ConstantInt::get(Func.getReturnType(), 0));
              } else if (Func.getReturnType()->isPointerTy()) {
                Inst = Builder.CreateRet(ConstantPointerNull::get(
                    cast<PointerType>(Func.getReturnType())));
              } else {
                Inst = Builder.CreateRet(
                    Constant::getNullValue(Func.getReturnType()));
              }

              /* Erase successive instructions */
              auto InstIter = CI->getIterator();
              std::vector<Instruction *> InstList;
              while (InstIter != BB.end()) {
                InstList.push_back(&*InstIter);
                InstIter++;
              }

              for (Instruction *Inst : InstList) {
                Inst->eraseFromParent();
              }

#ifdef HOOK_DEBUG
              std::cerr << PRINT_CYAN;
              errs() << "+ ";
              Inst->print(errs());
              errs() << "\n";
              std::cerr << PRINT_RESET;
#endif
              break;
            }
          }
        }
      }
    }
  }
}

static void hookIgnoreFunction(Module &M) {
  while (!ignoreFunctionList.empty()) {
    std::string ignoreFunctionName = ignoreFunctionList.back();
    ignoreFunctionList.pop_back();

    Function *F = M.getFunction(ignoreFunctionName);
    if (!F) {
      continue;
    }

    FunctionType *hookFunctionType = FunctionType::get(
        F->getReturnType(), F->getFunctionType()->params(), false);

    std::string functionType;
    if (F->getReturnType()->isIntegerTy()) {
      functionType =
          "13_" + std::to_string(F->getReturnType()->getIntegerBitWidth());
    } else {
      functionType = std::to_string(F->getReturnType()->getTypeID());
    }
    Function *hookFunction = M.getFunction("ignoreHook" + functionType);
    if (!hookFunction) {
      hookFunction =
          Function::Create(hookFunctionType, Function::ExternalLinkage,
                           "ignoreHook" + functionType, M);
    }

#ifdef HOOK_DEBUG
    std::cerr << PRINT_MAGENTA;
    std::cerr << "++ ignoreHookFunction generation ++\n";
    std::cerr << PRINT_RESET;
#endif

    for (auto &Func : M) {
      for (auto &BB : Func) {
        for (auto &I : BB) {
          if (auto *CI = dyn_cast<CallInst>(&I)) {
            if (CI->getCalledFunction() == F) {
#ifdef HOOK_DEBUG
              std::cerr << PRINT_YELLOW;
              errs() << "- ";
              CI->print(errs());
              errs() << "\n";
              std::cerr << PRINT_RESET;
#endif
              CI->setCalledFunction(hookFunction);
#ifdef HOOK_DEBUG
              std::cerr << PRINT_CYAN;
              errs() << "+ ";
              CI->print(errs());
              errs() << "\n";
              std::cerr << PRINT_RESET;
#endif
            }
          }
        }
      }
    }
  }
}

static void hookFunctions(Module &M) {

#ifdef FREE_HOOK
  hookFunctionList.push_back("free");
#endif

#ifdef MALLOC_HOOK
  hookFunctionList.push_back("malloc");
#endif

#ifdef ASSERT_HOOK
  hookAssertFunction(M);
#endif

#ifdef IGNORE_HOOK
  hookIgnoreFunction(M);
#endif

#ifdef MEMCPY_HOOK
  hookFunctionList.push_back("memcpy");
  hookFunctionList.push_back("llvm.memcpy.p0.p0.i32");
  hookFunctionList.push_back("llvm.memcpy.p0.p0.i64");
  hookFunctionList.push_back("memset");
#endif

  while (!hookFunctionList.empty()) {
    std::string hookFunctionName = hookFunctionList.back();
    hookFunctionList.pop_back();

    Function *F = M.getFunction(hookFunctionName);
    if (!F) {
      continue;
    }
    /* Print current file name */
    std::cerr << PRINT_YELLOW;
    if (F->getSubprogram()) {
      errs() << "Current file name: " << F->getSubprogram()->getFilename() << "\n";
    } else {
      errs() << "Current file name: Unknown\n";
    }
    // errs() << "Current file name: " << M.getSourceFileName() << "\n";
    errs() << "Find hook function: " << hookFunctionName << "\n";
    F->print(errs());
    errs() << "\n";
    errs() << "----------------------------------------\n";
    errs() << "\n";
    errs() << PRINT_RESET;

    /* LLVM functions */
    if (hookFunctionName.find("llvm.memcpy") != std::string::npos) {
      hookFunctionName = "memcpy";
    }

#ifdef HOOK_DEBUG
    std::cerr << PRINT_MAGENTA;
    std::cerr << "++ HookFunction generation ++\n";
    std::cerr << PRINT_RESET;
#endif

    Function *hookFunction = M.getFunction("hook_" + hookFunctionName);
    if (!hookFunction) {
      FunctionType *hookFunctionType = FunctionType::get(
          F->getReturnType(), F->getFunctionType()->params(), false);
      hookFunction =
          Function::Create(hookFunctionType, Function::ExternalLinkage,
                           "hook_" + hookFunctionName, M);
    }

    /* Change every use sites */
    for (auto &everyF : M) {
      for (auto &BB : everyF) {
        for (auto &I : BB) {
          if (auto *CI = dyn_cast<CallInst>(&I)) {
            if (CI->getCalledFunction() == F) {
              if (CI->getParent()->getParent() == hookFunction) {
                continue;
              }
              if ((CI->getParent()->getParent()->getName() == "clearNeverFree") ||
                  (CI->getParent()->getParent()->getName() == "clearPlayGround")) {
                continue;
              }
              #ifdef HOOK_DEBUG
                        std::cerr << PRINT_YELLOW;
                        errs() << "- ";
                        CI->print(errs());
                        errs() << "\n";
                        std::cerr << PRINT_RESET;
              #endif
                        CI->setCalledFunction(hookFunction);
              #ifdef HOOK_DEBUG
                        std::cerr << PRINT_CYAN;
                        errs() << "+ ";
                        CI->print(errs());
                        errs() << "\n";
                        std::cerr << PRINT_RESET;
              #endif
            }
          }
        }
      }
    }
  }
}

/*******************************************
 * Passes
 *******************************************/
struct GlobalSanitizer : public PassInfoMixin<GlobalSanitizer> {

  GlobalSanitizer() {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
    for (GlobalVariable &G : M.globals()) {
      /* Constant global variable is out of the interest */
      if (G.isConstant())
        continue;

      /* Taint value itself */
      addTaintValue(nullptr, &G);

      /* Taint every use sites */
      for (auto *U : G.users()) {
        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(U)) {
          for (auto *CEU : CE->users()) {
            if (LoadInst *CEUL = dyn_cast<LoadInst>(CEU)) {
              Value *from = CEUL->getPointerOperand();
              if (from->getType()->isPointerTy()) {
                addTaintValue(nullptr, from);
              }
            } else if (StoreInst *CEUS = dyn_cast<StoreInst>(CEU)) {
              Value *to = CEUS->getPointerOperand();
              if (to->getType()->isPointerTy()) {
                addTaintValue(nullptr, to);
              }
            }
          }
        }
      }
    }

    return PreservedAnalyses::all();
  }
};

struct FunctionTaintFlow : public PassInfoMixin<FunctionTaintFlow> {

  FunctionTaintFlow() {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
    if (!readConfiguration(M)) {
      /* Early return */
      report_fatal_error("taint.conf not found or invalid format");
    }

    for (Function &F : M) {
      if (std::find(GlobalScopeList.begin(), GlobalScopeList.end(),
                    F.getName().str()) == GlobalScopeList.end()) {
        // We do not need to do as we use other coverage metrics
        // F.addFnAttr(Attribute::NoSanitizeCoverage);
      }
    }

    while (!taintFunctions.empty()) {
      Function *F = std::get<0>(taintFunctions.back());
      /* If taintIndices info is in taintFunctionsList, we take it */
      std::vector<int> *taintIndices;
      if (isInTaintFunctionsList(F)) {
        taintIndices = getTaintIndices(F);
      } else {
        taintIndices = std::get<1>(taintFunctions.back());
      }
      taintFunctions.pop_back();

#ifdef TAINT_DEBUG
      std::cerr << PRINT_BLUE;
      std::cerr << "++ Function " << F->getName().str()
                << " taint analysis ++\n";
      std::cerr << PRINT_RESET;
#endif

      singleFunctionTaintFlow(M, *F, taintIndices);
    }

    return PreservedAnalyses::all();
  }
};

struct Sanitizer : public PassInfoMixin<Sanitizer> {

  Sanitizer() {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
    /* taintFunctionList iterate */
    for (auto &pair : taintFunctionsList) {
      Function *F = pair.first;
      Value *callInst = std::get<0>(pair.second);
      std::vector<int> *taintIndices = std::get<1>(pair.second);

#ifdef SANITIZE_DEBUG
      std::cerr << PRINT_MAGENTA;
      std::cerr << "++ Function " << F->getName().str() << " sanitization ++\n";
      std::cerr << PRINT_RESET;
#endif
      registerSanitizeScope(M, *F);

      singleFunctionSanitize(M, *F, callInst);
    }
    return PreservedAnalyses::all();
  }
};

struct EntryFunctionGenerator : public PassInfoMixin<EntryFunctionGenerator> {

  EntryFunctionGenerator() {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
    Function *F = M.getFunction(entryFunctionName);
    if (F && !F->isDeclaration() && isInGlobalFileScopeList(M.getSourceFileName())) {
#ifdef ENTRY_DEBUG
      std::cerr << PRINT_RED;
      std::cerr << "++ entryFunction generation ++\n";
      std::cerr << PRINT_RESET;
#endif

      createEntryFunction(M);
    }
    return PreservedAnalyses::all();
  }
};

struct TargetEntryFunctionGenerator : public PassInfoMixin<TargetEntryFunctionGenerator> {

  TargetEntryFunctionGenerator() {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
    /* TARGET_FUNCTION_NAME is set at build time */
    if (!std::getenv("TARGET_FUNC_NAME")) {
      report_fatal_error("TARGET_FUNC_NAME is not set");
    }
    std::string targetEntryFunctionName = std::getenv("TARGET_FUNC_NAME");
    Function *F = M.getFunction(targetEntryFunctionName);
    if (F && !F->isDeclaration() && isInGlobalFileScopeList(M.getSourceFileName())) {
#ifdef ENTRY_DEBUG
      std::cerr << PRINT_RED;
      std::cerr << "++ targetEntryFunction generation ++\n";
      std::cerr << PRINT_RESET;
#endif

      createTargetEntryFunction(M);
    }
    return PreservedAnalyses::all();
  }
};

struct FunctionHook : public PassInfoMixin<FunctionHook> {

  FunctionHook() {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {

    hookFunctions(M);

    return PreservedAnalyses::all();
  }
};

struct ConfigReader : public PassInfoMixin<ConfigReader> {

  ConfigReader() {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
    if (!readConfiguration(M)) {
      /* Early return */
      report_fatal_error("taint configuration is not found or invalid format");
    }

    return PreservedAnalyses::all();
  }
};

struct FunctionReplicator : public PassInfoMixin<FunctionReplicator> {

  FunctionReplicator() {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {

    /* Get file name */
    std::string fileName = M.getSourceFileName();
    if (isInGlobalFileScopeList(fileName)) {
      // We will replicate functions on-demand during taint analysis
      // replicateFunctions(M);
    }

    return PreservedAnalyses::all();
  }
};

struct MainRemover : public PassInfoMixin<MainRemover> {

  MainRemover() {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {

    /* Get file name */
    Function *mainF = M.getFunction("main");
    if (mainF && !mainF->isDeclaration()) {
      if (mainF->getSubprogram() && mainF->getSubprogram()->getFilename() != "") {
        std::string mainFileName = mainF->getSubprogram()->getFilename().str();
        errs() << PRINT_YELLOW;
        errs() << "[*] Removing 'main' function in file name: " << mainFileName << "\n";
        errs() << PRINT_RESET;

        /* Change main function name to old_main */
        // mainF->setName("old_main");

        /* Change main function linkage as weak */
        mainF->setLinkage(GlobalValue::WeakAnyLinkage);
      }
    }

    return PreservedAnalyses::all();
  }
};

} // namespace

PassPluginLibraryInfo getPassPluginInfo() {
  const auto callback = [](PassBuilder &PB) {
    // REGISTRATION FOR "-O{1|2|3|s}"
    PB.registerOptimizerLastEPCallback(
        [](ModulePassManager &MPM, OptimizationLevel level) {
          MPM.addPass(ConfigReader());
          MPM.addPass(GlobalSanitizer());
          MPM.addPass(FunctionHook());
          MPM.addPass(FunctionReplicator());
          MPM.addPass(FunctionTaintFlow());
          MPM.addPass(Sanitizer());
          MPM.addPass(EntryFunctionGenerator());
          MPM.addPass(MainRemover());
        });
  };

  return {LLVM_PLUGIN_API_VERSION, "name", "0.0.1", callback};
};

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return getPassPluginInfo();
}