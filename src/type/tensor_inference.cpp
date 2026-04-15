// Claw Compiler - Tensor Type Inference Implementation

#include "type/tensor_inference.h"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace claw {
namespace type {

// =============================================================================
// TensorShape Implementation
// =============================================================================

std::optional<TensorShape> TensorShape::unify(const TensorShape& other) const {
    if (rank() != other.rank()) {
        return std::nullopt;
    }
    
    std::vector<int64_t> result(rank());
    for (size_t i = 0; i < rank(); i++) {
        int64_t d1 = dim(i);
        int64_t d2 = other.dim(i);
        
        if (d1 == d2) {
            result[i] = d1;
        } else if (d1 < 0) {
            result[i] = d2;
        } else if (d2 < 0) {
            result[i] = d1;
        } else {
            return std::nullopt;  // Conflict
        }
    }
    
    return TensorShape(result);
}

std::optional<TensorShape> TensorShape::broadcast(
    const std::vector<TensorShape>& shapes) {
    if (shapes.empty()) {
        return TensorShape();  // Scalar
    }
    
    // Find max rank
    size_t max_rank = 0;
    for (const auto& s : shapes) {
        max_rank = std::max(max_rank, s.rank());
    }
    
    // Broadcast to max rank
    std::vector<int64_t> result(max_rank, 1);
    
    for (const auto& shape : shapes) {
        size_t offset = max_rank - shape.rank();
        for (size_t i = 0; i < shape.rank(); i++) {
            size_t idx = offset + i;
            int64_t d = shape.dim(i);
            
            if (result[idx] == 1) {
                result[idx] = d;
            } else if (d != 1 && result[idx] != d && result[idx] > 0 && d > 0) {
                return std::nullopt;  // Cannot broadcast
            }
        }
    }
    
    return TensorShape(result);
}

// =============================================================================
// TensorInferContext Implementation
// =============================================================================

TypePtr TensorInferContext::infer_tensor_type(ast::ExprNode* expr) {
    if (!expr) return TypePtr(nullptr);
    
    switch (expr->get_kind()) {
        case ast::Expression::Kind::Literal:
            return infer_literal(static_cast<ast::LiteralExprNode*>(expr));
        case ast::Expression::Kind::Identifier:
            return infer_ident(static_cast<ast::IdentExprNode*>(expr));
        case ast::Expression::Kind::Binary:
            return infer_binary(static_cast<ast::BinaryExprNode*>(expr));
        case ast::Expression::Kind::Call:
            return infer_call(static_cast<ast::CallExprNode*>(expr));
        case ast::Expression::Kind::Index:
            return infer_index(static_cast<ast::IndexExprNode*>(expr));
        case ast::Expression::Kind::TensorLiteral:
            return infer_tensor_literal(static_cast<ast::TensorLiteralNode*>(expr));
        case ast::Expression::Kind::Array:
            return infer_array(static_cast<ast::ArrayExprNode*>(expr));
        case ast::Expression::Kind::Member:
            return infer_member(static_cast<ast::MemberExprNode*>(expr));
        default:
            add_error("Cannot infer tensor type for expression kind: " + 
                     std::to_string(static_cast<int>(expr->get_kind())));
            return TypePtr(nullptr);
    }
}

std::optional<TensorShape> TensorInferContext::infer_shape(ast::ExprNode* expr) {
    if (!expr) return std::nullopt;
    
    switch (expr->get_kind()) {
        case ast::Expression::Kind::Binary:
            return infer_shape_binary(static_cast<ast::BinaryExprNode*>(expr));
        case ast::Expression::Kind::Call:
            return infer_shape_call(static_cast<ast::CallExprNode*>(expr));
        case ast::Expression::Kind::Index:
            return infer_shape_index(static_cast<ast::IndexExprNode*>(expr));
        case ast::Expression::Kind::TensorLiteral: {
            auto* tens = static_cast<ast::TensorLiteralNode*>(expr);
            return TensorShape(tens->get_shape());
        }
        case ast::Expression::Kind::Array: {
            auto* arr = static_cast<ast::ArrayExprNode*>(expr);
            if (arr->get_elements().empty()) return std::nullopt;
            auto first_shape = infer_shape(arr->get_elements()[0].get());
            if (!first_shape) return std::nullopt;
            std::vector<int64_t> dims = {static_cast<int64_t>(arr->get_elements().size())};
            dims.insert(dims.end(), first_shape->dims().begin(), first_shape->dims().end());
            return TensorShape(dims);
        }
        default:
            return std::nullopt;
    }
}

void TensorInferContext::add_shape_constraint(const std::string& name, 
                                              TensorShape shape) {
    shape_constraints_[name] = shape;
}

std::optional<TensorShape> TensorInferContext::get_shape_constraint(
    const std::string& name) {
    auto it = shape_constraints_.find(name);
    if (it != shape_constraints_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void TensorInferContext::add_dim_var(const std::string& var, int64_t value) {
    dim_vars_[var] = value;
}

std::optional<int64_t> TensorInferContext::get_dim_var(const std::string& var) {
    auto it = dim_vars_.find(var);
    if (it != dim_vars_.end()) {
        return it->second;
    }
    return std::nullopt;
}

TypePtr TensorInferContext::infer_literal(ast::LiteralExprNode* expr) {
    // Scalars are treated as tensor<type, [1]>
    auto& val = expr->get_value();
    
    TypePtr elem_type;
    if (std::holds_alternative<int64_t>(val)) {
        elem_type = Type::getPrimitive("i64");
    } else if (std::holds_alternative<double>(val)) {
        elem_type = Type::getPrimitive("f64");
    } else if (std::holds_alternative<bool>(val)) {
        elem_type = Type::getPrimitive("bool");
    } else {
        return TypePtr(nullptr);
    }
    
    return Type::get_tensor(elem_type, {1});
}

TypePtr TensorInferContext::infer_ident(ast::IdentExprNode* expr) {
    const std::string& name = expr->get_name();
    
    // Check in type environment
    if (env_) {
        auto type = env_->lookup(name);
        if (type) return type;
    }
    
    // Check shape constraints
    auto shape = get_shape_constraint(name);
    if (shape) {
        return Type::get_tensor(Type::getPrimitive("f32"), shape->dims());
    }
    
    add_error("Undefined identifier: " + name);
    return TypePtr(nullptr);
}

TypePtr TensorInferContext::infer_binary(ast::BinaryExprNode* expr) {
    auto* left = expr->get_left();
    auto* right = expr->get_right();
    
    auto left_type = infer_tensor_type(left);
    auto right_type = infer_tensor_type(right);
    
    if (!left_type || !right_type) {
        return TypePtr(nullptr);
    }
    
    // Element-wise operation: result shape = broadcast(left, right)
    auto left_shape = get_tensor_shape(left_type);
    auto right_shape = get_tensor_shape(right_type);
    
    if (left_shape && right_shape) {
        auto result_shape = TensorShape::broadcast({*left_shape, *right_shape});
        if (result_shape) {
            TypePtr elem_type;
            // Determine result element type
            auto left_elem = get_tensor_element_type(left_type);
            auto right_elem = get_tensor_element_type(right_type);
            
            // Promote to higher precision
            if (left_elem->is_float() || right_elem->is_float()) {
                elem_type = Type::getPrimitive("f64");
            } else {
                elem_type = Type::getPrimitive("i64");
            }
            
            return Type::get_tensor(elem_type, result_shape->dims());
        }
    }
    
    return TypePtr(nullptr);
}

TypePtr TensorInferContext::infer_call(ast::CallExprNode* expr) {
    const std::string& func_name = expr->get_function()->get_name();
    
    // Handle tensor::zeros, tensor::ones, etc.
    if (func_name.find("tensor::") == 0) {
        std::string op_name = func_name.substr(8);
        return TensorTypeInferrer().infer_creation_op(op_name, expr->get_arguments());
    }
    
    // Handle built-in tensor operations
    auto sig = TensorFunctionRegistry::instance().lookup(func_name);
    if (sig) {
        return sig->output_type;
    }
    
    // Try to infer from function signature
    add_error("Cannot infer type for function call: " + func_name);
    return TypePtr(nullptr);
}

TypePtr TensorInferContext::infer_index(ast::IndexExprNode* expr) {
    auto* base = expr->get_base();
    auto base_type = infer_tensor_type(base);
    
    if (!base_type || !base_type->is_tensor()) {
        add_error("Cannot index non-tensor type");
        return TypePtr(nullptr);
    }
    
    auto base_shape = get_tensor_shape(base_type);
    if (!base_shape) {
        return TypePtr(nullptr);
    }
    
    // Result shape = base shape minus indexed dimensions
    size_t num_indices = expr->get_indices().size();
    if (num_indices >= base_shape->rank()) {
        // Scalar result
        auto elem_type = get_tensor_element_type(base_type);
        return Type::get_tensor(elem_type, {1});
    }
    
    // Slice result
    std::vector<int64_t> result_dims;
    for (size_t i = num_indices; i < base_shape->rank(); i++) {
        result_dims.push_back(base_shape->dim(i));
    }
    
    auto elem_type = get_tensor_element_type(base_type);
    return Type::get_tensor(elem_type, result_dims);
}

TypePtr TensorInferContext::infer_tensor_literal(ast::TensorLiteralNode* expr) {
    auto shape = expr->get_shape();
    auto elem_type = Type::getPrimitive(expr->get_element_type());
    return Type::get_tensor(elem_type, shape);
}

TypePtr TensorInferContext::infer_array(ast::ArrayExprNode* expr) {
    auto& elements = expr->get_elements();
    if (elements.empty()) {
        return Type::get_array(Type::getPrimitive("i32"), 0);
    }
    
    // Infer first element type
    auto first_type = infer_tensor_type(elements[0].get());
    if (!first_type) {
        return TypePtr(nullptr);
    }
    
    // All elements should have same type
    for (size_t i = 1; i < elements.size(); i++) {
        auto elem_type = infer_tensor_type(elements[i].get());
        if (elem_type != first_type) {
            add_error("Array elements must have same type");
            return TypePtr(nullptr);
        }
    }
    
    std::vector<int64_t> shape = {static_cast<int64_t>(elements.size())};
    
    if (first_type->is_tensor()) {
        auto inner_shape = get_tensor_shape(first_type);
        if (inner_shape) {
            shape.insert(shape.end(), inner_shape->dims().begin(), inner_shape->dims().end());
        }
        auto elem_type = get_tensor_element_type(first_type);
        return Type::get_tensor(elem_type, shape);
    }
    
    return Type::get_array(first_type, elements.size());
}

TypePtr TensorInferContext::infer_member(ast::MemberExprNode* expr) {
    auto* object = expr->get_object();
    auto object_type = infer_tensor_type(object);
    
    if (!object_type) {
        return TypePtr(nullptr);
    }
    
    const std::string& member = expr->get_member();
    
    // Handle tensor.shape, tensor.dtype, etc.
    if (object_type->is_tensor()) {
        if (member == "shape") {
            // Return array of dimensions
            auto shape = get_tensor_shape(object_type);
            if (shape) {
                return Type::get_array(Type::getPrimitive("usize"), shape->rank());
            }
        } else if (member == "dtype" || member == "element_type") {
            auto elem_type = get_tensor_element_type(object_type);
            return elem_type;
        }
    }
    
    add_error("Unknown member: " + member);
    return TypePtr(nullptr);
}

std::optional<TensorShape> TensorInferContext::infer_shape_binary(
    ast::BinaryExprNode* expr) {
    auto left_shape = infer_shape(expr->get_left());
    auto right_shape = infer_shape(expr->get_right());
    
    if (!left_shape || !right_shape) {
        return std::nullopt;
    }
    
    return TensorShape::broadcast({*left_shape, *right_shape});
}

std::optional<TensorShape> TensorInferContext::infer_shape_call(
    ast::CallExprNode* expr) {
    const std::string& func_name = expr->get_function()->get_name();
    
    // Handle common tensor operations
    if (func_name == "tensor::zeros" || func_name == "tensor::ones") {
        // Shape from first argument
        if (!expr->get_arguments().empty()) {
            return infer_shape(expr->get_arguments()[0].get());
        }
    } else if (func_name == "tensor::matmul") {
        auto a_shape = infer_shape(expr->get_arguments()[0].get());
        auto b_shape = infer_shape(expr->get_arguments()[1].get());
        if (a_shape && b_shape && a_shape->rank() == 2 && b_shape->rank() == 2) {
            // [M, K] @ [K, N] -> [M, N]
            return TensorShape({a_shape->dim(0), b_shape->dim(1)});
        }
    }
    
    return std::nullopt;
}

std::optional<TensorShape> TensorInferContext::infer_shape_index(
    ast::IndexExprNode* expr) {
    auto base_shape = infer_shape(expr->get_base());
    if (!base_shape) return std::nullopt;
    
    size_t num_indices = expr->get_indices().size();
    if (num_indices >= base_shape->rank()) {
        return TensorShape({1});  // Scalar
    }
    
    std::vector<int64_t> result_dims;
    for (size_t i = num_indices; i < base_shape->rank(); i++) {
        result_dims.push_back(base_shape->dim(i));
    }
    
    return TensorShape(result_dims);
}

// =============================================================================
// TensorTypeInferrer Implementation
// =============================================================================

TensorTypeInferrer::TensorTypeInferrer() = default;

bool TensorTypeInferrer::infer(ast::ProgramPtr program) {
    if (!program) return false;
    
    // Process each declaration
    for (auto& decl : program->get_declarations()) {
        if (decl->get_kind() == ast::Statement::Kind::Function) {
            auto* func = static_cast<ast::FunctionStmt*>(decl.get());
            
            // Infer function body
            if (func->get_body()) {
                // Process statements and expressions
                // (Implementation would traverse AST)
            }
        }
    }
    
    return !has_errors();
}

TypePtr TensorTypeInferrer::infer_expression(ast::ExprNode* expr, 
                                              TypeEnvironment* env) {
    TensorInferContext ctx(env);
    return ctx.infer_tensor_type(expr);
}

bool TensorTypeInferrer::validate_tensor_op(TensorOp op, 
                                            const std::vector<TypePtr>& args,
                                            TypePtr& result_type, 
                                            std::string& error) {
    // Check argument types
    for (auto* arg : args) {
        if (!arg->is_tensor()) {
            error = "Expected tensor argument";
            return false;
        }
    }
    
    // Validate based on operation
    switch (op) {
        case TensorOp::MATMUL:
            if (args.size() != 2) {
                error = "matmul requires 2 arguments";
                return false;
            }
            result_type = infer_matmul(args[0], args[1]);
            break;
            
        case TensorOp::CONV2D:
            if (args.size() < 2) {
                error = "conv2d requires at least 2 arguments";
                return false;
            }
            // Would need stride/padding parameters
            result_type = args[0];
            break;
            
        default:
            // Element-wise operations
            if (args.size() >= 2) {
                result_type = infer_elementwise_op(op, args[0], args[1]);
            } else {
                result_type = args[0];
            }
    }
    
    return true;
}

TypePtr TensorTypeInferrer::get_tensor_element_type(TypePtr tensor_type) {
    if (!tensor_type->is_tensor()) return TypePtr(nullptr);
    
    auto* tens = static_cast<TensorType*>(tensor_type.get());
    return tens->get_element_type();
}

std::optional<TensorShape> TensorTypeInferrer::get_tensor_shape(TypePtr tensor_type) {
    if (!tensor_type->is_tensor()) return std::nullopt;
    
    auto* tens = static_cast<TensorType*>(tensor_type.get());
    return TensorShape(tens->get_shape());
}

TypePtr TensorTypeInferrer::infer_creation_op(
    const std::string& op_name,
    const std::vector<ast::ExprNode*>& args) {
    
    std::vector<int64_t> shape;
    std::string dtype = "f32";
    
    // Parse shape from first argument
    if (!args.empty()) {
        auto* shape_arg = args[0];
        if (shape_arg->get_kind() == ast::Expression::Kind::Array) {
            auto* arr = static_cast<ast::ArrayExprNode*>(shape_arg);
            for (auto& elem : arr->get_elements()) {
                if (elem->get_kind() == ast::Expression::Kind::Literal) {
                    auto* lit = static_cast<ast::LiteralExprNode*>(elem.get());
                    auto& val = lit->get_value();
                    if (std::holds_alternative<int64_t>(val)) {
                        shape.push_back(std::get<int64_t>(val));
                    }
                }
            }
        }
    }
    
    // Parse dtype if provided
    if (args.size() > 1) {
        auto* dtype_arg = args[1];
        if (dtype_arg->get_kind() == ast::Expression::Kind::Identifier) {
            auto* ident = static_cast<ast::IdentExprNode*>(dtype_arg);
            dtype = ident->get_name();
        }
    }
    
    if (shape.empty()) {
        shape = {1};  // Default to scalar
    }
    
    auto elem_type = Type::getPrimitive(dtype);
    return Type::get_tensor(elem_type, shape);
}

TypePtr TensorTypeInferrer::infer_elementwise_op(TensorOp op, 
                                                  TypePtr left, 
                                                  TypePtr right) {
    auto left_elem = get_tensor_element_type(left);
    auto right_elem = get_tensor_element_type(right);
    
    // Determine result element type
    TypePtr result_elem;
    if (left_elem->is_float() || right_elem->is_float()) {
        // Promote to higher precision
        if (left_elem->to_string() == "f64" || right_elem->to_string() == "f64") {
            result_elem = Type::getPrimitive("f64");
        } else {
            result_elem = Type::getPrimitive("f32");
        }
    } else {
        result_elem = Type::getPrimitive("i64");
    }
    
    // Broadcast shapes
    auto left_shape = get_tensor_shape(left);
    auto right_shape = get_tensor_shape(right);
    
    std::vector<int64_t> result_dims;
    if (left_shape && right_shape) {
        auto broadcasted = TensorShape::broadcast({*left_shape, *right_shape});
        if (broadcasted) {
            result_dims = broadcasted->dims();
        }
    }
    
    if (result_dims.empty()) {
        result_dims = {1};
    }
    
    return Type::get_tensor(result_elem, result_dims);
}

TypePtr TensorTypeInferrer::infer_reduction_op(
    TensorOp op, 
    TypePtr input,
    const std::optional<std::vector<int64_t>>& axes) {
    auto elem_type = get_tensor_element_type(input);
    auto shape = get_tensor_shape(input);
    
    if (!shape || !elem_type) {
        return TypePtr(nullptr);
    }
    
    std::vector<int64_t> result_dims;
    
    if (axes) {
        // Reduce specific axes
        for (size_t i = 0; i < shape->rank(); i++) {
            bool reduced = false;
            for (auto ax : *axes) {
                if (static_cast<int64_t>(i) == ax) {
                    reduced = true;
                    break;
                }
            }
            if (!reduced) {
                result_dims.push_back(shape->dim(i));
            }
        }
    } else {
        // Reduce all -> scalar
        result_dims = {1};
    }
    
    // Reduction changes element type for some ops
    TypePtr result_elem = elem_type;
    if (op == TensorOp::ARGMAX || op == TensorOp::ARGMIN) {
        result_elem = Type::getPrimitive("i64");
    }
    
    return Type::get_tensor(result_elem, result_dims);
}

TypePtr TensorTypeInferrer::infer_matmul(TypePtr a, TypePtr b) {
    auto a_shape = get_tensor_shape(a);
    auto b_shape = get_tensor_shape(b);
    auto a_elem = get_tensor_element_type(a);
    auto b_elem = get_tensor_element_type(b);
    
    if (!a_shape || !b_shape) return TypePtr(nullptr);
    if (a_shape->rank() < 2 || b_shape->rank() < 2) return TypePtr(nullptr);
    
    // [M, K] @ [K, N] -> [M, N]
    // Or batch matmul: [..., M, K] @ [..., K, N] -> [..., M, N]
    
    std::vector<int64_t> result_dims;
    
    size_t a_rank = a_shape->rank();
    size_t b_rank = b_shape->rank();
    
    // Handle batch dimensions
    size_t batch_a = a_rank > 2 ? a_rank - 2 : 0;
    size_t batch_b = b_rank > 2 ? b_rank - 2 : 0;
    size_t max_batch = std::max(batch_a, batch_b);
    
    // Copy batch dimensions
    for (size_t i = 0; i < max_batch; i++) {
        size_t idx_a = (a_rank > 2) ? i : 0;
        size_t idx_b = (b_rank > 2) ? i : 0;
        int64_t dim_a = (a_rank > 2) ? a_shape->dim(idx_a) : 1;
        int64_t dim_b = (b_rank > 2) ? b_shape->dim(idx_b) : 1;
        
        if (dim_a == dim_b || dim_a == 1 || dim_b == 1) {
            result_dims.push_back(std::max(dim_a, dim_b));
        } else {
            return TypePtr(nullptr);  // Incompatible batch dims
        }
    }
    
    // Result is [M, N]
    result_dims.push_back(a_shape->dim(a_rank - 2));  // M
    result_dims.push_back(b_shape->dim(b_rank - 1));  // N
    
    // Promote element type
    TypePtr result_elem;
    if (a_elem->to_string() == "f64" || b_elem->to_string() == "f64") {
        result_elem = Type::getPrimitive("f64");
    } else {
        result_elem = Type::getPrimitive("f32");
    }
    
    return Type::get_tensor(result_elem, result_dims);
}

TypePtr TensorTypeInferrer::infer_conv2d(TypePtr input, TypePtr weight,
                                         const std::vector<int64_t>& stride,
                                         const std::vector<int64_t>& padding) {
    auto in_shape = get_tensor_shape(input);
    auto wt_shape = get_tensor_shape(weight);
    
    if (!in_shape || !wt_shape) return TypePtr(nullptr);
    
    // Input: [N, C, H, W]
    // Weight: [K, C, R, S]
    // Output: [N, K, H', W']
    
    if (in_shape->rank() != 4 || wt_shape->rank() != 4) {
        return TypePtr(nullptr);
    }
    
    int64_t N = in_shape->dim(0);
    int64_t H = in_shape->dim(2);
    int64_t W = in_shape->dim(3);
    int64_t K = wt_shape->dim(0);
    int64_t R = wt_shape->dim(2);
    int64_t S = wt_shape->dim(3);
    
    int64_t stride_h = stride.empty() ? 1 : stride[0];
    int64_t stride_w = stride.empty() ? 1 : stride.back();
    int64_t pad_h = padding.empty() ? 0 : padding[0];
    int64_t pad_w = padding.empty() ? 0 : padding.back();
    
    int64_t H_out = (H + 2 * pad_h - R) / stride_h + 1;
    int64_t W_out = (W + 2 * pad_w - S) / stride_w + 1;
    
    auto elem_type = get_tensor_element_type(input);
    return Type::get_tensor(elem_type, {N, K, H_out, W_out});
}

std::optional<TensorShape> TensorTypeInferrer::infer_broadcast(
    const TensorShape& a, const TensorShape& b) {
    return TensorShape::broadcast({a, b});
}

bool TensorTypeInferrer::validate_shape_match(
    const TensorShape& expected,
    const TensorShape& actual,
    const std::string& context) {
    
    if (expected.rank() != actual.rank()) {
        errors_.push_back(context + ": rank mismatch");
        return false;
    }
    
    for (size_t i = 0; i < expected.rank(); i++) {
        int64_t exp_dim = expected.dim(i);
        int64_t act_dim = actual.dim(i);
        
        if (exp_dim > 0 && act_dim > 0 && exp_dim != act_dim) {
            errors_.push_back(context + ": dimension " + std::to_string(i) + 
                            " mismatch: expected " + std::to_string(exp_dim) +
                            ", got " + std::to_string(act_dim));
            return false;
        }
    }
    
    return true;
}

bool TensorTypeInferrer::validate_element_type(TypePtr tensor_type, 
                                                TypePtr expected_elem_type) {
    auto elem_type = get_tensor_element_type(tensor_type);
    if (elem_type != expected_elem_type) {
        errors_.push_back("Element type mismatch: expected " + 
                         expected_elem_type->to_string() + 
                         ", got " + elem_type->to_string());
        return false;
    }
    return true;
}

// =============================================================================
// TensorFunctionRegistry Implementation
// =============================================================================

TensorFunctionRegistry& TensorFunctionRegistry::instance() {
    static TensorFunctionRegistry instance;
    return instance;
}

void TensorFunctionRegistry::register_builtins() {
    // Creation operations
    functions_["tensor::zeros"] = {
        TensorOp::ZEROS,
        {Type::get_array(Type::getPrimitive("usize"), -1)},
        TypePtr(nullptr),  // Depends on arguments
        TensorShape()
    };
    
    functions_["tensor::ones"] = {
        TensorOp::ONES,
        {Type::get_array(Type::getPrimitive("usize"), -1)},
        TypePtr(nullptr),
        TensorShape()
    };
    
    functions_["tensor::fill"] = {
        TensorOp::FILL,
        {Type::get_array(Type::getPrimitive("usize"), -1), Type::getPrimitive("f32")},
        TypePtr(nullptr),
        TensorShape()
    };
    
    functions_["tensor::random"] = {
        TensorOp::RANDOM,
        {Type::get_array(Type::getPrimitive("usize"), -1)},
        TypePtr(nullptr),
        TensorShape()
    };
    
    // Matrix operations
    functions_["tensor::matmul"] = {
        TensorOp::MATMUL,
        {TypePtr(nullptr), TypePtr(nullptr)},  // Inferred from args
        TypePtr(nullptr),
        TensorShape()
    };
    
    functions_["tensor::dot"] = {
        TensorOp::DOT,
        {TypePtr(nullptr), TypePtr(nullptr)},
        TypePtr(nullptr),
        TensorShape({1})
    };
    
    // Reduction operations
    functions_["tensor::sum"] = {
        TensorOp::SUM,
        {TypePtr(nullptr)},
        TypePtr(nullptr),
        TensorShape({1})
    };
    
    functions_["tensor::mean"] = {
        TensorOp::MEAN,
        {TypePtr(nullptr)},
        TypePtr(nullptr),
        TensorShape({1})
    };
    
    functions_["tensor::max"] = {
        TensorOp::MAX,
        {TypePtr(nullptr)},
        TypePtr(nullptr),
        TensorShape({1})
    };
    
    functions_["tensor::min"] = {
        TensorOp::MIN,
        {TypePtr(nullptr)},
        TypePtr(nullptr),
        TensorShape({1})
    };
    
    functions_["tensor::argmax"] = {
        TensorOp::ARGMAX,
        {TypePtr(nullptr)},
        Type::get_tensor(Type::getPrimitive("i64"), {1}),
        TensorShape({1})
    };
    
    // Shape operations
    functions_["tensor::reshape"] = {
        TensorOp::RESHAPE,
        {TypePtr(nullptr), Type::get_array(Type::getPrimitive("usize"), -1)},
        TypePtr(nullptr),
        TensorShape()
    };
    
    functions_["tensor::transpose"] = {
        TensorOp::TRANSPOSE,
        {TypePtr(nullptr)},
        TypePtr(nullptr),
        TensorShape()
    };
    
    // Neural network operations
    functions_["tensor::conv2d"] = {
        TensorOp::CONV2D,
        {TypePtr(nullptr), TypePtr(nullptr)},
        TypePtr(nullptr),
        TensorShape()
    };
    
    functions_["tensor::max_pool2d"] = {
        TensorOp::MAX_POOL2D,
        {TypePtr(nullptr)},
        TypePtr(nullptr),
        TensorShape()
    };
    
    functions_["tensor::avg_pool2d"] = {
        TensorOp::AVG_POOL2D,
        {TypePtr(nullptr)},
        TypePtr(nullptr),
        TensorShape()
    };
}

// Helper to parse tensor operation from string
std::optional<TensorOp> parse_tensor_op(const std::string& name) {
    static const std::unordered_map<std::string, TensorOp> ops = {
        {"zeros", TensorOp::ZEROS},
        {"ones", TensorOp::ONES},
        {"fill", TensorOp::FILL},
        {"random", TensorOp::RANDOM},
        {"add", TensorOp::ADD},
        {"sub", TensorOp::SUB},
        {"mul", TensorOp::MUL},
        {"div", TensorOp::DIV},
        {"matmul", TensorOp::MATMUL},
        {"dot", TensorOp::DOT},
        {"sum", TensorOp::SUM},
        {"mean", TensorOp::MEAN},
        {"max", TensorOp::MAX},
        {"min", TensorOp::MIN},
        {"argmax", TensorOp::ARGMAX},
        {"argmin", TensorOp::ARGMIN},
        {"reshape", TensorOp::RESHAPE},
        {"transpose", TensorOp::TRANSPOSE},
        {"flatten", TensorOp::FLATTEN},
        {"squeeze", TensorOp::SQUEEZE},
        {"expand", TensorOp::EXPAND},
        {"slice", TensorOp::SLICE},
        {"conv2d", TensorOp::CONV2D},
        {"max_pool2d", TensorOp::MAX_POOL2D},
        {"avg_pool2d", TensorOp::AVG_POOL2D},
        {"concat", TensorOp::CONCAT},
        {"stack", TensorOp::STACK},
    };
    
    auto it = ops.find(name);
    if (it != ops.end()) {
        return it->second;
    }
    return std::nullopt;
}

// Initialize registry
namespace {
    struct RegistryInitializer {
        RegistryInitializer() {
            TensorFunctionRegistry::instance().register_builtins();
        }
    };
    static RegistryInitializer init;
}

} // namespace type
} // namespace claw
