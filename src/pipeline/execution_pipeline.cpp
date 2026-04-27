// pipeline/execution_pipeline.cpp - 统一执行管道实现

#include "execution_pipeline.h"
#include "../interpreter/interpreter.h"
#include "../optimizer.h"
#include "../codegen/c_codegen.h"
#include "../bytecode/bytecode_compiler_simple.h"
#include "../vm/claw_vm.h"
#include "../jit/jit_compiler.h"
#include <fstream>
#include <sstream>
#include <chrono>

namespace claw {
namespace pipeline {

// ============================================================================
// ExecutionPipeline 实现
// ============================================================================

ExecutionPipeline::ExecutionPipeline() {
    // 初始化
}

ExecutionPipeline::~ExecutionPipeline() {
    // 清理
}

std::vector<Token> ExecutionPipeline::lex(const std::string& source, 
                                           DiagnosticReporter& reporter) {
    auto start = std::chrono::high_resolution_clock::now();
    
    Lexer lexer(source);
    auto tokens = lexer.scan_all();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    if (verbose_) {
        std::cout << "  [Lex] " << tokens.size() << " tokens in " 
                  << duration.count() << "us\n";
    }
    
    return tokens;
}

std::shared_ptr<ast::Program> ExecutionPipeline::parse(const std::vector<Token>& tokens,
                                                        DiagnosticReporter& reporter) {
    auto start = std::chrono::high_resolution_clock::now();
    
    Parser parser(tokens);
    parser.set_reporter(&reporter);
    auto program = parser.parse();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    if (verbose_) {
        if (program) {
            std::cout << "  [Parse] Success in " << duration.count() << "us\n";
        } else {
            std::cout << "  [Parse] Failed in " << duration.count() << "us\n";
        }
    }
    
    return program;
}

std::unique_ptr<bytecode::Module> ExecutionPipeline::compile_to_bytecode(
    std::shared_ptr<ast::Program> ast) {
    if (!ast) return nullptr;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    try {
        // 使用 SimpleBytecodeCompiler 将 AST 转换为字节码
        SimpleBytecodeCompiler compiler;
        compiler.setDebugInfo(debugInfo_);
        
        auto result_module = compiler.compile(ast);
        
        if (!result_module) {
            if (verbose_) {
                std::cerr << "  [Bytecode] Compilation failed: " << compiler.getLastError() << "\n";
            }
            return nullptr;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        if (verbose_) {
            std::cout << "  [Bytecode] Compiled in " << duration.count() << "us\n";
            std::cout << "    Functions: " << result_module->functions.size() << "\n";
            std::cout << "    Constants: " << result_module->constants.size() << "\n";
            std::cout << "    Globals: " << result_module->global_names.size() << "\n";
        }
        
        codegen_time_us = duration.count();
        return result_module;
        
    } catch (const std::exception& e) {
        if (verbose_) {
            std::cerr << "  [Bytecode] Exception: " << e.what() << "\n";
        }
        return nullptr;
    }
}

Value ExecutionPipeline::run_interpreter(std::shared_ptr<ast::Program> ast) {
    if (!ast) return Value::nil();
    
    auto start = std::chrono::high_resolution_clock::now();
    
    try {
        Interpreter interpreter;
        auto result = interpreter.interpret(ast);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        if (verbose_) {
            std::cout << "  [Interpret] Completed in " << duration.count() << "us\n";
        }
        
        interpreter_result_ = result;
        return result;
    } catch (const std::exception& e) {
        std::cerr << "Runtime error: " << e.what() << "\n";
        return Value::nil();
    }
}

Value ExecutionPipeline::run_bytecode(const bytecode::Module& module) {
    auto start = std::chrono::high_resolution_clock::now();
    
    // 创建并初始化 ClawVM
    vm::ClawVM vm;
    
    // 加载字节码模块
    if (!vm.load_module(module)) {
        if (verbose_) {
            std::cerr << "  [BytecodeVM] Failed to load module: " << vm.last_error << "\n";
        }
        vm_result_ = Value::nil();
        return Value::nil();
    }
    
    if (verbose_) {
        std::cout << "  [BytecodeVM] Loaded " << module.functions.size() << " functions\n";
        std::cout << "    Globals: " << module.global_names.size() << "\n";
    }
    
    // 执行模块
    vm_result_ = vm.execute();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    if (verbose_) {
        std::cout << "  [BytecodeVM] Completed in " << duration.count() << "us\n";
    }
    
    return vm_result_;
}

// ============================================================================
// C 代码生成器执行
// ============================================================================

std::string ExecutionPipeline::run_c_codegen(std::shared_ptr<ast::Program> ast) {
    auto start = std::chrono::high_resolution_clock::now();
    
    // 使用 CCodeGenerator 生成 C 代码
    claw::codegen::CCodeGenerator cgen;
    
    // 尝试使用新的 API (Program*)
    ast::Program* program_ptr = ast.get();
    bool success = cgen.generate(program_ptr);
    
    if (!success) {
        if (verbose_) {
            std::cerr << "  [CCodeGen] Code generation failed\n";
        }
        return "";
    }
    
    std::string c_code = cgen.get_code();
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    if (verbose_) {
        std::cout << "  [CCodeGen] Generated " << c_code.size() << " bytes of C code\n";
        std::cout << "  [CCodeGen] Completed in " << duration.count() << "us\n";
    }
    
    return c_code;
}

Value ExecutionPipeline::run_jit(const bytecode::Module& module) {
    auto start = std::chrono::high_resolution_clock::now();
    
    // 使用 ExecutionEngine 执行 JIT 模式
    claw::ExecutionConfig config;
    config.mode = claw::ExecutionMode::JIT_COMPILED;
    config.enable_method_jit = true;
    config.enable_optimizing_jit = true;
    config.hot_threshold = 100;
    
    claw::ExecutionEngine engine(config);
    
    // 加载模块
    if (!engine.load_module(module)) {
        if (verbose_) {
            std::cerr << "  [JIT] Failed to load module: " << engine.get_last_error() << "\n";
        }
        return Value::nil();
    }
    
    if (verbose_) {
        std::cout << "  [JIT] Compiling module: " << module.name << "\n";
    }
    
    // 执行 JIT 编译
    auto result = engine.execute("main");
    
    if (!result.success) {
        if (verbose_) {
            std::cerr << "  [JIT] Execution failed: " << result.error_message << "\n";
        }
        return Value::nil();
    }
    
    if (verbose_) {
        auto stats = engine.get_stats();
        std::cout << "  [JIT] Compiled " << stats.jit_compilations << " functions\n";
        std::cout << "  [JIT] Inline cache hits: " << stats.inline_cache_hits << "\n";
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    if (verbose_) {
        std::cout << "  [JIT] Completed in " << duration.count() << "us\n";
    }
    
    return Value::nil();
}

CompilationResult ExecutionPipeline::execute(const std::string& source_code,
                                              const std::string& source_name) {
    CompilationResult result;
    
    auto total_start = std::chrono::high_resolution_clock::now();
    
    // 1. 词法分析
    DiagnosticReporter reporter(source_name);
    auto tokens = lex(source_code, reporter);
    
    if (mode_ == ExecutionMode::Tokens) {
        result.success = !reporter.has_errors();
        result.tokens = tokens;
        return result;
    }
    
    if (reporter.has_errors()) {
        result.error_message = "Lexical errors occurred";
        reporter.print_diagnostics();
        return result;
    }
    
    // 2. 语法分析
    auto start = std::chrono::high_resolution_clock::now();
    auto ast = parse(tokens, reporter);
    auto end = std::chrono::high_resolution_clock::now();
    result.parse_time_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    
    if (mode_ == ExecutionMode::AST) {
        result.success = (ast != nullptr);
        result.ast = ast;
        return result;
    }
    
    if (!ast || reporter.has_errors()) {
        result.error_message = "Parse errors occurred";
        reporter.print_diagnostics();
        return result;
    }
    
    // 3. 字节码编译
    if (mode_ == ExecutionMode::Bytecode || mode_ == ExecutionMode::JIT) {
        auto bc_start = std::chrono::high_resolution_clock::now();
        result.bytecode_module = compile_to_bytecode(ast);
        auto bc_end = std::chrono::high_resolution_clock::now();
        result.codegen_time_us = std::chrono::duration_cast<std::chrono::microseconds>(bc_end - bc_start).count();
    }
    
    // 4. 执行
    Value exec_result = Value::nil();
    
    switch (mode_) {
        case ExecutionMode::Interpret:
            exec_result = run_interpreter(ast);
            break;
            
        case ExecutionMode::Bytecode:
            if (result.bytecode_module) {
                exec_result = run_bytecode(*result.bytecode_module);
            }
            break;
            
        case ExecutionMode::JIT:
            if (result.bytecode_module) {
                exec_result = run_jit(*result.bytecode_module);
            }
            break;
            
        case ExecutionMode::CCodeGen:
            {
                // 调用 C 代码生成器
                std::string c_code = run_c_codegen(ast);
                if (!c_code.empty()) {
                    // 将生成的 C 代码存储到结果中
                    result.generated_ccode = c_code;
                    if (verbose_) {
                        std::cout << "  [CCodeGen] Generated C code successfully\n";
                    }
                } else {
                    result.success = false;
                    result.error_message = "C code generation failed";
                }
            }
            break;
            
        default:
            break;
    }
    
    auto total_end = std::chrono::high_resolution_clock::now();
    auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start);
    result.execution_time_us = total_duration.count();
    
    result.success = true;
    return result;
}

CompilationResult ExecutionPipeline::execute_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return CompilationResult{false, "Cannot open file: " + filepath};
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    file.close();
    
    return execute(source, filepath);
}

// ============================================================================
// 便捷函数实现
// ============================================================================

CompilationResult execute_claw(const std::string& code, ExecutionMode mode) {
    ExecutionPipeline pipeline;
    pipeline.set_mode(mode);
    return pipeline.execute(code);
}

CompilationResult execute_claw_file(const std::string& filepath, ExecutionMode mode) {
    ExecutionPipeline pipeline;
    pipeline.set_mode(mode);
    return pipeline.execute_file(filepath);
}

} // namespace pipeline
} // namespace claw
