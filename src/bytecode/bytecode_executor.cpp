// BytecodeExecutor.cpp - 字节码执行器实现

#include "bytecode_executor.h"
#include "../lexer/lexer.h"
#include "../parser/parser.h"

namespace claw {

// ============================================================================
// BytecodeExecutor 实现
// ============================================================================

BytecodeExecutionResult BytecodeExecutor::execute(
    std::shared_ptr<ast::Program> program, 
    bool verbose) {
    
    BytecodeExecutionResult result;
    
    if (!program) {
        result.error_message = "Null program pointer";
        return result;
    }
    
    if (verbose) {
        std::cout << "[BytecodeExecutor] Starting bytecode execution...\n";
    }
    
    // 编译阶段
    auto compile_start = std::chrono::high_resolution_clock::now();
    auto module = compile_to_bytecode(program, result);
    auto compile_end = std::chrono::high_resolution_clock::now();
    result.stats.compile_time = std::chrono::duration_cast<std::chrono::microseconds>(
        compile_end - compile_start);
    
    if (!module) {
        // compile_to_bytecode 已经设置 error_message
        return result;
    }
    
    // 统计
    result.stats.functions_compiled = module->functions.size();
    for (const auto& func : module->functions) {
        result.stats.bytecode_instructions += func.code.size();
    }
    
    // 执行阶段
    auto exec_start = std::chrono::high_resolution_clock::now();
    result.return_value = execute_in_vm(*module, result);
    auto exec_end = std::chrono::high_resolution_clock::now();
    result.stats.execution_time = std::chrono::duration_cast<std::chrono::microseconds>(
        exec_end - exec_start);
    
    result.success = true;
    
    if (verbose) {
        std::cout << "[BytecodeExecutor] Execution completed successfully\n";
        std::cout << "  Compile time: " << result.stats.compile_time.count() << " us\n";
        std::cout << "  Execution time: " << result.stats.execution_time.count() << " us\n";
        std::cout << "  Functions: " << result.stats.functions_compiled << "\n";
        std::cout << "  Bytecode instructions: " << result.stats.bytecode_instructions << "\n";
    }
    
    return result;
}

BytecodeExecutionResult BytecodeExecutor::execute_from_source(
    const std::string& source, 
    bool verbose) {
    
    BytecodeExecutionResult result;
    
    if (source.empty()) {
        result.error_message = "Empty source code";
        return result;
    }
    
    if (verbose) {
        std::cout << "[BytecodeExecutor] Loading source (" << source.size() << " bytes)\n";
    }
    
    // 词法分析
    Lexer lexer(source);
    auto tokens = lexer.scan_all();
    
    if (verbose) {
        std::cout << "[BytecodeExecutor] Lexed " << tokens.size() << " tokens\n";
    }
    
    // 语法分析
    Parser parser(tokens);
    auto program = parser.parse();
    
    if (!program) {
        result.error_message = "Parse failed";
        return result;
    }
    
    if (verbose) {
        std::cout << "[BytecodeExecutor] Parsed " << program->get_declarations().size() 
                  << " declarations\n";
    }
    
    // 执行编译和运行
    return execute(std::shared_ptr<ast::Program>(std::move(program)), verbose);
}

std::shared_ptr<bytecode::Module> BytecodeExecutor::compile_to_bytecode(
    std::shared_ptr<ast::Program> program, 
    BytecodeExecutionResult& result) {
    
    try {
        // 创建编译器
        BytecodeCompiler compiler;
        compiler.setDebugInfo(debug_);
        
        // 编译
        auto module = compiler.compile(*program);
        
        if (!module) {
            result.error_message = "Compilation failed: " + compiler.getLastError();
            return nullptr;
        }
        
        if (debug_) {
            std::cout << "[BytecodeExecutor] Compiled " << module->functions.size() 
                      << " functions\n";
        }
        
        return module;
        
    } catch (const std::exception& e) {
        result.error_message = std::string("Compilation exception: ") + e.what();
        return nullptr;
    }
}

vm::Value BytecodeExecutor::execute_in_vm(
    const bytecode::Module& module, 
    BytecodeExecutionResult& result) {
    
    try {
        // 创建虚拟机
        vm::ClawVM vm;
        
        // 加载模块
        if (!vm.load_module(module)) {
            result.error_message = "Failed to load module into VM: " + vm.last_error;
            return vm::Value::nil();
        }
        
        if (debug_) {
            std::cout << "[BytecodeExecutor] Loaded module into VM\n";
        }
        
        // 执行
        vm::Value return_value = vm.execute();
        
        // 获取执行统计
        result.stats.vm_instructions_executed = vm.instructions_executed;
        
        if (debug_) {
            std::cout << "[BytecodeExecutor] Executed " << vm.instructions_executed 
                      << " VM instructions\n";
        }
        
        return return_value;
        
    } catch (const std::exception& e) {
        result.error_message = std::string("Runtime exception: ") + e.what();
        return vm::Value::nil();
    }
}

} // namespace claw
