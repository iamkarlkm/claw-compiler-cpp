// pipeline/execution_pipeline.h - 统一执行管道
// 桥接 Lexer → Parser → Bytecode → VM/JIT 的执行流水线

#ifndef CLAW_EXECUTION_PIPELINE_H
#define CLAW_EXECUTION_PIPELINE_H

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include "../lexer/lexer.h"
#include "../lexer/token.h"
#include "../parser/parser.h"
#include "../common/common.h"
#include "../bytecode/bytecode.h"
#include "../interpreter/interpreter.h"

namespace claw {
namespace pipeline {

// 导入 interpreter::Value 类型
using Value = interpreter::Value;

// ============================================================================
// 执行模式
// ============================================================================

enum class ExecutionMode {
    Tokens,      // 仅词法分析
    AST,         // 仅解析到 AST
    Interpret,   // AST 解释器
    Bytecode,    // 字节码 + VM
    JIT,         // JIT 编译执行
    CCodeGen     // C 代码生成
};

// ============================================================================
// 编译结果
// ============================================================================

struct CompilationResult {
    bool success = false;
    std::string error_message;
    
    // 词法分析结果
    std::vector<Token> tokens;
    
    // 解析结果
    std::shared_ptr<ast::Program> ast;
    
    // 字节码结果
    std::unique_ptr<bytecode::Module> bytecode_module;
    
    // C 代码生成结果
    std::string generated_ccode;
    
    // 统计信息
    size_t lex_time_us = 0;
    size_t parse_time_us = 0;
    size_t codegen_time_us = 0;
    size_t execution_time_us = 0;
};

// ============================================================================
// 统一执行管道
// ============================================================================

class ExecutionPipeline {
public:
    ExecutionPipeline();
    ~ExecutionPipeline();
    
    // 设置执行模式
    void set_mode(ExecutionMode mode) { mode_ = mode; }
    ExecutionMode get_mode() const { return mode_; }
    
    // 设置选项
    void set_verbose(bool v) { verbose_ = v; }
    void set_show_ir(bool v) { show_ir_ = v; }
    void set_optimize_level(int level) { opt_level_ = level; }
    
    // 执行编译流程
    CompilationResult execute(const std::string& source_code, 
                              const std::string& source_name = "<input>");
    
    // 从文件执行
    CompilationResult execute_file(const std::string& filepath);
    
    // 获取解释器结果 (用于 Interpret 模式)
    Value get_interpreter_result() const { return interpreter_result_; }
    
    // 获取 VM 结果 (用于 Bytecode 模式)
    Value get_vm_result() const { return vm_result_; }

private:
    ExecutionMode mode_ = ExecutionMode::Interpret;
    bool verbose_ = false;
    bool show_ir_ = false;
    int opt_level_ = 0;
    
    // 运行时结果
    Value interpreter_result_;
    Value vm_result_;
    
    // 内部方法
    std::vector<Token> lex(const std::string& source, DiagnosticReporter& reporter);
    std::shared_ptr<ast::Program> parse(const std::vector<Token>& tokens, 
                                         DiagnosticReporter& reporter);
    std::unique_ptr<bytecode::Module> compile_to_bytecode(std::shared_ptr<ast::Program> ast);
    Value run_interpreter(std::shared_ptr<ast::Program> ast);
    Value run_bytecode(const bytecode::Module& module);
    Value run_jit(const bytecode::Module& module);
    std::string run_c_codegen(std::shared_ptr<ast::Program> ast);
};

// ============================================================================
// 便捷函数
// ============================================================================

// 从字符串创建管道并执行
CompilationResult execute_claw(const std::string& code, ExecutionMode mode = ExecutionMode::Interpret);

// 从文件创建管道并执行
CompilationResult execute_claw_file(const std::string& filepath, ExecutionMode mode = ExecutionMode::Interpret);

} // namespace pipeline
} // namespace claw

#endif // CLAW_EXECUTION_PIPELINE_H
