// Claw Compiler - Semantic Analyzer Implementation
// Symbol table, scope management, and name resolution

#include "semantic/semantic_analyzer.h"
#include <algorithm>

namespace claw {
namespace semantic {

// ============================================================================
// Scope Implementation
// ============================================================================

bool Scope::define(const std::string& name, Symbol symbol) {
    if (symbols_.count(name)) {
        return false;  // Already defined in this scope
    }
    symbols_[name] = symbol;
    return true;
}

Symbol* Scope::lookup_local(const std::string& name) {
    auto it = symbols_.find(name);
    if (it != symbols_.end()) {
        return &it->second;
    }
    return nullptr;
}

Symbol* Scope::lookup(const std::string& name) {
    Symbol* result = lookup_local(name);
    if (result) return result;
    if (parent_) return parent_->lookup(name);
    return nullptr;
}

void Scope::mark_initialized(const std::string& name) {
    initialized_vars_.insert(name);
}

bool Scope::is_initialized(const std::string& name) const {
    if (initialized_vars_.count(name)) return true;
    if (parent_) return parent_->is_initialized(name);
    return false;
}

Scope* Scope::create_child(const std::string& name) {
    Scope* child = new Scope(this, name);
    children_.push_back(child);
    return child;
}

void Scope::capture(const std::string& name) {
    captured_vars_.insert(name);
    if (parent_) parent_->capture(name);
}

// ============================================================================
// SymbolTable Implementation
// ============================================================================

SymbolTable::SymbolTable() : current_return_type_(nullptr), loop_depth_(0) {
    // Create global scope
    scope_stack_.push(new Scope(nullptr, "global"));
}

Scope* SymbolTable::enter_scope(const std::string& name) {
    Scope* child = current_scope()->create_child(name);
    scope_stack_.push(child);
    return child;
}

void SymbolTable::exit_scope() {
    if (!scope_stack_.empty()) {
        scope_stack_.pop();
    }
}

bool SymbolTable::define(const std::string& name, SymbolKind kind, TypePtr type,
                         ast::ASTNode* def, bool is_mutable) {
    Symbol symbol(name, kind, type, def, current_scope());
    symbol.is_mutable = is_mutable;
    symbol.depth = current_depth();
    
    bool success = current_scope()->define(name, symbol);
    
    if (success && kind == SymbolKind::Function) {
        // Track functions for code generation
        Symbol* sym = current_scope()->lookup_local(name);
        if (sym) functions_.push_back(sym);
    }
    
    return success;
}

Symbol* SymbolTable::lookup(const std::string& name) {
    return current_scope()->lookup(name);
}

Symbol* SymbolTable::lookup_local(const std::string& name) {
    return current_scope()->lookup_local(name);
}

bool SymbolTable::in_function() const {
    return !current_function_.empty();
}

std::vector<Symbol*> SymbolTable::get_captured_variables() {
    std::vector<Symbol*> result;
    std::unordered_set<std::string> seen;
    
    // Traverse all scopes and collect captured variables
    std::stack<Scope*> stack;
    stack.push(scope_stack_.top());
    
    while (!stack.empty()) {
        Scope* scope = stack.top();
        stack.pop();
        
        for (auto& [name, symbol] : scope->symbols()) {
            if (symbol.is_captured && !seen.count(name)) {
                seen.insert(name);
                result.push_back(&symbol);
            }
        }
        
        for (Scope* child : scope->children()) {
            stack.push(child);
        }
    }
    
    return result;
}

// ============================================================================
// SemanticAnalyzer Implementation
// ============================================================================

SemanticAnalyzer::SemanticAnalyzer() : has_errors_(false) {
    symbol_table_ = std::make_shared<SymbolTable>();
}

AnalysisResult SemanticAnalyzer::analyze(ast::Program* program) {
    visit_program(program);
    
    AnalysisResult result;
    result.success = !has_errors_;
    result.diagnostics = diagnostics_;
    result.symbol_table = symbol_table_;
    return result;
}

void SemanticAnalyzer::report_error(const std::string& msg, const SourceSpan& span) {
    diagnostics_.push_back({DiagnosticKind::Error, msg, span});
    has_errors_ = true;
}

void SemanticAnalyzer::report_warning(const std::string& msg, const SourceSpan& span) {
    diagnostics_.push_back({DiagnosticKind::Warning, msg, span});
}

void SemanticAnalyzer::report_note(const std::string& msg, const SourceSpan& span) {
    diagnostics_.push_back({DiagnosticKind::Note, msg, span});
}

// ============================================================================
// Program and Function Visitors
// ============================================================================

void SemanticAnalyzer::visit_program(ast::Program* program) {
    // First pass: collect all function declarations
    for (auto& stmt : program->statements) {
        if (auto* func = dynamic_cast<ast::FunctionDecl*>(stmt.get())) {
            // Register function in global scope
            TypePtr func_type = Type::create_function({}, Type::create_unit());
            symbol_table_->define(func->name, SymbolKind::Function, func_type, func, false);
        }
    }
    
    // Second pass: analyze each statement
    for (auto& stmt : program->statements) {
        visit_statement(stmt.get());
    }
}

void SemanticAnalyzer::visit_function(ast::FunctionDecl* func) {
    // Enter function scope
    symbol_table_->enter_scope(func->name);
    symbol_table_->set_function(func->name);
    
    // Determine return type
    TypePtr return_type = Type::create_unit();  // Default unit
    if (func->return_type) {
        // Parse return type - simplified for now
        return_type = Type::create_primitive(func->return_type.value().name);
    }
    symbol_table_->set_return_type(return_type);
    
    // Analyze parameters
    for (auto& param : func->params) {
        visit_parameter(param.get());
    }
    
    // Analyze function body
    if (func->body) {
        visit_block(func->body.get());
    }
    
    symbol_table_->clear_function();
    symbol_table_->exit_scope();
}

void SemanticAnalyzer::visit_parameter(ast::ParamDecl* param) {
    TypePtr param_type = Type::create_primitive("i64");  // Default
    
    if (param->type_annotation) {
        param_type = Type::create_primitive(param->type_annotation.value().name);
    }
    
    symbol_table_->define(param->name, SymbolKind::Parameter, param_type, param, false);
    
    // Mark parameter as initialized
    symbol_table_->current_scope()->mark_initialized(param->name);
}

// ============================================================================
// Statement Visitors
// ============================================================================

void SemanticAnalyzer::visit_statement(ast::Statement* stmt) {
    if (!stmt) return;
    
    if (auto* block = dynamic_cast<ast::BlockStmt*>(stmt)) {
        visit_block(block);
    } else if (auto* if_stmt = dynamic_cast<ast::IfStmt*>(stmt)) {
        visit_if(if_stmt);
    } else if (auto* while_stmt = dynamic_cast<ast::WhileStmt*>(stmt)) {
        visit_while(while_stmt);
    } else if (auto* for_stmt = dynamic_cast<ast::ForStmt*>(stmt)) {
        visit_for(for_stmt);
    } else if (auto* loop_stmt = dynamic_cast<ast::LoopStmt*>(stmt)) {
        visit_loop(loop_stmt);
    } else if (auto* ret = dynamic_cast<ast::ReturnStmt*>(stmt)) {
        visit_return(ret);
    } else if (auto* brk = dynamic_cast<ast::BreakStmt*>(stmt)) {
        visit_break(brk);
    } else if (auto* cont = dynamic_cast<ast::ContinueStmt*>(stmt)) {
        visit_continue(cont);
    } else if (auto* let = dynamic_cast<ast::LetStmt*>(stmt)) {
        visit_let(let);
    } else if (auto* assign = dynamic_cast<ast::AssignStmt*>(stmt)) {
        visit_assign(assign);
    } else if (auto* expr_stmt = dynamic_cast<ast::ExprStmt*>(stmt)) {
        visit_expr_stmt(expr_stmt);
    } else if (auto* match = dynamic_cast<ast::MatchStmt*>(stmt)) {
        visit_match(match);
    } else if (auto* pub = dynamic_cast<ast::PublishStmt*>(stmt)) {
        visit_publish(pub);
    } else if (auto* sub = dynamic_cast<ast::SubscribeStmt*>(stmt)) {
        visit_subscribe(sub);
    }
}

void SemanticAnalyzer::visit_block(ast::BlockStmt* block) {
    symbol_table_->enter_scope("block");
    
    for (auto& stmt : block->statements) {
        visit_statement(stmt.get());
    }
    
    symbol_table_->exit_scope();
}

void SemanticAnalyzer::visit_if(ast::IfStmt* if_stmt) {
    // Analyze condition
    if (if_stmt->condition) {
        visit_expression(if_stmt->condition.get());
    }
    
    // Analyze then branch
    if (if_stmt->then_branch) {
        visit_statement(if_stmt->then_branch.get());
    }
    
    // Analyze else branch
    if (if_stmt->else_branch) {
        visit_statement(if_stmt->else_branch.get());
    }
}

void SemanticAnalyzer::visit_while(ast::WhileStmt* while_stmt) {
    symbol_table_->enter_loop();
    
    // Analyze condition
    if (while_stmt->condition) {
        visit_expression(while_stmt->condition.get());
    }
    
    // Analyze body
    if (while_stmt->body) {
        visit_statement(while_stmt->body.get());
    }
    
    symbol_table_->exit_loop();
}

void SemanticAnalyzer::visit_for(ast::ForStmt* for_stmt) {
    symbol_table_->enter_loop();
    symbol_table_->enter_scope("for");
    
    // Analyze iterator expression
    if (for_stmt->iterable) {
        visit_expression(for_stmt->iterable.get());
    }
    
    // Define loop variable
    TypePtr var_type = Type::create_primitive("i64");
    symbol_table_->define(for_stmt->var_name, SymbolKind::Variable, var_type, 
                          for_stmt, false);
    symbol_table_->current_scope()->mark_initialized(for_stmt->var_name);
    
    // Analyze body
    if (for_stmt->body) {
        visit_statement(for_stmt->body.get());
    }
    
    symbol_table_->exit_scope();
    symbol_table_->exit_loop();
}

void SemanticAnalyzer::visit_loop(ast::LoopStmt* loop_stmt) {
    symbol_table_->enter_loop();
    symbol_table_->enter_scope("loop");
    
    // Analyze body
    if (loop_stmt->body) {
        visit_statement(loop_stmt->body.get());
    }
    
    symbol_table_->exit_scope();
    symbol_table_->exit_loop();
}

void SemanticAnalyzer::visit_return(ast::ReturnStmt* ret) {
    if (!symbol_table_->in_function()) {
        report_error("return statement outside of function", ret->span);
        return;
    }
    
    // Check return value type
    if (ret->value) {
        visit_expression(ret->value.get());
        
        TypePtr expected = symbol_table_->current_return_type();
        TypePtr actual = infer_expression_type(ret->value.get());
        
        if (expected && actual && !check_compatibility(expected, actual, ret->span)) {
            report_error("return type mismatch", ret->span);
        }
    } else {
        // Return unit if no value
        TypePtr expected = symbol_table_->current_return_type();
        if (expected && expected->kind() != TypeKind::Unit) {
            report_error("expected return value", ret->span);
        }
    }
}

void SemanticAnalyzer::visit_break(ast::BreakStmt* brk) {
    if (symbol_table_->loop_depth() == 0) {
        report_error("break statement outside of loop", brk->span);
    }
}

void SemanticAnalyzer::visit_continue(ast::ContinueStmt* cont) {
    if (symbol_table_->loop_depth() == 0) {
        report_error("continue statement outside of loop", cont->span);
    }
}

void SemanticAnalyzer::visit_let(ast::LetStmt* let) {
    // Determine variable type
    TypePtr var_type = Type::create_primitive("i64");  // Default
    
    if (let->type_annotation) {
        var_type = Type::create_primitive(let->type_annotation.value().name);
    }
    
    // Define variable in current scope
    bool success = symbol_table_->define(let->name, SymbolKind::Variable, var_type, let, let->is_mutable);
    
    if (!success) {
        report_error("variable '" + let->name + "' already defined", let->span);
        return;
    }
    
    // Check initialization
    if (let->initializer) {
        visit_expression(let->initializer.get());
        
        TypePtr init_type = infer_expression_type(let->initializer.get());
        if (init_type && !check_compatibility(var_type, init_type, let->span)) {
            report_error("type mismatch in initialization", let->span);
        }
        
        // Mark as initialized
        symbol_table_->current_scope()->mark_initialized(let->name);
    }
}

void SemanticAnalyzer::visit_assign(ast::AssignStmt* assign) {
    // Check if variable exists
    Symbol* sym = symbol_table_->lookup(assign->target);
    if (!sym) {
        report_error("undefined variable: " + assign->target, assign->span);
        return;
    }
    
    if (!sym->is_mutable) {
        report_error("cannot assign to immutable variable: " + assign->target, assign->span);
        return;
    }
    
    // Analyze value
    if (assign->value) {
        visit_expression(assign->value.get());
        
        TypePtr target_type = sym->type;
        TypePtr value_type = infer_expression_type(assign->value.get());
        
        if (target_type && value_type && !check_compatibility(target_type, value_type, assign->span)) {
            report_error("type mismatch in assignment", assign->span);
        }
        
        // Mark as initialized
        symbol_table_->current_scope()->mark_initialized(assign->target);
    }
}

void SemanticAnalyzer::visit_expr_stmt(ast::ExprStmt* expr_stmt) {
    if (expr_stmt->expression) {
        visit_expression(expr_stmt->expression.get());
    }
}

void SemanticAnalyzer::visit_match(ast::MatchStmt* match) {
    // Analyze match expression
    if (match->subject) {
        visit_expression(match->subject.get());
    }
    
    // Analyze each arm
    for (auto& arm : match->arms) {
        if (arm->pattern) {
            visit_expression(arm->pattern.get());
        }
        if (arm->body) {
            visit_statement(arm->body.get());
        }
    }
}

void SemanticAnalyzer::visit_publish(ast::PublishStmt* pub) {
    // Analyze event name
    if (pub->event_name) {
        visit_expression(pub->event_name.get());
    }
    
    // Analyze payload
    if (pub->payload) {
        visit_expression(pub->payload.get());
    }
}

void SemanticAnalyzer::visit_subscribe(ast::SubscribeStmt* sub) {
    // Analyze event name
    if (sub->event_name) {
        visit_expression(sub->event_name.get());
    }
    
    // Enter handler scope
    symbol_table_->enter_scope("subscribe_handler");
    
    // Define event parameter if present
    if (sub->param_name) {
        TypePtr param_type = Type::create_primitive("any");
        symbol_table_->define(sub->param_name.value(), SymbolKind::Parameter, 
                             param_type, sub, false);
        symbol_table_->current_scope()->mark_initialized(sub->param_name.value());
    }
    
    // Analyze handler body
    if (sub->handler) {
        visit_statement(sub->handler.get());
    }
    
    symbol_table_->exit_scope();
}

// ============================================================================
// Expression Visitors
// ============================================================================

void SemanticAnalyzer::visit_expression(ast::Expression* expr) {
    if (!expr) return;
    
    if (auto* ident = dynamic_cast<ast::IdentifierExpr*>(expr)) {
        visit_identifier(ident);
    } else if (auto* lit = dynamic_cast<ast::LiteralExpr*>(expr)) {
        visit_literal(lit);
    } else if (auto* bin = dynamic_cast<ast::BinaryExpr*>(expr)) {
        visit_binary(bin);
    } else if (auto* un = dynamic_cast<ast::UnaryExpr*>(expr)) {
        visit_unary(un);
    } else if (auto* call = dynamic_cast<ast::CallExpr*>(expr)) {
        visit_call(call);
    } else if (auto* idx = dynamic_cast<ast::IndexExpr*>(expr)) {
        visit_index(idx);
    } else if (auto* member = dynamic_cast<ast::MemberExpr*>(expr)) {
        visit_member(member);
    } else if (auto* lambda = dynamic_cast<ast::LambdaExpr*>(expr)) {
        visit_lambda(lambda);
    } else if (auto* arr = dynamic_cast<ast::ArrayExpr*>(expr)) {
        visit_array(arr);
    } else if (auto* tup = dynamic_cast<ast::TupleExpr*>(expr)) {
        visit_tuple(tup);
    } else if (auto* ref = dynamic_cast<ast::RefExpr*>(expr)) {
        visit_ref(ref);
    }
}

void SemanticAnalyzer::visit_identifier(ast::IdentifierExpr* ident) {
    Symbol* sym = symbol_table_->lookup(ident->name);
    if (!sym) {
        report_error("undefined identifier: " + ident->name, ident->span);
        return;
    }
    
    // Check initialization for variables
    if (sym->kind == SymbolKind::Variable && !sym->is_initialized) {
        if (!symbol_table_->current_scope()->is_initialized(ident->name)) {
            report_warning("variable used before initialization: " + ident->name, ident->span);
        }
    }
}

void SemanticAnalyzer::visit_literal(ast::LiteralExpr* lit) {
    // Literals are self-typed, no analysis needed
    // The type is determined by the literal value
    (void)lit;  // Suppress unused warning
}

void SemanticAnalyzer::visit_binary(ast::BinaryExpr* bin) {
    // Analyze operands
    if (bin->left) visit_expression(bin->left.get());
    if (bin->right) visit_expression(bin->right.get());
    
    // Check operator compatibility
    // This is a simplified check - full implementation would check
    // that the operator is valid for the operand types
}

void SemanticAnalyzer::visit_unary(ast::UnaryExpr* un) {
    if (un->operand) {
        visit_expression(un->operand.get());
    }
}

void SemanticAnalyzer::visit_call(ast::CallExpr* call) {
    // Analyze function being called
    if (call->callee) {
        visit_expression(call->callee.get());
    }
    
    // Analyze arguments
    for (auto& arg : call->arguments) {
        visit_expression(arg.get());
    }
    
    // Check that function exists
    if (auto* ident = dynamic_cast<ast::IdentifierExpr*>(call->callee.get())) {
        Symbol* sym = symbol_table_->lookup(ident->name);
        if (!sym || (sym->kind != SymbolKind::Function && sym->kind != SymbolKind::Parameter)) {
            report_warning("calling undefined function: " + ident->name, call->span);
        }
    }
}

void SemanticAnalyzer::visit_index(ast::IndexExpr* idx) {
    // Analyze indexed expression
    if (idx->object) {
        visit_expression(idx->object.get());
    }
    
    // Analyze index expression
    if (idx->index) {
        visit_expression(idx->index.get());
    }
}

void SemanticAnalyzer::visit_member(ast::MemberExpr* member) {
    // Analyze object
    if (member->object) {
        visit_expression(member->object.get());
    }
}

void SemanticAnalyzer::visit_lambda(ast::LambdaExpr* lambda) {
    symbol_table_->enter_scope("lambda");
    
    // Analyze parameters
    for (auto& param : lambda->params) {
        TypePtr param_type = Type::create_primitive("i64");  // Default
        symbol_table_->define(param->name, SymbolKind::Parameter, param_type, param, false);
    }
    
    // Analyze body
    if (lambda->body) {
        visit_statement(lambda->body.get());
    }
    
    symbol_table_->exit_scope();
}

void SemanticAnalyzer::visit_array(ast::ArrayExpr* arr) {
    // Analyze each element
    for (auto& elem : arr->elements) {
        visit_expression(elem.get());
    }
}

void SemanticAnalyzer::visit_tuple(ast::TupleExpr* tup) {
    // Analyze each element
    for (auto& elem : tup->elements) {
        visit_expression(elem.get());
    }
}

void SemanticAnalyzer::visit_ref(ast::RefExpr* ref) {
    // Analyze referenced expression
    if (ref->expression) {
        visit_expression(ref->expression.get());
        
        // Check that the referenced variable is initialized
        if (auto* ident = dynamic_cast<ast::IdentifierExpr*>(ref->expression.get())) {
            Symbol* sym = symbol_table_->lookup(ident->name);
            if (sym && !symbol_table_->current_scope()->is_initialized(ident->name)) {
                report_warning("referencing potentially uninitialized variable", ref->span);
            }
        }
    }
}

// ============================================================================
// Type Checking Helpers
// ============================================================================

TypePtr SemanticAnalyzer::infer_expression_type(ast::Expression* expr) {
    if (!expr) return nullptr;
    
    // Infer type based on expression type
    if (auto* lit = dynamic_cast<ast::LiteralExpr*>(expr)) {
        switch (lit->literal_type) {
            case ast::LiteralType::Integer:
                return Type::create_primitive("i64");
            case ast::LiteralType::Float:
                return Type::create_primitive("f64");
            case ast::LiteralType::String:
                return Type::create_primitive("str");
            case ast::LiteralType::Boolean:
                return Type::create_primitive("bool");
            case ast::LiteralType::Char:
                return Type::create_primitive("char");
            default:
                return Type::create_primitive("any");
        }
    }
    
    if (auto* ident = dynamic_cast<ast::IdentifierExpr*>(expr)) {
        Symbol* sym = symbol_table_->lookup(ident->name);
        if (sym) return sym->type;
        return nullptr;
    }
    
    if (auto* bin = dynamic_cast<ast::BinaryExpr*>(expr)) {
        // Binary operators return numeric types for arithmetic
        return Type::create_primitive("i64");
    }
    
    if (auto* un = dynamic_cast<ast::UnaryExpr*>(expr)) {
        return infer_expression_type(un->operand.get());
    }
    
    if (auto* call = dynamic_cast<ast::CallExpr*>(expr)) {
        if (auto* ident = dynamic_cast<ast::IdentifierExpr*>(call->callee.get())) {
            Symbol* sym = symbol_table_->lookup(ident->name);
            if (sym) return sym->type;
        }
        return Type::create_primitive("any");
    }
    
    if (auto* arr = dynamic_cast<ast::ArrayExpr*>(expr)) {
        if (!arr->elements.empty()) {
            TypePtr elem_type = infer_expression_type(arr->elements[0].get());
            return Type::create_array(elem_type, arr->elements.size());
        }
        return Type::create_array(Type::create_primitive("any"), 0);
    }
    
    if (auto* idx = dynamic_cast<ast::IndexExpr*>(expr)) {
        TypePtr obj_type = infer_expression_type(idx->object.get());
        if (obj_type && obj_type->kind() == TypeKind::Array) {
            // Return element type of array
            return Type::create_primitive("any");
        }
        return nullptr;
    }
    
    return Type::create_primitive("any");
}

bool SemanticAnalyzer::check_assignment(TypePtr target, TypePtr source, const SourceSpan& span) {
    return check_compatibility(target, source, span);
}

bool SemanticAnalyzer::check_compatibility(TypePtr expected, TypePtr actual, const SourceSpan& span) {
    if (!expected || !actual) return true;  // Allow if types unknown
    
    // Same type is compatible
    if (expected->kind() == actual->kind()) return true;
    
    // Unit is compatible with anything (void return)
    if (expected->kind() == TypeKind::Unit) return true;
    if (actual->kind() == TypeKind::Unit) return true;
    
    // Numeric types are loosely compatible
    bool expected_numeric = expected->is_numeric();
    bool actual_numeric = actual->is_numeric();
    if (expected_numeric && actual_numeric) return true;
    
    // Any type accepts anything
    if (expected->kind() == TypeKind::Primitive) {
        auto* prim = static_cast<const PrimitiveType*>(expected.get());
        if (prim->type_name == "any") return true;
    }
    
    return false;
}

} // namespace semantic
} // namespace claw
