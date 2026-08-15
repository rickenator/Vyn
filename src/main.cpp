#include "vyb/vyb.hpp"
#include "vyb/parser/lexer.hpp"   // For Lexer
#include "vyb/parser/parser.hpp"  // For vyb::Parser
#include "vyb/semantic.hpp"       // For vyb::SemanticAnalyzer
#include "vyb/module_registry.hpp"
#include "vyb/vre/llvm/codegen.hpp" // For vyb::LLVMCodegen
#include "vyb/bindgen.hpp"      // For vyb::bindgen::generateBindings
#include <catch2/catch_session.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <set> // For test and parser verbosity specifiers
#include <filesystem>
#include <unordered_set>
#include <unordered_map>
#include <algorithm> // For std::find
#include <cctype>
#include <optional>
#include <utility>
#include <cstdio> // For printf and fflush
#include <cstdlib> // For malloc/free
#include <cstring> // For memset

// Declare the intrinsic functions from intrinsics.cpp
extern "C" {
    // This is just a declaration - implementation is in intrinsics.cpp
    void __vyb_println(const char* str);
    void __vyb_print(const char* str);
    void __vyb_println_int(int64_t val);
    void __vyb_print_int(int64_t val);
    void __vyb_println_bool(int64_t val);
    void __vyb_print_bool(int64_t val);
    char* __vyb_serialize_to_json(void* obj, const char* type_name);
    char* __vyb_convert_lit_string(const char* str);
    char* __vyb_string_concat(const char* left, const char* right);

    // String concatenation intrinsic function
    char* __vyb_string_concat(const char* left, const char* right);

    // String replace runtime helper
    char* __vyb_string_replace(const char* src, int64_t src_len,
                               const char* old_s, const char* new_s,
                               int64_t* out_len);

    // Closure capture-environment reference counting helpers
    void* __vyb_closure_retain(void* env);
    void  __vyb_closure_release(void* env);

    // ToString intrinsic functions for automatic string conversion - all basic types
    char* __vyb_toString_int(int64_t value);
    char* __vyb_toString_int8(int8_t value);
    char* __vyb_toString_int16(int16_t value);
    char* __vyb_toString_int32(int32_t value);
    char* __vyb_toString_int64(int64_t value);
    char* __vyb_toString_uint8(uint8_t value);
    char* __vyb_toString_uint16(uint16_t value);
    char* __vyb_toString_uint32(uint32_t value);
    char* __vyb_toString_uint64(uint64_t value);
    char* __vyb_toString_float(double value);
    char* __vyb_toString_float32(float value);
    char* __vyb_toString_bool(bool value);
    char* __vyb_toString_string(const char* value);
    char* __vyb_toString_char(uint8_t value);
    char* __vyb_toString_rune(uint32_t value);
    char* __vyb_toString_byte(uint8_t value);

    // New type conversion functions (primitive to_string/from_string)
    char* __vyb_int_to_string(int64_t value);
    char* __vyb_float_to_string(double value);
    char* __vyb_bool_to_string(bool value);
    char* __vyb_string_to_string(const char* str);
    void __vyb_string_register(void* ptr);
    void __vyb_string_free(void* ptr);

    int64_t __vyb_int_from_string(const char* str, bool* success);
    double __vyb_float_from_string(const char* str, bool* success);
    bool __vyb_bool_from_string(const char* str, bool* success);
    char* __vyb_string_from_string(const char* str, bool* success);

    // JSON serialization for complex types
    char* __vyb_complex_to_json(void* instance, const char* type_name);
    void* __vyb_complex_from_json(const char* json_str, const char* type_name);

    // Type metadata registration
    void __vyb_register_type(void* metadata);

    // Type identity registry (id -> name)
    void __vyb_register_typename(uint64_t type_id, const char* type_name);
    const char* __vyb_get_typename(uint64_t type_id);

    // Error handling runtime functions (from error_handling.cpp)
    void __vyb_runtime_panic(const char* message) __attribute__((noreturn));
    void __vyb_runtime_untrapped_error(void* error) __attribute__((noreturn));
    void* __vyb_runtime_create_error_ex(const char* type_name, void* type_id, void* data, uint64_t data_size, void (*destructor)(void*), const char* file, uint32_t line, uint32_t column);
    void __vyb_runtime_free_error(void* error);

    // Stack trace runtime functions (Phase 6.4 - from error_handling.cpp)
    void __vyb_runtime_push_call_frame(const char* function_name, const char* file_path, uint32_t line, uint32_t column);
    void __vyb_runtime_pop_call_frame();
    void* __vyb_runtime_get_current_stack_trace();  // Returns VybStackTrace*

    // TODO: Future toString functions for compound types:
    // char* __vyb_toString_vec(void* vec_ptr, const char* element_type);
    // char* __vyb_toString_tuple(void* tuple_ptr, const char* type_spec);
}

// LLVM includes for ORC JIT compilation
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/Error.h>

// LLVM includes for object file emission
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>

// LLVM includes for IR optimization passes (new pass manager)
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/StandardInstrumentations.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/Scalar/Reassociate.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>

// System includes for linking
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>  // For chmod

// Globals for test verbose control
std::set<std::string> g_verbose_test_specifiers;
bool g_make_all_tests_verbose = false;
bool g_suppress_all_debug_output = false;

// Globals for parser verbose control
namespace vyb {
    std::set<std::string> g_verbose_parser_test_specifiers;
    bool g_make_all_parser_verbose = false;
    bool g_suppress_all_parser_debug_output = false;
    // Codegen debug output: off by default; enable with --debug-codegen
    bool g_debug_codegen = false;
}

// Concrete implementation of SemanticAnalyzer


// Function to optimize LLVM IR module based on optimization level
void optimize_module(llvm::Module* module, llvm::TargetMachine* targetMachine, int optLevel) {
    if (optLevel == 0) {
        if (vyb::g_debug_codegen) std::cout << "Skipping IR optimization (-O0)" << std::endl;
        return;  // No optimization at -O0
    }

    if (vyb::g_debug_codegen) std::cout << "Applying IR optimization passes (-O" << optLevel << ")..." << std::endl;

    // Create analysis managers
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    // Create pass builder
    llvm::PassBuilder PB(targetMachine);

    // Register all analysis passes
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    // Create module pass manager based on optimization level
    llvm::ModulePassManager MPM;

    switch (optLevel) {
        case 1: {
            if (vyb::g_debug_codegen) std::cout << "  Using O1 optimization pipeline (basic)" << std::endl;
            MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O1);
            break;
        }
        case 2: {
            if (vyb::g_debug_codegen) std::cout << "  Using O2 optimization pipeline (default)" << std::endl;
            MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
            break;
        }
        case 3: {
            if (vyb::g_debug_codegen) std::cout << "  Using O3 optimization pipeline (aggressive)" << std::endl;
            MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O3);
            break;
        }
        default: {
            if (vyb::g_debug_codegen) std::cout << "  Using O2 optimization pipeline (default)" << std::endl;
            MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
            break;
        }
    }

    // Run the optimization pipeline
    MPM.run(*module, MAM);
    if (vyb::g_debug_codegen) std::cout << "  IR optimization completed" << std::endl;
}

namespace {
namespace fs = std::filesystem;

struct ModuleParseOptions {
    std::vector<fs::path> cliModulePaths;
    fs::path executablePath;
    bool skipImportResolution = false;
};

ModuleParseOptions g_module_parse_options;

std::unique_ptr<vyb::ast::Module> parse_vyb_module(const std::string& source, const std::string& fileName) {
    vyb::ModuleRegistryOptions options;
    options.cliModulePaths = g_module_parse_options.cliModulePaths;
    options.executablePath = g_module_parse_options.executablePath;
    options.skipImportResolution = g_module_parse_options.skipImportResolution;
    vyb::ModuleRegistry registry(std::move(options));
    return registry.resolveRoot(source, fileName);
}
} // namespace



// Function to compile Vyb code to object file
int compile_vyb_to_object(const std::string& source, const std::string& fileName,
                          const std::string& outputFile, int optLevel = 2) {
    std::cout << "Compiling " << fileName << " to object file..." << std::endl;

    // Initialize LLVM targets
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    try {
        std::cout << "Creating driver instance..." << std::endl;
        vyb::Driver driver;

        std::cout << "Parsing source and resolving imports..." << std::endl;
        auto ast = parse_vyb_module(source, fileName);
        std::cout << "AST created successfully" << std::endl;

        std::cout << "Running semantic analysis..." << std::endl;
        vyb::SemanticAnalyzer semanticAnalyzer(driver);
        driver.setSemanticAnalyzer(&semanticAnalyzer);
        semanticAnalyzer.analyze(ast.get());

        const auto& semanticErrors = semanticAnalyzer.getErrors();
        if (!semanticErrors.empty()) {
            std::cerr << "\nSemantic Errors:" << std::endl;
            for (const auto& error : semanticErrors) {
                std::cerr << "  " << error << std::endl;
            }
            throw std::runtime_error("Semantic analysis failed with " +
                std::to_string(semanticErrors.size()) + " error(s)");
        }
        std::cout << "Semantic analysis completed" << std::endl;

        std::cout << "Generating LLVM IR code..." << std::endl;
        vyb::LLVMCodegen codegen(driver);
        codegen.generate(ast.get(), fileName + ".ll");
        std::cout << "LLVM IR generation completed" << std::endl;

        // Get the LLVM module
        llvm::Module* module = codegen.getModule();

        // Verify the module
        std::cout << "Verifying module..." << std::endl;
        std::string verifyErrors;
        llvm::raw_string_ostream verifyStream(verifyErrors);
        if (llvm::verifyModule(*module, &verifyStream)) {
            verifyStream.flush();
            std::cerr << "Module verification failed:\n" << verifyErrors << std::endl;
            throw std::runtime_error("Module verification failed: " + verifyErrors);
        }
        std::cout << "Module verified successfully" << std::endl;

        // Setup target machine
        auto targetTriple = llvm::sys::getDefaultTargetTriple();
        std::cout << "Target triple: " << targetTriple << std::endl;
        module->setTargetTriple(targetTriple);

        std::string error;
        auto target = llvm::TargetRegistry::lookupTarget(targetTriple, error);
        if (!target) {
            throw std::runtime_error("Failed to lookup target: " + error);
        }

        auto CPU = "generic";
        auto features = "";

        llvm::TargetOptions opt;
        auto relocModel = std::optional<llvm::Reloc::Model>(llvm::Reloc::PIC_);
        auto targetMachine = target->createTargetMachine(targetTriple, CPU, features,
                                                         opt, relocModel);

        module->setDataLayout(targetMachine->createDataLayout());

        // Apply IR optimization passes before code generation
        optimize_module(module, targetMachine, optLevel);

        // Open output file
        std::error_code EC;
        llvm::raw_fd_ostream dest(outputFile, EC, llvm::sys::fs::OF_None);
        if (EC) {
            throw std::runtime_error("Could not open file: " + EC.message());
        }

        // Set optimization level
        llvm::CodeGenOptLevel cgOptLevel;
        switch (optLevel) {
            case 0: cgOptLevel = llvm::CodeGenOptLevel::None; break;
            case 1: cgOptLevel = llvm::CodeGenOptLevel::Less; break;
            case 3: cgOptLevel = llvm::CodeGenOptLevel::Aggressive; break;
            default: cgOptLevel = llvm::CodeGenOptLevel::Default; break;
        }

        // Emit object file
        llvm::legacy::PassManager pass;
        auto fileType = llvm::CodeGenFileType::ObjectFile;

        if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, fileType)) {
            throw std::runtime_error("TargetMachine can't emit a file of this type");
        }

        std::cout << "Emitting object file with optimization level -O" << optLevel << "..." << std::endl;
        pass.run(*module);
        dest.flush();

        std::cout << "Successfully compiled to: " << outputFile << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error compiling to object file: " << e.what() << std::endl;
        return 1;
    }
}

// Function to link object files into executable
int link_vyb_executable(const std::vector<std::string>& objectFiles,
                        const std::string& outputExecutable,
                        const std::vector<std::string>& userLinkArgs = {},
                        bool staticLink = false) {
    std::cout << "Linking executable: " << outputExecutable << std::endl;

    // First, compile the runtime support files if needed.
    std::vector<std::pair<std::string, std::string>> runtimeUnits = {
        {"runtime/vyb_runtime.c", "runtime/vyb_runtime.o"},
        {"runtime/vyb_type_metadata.c", "runtime/vyb_type_metadata.o"}
    };
    std::vector<std::string> runtimeObjects;

    for (const auto& unit : runtimeUnits) {
        const std::string& runtimeSource = unit.first;
        const std::string& runtimeObject = unit.second;
        if (access(runtimeSource.c_str(), F_OK) == 0) {
            std::cout << "Compiling Vyb runtime library..." << std::endl;

            std::string compileCmd = "cc -c " + runtimeSource + " -o " + runtimeObject + " -O2";
            int compileResult = system(compileCmd.c_str());
            if (compileResult != 0) {
                std::cerr << "Failed to compile runtime library" << std::endl;
                return 1;
            }
            runtimeObjects.push_back(runtimeObject);
        }
    }

    // Detect platform and choose linker
    std::string linker = "ld";
    std::vector<std::string> linkerArgs;

#ifdef __APPLE__
    // macOS uses different linker and flags
    linker = "ld";
    linkerArgs.push_back("-macosx_version_min");
    linkerArgs.push_back("10.15");
    linkerArgs.push_back("-arch");
    linkerArgs.push_back("x86_64");
    // macOS dynamic linker
    linkerArgs.push_back("-dynamic");
    linkerArgs.push_back("-dylib");
    // System library search paths
    linkerArgs.push_back("-L/usr/lib");
    linkerArgs.push_back("-L/usr/local/lib");
#else
    // Linux - try lld first (faster), fallback to ld
    if (system("which lld >/dev/null 2>&1") == 0) {
        linker = "lld";
    } else if (system("which ld.lld >/dev/null 2>&1") == 0) {
        linker = "ld.lld";
    } else {
        linker = "ld";
    }

    // Dynamic linker path for Linux
    linkerArgs.push_back("-dynamic-linker");
    linkerArgs.push_back("/lib64/ld-linux-x86-64.so.2");
#endif

    // Output file
    linkerArgs.push_back("-o");
    linkerArgs.push_back(outputExecutable);

    // Add CRT startup files for proper C runtime initialization
#ifdef __APPLE__
    // macOS doesn't need crt files explicitly in modern versions
#else
    // Linux needs crt files
    std::vector<std::string> crtPaths = {
        "/usr/lib/x86_64-linux-gnu/",
        "/usr/lib64/",
        "/usr/lib/",
        "/lib/x86_64-linux-gnu/",
        "/lib64/",
        "/lib/"
    };

    auto findCrtFile = [&](const std::string& filename) -> std::string {
        for (const auto& path : crtPaths) {
            std::string fullPath = path + filename;
            if (access(fullPath.c_str(), F_OK) == 0) {
                return fullPath;
            }
        }
        return "";
    };

    std::string crt1 = findCrtFile("crt1.o");
    std::string crti = findCrtFile("crti.o");
    std::string crtn = findCrtFile("crtn.o");

    if (!crt1.empty()) linkerArgs.push_back(crt1);
    if (!crti.empty()) linkerArgs.push_back(crti);
#endif

    // Add all object files
    for (const auto& objFile : objectFiles) {
        linkerArgs.push_back(objFile);
    }

    // Add runtime libraries if they were compiled
    for (const auto& runtimeObject : runtimeObjects) {
        if (access(runtimeObject.c_str(), F_OK) == 0) {
            linkerArgs.push_back(runtimeObject);
        }
    }

    // Add C runtime library paths
#ifndef __APPLE__
    linkerArgs.push_back("-L/usr/lib/x86_64-linux-gnu");
    linkerArgs.push_back("-L/usr/lib64");
    linkerArgs.push_back("-L/usr/lib");
#endif

    auto normalizeLinkArg = [](const std::string& linkArg) -> std::string {
        auto endsWith = [](const std::string& value, const std::string& suffix) -> bool {
            return value.size() >= suffix.size() &&
                   value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
        };

        if (linkArg.empty() || linkArg[0] == '-' || linkArg.find('/') != std::string::npos ||
            endsWith(linkArg, ".a") || endsWith(linkArg, ".so") ||
            endsWith(linkArg, ".dylib") || endsWith(linkArg, ".o")) {
            return linkArg;
        }
        return "-l" + linkArg;
    };

    for (const auto& linkArg : userLinkArgs) {
        linkerArgs.push_back(normalizeLinkArg(linkArg));
    }

    // Link against C standard library and math library
    if (staticLink) {
        linkerArgs.push_back("-static");
        linkerArgs.push_back("-lc");
        linkerArgs.push_back("-lm");
    } else {
        linkerArgs.push_back("-lc");
        linkerArgs.push_back("-lm");
    }

    // Add crtn at the end (Linux)
#ifdef __APPLE__
    // macOS doesn't need this
#else
    if (!crtn.empty()) linkerArgs.push_back(crtn);
#endif

    // Build command for display
    std::string command = linker;
    for (const auto& arg : linkerArgs) {
        command += " " + arg;
    }
    std::cout << "Linker command: " << command << std::endl;

    // Execute linker using fork/exec for better control
    pid_t pid = fork();
    if (pid == -1) {
        std::cerr << "Failed to fork process" << std::endl;
        return 1;
    }

    if (pid == 0) {
        // Child process - execute linker
        std::vector<char*> args;
        args.push_back(const_cast<char*>(linker.c_str()));
        for (const auto& arg : linkerArgs) {
            args.push_back(const_cast<char*>(arg.c_str()));
        }
        args.push_back(nullptr);

        execvp(linker.c_str(), args.data());

        // If execvp returns, it failed
        std::cerr << "Failed to execute linker: " << linker << std::endl;
        exit(1);
    } else {
        // Parent process - wait for linker to complete
        int status;
        waitpid(pid, &status, 0);

        if (WIFEXITED(status)) {
            int exitCode = WEXITSTATUS(status);
            if (exitCode == 0) {
                std::cout << "Successfully linked executable: " << outputExecutable << std::endl;

                // Make executable
                chmod(outputExecutable.c_str(), 0755);

                return 0;
            } else {
                std::cerr << "Linker failed with exit code: " << exitCode << std::endl;
                return exitCode;
            }
        } else {
            std::cerr << "Linker process terminated abnormally" << std::endl;
            return 1;
        }
    }
}

// Function to execute Vyb code using LLVM JIT
int run_vyb_code(const std::string& source, const std::string& fileName, bool generateLLVMIR) {
    VYB_CDBG << "Starting run_vyb_code for file: " << fileName << std::endl;

    // Initialize LLVM targets for JIT
    VYB_CDBG << "Initializing LLVM targets..." << std::endl;
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    // Setup for ORC JIT execution
    VYB_CDBG << "Setting up ORC JIT options..." << std::endl;
    VYB_CDBG << "LLVM components ready for ORC JIT." << std::endl;

    try {
        VYB_CDBG << "Creating driver instance..." << std::endl;
        vyb::Driver driver;

        VYB_CDBG << "Parsing source and resolving imports..." << std::endl;
        auto ast = parse_vyb_module(source, fileName);
        VYB_CDBG << "AST created successfully" << std::endl;

        VYB_CDBG << "Running semantic analysis..." << std::endl;
        vyb::SemanticAnalyzer semanticAnalyzer(driver);
        driver.setSemanticAnalyzer(&semanticAnalyzer);  // Make semantic data available to codegen
        semanticAnalyzer.analyze(ast.get());

        // Check for semantic errors and fail if any exist
        const auto& semanticErrors = semanticAnalyzer.getErrors();
        if (!semanticErrors.empty()) {
            std::cerr << "\nSemantic Errors:" << std::endl;
            for (const auto& error : semanticErrors) {
                std::cerr << "  " << error << std::endl;
            }
            throw std::runtime_error("Semantic analysis failed with " +
                std::to_string(semanticErrors.size()) + " error(s)");
        }
        VYB_CDBG << "Semantic analysis completed" << std::endl;

        VYB_CDBG << "Generating LLVM IR code..." << std::endl;
        vyb::LLVMCodegen codegen(driver);
        codegen.generate(ast.get(), fileName + ".ll");
        VYB_CDBG << "LLVM IR generation completed" << std::endl;

        if (generateLLVMIR) {
            // Generate LLVM IR to a file if requested
            std::string irFilename = fileName + ".ll";
            std::error_code EC;
            llvm::raw_fd_ostream irFile(irFilename, EC);
            if (EC) {
                throw std::runtime_error("Failed to open file for IR output: " + EC.message());
            }
            codegen.getModule()->print(irFile, nullptr);
            irFile.flush();
            std::cout << "Generated LLVM IR to " << irFilename << std::endl;
        }

        // Get the LLVM module and context from the code generator
        std::unique_ptr<llvm::Module> module = codegen.releaseModule();
        std::unique_ptr<llvm::LLVMContext> context = codegen.releaseContext();

        VYB_CDBG << "Setting up execution engine..." << std::endl;

        // Retrieve intrinsic functions before moving module into JIT
        llvm::Function* printlnFunc = module->getFunction("__vyb_println");
        llvm::Function* serializeFunc = module->getFunction("__vyb_serialize_to_json");
        llvm::Function* litConvertFunc = module->getFunction("__vyb_convert_lit_string");
        llvm::Function* stringConcatFunc = module->getFunction("__vyb_string_concat");

        // Retrieve toString functions
        llvm::Function* toStringIntFunc = module->getFunction("__vyb_toString_int");
        llvm::Function* toStringInt8Func = module->getFunction("__vyb_toString_int8");
        llvm::Function* toStringInt16Func = module->getFunction("__vyb_toString_int16");
        llvm::Function* toStringInt32Func = module->getFunction("__vyb_toString_int32");
        llvm::Function* toStringInt64Func = module->getFunction("__vyb_toString_int64");
        llvm::Function* toStringUInt8Func = module->getFunction("__vyb_toString_uint8");
        llvm::Function* toStringUInt16Func = module->getFunction("__vyb_toString_uint16");
        llvm::Function* toStringUInt32Func = module->getFunction("__vyb_toString_uint32");
        llvm::Function* toStringUInt64Func = module->getFunction("__vyb_toString_uint64");
        llvm::Function* toStringFloatFunc = module->getFunction("__vyb_toString_float");
        llvm::Function* toStringFloat32Func = module->getFunction("__vyb_toString_float32");
        llvm::Function* toStringBoolFunc = module->getFunction("__vyb_toString_bool");
        llvm::Function* toStringStringFunc = module->getFunction("__vyb_toString_string");
        llvm::Function* toStringCharFunc = module->getFunction("__vyb_toString_char");
        llvm::Function* toStringRuneFunc = module->getFunction("__vyb_toString_rune");
        llvm::Function* toStringByteFunc = module->getFunction("__vyb_toString_byte");

        if (!printlnFunc || !serializeFunc) {
            throw std::runtime_error("Missing required intrinsic functions in module");
        }

        // Verify the module before JIT compilation
        VYB_CDBG << "Verifying module..." << std::endl;
        std::string verifyErrors;
        llvm::raw_string_ostream verifyStream(verifyErrors);
        if (llvm::verifyModule(*module, &verifyStream)) {
            verifyStream.flush();
            std::cerr << "Module verification failed:\n" << verifyErrors << std::endl;
            throw std::runtime_error("Module verification failed: " + verifyErrors);
        }
        VYB_CDBG << "Module verified successfully" << std::endl;

        // Create the ORC JIT execution engine
        auto jitOrErr = llvm::orc::LLJITBuilder().create();
        if (!jitOrErr) {
            std::string errorMsg;
            llvm::raw_string_ostream stream(errorMsg);
            stream << jitOrErr.takeError();
            throw std::runtime_error("Failed to create LLJIT: " + errorMsg);
        }
        auto& jit = *jitOrErr;

        // Register runtime functions with the JIT before adding module
        auto& mainDylib = jit->getMainJITDylib();
        llvm::orc::MangleAndInterner mangle(jit->getExecutionSession(), jit->getDataLayout());

        auto processSymbols = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
            jit->getDataLayout().getGlobalPrefix());
        if (!processSymbols) {
            std::string errorMsg;
            llvm::raw_string_ostream stream(errorMsg);
            stream << processSymbols.takeError();
            throw std::runtime_error("Failed to expose process symbols to JIT: " + errorMsg);
        }
        mainDylib.addGenerator(std::move(*processSymbols));

        // Define symbol mappings for our runtime functions
        llvm::orc::SymbolMap runtimeSymbols;
        runtimeSymbols[mangle("__vyb_println")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_println), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_print")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_print), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_println_int")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_println_int), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_print_int")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_print_int), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_println_bool")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_println_bool), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_print_bool")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_print_bool), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_serialize_to_json")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_serialize_to_json), llvm::JITSymbolFlags::Exported);

        // Register error handling runtime functions
        runtimeSymbols[mangle("__vyb_runtime_panic")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr((void*)&__vyb_runtime_panic), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_runtime_untrapped_error")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr((void*)&__vyb_runtime_untrapped_error), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_runtime_create_error_ex")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr((void*)&__vyb_runtime_create_error_ex), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_runtime_free_error")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr((void*)&__vyb_runtime_free_error), llvm::JITSymbolFlags::Exported);

        // Register stack trace runtime functions (Phase 6.4)
        runtimeSymbols[mangle("__vyb_runtime_push_call_frame")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr((void*)&__vyb_runtime_push_call_frame), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_runtime_pop_call_frame")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr((void*)&__vyb_runtime_pop_call_frame), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_runtime_get_current_stack_trace")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr((void*)&__vyb_runtime_get_current_stack_trace), llvm::JITSymbolFlags::Exported);

        // Register standard library functions
        // Register malloc/free/memset/memcpy variants with numeric suffixes
        // LLVM may create renamed variants (malloc.1, malloc.2, etc.) when the same
        // function type is declared multiple times in the module.
        {
            auto mallocPtr = llvm::orc::ExecutorAddr::fromPtr((void*)&malloc);
            auto freePtr = llvm::orc::ExecutorAddr::fromPtr((void*)&free);
            auto memsetPtr = llvm::orc::ExecutorAddr::fromPtr((void*)&memset);
            auto memcpyPtr = llvm::orc::ExecutorAddr::fromPtr((void*)&memcpy);
            auto memmovePtr = llvm::orc::ExecutorAddr::fromPtr((void*)&memmove);
            auto strlenPtr = llvm::orc::ExecutorAddr::fromPtr((void*)&strlen);
            auto strcpyPtr = llvm::orc::ExecutorAddr::fromPtr((void*)&strcpy);
            auto strdupPtr = llvm::orc::ExecutorAddr::fromPtr((void*)&strdup);

            // Register base names
            runtimeSymbols[mangle("malloc")] = llvm::orc::ExecutorSymbolDef(mallocPtr, llvm::JITSymbolFlags::Exported);
            runtimeSymbols[mangle("free")] = llvm::orc::ExecutorSymbolDef(freePtr, llvm::JITSymbolFlags::Exported);
            runtimeSymbols[mangle("memset")] = llvm::orc::ExecutorSymbolDef(memsetPtr, llvm::JITSymbolFlags::Exported);
            runtimeSymbols[mangle("memcpy")] = llvm::orc::ExecutorSymbolDef(memcpyPtr, llvm::JITSymbolFlags::Exported);
            runtimeSymbols[mangle("memmove")] = llvm::orc::ExecutorSymbolDef(memmovePtr, llvm::JITSymbolFlags::Exported);
            runtimeSymbols[mangle("strlen")] = llvm::orc::ExecutorSymbolDef(strlenPtr, llvm::JITSymbolFlags::Exported);
            runtimeSymbols[mangle("strcpy")] = llvm::orc::ExecutorSymbolDef(strcpyPtr, llvm::JITSymbolFlags::Exported);
            runtimeSymbols[mangle("strdup")] = llvm::orc::ExecutorSymbolDef(strdupPtr, llvm::JITSymbolFlags::Exported);

            // Register numbered variants (LLVM auto-renames when same function declared multiple times
            // in the module; e.g. malloc.1, malloc.2, ... up to MAX_LIBC_SYMBOL_VARIANTS)
            static constexpr int MAX_LIBC_SYMBOL_VARIANTS = 20;
            for (int i = 1; i <= MAX_LIBC_SYMBOL_VARIANTS; ++i) {
                std::string suffix = "." + std::to_string(i);
                runtimeSymbols[mangle("malloc" + suffix)] = llvm::orc::ExecutorSymbolDef(mallocPtr, llvm::JITSymbolFlags::Exported);
                runtimeSymbols[mangle("free" + suffix)] = llvm::orc::ExecutorSymbolDef(freePtr, llvm::JITSymbolFlags::Exported);
                runtimeSymbols[mangle("memset" + suffix)] = llvm::orc::ExecutorSymbolDef(memsetPtr, llvm::JITSymbolFlags::Exported);
                runtimeSymbols[mangle("memcpy" + suffix)] = llvm::orc::ExecutorSymbolDef(memcpyPtr, llvm::JITSymbolFlags::Exported);
                runtimeSymbols[mangle("memmove" + suffix)] = llvm::orc::ExecutorSymbolDef(memmovePtr, llvm::JITSymbolFlags::Exported);
                runtimeSymbols[mangle("strlen" + suffix)] = llvm::orc::ExecutorSymbolDef(strlenPtr, llvm::JITSymbolFlags::Exported);
            }
        }

        if (litConvertFunc) {
            runtimeSymbols[mangle("__vyb_convert_lit_string")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_convert_lit_string), llvm::JITSymbolFlags::Exported);
        }
        if (stringConcatFunc) {
            runtimeSymbols[mangle("__vyb_string_concat")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_string_concat), llvm::JITSymbolFlags::Exported);
        }

        // Register string replace helper (always export — codegen may emit the symbol)
        runtimeSymbols[mangle("__vyb_string_replace")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_string_replace), llvm::JITSymbolFlags::Exported);

        // Register closure reference-count helpers (always export — codegen may emit the symbols)
        runtimeSymbols[mangle("__vyb_closure_retain")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_closure_retain), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_closure_release")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_closure_release), llvm::JITSymbolFlags::Exported);

        // Register toString functions
        if (toStringIntFunc) {
            runtimeSymbols[mangle("__vyb_toString_int")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_int), llvm::JITSymbolFlags::Exported);
        }
        if (toStringInt8Func) {
            runtimeSymbols[mangle("__vyb_toString_int8")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_int8), llvm::JITSymbolFlags::Exported);
        }
        if (toStringInt16Func) {
            runtimeSymbols[mangle("__vyb_toString_int16")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_int16), llvm::JITSymbolFlags::Exported);
        }
        if (toStringInt32Func) {
            runtimeSymbols[mangle("__vyb_toString_int32")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_int32), llvm::JITSymbolFlags::Exported);
        }
        if (toStringInt64Func) {
            runtimeSymbols[mangle("__vyb_toString_int64")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_int64), llvm::JITSymbolFlags::Exported);
        }
        if (toStringUInt8Func) {
            runtimeSymbols[mangle("__vyb_toString_uint8")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_uint8), llvm::JITSymbolFlags::Exported);
        }
        if (toStringUInt16Func) {
            runtimeSymbols[mangle("__vyb_toString_uint16")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_uint16), llvm::JITSymbolFlags::Exported);
        }
        if (toStringUInt32Func) {
            runtimeSymbols[mangle("__vyb_toString_uint32")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_uint32), llvm::JITSymbolFlags::Exported);
        }
        if (toStringUInt64Func) {
            runtimeSymbols[mangle("__vyb_toString_uint64")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_uint64), llvm::JITSymbolFlags::Exported);
        }
        if (toStringFloatFunc) {
            runtimeSymbols[mangle("__vyb_toString_float")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_float), llvm::JITSymbolFlags::Exported);
        }
        if (toStringFloat32Func) {
            runtimeSymbols[mangle("__vyb_toString_float32")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_float32), llvm::JITSymbolFlags::Exported);
        }
        if (toStringBoolFunc) {
            runtimeSymbols[mangle("__vyb_toString_bool")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_bool), llvm::JITSymbolFlags::Exported);
        }
        if (toStringStringFunc) {
            runtimeSymbols[mangle("__vyb_toString_string")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_string), llvm::JITSymbolFlags::Exported);
        }
        if (toStringCharFunc) {
            runtimeSymbols[mangle("__vyb_toString_char")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_char), llvm::JITSymbolFlags::Exported);
        }
        if (toStringRuneFunc) {
            runtimeSymbols[mangle("__vyb_toString_rune")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_rune), llvm::JITSymbolFlags::Exported);
        }
        if (toStringByteFunc) {
            runtimeSymbols[mangle("__vyb_toString_byte")] = llvm::orc::ExecutorSymbolDef(
                llvm::orc::ExecutorAddr::fromPtr(&__vyb_toString_byte), llvm::JITSymbolFlags::Exported);
        }

        // Register new type conversion functions (to_string/from_string)
        runtimeSymbols[mangle("__vyb_int_to_string")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_int_to_string), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_float_to_string")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_float_to_string), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_bool_to_string")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_bool_to_string), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_string_to_string")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_string_to_string), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_string_free")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_string_free), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_string_register")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_string_register), llvm::JITSymbolFlags::Exported);

        runtimeSymbols[mangle("__vyb_int_from_string")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_int_from_string), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_float_from_string")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_float_from_string), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_bool_from_string")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_bool_from_string), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_string_from_string")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_string_from_string), llvm::JITSymbolFlags::Exported);

        // Register JSON serialization functions
        runtimeSymbols[mangle("__vyb_complex_to_json")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_complex_to_json), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_complex_from_json")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_complex_from_json), llvm::JITSymbolFlags::Exported);

        // Register type metadata functions
        runtimeSymbols[mangle("__vyb_register_type")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_register_type), llvm::JITSymbolFlags::Exported);

        // Register the type identity registry
        runtimeSymbols[mangle("__vyb_register_typename")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_register_typename), llvm::JITSymbolFlags::Exported);
        runtimeSymbols[mangle("__vyb_get_typename")] = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(&__vyb_get_typename), llvm::JITSymbolFlags::Exported);

        // Add all the runtime symbols to the main dylib
        auto defineErr = mainDylib.define(llvm::orc::absoluteSymbols(runtimeSymbols));
        if (defineErr) {
            std::string errorMsg;
            llvm::raw_string_ostream stream(errorMsg);
            stream << defineErr;
            throw std::runtime_error("Failed to define runtime symbols: " + errorMsg);
        }

        // Check main function's return type before moving module
        llvm::Function* mainFuncForTypeCheck = module->getFunction("main");
        bool mainReturnsStruct = false;
        bool mainReturnsString = false;    // { ptr, i64 } Vyb string struct
        bool mainReturnsFailableStruct = false; // { T, i8* }
        bool mainFailablePayloadIsInt = false;
        bool mainFailablePayloadIsVoidDummy = false;

        if (mainFuncForTypeCheck) {
            llvm::Type* returnType = mainFuncForTypeCheck->getReturnType();
            mainReturnsStruct = returnType->isStructTy();
            // Detect if this is a Vyb string struct: { ptr, i64 } with 2 elements
            if (mainReturnsStruct) {
                llvm::StructType* st = llvm::cast<llvm::StructType>(returnType);
                if (st->getNumElements() == 2) {
                    if (st->getElementType(1)->isPointerTy()) {
                        // Failable return ABI { payload, error_ptr }.
                        mainReturnsFailableStruct = true;
                        mainFailablePayloadIsInt = st->getElementType(0)->isIntegerTy(64);
                        mainFailablePayloadIsVoidDummy = st->getElementType(0)->isIntegerTy(1);
                    } else if (st->getElementType(0)->isPointerTy() &&
                               st->getElementType(1)->isIntegerTy(64)) {
                        mainReturnsString = true;
                    }
                }
            }
        }

        // Apply IR optimizations before JIT execution (default -O2 for JIT)
        // Note: We need a target machine for optimization, but JIT uses default target
        std::string targetTriple = llvm::sys::getDefaultTargetTriple();
        std::string error;
        auto target = llvm::TargetRegistry::lookupTarget(targetTriple, error);
        if (!target) {
            std::cerr << "Warning: Could not create target for optimization: " << error << std::endl;
        } else {
            llvm::TargetOptions opt;
            auto targetMachine = target->createTargetMachine(targetTriple, "generic", "",
                                                             opt, std::optional<llvm::Reloc::Model>());
            if (targetMachine) {
                module->setDataLayout(targetMachine->createDataLayout());
                optimize_module(module.get(), targetMachine, 2);  // Use O2 for JIT by default
                delete targetMachine;
            }
        }

        // Add the module to the JIT
        auto tsm = llvm::orc::ThreadSafeModule(std::move(module), std::move(context));
        auto addErr = jit->addIRModule(std::move(tsm));
        if (addErr) {
            std::string errorMsg;
            llvm::raw_string_ostream stream(errorMsg);
            stream << addErr;
            throw std::runtime_error("Failed to add module to JIT: " + errorMsg);
        }
        VYB_CDBG << "ORC JIT execution engine created successfully" << std::endl;

        // Call type registration function before main (simulates global constructors)
        auto registerTypesResult = jit->lookup("__vyb_register_all_types");
        if (registerTypesResult) {
            typedef void (*RegisterTypesFuncType)();
            RegisterTypesFuncType registerFunc = reinterpret_cast<RegisterTypesFuncType>(
                static_cast<void*>(registerTypesResult->toPtr<void*>()));
            registerFunc();
            VYB_CDBG << "Type metadata registered successfully" << std::endl;
        } else {
            VYB_CDBG << "No type registration function found (no custom types in program)" << std::endl;
        }

        // Look up the main function symbol
        auto symbolResult = jit->lookup("main");
        if (!symbolResult) {
            std::cerr << "Error: Could not find main function" << std::endl;
            return 1;
        }

        auto executorAddr = *symbolResult;

        // Check if return type is a struct (for String returns handled specially)
        if (mainReturnsStruct) {
            if (mainReturnsFailableStruct) {
                if (mainFailablePayloadIsVoidDummy) {
                    struct FailableVoidMainResult { bool ok_dummy; void* error; };
                    typedef FailableVoidMainResult (*FailableVoidMainFuncType)();
                    FailableVoidMainFuncType fMain = reinterpret_cast<FailableVoidMainFuncType>(
                        static_cast<void*>(executorAddr.toPtr<void*>()));
                    FailableVoidMainResult result = fMain();
                    if (result.error != nullptr) {
                        __vyb_runtime_untrapped_error(result.error);
                    }
                    return 0;
                }
                if (mainFailablePayloadIsInt) {
                    struct FailableIntMainResult { int64_t value; void* error; };
                    typedef FailableIntMainResult (*FailableIntMainFuncType)();
                    FailableIntMainFuncType fMain = reinterpret_cast<FailableIntMainFuncType>(
                        static_cast<void*>(executorAddr.toPtr<void*>()));
                    FailableIntMainResult result = fMain();
                    if (result.error != nullptr) {
                        __vyb_runtime_untrapped_error(result.error);
                    }
                    return 0;
                }
            }
            if (mainReturnsString) {
                // Single String return: call as struct { char*, int64_t } returning function
                // On x86_64 SysV ABI, { ptr, i64 } is returned in registers (rax + rdx)
                struct VybStringResult { const char* ptr; int64_t len; };
                typedef VybStringResult (*StringMainFuncType)();
                StringMainFuncType strMainFunc = reinterpret_cast<StringMainFuncType>(
                    static_cast<void*>(executorAddr.toPtr<void*>()));
                VybStringResult result = strMainFunc();
                if (result.ptr) {
                    // Output as JSON-encoded string with quotes
                    std::cout << "\"" << result.ptr << "\"" << std::endl;
                } else {
                    std::cout << "null" << std::endl;
                }
                return 0;
            }
            // Non-String struct: fall through to void call (codegen changed return type to void)
        }
        // Void return (or any non-String type, which codegen has lowered to void with
        // inline serialization): call as void and return 0.
        {
            typedef void (*VoidMainFuncType)();
            VoidMainFuncType voidMainFunc = reinterpret_cast<VoidMainFuncType>(
                static_cast<void*>(executorAddr.toPtr<void*>()));
            voidMainFunc();
            return 0;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error running Vyb code: " << e.what() << std::endl;
        throw; // Re-throw the exception to allow calling code to handle errors
    }
}

int main(int argc, char* argv[]) {
    Catch::Session session; // Catch2 entry point

    // `vyb bindgen <header.h> [-o out.vyb]` -- generate Vyb FFI bindings from a C header.
    if (argc >= 2 && std::string(argv[1]) == "bindgen") {
        if (argc < 3) {
            std::cerr << "Usage: " << argv[0] << " bindgen <header.h> [-o out.vyb]" << std::endl;
            return 1;
        }
        std::string headerPath = argv[2];
        std::string outputPath;
        for (int i = 3; i < argc; ++i) {
            if (std::string(argv[i]) == "-o" && i + 1 < argc) {
                outputPath = argv[++i];
            }
        }
        std::ifstream headerFile(headerPath);
        if (!headerFile) {
            std::cerr << "Error: could not open C header '" << headerPath << "'" << std::endl;
            return 1;
        }
        std::string source((std::istreambuf_iterator<char>(headerFile)),
                           std::istreambuf_iterator<char>());
        headerFile.close();

        std::vector<std::string> bindgenWarnings;
        std::string bindings = vyb::bindgen::generateBindings(source, &bindgenWarnings);
        for (const auto& warning : bindgenWarnings) {
            std::cerr << "warning: " << warning << std::endl;
        }

        if (!outputPath.empty()) {
            std::ofstream outFile(outputPath);
            if (!outFile) {
                std::cerr << "Error: could not write '" << outputPath << "'" << std::endl;
                return 1;
            }
            outFile << bindings;
            outFile.close();
            std::cout << "Wrote bindings to " << outputPath << std::endl;
        } else {
            std::cout << bindings;
        }
        return 0;
    }

    std::vector<std::string> catch_args;
    catch_args.push_back(argv[0]); // Program name

    bool next_arg_is_test_specifier_for_verbose = false;
    bool test_mode_active = false;
    bool parse_only_mode = false;
    bool semantic_only_mode = false;
    bool emit_llvm_ir = false;
    bool compile_mode = false;
    bool build_mode = false;
    std::string compile_output;
    std::string build_output;
    bool static_link = false;
    std::vector<std::string> link_libraries;
    std::vector<std::string> input_files;
    std::vector<fs::path> module_search_paths;
    int optimization_level = 2;  // Default -O2
    bool execute_jit = true;  // By default, execute the code with JIT

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--test") {
            test_mode_active = true;
            // Enter test mode for Catch2; do not forward our own flag
            continue;
        } else if (arg == "--parse-only") {
            parse_only_mode = true;
            g_module_parse_options.skipImportResolution = true;
            execute_jit = false;  // Don't execute if parse-only
            continue;
        } else if (arg == "--semantic-only") {
            semantic_only_mode = true;
            execute_jit = false;  // Don't execute if semantic-only
            continue;
        } else if (arg == "--emit-llvm") {
            emit_llvm_ir = true;
            continue;
        } else if (arg == "--no-execute") {
            execute_jit = false;  // Explicitly disable JIT execution
            continue;
        } else if (arg == "--compile" || arg == "-c") {
            compile_mode = true;
            execute_jit = false;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                compile_output = argv[++i];
            }
            continue;
        } else if (arg == "--build" || arg == "-b") {
            build_mode = true;
            execute_jit = false;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                build_output = argv[++i];
            }
            continue;
        } else if (arg == "--static") {
            static_link = true;
            continue;
        } else if (arg == "--link") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --link requires a library name or path" << std::endl;
                return 1;
            }
            link_libraries.emplace_back(argv[++i]);
            continue;
        } else if (arg == "--module-path") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --module-path requires a directory path" << std::endl;
                return 1;
            }
            module_search_paths.emplace_back(argv[++i]);
            continue;
        } else if (arg.substr(0, 2) == "-O") {
            // Parse optimization level: -O0, -O1, -O2, -O3
            if (arg.length() > 2) {
                optimization_level = arg[2] - '0';
                if (optimization_level < 0 || optimization_level > 3) {
                    std::cerr << "Invalid optimization level: " << arg << std::endl;
                    optimization_level = 2;
                }
            }
            continue;
        } else if (arg == "--debug-verbose") {
            if (i + 1 < argc) {
                std::string specifiers_str = argv[++i];
                if (specifiers_str == "all") {
                    g_make_all_tests_verbose = true;
                } else {
                    // Parse comma-separated specifiers
                    size_t start = 0;
                    size_t end = specifiers_str.find(',');
                    while (end != std::string::npos) {
                        g_verbose_test_specifiers.insert(specifiers_str.substr(start, end - start));
                        start = end + 1;
                        end = specifiers_str.find(',', start);
                    }
                    g_verbose_test_specifiers.insert(specifiers_str.substr(start));
                }
            } else {
                std::cerr << "Warning: --debug-verbose requires an argument (e.g., \"all\" or test_name,[tag])." << std::endl;
            }
        } else if (arg == "--no-debug-output") {
            g_suppress_all_debug_output = true;
        } else if (arg == "--debug-parser-verbose") {
            if (i + 1 < argc) {
                std::string spec_str = argv[++i];
                if (spec_str == "all") {
                    vyb::g_make_all_parser_verbose = true;
                } else {
                    size_t start = 0;
                    size_t end = spec_str.find(',');
                    while (end != std::string::npos) {
                        vyb::g_verbose_parser_test_specifiers.insert(spec_str.substr(start, end - start));
                        start = end + 1;
                        end = spec_str.find(',', start);
                    }
                    vyb::g_verbose_parser_test_specifiers.insert(spec_str.substr(start));
                }
            } else {
                std::cerr << "Warning: --debug-parser-verbose requires an argument." << std::endl;
            }
        } else if (arg == "--no-parser-debug-output") {
            vyb::g_suppress_all_parser_debug_output = true;
        } else if (arg == "--debug-codegen") {
            vyb::g_debug_codegen = true;
        }
        else if (test_mode_active || arg[0] == '-' || arg[0] == '+' || arg[0] == '[') {
            // In test mode, or it's a Catch2 flag/tag/filter — pass it along
            catch_args.push_back(arg);
        } else {
            // Non-option argument: treat as an input file, not a Catch2 filter
            input_files.push_back(arg);
        }
    }

    if (!test_mode_active && (g_make_all_tests_verbose || !g_verbose_test_specifiers.empty() || g_suppress_all_debug_output ||
                              vyb::g_make_all_parser_verbose || !vyb::g_verbose_parser_test_specifiers.empty() || vyb::g_suppress_all_parser_debug_output)) {
         std::cerr << "Warning: Debug verbosity flags (--debug-verbose, --no-debug-output, --debug-parser-verbose, --no-parser-debug-output) are intended for use with --test mode." << std::endl;
    }

    g_module_parse_options.cliModulePaths = module_search_paths;
    std::error_code exePathError;
    g_module_parse_options.executablePath = fs::absolute(argv[0], exePathError);
    if (exePathError) {
        g_module_parse_options.executablePath = argv[0];
    }

    // Convert std::vector<std::string> to char* array for Catch2
    std::vector<char*> C_catch_args;
    for(const auto& s : catch_args) {
        C_catch_args.push_back(const_cast<char*>(s.c_str()));
    }

    // Only run Catch2 tests when --test flag is explicitly provided
    if (test_mode_active) {
        int result = session.run(C_catch_args.size(), C_catch_args.data());
        return result;
    }

    // If not in test mode, proceed with original file processing logic
    if (!input_files.empty()) {
        // Use the first input file collected during argument parsing
        std::string filename = input_files[0];
        VYB_CDBG << "Processing file: " << filename << std::endl;
        try {
            // Read the source file
            std::ifstream file(filename);
            if (!file.is_open()) {
                std::cerr << "Error: Could not open file " << filename << std::endl;
                return 1;
            }
            std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();

            // In parse-only mode, just tokenize and parse
            if (parse_only_mode) {
                auto ast = parse_vyb_module(source, filename);

                std::cout << "Parse completed successfully" << std::endl;
                return 0;
            }

            // Generate LLVM IR to a file if requested
            if (emit_llvm_ir) {
                auto ast = parse_vyb_module(source, filename);

                vyb::Driver driver;

                // CRITICAL: Run semantic analysis to mark functions with needsErrorReturn
                std::cout << "Running semantic analysis..." << std::endl;
                vyb::SemanticAnalyzer semanticAnalyzer(driver);
                driver.setSemanticAnalyzer(&semanticAnalyzer);
                semanticAnalyzer.analyze(ast.get());

                // Check for semantic errors
                const auto& semanticErrors = semanticAnalyzer.getErrors();
                if (!semanticErrors.empty()) {
                    std::cerr << "\nSemantic Errors:" << std::endl;
                    for (const auto& error : semanticErrors) {
                        std::cerr << "  " << error << std::endl;
                    }
                    return 1;
                }
                std::cout << "Semantic analysis completed" << std::endl;

                vyb::LLVMCodegen codegen(driver);

                // Output file: <input>.ll
                std::string out_ll = filename;
                size_t dot = out_ll.find_last_of('.');
                if (dot != std::string::npos) out_ll = out_ll.substr(0, dot);
                out_ll += ".ll";

                codegen.generate(ast.get(), out_ll);
                std::cout << "LLVM IR generated to " << out_ll << std::endl;
                return 0;
            }

            // In semantic-only mode, run semantic analysis without execution
            if (semantic_only_mode) {
                auto ast = parse_vyb_module(source, filename);

                vyb::Driver driver;
                vyb::SemanticAnalyzer semanticAnalyzer(driver);
                driver.setSemanticAnalyzer(&semanticAnalyzer);  // Make semantic data available
                semanticAnalyzer.analyze(ast.get());

                const auto& semanticErrors = semanticAnalyzer.getErrors();
                if (!semanticErrors.empty()) {
                    std::cerr << "\nSemantic Errors:" << std::endl;
                    for (const auto& error : semanticErrors) {
                        std::cerr << "  " << error << std::endl;
                    }
                    return 1;
                }

                std::cout << "Semantic analysis completed successfully" << std::endl;
                return 0;
            }

            // Compile mode: emit object file
            if (compile_mode) {
                std::string objFile = compile_output;
                if (objFile.empty()) {
                    // Default: replace extension with .o
                    objFile = filename;
                    size_t dot = objFile.find_last_of('.');
                    if (dot != std::string::npos) {
                        objFile = objFile.substr(0, dot);
                    }
                    objFile += ".o";
                }
                return compile_vyb_to_object(source, filename, objFile, optimization_level);
            }

            // Build mode: compile and link to executable
            if (build_mode) {
                std::string executableName = build_output;
                if (executableName.empty()) {
                    // Default: replace extension with no extension (executable name)
                    executableName = filename;
                    size_t dot = executableName.find_last_of('.');
                    if (dot != std::string::npos) {
                        executableName = executableName.substr(0, dot);
                    }
                }

                // Step 1: Compile to object file
                std::string objFile = executableName + ".o";
                std::cout << "Step 1: Compiling to object file..." << std::endl;
                int compileResult = compile_vyb_to_object(source, filename, objFile, optimization_level);
                if (compileResult != 0) {
                    std::cerr << "Compilation failed" << std::endl;
                    return compileResult;
                }

                // Step 2: Link to executable
                std::cout << "\nStep 2: Linking to executable..." << std::endl;
                std::vector<std::string> objectFiles = { objFile };
                int linkResult = link_vyb_executable(objectFiles, executableName, link_libraries, static_link);

                if (linkResult == 0) {
                    std::cout << "\n✅ Build successful!" << std::endl;
                    std::cout << "Executable: " << executableName << std::endl;
                    std::cout << "Run with: ./" << executableName << std::endl;
                }

                return linkResult;
            }

            // Default behavior: JIT compile and execute the code
            if (execute_jit) {
                try {
                    VYB_CDBG << "Starting JIT execution of " << filename << std::endl;
                    int result = run_vyb_code(source, filename, emit_llvm_ir);
                    return result;
                } catch (const std::exception& e) {
                    std::cerr << "Error during code execution: " << e.what() << std::endl;
                    return 1;
                }
            }

            // --no-execute: parse + semantic analysis to validate the file without running it
            {
                auto ast = parse_vyb_module(source, filename);

                vyb::Driver driver;
                vyb::SemanticAnalyzer semanticAnalyzer(driver);
                driver.setSemanticAnalyzer(&semanticAnalyzer);
                semanticAnalyzer.analyze(ast.get());

                const auto& semanticErrors = semanticAnalyzer.getErrors();
                if (!semanticErrors.empty()) {
                    std::cerr << "\nSemantic Errors:" << std::endl;
                    for (const auto& err : semanticErrors) {
                        std::cerr << "  " << err << std::endl;
                    }
                    std::cerr << "Error running Vyb code: Semantic analysis failed with "
                              << semanticErrors.size() << " error(s)" << std::endl;
                    return 1;
                }
                return 0;
            }
        } catch (const std::exception& e) {
            std::cerr << "Exception: " << e.what() << std::endl;
            return 1;
        }
    } else {
        std::cout << "Vyb Compiler - Usage: " << argv[0] << " <filename> [options] | --test [catch2_options]" << std::endl;
        std::cout << "Options:" << std::endl;
        std::cout << "  --parse-only          Stop after parsing (validates syntax only)" << std::endl;
        std::cout << "  --semantic-only       Stop after semantic analysis" << std::endl;
        std::cout << "  --emit-llvm           Generate LLVM IR to a .ll file" << std::endl;
        std::cout << "  --compile, -c [file]  Compile to object file (.o)" << std::endl;
        std::cout << "  --build, -b [file]    Compile and link to executable (NEW!)" << std::endl;
        std::cout << "  --link <lib-or-path>  Link a native build with -l<lib> or an explicit library/object path (repeatable)" << std::endl;
        std::cout << "  --static              Use static linking (with --build)" << std::endl;
        std::cout << "  --module-path <dir>   Add a module search path (repeatable)" << std::endl;
        std::cout << "  -O0, -O1, -O2, -O3    Set optimization level (default: -O2)" << std::endl;
        std::cout << "  --no-execute          Do not execute the code (JIT is on by default)" << std::endl;
        std::cout << std::endl;
        std::cout << "Examples:" << std::endl;
        std::cout << "  " << argv[0] << " program.vyb                    # JIT compile and run" << std::endl;
        std::cout << "  " << argv[0] << " program.vyb --build myapp      # Build executable" << std::endl;
        std::cout << "  " << argv[0] << " program.vyb --build myapp --link m  # Build and link libm" << std::endl;
        std::cout << "  " << argv[0] << " app.vyb --module-path modules   # Add module search path" << std::endl;
        std::cout << "  " << argv[0] << " program.vyb -b myapp -O3       # Build with max optimization" << std::endl;
        std::cout << "  " << argv[0] << " program.vyb --compile prog.o   # Compile to object file" << std::endl;
        std::cout << std::endl;
        std::cout << "Test Mode Options:" << std::endl;
        std::cout << "  --test                Run test suite" << std::endl;
        std::cout << "  --debug-verbose <all|test_name,[tag],...]> Enable verbose output for tests" << std::endl;
        std::cout << "  --no-debug-output     Suppress all debug output" << std::endl;
        std::cout << "  --debug-parser-verbose <all|test_name,[tag],...]> Enable verbose parser output" << std::endl;
        std::cout << "  --no-parser-debug-output Suppress parser debug output" << std::endl;
    }

    return 0; // Reached only when no input file given and not in test mode (usage printed above)
}
