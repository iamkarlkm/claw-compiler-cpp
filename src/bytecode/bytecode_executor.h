// BytecodeExecutor - 独立的字节码执行管道
// 修复 Bytecode 模式执行问题的完整解决方案
// 
// 功能:
// 1. 从 shared_ptr<Program> 编译到字节码
// 2. 正确加载到 VM 并执行
// 3. 返回执行结果和统计信息

#ifndef CLAW_BYTECODE_EXECUTOR_H
#define CLAW_BYTECODE_EXECUTOR_H

#include <memory>
#include <string>
#include <iostream>
#include <chrono>
#include "../ast/ast.h"
#include "../bytecode/bytecode.h"
#include "../bytecode/bytecode_compiler.h"
#include "../vm/claw_vm.h"

namespace claw {

// ============================================================================
// 执行结果
// ============================================================================

struct BytecodeExecutionResult {
    bool success = false;
    std::string error_message;
    vm::Value return_value;
    
    // 统计信息
    struct Stats {
        std::chrono::microseconds compile_time;
        std::chrono::microseconds execution_time;
        size_t functions_compiled = 0;
        size_t bytecode_instructions = 0;
        uint64_t vm_instructions_executed = 0;
    };
    Stats stats;
};

// ============================================================================
// 字节码执行器
// ============================================================================

class BytecodeExecutor {
public:
    BytecodeExecutor() = default;
    ~BytecodeExecutor() = default;
    
    /**
     * @brief 执行字节码模式
     * @param program 解析后的 AST 程序
     * @param verbose 是否输出详细信息
     * @return 执行结果
     */
    BytecodeExecutionResult execute(std::shared_ptr<ast::Program> program, bool verbose = false);
    
    /**
     * @brief 执行字节码模式 (从源代码)
     * @param source 源代码字符串
     * @param verbose 是否输出详细信息
     * @return 执行结果
     */
    BytecodeExecutionResult execute_from_source(const std::string& source, bool verbose = false);
    
    /**
     * @brief 设置调试模式
     */
    void set_debug(bool enable) { debug_ = enable; }

private:
    bool debug_ = false;
    
    /**
     * @brief 编译 AST 到字节码模块
     */
    std::shared_ptr<bytecode::Module> compile_to_bytecode(
        std::shared_ptr<ast::Program> program, 
        BytecodeExecutionResult& result);
    
    /**
     * @brief 在 VM 中执行字节码
     */
    vm::Value execute_in_vm(
        const bytecode::Module& module, 
        BytecodeExecutionResult& result);
};

// ============================================================================
// 便捷函数
// ============================================================================

/**
 * @brief 一行代码执行 Claw 源码 (字节码模式)
 */
inline BytecodeExecutionResult run_bytecode(
    const std::string& source, 
    bool verbose = false) {
    BytecodeExecutor executor;
    executor.set_debug(verbose);
    return executor.execute_from_source(source, verbose);
}

/**
 * @brief 从文件执行 (字节码模式)
 */
inline BytecodeExecutionResult run_bytecode_file(
    const std::string& filepath,
    bool verbose = false) {
    // 读取文件
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return {false, "Cannot open file: " + filepath, vm::Value::nil(), {}};
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    
    BytecodeExecutor executor;
    executor.set_debug(verbose);
    return executor.execute_from_source(source, verbose);
}

} // namespace claw

#endif // CLAW_BYTECODE_EXECUTOR_H
