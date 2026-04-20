// Claw Compiler - Semantic Analyzer
// Symbol table, scope management, and name resolution

#ifndef CLAW_SEMANTIC_ANALYZER_H
#define CLAW_SEMANTIC_ANALYZER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <stack>
#include <optional>
#include "ast/ast.h"
#include "common/common.h"
#include "type/type_system.h"

namespace claw {
namespace semantic {

// Forward declarations
class SemanticAnalyzer;
class Scope;
class Symbol;

// Symbol kinds
enum class SymbolKind {
    Variable,
    Function,
    Parameter,
    Type,
    Field,
    Module,
    Constant,
    Enum,
    EnumVariant,
    Trait,
    Impl,
};

// Symbol visibility
enum class SymbolVisibility {
    Private,
    Public,
    Protected,
};

// Symbol represents a named entity in the program
struct Symbol {
    std::string name;
    SymbolKind kind;
    claw::type::TypePtr type;
    ast::ASTNode* definition;
    Scope* scope;
    SymbolVisibility visibility;
    bool is_mutable;
    bool is_initialized;
    bool is_captured;
    int depth;
    
    Symbol(const std::string& n, SymbolKind k, claw::type::TypePtr t, ast::ASTNode* def, Scope* s)
        : name(n), kind(k), type(t), definition(def), scope(s),
          visibility(SymbolVisibility::Private), 
          is_mutable(false), is_initialized(false), 
          is_captured(false), depth(0) {}
};

// Scope represents a lexical scoping level
class Scope {
public:
    explicit Scope(Scope* parent = nullptr, const std::string& name = "")
        : parent_(parent), name_(name), depth_(parent ? parent->depth_ + 1 : 0) {}
    
    // Define a symbol in this scope
    bool define(const std::string& name, Symbol symbol);
    
    // Look up a symbol in this scope (not parent)
    Symbol* lookup_local(const std::string& name);
    
    // Look up a symbol in this scope and parent scopes
    Symbol* lookup(const std::string& name);
    
    // Get parent scope
    Scope* parent() { return parent_; }
    const Scope* parent() const { return parent_; }
    
    // Get scope depth
    int depth() const { return depth_; }
    
    // Get scope name
    const std::string& name() const { return name_; }
    
    // Get all symbols defined in this scope
    const std::unordered_map<std::string, Symbol>& symbols() const { return symbols_; }
    
    // Get all child scopes
    const std::vector<Scope*>& children() const { return children_; }
    
    // Mark symbol as initialized
    void mark_initialized(const std::string& name);
    
    // Check if symbol is initialized
    bool is_initialized(const std::string& name) const;
    
    // Create child scope
    Scope* create_child(const std::string& name = "");
    
    // Capture a variable (for closures)
    void capture(const std::string& name);
    
private:
    Scope* parent_;
    std::string name_;
    int depth_;
    std::unordered_map<std::string, Symbol> symbols_;
    std::vector<Scope*> children_;
    std::unordered_set<std::string> initialized_vars_;
    std::unordered_set<std::string> captured_vars_;
};

// Symbol table manages all scopes
class SymbolTable {
public:
    SymbolTable();
    
    // Scope management
    Scope* enter_scope(const std::string& name = "");
    void exit_scope();
    Scope* current_scope() { return scope_stack_.top(); }
    
    // Symbol operations
    bool define(const std::string& name, SymbolKind kind, claw::type::TypePtr type, 
                ast::ASTNode* def, bool is_mutable = false);
    Symbol* lookup(const std::string& name);
    Symbol* lookup_local(const std::string& name);
    
    // Current depth
    int current_depth() const { return current_scope()->depth(); }
    
    // Check if in function scope
    bool in_function() const;
    
    // Get function return type
    claw::type::TypePtr current_return_type() const { return current_return_type_; }
    void set_return_type(claw::type::TypePtr type) { current_return_type_ = type; }
    
    // Get current function name
    const std::string& current_function() const { return current_function_; }
    void set_function(const std::string& name) { current_function_ = name; }
    void clear_function() { current_function_.clear(); }
    
    // Loop depth for break/continue
    int loop_depth() const { return loop_depth_; }
    void enter_loop() { loop_depth_++; }
    void exit_loop() { loop_depth_--; }
    
    // Collect all captured variables
    std::vector<Symbol*> get_captured_variables();
    
    // Get all functions for code generation
    const std::vector<Symbol*>& functions() const { return functions_; }
    void add_function(Symbol* sym) { functions_.push_back(sym); }
    
private:
    std::stack<Scope*> scope_stack_;
    claw::type::TypePtr current_return_type_;
    std::string current_function_;
    int loop_depth_;
    std::vector<Symbol*> functions_;
};

// Diagnostic messages
enum class DiagnosticKind {
    Error,
    Warning,
    Note,
};

struct Diagnostic {
    DiagnosticKind kind;
    std::string message;
    SourceSpan span;
};

// Semantic analysis result
struct AnalysisResult {
    bool success;
    std::vector<Diagnostic> diagnostics;
    std::shared_ptr<SymbolTable> symbol_table;
};

// Main semantic analyzer
class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer();
    AnalysisResult analyze(ast::Program* program);
    
    // Get symbol table
    std::shared_ptr<SymbolTable> get_symbol_table() { return symbol_table_; }
    
private:
    std::shared_ptr<SymbolTable> symbol_table_;
    std::vector<Diagnostic> diagnostics_;
    bool has_errors_;
    
    // Helper methods
    void report_error(const std::string& msg, const SourceSpan& span);
    void report_warning(const std::string& msg, const SourceSpan& span);
    void report_note(const std::string& msg, const SourceSpan& span);
    
    // Visit methods for AST nodes
    void visit_program(ast::Program* program);
    void visit_function(ast::FunctionStmt* func);
    void visit_parameter(ast::ParamDecl* param);
    void visit_statement(ast::Statement* stmt);
    void visit_expression(ast::Expression* expr);
    
    // Specific statement visitors
    void visit_block(ast::BlockStmt* block);
    void visit_if(ast::IfStmt* if_stmt);
    void visit_while(ast::WhileStmt* while_stmt);
    void visit_for(ast::ForStmt* for_stmt);
    void visit_loop(ast::LoopStmt* loop_stmt);
    void visit_return(ast::ReturnStmt* ret);
    void visit_break(ast::BreakStmt* brk);
    void visit_continue(ast::ContinueStmt* cont);
    void visit_let(ast::LetStmt* let);
    void visit_assign(ast::AssignStmt* assign);
    void visit_expr_stmt(ast::ExprStmt* expr_stmt);
    void visit_match(ast::MatchStmt* match);
    void visit_publish(ast::PublishStmt* pub);
    void visit_subscribe(ast::SubscribeStmt* sub);
    
    // Specific expression visitors
    void visit_identifier(ast::IdentifierExpr* ident);
    void visit_literal(ast::LiteralExpr* lit);
    void visit_binary(ast::BinaryExpr* bin);
    void visit_unary(ast::UnaryExpr* un);
    void visit_call(ast::CallExpr* call);
    void visit_index(ast::IndexExpr* idx);
    void visit_member(ast::MemberExpr* member);
    void visit_lambda(ast::LambdaExpr* lambda);
    void visit_array(ast::ArrayExpr* arr);
    void visit_tuple(ast::TupleExpr* tup);
    void visit_ref(ast::RefExpr* ref);
    
    // Type checking helpers
    claw::type::TypePtr infer_expression_type(ast::Expression* expr);
    bool check_assignment(claw::type::TypePtr target, claw::type::TypePtr source, const SourceSpan& span);
    bool check_compatibility(claw::type::TypePtr expected, claw::type::TypePtr actual, const SourceSpan& span);
    
    // Built-in type names
    std::unordered_set<std::string> builtin_types_ = {
        "i8", "i16", "i32", "i64", "i128",
        "u8", "u16", "u32", "u64", "u128",
        "f32", "f64", "bool", "char", "str",
        "unit", "never", "any", "void"
    };
};

} // namespace semantic
} // namespace claw

#endif // CLAW_SEMANTIC_ANALYZER_H
