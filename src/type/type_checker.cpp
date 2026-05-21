// Type Checker Implementation - Core type checking logic
// Completes the TypeChecker framework from type_system.h

#include "type/type_system.h"
#include "type/pattern_checker.h"
#include "ast/ast.h"
#include "ast/pattern.h"
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

TypePtr TypeCache::get_future(TypePtr inner) {
    if (future_types_.count(inner) == 0) {
        future_types_[inner] = std::make_shared<FutureType>(inner);
    }
    return future_types_[inner];
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
// TypeChecker Implementation
// =============================================================================

TypeChecker::TypeChecker() {}

// Variable scopes
static std::vector<std::unordered_map<std::string, TypePtr>> var_scopes;
static std::unordered_map<std::string, std::vector<std::pair<std::string, TypePtr>>> struct_fields;
static std::unordered_map<std::string, std::vector<std::string>> enum_variants;
static std::unordered_map<std::string, TypePtr> function_sigs;
static bool in_async_function = false;

static void push_scope() { var_scopes.emplace_back(); }
static void pop_scope() { if (!var_scopes.empty()) var_scopes.pop_back(); }
static void define_var(const std::string& name, TypePtr type) {
    if (!var_scopes.empty()) var_scopes.back()[name] = type;
}
static TypePtr lookup_var(const std::string& name) {
    for (auto it = var_scopes.rbegin(); it != var_scopes.rend(); ++it) {
        auto jt = it->find(name);
        if (jt != it->end()) return jt->second;
    }
    return nullptr;
}

static TypePtr parse_type_name(const std::string& name) {
    if (name == "i64" || name == "int") return Type::int64();
    if (name == "f64" || name == "float") return Type::float64();
    if (name == "bool") return Type::boolean();
    if (name == "string" || name == "str") return Type::string();
    if (name.empty()) return Type::unknown();
    return TypeCache::instance().parse_type(name);
}

void TypeChecker::check(const ast::Program& program) {
    var_scopes.clear();
    struct_fields.clear();
    enum_variants.clear();
    function_sigs.clear();
    push_scope();

    const auto& decls = program.get_declarations();

    // First pass: register struct, enum, and function signatures
    for (const auto& stmt : decls) {
        if (!stmt) continue;
        if (stmt->get_kind() == ast::Statement::Kind::Struct) {
            auto* s = static_cast<const ast::StructStmt*>(stmt.get());
            std::vector<std::pair<std::string, TypePtr>> fields;
            for (const auto& f : s->get_fields()) {
                fields.push_back({f.name, parse_type_name(f.type)});
            }
            struct_fields[s->get_name()] = fields;
            ctx_.env().add_struct(s->get_name(), TypeCache::instance().get_generic(s->get_name()));
        } else if (stmt->get_kind() == ast::Statement::Kind::Enum) {
            auto* e = static_cast<const ast::EnumStmt*>(stmt.get());
            std::vector<std::string> variants;
            for (const auto& v : e->get_variants()) {
                variants.push_back(v.name);
            }
            enum_variants[e->get_name()] = variants;
            ctx_.env().add_enum(e->get_name(), TypeCache::instance().get_generic(e->get_name()));
        } else if (stmt->get_kind() == ast::Statement::Kind::Function) {
            auto* f = static_cast<const ast::FunctionStmt*>(stmt.get());
            TypePtr ret = parse_type_name(f->get_return_type());
            if (f->is_async()) {
                ret = TypeCache::instance().get_future(ret);
            }
            function_sigs[f->get_name()] = ret;
        }
    }

    // Second pass: check all declarations
    for (const auto& stmt : decls) {
        if (!stmt) continue;
        check_stmt(stmt.get());
    }

    pop_scope();
}

TypePtr TypeChecker::check_stmt(const ast::Statement* stmt) {
    if (!stmt) return Type::unit();

    switch (stmt->get_kind()) {
        case ast::Statement::Kind::Function: {
            auto* f = static_cast<const ast::FunctionStmt*>(stmt);
            return check_function(*f);
        }
        case ast::Statement::Kind::Struct: {
            auto* s = static_cast<const ast::StructStmt*>(stmt);
            return check_struct(*s);
        }
        case ast::Statement::Kind::Enum: {
            auto* e = static_cast<const ast::EnumStmt*>(stmt);
            return check_enum(*e);
        }
        case ast::Statement::Kind::Let: {
            auto* let = static_cast<const ast::LetStmt*>(stmt);
            TypePtr init_type = Type::unknown();
            if (let->get_initializer()) {
                init_type = check_expr(let->get_initializer());
            }
            TypePtr declared = parse_type_name(let->get_type());
            if (declared && !declared->is_unknown() && init_type && !init_type->is_unknown()) {
                if (!can_coerce(init_type, declared)) {
                    mismatch_error(declared, init_type, let->get_span());
                }
            }
            define_var(let->get_name(), declared && !declared->is_unknown() ? declared : init_type);
            return Type::unit();
        }
        case ast::Statement::Kind::Assign: {
            auto* assign = static_cast<const ast::AssignStmt*>(stmt);
            TypePtr target = check_expr(assign->get_target());
            TypePtr value = check_expr(assign->get_value());
            if (target && !target->is_unknown() && value && !value->is_unknown()) {
                if (!can_coerce(value, target)) {
                    mismatch_error(target, value, assign->get_span());
                }
            }
            return Type::unit();
        }
        case ast::Statement::Kind::Expression: {
            auto* es = static_cast<const ast::ExprStmt*>(stmt);
            check_expr(es->get_expr());
            return Type::unit();
        }
        case ast::Statement::Kind::Block: {
            auto* block = static_cast<const ast::BlockStmt*>(stmt);
            push_scope();
            for (const auto& s : block->get_statements()) {
                check_stmt(s.get());
            }
            pop_scope();
            return Type::unit();
        }
        case ast::Statement::Kind::If: {
            auto* if_stmt = static_cast<const ast::IfStmt*>(stmt);
            const auto& conds = if_stmt->get_conditions();
            const auto& bodies = if_stmt->get_bodies();
            for (size_t i = 0; i < conds.size(); ++i) {
                if (conds[i]) check_expr(conds[i].get());
                if (i < bodies.size() && bodies[i]) {
                    push_scope();
                    check_stmt(dynamic_cast<const ast::Statement*>(bodies[i].get()));
                    pop_scope();
                }
            }
            if (if_stmt->get_else_body()) {
                push_scope();
                check_stmt(dynamic_cast<const ast::Statement*>(if_stmt->get_else_body()));
                pop_scope();
            }
            return Type::unit();
        }
        case ast::Statement::Kind::Match: {
            auto* m = static_cast<const ast::MatchStmt*>(stmt);
            return check_match(*m);
        }
        case ast::Statement::Kind::For: {
            auto* f = static_cast<const ast::ForStmt*>(stmt);
            return check_for(*f);
        }
        case ast::Statement::Kind::While: {
            auto* w = static_cast<const ast::WhileStmt*>(stmt);
            if (w->get_condition()) check_expr(w->get_condition());
            push_scope();
            check_stmt(dynamic_cast<const ast::Statement*>(w->get_body()));
            pop_scope();
            return Type::unit();
        }
        case ast::Statement::Kind::Loop: {
            auto* l = static_cast<const ast::LoopStmt*>(stmt);
            push_scope();
            check_stmt(dynamic_cast<const ast::Statement*>(l->get_body()));
            pop_scope();
            return Type::unit();
        }
        case ast::Statement::Kind::Return: {
            auto* ret = static_cast<const ast::ReturnStmt*>(stmt);
            if (ret->get_value()) check_expr(ret->get_value());
            return Type::unit();
        }
        case ast::Statement::Kind::Break:
        case ast::Statement::Kind::Continue:
            return Type::unit();
        default:
            return Type::unit();
    }
}

TypePtr TypeChecker::check_expr(const ast::Expression* expr) {
    if (!expr) return Type::unknown();

    switch (expr->get_kind()) {
        case ast::Expression::Kind::Literal: {
            auto* lit = static_cast<const ast::LiteralExpr*>(expr);
            const auto& v = lit->get_value();
            if (std::holds_alternative<int64_t>(v)) return Type::int64();
            if (std::holds_alternative<double>(v)) return Type::float64();
            if (std::holds_alternative<bool>(v)) return Type::boolean();
            if (std::holds_alternative<std::string>(v)) return Type::string();
            return Type::unknown();
        }
        case ast::Expression::Kind::Identifier: {
            auto* ident = static_cast<const ast::IdentifierExpr*>(expr);
            TypePtr t = lookup_var(ident->get_name());
            if (t) return t;
            // Could be a function reference
            auto fit = function_sigs.find(ident->get_name());
            if (fit != function_sigs.end()) return fit->second;
            return Type::unknown();
        }
        case ast::Expression::Kind::Binary: {
            auto* bin = static_cast<const ast::BinaryExpr*>(expr);
            return check_binary_op(*bin);
        }
        case ast::Expression::Kind::Unary: {
            auto* un = static_cast<const ast::UnaryExpr*>(expr);
            return check_unary_op(*un);
        }
        case ast::Expression::Kind::Call: {
            auto* call = static_cast<const ast::CallExpr*>(expr);
            return check_call(*call);
        }
        case ast::Expression::Kind::Index: {
            auto* idx = static_cast<const ast::IndexExpr*>(expr);
            return check_index(*idx);
        }
        case ast::Expression::Kind::Member: {
            auto* member = static_cast<const ast::MemberExpr*>(expr);
            return check_field(*member);
        }
        case ast::Expression::Kind::Lambda: {
            auto* lam = static_cast<const ast::LambdaExpr*>(expr);
            return check_lambda(*lam);
        }
        case ast::Expression::Kind::Array: {
            auto* arr = static_cast<const ast::ArrayExpr*>(expr);
            TypePtr elem = Type::unknown();
            for (const auto& e : arr->get_elements()) {
                TypePtr t = check_expr(e.get());
                if (!elem || elem->is_unknown()) elem = t;
            }
            if (!elem || elem->is_unknown()) elem = Type::int64();
            return TypeCache::instance().get_array(elem, arr->size());
        }
        case ast::Expression::Kind::Await: {
            auto* await = static_cast<const ast::AwaitExpr*>(expr);
            if (!in_async_function) {
                type_error("await can only be used inside async functions", await->get_span());
            }
            TypePtr operand_type = check_expr(await->get_operand());
            if (operand_type && operand_type->is_future()) {
                auto* future = static_cast<FutureType*>(operand_type.get());
                return future->inner_type;
            }
            if (operand_type && !operand_type->is_unknown()) {
                type_error("await requires a Future type, found " + operand_type->to_string(), await->get_span());
            }
            return Type::unknown();
        }
        case ast::Expression::Kind::TryQuestion: {
            auto* tq = static_cast<const ast::TryQuestionExpr*>(expr);
            TypePtr operand_type = check_expr(tq->get_operand());
            if (!operand_type || operand_type->is_unknown()) {
                return Type::unknown();
            }
            // try? expr returns Result<T, Error> where T is the type of expr
            // For now, use generic Error type
            TypePtr error_type = TypeCache::instance().get_generic("Error");
            return TypeCache::instance().get_result(operand_type, error_type);
        }
        default:
            return Type::unknown();
    }
}

TypePtr TypeChecker::check_binary_op(const ast::BinaryExpr& op) {
    TypePtr left = check_expr(op.get_left());
    TypePtr right = check_expr(op.get_right());
    if (!left || !right) return Type::unknown();

    auto oper = op.get_operator();
    bool is_arith = (oper == TokenType::Op_plus || oper == TokenType::Op_minus ||
                     oper == TokenType::Op_star || oper == TokenType::Op_slash ||
                     oper == TokenType::Op_percent);
    bool is_cmp = (oper == TokenType::Op_eq || oper == TokenType::Op_neq ||
                   oper == TokenType::Op_lt || oper == TokenType::Op_lte ||
                   oper == TokenType::Op_gt || oper == TokenType::Op_gte);
    bool is_logical = (oper == TokenType::Op_and || oper == TokenType::Op_or);

    if (is_logical) {
        return Type::boolean();
    }
    if (is_cmp) {
        return Type::boolean();
    }
    if (is_arith) {
        if (left->is_unknown() || right->is_unknown()) {
            return Type::unknown();
        }
        if (left->is_numeric() && right->is_numeric()) {
            return numeric_coerce(left, right);
        }
        type_error("Arithmetic operation requires numeric types", op.get_span());
        return Type::unknown();
    }
    return Type::unknown();
}

TypePtr TypeChecker::check_unary_op(const ast::UnaryExpr& op) {
    TypePtr operand = check_expr(op.get_operand());
    if (!operand) return Type::unknown();
    auto oper = op.get_operator();
    if (oper == TokenType::Op_minus || oper == TokenType::Op_plus) {
        if (operand->is_numeric()) return operand;
        type_error("Unary arithmetic requires numeric type", op.get_span());
        return Type::unknown();
    }
    if (oper == TokenType::Op_bang) {
        return Type::boolean();
    }
    return operand;
}

TypePtr TypeChecker::check_call(const ast::CallExpr& call) {
    auto* callee = call.get_callee();
    if (!callee) return Type::unknown();

    // Check arguments
    for (const auto& arg : call.get_arguments()) {
        check_expr(arg.get());
    }

    if (callee->get_kind() == ast::Expression::Kind::Identifier) {
        auto* ident = static_cast<const ast::IdentifierExpr*>(callee);
        const std::string& name = ident->get_name();

        // Check for struct constructor
        auto sit = struct_fields.find(name);
        if (sit != struct_fields.end()) {
            return ctx_.env().get_struct(name);
        }

        // Check for enum variant constructor
        for (const auto& ep : enum_variants) {
            for (const auto& vname : ep.second) {
                if (vname == name) {
                    return ctx_.env().get_enum(ep.first);
                }
            }
        }

        // Function call
        auto fit = function_sigs.find(name);
        if (fit != function_sigs.end()) {
            return fit->second;
        }
    }
    return Type::unknown();
}

TypePtr TypeChecker::check_index(const ast::IndexExpr& index) {
    TypePtr obj = check_expr(index.get_object());
    check_expr(index.get_index());
    if (obj && obj->is_array()) {
        auto* arr = static_cast<ArrayType*>(obj.get());
        return arr->element_type;
    }
    return Type::unknown();
}

TypePtr TypeChecker::check_field(const ast::MemberExpr& field) {
    TypePtr obj = check_expr(field.get_object());
    if (!obj) return Type::unknown();

    // Check struct fields
    for (const auto& sp : struct_fields) {
        for (const auto& f : sp.second) {
            if (f.first == field.get_member()) {
                return f.second;
            }
        }
    }
    return Type::unknown();
}

TypePtr TypeChecker::check_lambda(const ast::LambdaExpr& lambda) {
    push_scope();
    for (const auto& param : lambda.get_params()) {
        define_var(param.first, parse_type_name(param.second));
    }
    if (lambda.get_body()) {
        auto* stmt = dynamic_cast<const ast::Statement*>(lambda.get_body());
        auto* expr = dynamic_cast<const ast::Expression*>(lambda.get_body());
        if (stmt) {
            check_stmt(stmt);
        } else if (expr) {
            check_expr(expr);
        }
    }
    pop_scope();
    return TypeCache::instance().get_function(Type::unknown(), Type::unknown());
}

// Forward declaration for recursive pattern checking
static void check_pattern(const ast::Pattern& pat, TypePtr scrutinee);

static void check_pattern(const ast::Pattern& pat, TypePtr scrutinee) {
    switch (pat.get_kind()) {
        case ast::Pattern::Kind::Wildcard:
            // Nothing to check
            break;

        case ast::Pattern::Kind::Variable: {
            auto& vp = static_cast<const ast::VariablePattern&>(pat);
            define_var(vp.get_name(), scrutinee);
            break;
        }

        case ast::Pattern::Kind::Literal: {
            auto& lp = static_cast<const ast::LiteralPattern&>(pat);
            // Determine literal type from value
            TypePtr pat_type = Type::unknown();
            std::visit([&](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, int64_t>) pat_type = Type::int64();
                else if constexpr (std::is_same_v<T, double>) pat_type = Type::float64();
                else if constexpr (std::is_same_v<T, std::string>) pat_type = Type::string();
                else if constexpr (std::is_same_v<T, bool>) pat_type = Type::boolean();
            }, lp.get_value());

            if (scrutinee && !scrutinee->is_unknown() && pat_type && !pat_type->is_unknown()) {
                if (!TypeChecker::can_coerce(pat_type, scrutinee)) {
                    // Use a simple report mechanism - for now just skip detailed error
                }
            }
            break;
        }

        case ast::Pattern::Kind::Constructor: {
            auto& cp = static_cast<const ast::ConstructorPattern&>(pat);
            // Look up enum variant
            bool found = false;
            for (const auto& ep : enum_variants) {
                for (const auto& vname : ep.second) {
                    if (vname == cp.get_name()) {
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }
            // For now, we don't deeply check constructor field types
            // because enum variant associated types are not fully stored.
            // Just recursively check sub-patterns with unknown type.
            for (const auto& field : cp.get_fields()) {
                check_pattern(*field, Type::unknown());
            }
            break;
        }

        case ast::Pattern::Kind::Tuple: {
            auto& tp = static_cast<const ast::TuplePattern&>(pat);
            // TODO: check tuple type arity and element types
            for (const auto& elem : tp.get_elements()) {
                check_pattern(*elem, Type::unknown());
            }
            break;
        }

        case ast::Pattern::Kind::Array: {
            auto& ap = static_cast<const ast::ArrayPattern&>(pat);
            for (const auto& elem : ap.get_elements()) {
                check_pattern(*elem, Type::unknown());
            }
            break;
        }

        case ast::Pattern::Kind::Or: {
            auto& op = static_cast<const ast::OrPattern&>(pat);
            check_pattern(*op.get_left(), scrutinee);
            check_pattern(*op.get_right(), scrutinee);
            break;
        }

        case ast::Pattern::Kind::Binding: {
            auto& bp = static_cast<const ast::BindingPattern&>(pat);
            define_var(bp.get_name(), scrutinee);
            check_pattern(*bp.get_sub_pattern(), scrutinee);
            break;
        }

        case ast::Pattern::Kind::Rest:
        case ast::Pattern::Kind::Range:
            // Simplified handling for now
            break;
    }
}

TypePtr TypeChecker::check_match(const ast::MatchStmt& match) {
    TypePtr scrutinee = Type::unknown();
    if (match.get_expr()) {
        scrutinee = check_expr(match.get_expr());
    }

    const auto& patterns = match.get_patterns();
    const auto& bodies = match.get_bodies();

    for (size_t i = 0; i < patterns.size(); ++i) {
        if (!patterns[i]) continue;

        // Check pattern type against scrutinee and bind variables
        check_pattern(*patterns[i], scrutinee);

        // Check body
        if (i < bodies.size() && bodies[i]) {
            push_scope();
            auto* stmt = dynamic_cast<const ast::Statement*>(bodies[i].get());
            auto* expr = dynamic_cast<const ast::Expression*>(bodies[i].get());
            if (stmt) check_stmt(stmt);
            else if (expr) check_expr(expr);
            pop_scope();
        }
    }

    // Exhaustiveness check
    if (!scrutinee->is_unknown()) {
        auto enum_query = [&](const std::string& enum_name) -> std::vector<std::string> {
            auto it = enum_variants.find(enum_name);
            if (it != enum_variants.end()) return it->second;
            return {};
        };
        auto opt_query = [&](TypePtr type) -> bool {
            return type->is_optional();
        };
        PatternChecker checker(enum_query, opt_query);
        auto result = checker.check_exhaustiveness(patterns, scrutinee);
        if (!result.exhaustive) {
            std::string msg = "Non-exhaustive match: missing ";
            for (size_t i = 0; i < result.missing_patterns.size(); ++i) {
                if (i > 0) msg += ", ";
                msg += result.missing_patterns[i];
            }
            type_error(msg, match.get_span());
        }
        for (const auto& redundant : result.redundant_patterns) {
            type_error("Redundant pattern: " + redundant, match.get_span());
        }
    }

    return Type::unit();
}

TypePtr TypeChecker::check_for(const ast::ForStmt& for_stmt) {
    TypePtr iter_type = Type::unknown();
    if (for_stmt.get_iterable()) {
        iter_type = check_expr(for_stmt.get_iterable());
    }

    TypePtr var_type = Type::unknown();
    if (iter_type) {
        if (iter_type->is_array()) {
            auto* arr = static_cast<ArrayType*>(iter_type.get());
            var_type = arr->element_type;
        } else if (iter_type->is_string()) {
            var_type = Type::string();
        } else if (iter_type->is_integer()) {
            var_type = Type::int64();
        }
    }

    push_scope();
    define_var(for_stmt.get_variable(), var_type);
    auto* body = dynamic_cast<const ast::Statement*>(for_stmt.get_body());
    if (body) check_stmt(body);
    pop_scope();
    return Type::unit();
}

TypePtr TypeChecker::check_function(const ast::FunctionStmt& decl) {
    bool prev_async = in_async_function;
    in_async_function = decl.is_async();
    push_scope();
    TypePtr ret = parse_type_name(decl.get_return_type());
    for (const auto& param : decl.get_params()) {
        define_var(param.first, parse_type_name(param.second));
    }
    auto* body = dynamic_cast<const ast::Statement*>(decl.get_body());
    if (body) check_stmt(body);
    pop_scope();
    in_async_function = prev_async;
    return Type::unit();
}

TypePtr TypeChecker::check_struct(const ast::StructStmt& decl) {
    // Validate all field types are known
    for (const auto& field : decl.get_fields()) {
        TypePtr ft = parse_type_name(field.type);
        if (!ft || ft->is_unknown()) {
            type_error("Unknown type for field '" + field.name + "': " + field.type, field.span);
        }
    }
    return Type::unit();
}

TypePtr TypeChecker::check_enum(const ast::EnumStmt& decl) {
    // Validate variant type consistency (simplified: just check types are known)
    for (const auto& variant : decl.get_variants()) {
        for (const auto& ty : variant.associated_types) {
            TypePtr vt = parse_type_name(ty);
            if (!vt || vt->is_unknown()) {
                type_error("Unknown type in enum variant '" + variant.name + "': " + ty, variant.span);
            }
        }
    }
    return Type::unit();
}

TypePtr TypeChecker::check_process(const ast::SerialProcessStmt& process) {
    push_scope();
    for (const auto& param : process.get_params()) {
        define_var(param.first, parse_type_name(param.second));
    }
    auto* body = dynamic_cast<const ast::Statement*>(process.get_body());
    if (body) check_stmt(body);
    pop_scope();
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
    if (!from || !to) return true;
    if (from->equals(to)) return true;
    if (from->is_numeric() && to->is_numeric()) return true;
    if (from->is_string() && to->is_string()) return true;
    if (to->is_unknown() || from->is_unknown()) return true;
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

// FutureType
bool FutureType::equals(const TypePtr& other) const {
    if (!other || !other->is_future()) return false;
    auto* o = static_cast<FutureType*>(other.get());
    return inner_type->equals(o->inner_type);
}

std::string FutureType::to_string() const {
    return "Future<" + inner_type->to_string() + ">";
}

TypePtr FutureType::clone() const {
    return std::make_shared<FutureType>(inner_type->clone());
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
        case TypeKind::FUTURE: return "future";
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
