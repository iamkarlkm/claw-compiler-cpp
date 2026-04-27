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
#include "lexer/lexer.h"
#include "lexer/token.h"
#include "parser/parser.h"
#include "common/common.h"
#include "type/type_system.h"
#include "interpreter/interpreter.h"
#include "bytecode/bytecode.h"
#include "bytecode/bytecode_compiler.h"
#include "vm/claw_vm.h"
#include "codegen/c_codegen.h"
#include "codegen/native_codegen.h"
#include "emitter/wasm/wasm_backend.h"
#include "pipeline/execution_engine.h"
#include "repl/claw_repl.h"

using namespace claw;
using namespace std::chrono;

// ============================================================================
// 配置与常量
// ============================================================================

constexpr const char* CLAW_VERSION = "0.2.0";
constexpr const char* CLAW_BUILD_DATE = __DATE__;

// ============================================================================
// 编译选项
// ============================================================================

struct CompileOptions {
    std::string input_file;
    std::string output_file;
    
    enum class Mode { 
        None, Tokens, AST, Semantic, TypeCheck, Interpret, Bytecode, JIT, Hybrid, CCodeGen, NativeCodegen, WebAssembly, REPL 
    } mode = Mode::None;
    
    int opt_level = 0;
    bool verbose = false;
    bool show_time = false;
    bool show_ir = false;
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
    std::cout << "  -s, --semantic      Run semantic analysis\n";
    std::cout << "  -T, --typecheck     Run type checking\n";
    std::cout << "  -r, --run           Interpret AST directly\n";
    std::cout << "  -b, --bytecode      Compile to bytecode (VM)\n";
    std::cout << "  -j, --jit           JIT compile and execute\n";
    std::cout << "  -H, --hybrid        Hybrid: interpret + JIT hot paths\n";
    std::cout << "  -C, --ccodegen      Generate C code\n";
    std::cout << "  -n, --native        Generate x86-64 native code\n";
    std::cout << "  -w, --wasm          Generate WebAssembly\n";
    std::cout << "  -i, --repl          Start REPL interactive mode\n";
    
    std::cout << "\nOptions:\n";
    std::cout << "  -o, --output <file> Output file\n";
    std::cout << "  -O<0|1|2|3>         Optimization level\n";
    std::cout << "  -v, --verbose       Verbose output\n";
    std::cout << "  --time              Show compilation time\n";
    std::cout << "  --show-ir           Show generated IR/code\n";
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
            std::cerr << "[DEBUG] parse_args: setting mode to Bytecode\n";
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
        else if (arg == "--mode" || arg == "-m") {
            // 支持 --mode=xxx 或 -m xxx 格式
            std::string mode_arg;
            if (arg == "--mode" && i + 1 < argc && argv[i+1][0] != '-') {
                mode_arg = argv[++i];
            } else if (arg == "-m" && i + 1 < argc) {
                mode_arg = argv[++i];
            } else if (arg.rfind("--mode=", 0) == 0) {
                mode_arg = arg.substr(7);
            } else {
                std::cerr << "Error: --mode requires a value (tokens|ast|interpret|bytecode|jit|hybrid|ccodegen)\n";
                return false;
            }
            
            if (mode_arg == "tokens") opts.mode = CompileOptions::Mode::Tokens;
            else if (mode_arg == "ast") opts.mode = CompileOptions::Mode::AST;
            else if (mode_arg == "interpret" || mode_arg == "interp") opts.mode = CompileOptions::Mode::Interpret;
            else if (mode_arg == "bytecode") {
                std::cerr << "[DEBUG] parse_args: setting mode to Bytecode (from --mode=)\n";
                opts.mode = CompileOptions::Mode::Bytecode;
            }
            else if (mode_arg == "jit") opts.mode = CompileOptions::Mode::JIT;
            else if (mode_arg == "hybrid") opts.mode = CompileOptions::Mode::Hybrid;
            else if (mode_arg == "ccodegen" || mode_arg == "c") opts.mode = CompileOptions::Mode::CCodeGen;
            else if (mode_arg == "native") opts.mode = CompileOptions::Mode::NativeCodegen;
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

std::vector<Token> lex(const std::string& source, bool verbose) {
    Lexer lexer(source);
    auto tokens = lexer.scan_all();
    if (verbose) {
        std::cout << "  Lexed " << tokens.size() << " tokens\n";
    }
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

bool type_check(ast::Program& program, bool verbose) {
    type::TypeChecker type_checker;
    type_checker.check(program);
    
    if (type_checker.has_errors()) {
        std::cerr << "=== Type Errors ===\n";
        for (const auto& err : type_checker.errors()) {
            std::cerr << "Error: " << err.what() << "\n";
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

bool run_bytecode(std::shared_ptr<ast::Program> program, bool verbose, bool show_ir) {
    std::cerr << "[DEBUG] run_bytecode called with shared_ptr, mode=Bytecode\n";
    if (!program) {
        std::cerr << "Error: Null program pointer\n";
        return false;
    }
    
    if (verbose) {
        std::cout << "  Compiling to bytecode...\n";
    }
    
    // 字节码编译 - 使用兼容层
    BytecodeCompiler compiler;
    auto module = compiler.compile(*program);
    
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
    
    // 在 VM 中执行
    vm::ClawVM vm;
    if (!vm.load_module(*module)) {
        std::cerr << "Error: Failed to load module into VM: " << vm.last_error << "\n";
        return false;
    }
    
    vm::Value result = vm.execute();
    
    // 输出返回值或错误信息
    if (result.tag != vm::ValueTag::NIL) {
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

bool run_bytecode(ast::Program& program, bool verbose, bool show_ir) {
    // 转发到 shared_ptr 版本
    auto program_ptr = std::shared_ptr<ast::Program>(&program, [](ast::Program*){});
    return run_bytecode(program_ptr, verbose, show_ir);
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
    auto tokens = lex(source, opts.verbose);
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
    DiagnosticReporter reporter;
    auto program = parse(tokens, reporter, opts.verbose);
    if (!program) {
        return 1;
    }
    
    if (opts.mode == CompileOptions::Mode::AST) {
        std::cout << "\n=== AST ===\n";
        std::cout << program->to_string() << "\n";
        return 0;
    }
    
    if (opts.mode == CompileOptions::Mode::Semantic) {
        std::cout << "\n=== Semantic Analysis ===\n";
        std::cout << "  (Not yet implemented - use -T for type checking)\n";
        return 0;
    }
    
    // 类型检查
    if (opts.mode != CompileOptions::Mode::Tokens && 
        opts.mode != CompileOptions::Mode::AST) {
        if (!type_check(*program, opts.verbose)) {
            return 1;
        }
    }
    
    if (opts.mode == CompileOptions::Mode::TypeCheck) {
        std::cout << "Type checking passed!\n";
        return 0;
    }
    
    // 根据模式执行
    bool success = false;
    
    std::cerr << "[DEBUG] Before switch, opts.mode = " << (int)opts.mode << std::endl;
    
    switch (opts.mode) {
        case CompileOptions::Mode::Interpret:
            success = run_interpreter(*program, opts.verbose);
            break;
            
        case CompileOptions::Mode::Bytecode:
            success = run_bytecode(*program, opts.verbose, opts.show_ir);
            break;
            
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
            
        case CompileOptions::Mode::WebAssembly:
            {
                // Generate WebAssembly using the new WASM backend
                claw::wasm::WasmModule wasm_module;
                claw::wasm::WasmCodeGenerator wasm_gen(wasm_module);
                std::string output;
                success = wasm_gen.generate_from_program(program, output, opts.verbose);
                if (success) {
                    // Determine output file
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
            std::cerr << "[DEBUG] default case hit! mode=" << (int)opts.mode << "\n";
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
