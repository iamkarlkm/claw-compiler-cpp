// Claw Type System - Core Type Definitions
// Phase 3: Type System Implementation

#ifndef CLAW_TYPE_SYSTEM_H
#define CLAW_TYPE_SYSTEM_H

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <variant>
#include <optional>
#include <functional>
#include "common/common.h"

namespace claw {

// Forward declarations for AST types
namespace ast {
    class ASTNode;
    class Expression;
    class Statement;
    class Program;
    class LiteralExpr;
    class IdentifierExpr;
    class BinaryExpr;
    class UnaryExpr;
    class CallExpr;
    class IndexExpr;
    class SliceExpr;
    class MemberExpr;
    class LambdaExpr;
    class ExprStmt;
    class LetStmt;
    class AssignStmt;
    class IfStmt;
    class MatchStmt;
    class ForStmt;
    class WhileStmt;
    class ReturnStmt;
    class BlockStmt;
    class FunctionStmt;
    class StructDecl;
    class ProcessDecl;
    class PublishStmt;
    class SubscribeStmt;
    class SerialProcessStmt;
    
    using ExprPtr = std::shared_ptr<Expression>;
    using StmtPtr = std::shared_ptr<Statement>;
    using NodePtr = std::shared_ptr<ASTNode>;
}

namespace type {

// =============================================================================
// Type Kind Enumeration
// =============================================================================
enum class TypeKind {
    // Primitive types
    UNIT,       // () - unit type
    BOOL,       // true/false
    INT8, INT16, INT32, INT64,   // Signed integers
    UINT8, UINT16, UINT32, UINT64, // Unsigned integers
    FLOAT16, FLOAT32, FLOAT64,    // Floating point
    STRING,     // String type
    CHAR,       // Character type
    
    // Compound types
    ARRAY,      // Fixed-size array
    TENSOR,     // Multi-dimensional tensor (for ML)
    TUPLE,      // Tuple type
    OPTIONAL,   // Optional type (T?)
    RESULT,     // Result type (T, E)
    
    // Function types
    FUNCTION,   // Function type (Fn)
    PROCESS,    // Serial process (Process)
    
    // Custom types
    STRUCT,     // Struct type
    ENUM,       // Enum type
    ALIAS,      // Type alias
    
    // Generic types
    GENERIC,    // Generic type placeholder (e.g., Array<T>)
    TYPE_VAR,   // Type variable (e.g., T, U, V)
    
    // Special
    UNKNOWN,    // Unknown/inferred type
    NEVER       // Never returns (diverges)
};

// =============================================================================
// Type Pointer (Intrusive shared_ptr for type graph)
// =============================================================================
class Type;
using TypePtr = std::shared_ptr<Type>;

class Type : public std::enable_shared_from_this<Type> {
public:
    TypeKind kind;
    std::string name;
    SourceSpan span;
    
    // For type parameters (generics)
    std::vector<TypePtr> type_params;
    
    // For compound types
    std::vector<TypePtr> type_args;
    
    virtual ~Type() = default;
    
    explicit Type(TypeKind k, std::string n = "") 
        : kind(k), name(std::move(n)) {}
    
    // Type identity checks
    virtual bool is_primitive() const;
    virtual bool is_numeric() const;
    virtual bool is_integer() const;
    virtual bool is_float() const;
    virtual bool is_bool() const;
    virtual bool is_string() const;
    virtual bool is_array() const;
    virtual bool is_tensor() const;
    virtual bool is_function() const;
    virtual bool is_process() const;
    virtual bool is_struct() const;
    virtual bool is_enum() const;
    virtual bool is_optional() const;
    virtual bool is_result() const;
    virtual bool is_reference() const;
    virtual bool is_generic() const;  // NEW: check if generic/type variable
    virtual bool is_type_var() const; // NEW: check if type variable
    virtual bool can_be_zero() const;
    virtual bool is_copyable() const;
    
    // Type comparison
    virtual bool equals(const TypePtr& other) const;
    virtual bool is_subtype_of(const TypePtr& other) const;
    virtual bool is_compatible_with(const TypePtr& other) const;
    
    // String representation
    virtual std::string to_string() const;
    virtual std::string signature() const;
    
    // Clone
    virtual TypePtr clone() const;
    
    // Convenience factory
    static TypePtr unit();
    static TypePtr boolean();
    static TypePtr int64();
    static TypePtr float64();
    static TypePtr string();
    static TypePtr never();
    static TypePtr unknown();
};

// =============================================================================
// Primitive Type Cache (Flyweight pattern)
// =============================================================================
class TypeCache {
private:
    std::map<TypeKind, TypePtr> primitives_;
    std::map<std::string, TypePtr> named_types_;
    std::map<std::pair<TypePtr, TypePtr>, TypePtr> tuple_types_;
    std::map<std::pair<TypePtr, int64_t>, TypePtr> array_types_;
    std::map<std::pair<TypePtr, std::vector<int64_t>>, TypePtr> tensor_types_;
    std::map<std::pair<TypePtr, TypePtr>, TypePtr> function_types_;
    std::map<TypePtr, TypePtr> optional_types_;
    std::map<std::pair<TypePtr, TypePtr>, TypePtr> result_types_;
    std::map<std::string, TypePtr> generic_types_;       // NEW: GenericType cache (e.g., Array<T>)
    std::map<std::string, TypePtr> type_vars_;           // NEW: TypeVar cache
    
    TypeCache();
    
public:
    static TypeCache& instance();
    
    // Get primitive types
    TypePtr get_unit() { return primitives_[TypeKind::UNIT]; }
    TypePtr get_bool() { return primitives_[TypeKind::BOOL]; }
    TypePtr get_int8() { return primitives_[TypeKind::INT8]; }
    TypePtr get_int16() { return primitives_[TypeKind::INT16]; }
    TypePtr get_int32() { return primitives_[TypeKind::INT32]; }
    TypePtr get_int64() { return primitives_[TypeKind::INT64]; }
    TypePtr get_uint8() { return primitives_[TypeKind::UINT8]; }
    TypePtr get_uint16() { return primitives_[TypeKind::UINT16]; }
    TypePtr get_uint32() { return primitives_[TypeKind::UINT32]; }
    TypePtr get_uint64() { return primitives_[TypeKind::UINT64]; }
    TypePtr get_float16() { return primitives_[TypeKind::FLOAT16]; }
    TypePtr get_float32() { return primitives_[TypeKind::FLOAT32]; }
    TypePtr get_float64() { return primitives_[TypeKind::FLOAT64]; }
    TypePtr get_string() { return primitives_[TypeKind::STRING]; }
    TypePtr get_char() { return primitives_[TypeKind::CHAR]; }
    TypePtr get_never() { return primitives_[TypeKind::NEVER]; }
    TypePtr get_unknown() { return primitives_[TypeKind::UNKNOWN]; }
    
    // Get or create compound types
    TypePtr get_array(TypePtr element, int64_t size);
    TypePtr get_tensor(TypePtr element, std::vector<int64_t> shape);
    TypePtr get_tuple(std::vector<TypePtr> elements);
    TypePtr get_function(TypePtr input, TypePtr output);
    TypePtr get_optional(TypePtr inner);
    TypePtr get_result(TypePtr ok, TypePtr err);
    
    // NEW: Generic type methods
    TypePtr get_generic(const std::string& base_name, std::vector<TypePtr> params = {});
    TypePtr get_type_var(const std::string& name, std::optional<TypePtr> bound = std::nullopt);
    TypePtr make_generic_instance(const std::string& base_name, const std::vector<TypePtr>& args);
    bool is_generic_type(const std::string& name) const;
    
    // Parse type from string
    TypePtr parse_type(const std::string& str);
    
    // Get by kind
    TypePtr get_primitive(TypeKind kind);
};

// =============================================================================
// Type Environment (Symbol table for types)
// =============================================================================
class TypeEnvironment {
private:
    std::map<std::string, TypePtr> type_vars_;      // Type variables (generics)
    std::map<std::string, TypePtr> type_aliases_;   // Type aliases
    std::map<std::string, TypePtr> struct_types_;   // Struct definitions
    std::map<std::string, TypePtr> enum_types_;     // Enum definitions
    std::vector<std::string> type_params_;          // NEW: Current scope type parameters
    
    std::shared_ptr<TypeEnvironment> parent_;
    int depth_;
    
public:
    explicit TypeEnvironment(std::shared_ptr<TypeEnvironment> parent = nullptr);
    
    // Type variable binding (for generics)
    void bind_type_var(const std::string& name, TypePtr type);
    TypePtr resolve_type_var(const std::string& name) const;
    bool has_type_var(const std::string& name) const;
    void clear_type_vars();
    
    // NEW: Type parameter management (for generic functions)
    void add_type_param(const std::string& name);
    bool is_type_param(const std::string& name) const;
    std::vector<std::string> get_type_params() const { return type_params_; }
    void clear_type_params();
    
    // Type aliases
    void add_alias(const std::string& name, TypePtr type);
    TypePtr resolve_alias(const std::string& name) const;
    
    // Struct/Enum definitions
    void add_struct(const std::string& name, TypePtr type);
    TypePtr get_struct(const std::string& name) const;
    
    void add_enum(const std::string& name, TypePtr type);
    TypePtr get_enum(const std::string& name) const;
    
    // Scope management
    std::shared_ptr<TypeEnvironment> push_scope();
    std::shared_ptr<TypeEnvironment> pop_scope();
    int depth() const { return depth_; }
    
    // Utility
    std::string to_string() const;
};

// =============================================================================
// Type Constraint (for generic inference)
// =============================================================================
enum class ConstraintKind {
    EQUAL,          // T = U
    SUBTYPE,        // T <: U
    COVARIANT,      // T -> U (covariant)
    CONTRAVARIANT,  // T <- U (contravariant)
    NUMERIC,        // T: Numeric
    INTEGRAL,       // T: Integral
    FLOATING,       // T: Floating
    STRINGABLE,     // T: Display
    ITERABLE,       // T: Iterable
    INDEXABLE,      // T: Indexable
    CALLABLE,       // T: Callable
};

class Constraint {
public:
    ConstraintKind kind;
    TypePtr lhs;
    TypePtr rhs;
    SourceSpan span;
    
    Constraint(ConstraintKind k, TypePtr l, TypePtr r, SourceSpan s = {})
        : kind(k), lhs(std::move(l)), rhs(std::move(r)), span(s) {}
    
    std::string to_string() const;
};

// =============================================================================
// Type Inference Context
// =============================================================================
class InferenceContext {
private:
    TypeEnvironment env_;
    std::map<std::string, TypePtr> inferred_types_;
    std::vector<Constraint> constraints_;
    int inference_id_;
    
public:
    InferenceContext();
    
    // Environment
    TypeEnvironment& env() { return env_; }
    const TypeEnvironment& env() const { return env_; }
    
    // Type variable generation
    std::string fresh_type_var();
    
    // Type inference
    void set_type(const std::string& name, TypePtr type);
    TypePtr get_type(const std::string& name) const;
    bool has_type(const std::string& name) const;
    
    // Constraints
    void add_constraint(ConstraintKind kind, TypePtr lhs, TypePtr rhs, SourceSpan span = {});
    const std::vector<Constraint>& constraints() const { return constraints_; }
    std::vector<Constraint>& constraints() { return constraints_; }
    
    // Solve constraints (unification)
    bool solve();
    
    // Substitute inferred types
    TypePtr substitute(TypePtr type) const;
    void substitute_all(std::map<std::string, TypePtr>& types) const;
};

// =============================================================================
// Unification Engine
// =============================================================================
class Unifier {
public:
    struct Substitution {
        std::map<std::string, TypePtr> type_vars;
        std::map<std::string, std::vector<Constraint>> deferred;
    };
    
    // Main unification entry point
    static std::optional<Substitution> unify(TypePtr a, TypePtr b);
    
    // Unification with occurs check
    static std::optional<Substitution> unify_occur_check(
        const std::string& var, TypePtr type);
    
    // Occurs check (prevent infinite types)
    static bool occurs_in(const std::string& var, TypePtr type);
    
    // Apply substitution
    static TypePtr apply(const Substitution& subst, TypePtr type);
    
    // Compose substitutions
    static Substitution compose(const Substitution& a, const Substitution& b);
};

// =============================================================================
// Type Checker (Semantic Analysis)
// =============================================================================
class TypeChecker {
private:
    InferenceContext ctx_;
    std::vector<CompilerError> errors_;
    std::map<std::string, TypePtr> function_signatures_;
    
    // AST node visitors
    TypePtr check_expr(const ast::ExprPtr& expr);
    TypePtr check_stmt(const ast::StmtPtr& stmt);
    
    // Specific checks
    TypePtr check_binary_op(const ast::BinaryExpr& op);
    TypePtr check_unary_op(const ast::UnaryExpr& op);
    TypePtr check_call(const ast::CallExpr& call);
    TypePtr check_index(const ast::IndexExpr& index);
    TypePtr check_field(const ast::MemberExpr& field);
    TypePtr check_function(const ast::FunctionStmt& decl);
    TypePtr check_struct(const ast::FunctionStmt& decl);
    TypePtr check_process(const ast::SerialProcessStmt& process);
    
    // Type coercion
    TypePtr coerce(TypePtr from, TypePtr to, const SourceSpan& span);
    bool can_coerce(TypePtr from, TypePtr to);
    
    // Error reporting
    void type_error(const std::string& msg, const SourceSpan& span);
    void mismatch_error(TypePtr expected, TypePtr found, const SourceSpan& span);
    
public:
    TypeChecker();
    
    // Entry point
    void check(const ast::Program& program);
    
    // Query
    const std::vector<CompilerError>& errors() const { return errors_; }
    bool has_errors() const { return !errors_.empty(); }
    const InferenceContext& context() const { return ctx_; }
    
    // Get inferred type for node (for IDE support)
    TypePtr get_inferred_type(const ast::NodePtr& node) const;
};

// =============================================================================
// Type Error Definitions
// =============================================================================
enum class TypeErrorCode {
    // Mismatches
    TYPE_MISMATCH,
    INCOMPATIBLE_TYPES,
    
    // Operations
    INVALID_BINARY_OP,
    INVALID_UNARY_OP,
    INVALID_INDEX,
    INVALID_CALL,
    INVALID_FIELD_ACCESS,
    
    // Nullability
    NULLABLE_ACCESS,
    REQUIRED_NON_NULL,
    
    // Generics
    CANNOT_INFER_TYPE,
    TYPE_PARAMETER_MISMATCH,
    CONSTRAINT_VIOLATION,
    
    // Definitions
    REDEFINED_TYPE,
    UNDEFINED_TYPE,
    CYCLIC_ALIAS,
    
    // Other
    DIVISION_BY_ZERO,  // Type-level
    TYPE_OVERFLOW,
};

struct TypeError : public CompilerError {
    TypeErrorCode code;
    TypePtr expected;
    TypePtr found;
    
    TypeError(TypeErrorCode c, const std::string& msg, 
              SourceSpan s = {}, TypePtr exp = nullptr, TypePtr fnd = nullptr)
        : CompilerError(msg, s, ErrorSeverity::Error, "TYPE"),
          code(c), expected(exp), found(fnd) {}
};

// =============================================================================
// Built-in Type Implementations
// =============================================================================
class PrimitiveType : public Type {
public:
    explicit PrimitiveType(TypeKind kind, const std::string& name)
        : Type(kind, name) {}
    
    bool is_primitive() const override { return true; }
    bool is_numeric() const override;
    bool is_integer() const override;
    bool is_float() const override;
    bool is_bool() const override { return kind == TypeKind::BOOL; }
    bool is_string() const override { return kind == TypeKind::STRING; }
    bool can_be_zero() const override;
    bool is_copyable() const override { return true; }
    
    bool equals(const TypePtr& other) const override;
    std::string to_string() const override;
    TypePtr clone() const override;
};

class ArrayType : public Type {
public:
    TypePtr element_type;
    int64_t size;
    
    ArrayType(TypePtr element, int64_t sz)
        : Type(TypeKind::ARRAY, "Array"), 
          element_type(std::move(element)), size(sz) {
        name = "[" + std::to_string(size) + "]" + element_type->name;
    }
    
    bool is_array() const override { return true; }
    bool can_be_zero() const override { return size == 0; }
    bool is_copyable() const override { return element_type->is_copyable(); }
    
    bool equals(const TypePtr& other) const override;
    std::string to_string() const override;
    TypePtr clone() const override;
};

class TensorType : public Type {
public:
    TypePtr element_type;
    std::vector<int64_t> shape;  // -1 means dynamic
    
    TensorType(TypePtr element, std::vector<int64_t> sh)
        : Type(TypeKind::TENSOR, "Tensor"),
          element_type(std::move(element)), shape(std::move(sh)) {
        name = "Tensor" + shape_string();
    }
    
    std::string shape_string() const;
    bool is_tensor() const override { return true; }
    bool can_be_zero() const override { return true; }
    bool is_copyable() const override { return element_type->is_copyable(); }
    
    bool equals(const TypePtr& other) const override;
    std::string to_string() const override;
    TypePtr clone() const override;
    
    int64_t num_elements() const;
    bool is_static() const;  // All dimensions known
    bool is_scalar() const;  // Rank 0
    bool is_vector() const;  // Rank 1
    bool is_matrix() const;  // Rank 2
};

class FunctionType : public Type {
public:
    TypePtr input_type;
    TypePtr output_type;
    bool is_pure;  // No side effects
    
    FunctionType(TypePtr input, TypePtr output, bool pure = false)
        : Type(TypeKind::FUNCTION, "Fn"),
          input_type(std::move(input)), 
          output_type(std::move(output)),
          is_pure(pure) {
        name = "fn(" + input_type->to_string() + ") -> " + output_type->to_string();
    }
    
    bool is_function() const override { return true; }
    bool can_be_zero() const override { return false; }
    bool is_copyable() const override { return true; }  // Function pointers
    
    bool equals(const TypePtr& other) const override;
    std::string to_string() const override;
    TypePtr clone() const override;
};

class OptionalType : public Type {
public:
    TypePtr inner_type;
    
    explicit OptionalType(TypePtr inner)
        : Type(TypeKind::OPTIONAL, "Optional"),
          inner_type(std::move(inner)) {
        name = inner_type->name + "?";
    }
    
    bool is_optional() const override { return true; }
    bool can_be_zero() const override { return true; }
    bool is_copyable() const override { return inner_type->is_copyable(); }
    
    bool equals(const TypePtr& other) const override;
    std::string to_string() const override;
    TypePtr clone() const override;
};

class ResultType : public Type {
public:
    TypePtr ok_type;
    TypePtr err_type;
    
    ResultType(TypePtr ok, TypePtr err)
        : Type(TypeKind::RESULT, "Result"),
          ok_type(std::move(ok)), err_type(std::move(err)) {
        name = "Result<" + ok_type->name + ", " + err_type->name + ">";
    }
    
    bool is_result() const override { return true; }
    bool can_be_zero() const override { return false; }
    bool is_copyable() const override { return true; }
    
    bool equals(const TypePtr& other) const override;
    std::string to_string() const override;
    TypePtr clone() const override;
};

class TupleType : public Type {
public:
    std::vector<TypePtr> elements;
    
    explicit TupleType(std::vector<TypePtr> elems)
        : Type(TypeKind::TUPLE, "Tuple"), elements(std::move(elems)) {
        name = "(";
        for (size_t i = 0; i < elements.size(); i++) {
            if (i > 0) name += ", ";
            name += elements[i]->name;
        }
        name += ")";
    }
    
    bool can_be_zero() const override { return elements.empty(); }
    bool is_copyable() const override;
    
    bool equals(const TypePtr& other) const override;
    std::string to_string() const override;
    TypePtr clone() const override;
    
    size_t arity() const { return elements.size(); }
    TypePtr element(size_t i) const { 
        return i < elements.size() ? elements[i] : nullptr; 
    }
};

// =============================================================================
// GenericType - Generic type with type parameters (e.g., Array<T>, Result<T, E>)
// =============================================================================
class GenericType : public Type {
public:
    std::string base_name;           // Base type name (e.g., "Array", "Result")
    std::vector<TypePtr> params;     // Type parameters (e.g., [T] for Array<T>)
    std::vector<TypePtr> args;       // Type arguments (e.g., [i32] for Array<i32>)
    bool is_instantiated;            // True if has concrete type args
    
    explicit GenericType(std::string base, std::vector<TypePtr> type_params = {})
        : Type(TypeKind::GENERIC, ""), 
          base_name(std::move(base)), 
          params(std::move(type_params)),
          is_instantiated(false) {
        // Rebuild name immediately
        name = base_name;
        if (!params.empty()) {
            name += "<";
            for (size_t i = 0; i < params.size(); i++) {
                if (i > 0) name += ", ";
                name += params[i]->name;
            }
            name += ">";
        }
    }
    
    // Instantiate generic with concrete types
    static TypePtr instantiate(TypePtr generic, const std::vector<TypePtr>& type_args);
    
    // Get the i-th type parameter
    TypePtr param(size_t i) const { 
        return i < params.size() ? params[i] : nullptr; 
    }
    
    // Get the i-th type argument (after instantiation)
    TypePtr arg(size_t i) const { 
        return i < args.size() ? args[i] : nullptr; 
    }
    
    // Rebuild name (public for external use)
    void rebuild() {
        name = base_name;
        if (!args.empty()) {
            name += "<";
            for (size_t i = 0; i < args.size(); i++) {
                if (i > 0) name += ", ";
                name += args[i]->name;
            }
            name += ">";
        } else if (!params.empty()) {
            name += "<";
            for (size_t i = 0; i < params.size(); i++) {
                if (i > 0) name += ", ";
                name += params[i]->name;
            }
            name += ">";
        }
    }
    
    bool is_generic() const override { return true; }
    bool can_be_zero() const override { return false; }
    bool is_copyable() const override { return true; }
    
    bool equals(const TypePtr& other) const override;
    std::string to_string() const override;
    TypePtr clone() const override;
};

// =============================================================================
// TypeVar - Type variable (e.g., T, U in generic functions)
// =============================================================================
class TypeVar : public Type {
public:
    std::string var_name;            // Variable name (e.g., "T", "U")
    std::optional<TypePtr> bound;    // Optional type bound (e.g., T: Display)
    int64_t level;                   // Quantification level (for polymorphic inference)
    
    explicit TypeVar(std::string name, std::optional<TypePtr> b = std::nullopt, int64_t lvl = 0)
        : Type(TypeKind::TYPE_VAR, ""), 
          var_name(std::move(name)), 
          bound(std::move(b)),
          level(lvl) {
        name = var_name;
    }
    
    // Check if this is a "free" type variable (unbound)
    bool is_free() const { return !bound.has_value(); }
    
    // Check if this type var is bound to another type
    bool has_bound() const { return bound.has_value(); }
    
    // Get the bound type if any
    TypePtr get_bound() const { 
        return bound.has_value() ? *bound : nullptr; 
    }
    
    bool is_generic() const override { return true; }
    bool can_be_zero() const override { return false; }
    bool is_copyable() const override { return true; }
    
    bool equals(const TypePtr& other) const override;
    std::string to_string() const override;
    TypePtr clone() const override;
};

// =============================================================================
// GenericFunctionType - Generic function type with type parameters
// =============================================================================
class GenericFunctionType : public Type {
public:
    std::vector<TypePtr> type_params;    // Generic type parameters (e.g., T, U)
    TypePtr inner_function;              // The underlying function type
    
    explicit GenericFunctionType(std::vector<TypePtr> params, TypePtr func)
        : Type(TypeKind::FUNCTION, ""), 
          type_params(std::move(params)),
          inner_function(std::move(func)) {
        rebuild();
    }
    
    // Rebuild name (public)
    void rebuild() {
        std::string s = "Fn";
        if (!type_params.empty()) {
            s += "<";
            for (size_t i = 0; i < type_params.size(); i++) {
                if (i > 0) s += ", ";
                s += type_params[i]->name;
            }
            s += ">";
        }
        if (inner_function) {
            s += " -> " + inner_function->to_string();
        }
        name = s;
    }
    
    bool is_function() const override { return true; }
    bool is_generic() const override { return true; }
    bool can_be_zero() const override { return false; }
    
    bool equals(const TypePtr& other) const override;
    std::string to_string() const override;
    TypePtr clone() const override;
    
    // Instantiate with concrete types
    TypePtr instantiate(const std::vector<TypePtr>& args) const;
};

// =============================================================================
// Utility Functions
// =============================================================================

// Get the "default" type for a literal value
TypePtr infer_literal_type(const ast::LiteralExpr& literal);

// Get common supertype (join in type lattice)
TypePtr common_supertype(TypePtr a, TypePtr b);

// Get the smaller subtype (meet in type lattice)
TypePtr common_subtype(TypePtr a, TypePtr b);

// Check if type needs drop (cleanup)
bool needs_drop(const TypePtr& type);

// Get size of type in bytes (if known)
std::optional<int64_t> type_size(const TypePtr& type);

// Get alignment of type in bytes (if known)
std::optional<int64_t> type_alignment(const TypePtr& type);

// =============================================================================
// Inline Implementations
// =============================================================================

inline bool Type::is_primitive() const {
    return kind >= TypeKind::UNIT && kind <= TypeKind::CHAR;
}

inline bool Type::is_numeric() const {
    return is_integer() || is_float();
}

inline bool Type::is_integer() const {
    return (kind >= TypeKind::INT8 && kind <= TypeKind::UINT64);
}

inline bool Type::is_float() const {
    return kind >= TypeKind::FLOAT16 && kind <= TypeKind::FLOAT64;
}

inline bool Type::is_array() const { return kind == TypeKind::ARRAY; }
inline bool Type::is_tensor() const { return kind == TypeKind::TENSOR; }
inline bool Type::is_function() const { return kind == TypeKind::FUNCTION; }
inline bool Type::is_process() const { return kind == TypeKind::PROCESS; }
inline bool Type::is_struct() const { return kind == TypeKind::STRUCT; }
inline bool Type::is_enum() const { return kind == TypeKind::ENUM; }
inline bool Type::is_optional() const { return kind == TypeKind::OPTIONAL; }
inline bool Type::is_result() const { return kind == TypeKind::RESULT; }
inline bool Type::is_string() const { return kind == TypeKind::STRING; }
inline bool Type::is_bool() const { return kind == TypeKind::BOOL; }
inline bool Type::is_copyable() const { return true; }
inline bool Type::is_generic() const { return kind == TypeKind::GENERIC || kind == TypeKind::TYPE_VAR; }
inline bool Type::is_type_var() const { return kind == TypeKind::TYPE_VAR; }

inline bool Type::can_be_zero() const {
    return is_numeric() || is_string() || is_optional();
}

// =============================================================================
// Type Utilities
// =============================================================================

// Promote integers to larger types
TypePtr promote_int(TypePtr a, TypePtr b);

// Numeric type coercion for binary ops
TypePtr numeric_coerce(TypePtr a, TypePtr b);

// Check if assignment is valid
bool can_assign(TypePtr target, TypePtr source);

// Check if types can be indexed
bool can_index(TypePtr type, TypePtr index_type);

// =============================================================================
// Type Display (for debugging/error messages)
// =============================================================================
std::string type_kind_name(TypeKind kind);

} // namespace type
} // namespace claw

#endif // CLAW_TYPE_SYSTEM_H
