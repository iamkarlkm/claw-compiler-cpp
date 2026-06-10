// Claw Compiler - Main Entry Point with Multi-Mode Execution
// Supports: AST interpretation, Bytecode VM, JIT compilation, C codegen

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include "lexer/lexer.h"
#include "lexer/token.h"
#include "parser/parser.h"
#include "common/common.h"
#include "type/type_system.h"
#include "type/error_effect_analyzer.h"
#include "interpreter/interpreter.h"
#include "bytecode/bytecode.h"
#include "bytecode/bytecode_compiler.h"
#include "vm/claw_vm.h"
#include "codegen/c_codegen.h"
#include "codegen/native_codegen.h"
#include "codegen/macho_writer.h"
#include "codegen/linker_integration.h"
#include "common/parse_cache.h"
#include "common/compilation_cache.h"
#include "emitter/wasm/wasm_backend.h"
#include "pipeline/execution_engine.h"
#include "optimizer/tree_shaker.h"
#include "optimizer/constant_folder.h"
#include "optimizer/control_flow_simplifier.h"
#include "optimizer/dead_code_eliminator.h"
#include "optimizer/function_inliner.h"
#include "optimizer/tail_call_optimizer.h"
#include "optimizer/algebraic_simplifier.h"
#include "optimizer/peephole_optimizer.h"
#include "optimizer/monomorphizer.h"
#include "optimizer/iterator_desugarer.h"
#include "optimizer/constant_propagator.h"
#include "type/type_inference.h"
#include "ast/ast_compact_repr.h"
#include "repl/claw_repl.h"

using namespace claw;
using namespace std::chrono;

// ============================================================================
// 配置与常量
// ============================================================================

constexpr const char* CLAW_VERSION = "0.2.0";
constexpr const char* CLAW_BUILD_DATE = __DATE__;

// ============================================================================
// AOT Build Cache - caches compiled executables for repeated builds
// ============================================================================

static std::string get_build_cache_dir() {
    const char* home = getenv("HOME");
    if (!home) home = ".";
    return std::string(home) + "/.claw/cache/builds";
}

static uint64_t fnv1a_hash(const std::string& data) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (unsigned char c : data) {
        hash ^= c;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

static std::string build_cache_key(const std::string& source) {
    // Hash source + compiler version to auto-invalidate on compiler updates
    return std::to_string(fnv1a_hash(source + CLAW_VERSION));
}

static std::string build_cache_path(const std::string& key) {
    return get_build_cache_dir() + "/" + key;
}

static bool copy_file(const std::string& from, const std::string& to) {
    std::ifstream in(from, std::ios::binary);
    if (!in) return false;
    std::ofstream out(to, std::ios::binary);
    if (!out) return false;
    out << in.rdbuf();
    return in.good() && out.good();
}

static bool check_build_cache(const std::string& source,
                               const std::string& output_file,
                               bool verbose) {
    std::string key = build_cache_key(source);
    std::string cached = build_cache_path(key);
    struct stat st;
    if (stat(cached.c_str(), &st) != 0) return false;

    if (!copy_file(cached, output_file)) return false;
    // Make executable
    chmod(output_file.c_str(), st.st_mode | 0111);

    if (verbose) {
        std::cout << "  Build cache hit: copied from " << cached << "\n";
    }
    return true;
}

static void save_build_cache(const std::string& source,
                              const std::string& output_file) {
    std::string cache_dir = get_build_cache_dir();
    std::string mkdir_cmd = "mkdir -p " + cache_dir;
    std::system(mkdir_cmd.c_str());

    std::string key = build_cache_key(source);
    std::string cached = build_cache_path(key);
    copy_file(output_file, cached);
    chmod(cached.c_str(), 0755);
}

// ============================================================================
// 编译选项
// ============================================================================

struct CompileOptions {
    std::string input_file;
    std::string output_file;
    
    enum class Mode {
        None, Tokens, AST, CompactAST, Semantic, TypeCheck, Interpret, Bytecode, JIT, Hybrid, CCodeGen, NativeCodegen, AOT, WebAssembly, REPL
    } mode = Mode::None;
    
    int opt_level = 0;
    bool verbose = false;
    bool show_time = false;
    bool show_ir = false;
    bool diagnostics_json = false;
    bool show_version = false;
};

// ============================================================================
// 辅助函数
// ============================================================================

void print_usage(const char* prog) {
    std::cout << "Claw Compiler v" << CLAW_VERSION << " (" << CLAW_BUILD_DATE << ")\n";
    std::cout << "Usage: " << prog << " [options] <file.claw>\n\n";
    
    std::cout << "Execution Modes:\n";
    std::cout << "  -t, --tokens        Print tokens only\n";
    std::cout << "  -a, --ast           Print AST\n";
    std::cout << "  --compact-ast       Print compact AST representation (AI-friendly)\n";
    std::cout << "  -s, --semantic      Run semantic analysis\n";
    std::cout << "  -T, --typecheck     Run type checking\n";
    std::cout << "  -r, --run           Interpret AST directly\n";
    std::cout << "  -b, --bytecode      Compile to bytecode (VM)\n";
    std::cout << "  -j, --jit           JIT compile and execute\n";
    std::cout << "  -H, --hybrid        Hybrid: interpret + JIT hot paths\n";
    std::cout << "  -C, --ccodegen      Generate C code\n";
    std::cout << "  -n, --native        Generate x86-64 native code\n";
    std::cout << "  --aot               AOT compile to executable (x86-64 + Mach-O)\n";
    std::cout << "  -w, --wasm          Generate WebAssembly\n";
    std::cout << "  -i, --repl          Start REPL interactive mode\n";
    
    std::cout << "\nOptions:\n";
    std::cout << "  -o, --output <file> Output file\n";
    std::cout << "  -O<0|1|2|3>         Optimization level\n";
    std::cout << "  -v, --verbose       Verbose output\n";
    std::cout << "  --time              Show compilation time\n";
    std::cout << "  --show-ir           Show generated IR/code\n";
    std::cout << "  --diagnostics-json  Output diagnostics as JSON\n";
    std::cout << "  -h, --help          Show this help\n";
}

std::string get_mode_name(CompileOptions::Mode mode) {
    switch (mode) {
        case CompileOptions::Mode::Tokens: return "Tokens";
        case CompileOptions::Mode::AST: return "AST";
        case CompileOptions::Mode::Semantic: return "Semantic";
        case CompileOptions::Mode::TypeCheck: return "TypeCheck";
        case CompileOptions::Mode::Interpret: return "Interpret (AST)";
        case CompileOptions::Mode::Bytecode: return "Bytecode (VM)";
        case CompileOptions::Mode::JIT: return "JIT Compiled";
        case CompileOptions::Mode::Hybrid: return "Hybrid (VM+JIT)";
        case CompileOptions::Mode::CCodeGen: return "C CodeGen";
        case CompileOptions::Mode::NativeCodegen: return "Native x86-64";
        case CompileOptions::Mode::AOT: return "AOT (x86-64 executable)";
        case CompileOptions::Mode::WebAssembly: return "WebAssembly";
        case CompileOptions::Mode::REPL: return "REPL (Interactive)";
        default: return "None";
    }
}

// ============================================================================
// 解析命令行参数
// ============================================================================

bool parse_args(int argc, char** argv, CompileOptions& opts) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return false;
        }
        else if (arg == "--version") {
            opts.show_version = true;
            return false;
        }
        else if (arg == "-t" || arg == "--tokens") {
            opts.mode = CompileOptions::Mode::Tokens;
        }
        else if (arg == "-a" || arg == "--ast") {
            opts.mode = CompileOptions::Mode::AST;
        }
        else if (arg == "-s" || arg == "--semantic") {
            opts.mode = CompileOptions::Mode::Semantic;
        }
        else if (arg == "-T" || arg == "--typecheck") {
            opts.mode = CompileOptions::Mode::TypeCheck;
        }
        else if (arg == "-r" || arg == "--run") {
            opts.mode = CompileOptions::Mode::Interpret;
        }
        else if (arg == "-b" || arg == "--bytecode") {
            opts.mode = CompileOptions::Mode::Bytecode;
        }
        else if (arg == "-j" || arg == "--jit") {
            opts.mode = CompileOptions::Mode::JIT;
        }
        else if (arg == "-H" || arg == "--hybrid") {
            opts.mode = CompileOptions::Mode::Hybrid;
        }
        else if (arg == "-C" || arg == "--ccodegen") {
            opts.mode = CompileOptions::Mode::CCodeGen;
        }
        else if (arg == "-n" || arg == "--native") {
            opts.mode = CompileOptions::Mode::NativeCodegen;
        }
        else if (arg == "--aot") {
            opts.mode = CompileOptions::Mode::AOT;
        }
        else if (arg == "-w" || arg == "--wasm") {
            opts.mode = CompileOptions::Mode::WebAssembly;
        }
        else if (arg == "-i" || arg == "--repl") {
            opts.mode = CompileOptions::Mode::REPL;
        }
        else if (arg == "-o" || arg == "--output") {
            if (i + 1 < argc) {
                opts.output_file = argv[++i];
            }
        }
        else if (arg.rfind("-O", 0) == 0 && arg.length() >= 2) {
            opts.opt_level = arg[2] - '0';
            if (opts.opt_level < 0 || opts.opt_level > 3) {
                std::cerr << "Error: Invalid optimization level: " << arg[2] << "\n";
                return false;
            }
        }
        else if (arg == "-v" || arg == "--verbose") {
            opts.verbose = true;
        }
        else if (arg == "--time") {
            opts.show_time = true;
        }
        else if (arg == "--show-ir") {
            opts.show_ir = true;
        }
        else if (arg == "--diagnostics-json") {
            opts.diagnostics_json = true;
        }
        else if (arg == "--mode" || arg.rfind("--mode=", 0) == 0 || arg == "-m") {
            // 支持 --mode=xxx 或 -m xxx 格式
            std::string mode_arg;
            if (arg.rfind("--mode=", 0) == 0) {
                mode_arg = arg.substr(7);
            } else if (arg == "--mode" && i + 1 < argc && argv[i+1][0] != '-') {
                mode_arg = argv[++i];
            } else if (arg == "-m" && i + 1 < argc) {
                mode_arg = argv[++i];
            } else {
                std::cerr << "Error: --mode requires a value (tokens|ast|interpret|bytecode|jit|hybrid|ccodegen)\n";
                return false;
            }
            
            if (mode_arg == "tokens") opts.mode = CompileOptions::Mode::Tokens;
            else if (mode_arg == "ast") opts.mode = CompileOptions::Mode::AST;
            else if (mode_arg == "compact-ast") opts.mode = CompileOptions::Mode::CompactAST;
            else if (mode_arg == "interpret" || mode_arg == "interp") opts.mode = CompileOptions::Mode::Interpret;
            else if (mode_arg == "bytecode") {
                opts.mode = CompileOptions::Mode::Bytecode;
            }
            else if (mode_arg == "jit") opts.mode = CompileOptions::Mode::JIT;
            else if (mode_arg == "hybrid") opts.mode = CompileOptions::Mode::Hybrid;
            else if (mode_arg == "ccodegen" || mode_arg == "c") opts.mode = CompileOptions::Mode::CCodeGen;
            else if (mode_arg == "native") opts.mode = CompileOptions::Mode::NativeCodegen;
            else if (mode_arg == "aot") opts.mode = CompileOptions::Mode::AOT;
            else if (mode_arg == "wasm" || mode_arg == "webassembly") opts.mode = CompileOptions::Mode::WebAssembly;
            else if (mode_arg == "repl") opts.mode = CompileOptions::Mode::REPL;
            else if (mode_arg == "typecheck") opts.mode = CompileOptions::Mode::TypeCheck;
            else if (mode_arg == "semantic") opts.mode = CompileOptions::Mode::Semantic;
            else {
                std::cerr << "Error: Unknown mode '" << mode_arg << "'\n";
                return false;
            }
        }
        else if (arg[0] != '-') {
            opts.input_file = arg;
        }
    }
    
    // REPL mode doesn't require input file
    if (opts.input_file.empty() && opts.mode != CompileOptions::Mode::REPL) {
        std::cerr << "Error: No input file specified\n";
        print_usage(argv[0]);
        return false;
    }
    
    return true;
}

// ============================================================================
// 加载源文件
// ============================================================================

bool load_source(const std::string& filename, std::string& source) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file: " << filename << "\n";
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    source = buffer.str();
    file.close();
    return true;
}

// ============================================================================
// 词法分析
// ============================================================================

std::vector<Token> lex(const std::string& source, const std::string& filename, bool verbose) {
    ParseCache cache;
    if (cache.has_cache(source, filename)) {
        auto tokens = cache.load_tokens();
        if (verbose) {
            std::cout << "  Loaded " << tokens.size() << " tokens from cache\n";
        }
        return tokens;
    }

    Lexer lexer(source);
    auto tokens = lexer.scan_all();
    if (verbose) {
        std::cout << "  Lexed " << tokens.size() << " tokens\n";
    }

    cache.save_tokens(source, filename, tokens);
    return tokens;
}

// ============================================================================
// 语法分析
// ============================================================================

std::shared_ptr<ast::Program> parse(const std::vector<Token>& tokens,
                                     DiagnosticReporter& reporter, bool verbose) {
    Parser parser(tokens);
    parser.set_reporter(&reporter);
    auto program = parser.parse();
    
    if (reporter.has_errors()) {
        std::cerr << "=== Parse Errors ===\n";
        reporter.print_diagnostics();
        return nullptr;
    }
    
    if (verbose) {
        std::cout << "  Parsed " << program->get_declarations().size() << " declarations\n";
    }
    return program;
}

// ============================================================================
// 类型检查
// ============================================================================

bool type_check(ast::Program& program, bool verbose,
                std::vector<CompilerError>* out_errors = nullptr) {
    type::TypeChecker type_checker;
    type_checker.check(program);

    if (type_checker.has_errors()) {
        if (out_errors) {
            *out_errors = type_checker.errors();
        } else {
            std::cerr << "=== Type Errors ===\n";
            for (const auto& err : type_checker.errors()) {
                std::cerr << "Error: " << err.what() << "\n";
            }
        }
        return false;
    }

    // Error effect analysis
    type::ErrorEffectAnalyzer eef_analyzer;
    eef_analyzer.analyze(program);

    if (eef_analyzer.has_errors()) {
        if (out_errors) {
            for (const auto& err : eef_analyzer.errors()) {
                out_errors->push_back(err);
            }
        } else {
            std::cerr << "=== Error Effect Errors ===\n";
            for (const auto& err : eef_analyzer.errors()) {
                std::cerr << "Error: " << err.what() << "\n";
            }
        }
        return false;
    }

    if (verbose) {
        std::cout << "  Type checking passed\n";
    }
    return true;
}

// ============================================================================
// AST 解释器执行
// ============================================================================

bool run_interpreter(ast::Program& program, bool verbose) {
    if (verbose) {
        std::cout << "  Running AST interpreter...\n";
    }

    interpreter::Interpreter interp;
    interp.execute(&program);
    return true;
}

// ============================================================================
// 字节码编译 - shared_ptr 版本 (主要入口)
// ============================================================================

bool run_bytecode(std::shared_ptr<ast::Program> program, bool verbose, bool show_ir,
                  const std::string& source = "", const std::string& filename = "",
                  const std::string& config_key = "") {
    (void)show_ir;
    if (!program) {
        std::cerr << "Error: Null program pointer\n";
        return false;
    }

    std::shared_ptr<bytecode::Module> module;
    CompilationCache comp_cache;

    // Try compilation cache first
    if (!source.empty() && !config_key.empty() &&
        comp_cache.has_cache(source, filename, config_key)) {
        module = comp_cache.load_module();
        if (module) {
            if (verbose) {
                std::cout << "  Loaded compiled module from cache\n";
            }
        }
    }

    if (!module) {
        if (verbose) {
            std::cout << "  Compiling to bytecode...\n";
        }

        // 字节码编译 - 使用兼容层
        BytecodeCompiler compiler;
        module = compiler.compile(*program);

        if (!module) {
            std::cerr << "Error: Bytecode compilation failed: " << compiler.getLastError() << "\n";
            return false;
        }

        if (verbose) {
            std::cout << "  Compiled " << module->functions.size() << " functions\n";
            std::cout << "  Total bytecode instructions: ";
            size_t total = 0;
            for (const auto& func : module->functions) {
                total += func.code.size();
            }
            std::cout << total << "\n";
        }

        // Bytecode peephole optimization
        if (config_key.find("O1") != std::string::npos || config_key.find("O2") != std::string::npos) {
            optimizer::PeepholeStats peep_stats;
            bool optimized = optimizer::optimize_peephole(*module, &peep_stats);
            if (verbose && optimized) {
                std::cout << "  Peephole: " << peep_stats.instructions_removed << " instructions removed ("
                          << peep_stats.patterns_matched << " patterns)\n";
            }
        }

        // Save to compilation cache
        if (!source.empty() && !config_key.empty()) {
            comp_cache.save_module(source, filename, config_key, *module);
        }
    }

    // 在 VM 中执行
    vm::ClawVM vm;
    if (!vm.load_module(*module)) {
        std::cerr << "Error: Failed to load module into VM: " << vm.last_error << "\n";
        return false;
    }
    // Setup async event loop callback
    vm.runtime.on_future_resolved = [&rt = vm.runtime](std::shared_ptr<vm::FutureValue> future) {
        for (auto& coro : future->waiting_coroutines) {
            if (coro) {
                rt.ready_coroutines.push_back(coro);
            }
        }
        future->waiting_coroutines.clear();
        std::lock_guard<std::mutex> lock(rt.event_mutex);
        rt.event_ready = true;
        rt.event_cv.notify_one();
    };

    vm::Value result = vm.execute();

    // Event loop: process ready coroutines and wait for external async events
    while (!vm.runtime.ready_coroutines.empty() || vm.runtime.pending_futures.load() > 0) {
        // Process all currently ready coroutines
        while (!vm.runtime.ready_coroutines.empty()) {
            auto coro = vm.runtime.ready_coroutines.front();
            vm.runtime.ready_coroutines.pop_front();
            if (!coro || coro->is_complete) continue;
            vm.resume_coroutine(coro);
        }

        // If no coroutines ready but async operations pending, wait for events
        if (vm.runtime.ready_coroutines.empty() && vm.runtime.pending_futures.load() > 0) {
            std::unique_lock<std::mutex> lock(vm.runtime.event_mutex);
            vm.runtime.event_cv.wait_for(lock, std::chrono::milliseconds(100), [&]() {
                return vm.runtime.event_ready || !vm.runtime.ready_coroutines.empty();
            });
            vm.runtime.event_ready = false;
        }
    }

    // 输出返回值或错误信息 (仅在 verbose 模式下)
    if (verbose && result.tag != vm::ValueTag::NIL) {
        std::cout << "Return value: " << result.to_string() << "\n";
    }

    if (verbose) {
        // 从 runtime 获取执行统计
        std::cout << "  VM execution completed\n";
    }

    return true;
}

// ============================================================================
// 字节码编译 - 引用版本 (兼容)
// ============================================================================

bool run_bytecode(ast::Program& program, bool verbose, bool show_ir,
                  const std::string& source, const std::string& filename,
                  const std::string& config_key) {
    // 转发到 shared_ptr 版本
    auto program_ptr = std::shared_ptr<ast::Program>(&program, [](ast::Program*){});
    return run_bytecode(program_ptr, verbose, show_ir, source, filename, config_key);
}

// ============================================================================
// JIT 编译执行
// ============================================================================

bool run_jit(const std::string& input_file, bool verbose, bool show_ir) {
    if (verbose) {
        std::cout << "  Running in JIT mode...\n";
    }

    // 读取源码文件用于 JIT 编译
    std::string source_content;
    std::ifstream input(input_file);
    if (!input.is_open()) {
        std::cerr << "Error: Cannot open input file: " << input_file << "\n";
        return false;
    }

    // 读取文件内容 (修复: 原来缺少这行!)
    std::stringstream buffer;
    buffer << input.rdbuf();
    source_content = buffer.str();

    if (verbose) {
        std::cout << "  Loaded " << source_content.size() << " bytes of source code\n";
    }

    // 使用 ExecutionEngine 执行 JIT 模式
    claw::ExecutionConfig config;
    config.mode = claw::ExecutionMode::JIT_COMPILED;
    config.enable_method_jit = true;
    config.enable_optimizing_jit = true;
    config.hot_threshold = 1000;
    config.trace_execution = verbose;
    config.dump_bytecode = show_ir;

    claw::ExecutionEngine engine(config);

    // 加载源码并编译
    if (!engine.load_source(source_content)) {
        std::cerr << "Error: Failed to load source for JIT compilation\n";
        return false;
    }

    // 执行
    auto result = engine.execute("main");
    
    if (!result.success) {
        std::cerr << "Error: JIT execution failed: " << result.error_message << "\n";
        return false;
    }
    
    if (verbose || !result.output.empty()) {
        std::cout << result.output;
    }
    
    std::cout << "\n[JIT] Execution completed successfully\n";
    std::cout << "  Total time: " << result.stats.total_time.count() / 1000.0 << " ms\n";
    std::cout << "  Instructions: " << result.stats.instructions_executed << "\n";
    std::cout << "  JIT compilations: " << result.stats.jit_compilations << "\n";
    
    return true;
}

// ============================================================================
// Hybrid 混合执行
// ============================================================================

bool run_hybrid(const std::string& input_file, bool verbose, bool show_ir) {
    if (verbose) {
        std::cout << "  Running in Hybrid mode (VM + JIT)...\n";
    }
    
    std::string source_content;
    std::ifstream input(input_file);
    if (!input.is_open()) {
        std::cerr << "Error: Cannot open input file: " << input_file << "\n";
        return false;
    } else {
        std::stringstream buffer;
        buffer << input.rdbuf();
        source_content = buffer.str();
    }
    
    claw::ExecutionConfig config;
    config.mode = claw::ExecutionMode::HYBRID;
    config.enable_method_jit = true;
    config.enable_optimizing_jit = true;
    config.hot_threshold = 100;  // 较低阈值触发 JIT
    config.trace_execution = verbose;
    config.dump_bytecode = show_ir;
    
    claw::ExecutionEngine engine(config);
    
    if (!engine.load_source(source_content)) {
        std::cerr << "Error: Failed to load source for hybrid execution\n";
        return false;
    }
    
    auto result = engine.execute("main");
    
    if (!result.success) {
        std::cerr << "Error: Hybrid execution failed: " << result.error_message << "\n";
        return false;
    }
    
    if (verbose || !result.output.empty()) {
        std::cout << result.output;
    }
    
    auto stats = engine.get_stats();
    std::cout << "\n[Hybrid] Execution completed\n";
    std::cout << "  Total time: " << result.stats.total_time.count() / 1000.0 << " ms\n";
    std::cout << "  Bytecode instructions: " << stats.bytecode_instructions_executed << "\n";
    std::cout << "  JIT compilations: " << stats.jit_compilations << "\n";
    std::cout << "  Inline cache hits: " << stats.inline_cache_hits << "\n";
    std::cout << "  OSR count: " << stats.osr_count << "\n";
    
    return true;
}

// ============================================================================
// C 代码生成
// ============================================================================

bool generate_c(ast::Program& program, bool verbose, bool show_ir, 
                const std::string& output_file) {
    if (verbose) {
        std::cout << "  Generating C code...\n";
    }
    
    codegen::CCodeGenerator codegen;
    bool success = codegen.generate(&program);
    
    if (!success) {
        std::cerr << "=== C Codegen Errors ===\n";
        return false;
    }
    
    if (show_ir) {
        std::cout << "\n=== Generated C Code ===\n";
        std::cout << codegen.get_code() << "\n";
    }
    
    // 写入输出文件
    if (!output_file.empty()) {
        std::ofstream out(output_file);
        if (!out.is_open()) {
            std::cerr << "Error: Cannot write to output file: " << output_file << "\n";
            return false;
        }
        out << codegen.get_code();
        out.close();
        if (verbose) {
            std::cout << "  C code written to: " << output_file << "\n";
        }
    }
    
    return true;
}

// ============================================================================
// 原生 x86-64 机器码生成
// ============================================================================

bool generate_native(ast::Program& program, bool verbose, bool show_ir,
                     const std::string& output_file) {
    if (verbose) {
        std::cout << "  Compiling to x86-64 native code...\n";
    }
    
    // First compile to bytecode
    BytecodeCompiler bc_compiler;
    auto module = bc_compiler.compile(program);
    
    if (!module) {
        std::cerr << "Error: Bytecode compilation failed: " << bc_compiler.getLastError() << "\n";
        return false;
    }
    
    if (verbose) {
        std::cout << "  Compiled " << module->functions.size() << " functions\n";
        std::cout << "  Total bytecode instructions: ";
        size_t total = 0;
        for (const auto& func : module->functions) {
            total += func.code.size();
        }
        std::cout << total << "\n";
    }
    
    // Then compile bytecode to native code
    codegen::NativeCodeGenerator native_codegen;
    codegen::NativeCodeGenerator::Config config;
    config.enable_sse2 = true;
    config.enable_avx = false;
    config.enable_optimizations = true;
    native_codegen.set_config(config);
    
    if (!native_codegen.compile_module(*module)) {
        std::cerr << "Error: Native code generation failed: " << native_codegen.get_error() << "\n";
        return false;
    }
    
    if (verbose) {
        std::cout << "  Generated " << native_codegen.get_code().size() << " bytes of native code\n";
    }
    
    // Get the compiled code
    const auto& code = native_codegen.get_code();
    
    if (show_ir) {
        std::cout << "\n=== Generated Native Code (hex) ===\n";
        size_t print_len = std::min(code.size(), size_t(256));
        for (size_t i = 0; i < print_len; i++) {
            printf("%02x ", code[i]);
            if ((i + 1) % 16 == 0) printf("\n");
        }
        if (code.size() > print_len) {
            printf("\n... (%zu more bytes)\n", code.size() - print_len);
        }
    }
    
    // Write output file
    if (!output_file.empty()) {
        // Write as raw binary
        std::ofstream out(output_file, std::ios::binary);
        if (!out.is_open()) {
            std::cerr << "Error: Cannot write to output file: " << output_file << "\n";
            return false;
        }
        out.write(reinterpret_cast<const char*>(code.data()), code.size());
        out.close();
        
        // Make executable (Unix)
        std::string chmod_cmd = "chmod +x " + output_file;
        system(chmod_cmd.c_str());
        
        if (verbose) {
            std::cout << "  Native code written to: " << output_file << "\n";
            std::cout << "  (Run with: " << output_file << ")\n";
        }
    }
    
    // If no output file, try to execute directly
    if (output_file.empty()) {
        void* entry = native_codegen.get_entry_point();
        if (entry) {
            if (verbose) {
                std::cout << "  Executing native code...\n";
            }
            
            // Execute the compiled function
            // Note: This requires proper runtime setup
            // For now, just report success
            std::cout << "\n[Native] Code generated and ready for execution\n";
            std::cout << "  Code size: " << code.size() << " bytes\n";
        }
    }
    
    return true;
}

// ============================================================================
// AOT 编译：生成可执行文件
// ============================================================================

bool generate_aot(ast::Program& program, bool verbose, bool show_ir,
                  const std::string& output_file) {
    std::cerr << "[DEBUG] generate_aot called\n";
    if (verbose) {
        std::cout << "  Compiling to AOT executable...\n";
    }

    // 1. Compile to bytecode
    auto t_bc_start = high_resolution_clock::now();
    BytecodeCompiler bc_compiler;
    auto module = bc_compiler.compile(program);
    auto t_bc_end = high_resolution_clock::now();
    if (verbose) {
        std::cout << "  Bytecode: " << module->functions.size() << " functions ("
                  << duration_cast<microseconds>(t_bc_end - t_bc_start).count() / 1000.0 << " ms)\n";
    }

    if (!module) {
        std::cerr << "Error: Bytecode compilation failed: " << bc_compiler.getLastError() << "\n";
        return false;
    }

    // 2. Compile bytecode to native code
    auto t_native_start = high_resolution_clock::now();
    codegen::NativeCodeGenerator native_codegen;
    codegen::NativeCodeGenerator::Config config;
    config.enable_sse2 = true;
    config.enable_optimizations = true;
    native_codegen.set_config(config);

    if (!native_codegen.compile_module(*module)) {
        std::cerr << "Error: Native code generation failed: " << native_codegen.get_error() << "\n";
        return false;
    }
    auto t_native_end = high_resolution_clock::now();
    if (verbose) {
        std::cout << "  Native: " << duration_cast<microseconds>(t_native_end - t_native_start).count() / 1000.0 << " ms\n";
    }

    const auto& compiled_funcs = native_codegen.get_compiled_functions();
    if (compiled_funcs.empty()) {
        std::cerr << "Error: No compiled functions generated\n";
        return false;
    }

    // 3. Write Mach-O object file
    auto t_macho_start = high_resolution_clock::now();
    std::string obj_path = output_file + ".o";
    claw::codegen::MachOWriter writer;

    // Collect unique external symbols
    std::unordered_set<std::string> external_syms;
    for (const auto& cf : compiled_funcs) {
        for (const auto& ext : cf.external_calls) {
            external_syms.insert(ext.symbol_name);
        }
    }

    // DEBUG: print external symbols
    for (const auto& sym : external_syms) {
        std::cerr << "[AOT-EXT] " << sym << "\n";
    }

    // Merge all functions into a single __text section
    claw::codegen::MachOSection text;
    text.name = "__text";
    text.segname = "__TEXT";
    text.align = 4;
    text.flags = 0x80000400;

    size_t current_offset = 0;
    for (size_t i = 0; i < compiled_funcs.size(); ++i) {
        const auto& cf = compiled_funcs[i];

        // Pad to 16-byte alignment between functions
        while (current_offset % 16 != 0) {
            text.data.push_back(0x90); // NOP
            ++current_offset;
        }

        size_t func_offset = current_offset;
        text.data.insert(text.data.end(), cf.code.begin(), cf.code.end());
        current_offset += cf.code.size();

        // Add relocations for external calls with adjusted offsets
        for (const auto& ext : cf.external_calls) {
            claw::codegen::MachORelocation rel;
            rel.offset = func_offset + ext.code_offset;
            rel.symbol = ext.symbol_name;
            rel.pcrel = true;
            rel.length = 2; // 4 bytes
            rel.type = 2;   // RELOC_X86_64_BRANCH
            rel.addend = 0;
            text.relocations.push_back(rel);
        }

        // Add function symbol
        claw::codegen::MachOSymbol sym;
        sym.name = "_" + cf.name;
        sym.value = func_offset;
        sym.section = 1; // single __text section
        sym.global = (cf.name == "main");
        writer.add_symbol(sym);
    }

    writer.add_section(text);

    // Add undefined external symbols
    for (const auto& sym_name : external_syms) {
        claw::codegen::MachOSymbol undef;
        undef.name = sym_name;
        undef.value = 0;
        undef.section = 0;
        undef.global = true;
        undef.undefined = true;
        writer.add_symbol(undef);
    }

    if (!writer.write(obj_path)) {
        std::cerr << "Error: Failed to write Mach-O object file: " << obj_path << "\n";
        return false;
    }
    auto t_macho_end = high_resolution_clock::now();
    if (verbose) {
        std::cout << "  Mach-O: " << duration_cast<microseconds>(t_macho_end - t_macho_start).count() / 1000.0 << " ms\n";
    }

    // 4. Link with system linker
    auto t_link_start = high_resolution_clock::now();
    claw::codegen::LinkerIntegration linker;

    // Pass string constants to linker for AOT string literal support
    std::vector<std::string> string_constants;
    for (const auto& cv : module->constants.values) {
        string_constants.push_back(cv.str);
    }
    linker.set_string_constants(string_constants);

    if (!linker.link_with_runtime(obj_path, output_file)) {
        std::cerr << "Error: Linking failed: " << linker.get_error() << "\n";
        return false;
    }
    auto t_link_end = high_resolution_clock::now();
    if (verbose) {
        std::cout << "  Link: " << duration_cast<microseconds>(t_link_end - t_link_start).count() / 1000.0 << " ms\n";
    }

    // Clean up temporary object file
    std::remove(obj_path.c_str());

    if (verbose) {
        std::cout << "  AOT executable written to: " << output_file << "\n";
    }

    return true;
}

// ============================================================================
// REPL 交互模式
// ============================================================================

bool run_repl(bool verbose) {
    if (verbose) {
        std::cout << "  Starting REPL...\n";
    }
    
    claw::repl::REPLConfig config;
    config.verbose = verbose;
    
    claw::repl::FullREPL repl(config);
    repl.run();
    
    return true;
}

// ============================================================================
// 主函数
// ============================================================================

int main(int argc, char** argv) {
    CompileOptions opts;

    if (!parse_args(argc, argv, opts)) {
        if (opts.show_version) {
            std::cout << "claw " << CLAW_VERSION << "\n";
            return 0;
        }
        return 1;
    }
    
    // REPL 模式直接启动交互环境，不需要加载文件
    if (opts.mode == CompileOptions::Mode::REPL) {
        std::cout << "Claw Compiler v" << CLAW_VERSION << " - REPL Mode\n";
        std::cout << "======================================\n\n";
        return run_repl(opts.verbose) ? 0 : 1;
    }
    
    std::string source;
    if (!load_source(opts.input_file, source)) {
        return 1;
    }
    
    std::cout << "Claw Compiler v" << CLAW_VERSION << "\n";
    std::cout << "Input: " << opts.input_file << " (" << source.size() << " bytes)\n";
    if (opts.mode != CompileOptions::Mode::None) {
        std::cout << "Mode: " << get_mode_name(opts.mode) << "\n";
    }
    if (opts.opt_level > 0) {
        std::cout << "Optimization: -O" << opts.opt_level << "\n";
    }
    
    auto start_time = high_resolution_clock::now();

    // 词法分析
    auto t_lex_start = high_resolution_clock::now();
    auto tokens = lex(source, opts.input_file, opts.verbose);
    auto t_lex_end = high_resolution_clock::now();
    if (opts.verbose) {
        std::cout << "  Lex: " << duration_cast<microseconds>(t_lex_end - t_lex_start).count() / 1000.0 << " ms\n";
    }
    if (opts.mode == CompileOptions::Mode::Tokens) {
        std::cout << "\n=== Tokens ===\n";
        for (size_t i = 0; i < tokens.size(); i++) {
            const auto& tok = tokens[i];
            std::cout << i << ": " << token_type_to_string(tok.type);
            if (!tok.text.empty()) {
                std::cout << " -> \"" << tok.text << "\"";
            }
            std::cout << " (line " << tok.span.start.line << ", col " << tok.span.start.column << ")\n";
        }
        return 0;
    }

    // 语法分析
    auto t_parse_start = high_resolution_clock::now();
    DiagnosticReporter reporter;
    auto program = parse(tokens, reporter, opts.verbose);
    auto t_parse_end = high_resolution_clock::now();
    if (opts.verbose) {
        std::cout << "  Parse: " << duration_cast<microseconds>(t_parse_end - t_parse_start).count() / 1000.0 << " ms\n";
    }
    if (!program) {
        if (opts.diagnostics_json) {
            std::cout << reporter.to_json() << "\n";
        }
        return 1;
    }

    if (opts.mode == CompileOptions::Mode::AST) {
        std::cout << "\n=== AST ===\n";
        std::cout << program->to_string() << "\n";
        return 0;
    }

    if (opts.mode == CompileOptions::Mode::CompactAST) {
        std::cout << "\n=== Compact AST ===\n";
        ast::CompactASTRepr repr;
        std::string compact = repr.to_compact(*program);
        std::cout << compact << "\n";
        auto [src_tokens, compact_tokens] = ast::CompactASTRepr::compare_sizes(*program, source);
        std::cout << "\n--- Token comparison ---\n";
        std::cout << "Source tokens:  " << src_tokens << "\n";
        std::cout << "Compact tokens: " << compact_tokens << "\n";
        if (src_tokens > 0) {
            int savings = static_cast<int>(100.0 * (1.0 - static_cast<double>(compact_tokens) / src_tokens));
            std::cout << "Savings:        " << savings << "%\n";
        }
        return 0;
    }

    if (opts.mode == CompileOptions::Mode::Semantic) {
        std::cout << "\n=== Semantic Analysis ===\n";
        std::cout << "  (Not yet implemented - use -T for type checking)\n";
        return 0;
    }

    // 类型检查
    auto t_type_start = high_resolution_clock::now();
    if (opts.mode != CompileOptions::Mode::Tokens &&
        opts.mode != CompileOptions::Mode::AST) {
        std::vector<CompilerError> type_errors;
        if (!type_check(*program, opts.verbose, opts.diagnostics_json ? &type_errors : nullptr)) {
            if (opts.diagnostics_json) {
                DiagnosticReporter type_reporter;
                for (const auto& err : type_errors) {
                    type_reporter.report_error(err);
                }
                std::cout << type_reporter.to_json() << "\n";
            }
            return 1;
        }
    }
    auto t_type_end = high_resolution_clock::now();
    if (opts.verbose) {
        std::cout << "  TypeCheck: " << duration_cast<microseconds>(t_type_end - t_type_start).count() / 1000.0 << " ms\n";
    }
    
    if (opts.mode == CompileOptions::Mode::TypeCheck) {
        std::cout << "Type checking passed!\n";
        return 0;
    }

    // Infer implicit generic type arguments (e.g. id(42) -> id<Int>(42))
    auto t_infer_start = high_resolution_clock::now();
    type::TypeInference type_inference;
    std::unordered_map<std::string, ast::FunctionStmt*> generic_functions;
    for (const auto& decl : program->get_declarations()) {
        if (auto* fn = dynamic_cast<ast::FunctionStmt*>(decl.get())) {
            if (fn->has_type_params()) {
                generic_functions[fn->get_name()] = fn;
            }
        }
    }
    int inferred = type_inference.infer_implicit_generic_args(*program, generic_functions);
    auto t_infer_end = high_resolution_clock::now();
    if (opts.verbose && inferred > 0) {
        std::cout << "  TypeInference: " << inferred << " implicit generic call(s) inferred"
                  << " (" << duration_cast<microseconds>(t_infer_end - t_infer_start).count() / 1000.0 << " ms)\n";
    }

    // Generic monomorphization (zero-cost generics)
    auto t_mono_start = high_resolution_clock::now();
    claw::optimizer::Monomorphizer mono;
    bool monomorphized = mono.monomorphize(*program);
    auto t_mono_end = high_resolution_clock::now();
    if (opts.verbose && monomorphized) {
        std::cout << "  Monomorphize: " << mono.get_instantiated_count() << " instances generated, "
                  << mono.get_replaced_count() << " calls replaced"
                  << " (" << duration_cast<microseconds>(t_mono_end - t_mono_start).count() / 1000.0 << " ms)\n";
    }

    // Optimization pipeline with convergence for -O2/-O3
    if (opts.opt_level > 0) {
        int max_iterations = (opts.opt_level >= 2) ? 5 : 1;
        for (int iter = 0; iter < max_iterations; iter++) {
            bool any_change = false;

            // Constant propagation (replace variables with their constant values)
            {
                auto t_prop_start = high_resolution_clock::now();
                claw::optimizer::PropagationStats prop_stats;
                bool propagated = claw::optimizer::propagate_constants(*program, &prop_stats);
                auto t_prop_end = high_resolution_clock::now();
                if (propagated) any_change = true;
                if (opts.verbose && propagated) {
                    std::cout << "  ConstProp" << (max_iterations > 1 ? "[" + std::to_string(iter) + "]" : "")
                              << ": " << prop_stats.variables_replaced << " variables replaced";
                    std::cout << " (" << duration_cast<microseconds>(t_prop_end - t_prop_start).count() / 1000.0 << " ms)\n";
                }
            }

            // Constant folding (compile-time expression evaluation)
            {
                auto t_fold_start = high_resolution_clock::now();
                claw::optimizer::FoldStats fold_stats;
                bool folded = claw::optimizer::fold_constants(*program, &fold_stats);
                auto t_fold_end = high_resolution_clock::now();
                if (folded) any_change = true;
                if (opts.verbose && folded) {
                    std::cout << "  ConstantFold" << (max_iterations > 1 ? "[" + std::to_string(iter) + "]" : "")
                              << ": " << fold_stats.expressions_folded << " expressions folded";
                    std::cout << " (" << duration_cast<microseconds>(t_fold_end - t_fold_start).count() / 1000.0 << " ms)\n";
                }
            }

            // Algebraic simplification (identity/absorbing element reductions)
            {
                auto t_alg_start = high_resolution_clock::now();
                claw::optimizer::SimplifyStats alg_stats;
                bool simplified = claw::optimizer::simplify_algebraic(*program, &alg_stats);
                auto t_alg_end = high_resolution_clock::now();
                if (simplified) any_change = true;
                if (opts.verbose && simplified) {
                    std::cout << "  Algebraic" << (max_iterations > 1 ? "[" + std::to_string(iter) + "]" : "")
                              << ": " << alg_stats.expressions_simplified << " expressions simplified";
                    std::cout << " (" << duration_cast<microseconds>(t_alg_end - t_alg_start).count() / 1000.0 << " ms)\n";
                }
            }

            // Control flow simplification (remove unreachable branches after constant folding)
            {
                auto t_cf_start = high_resolution_clock::now();
                claw::optimizer::CFSimplifyStats cf_stats;
                bool simplified = claw::optimizer::simplify_control_flow(*program, &cf_stats);
                auto t_cf_end = high_resolution_clock::now();
                if (simplified) any_change = true;
                if (opts.verbose && simplified) {
                    std::cout << "  ControlFlow" << (max_iterations > 1 ? "[" + std::to_string(iter) + "]" : "")
                              << ": " << cf_stats.if_stmts_simplified << " ifs, "
                              << cf_stats.while_loops_removed << " whiles, "
                              << cf_stats.for_loops_removed << " fors simplified";
                    std::cout << " (" << duration_cast<microseconds>(t_cf_end - t_cf_start).count() / 1000.0 << " ms)\n";
                }
            }

            // Dead code elimination (function-level unreachable code removal)
            {
                auto t_dce_start = high_resolution_clock::now();
                claw::optimizer::DCEStats dce_stats;
                bool eliminated = claw::optimizer::eliminate_dead_code(*program, &dce_stats);
                auto t_dce_end = high_resolution_clock::now();
                if (eliminated) any_change = true;
                if (opts.verbose && eliminated) {
                    std::cout << "  DCE" << (max_iterations > 1 ? "[" + std::to_string(iter) + "]" : "")
                              << ": " << dce_stats.unreachable_statements_removed << " statements removed, "
                              << dce_stats.blocks_cleaned << " blocks cleaned";
                    std::cout << " (" << duration_cast<microseconds>(t_dce_end - t_dce_start).count() / 1000.0 << " ms)\n";
                }
            }

            // Iterator desugaring (zero-cost for-loops → indexed loops)
            if (iter == 0) {
                auto t_iter_start = high_resolution_clock::now();
                claw::optimizer::DesugarStats iter_stats;
                bool desugared = claw::optimizer::desugar_iterators(*program, &iter_stats);
                auto t_iter_end = high_resolution_clock::now();
                if (desugared) any_change = true;
                if (opts.verbose && desugared) {
                    std::cout << "  IteratorDesugar: " << iter_stats.summary();
                    std::cout << " (" << duration_cast<microseconds>(t_iter_end - t_iter_start).count() / 1000.0 << " ms)\n";
                }
            }

            // Tail call optimization (transform tail-recursive functions into loops)
            if (iter == 0) {
                auto t_tco_start = high_resolution_clock::now();
                claw::optimizer::TCOStats tco_stats;
                bool tco_done = claw::optimizer::optimize_tail_calls(*program, &tco_stats);
                auto t_tco_end = high_resolution_clock::now();
                if (tco_done) any_change = true;
                if (opts.verbose && tco_done) {
                    std::cout << "  TCO: " << tco_stats.functions_transformed << " functions transformed, "
                              << tco_stats.tail_calls_eliminated << " tail calls eliminated";
                    std::cout << " (" << duration_cast<microseconds>(t_tco_end - t_tco_start).count() / 1000.0 << " ms)\n";
                }
            }

            // Function inlining (inline small expression-returning functions)
            if (iter == 0) {
                auto t_inline_start = high_resolution_clock::now();
                claw::optimizer::InlineStats inline_stats;
                claw::optimizer::FunctionInliner inliner;
                bool inlined = inliner.inline_functions(*program, &inline_stats);
                auto t_inline_end = high_resolution_clock::now();
                if (inlined) any_change = true;
                if (opts.verbose && inlined) {
                    std::cout << "  Inline: " << inline_stats.call_sites_inlined << " call sites, "
                              << inline_stats.functions_inlined << " functions inlined";
                    std::cout << " (" << duration_cast<microseconds>(t_inline_end - t_inline_start).count() / 1000.0 << " ms)\n";
                }
            }

            if (!any_change) break;
        }

        // Tree shaking (module-level dead code elimination) - run once at the end
        {
            auto t_shake_start = high_resolution_clock::now();
            claw::optimizer::TreeShakeStats shake_stats;
            bool shaken = claw::optimizer::tree_shake(*program, &shake_stats);
            auto t_shake_end = high_resolution_clock::now();
            if (opts.verbose || shaken) {
                std::cout << "  TreeShake: " << shake_stats.summary();
                if (opts.verbose) {
                    std::cout << " (" << duration_cast<microseconds>(t_shake_end - t_shake_start).count() / 1000.0 << " ms)";
                }
                std::cout << "\n";
            }
        }
    }

    // 根据模式执行
    bool success = false;
    switch (opts.mode) {
        case CompileOptions::Mode::Interpret:
            success = run_interpreter(*program, opts.verbose);
            break;
            
        case CompileOptions::Mode::Bytecode: {
            std::string config_key = "bc-O" + std::to_string(opts.opt_level);
            success = run_bytecode(*program, opts.verbose, opts.show_ir, source, opts.input_file, config_key);
            break;
        }
            
        case CompileOptions::Mode::JIT:
            success = run_jit(opts.input_file, opts.verbose, opts.show_ir);
            break;
            
        case CompileOptions::Mode::Hybrid:
            success = run_hybrid(opts.input_file, opts.verbose, opts.show_ir);
            break;
            
        case CompileOptions::Mode::CCodeGen:
            success = generate_c(*program, opts.verbose, opts.show_ir, opts.output_file);
            break;
            
        case CompileOptions::Mode::NativeCodegen:
            success = generate_native(*program, opts.verbose, opts.show_ir, opts.output_file);
            break;

        case CompileOptions::Mode::AOT:
            if (opts.output_file.empty()) {
                std::cerr << "Error: AOT mode requires -o <output_file>\n";
                success = false;
            } else if (check_build_cache(source, opts.output_file, opts.verbose)) {
                success = true;
            } else {
                success = generate_aot(*program, opts.verbose, opts.show_ir, opts.output_file);
                if (success) {
                    save_build_cache(source, opts.output_file);
                }
            }
            break;

        case CompileOptions::Mode::WebAssembly:
            {
                claw::wasm::WasmModule wasm_module;
                claw::wasm::WasmCodeGenerator wasm_gen(wasm_module);
                std::string output;
                success = wasm_gen.generate_from_program(program, output, opts.verbose);
                if (success) {
                    std::string out_file = opts.output_file.empty() ? "output.wasm" : opts.output_file;
                    std::ofstream out(out_file, std::ios::binary);
                    if (out.is_open()) {
                        out.write(output.data(), output.size());
                        out.close();
                        std::cout << "WebAssembly output written to: " << out_file << "\n";
                    } else {
                        std::cerr << "Error: Could not write to output file: " << out_file << "\n";
                        success = false;
                    }
                } else {
                    std::cerr << "WebAssembly generation failed\n";
                }
            }
            break;
            
        case CompileOptions::Mode::REPL:
            // REPL is handled above before loading source
            success = true;
            break;
            
        default:
            std::cout << "Compilation successful (parse + typecheck only)\n";
            success = true;
            break;
    }
    
    if (opts.show_time || success) {
        auto end_time = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(end_time - start_time);
        std::cout << "\nTotal time: " << (duration.count() / 1000.0) << " ms\n";
    }
    
    return success ? 0 : 1;
}
