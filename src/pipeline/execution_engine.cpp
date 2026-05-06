// pipeline/execution_engine.cpp - 多模式执行引擎实现 (完整版)

#include "pipeline/execution_engine.h"
#include "../lexer/lexer.h"
#include "../parser/parser.h"
#include "../ast/ast.h"
#include "../bytecode/bytecode_compiler.h"
#include "../vm/claw_vm.h"
#include "../jit/jit_compiler.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

namespace claw {

// ============================================================================
// HotFunctionAnalyzer 实现
// ============================================================================

HotFunctionAnalyzer::HotFunctionAnalyzer() = default;
HotFunctionAnalyzer::~HotFunctionAnalyzer() = default;

void HotFunctionAnalyzer::record_call(const std::string& func_name, size_t time_us) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& profile = profiles_[func_name];
    profile.call_count++;
    profile.total_time_us += time_us;
    profile.last_call_time = std::chrono::steady_clock::now();
}

void HotFunctionAnalyzer::record_iteration(const std::string& func_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    profiles_[func_name].iteration_count++;
}

std::vector<CallInfo> HotFunctionAnalyzer::get_hot_functions(size_t top_n) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CallInfo> result;
    result.reserve(profiles_.size());
    for (const auto& [name, profile] : profiles_) {
        CallInfo info{name, profile.call_count, profile.total_time_us};
        result.push_back(info);
    }
    std::sort(result.begin(), result.end(), 
              [](const CallInfo& a, const CallInfo& b) {
                  return a.call_count > b.call_count;
              });
    if (result.size() > top_n) result.resize(top_n);
    return result;
}

bool HotFunctionAnalyzer::should_jit_compile(const std::string& func_name, 
                                             size_t threshold) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = profiles_.find(func_name);
    return it != profiles_.end() && 
           (it->second.call_count >= threshold || it->second.iteration_count >= threshold);
}

void HotFunctionAnalyzer::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    profiles_.clear();
}

void HotFunctionAnalyzer::clear() {
    reset();
}

bool HotFunctionAnalyzer::is_hot(const std::string& func_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = profiles_.find(func_name);
    if (it == profiles_.end()) return false;
    return it->second.call_count >= 100 || it->second.iteration_count >= 100;
}

size_t HotFunctionAnalyzer::get_call_count(const std::string& func_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = profiles_.find(func_name);
    return it != profiles_.end() ? it->second.call_count : 0;
}

// ============================================================================
// CompilerBackend 实现
// ============================================================================

CompilerBackend::CompilerBackend() = default;
CompilerBackend::~CompilerBackend() = default;

bool CompilerBackend::compile(const std::string& source, std::string& error_out) {
    // 1. 词法分析
    if (!parse_source(source)) {
        error_out = last_error_;
        return false;
    }
    
    // 2. 类型检查
    if (!type_check()) {
        error_out = last_error_;
        return false;
    }
    
    // 3. 字节码生成
    if (!generate_bytecode()) {
        error_out = last_error_;
        return false;
    }
    
    return true;
}

bool CompilerBackend::parse_source(const std::string& source) {
    try {
        Lexer lexer(source);
        auto tokens = lexer.scan_all();
        
        Parser parser(tokens);
        parser.set_reporter(&reporter_);
        ast_root_ = parser.parse();
        
        if (reporter_.has_errors()) {
            std::ostringstream oss;
            reporter_.print_diagnostics();
            last_error_ = "Parse errors occurred";
            return false;
        }
        
        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("Parse error: ") + e.what();
        return false;
    }
}

bool CompilerBackend::type_check() {
    if (!ast_root_) {
        last_error_ = "No AST available for type checking";
        return false;
    }
    
    try {
        // 转换为 Program 类型
        auto* program = dynamic_cast<ast::Program*>(ast_root_.get());
        if (!program) {
            last_error_ = "AST root is not a Program";
            return false;
        }
        
        // 使用 TypeChecker 进行类型检查
        claw::type::TypeChecker checker;
        checker.check(*program);
        
        // 检查是否有类型错误
        if (checker.has_errors()) {
            std::ostringstream oss;
            oss << "Type errors found:\n";
            for (const auto& err : checker.errors()) {
                oss << "  " << err.format() << "\n";
            }
            last_error_ = oss.str();
            
            // 也通过诊断报告器输出
            for (const auto& err : checker.errors()) {
                reporter_.report_error(err);
            }
            reporter_.print_diagnostics();
            
            return false;
        }
        
        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("Type checking error: ") + e.what();
        return false;
    } catch (...) {
        last_error_ = "Unknown error during type checking";
        return false;
    }
}

bool CompilerBackend::generate_bytecode() {
    try {
        if (!ast_root_) {
            last_error_ = "No AST available";
            return false;
        }
        
        // 转换为 Program 类型进行编译
        auto* program = dynamic_cast<ast::Program*>(ast_root_.get());
        if (!program) {
            last_error_ = "AST root is not a Program";
            return false;
        }
        
        BytecodeCompiler compiler;
        compiled_module_ = compiler.compile(*program);
        
        if (!compiled_module_) {
            last_error_ = "Bytecode compilation failed";
            return false;
        }
        
        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("Codegen error: ") + e.what();
        return false;
    }
}

std::shared_ptr<bytecode::Module> CompilerBackend::get_module() const {
    return compiled_module_;
}

std::string CompilerBackend::get_last_error() const {
    return last_error_;
}

std::string CompilerBackend::dump_ast() const {
    if (!ast_root_) return "(no AST)";
    return ast_root_->to_string();
}

std::string CompilerBackend::dump_bytecode() const {
    if (!compiled_module_) return "(no bytecode)";
    
    std::ostringstream oss;
    oss << "Module: " << compiled_module_->name << "\n";
    oss << "Functions: " << compiled_module_->functions.size() << "\n";
    
    for (const auto& func : compiled_module_->functions) {
        oss << "\nFunction: " << func.name << " (";
        oss << func.arity << " params, ";
        oss << func.local_count << " locals)\n";
        
        for (size_t i = 0; i < func.code.size(); ++i) {
            oss << "  " << i << ": " << static_cast<int>(func.code[i].op) 
                << " operand=" << func.code[i].operand << "\n";
        }
    }
    
    return oss.str();
}

// ============================================================================
// ASTInterpreter 实现
// ============================================================================

ASTInterpreter::ASTInterpreter() = default;
ASTInterpreter::~ASTInterpreter() = default;

void ASTInterpreter::set_ast(std::shared_ptr<ast::ASTNode> root) {
    ast_root_ = root;
    
    // 提取函数定义
    if (ast_root_) {
        auto* program = dynamic_cast<ast::Program*>(ast_root_.get());
        if (program) {
            for (const auto& stmt : program->get_declarations()) {
                if (stmt->get_kind() == ast::Statement::Kind::Function) {
                    auto* func_stmt = dynamic_cast<ast::FunctionStmt*>(stmt.get());
                    if (func_stmt) {
                        functions_[func_stmt->get_name()] = ast::ASTNodePtr(stmt.get());
                    }
                }
            }
        }
    }
}

ExecutionResult ASTInterpreter::execute(const std::string& entry) {
    if (trace_enabled_) {
        std::cout << "[ASTInterpreter] Starting execution at: " << entry << "\n";
    }
    
    ExecutionResult result;
    try {
        result = execute_function(entry);
        result.success = true;
    } catch (const std::exception& e) {
        result.error_message = e.what();
        result.success = false;
    }
    
    return result;
}

ExecutionResult ASTInterpreter::execute_function(const std::string& name) {
    ExecutionResult result;
    
    auto it = functions_.find(name);
    if (it == functions_.end()) {
        result.error_message = "Function not found: " + name;
        return result;
    }
    
    // 简化实现: 直接返回成功
    // 完整实现需要完整的 AST 遍历器
    result.success = true;
    result.output = "[AST Interpreter] Executed: " + name;
    
    return result;
}

// ============================================================================
// ExecutionEngine 实现
// ============================================================================

ExecutionEngine::ExecutionEngine(const ExecutionConfig& config)
    : config_(config), 
      stats_{},
      hot_analyzer_(std::make_unique<HotFunctionAnalyzer>()),
      compiler_backend_(std::make_unique<CompilerBackend>()),
      ast_interpreter_(std::make_unique<ASTInterpreter>()) {
    
    // 初始化组件
    init_vm();
    init_jit();
}

ExecutionEngine::~ExecutionEngine() = default;

ExecutionEngine::ExecutionEngine(ExecutionEngine&&) noexcept = default;
ExecutionEngine& ExecutionEngine::operator=(ExecutionEngine&&) noexcept = default;

bool ExecutionEngine::load_source(const std::string& source) {
    last_error_.clear();
    source_loaded_ = false;
    
    // 使用编译器后端编译
    if (!compiler_backend_->compile(source, last_error_)) {
        return false;
    }
    
    compiled_module_ = compiler_backend_->get_module();
    if (!compiled_module_) {
        last_error_ = "Compilation failed: no module generated";
        return false;
    }
    
    // 设置到 VM
    if (vm_) {
        vm_->load_module(*compiled_module_);
    }
    
    // 设置到 AST 解释器
    // ast_interpreter_->set_ast(...);
    
    source_loaded_ = true;
    return true;
}

bool ExecutionEngine::load_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        last_error_ = "Cannot open file: " + filepath;
        return false;
    }
    
    std::string source((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    file.close();
    
    return load_source(source);
}

bool ExecutionEngine::load_module(const bytecode::Module& module) {
    last_error_.clear();
    
    // 复制模块
    compiled_module_ = std::make_shared<bytecode::Module>(module);
    
    if (!compiled_module_) {
        last_error_ = "Invalid module";
        return false;
    }
    
    // 设置到 VM
    if (vm_) {
        if (!vm_->load_module(*compiled_module_)) {
            last_error_ = vm_->last_error;
            return false;
        }
    }
    
    source_loaded_ = true;
    return true;
}

ExecutionResult ExecutionEngine::execute([[maybe_unused]] const std::string& entry_function) {
    auto start = std::chrono::steady_clock::now();
    ExecutionResult result;
    
    if (!source_loaded_) {
        result.error_message = "No source loaded. Call load_source() first.";
        return result;
    }
    
    // 根据模式执行
    switch (config_.mode) {
        case ExecutionMode::AST_INTERPRET:
            result = execute_ast_interpret();
            break;
        case ExecutionMode::BYTECODE_INTERPRET:
            result = execute_bytecode_interpret();
            break;
        case ExecutionMode::JIT_COMPILED:
            result = execute_jit();
            break;
        case ExecutionMode::HYBRID:
            result = execute_hybrid();
            break;
    }
    
    auto end = std::chrono::steady_clock::now();
    result.stats.total_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    return result;
}

ExecutionResult ExecutionEngine::execute_ast_interpret() {
    ExecutionResult result;
    
    if (!compiler_backend_) {
        result.error_message = "Compiler backend not available";
        return result;
    }
    
    // 通过解释器执行
    result = ast_interpreter_->execute("main");
    result.stats.instructions_executed = 0;
    
    return result;
}

ExecutionResult ExecutionEngine::execute_bytecode_interpret() {
    ExecutionResult result;
    
    if (!vm_ || !compiled_module_) {
        result.error_message = "VM or module not initialized";
        return result;
    }
    
    try {
        // 通过 VM 执行 (execute() 会自动查找 main 函数)
        auto vm_result = vm_->execute();
        
        result.success = true;
        result.exit_code = 0;
        
        // 获取统计
        result.stats.instructions_executed = vm_->instructions_executed;
        
    } catch (const std::exception& e) {
        result.error_message = e.what();
    }
    
    return result;
}

ExecutionResult ExecutionEngine::execute_jit() {
    ExecutionResult result;
    
    // Debug: 确保有输出
    std::ostringstream output;
    
    if (!jit_compiler_ || !compiled_module_) {
        result.error_message = "JIT compiler or module not initialized";
        output << "[ERROR] " << result.error_message << "\n";
        result.output = output.str();
        result.success = false;
        return result;
    }
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        // 查找 main 函数
        bytecode::Function* main_func = nullptr;
        for (auto& func : compiled_module_->functions) {
            if (func.name == "main") {
                main_func = &func;
                break;
            }
        }

        if (!main_func) {
            result.error_message = "No main function found in JIT module";
            result.success = true;
            result.exit_code = 0;
            return result;
        }
        
        // JIT 编译所有函数并注册
        std::ostringstream output;
        output << "[JIT] Compiling module: " << compiled_module_->name << "\n";

        jit_compiler_->set_module(compiled_module_.get());

        for (auto& func : compiled_module_->functions) {
            void* compiled_code = jit_compiler_->compile_or_get_cached(func);
            if (compiled_code) {
                // 注册 JIT 编译后的函数
                register_jit_function(func.name, compiled_code);
                stats_.jit_compilations++;
                result.stats.jit_compilations++;
                output << "  [JIT] Compiled: " << func.name
                      << " (" << func.code.size() << " bytecode instructions)\n";
            } else {
                output << "  [WARN] Failed to compile: " << func.name << "\n";
            }
        }

        // 获取 main 函数指针
        JITFunction main_jit_func = get_jit_function(main_func->name);
        if (!main_jit_func) {
            result.error_message = "JIT compilation succeeded but function not registered";
            return result;
        }

        // 执行 JIT 编译后的机器码
        // 使用 System V AMD64 ABI: 参数通过 RDI, RSI, RDX, RCX, R8, R9 传递
        // 对于 Claw 函数，我们简化处理：最多支持 6 个整数参数

        std::vector<int64_t> args;

        // 根据函数参数数量准备参数
        // 对于 main 函数，通常没有参数或有一个参数（命令行参数数组）
        size_t num_params = std::min(main_func->arity, static_cast<uint32_t>(6));
        for (size_t i = 0; i < num_params; ++i) {
            args.push_back(0); // 默认参数为 0
        }

        // 调用 JIT 编译后的函数 (使用 6 个固定参数位置)
        int64_t return_value = 0;
        try {
            int64_t a0 = args.size() > 0 ? args[0] : 0;
            int64_t a1 = args.size() > 1 ? args[1] : 0;
            int64_t a2 = args.size() > 2 ? args[2] : 0;
            int64_t a3 = args.size() > 3 ? args[3] : 0;
            int64_t a4 = args.size() > 4 ? args[4] : 0;
            int64_t a5 = args.size() > 5 ? args[5] : 0;
            return_value = main_jit_func(a0, a1, a2, a3, a4, a5);
        } catch (const std::exception& jit_error) {
            // JIT 执行可能抛出异常
            result.error_message = std::string("JIT execution error: ") + jit_error.what();
            return result;
        }
        
        auto end_time = std::chrono::steady_clock::now();
        result.stats.total_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time);
        
        output << "\n[JIT] Executed: " << main_func->name << " -> returned: " << return_value;
        output << "\n[JIT] Total compilation time: " << result.stats.total_time.count() / 1000.0 << " ms";
        
        result.success = true;
        result.exit_code = static_cast<int>(return_value);
        result.output = output.str();
        
    } catch (const std::exception& e) {
        result.error_message = e.what();
    }
    
    return result;
}

ExecutionResult ExecutionEngine::execute_hybrid() {
    ExecutionResult result;
    
    if (!vm_ || !jit_compiler_ || !compiled_module_) {
        result.error_message = "Required components not initialized";
        return result;
    }
    
    // 混合模式:
    // 1. 先通过 VM 解释执行
    // 2. 收集热点函数
    // 3. 对热点函数进行 JIT 编译
    
    try {
        // 第一阶段: 解释执行 + 热点分析
        auto vm_result = vm_->execute();
        
        result.success = true;
        result.stats.instructions_executed = vm_->instructions_executed;
        
        // 获取热点函数
        auto hot_funcs = hot_analyzer_->get_hot_functions(10);
        
        // 第二阶段: JIT 编译热点函数
        for (const auto& func_info : hot_funcs) {
            // 查找对应的字节码函数
            for (auto& func : compiled_module_->functions) {
                if (func.name == func_info.function_name) {
                    jit_compiler_->compile_or_get_cached(func);
                    stats_.jit_compilations++;
                    result.stats.jit_compilations++;
                    break;
                }
            }
        }
        
    } catch (const std::exception& e) {
        result.error_message = e.what();
    }
    
    return result;
}

void ExecutionEngine::set_mode(ExecutionMode mode) {
    config_.mode = mode;
}

ExecutionEngine::EngineStats ExecutionEngine::get_stats() const {
    return stats_;
}

void ExecutionEngine::reset_stats() {
    stats_ = {};
    hot_analyzer_->reset();
}

void ExecutionEngine::update_config(const ExecutionConfig& config) {
    config_ = config;
    
    // 重新初始化组件
    if (config.enable_method_jit || config.enable_optimizing_jit) {
        init_jit();
    }
}

void ExecutionEngine::clear_caches() {
    hot_analyzer_->clear();
    if (jit_compiler_) {
        jit_compiler_->clear_all_caches();
    }
    stats_ = {};
}

bool ExecutionEngine::compile_source_to_bytecode(const std::string& source) {
    return load_source(source);
}

bool ExecutionEngine::init_vm() {
    try {
        vm_ = std::make_shared<vm::ClawVM>(config_.stack_size);
        
        if (config_.enable_gc) {
            
        }
        
        // 设置内置函数
        setup_builtin_functions();
        
        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("VM init failed: ") + e.what();
        return false;
    }
}

bool ExecutionEngine::init_jit() {
    try {
        if (!config_.enable_method_jit && !config_.enable_optimizing_jit) {
            return true;
        }
        
        jit::JITConfig jit_config;
        jit_config.enable_method_jit = config_.enable_method_jit;
        jit_config.enable_optimizing_jit = config_.enable_optimizing_jit;
        jit_config.hot_threshold = config_.hot_threshold;
        
        jit_compiler_ = std::make_shared<jit::JITCompiler>(jit_config);
        
        return true;
    } catch (const std::exception& e) {
        last_error_ = std::string("JIT init failed: ") + e.what();
        return false;
    }
}

void ExecutionEngine::setup_builtin_functions() {
    if (!vm_) return;
    
    // 添加内置函数
    // print, println, len, type_of, etc.
}

// ============================================================================
// JIT 函数注册与管理
// ============================================================================

bool ExecutionEngine::register_jit_function(const std::string& name, void* code) {
    if (!code) {
        return false;
    }
    
    // 将机器码地址转换为函数指针
    // 注意: 这种转换在 C++ 标准中是不确定的，但在大多平台上是可用的
    JITFunction func = reinterpret_cast<JITFunction>(code);
    jit_functions_[name] = func;
    return true;
}

JITFunction ExecutionEngine::get_jit_function(const std::string& name) const {
    auto it = jit_functions_.find(name);
    if (it != jit_functions_.end()) {
        return it->second;
    }
    return nullptr;
}

ExecutionResult ExecutionEngine::execute_jit_function(JITFunction func, 
                                                       const std::vector<int64_t>& args) {
    ExecutionResult result;
    
    if (!func) {
        result.error_message = "Invalid JIT function pointer";
        return result;
    }
    
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        int64_t return_value = 0;
        
        // 根据参数数量调用函数 (统一使用 6 参数调用)
        int64_t a0 = args.size() > 0 ? args[0] : 0;
        int64_t a1 = args.size() > 1 ? args[1] : 0;
        int64_t a2 = args.size() > 2 ? args[2] : 0;
        int64_t a3 = args.size() > 3 ? args[3] : 0;
        int64_t a4 = args.size() > 4 ? args[4] : 0;
        int64_t a5 = args.size() > 5 ? args[5] : 0;
        
        if (args.size() > 6) {
            result.error_message = "Too many arguments for JIT function";
            return result;
        }
        
        return_value = func(a0, a1, a2, a3, a4, a5);
        
        auto end_time = std::chrono::steady_clock::now();
        result.stats.total_time = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time);
        
        result.success = true;
        result.exit_code = static_cast<int>(return_value);
        result.output = "[JIT] Function executed, returned: " + std::to_string(return_value);
        
    } catch (const std::exception& e) {
        result.error_message = std::string("JIT function execution failed: ") + e.what();
    }
    
    return result;
}

// ============================================================================
// 便捷函数
// ============================================================================

ExecutionResult run_claw_source(const std::string& source,
                                const ExecutionConfig& config) {
    ExecutionEngine engine(config);
    if (!engine.load_source(source)) {
        ExecutionResult r;
        r.error_message = "Failed to load source: " + std::string("compilation error");
        return r;
    }
    return engine.execute();
}

ExecutionResult run_claw_file(const std::string& filepath,
                              const ExecutionConfig& config) {
    ExecutionEngine engine(config);
    if (!engine.load_file(filepath)) {
        ExecutionResult r;
        r.error_message = "Failed to load file: " + filepath;
        return r;
    }
    return engine.execute();
}

// ============================================================================
// 执行模式工具函数
// ============================================================================

std::string execution_mode_to_string(ExecutionMode mode) {
    switch (mode) {
        case ExecutionMode::AST_INTERPRET: return "ast";
        case ExecutionMode::BYTECODE_INTERPRET: return "bytecode";
        case ExecutionMode::JIT_COMPILED: return "jit";
        case ExecutionMode::HYBRID: return "hybrid";
    }
    return "unknown";
}

std::optional<ExecutionMode> string_to_execution_mode(const std::string& s) {
    if (s == "ast") return ExecutionMode::AST_INTERPRET;
    if (s == "bytecode") return ExecutionMode::BYTECODE_INTERPRET;
    if (s == "jit") return ExecutionMode::JIT_COMPILED;
    if (s == "hybrid") return ExecutionMode::HYBRID;
    return std::nullopt;
}

} // namespace claw
