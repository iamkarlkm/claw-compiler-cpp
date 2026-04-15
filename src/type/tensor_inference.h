// Claw Compiler - Tensor Type Inference
// Infers tensor types from tensor expressions and operations

#ifndef CLAW_TENSOR_INFERENCE_H
#define CLAW_TENSOR_INFERENCE_H

#include "type/type_system.h"
#include "ast/ast.h"
#include <memory>
#include <vector>
#include <string>
#include <optional>
#include <functional>

namespace claw {
namespace type {

// =============================================================================
// Tensor Shape Inference
// =============================================================================

class TensorShape {
public:
    TensorShape() = default;
    explicit TensorShape(std::vector<int64_t> dims) : dimensions_(std::move(dims)) {}
    
    // Create with dynamic dimensions
    static TensorShape dynamic(size_t rank) {
        std::vector<int64_t> dims(rank, -1);  // -1 means dynamic
        return TensorShape(dims);
    }
    
    size_t rank() const { return dimensions_.size(); }
    
    int64_t dim(size_t index) const {
        if (index >= dimensions_.size()) return -1;
        return dimensions_[index];
    }
    
    const std::vector<int64_t>& dims() const { return dimensions_; }
    
    bool is_dynamic() const {
        for (auto d : dimensions_) {
            if (d < 0) return true;
        }
        return false;
    }
    
    bool is_static() const { return !is_dynamic(); }
    
    // Unify two shapes (for broadcasting)
    std::optional<TensorShape> unify(const TensorShape& other) const;
    
    // Broadcast shapes
    static std::optional<TensorShape> broadcast(
        const std::vector<TensorShape>& shapes);
    
    std::string to_string() const {
        if (dimensions_.empty()) return "Scalar";
        std::string s = "[";
        for (size_t i = 0; i < dimensions_.size(); i++) {
            if (i > 0) s += ", ";
            s += (dimensions_[i] < 0) ? "?" : std::to_string(dimensions_[i]);
        }
        return s + "]";
    }
    
    bool operator==(const TensorShape& other) const {
        return dimensions_ == other.dimensions_;
    }
    
private:
    std::vector<int64_t> dimensions_;
};

// =============================================================================
// Tensor Operation Types
// =============================================================================

enum class TensorOp {
    // Creation ops
    ZEROS,
    ONES,
    FILL,
    RANDOM,
    Range,
    Linspace,
    
    // Element-wise ops
    ADD,
    SUB,
    MUL,
    DIV,
    POW,
    MOD,
    
    // Matrix ops
    MATMUL,
    DOT,
    CROSS,
    
    // Reduction ops
    SUM,
    MEAN,
    MAX,
    MIN,
    ARGMAX,
    ARGMIN,
    
    // Shape ops
    RESHAPE,
    TRANSPOSE,
    FLATTEN,
    SQUEEZE,
    EXPAND,
    SLICE,
    
    // Neural network ops
    CONV2D,
    MAX_POOL2D,
    AVG_POOL2D,
    BATCH_NORM,
    DROPOUT,
    
    // Tensor ops
    CONCAT,
    STACK,
    SPLIT,
    TILE,
};

// Tensor operation signature
struct TensorOpSignature {
    TensorOp op;
    std::vector<TypePtr> input_types;
    TypePtr output_type;
    TensorShape output_shape;
};

// =============================================================================
// Tensor Type Inference Context
// =============================================================================

class TensorInferContext {
public:
    TensorInferContext(TypeEnvironment* env) 
        : env_(env), current_fn_(nullptr) {}
    
    // Infer tensor type from expression
    TypePtr infer_tensor_type(ast::ExprNode* expr);
    
    // Infer tensor shape from expression
    std::optional<TensorShape> infer_shape(ast::ExprNode* expr);
    
    // Set current function context (for return type inference)
    void set_function_context(ast::FunctionNode* fn) { current_fn_ = fn; }
    
    // Get shape constraint
    void add_shape_constraint(const std::string& name, TensorShape shape);
    std::optional<TensorShape> get_shape_constraint(const std::string& name);
    
    // Dimension variable solving
    void add_dim_var(const std::string& var, int64_t value);
    std::optional<int64_t> get_dim_var(const std::string& var);
    
    // Error tracking
    void add_error(const std::string& msg) { errors_.push_back(msg); }
    const std::vector<std::string>& errors() const { return errors_; }
    bool has_errors() const { return !errors_.empty(); }
    
private:
    TypeEnvironment* env_;
    ast::FunctionNode* current_fn_;
    
    // Shape constraints: var_name -> shape
    std::unordered_map<std::string, TensorShape> shape_constraints_;
    
    // Dimension variables: dim_var -> value
    std::unordered_map<std::string, int64_t> dim_vars_;
    
    // Error messages
    std::vector<std::string> errors_;
    
    // Infer methods for specific expression types
    TypePtr infer_literal(ast::LiteralExprNode* expr);
    TypePtr infer_ident(ast::IdentExprNode* expr);
    TypePtr infer_binary(ast::BinaryExprNode* expr);
    TypePtr infer_call(ast::CallExprNode* expr);
    TypePtr infer_index(ast::IndexExprNode* expr);
    TypePtr infer_tensor_literal(ast::TensorLiteralNode* expr);
    TypePtr infer_array(ast::ArrayExprNode* expr);
    TypePtr infer_member(ast::MemberExprNode* expr);
    
    // Shape inference helpers
    std::optional<TensorShape> infer_shape_binary(ast::BinaryExprNode* expr);
    std::optional<TensorShape> infer_shape_call(ast::CallExprNode* expr);
    std::optional<TensorShape> infer_shape_index(ast::IndexExprNode* expr);
};

// =============================================================================
// Tensor Type Inferrer
// =============================================================================

class TensorTypeInferrer {
public:
    TensorTypeInferrer();
    ~TensorTypeInferrer() = default;
    
    // Main entry point: infer all tensor types in program
    bool infer(ast::ProgramPtr program);
    
    // Infer tensor type for a specific expression
    TypePtr infer_expression(ast::ExprNode* expr, TypeEnvironment* env);
    
    // Validate tensor operations
    bool validate_tensor_op(TensorOp op, const std::vector<TypePtr>& args,
                           TypePtr& result_type, std::string& error);
    
    // Get inferred shapes
    const std::unordered_map<ast::ExprNode*, TensorShape>& get_inferred_shapes() const {
        return inferred_shapes_;
    }
    
    // Get errors
    const std::vector<std::string>& errors() const { return errors_; }
    bool has_errors() const { return !errors_.empty(); }
    
private:
    // Inferred shapes cache
    std::unordered_map<ast::ExprNode*, TensorShape> inferred_shapes_;
    
    // Errors
    std::vector<std::string> errors_;
    
    // Helper methods
    TypePtr get_tensor_element_type(TypePtr tensor_type);
    std::optional<TensorShape> get_tensor_shape(TypePtr tensor_type);
    
    // Operation inference
    TypePtr infer_creation_op(const std::string& op_name, 
                              const std::vector<ast::ExprNode*>& args);
    TypePtr infer_elementwise_op(TensorOp op, TypePtr left, TypePtr right);
    TypePtr infer_reduction_op(TensorOp op, TypePtr input, 
                               const std::optional<std::vector<int64_t>>& axes);
    TypePtr infer_matmul(TypePtr a, TypePtr b);
    TypePtr infer_conv2d(TypePtr input, TypePtr weight, 
                        const std::vector<int64_t>& stride,
                        const std::vector<int64_t>& padding);
    
    // Broadcasting inference
    std::optional<TensorShape> infer_broadcast(const TensorShape& a, 
                                               const TensorShape& b);
    
    // Validation
    bool validate_shape_match(const TensorShape& expected, 
                             const TensorShape& actual,
                             const std::string& context);
    bool validate_element_type(TypePtr tensor_type, TypePtr expected_elem_type);
};

// =============================================================================
// Built-in Tensor Functions Registry
// =============================================================================

class TensorFunctionRegistry {
public:
    static TensorFunctionRegistry& instance();
    
    // Register built-in tensor functions
    void register_builtins();
    
    // Lookup function signature
    std::optional<TensorOpSignature> lookup(const std::string& name) const;
    
    // Get all registered functions
    const std::unordered_map<std::string, TensorOpSignature>& all() const {
        return functions_;
    }
    
private:
    TensorFunctionRegistry() = default;
    std::unordered_map<std::string, TensorOpSignature> functions_;
};

// Helper to get TensorOp from string
std::optional<TensorOp> parse_tensor_op(const std::string& name);

} // namespace type
} // namespace claw

#endif // CLAW_TENSOR_INFERENCE_H
