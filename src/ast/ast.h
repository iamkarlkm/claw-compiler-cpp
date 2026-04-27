// Claw Compiler - AST Node Definitions
// Abstract Syntax Tree node types for Claw

#ifndef CLAW_AST_H
#define CLAW_AST_H

#include <memory>
#include <vector>
#include <string>
#include <variant>
#include "common/common.h"
#include "lexer/token.h"

namespace claw {
namespace ast {

// Forward declarations
class ASTNode;
class Expression;
class Statement;
class Type;

// AST node base class
class ASTNode {
public:
    virtual ~ASTNode() = default;
    
    virtual std::string to_string() const = 0;
    
    // Clone AST node - default returns nullptr (not all nodes are cloneable)
    virtual std::unique_ptr<ASTNode> clone() const {
        return nullptr;
    }
    
    // Accept visitor
    template<typename Visitor>
    auto accept(Visitor& visitor) -> decltype(auto) {
        return accept_impl(visitor, *this);
    }
    
    // Get source span
    const SourceSpan& get_span() const { return span_; }
    
protected:
    template<typename Visitor, typename Node>
    auto accept_impl(Visitor& visitor, Node& node) -> decltype(auto) {
        return visitor.visit(node);
    }

    SourceSpan span_;
};

// Expression types
class Expression : public ASTNode {
public:
    enum class Kind {
        Identifier,
        Literal,
        Binary,
        Unary,
        Call,
        Index,
        Slice,
        Cast,
        Member,
        Lambda,
        Tuple,
        Array,
        Range,
        Ref,
        MutRef,
        Borrow,
    };
    
    Expression(Kind kind, const SourceSpan& span) : kind_(kind) { span_ = span; }
    Kind get_kind() const { return kind_; }
    const SourceSpan& get_span() const { return span_; }
    
private:
    Kind kind_;
};

// Literal expression (integers, floats, strings, bools)
class LiteralExpr : public Expression {
public:
    using Value = std::variant<
        std::monostate,
        int64_t,
        double,
        std::string,
        char,
        bool
    >;
    
    LiteralExpr(const Value& value, const SourceSpan& span)
        : Expression(Kind::Literal, span), value_(value) {}
    
    // Constructor for bool
    LiteralExpr(bool val, const SourceSpan& span)
        : Expression(Kind::Literal, span), value_(Value(val)) {}
    
    // Constructor for char
    LiteralExpr(char val, const SourceSpan& span)
        : Expression(Kind::Literal, span), value_(Value(val)) {}
    
    const Value& get_value() const { return value_; }
    
    std::string to_string() const override {
        return std::visit([](auto&& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, int64_t>) return std::to_string(v);
            else if constexpr (std::is_same_v<T, double>) return std::to_string(v);
            else if constexpr (std::is_same_v<T, std::string>) return "\"" + v + "\"";
            else if constexpr (std::is_same_v<T, bool>) return v ? "true" : "false";
            else if constexpr (std::is_same_v<T, char>) return std::string(1, v);
            return "null";
        }, value_);
    }
    
private:
    Value value_;
};

// Identifier expression
class IdentifierExpr : public Expression {
public:
    IdentifierExpr(const std::string& name, const SourceSpan& span)
        : Expression(Kind::Identifier, span), name_(name) {}
    
    const std::string& get_name() const { return name_; }
    
    std::string to_string() const override { return name_; }
    
private:
    std::string name_;
};

// Binary expression (a op b)
class BinaryExpr : public Expression {
public:
    BinaryExpr(TokenType op, std::unique_ptr<Expression> left,
               std::unique_ptr<Expression> right, const SourceSpan& span)
        : Expression(Kind::Binary, span), op_(op), left_(std::move(left)), 
          right_(std::move(right)) {}
    
    TokenType get_operator() const { return op_; }
    Expression* get_left() const { return left_.get(); }
    Expression* get_right() const { return right_.get(); }
    
    std::string to_string() const override {
        return "(" + left_->to_string() + " " + token_type_to_string(op_) + 
               " " + right_->to_string() + ")";
    }
    
private:
    TokenType op_;
    std::unique_ptr<Expression> left_;
    std::unique_ptr<Expression> right_;
};

// Unary expression (op expr)
class UnaryExpr : public Expression {
public:
    UnaryExpr(TokenType op, std::unique_ptr<Expression> operand, 
              const SourceSpan& span)
        : Expression(Kind::Unary, span), op_(op), operand_(std::move(operand)) {}
    
    TokenType get_operator() const { return op_; }
    Expression* get_operand() const { return operand_.get(); }
    
    std::string to_string() const override {
        return "(" + std::string(token_type_to_string(op_)) + 
               operand_->to_string() + ")";
    }
    
private:
    TokenType op_;
    std::unique_ptr<Expression> operand_;
};

// Function call expression
class CallExpr : public Expression {
public:
    CallExpr(std::unique_ptr<Expression> callee, const SourceSpan& span)
        : Expression(Kind::Call, span), callee_(std::move(callee)) {}
    
    void add_argument(std::unique_ptr<Expression> arg) {
        arguments_.push_back(std::move(arg));
    }
    
    Expression* get_callee() const { return callee_.get(); }
    const std::vector<std::unique_ptr<Expression>>& get_arguments() const {
        return arguments_;
    }
    
    std::string to_string() const override {
        std::string result = callee_->to_string() + "(";
        for (size_t i = 0; i < arguments_.size(); i++) {
            result += arguments_[i]->to_string();
            if (i < arguments_.size() - 1) result += ", ";
        }
        result += ")";
        return result;
    }
    
private:
    std::unique_ptr<Expression> callee_;
    std::vector<std::unique_ptr<Expression>> arguments_;
};

// Index expression (expr[idx])
class IndexExpr : public Expression {
public:
    IndexExpr(std::unique_ptr<Expression> object, std::unique_ptr<Expression> index,
              const SourceSpan& span)
        : Expression(Kind::Index, span), object_(std::move(object)), 
          index_(std::move(index)) {}
    
    Expression* get_object() const { return object_.get(); }
    Expression* get_index() const { return index_.get(); }
    
    std::string to_string() const override {
        return object_->to_string() + "[" + index_->to_string() + "]";
    }
    
private:
    std::unique_ptr<Expression> object_;
    std::unique_ptr<Expression> index_;
};

// Slice expression (expr[start..end])
class SliceExpr : public Expression {
public:
    SliceExpr(std::unique_ptr<Expression> object,
              std::unique_ptr<Expression> start,
              std::unique_ptr<Expression> end,
              const SourceSpan& span)
        : Expression(Kind::Slice, span), object_(std::move(object)),
          start_(std::move(start)), end_(std::move(end)) {}
    
    Expression* get_object() const { return object_.get(); }
    Expression* get_start() const { return start_.get(); }
    Expression* get_end() const { return end_.get(); }
    
    std::string to_string() const override {
        return object_->to_string() + "[" + 
               (start_ ? start_->to_string() : "") + ".." + 
               (end_ ? end_->to_string() : "") + "]";
    }
    
private:
    std::unique_ptr<Expression> object_;
    std::unique_ptr<Expression> start_;
    std::unique_ptr<Expression> end_;
};

// Tuple expression: (expr1, expr2, expr3)
class TupleExpr : public Expression {
public:
    TupleExpr(std::vector<std::unique_ptr<Expression>> elements,
              const SourceSpan& span)
        : Expression(Kind::Tuple, span), elements_(std::move(elements)) {}
    
    const auto& get_elements() const { return elements_; }
    size_t size() const { return elements_.size(); }
    Expression* get_element(size_t i) const { 
        return i < elements_.size() ? elements_[i].get() : nullptr; 
    }
    
    std::string to_string() const override {
        std::string s = "(";
        for (size_t i = 0; i < elements_.size(); i++) {
            if (i > 0) s += ", ";
            s += elements_[i]->to_string();
        }
        s += ")";
        return s;
    }
    
    std::unique_ptr<ASTNode> clone() const override {
        // Clone not needed for this task
        return nullptr;
    }
    
private:
    std::vector<std::unique_ptr<Expression>> elements_;
};

// Array literal expression [a, b, c]
class ArrayExpr : public Expression {
public:
    ArrayExpr(std::vector<std::unique_ptr<Expression>> elements,
              const SourceSpan& span)
        : Expression(Kind::Array, span), elements_(std::move(elements)) {}
    
    const auto& get_elements() const { return elements_; }
    size_t size() const { return elements_.size(); }
    Expression* get_element(size_t i) const {
        return i < elements_.size() ? elements_[i].get() : nullptr;
    }
    
    std::string to_string() const override {
        std::string s = "[";
        for (size_t i = 0; i < elements_.size(); i++) {
            if (i > 0) s += ", ";
            s += elements_[i]->to_string();
        }
        s += "]";
        return s;
    }
    
    std::unique_ptr<ASTNode> clone() const override {
        return nullptr;
    }
    
private:
    std::vector<std::unique_ptr<Expression>> elements_;
};

// Member access expression (expr.member)
class MemberExpr : public Expression {
public:
    MemberExpr(std::unique_ptr<Expression> object, const std::string& member,
               const SourceSpan& span)
        : Expression(Kind::Member, span), object_(std::move(object)), 
          member_(member) {}
    
    Expression* get_object() const { return object_.get(); }
    const std::string& get_member() const { return member_; }
    
    std::string to_string() const override {
        return object_->to_string() + "." + member_;
    }
    
private:
    std::unique_ptr<Expression> object_;
    std::string member_;
};

// Lambda expression
class LambdaExpr : public Expression {
public:
    LambdaExpr(const SourceSpan& span) : Expression(Kind::Lambda, span) {}
    
    void set_params(std::vector<std::pair<std::string, std::string>> params) {
        params_ = std::move(params);
    }
    void set_return_type(const std::string& ret) { return_type_ = ret; }
    void set_body(std::unique_ptr<ASTNode> body) { body_ = std::move(body); }
    
    const auto& get_params() const { return params_; }
    const std::string& get_return_type() const { return return_type_; }
    ASTNode* get_body() const { return body_.get(); }
    
    std::string to_string() const override {
        std::string result = "fn (";
        for (size_t i = 0; i < params_.size(); i++) {
            result += params_[i].first + ": " + params_[i].second;
            if (i < params_.size() - 1) result += ", ";
        }
        result += ")";
        if (!return_type_.empty()) result += " -> " + return_type_;
        result += " { ... }";
        return result;
    }
    
private:
    std::vector<std::pair<std::string, std::string>> params_;
    std::string return_type_;
    std::unique_ptr<ASTNode> body_;
};

// Statement types
class Statement : public ASTNode {
public:
    enum class Kind {
        Expression,
        Let,
        Assign,
        If,
        Match,
        For,
        While,
        Loop,
        Return,
        Break,
        Continue,
        Block,
        Function,
        Struct,
        Enum,
        Trait,
        Impl,
        TypeAlias,
        Module,
        Import,
        Export,
        Use,
        Publish,
        Subscribe,
        SerialProcess,
        Const,
        Try,
        Throw,
    };
    
    Statement(Kind kind, const SourceSpan& span) : kind_(kind) { span_ = span; }
    Kind get_kind() const { return kind_; }
    const SourceSpan& get_span() const { return span_; }
    
private:
    Kind kind_;
};

// Expression statement
class ExprStmt : public Statement {
public:
    ExprStmt(std::unique_ptr<Expression> expr)
        : Statement(Kind::Expression, expr->get_span()), expr_(std::move(expr)) {}
    
    Expression* get_expr() const { return expr_.get(); }
    
    std::string to_string() const override {
        return expr_->to_string() + ";";
    }
    
private:
    std::unique_ptr<Expression> expr_;
};

// Let binding statement
class LetStmt : public Statement {
public:
    LetStmt(const std::string& name, const SourceSpan& span)
        : Statement(Kind::Let, span), name_(name) {}
    
    void set_type(const std::string& type) { type_ = type; }
    void set_name(const std::string& name) { name_ = name; }
    void set_initializer(std::unique_ptr<Expression> init) { 
        initializer_ = std::move(init); 
    }
    
    const std::string& get_name() const { return name_; }
    const std::string& get_type() const { return type_; }
    Expression* get_initializer() const { return initializer_.get(); }
    
    std::string to_string() const override {
        std::string result = "let " + name_;
        if (!type_.empty()) result += ": " + type_;
        if (initializer_) result += " = " + initializer_->to_string();
        result += ";";
        return result;
    }
    
private:
    std::string name_;
    std::string type_;
    std::unique_ptr<Expression> initializer_;
};

// Const declaration statement
class ConstStmt : public Statement {
public:
    ConstStmt(const std::string& name, const SourceSpan& span)
        : Statement(Kind::Const, span), name_(name) {}
    
    void set_type(const std::string& type) { type_ = type; }
    void set_name(const std::string& name) { name_ = name; }
    void set_initializer(std::unique_ptr<Expression> init) {
        initializer_ = std::move(init);
    }
    
    const std::string& get_name() const { return name_; }
    const std::string& get_type() const { return type_; }
    Expression* get_initializer() const { return initializer_.get(); }
    
    std::string to_string() const override {
        std::string result = "const " + name_;
        if (!type_.empty()) result += ": " + type_;
        if (initializer_) result += " = " + initializer_->to_string();
        result += ";";
        return result;
    }
    
private:
    std::string name_;
    std::string type_;
    std::unique_ptr<Expression> initializer_;
};

// Assignment statement
class AssignStmt : public Statement {
public:
    AssignStmt(std::unique_ptr<Expression> target, 
               std::unique_ptr<Expression> value,
               const SourceSpan& span)
        : Statement(Kind::Assign, span), target_(std::move(target)), 
          value_(std::move(value)) {}
    
    Expression* get_target() const { return target_.get(); }
    Expression* get_value() const { return value_.get(); }
    
    std::string to_string() const override {
        return target_->to_string() + " = " + value_->to_string() + ";";
    }
    
private:
    std::unique_ptr<Expression> target_;
    std::unique_ptr<Expression> value_;
};

// If statement
class IfStmt : public Statement {
public:
    IfStmt(const SourceSpan& span) : Statement(Kind::If, span) {}
    
    void add_branch(std::unique_ptr<Expression> condition, 
                    std::unique_ptr<ASTNode> body) {
        conditions_.push_back(std::move(condition));
        bodies_.push_back(std::move(body));
    }
    void set_else_body(std::unique_ptr<ASTNode> body) { else_body_ = std::move(body); }
    
    const auto& get_conditions() const { return conditions_; }
    const auto& get_bodies() const { return bodies_; }
    ASTNode* get_else_body() const { return else_body_.get(); }
    
    std::string to_string() const override {
        std::string result = "if " + conditions_[0]->to_string() + " { ... }";
        if (else_body_) result += " else { ... }";
        return result;
    }
    
private:
    std::vector<std::unique_ptr<Expression>> conditions_;
    std::vector<std::unique_ptr<ASTNode>> bodies_;
    std::unique_ptr<ASTNode> else_body_;
};

// Match statement
class MatchStmt : public Statement {
public:
    MatchStmt(std::unique_ptr<Expression> expr, const SourceSpan& span)
        : Statement(Kind::Match, span), expr_(std::move(expr)) {}
    
    void add_case(std::unique_ptr<Expression> pattern, std::unique_ptr<ASTNode> body) {
        patterns_.push_back(std::move(pattern));
        bodies_.push_back(std::move(body));
    }
    
    Expression* get_expr() const { return expr_.get(); }
    const auto& get_patterns() const { return patterns_; }
    const auto& get_bodies() const { return bodies_; }
    
    std::string to_string() const override {
        return "match " + expr_->to_string() + " { ... }";
    }
    
private:
    std::unique_ptr<Expression> expr_;
    std::vector<std::unique_ptr<Expression>> patterns_;
    std::vector<std::unique_ptr<ASTNode>> bodies_;
};

// For loop statement
class ForStmt : public Statement {
public:
    ForStmt(const std::string& variable, std::unique_ptr<Expression> iterable,
            std::unique_ptr<ASTNode> body, const SourceSpan& span)
        : Statement(Kind::For, span), variable_(variable),
          iterable_(std::move(iterable)), body_(std::move(body)) {}
    
    const std::string& get_variable() const { return variable_; }
    Expression* get_iterable() const { return iterable_.get(); }
    ASTNode* get_body() const { return body_.get(); }
    
    std::string to_string() const override {
        return "for " + variable_ + " in " + iterable_->to_string() + " { ... }";
    }
    
private:
    std::string variable_;
    std::unique_ptr<Expression> iterable_;
    std::unique_ptr<ASTNode> body_;
};

// While loop statement
class WhileStmt : public Statement {
public:
    WhileStmt(std::unique_ptr<Expression> condition, std::unique_ptr<ASTNode> body,
              const SourceSpan& span)
        : Statement(Kind::While, span), condition_(std::move(condition)),
          body_(std::move(body)) {}
    
    Expression* get_condition() const { return condition_.get(); }
    ASTNode* get_body() const { return body_.get(); }
    
    std::string to_string() const override {
        return "while " + condition_->to_string() + " { ... }";
    }
    
private:
    std::unique_ptr<Expression> condition_;
    std::unique_ptr<ASTNode> body_;
};

// Return statement
class ReturnStmt : public Statement {
public:
    ReturnStmt(const SourceSpan& span) : Statement(Kind::Return, span) {}
    ReturnStmt(std::unique_ptr<Expression> value, const SourceSpan& span)
        : Statement(Kind::Return, span), value_(std::move(value)) {}
    
    void set_value(std::unique_ptr<Expression> value) { value_ = std::move(value); }
    Expression* get_value() const { return value_.get(); }
    
    std::string to_string() const override {
        if (value_) return "return " + value_->to_string() + ";";
        return "return;";
    }
    
private:
    std::unique_ptr<Expression> value_;
};

// Catch clause for try statement
class CatchClause : public ASTNode {
public:
    CatchClause(const std::string& name, const std::string& type_name, 
                std::unique_ptr<Statement> body, const SourceSpan& span)
        : name_(name), type_name_(type_name), body_(std::move(body)), span_(span) {}
    
    const std::string& get_name() const { return name_; }
    const std::string& get_type_name() const { return type_name_; }
    Statement* get_body() const { return body_.get(); }
    const SourceSpan& get_span() const { return span_; }
    
    std::string to_string() const override {
        if (name_.empty()) return "catch " + body_->to_string();
        return "catch " + name_ + ": " + type_name_ + " " + body_->to_string();
    }
    
    bool is_catch_all() const { return name_.empty(); }
    
private:
    std::string name_;
    std::string type_name_;
    std::unique_ptr<Statement> body_;
    SourceSpan span_;
};

// Try statement with catch clauses
class TryStmt : public Statement {
public:
    TryStmt(const SourceSpan& span) : Statement(Kind::Try, span) {}
    
    void set_body(std::unique_ptr<Statement> body) { body_ = std::move(body); }
    Statement* get_body() const { return body_.get(); }
    
    void add_catch(std::unique_ptr<CatchClause> clause) {
        catches_.push_back(std::move(clause));
    }
    const std::vector<std::unique_ptr<CatchClause>>& get_catches() const { return catches_; }
    
    std::string to_string() const override {
        std::string result = "try " + body_->to_string();
        for (const auto& c : catches_) {
            result += " " + c->to_string();
        }
        return result;
    }
    
private:
    std::unique_ptr<Statement> body_;
    std::vector<std::unique_ptr<CatchClause>> catches_;
};

// Throw statement
class ThrowStmt : public Statement {
public:
    ThrowStmt(std::unique_ptr<Expression> value, const SourceSpan& span)
        : Statement(Kind::Throw, span), value_(std::move(value)) {}
    
    Expression* get_value() const { return value_.get(); }
    
    std::string to_string() const override {
        return "throw " + value_->to_string() + ";";
    }
    
private:
    std::unique_ptr<Expression> value_;
};

// Break statement
class BreakStmt : public Statement {
public:
    BreakStmt(const SourceSpan& span) : Statement(Kind::Break, span) {}

    std::string to_string() const override {
        return "break;";
    }
};

// Continue statement
class ContinueStmt : public Statement {
public:
    ContinueStmt(const SourceSpan& span) : Statement(Kind::Continue, span) {}

    std::string to_string() const override {
        return "continue;";
    }
};

// Block statement
class BlockStmt : public Statement {
public:
    BlockStmt(const SourceSpan& span) : Statement(Kind::Block, span) {}
    
    void add_statement(std::unique_ptr<Statement> stmt) {
        statements_.push_back(std::move(stmt));
    }
    
    const auto& get_statements() const { return statements_; }
    
    std::string to_string() const override {
        std::string result = "{\n";
        for (const auto& s : statements_) {
            result += "  " + s->to_string() + "\n";
        }
        result += "}";
        return result;
    }
    
private:
    std::vector<std::unique_ptr<Statement>> statements_;
};

// Function declaration
class FunctionStmt : public Statement {
public:
    FunctionStmt(const std::string& name, const SourceSpan& span)
        : Statement(Kind::Function, span), name_(name) {}
    
    void set_name(const std::string& name) { name_ = name; }
    
    // Generic type parameters support (e.g., fn foo<T>(x: T) -> T)
    void set_type_params(std::vector<std::string> type_params) {
        type_params_ = std::move(type_params);
    }
    void add_type_param(const std::string& param) {
        type_params_.push_back(param);
    }
    
    void set_params(std::vector<std::pair<std::string, std::string>> params) {
        params_ = std::move(params);
    }
    void set_return_type(const std::string& ret) { return_type_ = ret; }
    void set_body(std::unique_ptr<ASTNode> body) { body_ = std::move(body); }
    void set_is_serial(bool is_serial) { is_serial_ = is_serial; }
    void set_is_async(bool is_async) { is_async_ = is_async; }
    
    const std::string& get_name() const { return name_; }
    const auto& get_type_params() const { return type_params_; }
    const auto& get_params() const { return params_; }
    const std::string& get_return_type() const { return return_type_; }
    ASTNode* get_body() const { return body_.get(); }
    bool is_serial() const { return is_serial_; }
    bool is_async() const { return is_async_; }
    bool has_type_params() const { return !type_params_.empty(); }
    
    std::string to_string() const override {
        std::string result = std::string(is_serial_ ? "serial " : "") + 
                            std::string(is_async_ ? "async " : "") +
                            "fn " + name_;
        // Add type parameters if present
        if (!type_params_.empty()) {
            result += "<";
            for (size_t i = 0; i < type_params_.size(); i++) {
                result += type_params_[i];
                if (i < type_params_.size() - 1) result += ", ";
            }
            result += ">";
        }
        result += "(";
        for (size_t i = 0; i < params_.size(); i++) {
            result += params_[i].first + ": " + params_[i].second;
            if (i < params_.size() - 1) result += ", ";
        }
        result += ")";
        if (!return_type_.empty()) result += " -> " + return_type_;
        result += " { ... }";
        return result;
    }
    
private:
    std::string name_;
    std::vector<std::string> type_params_;  // Generic type parameters (e.g., T, U)
    std::vector<std::pair<std::string, std::string>> params_;
    std::string return_type_;
    std::unique_ptr<ASTNode> body_;
    bool is_serial_ = false;
    bool is_async_ = false;
};

// Publish statement (event system)
class PublishStmt : public Statement {
public:
    PublishStmt(const std::string& event_name, const SourceSpan& span)
        : Statement(Kind::Publish, span), event_name_(event_name) {}
    
    void set_event_name(const std::string& name) { event_name_ = name; }
    void add_argument(std::unique_ptr<Expression> arg) {
        arguments_.push_back(std::move(arg));
    }
    
    const std::string& get_event_name() const { return event_name_; }
    const auto& get_arguments() const { return arguments_; }
    
    std::string to_string() const override {
        std::string result = "publish " + event_name_ + "(";
        for (size_t i = 0; i < arguments_.size(); i++) {
            result += arguments_[i]->to_string();
            if (i < arguments_.size() - 1) result += ", ";
        }
        result += ");";
        return result;
    }
    
private:
    std::string event_name_;
    std::vector<std::unique_ptr<Expression>> arguments_;
};

// Subscribe statement (event system)
class SubscribeStmt : public Statement {
public:
    SubscribeStmt(const std::string& event_name, const SourceSpan& span)
        : Statement(Kind::Subscribe, span), event_name_(event_name) {}
    
    void set_event_name(const std::string& name) { event_name_ = name; }
    void set_handler(std::unique_ptr<FunctionStmt> handler) {
        handler_ = std::move(handler);
    }
    
    const std::string& get_event_name() const { return event_name_; }
    FunctionStmt* get_handler() const { return handler_.get(); }
    
    std::string to_string() const override {
        return "subscribe " + event_name_ + " { ... }";
    }
    
private:
    std::string event_name_;
    std::unique_ptr<FunctionStmt> handler_;
};

// Serial process declaration (event handler)
class SerialProcessStmt : public Statement {
public:
    SerialProcessStmt(const std::string& name, const SourceSpan& span)
        : Statement(Kind::SerialProcess, span), name_(name) {}
    
    void set_name(const std::string& name) { name_ = name; }
    void set_params(std::vector<std::pair<std::string, std::string>> params) {
        params_ = std::move(params);
    }
    void set_body(std::unique_ptr<ASTNode> body) { body_ = std::move(body); }
    
    const std::string& get_name() const { return name_; }
    const auto& get_params() const { return params_; }
    ASTNode* get_body() const { return body_.get(); }
    
    std::string to_string() const override {
        std::string result = "serial process " + name_ + "(";
        for (size_t i = 0; i < params_.size(); i++) {
            result += params_[i].first + ": " + params_[i].second;
            if (i < params_.size() - 1) result += ", ";
        }
        result += ") { ... }";
        return result;
    }
    
private:
    std::string name_;
    std::vector<std::pair<std::string, std::string>> params_;
    std::unique_ptr<ASTNode> body_;
};

// Import statement - "use path::to::symbol [as alias]"
class ImportStmt : public Statement {
public:
    ImportStmt(const SourceSpan& span) 
        : Statement(Kind::Import, span), is_pub_(false), is_reexport_(false) {}
    
    void add_import_path(const std::string& path) { import_paths_.push_back(path); }
    void set_alias(const std::string& alias) { alias_ = alias; }
    void set_pub(bool pub) { is_pub_ = pub; }
    void set_reexport(bool reexport) { is_reexport_ = reexport; }
    
    const auto& get_import_paths() const { return import_paths_; }
    const std::string& get_alias() const { return alias_; }
    bool is_pub() const { return is_pub_; }
    bool is_reexport() const { return is_reexport_; }
    
    std::string to_string() const override {
        std::string result = "use ";
        for (size_t i = 0; i < import_paths_.size(); i++) {
            result += import_paths_[i];
            if (i < import_paths_.size() - 1) result += "::";
        }
        if (!alias_.empty()) {
            result += " as " + alias_;
        }
        result += ";";
        return result;
    }
    
private:
    std::vector<std::string> import_paths_;
    std::string alias_;
    bool is_pub_;
    bool is_reexport_;
};

// Export statement - "export name1, name2, ..."
class ExportStmt : public Statement {
public:
    ExportStmt(const SourceSpan& span) 
        : Statement(Kind::Export, span), is_pub_(true) {}
    
    void add_export_name(const std::string& name) { export_names_.push_back(name); }
    void set_pub(bool pub) { is_pub_ = pub; }
    
    const auto& get_export_names() const { return export_names_; }
    bool is_pub() const { return is_pub_; }
    
    std::string to_string() const override {
        std::string result = "export ";
        for (size_t i = 0; i < export_names_.size(); i++) {
            result += export_names_[i];
            if (i < export_names_.size() - 1) result += ", ";
        }
        result += ";";
        return result;
    }
    
private:
    std::vector<std::string> export_names_;
    bool is_pub_;
};

// Module declaration - "mod name { ... }"
class ModuleStmt : public Statement {
public:
    ModuleStmt(const std::string& name, const SourceSpan& span)
        : Statement(Kind::Module, span), name_(name), is_pub_(false) {}
    
    void set_name(const std::string& name) { name_ = name; }
    void set_body(std::vector<std::unique_ptr<Statement>> body) { body_ = std::move(body); }
    void set_pub(bool pub) { is_pub_ = pub; }
    
    const std::string& get_name() const { return name_; }
    const auto& get_body() const { return body_; }
    bool is_pub() const { return is_pub_; }
    
    std::string to_string() const override {
        std::string result = "mod " + name_ + " { ... }";
        return result;
    }
    
private:
    std::string name_;
    std::vector<std::unique_ptr<Statement>> body_;
    bool is_pub_;
};

// ============================================================
// Struct declaration - "struct Name { field: Type, ... }"
// ============================================================
struct StructField {
    std::string name;
    std::string type;
    bool is_pub = false;
    SourceSpan span;
};

class StructStmt : public Statement {
public:
    StructStmt(const std::string& name, const SourceSpan& span)
        : Statement(Kind::Struct, span), name_(name), is_pub_(false) {}
    
    void set_name(const std::string& name) { name_ = name; }
    void add_field(const StructField& field) { fields_.push_back(field); }
    void set_pub(bool pub) { is_pub_ = pub; }
    void set_type_params(const std::vector<std::string>& params) { type_params_ = params; }
    
    const std::string& get_name() const { return name_; }
    const auto& get_fields() const { return fields_; }
    bool is_pub() const { return is_pub_; }
    const auto& get_type_params() const { return type_params_; }
    
    std::string to_string() const override {
        std::string result = (is_pub_ ? "pub " : "") + std::string("struct ") + name_;
        if (!type_params_.empty()) {
            result += "<" + type_params_[0];
            for (size_t i = 1; i < type_params_.size(); i++) result += ", " + type_params_[i];
            result += ">";
        }
        result += " { ";
        for (size_t i = 0; i < fields_.size(); i++) {
            result += fields_[i].name + ": " + fields_[i].type;
            if (i < fields_.size() - 1) result += ", ";
        }
        result += " }";
        return result;
    }
    
private:
    std::string name_;
    std::vector<StructField> fields_;
    bool is_pub_;
    std::vector<std::string> type_params_;
};

// ============================================================
// Enum declaration - "enum Name { Variant1, Variant2(Type), ... }"
// ============================================================
struct EnumVariant {
    std::string name;
    std::vector<std::string> associated_types;  // empty = unit variant
    SourceSpan span;
};

class EnumStmt : public Statement {
public:
    EnumStmt(const std::string& name, const SourceSpan& span)
        : Statement(Kind::Enum, span), name_(name), is_pub_(false) {}
    
    void set_name(const std::string& name) { name_ = name; }
    void add_variant(const EnumVariant& variant) { variants_.push_back(variant); }
    void set_pub(bool pub) { is_pub_ = pub; }
    void set_type_params(const std::vector<std::string>& params) { type_params_ = params; }
    
    const std::string& get_name() const { return name_; }
    const auto& get_variants() const { return variants_; }
    bool is_pub() const { return is_pub_; }
    const auto& get_type_params() const { return type_params_; }
    
    std::string to_string() const override {
        std::string result = (is_pub_ ? "pub " : "") + std::string("enum ") + name_;
        if (!type_params_.empty()) {
            result += "<" + type_params_[0];
            for (size_t i = 1; i < type_params_.size(); i++) result += ", " + type_params_[i];
            result += ">";
        }
        result += " { ";
        for (size_t i = 0; i < variants_.size(); i++) {
            result += variants_[i].name;
            if (!variants_[i].associated_types.empty()) {
                result += "(" + variants_[i].associated_types[0];
                for (size_t j = 1; j < variants_[i].associated_types.size(); j++)
                    result += ", " + variants_[i].associated_types[j];
                result += ")";
            }
            if (i < variants_.size() - 1) result += ", ";
        }
        result += " }";
        return result;
    }
    
private:
    std::string name_;
    std::vector<EnumVariant> variants_;
    bool is_pub_;
    std::vector<std::string> type_params_;
};

// ============================================================
// Trait declaration - "trait Name { fn method(&self) -> RetType; ... }"
// ============================================================
struct TraitMethod {
    std::string name;
    std::vector<std::pair<std::string, std::string>> params;
    std::string return_type;
    SourceSpan span;
    bool has_default_impl = false;
    std::unique_ptr<BlockStmt> default_body;
};

class TraitStmt : public Statement {
public:
    TraitStmt(const std::string& name, const SourceSpan& span)
        : Statement(Kind::Trait, span), name_(name), is_pub_(false) {}
    
    void set_name(const std::string& name) { name_ = name; }
    void add_method(const TraitMethod& method) { methods_.push_back(method); }
    void set_pub(bool pub) { is_pub_ = pub; }
    void set_type_params(const std::vector<std::string>& params) { type_params_ = params; }
    
    const std::string& get_name() const { return name_; }
    const auto& get_methods() const { return methods_; }
    bool is_pub() const { return is_pub_; }
    const auto& get_type_params() const { return type_params_; }
    
    std::string to_string() const override {
        std::string result = (is_pub_ ? "pub " : "") + std::string("trait ") + name_;
        if (!type_params_.empty()) {
            result += "<" + type_params_[0];
            for (size_t i = 1; i < type_params_.size(); i++) result += ", " + type_params_[i];
            result += ">";
        }
        result += " { ";
        for (size_t i = 0; i < methods_.size(); i++) {
            result += "fn " + methods_[i].name + "(";
            for (size_t j = 0; j < methods_[i].params.size(); j++) {
                result += methods_[i].params[j].first + ": " + methods_[i].params[j].second;
                if (j < methods_[i].params.size() - 1) result += ", ";
            }
            result += ") -> " + methods_[i].return_type + ";";
            if (i < methods_.size() - 1) result += " ";
        }
        result += " }";
        return result;
    }
    
private:
    std::string name_;
    std::vector<TraitMethod> methods_;
    bool is_pub_;
    std::vector<std::string> type_params_;
};

// ============================================================
// Impl block - "impl TypeName { fn method(&self) -> RetType { ... } ... }"
// Also: "impl TraitName for TypeName { ... }"
// ============================================================
struct ImplMethod {
    std::string name;
    std::vector<std::pair<std::string, std::string>> params;
    std::string return_type;
    std::unique_ptr<BlockStmt> body;
    SourceSpan span;
    bool is_pub = false;
};

class ImplStmt : public Statement {
public:
    ImplStmt(const std::string& target_type, const SourceSpan& span)
        : Statement(Kind::Impl, span), target_type_(target_type), trait_name_(""), is_trait_impl_(false) {}
    
    void set_target_type(const std::string& type) { target_type_ = type; }
    void set_trait_name(const std::string& name) { trait_name_ = name; is_trait_impl_ = true; }
    void add_method(const ImplMethod& method) { methods_.push_back(method); }
    
    const std::string& get_target_type() const { return target_type_; }
    const std::string& get_trait_name() const { return trait_name_; }
    bool is_trait_impl() const { return is_trait_impl_; }
    const auto& get_methods() const { return methods_; }
    
    std::string to_string() const override {
        std::string result = "impl ";
        if (is_trait_impl_) {
            result += trait_name_ + " for " + target_type_;
        } else {
            result += target_type_;
        }
        result += " { ";
        for (size_t i = 0; i < methods_.size(); i++) {
            result += (methods_[i].is_pub ? "pub " : "") + "fn " + methods_[i].name + "(";
            for (size_t j = 0; j < methods_[i].params.size(); j++) {
                result += methods_[i].params[j].first + ": " + methods_[i].params[j].second;
                if (j < methods_[i].params.size() - 1) result += ", ";
            }
            result += ") -> " + methods_[i].return_type + " { ... }";
            if (i < methods_.size() - 1) result += " ";
        }
        result += " }";
        return result;
    }
    
private:
    std::string target_type_;
    std::string trait_name_;
    bool is_trait_impl_;
    std::vector<ImplMethod> methods_;
};

// Program AST (root node)
class Program : public ASTNode {
public:
    Program() : ASTNode() {}
    
    void add_declaration(std::unique_ptr<Statement> decl) {
        declarations_.push_back(std::move(decl));
    }
    
    const auto& get_declarations() const { return declarations_; }
    
    std::string to_string() const override {
        std::string result;
        for (const auto& d : declarations_) {
            result += d->to_string() + "\n";
        }
        return result;
    }
    
private:
    std::vector<std::unique_ptr<Statement>> declarations_;
};

} // namespace ast
} // namespace claw

#endif // CLAW_AST_H
