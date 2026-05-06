// Type Checker Implementation - Core type checking logic
// Completes the TypeChecker framework from type_system.h

#include "type/type_system.h"
#include <algorithm>
#include <cmath>

namespace claw {
namespace type {

// =============================================================================
// Type Static Method Implementations (needed by TypeChecker)
// =============================================================================

TypePtr Type::unit() { return TypeCache::instance().get_unit(); }
TypePtr Type::boolean() { return TypeCache::instance().get_bool(); }
TypePtr Type::int64() { return TypeCache::instance().get_int64(); }
TypePtr Type::float64() { return TypeCache::instance().get_float64(); }
TypePtr Type::string() { return TypeCache::instance().get_string(); }
TypePtr Type::unknown() { return TypeCache::instance().get_unknown(); }

// Virtual method stubs
bool Type::is_reference() const { return false; }
bool Type::equals(const TypePtr& other) const { return other.get() == this; }
bool Type::is_subtype_of(const TypePtr& other) const { return equals(other); }
bool Type::is_compatible_with(const TypePtr& other) const { return equals(other); }
std::string Type::signature() const { return to_string(); }
TypePtr Type::clone() const { return std::make_shared<Type>(kind, name); }

std::string Type::to_string() const { return name; }

// =============================================================================
// TypeCache Implementation
// =============================================================================

TypeCache::TypeCache() {
    // Initialize primitive types
    primitives_[TypeKind::UNIT] = std::make_shared<Type>(TypeKind::UNIT, "()");
    primitives_[TypeKind::BOOL] = std::make_shared<Type>(TypeKind::BOOL, "bool");
    primitives_[TypeKind::INT8] = std::make_shared<Type>(TypeKind::INT8, "i8");
    primitives_[TypeKind::INT16] = std::make_shared<Type>(TypeKind::INT16, "i16");
    primitives_[TypeKind::INT32] = std::make_shared<Type>(TypeKind::INT32, "i32");
    primitives_[TypeKind::INT64] = std::make_shared<Type>(TypeKind::INT64, "i64");
    primitives_[TypeKind::UINT8] = std::make_shared<Type>(TypeKind::UINT8, "u8");
    primitives_[TypeKind::UINT16] = std::make_shared<Type>(TypeKind::UINT16, "u16");
    primitives_[TypeKind::UINT32] = std::make_shared<Type>(TypeKind::UINT32, "u32");
    primitives_[TypeKind::UINT64] = std::make_shared<Type>(TypeKind::UINT64, "u64");
    primitives_[TypeKind::FLOAT16] = std::make_shared<Type>(TypeKind::FLOAT16, "f16");
    primitives_[TypeKind::FLOAT32] = std::make_shared<Type>(TypeKind::FLOAT32, "f32");
    primitives_[TypeKind::FLOAT64] = std::make_shared<Type>(TypeKind::FLOAT64, "f64");
    primitives_[TypeKind::STRING] = std::make_shared<Type>(TypeKind::STRING, "string");
    primitives_[TypeKind::CHAR] = std::make_shared<Type>(TypeKind::CHAR, "char");
    primitives_[TypeKind::NEVER] = std::make_shared<Type>(TypeKind::NEVER, "never");
    primitives_[TypeKind::UNKNOWN] = std::make_shared<Type>(TypeKind::UNKNOWN, "unknown");
}

TypeCache& TypeCache::instance() {
    static TypeCache instance;
    return instance;
}

TypePtr TypeCache::get_array(TypePtr element, int64_t size) {
    auto key = std::make_pair(element, size);
    if (array_types_.count(key) == 0) {
        array_types_[key] = std::make_shared<ArrayType>(element, size);
    }
    return array_types_[key];
}

TypePtr TypeCache::get_tensor(TypePtr element, std::vector<int64_t> shape) {
    auto key = std::make_pair(element, shape);
    if (tensor_types_.count(key) == 0) {
        tensor_types_[key] = std::make_shared<TensorType>(element, shape);
    }
    return tensor_types_[key];
}

TypePtr TypeCache::get_tuple(std::vector<TypePtr> elements) {
    if (elements.empty()) {
        return primitives_[TypeKind::UNIT];
    }
    // Use vector size as simple key for now
    size_t key = elements.size();
    static std::map<size_t, TypePtr> tuple_cache;
    if (tuple_cache.count(key) == 0) {
        tuple_cache[key] = std::make_shared<TupleType>(elements);
    }
    return tuple_cache[key];
}

TypePtr TypeCache::get_function(TypePtr input, TypePtr output) {
    auto key = std::make_pair(input, output);
    if (function_types_.count(key) == 0) {
        function_types_[key] = std::make_shared<FunctionType>(input, output);
    }
    return function_types_[key];
}

TypePtr TypeCache::get_optional(TypePtr inner) {
    if (optional_types_.count(inner) == 0) {
        optional_types_[inner] = std::make_shared<OptionalType>(inner);
    }
    return optional_types_[inner];
}

TypePtr TypeCache::get_result(TypePtr ok, TypePtr err) {
    auto key = std::make_pair(ok, err);
    if (result_types_.count(key) == 0) {
        result_types_[key] = std::make_shared<ResultType>(ok, err);
    }
    return result_types_[key];
}

TypePtr TypeCache::get_primitive(TypeKind kind) {
    if (primitives_.count(kind) > 0) {
        return primitives_[kind];
    }
    return primitives_[TypeKind::UNKNOWN];
}

TypePtr TypeCache::parse_type(const std::string& str) {
    // Simple parser for basic types
    if (str == "()" || str == "unit") return get_unit();
    if (str == "bool") return get_bool();
    if (str == "i8") return get_int8();
    if (str == "i16") return get_int16();
    if (str == "i32") return get_int32();
    if (str == "i64") return get_int64();
    if (str == "u8") return get_uint8();
    if (str == "u16") return get_uint16();
    if (str == "u32") return get_uint32();
    if (str == "u64") return get_uint64();
    if (str == "f16") return get_float16();
    if (str == "f32") return get_float32();
    if (str == "f64") return get_float64();
    if (str == "string") return get_string();
    if (str == "char") return get_char();
    // TODO: Handle compound types
    return get_unknown();
}

// =============================================================================
// TypeEnvironment Implementation
// =============================================================================

TypeEnvironment::TypeEnvironment(std::shared_ptr<TypeEnvironment> parent)
    : parent_(parent), depth_(parent ? parent->depth_ + 1 : 0) {}

void TypeEnvironment::bind_type_var(const std::string& name, TypePtr type) {
    type_vars_[name] = type;
}

TypePtr TypeEnvironment::resolve_type_var(const std::string& name) const {
    auto it = type_vars_.find(name);
    if (it != type_vars_.end()) return it->second;
    if (parent_) return parent_->resolve_type_var(name);
    return nullptr;
}

bool TypeEnvironment::has_type_var(const std::string& name) const {
    if (type_vars_.find(name) != type_vars_.end()) return true;
    if (parent_) return parent_->has_type_var(name);
    return false;
}

void TypeEnvironment::clear_type_vars() {
    type_vars_.clear();
}

// =============================================================================
// TypeParameter Management (for generic functions)
// =============================================================================
void TypeEnvironment::add_type_param(const std::string& name) {
    type_params_.push_back(name);
}

bool TypeEnvironment::is_type_param(const std::string& name) const {
    for (const auto& param : type_params_) {
        if (param == name) return true;
    }
    if (parent_) return parent_->is_type_param(name);
    return false;
}

void TypeEnvironment::clear_type_params() {
    type_params_.clear();
}

void TypeEnvironment::add_alias(const std::string& name, TypePtr type) {
    type_aliases_[name] = type;
}

TypePtr TypeEnvironment::resolve_alias(const std::string& name) const {
    auto it = type_aliases_.find(name);
    if (it != type_aliases_.end()) return it->second;
    if (parent_) return parent_->resolve_alias(name);
    return nullptr;
}

void TypeEnvironment::add_struct(const std::string& name, TypePtr type) {
    struct_types_[name] = type;
}

TypePtr TypeEnvironment::get_struct(const std::string& name) const {
    auto it = struct_types_.find(name);
    if (it != struct_types_.end()) return it->second;
    if (parent_) return parent_->get_struct(name);
    return nullptr;
}

void TypeEnvironment::add_enum(const std::string& name, TypePtr type) {
    enum_types_[name] = type;
}

TypePtr TypeEnvironment::get_enum(const std::string& name) const {
    auto it = enum_types_.find(name);
    if (it != enum_types_.end()) return it->second;
    if (parent_) return parent_->get_enum(name);
    return nullptr;
}

std::shared_ptr<TypeEnvironment> TypeEnvironment::push_scope() {
    return std::make_shared<TypeEnvironment>(std::shared_ptr<TypeEnvironment>(this));
}

std::shared_ptr<TypeEnvironment> TypeEnvironment::pop_scope() {
    return parent_;
}

std::string TypeEnvironment::to_string() const {
    std::string result = "TypeEnvironment(depth=" + std::to_string(depth_) + ")\n";
    result += "  Type params: " + std::to_string(type_params_.size()) + "\n";
    result += "  Type vars: " + std::to_string(type_vars_.size()) + "\n";
    result += "  Aliases: " + std::to_string(type_aliases_.size()) + "\n";
    result += "  Structs: " + std::to_string(struct_types_.size()) + "\n";
    result += "  Enums: " + std::to_string(enum_types_.size()) + "\n";
    return result;
}

// =============================================================================
// Constraint Implementation
// =============================================================================

std::string Constraint::to_string() const {
    std::string kind_str;
    switch (kind) {
        case ConstraintKind::EQUAL: kind_str = "="; break;
        case ConstraintKind::SUBTYPE: kind_str = "<:"; break;
        case ConstraintKind::COVARIANT: kind_str = "->"; break;
        case ConstraintKind::CONTRAVARIANT: kind_str = "<-"; break;
        default: kind_str = "?"; break;
    }
    return lhs->to_string() + " " + kind_str + " " + rhs->to_string();
}

// =============================================================================
// InferenceContext Implementation
// =============================================================================

InferenceContext::InferenceContext() : inference_id_(0) {}

std::string InferenceContext::fresh_type_var() {
    return "_T" + std::to_string(++inference_id_);
}

void InferenceContext::set_type(const std::string& name, TypePtr type) {
    inferred_types_[name] = type;
}

TypePtr InferenceContext::get_type(const std::string& name) const {
    auto it = inferred_types_.find(name);
    if (it != inferred_types_.end()) return it->second;
    return env_.resolve_type_var(name);
}

bool InferenceContext::has_type(const std::string& name) const {
    return inferred_types_.find(name) != inferred_types_.end() || env_.has_type_var(name);
}

void InferenceContext::add_constraint(ConstraintKind kind, TypePtr lhs, TypePtr rhs, SourceSpan span) {
    constraints_.emplace_back(kind, lhs, rhs, span);
}

bool InferenceContext::solve() {
    // Simplified unification solver
    for (const auto& constraint : constraints_) {
        if (constraint.kind == ConstraintKind::EQUAL) {
            auto subst = Unifier::unify(constraint.lhs, constraint.rhs);
            if (!subst) {
                return false;
            }
        }
    }
    return true;
}

TypePtr InferenceContext::substitute(TypePtr type) const {
    if (!type) return nullptr;
    return type;
}

void InferenceContext::substitute_all(std::map<std::string, TypePtr>& types) const {
    for (auto& pair : types) {
        pair.second = substitute(pair.second);
    }
}

// =============================================================================
// Unifier Implementation
// =============================================================================

std::optional<Unifier::Substitution> Unifier::unify(TypePtr a, TypePtr b) {
    if (!a || !b) return std::nullopt;
    
    if (a->equals(b)) return Substitution{};
    
    // Handle type variables
    if (a->name.find("_T") == 0) {
        return unify_occur_check(a->name, b);
    }
    if (b->name.find("_T") == 0) {
        return unify_occur_check(b->name, a);
    }
    
    // Structural unification for compound types
    if (a->is_array() && b->is_array()) {
        return Substitution{};
    }
    
    if (a->is_function() && b->is_function()) {
        return Substitution{};
    }
    
    return std::nullopt;
}

std::optional<Unifier::Substitution> Unifier::unify_occur_check(
    const std::string& var, TypePtr type) {
    if (occurs_in(var, type)) {
        return std::nullopt;
    }
    Substitution subst;
    subst.type_vars[var] = type;
    return subst;
}

bool Unifier::occurs_in(const std::string& var, TypePtr type) {
    if (!type) return false;
    if (type->name == var) return true;
    
    for (const auto& tp : type->type_params) {
        if (occurs_in(var, tp)) return true;
    }
    for (const auto& ta : type->type_args) {
        if (occurs_in(var, ta)) return true;
    }
    
    return false;
}

TypePtr Unifier::apply(const Substitution& subst, TypePtr type) {
    if (!type) return nullptr;
    
    if (type->name.find("_T") == 0) {
        auto it = subst.type_vars.find(type->name);
        if (it != subst.type_vars.end()) {
            return it->second;
        }
    }
    
    auto result = type->clone();
    for (auto& tp : result->type_params) {
        tp = apply(subst, tp);
    }
    for (auto& ta : result->type_args) {
        ta = apply(subst, ta);
    }
    
    return result;
}

Unifier::Substitution Unifier::compose(const Substitution& a, const Substitution& b) {
    Substitution result;
    result.type_vars = b.type_vars;
    for (const auto& pair : a.type_vars) {
        auto it = result.type_vars.find(pair.first);
        if (it != result.type_vars.end()) {
            result.type_vars[pair.first] = apply(b, pair.second);
        } else {
            result.type_vars[pair.first] = pair.second;
        }
    }
    return result;
}

// =============================================================================
// TypeChecker Implementation (Stub - requires AST integration)
// =============================================================================

TypeChecker::TypeChecker() {}

// Placeholder implementations - full versions require AST headers
void TypeChecker::check(const ast::Program& program) {
    (void)program;
    // Would traverse AST and check types
}

TypePtr TypeChecker::check_expr(const ast::ExprPtr& expr) {
    (void)expr;
    return Type::unknown();
}

TypePtr TypeChecker::check_stmt(const ast::StmtPtr& stmt) {
    (void)stmt;
    return Type::unit();
}

TypePtr TypeChecker::check_binary_op(const ast::BinaryExpr& op) {
    (void)op;
    return Type::unknown();
}

TypePtr TypeChecker::check_unary_op(const ast::UnaryExpr& op) {
    (void)op;
    return Type::unknown();
}

TypePtr TypeChecker::check_call(const ast::CallExpr& call) {
    (void)call;
    return Type::unknown();
}

TypePtr TypeChecker::check_index(const ast::IndexExpr& index) {
    (void)index;
    return Type::unknown();
}

TypePtr TypeChecker::check_field(const ast::MemberExpr& field) {
    (void)field;
    return Type::unknown();
}

TypePtr TypeChecker::check_function(const ast::FunctionStmt& decl) {
    (void)decl;
    return Type::unit();
}

TypePtr TypeChecker::check_struct(const ast::FunctionStmt& decl) {
    (void)decl;
    return Type::unit();
}

TypePtr TypeChecker::check_process(const ast::SerialProcessStmt& process) {
    (void)process;
    return Type::unit();
}

TypePtr TypeChecker::coerce(TypePtr from, TypePtr to, const SourceSpan& span) {
    (void)span;
    if (from->equals(to)) return from;
    if (from->is_numeric() && to->is_numeric()) return to;
    if (from->is_string() && to->is_string()) return to;
    return from;
}

bool TypeChecker::can_coerce(TypePtr from, TypePtr to) {
    if (from->equals(to)) return true;
    if (from->is_numeric() && to->is_numeric()) return true;
    if (from->is_string() && to->is_string()) return true;
    return false;
}

void TypeChecker::type_error(const std::string& msg, const SourceSpan& span) {
    errors_.emplace_back(msg, span, ErrorSeverity::Error, "TYPE");
}

void TypeChecker::mismatch_error(TypePtr expected, TypePtr found, const SourceSpan& span) {
    std::string msg = "Type mismatch: expected " + expected->to_string() + 
                      ", found " + found->to_string();
    errors_.emplace_back(msg, span, ErrorSeverity::Error, "TYPE");
}

TypePtr TypeChecker::get_inferred_type(const ast::NodePtr& node) const {
    (void)node;
    return Type::unknown();
}

// =============================================================================
// Concrete Type Implementations
// =============================================================================

bool PrimitiveType::is_numeric() const {
    return kind >= TypeKind::INT8 && kind <= TypeKind::FLOAT64;
}

bool PrimitiveType::is_integer() const {
    return (kind >= TypeKind::INT8 && kind <= TypeKind::UINT64);
}

bool PrimitiveType::is_float() const {
    return (kind >= TypeKind::FLOAT16 && kind <= TypeKind::FLOAT64);
}

bool PrimitiveType::can_be_zero() const {
    return kind != TypeKind::NEVER;
}

bool PrimitiveType::equals(const TypePtr& other) const {
    if (!other) return false;
    return kind == other->kind;
}

std::string PrimitiveType::to_string() const {
    switch (kind) {
        case TypeKind::UNIT: return "()";
        case TypeKind::BOOL: return "bool";
        case TypeKind::INT8: return "i8";
        case TypeKind::INT16: return "i16";
        case TypeKind::INT32: return "i32";
        case TypeKind::INT64: return "i64";
        case TypeKind::UINT8: return "u8";
        case TypeKind::UINT16: return "u16";
        case TypeKind::UINT32: return "u32";
        case TypeKind::UINT64: return "u64";
        case TypeKind::FLOAT16: return "f16";
        case TypeKind::FLOAT32: return "f32";
        case TypeKind::FLOAT64: return "f64";
        case TypeKind::STRING: return "string";
        case TypeKind::CHAR: return "char";
        default: return name;
    }
}

TypePtr PrimitiveType::clone() const {
    return std::make_shared<PrimitiveType>(kind, name);
}

// ArrayType
bool ArrayType::equals(const TypePtr& other) const {
    if (!other || !other->is_array()) return false;
    return element_type->equals(other->type_args.empty() ? nullptr : other->type_args[0])
           && static_cast<size_t>(size) == other->type_args.size();
}

std::string ArrayType::to_string() const {
    return "[" + std::to_string(size) + "]" + element_type->to_string();
}

TypePtr ArrayType::clone() const {
    return std::make_shared<ArrayType>(element_type->clone(), size);
}

// TensorType
std::string TensorType::shape_string() const {
    if (shape.empty()) return "<>";
    std::string s = "<";
    for (size_t i = 0; i < shape.size(); i++) {
        if (i > 0) s += "x";
        s += shape[i] < 0 ? "?" : std::to_string(shape[i]);
    }
    return s + ">";
}

bool TensorType::equals(const TypePtr& other) const {
    if (!other || !other->is_tensor()) return false;
    if (shape.size() != other->type_args.size()) return false;
    return element_type->equals(other->type_args.empty() ? nullptr : other->type_args[0]);
}

std::string TensorType::to_string() const {
    return "Tensor" + shape_string() + "<" + element_type->to_string() + ">";
}

TypePtr TensorType::clone() const {
    return std::make_shared<TensorType>(element_type->clone(), shape);
}

int64_t TensorType::num_elements() const {
    int64_t n = 1;
    for (int64_t d : shape) {
        if (d < 0) return -1;
        n *= d;
    }
    return n;
}

bool TensorType::is_static() const {
    for (int64_t d : shape) {
        if (d < 0) return false;
    }
    return true;
}

bool TensorType::is_scalar() const { return shape.empty(); }
bool TensorType::is_vector() const { return shape.size() == 1; }
bool TensorType::is_matrix() const { return shape.size() == 2; }

// FunctionType
bool FunctionType::equals(const TypePtr& other) const {
    if (!other || !other->is_function()) return false;
    return input_type->equals(other->type_args.empty() ? nullptr : other->type_args[0])
           && output_type->equals(other->type_args.size() > 1 ? other->type_args[1] : nullptr);
}

std::string FunctionType::to_string() const {
    return "fn(" + input_type->to_string() + ") -> " + output_type->to_string();
}

TypePtr FunctionType::clone() const {
    return std::make_shared<FunctionType>(input_type->clone(), output_type->clone(), is_pure);
}

// OptionalType
bool OptionalType::equals(const TypePtr& other) const {
    if (!other || !other->is_optional()) return false;
    return inner_type->equals(other->type_args.empty() ? nullptr : other->type_args[0]);
}

std::string OptionalType::to_string() const {
    return inner_type->to_string() + "?";
}

TypePtr OptionalType::clone() const {
    return std::make_shared<OptionalType>(inner_type->clone());
}

// ResultType
bool ResultType::equals(const TypePtr& other) const {
    if (!other || !other->is_result()) return false;
    return ok_type->equals(other->type_args.empty() ? nullptr : other->type_args[0])
           && err_type->equals(other->type_args.size() > 1 ? other->type_args[1] : nullptr);
}

std::string ResultType::to_string() const {
    return "Result<" + ok_type->to_string() + ", " + err_type->to_string() + ">";
}

TypePtr ResultType::clone() const {
    return std::make_shared<ResultType>(ok_type->clone(), err_type->clone());
}

// TupleType
bool TupleType::equals(const TypePtr& other) const {
    if (!other || (other->kind != TypeKind::TUPLE)) return false;
    if (elements.size() != other->type_args.size()) return false;
    for (size_t i = 0; i < elements.size(); i++) {
        if (!elements[i]->equals(other->type_args[i])) return false;
    }
    return true;
}

bool TupleType::is_copyable() const {
    for (const auto& e : elements) {
        if (!e->is_copyable()) return false;
    }
    return true;
}

std::string TupleType::to_string() const {
    std::string s = "(";
    for (size_t i = 0; i < elements.size(); i++) {
        if (i > 0) s += ", ";
        s += elements[i]->to_string();
    }
    if (elements.size() == 1) s += ",";
    s += ")";
    return s;
}

TypePtr TupleType::clone() const {
    std::vector<TypePtr> cloned;
    for (const auto& e : elements) {
        cloned.push_back(e->clone());
    }
    return std::make_shared<TupleType>(cloned);
}

// =============================================================================
// Utility Functions
// =============================================================================

TypePtr infer_literal_type(const ast::LiteralExpr& literal) {
    (void)literal;
    return Type::unknown();
}

TypePtr common_supertype(TypePtr a, TypePtr b) {
    if (!a || !b) return Type::unknown();
    if (a->equals(b)) return a;
    return Type::float64();
}

TypePtr common_subtype(TypePtr a, TypePtr b) {
    (void)a;
    (void)b;
    return Type::unknown();
}

bool needs_drop(const TypePtr& type) {
    (void)type;
    return false;
}

std::optional<int64_t> type_size(const TypePtr& type) {
    if (!type) return std::nullopt;
    switch (type->kind) {
        case TypeKind::BOOL:
        case TypeKind::INT8:
        case TypeKind::UINT8:
            return 1;
        case TypeKind::INT16:
        case TypeKind::UINT16:
            return 2;
        case TypeKind::INT32:
        case TypeKind::UINT32:
        case TypeKind::FLOAT32:
            return 4;
        case TypeKind::INT64:
        case TypeKind::UINT64:
        case TypeKind::FLOAT64:
            return 8;
        default:
            return std::nullopt;
    }
}

std::optional<int64_t> type_alignment(const TypePtr& type) {
    return type_size(type);
}

TypePtr promote_int(TypePtr a, TypePtr b) {
    if (!a || !b) return Type::unknown();
    if (a->is_float()) return a;
    if (b->is_float()) return b;
    return Type::int64();
}

TypePtr numeric_coerce(TypePtr a, TypePtr b) {
    if (!a || !b) return Type::unknown();
    if (a->is_float() && b->is_float()) {
        if (a->kind == TypeKind::FLOAT64 || b->kind == TypeKind::FLOAT64) {
            return Type::float64();
        }
        return TypeCache::instance().get_float32();
    }
    if (a->is_numeric() && b->is_numeric()) {
        return promote_int(a, b);
    }
    return Type::unknown();
}

bool can_assign(TypePtr target, TypePtr source) {
    if (!target || !source) return false;
    if (target->equals(source)) return true;
    return source->is_subtype_of(target);
}

bool can_index(TypePtr type, TypePtr index_type) {
    if (!type || !index_type) return false;
    if (type->is_array() || type->is_tensor() || type->is_string()) {
        return index_type->is_integer();
    }
    return false;
}

std::string type_kind_name(TypeKind kind) {
    switch (kind) {
        case TypeKind::UNIT: return "unit";
        case TypeKind::BOOL: return "bool";
        case TypeKind::INT8: return "i8";
        case TypeKind::INT16: return "i16";
        case TypeKind::INT32: return "i32";
        case TypeKind::INT64: return "i64";
        case TypeKind::UINT8: return "u8";
        case TypeKind::UINT16: return "u16";
        case TypeKind::UINT32: return "u32";
        case TypeKind::UINT64: return "u64";
        case TypeKind::FLOAT16: return "f16";
        case TypeKind::FLOAT32: return "f32";
        case TypeKind::FLOAT64: return "f64";
        case TypeKind::STRING: return "string";
        case TypeKind::CHAR: return "char";
        case TypeKind::ARRAY: return "array";
        case TypeKind::TENSOR: return "tensor";
        case TypeKind::TUPLE: return "tuple";
        case TypeKind::OPTIONAL: return "optional";
        case TypeKind::RESULT: return "result";
        case TypeKind::FUNCTION: return "function";
        case TypeKind::PROCESS: return "process";
        case TypeKind::STRUCT: return "struct";
        case TypeKind::ENUM: return "enum";
        case TypeKind::ALIAS: return "alias";
        case TypeKind::GENERIC: return "generic";  // NEW
        case TypeKind::TYPE_VAR: return "type_var";  // NEW
        case TypeKind::UNKNOWN: return "unknown";
        case TypeKind::NEVER: return "never";
        default: return "<?>";
    }
}

// =============================================================================
// GenericType Implementation
// =============================================================================

TypePtr GenericType::instantiate(TypePtr generic, const std::vector<TypePtr>& type_args) {
    if (!generic || generic->kind != TypeKind::GENERIC) return nullptr;
    auto gen = std::dynamic_pointer_cast<GenericType>(generic);
    if (!gen) return nullptr;
    
    auto result = std::make_shared<GenericType>(gen->base_name, gen->params);
    result->args = type_args;
    result->is_instantiated = true;
    result->rebuild();
    return result;
}

bool GenericType::equals(const TypePtr& other) const {
    if (!other || other->kind != TypeKind::GENERIC) return false;
    auto o = std::dynamic_pointer_cast<GenericType>(other);
    if (!o) return false;
    if (base_name != o->base_name) return false;
    if (args.size() != o->args.size()) return false;
    for (size_t i = 0; i < args.size(); i++) {
        if (!args[i]->equals(o->args[i])) return false;
    }
    return true;
}

std::string GenericType::to_string() const {
    std::string s = base_name;
    if (!args.empty()) {
        s += "<";
        for (size_t i = 0; i < args.size(); i++) {
            if (i > 0) s += ", ";
            s += args[i]->to_string();
        }
        s += ">";
    } else if (!params.empty()) {
        s += "<";
        for (size_t i = 0; i < params.size(); i++) {
            if (i > 0) s += ", ";
            s += params[i]->to_string();
        }
        s += ">";
    }
    return s;
}

TypePtr GenericType::clone() const {
    auto clone = std::make_shared<GenericType>(base_name, params);
    clone->args = args;
    clone->is_instantiated = is_instantiated;
    return clone;
}

// =============================================================================
// TypeVar Implementation
// =============================================================================
bool TypeVar::equals(const TypePtr& other) const {
    if (!other) return false;
    if (other->kind == TypeKind::TYPE_VAR) {
        auto o = std::dynamic_pointer_cast<TypeVar>(other);
        return o && var_name == o->var_name && level == o->level;
    }
    // If bound, compare with bound type
    if (bound.has_value()) {
        return (*bound)->equals(other);
    }
    return false;
}

std::string TypeVar::to_string() const {
    std::string s = var_name;
    if (bound.has_value()) {
        s += ": " + (*bound)->to_string();
    }
    return s;
}

TypePtr TypeVar::clone() const {
    return std::make_shared<TypeVar>(var_name, bound, level);
}

// =============================================================================
// GenericFunctionType Implementation
// =============================================================================
// Note: rebuild() is now inline in the header

bool GenericFunctionType::equals(const TypePtr& other) const {
    if (!other || other->kind != TypeKind::FUNCTION) return false;
    auto o = std::dynamic_pointer_cast<GenericFunctionType>(other);
    if (!o) return false;
    if (type_params.size() != o->type_params.size()) return false;
    return inner_function && inner_function->equals(o->inner_function);
}

std::string GenericFunctionType::to_string() const {
    return name;
}

TypePtr GenericFunctionType::clone() const {
    return std::make_shared<GenericFunctionType>(type_params, inner_function);
}

TypePtr GenericFunctionType::instantiate(const std::vector<TypePtr>& args) const {
    // Instantiate: replace type params with concrete args
    if (args.size() != type_params.size()) return nullptr;
    // Simplified: just return the inner function for now
    return inner_function;
}

// =============================================================================
// TypeCache Generic Methods Implementation
// =============================================================================
TypePtr TypeCache::get_generic(const std::string& base_name, std::vector<TypePtr> params) {
    // Check cache first
    std::string key = base_name;
    if (!params.empty()) {
        key += "<";
        for (size_t i = 0; i < params.size(); i++) {
            if (i > 0) key += ", ";
            key += params[i]->name;
        }
        key += ">";
    }
    
    auto it = generic_types_.find(key);
    if (it != generic_types_.end()) {
        return it->second;
    }
    
    // Create new generic type
    auto gen = std::make_shared<GenericType>(base_name, std::move(params));
    generic_types_[key] = gen;
    return gen;
}

TypePtr TypeCache::get_type_var(const std::string& name, std::optional<TypePtr> bound) {
    auto it = type_vars_.find(name);
    if (it != type_vars_.end()) {
        return it->second;
    }
    
    auto tv = std::make_shared<TypeVar>(name, bound);
    type_vars_[name] = tv;
    return tv;
}

TypePtr TypeCache::make_generic_instance(const std::string& base_name, const std::vector<TypePtr>& args) {
    // Create a generic type and mark it as instantiated
    std::vector<TypePtr> params;
    char param_name = 'T';
    for (size_t i = 0; i < args.size(); i++) {
        params.push_back(get_type_var(std::string(1, param_name + i)));
    }
    
    auto gen = std::make_shared<GenericType>(base_name, params);
    gen->args = args;
    gen->is_instantiated = true;
    gen->rebuild();
    return gen;
}

bool TypeCache::is_generic_type(const std::string& name) const {
    // Check if name is a known generic type (Array, Result, Option, etc.)
    static const std::set<std::string> known_generics = {
        "Array", "Vec", "Option", "Result", "Box", "Rc", "Arc"
    };
    return known_generics.count(name) > 0;
}

} // namespace type
} // namespace claw
