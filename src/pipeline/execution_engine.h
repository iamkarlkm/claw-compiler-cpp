// pipeline/execution_engine.h - 多模式执行引擎 (完整版)
// 支持: AST 解释执行 / 字节码解释执行 / JIT 编译执行 / 混合执行

#ifndef CLAW_EXECUTION_ENGINE_H
#define CLAW_EXECUTION_ENGINE_H

#include <memory>
#include <string>
#include <chrono>
#include <vector>
#include <unordered_map>
#include <variant>
#include <optional>

#include "../common/common.h"
#include "../bytecode/bytecode.h"
#include "../ast/ast.h"
#include "../type/type_system.h"

// 前向声明 - 避免循环依赖
namespace claw {
namespace ast {
    class ASTNode;
    class Program;
    using ASTNodePtr = std::shared_ptr<ASTNode>;
}
namespace bytecode {
    struct Module;
    struct Function;
}
namespace vm {
    class ClawVM;
}
namespace jit {
    class JITCompiler;
}
}

namespace claw {

// ============================================================================
// JIT 编译后的函数类型定义 (System V AMD64 ABI)
// ============================================================================

// JIT 编译后的函数指针类型
// 返回值通过 RAX/XMM0传递
// 参数通过 RDI, RSI, RDX, RCX, R8, R9 传递
using JITFunction = int64_t(*)(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);

// ============================================================================
// 执行模式
// ============================================================================

enum class ExecutionMode {
    AST_INTERPRET,      // AST 直译 (开发调试用)
    BYTECODE_INTERPRET, // 字节码解释 (默认)
    JIT_COMPILED,       // JIT 编译执行 (高性能)
    HYBRID              // 混合: 解释冷路径 + JIT 热路径
};

// 执行配置
struct ExecutionConfig {
    ExecutionMode mode = ExecutionMode::BYTECODE_INTERPRET;
    bool enable_method_jit = true;
    bool enable_optimizing_jit = true;
    size_t hot_threshold = 1000;
    bool trace_execution = false;
    bool dump_bytecode = false;
    bool enable_gc = true;
    size_t stack_size = 1024 * 1024;  // 1MB 栈
};

// 执行结果
struct ExecutionResult {
    bool success = false;
    int exit_code = 0;
    std::string output;
    std::string error_message;
    
    struct PerformanceStats {
        std::chrono::microseconds total_time{};
        size_t instructions_executed = 0;
        size_t function_calls = 0;
        size_t jit_compilations = 0;
        size_t gc_cycles = 0;
    };
    PerformanceStats stats;
};

// 函数调用信息
struct CallInfo {
    std::string function_name;
    size_t call_count = 0;
    size_t total_time_us = 0;
};

// ============================================================================
// 热点函数分析器
// ============================================================================

class HotFunctionAnalyzer {
public:
    HotFunctionAnalyzer();
    ~HotFunctionAnalyzer();
    
    void record_call(const std::string& func_name, size_t time_us);
    void record_iteration(const std::string& func_name);
    std::vector<CallInfo> get_hot_functions(size_t top_n) const;
    bool should_jit_compile(const std::string& func_name, size_t threshold) const;
    void reset();
    void clear();
    
    // 热点检测
    bool is_hot(const std::string& func_name) const;
    size_t get_call_count(const std::string& func_name) const;

private:
    struct FunctionProfile {
        size_t call_count = 0;
        size_t iteration_count = 0;
        size_t total_time_us = 0;
        std::chrono::steady_clock::time_point last_call_time;
    };
    std::unordered_map<std::string, FunctionProfile> profiles_;
    mutable std::mutex mutex_;
};

// ============================================================================
// 编译器接口 - 统一不同前端
// ============================================================================

class CompilerBackend {
public:
    CompilerBackend();
    ~CompilerBackend();
    
    // 编译源码到字节码
    bool compile(const std::string& source, std::string& error_out);
    
    // 获取编译后的模块
    std::shared_ptr<bytecode::Module> get_module() const;
    
    // 获取错误信息
    std::string get_last_error() const;
    
    // 调试: 打印 AST
    std::string dump_ast() const;
    
    // 调试: 打印字节码
    std::string dump_bytecode() const;

private:
    std::shared_ptr<ast::ASTNode> ast_root_;
    std::shared_ptr<bytecode::Module> compiled_module_;
    std::string last_error_;
    DiagnosticReporter reporter_;  // 诊断报告器
    
    // 内部编译步骤
    bool parse_source(const std::string& source);
    bool type_check();
    bool generate_bytecode();
};

// ============================================================================
// 解释器接口
// ============================================================================

class ASTInterpreter {
public:
    ASTInterpreter();
    ~ASTInterpreter();
    
    // 设置 AST 根节点
    void set_ast(std::shared_ptr<ast::ASTNode> root);
    
    // 执行
    ExecutionResult execute(const std::string& entry = "main");
    
    // 调试跟踪
    void set_trace(bool enable) { trace_enabled_ = enable; }

private:
    std::shared_ptr<ast::ASTNode> ast_root_;
    std::unordered_map<std::string, std::shared_ptr<ast::ASTNode>> functions_;
    bool trace_enabled_ = false;
    
    // 执行辅助
    ExecutionResult execute_function(const std::string& name);
};

// ============================================================================
// 主执行引擎 (完整版)
// ============================================================================

class ExecutionEngine {
public:
    explicit ExecutionEngine(const ExecutionConfig& config = ExecutionConfig());
    ~ExecutionEngine();
    
    // 禁用拷贝
    ExecutionEngine(const ExecutionEngine&) = delete;
    ExecutionEngine& operator=(const ExecutionEngine&) = delete;
    
    // 允许移动
    ExecutionEngine(ExecutionEngine&&) noexcept;
    ExecutionEngine& operator=(ExecutionEngine&&) noexcept;
    
    // 加载源码
    bool load_source(const std::string& source);
    
    // 加载文件
    bool load_file(const std::string& filepath);
    
    // 加载字节码模块 [NEW]
    bool load_module(const bytecode::Module& module);
    
    // 执行入口点
    ExecutionResult execute(const std::string& entry_function = "main");
    
    // 执行模式切换
    void set_mode(ExecutionMode mode);
    ExecutionMode get_mode() const { return config_.mode; }
    
    // 性能分析
    HotFunctionAnalyzer& get_hot_analyzer() { return *hot_analyzer_; }
    const HotFunctionAnalyzer& get_hot_analyzer() const { return *hot_analyzer_; }
    
    // 统计信息
    struct EngineStats {
        size_t bytecode_instructions_executed = 0;
        size_t jit_compilations = 0;
        size_t inline_cache_hits = 0;
        size_t osr_count = 0;
    };
    EngineStats get_stats() const;
    void reset_stats();
    
    // 配置
    void update_config(const ExecutionConfig& config);
    const ExecutionConfig& get_config() const { return config_; }
    
    // 清理
    void clear_caches();
    
    // 获取详细的编译诊断信息
    std::string get_compilation_diagnostics(const std::string& source);
    
    // 增强版加载源码（带完整语义/类型检查）
    bool load_source_with_checks(const std::string& source, 
                                  void* check_result = nullptr);
    std::shared_ptr<jit::JITCompiler> get_jit() const { return jit_compiler_; }
    std::shared_ptr<bytecode::Module> get_module() const { return compiled_module_; }

private:
    ExecutionConfig config_;
    EngineStats stats_;
    std::unique_ptr<HotFunctionAnalyzer> hot_analyzer_;  // 使用 unique_ptr 以支持移动
    
    // 编译后端
    std::unique_ptr<CompilerBackend> compiler_backend_;
    
    // 编译后的字节码模块
    std::shared_ptr<bytecode::Module> compiled_module_;
    
    // 虚拟机 (字节码解释用)
    std::shared_ptr<vm::ClawVM> vm_;
    
    // JIT 编译器
    std::shared_ptr<jit::JITCompiler> jit_compiler_;
    
    // AST 解释器
    std::unique_ptr<ASTInterpreter> ast_interpreter_;
    
    // JIT 编译后的函数注册表 (函数名 → 机器码函数指针)
    std::unordered_map<std::string, JITFunction> jit_functions_;
    
    // JIT 运行时栈 (用于保存/恢复执行上下文)
    std::vector<int64_t> jit_runtime_stack_;
    
    // 内部状态
    bool source_loaded_ = false;
    std::string last_error_;
    
    // 内部执行方法
    ExecutionResult execute_ast_interpret();
    ExecutionResult execute_bytecode_interpret();
    ExecutionResult execute_jit();
    ExecutionResult execute_hybrid();
    
    // JIT 执行辅助方法
    bool register_jit_function(const std::string& name, void* code);
    JITFunction get_jit_function(const std::string& name) const;
    ExecutionResult execute_jit_function(JITFunction func, const std::vector<int64_t>& args);
    
    // 辅助方法
    bool compile_source_to_bytecode(const std::string& source);
    bool init_vm();
    bool init_jit();
    void setup_builtin_functions();
};

// ============================================================================
// 便捷函数
// ============================================================================

// 一行代码执行 Claw 源码
ExecutionResult run_claw_source(const std::string& source,
                                const ExecutionConfig& config = ExecutionConfig());

// 从文件执行
ExecutionResult run_claw_file(const std::string& filepath,
                              const ExecutionConfig& config = ExecutionConfig());

// ============================================================================
// 执行模式工具函数
// ============================================================================

// 获取执行模式名称
std::string execution_mode_to_string(ExecutionMode mode);

// 解析执行模式字符串
std::optional<ExecutionMode> string_to_execution_mode(const std::string& s);

} // namespace claw

#endif // CLAW_EXECUTION_ENGINE_H
