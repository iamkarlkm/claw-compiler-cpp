// native_codegen.h - 本地代码生成器 (Claw IR → C 代码)
// 将 Claw 中间表示转换为可移植的 C 代码

#ifndef CLAW_NATIVE_CODEGEN_H
#define CLAW_NATIVE_CODEGEN_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>

#include "../ir/ir.h"

namespace claw {
namespace codegen {

// ============================================================================
// 代码生成配置
// ============================================================================

struct NativeCodegenConfig {
    bool emit_comments = true;           // 注释
    bool emit_line_directives = true;    // #line 指令
    bool emit_debug_info = false;        // 调试信息
    bool optimize_const = true;          // 常量优化
    bool emit_headers = true;            // 生成头文件
    bool c99_compatible = true;          // C99 兼容模式
    bool emit_type_definitions = true;   // 类型定义
    std::string output_prefix = "claw_"; // 输出文件前缀
    int indent_size = 4;                 // 缩进大小
};

// ============================================================================
// C 类型映射
// ============================================================================

class CTypeMapper {
public:
    static std::string to_c_type(const ir::Type* type);
    static std::string to_c_type(const std::string& claw_type);
    static bool needs_pointer(const ir::Type* type);
    static std::string get_type_name(const ir::Type* type);
};

// ============================================================================
// C 代码生成器
// ============================================================================

class NativeCodegen {
public:
    explicit NativeCodegen(const NativeCodegenConfig& config = {});
    ~NativeCodegen() = default;

    // 主转换接口
    std::string generate(const ir::Module& module);
    std::string generate_header(const ir::Module& module);
    std::string generate_source(const ir::Module& module);

    // 单独函数生成
    std::string generate_function(const ir::Function* func);
    std::string generate_global(const ir::GlobalVariable* global);

    // 配置
    void set_config(const NativeCodegenConfig& config);
    const NativeCodegenConfig& get_config() const { return config_; }

    // 统计信息
    struct Stats {
        int num_functions = 0;
        int num_globals = 0;
        int num_types = 0;
        int total_lines = 0;
    };
    const Stats& get_stats() const { return stats_; }

private:
    NativeCodegenConfig config_;
    Stats stats_;

    // 上下文
    const ir::Module* current_module_ = nullptr;
    const ir::Function* current_function_ = nullptr;
    std::unordered_set<std::string> emitted_functions_;
    std::unordered_set<std::string> emitted_types_;
    std::unordered_map<std::string, std::string> local_vars_;
    int temp_var_counter_ = 0;

    // 缩进管理
    int indent_level_ = 0;
    std::string indent() const;

    // 生成辅助
    std::string generate_type_definitions();
    std::string generate_function_declarations();
    std::string generate_global_declarations();
    std::string generate_struct_definitions();

    // 函数生成
    std::string generate_function_signature(const ir::Function* func);
    std::string generate_function_body(const ir::Function* func);
    std::string generate_basic_block(const ir::BasicBlock* block);

    // 指令生成
    std::string generate_instruction(const ir::Instruction* inst);
    std::string generate_binary_op(const ir::Instruction* inst);
    std::string generate_unary_op(const ir::Instruction* inst);
    std::string generate_call(const ir::Instruction* inst);
    std::string generate_load(const ir::Instruction* inst);
    std::string generate_store(const ir::Instruction* inst);
    std::string generate_alloca(const ir::Instruction* inst);
    std::string generate_gep(const ir::Instruction* inst);
    std::string generate_cmp(const ir::Instruction* inst);
    std::string generate_phi(const ir::Instruction* inst);
    std::string generate_select(const ir::Instruction* inst);
    std::string generate_cast(const ir::Instruction* inst);
    std::string generate_return(const ir::Instruction* inst);
    std::string generate_branch(const ir::Instruction* inst);
    std::string generate_switch(const ir::Instruction* inst);

    // 表达式生成
    std::string generate_constant(const ir::Constant* constant);
    std::string generate_constant_array(const ir::ConstantArray* arr);
    std::string generate_constant_tuple(const ir::ConstantTuple* tup);
    std::string generate_operand(const ir::Value* operand);

    // 工具函数
    std::string make_safe_name(const std::string& name);
    std::string make_temp_var();
    std::string get_value_name(const ir::Value* value);
    bool is_simple_type(const ir::Type* type);
    std::string get_c_operator(const ir::OpCode op);
};

// ============================================================================
// 便捷函数
// ============================================================================

inline std::string generate_c_code(const ir::Module& module, 
                                    const NativeCodegenConfig& config = {}) {
    NativeCodegen codegen(config);
    return codegen.generate(module);
}

} // namespace codegen
} // namespace claw

#endif // CLAW_NATIVE_CODEGEN_H
