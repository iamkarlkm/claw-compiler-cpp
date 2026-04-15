// TensorIRGenerator.h - Generates TensorIR from Claw AST/IR
#ifndef CLAW_TENSOR_IR_GENERATOR_H
#define CLAW_TENSOR_IR_GENERATOR_H

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

namespace claw {
namespace ast {
    class ASTNode;
    class Expression;
    class Statement;
    class FunctionDef;
    class TensorLiteral;
    class BinaryOp;
    class CallExpr;
}

namespace ir {
    class IRModule;
    class BasicBlock;
    class Instruction;
}

namespace tensorir {

// ========== Conversion Context ==========

struct TensorIRGenContext {
    // 映射表
    std::unordered_map<std::string, Buffer*> buffer_map;
    std::unordered_map<std::string, IterVar*> iter_var_map;
    std::unordered_set<std::string> processed_functions;
    
    // 当前函数信息
    std::string current_func_name;
    std::vector<Buffer*> current_params;
    Buffer* current_return_buffer = nullptr;
    
    // Tensor 操作收集
    std::vector<TensorOp*> tensor_ops;
    
    // 配置
    bool enable_tensor_optimization = true;
    bool generate_schedules = false;
    
    // 工具方法
    Buffer* find_buffer(const std::string& name) const;
    IterVar* find_iter_var(const std::string& name) const;
};

// ========== TensorIR Generator ==========

class TensorIRGenerator {
public:
    TensorIRGenerator();
    ~TensorIRGenerator();
    
    // 主转换入口
    std::unique_ptr<TensorIRModule> generate_from_ast(
        const std::vector<std::unique_ptr<ast::Statement>>& stmts);
    
    std::unique_ptr<TensorIRModule> generate_from_ir(
        const ir::IRModule& ir_module);
    
    // 配置
    void set_optimization_level(int level);  // 0-3
    void set_target(const std::string& target);  // "cpu", "cuda", "tpu"
    void enable_auto_schedule(bool enable);
    
    // 获取统计信息
    struct GenStats {
        size_t num_buffers;
        size_t num_ops;
        size_t num_schedules;
        size_t lines_generated;
    };
    GenStats get_stats() const;
    
private:
    // 内部实现
    TensorIRGenContext ctx_;
    std::unique_ptr<TensorIRModule> module_;
    int optimization_level_ = 2;
    std::string target_ = "cpu";
    bool auto_schedule_ = false;
    
    // AST 转换
    void convert_function(const ast::FunctionDef& func);
    void convert_statement(const ast::Statement& stmt);
    
    // 表达式转换
    Buffer* convert_expression(const ast::Expression& expr);
    IterVar* convert_iteration(const ast::Statement& stmt);
    
    // 张量操作识别与转换
    TensorOp* detect_matmul(Buffer* a, Buffer* b, Buffer* c);
    TensorOp* detect_conv2d(Buffer* input, Buffer* weight, Buffer* output,
                           const std::vector<int64_t>& strides);
    TensorOp* detect_reduce(const std::string& op_name, 
                           Buffer* input, const std::vector<int>& axes);
    
    // 形状推断
    DimList infer_shape(const ast::Expression& expr);
    DimList infer_binary_op_shape(const ast::BinaryOp& op);
    
    // 自动调度生成
    void generate_schedule(TensorOp* op);
    void schedule_matmul(MatmulOp* op);
    void schedule_conv2d(Conv2dOp* op);
    
    // 工具方法
    std::string get_tensor_op_type(const ast::CallExpr& call) const;
    bool is_tensor_operation(const ast::Expression& expr) const;
    StorageScope get_storage_scope(const std::string& name) const;
    
    // 缓冲区管理
    Buffer* get_or_create_buffer(const std::string& name, 
                                 const std::string& elem_type,
                                 const DimList& shape,
                                 StorageScope scope = StorageScope::Local);
    
    // 代码生成
    std::string generate_loop_nest(TensorOp* op) const;
    std::string generate_compute_body(ComputeOp* op) const;
};

// ========== TensorIR 优化器 ==========

class TensorIROptimizer {
public:
    TensorIROptimizer(TensorIRModule& module);
    
    // 优化 passes
    void fold_constants();
    void fuse_loops();
    void simplify_shapes();
    void remove_dead_code();
    void vectorize_loops();
    
    // 执行所有优化
    void optimize(int level);
    
private:
    TensorIRModule& module_;
    int level_ = 2;
    
    // 内部工具
    bool can_fuse(IterVar* iter1, IterVar* iter2) const;
    DimList simplify_dim_list(const DimList& dims) const;
};

// ========== 代码生成器 ==========

class TensorIRCodegen {
public:
    TensorIRCodegen(const TensorIRModule& module);
    
    // 生成目标代码
    std::string generate_c() const;
    std::string generate_cuda() const;
    std::string generate_llvm_ir() const;
    
    // 获取输出
    std::string get_output() const { return output_; }
    
private:
    const TensorIRModule& module_;
    mutable std::string output_;
    
    // 辅助方法
    void emit(const std::string& s) const { output_ += s; }
    void emit_line(const std::string& s) const { output_ += s + "\n"; }
    
    std::string to_c_type(const std::string& elem_type) const;
    std::string generate_loop_code(TensorOp* op, int indent) const;
};

// ========== Helper Functions ==========

// 从 AST 张量字面量推断形状
DimList infer_tensor_shape(const ast::TensorLiteral& tensor);

// 检查是否是张量操作
bool is_tensor_call(const ast::CallExpr& call);

// 内置张量函数列表
const std::unordered_set<std::string>& get_builtin_tensor_funcs();

} // namespace tensorir
} // namespace claw

#endif // CLAW_TENSOR_IR_GENERATOR_H
