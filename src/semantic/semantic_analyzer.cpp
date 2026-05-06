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

bool SymbolTable::define(const std::string& name, SymbolKind kind, claw::type::TypePtr type,
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
        
        for (auto& [sname, symbol] : scope->symbols()) {
            if (symbol.is_captured && !seen.count(sname)) {
                seen.insert(sname);
                result.push_back(const_cast<Symbol*>(&symbol));
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
    for (auto& stmt : program->get_declarations()) {
        if (auto* func = dynamic_cast<ast::FunctionStmt*>(stmt.get())) {
            // Register function in global scope
            claw::type::TypePtr func_type = claw::type::Type::unit();
            symbol_table_->define(func->get_name(), SymbolKind::Function, func_type, func, false);
        }
    }
    
    // Second pass: analyze each statement
    for (auto& stmt : program->get_declarations()) {
        visit_statement(stmt.get());
    }
}

void SemanticAnalyzer::visit_function(ast::FunctionStmt* func) {
    // Enter function scope
    symbol_table_->enter_scope(func->get_name());
    symbol_table_->set_function(func->get_name());
    
    // Determine return type
    claw::type::TypePtr return_type = claw::type::Type::unit();  // Default unit
    const auto& ret_str = func->get_return_type();
    if (!ret_str.empty()) {
        // Simple mapping from string to type
        return_type = claw::type::Type::unknown();
    }
    symbol_table_->set_return_type(return_type);
    
    // Analyze parameters
    for (auto& param : func->get_params()) {
        visit_parameter(param.first, param.second);
    }
    
    // Analyze function body
    if (func->get_body()) {
        if (auto* block = dynamic_cast<ast::BlockStmt*>(func->get_body())) {
            visit_block(block);
        } else {
            visit_statement(static_cast<ast::Statement*>(func->get_body()));
        }
    }
    
    symbol_table_->clear_function();
    symbol_table_->exit_scope();
}

void SemanticAnalyzer::visit_parameter(const std::string& name, const std::string& type_name) {
    claw::type::TypePtr param_type = claw::type::Type::unknown();
    
    if (!type_name.empty()) {
        param_type = claw::type::Type::unknown();
    }
    
    symbol_table_->define(name, SymbolKind::Parameter, param_type, nullptr, false);
    
    // Mark parameter as initialized
    symbol_table_->current_scope()->mark_initialized(name);
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
    
    for (auto& stmt : block->get_statements()) {
        visit_statement(stmt.get());
    }
    
    symbol_table_->exit_scope();
}

void SemanticAnalyzer::visit_if(ast::IfStmt* if_stmt) {
    // Analyze conditions and branches
    const auto& conditions = if_stmt->get_conditions();
    const auto& bodies = if_stmt->get_bodies();
    
    for (size_t i = 0; i < conditions.size() && i < bodies.size(); i++) {
        if (conditions[i]) {
            visit_expression(conditions[i].get());
        }
        if (bodies[i]) {
            visit_statement(static_cast<ast::Statement*>(bodies[i].get()));
        }
    }
    
    // Analyze else branch
    if (if_stmt->get_else_body()) {
        visit_statement(static_cast<ast::Statement*>(if_stmt->get_else_body()));
    }
}

void SemanticAnalyzer::visit_while(ast::WhileStmt* while_stmt) {
    symbol_table_->enter_loop();
    
    // Analyze condition
    if (while_stmt->get_condition()) {
        visit_expression(while_stmt->get_condition());
    }
    
    // Analyze body
    if (while_stmt->get_body()) {
        visit_statement(static_cast<ast::Statement*>(while_stmt->get_body()));
    }
    
    symbol_table_->exit_loop();
}

void SemanticAnalyzer::visit_for(ast::ForStmt* for_stmt) {
    symbol_table_->enter_loop();
    symbol_table_->enter_scope("for");
    
    // Analyze iterator expression
    if (for_stmt->get_iterable()) {
        visit_expression(for_stmt->get_iterable());
    }
    
    // Define loop variable
    claw::type::TypePtr var_type = claw::type::Type::unknown();
    symbol_table_->define(for_stmt->get_variable(), SymbolKind::Variable, var_type, 
                          for_stmt, false);
    symbol_table_->current_scope()->mark_initialized(for_stmt->get_variable());
    
    // Analyze body
    if (for_stmt->get_body()) {
        visit_statement(static_cast<ast::Statement*>(for_stmt->get_body()));
    }
    
    symbol_table_->exit_scope();
    symbol_table_->exit_loop();
}

void SemanticAnalyzer::visit_return(ast::ReturnStmt* ret) {
    if (!symbol_table_->in_function()) {
        report_error("return statement outside of function", ret->get_span());
        return;
    }
    
    // Check return value type
    if (ret->get_value()) {
        visit_expression(ret->get_value());
        
        claw::type::TypePtr expected = symbol_table_->current_return_type();
        claw::type::TypePtr actual = infer_expression_type(ret->get_value());
        
        if (expected && actual && !check_compatibility(expected, actual, ret->get_span())) {
            report_error("return type mismatch", ret->get_span());
        }
    } else {
        // Return unit if no value
        claw::type::TypePtr expected = symbol_table_->current_return_type();
        if (expected && expected->kind != claw::type::TypeKind::UNIT) {
            report_error("expected return value", ret->get_span());
        }
    }
}

void SemanticAnalyzer::visit_break(ast::BreakStmt* brk) {
    if (symbol_table_->loop_depth() == 0) {
        report_error("break statement outside of loop", brk->get_span());
    }
}

void SemanticAnalyzer::visit_continue(ast::ContinueStmt* cont) {
    if (symbol_table_->loop_depth() == 0) {
        report_error("continue statement outside of loop", cont->get_span());
    }
}

void SemanticAnalyzer::visit_let(ast::LetStmt* let) {
    // Determine variable type
    claw::type::TypePtr var_type = claw::type::Type::unknown();

    const auto& type_str = let->get_type();
    if (!type_str.empty()) {
        var_type = claw::type::Type::unknown();
    }

    // Tuple destructuring: let (a, b) = expr
    if (let->is_tuple_destructuring()) {
        for (const auto& name : let->get_tuple_names()) {
            if (name == "_") continue; // skip discard placeholder
            bool success = symbol_table_->define(name, SymbolKind::Variable, var_type, let, true);
            if (!success) {
                report_error("variable '" + name + "' already defined", let->get_span());
                return;
            }
        }

        if (let->get_initializer()) {
            visit_expression(let->get_initializer());
            claw::type::TypePtr init_type = infer_expression_type(let->get_initializer());
            if (init_type && !check_compatibility(var_type, init_type, let->get_span())) {
                report_error("type mismatch in initialization", let->get_span());
            }
            for (const auto& name : let->get_tuple_names()) {
                if (name == "_") continue;
                symbol_table_->current_scope()->mark_initialized(name);
            }
        }
        return;
    }

    // Define variable in current scope (always mutable for let bindings)
    bool success = symbol_table_->define(let->get_name(), SymbolKind::Variable, var_type, let, true);

    if (!success) {
        report_error("variable '" + let->get_name() + "' already defined", let->get_span());
        return;
    }

    // Check initialization
    if (let->get_initializer()) {
        visit_expression(let->get_initializer());

        claw::type::TypePtr init_type = infer_expression_type(let->get_initializer());
        if (init_type && !check_compatibility(var_type, init_type, let->get_span())) {
            report_error("type mismatch in initialization", let->get_span());
        }

        // Mark as initialized
        symbol_table_->current_scope()->mark_initialized(let->get_name());
    }
}

void SemanticAnalyzer::visit_assign(ast::AssignStmt* assign) {
    // Check if variable exists - target is an Expression, extract name if identifier
    std::string target_name;
    if (auto* ident = dynamic_cast<ast::IdentifierExpr*>(assign->get_target())) {
        target_name = ident->get_name();
    }
    
    if (!target_name.empty()) {
        Symbol* sym = symbol_table_->lookup(target_name);
        if (!sym) {
            report_error("undefined variable: " + target_name, assign->get_span());
            return;
        }
        
        if (!sym->is_mutable) {
            report_error("cannot assign to immutable variable: " + target_name, assign->get_span());
            return;
        }
        
        // Mark as initialized
        symbol_table_->current_scope()->mark_initialized(target_name);
    }
    
    // Analyze target and value
    visit_expression(assign->get_target());
    if (assign->get_value()) {
        visit_expression(assign->get_value());
    }
}

void SemanticAnalyzer::visit_expr_stmt(ast::ExprStmt* expr_stmt) {
    if (expr_stmt->get_expr()) {
        visit_expression(expr_stmt->get_expr());
    }
}

void SemanticAnalyzer::visit_match(ast::MatchStmt* match) {
    // Analyze match expression
    if (match->get_expr()) {
        visit_expression(match->get_expr());
    }
    
    // Analyze each arm
    const auto& patterns = match->get_patterns();
    const auto& bodies = match->get_bodies();
    for (size_t i = 0; i < patterns.size() && i < bodies.size(); i++) {
        if (patterns[i]) {
            visit_expression(patterns[i].get());
        }
        if (bodies[i]) {
            visit_statement(static_cast<ast::Statement*>(bodies[i].get()));
        }
    }
}

void SemanticAnalyzer::visit_publish(ast::PublishStmt* pub) {
    // Analyze arguments
    for (auto& arg : pub->get_arguments()) {
        visit_expression(arg.get());
    }
}

void SemanticAnalyzer::visit_subscribe(ast::SubscribeStmt* sub) {
    // Enter handler scope
    symbol_table_->enter_scope("subscribe_handler");
    
    // Analyze handler parameters and body
    if (sub->get_handler()) {
        // Register handler parameters
        for (auto& param : sub->get_handler()->get_params()) {
            visit_parameter(param.first, param.second);
        }
        
        // Analyze handler body
        if (sub->get_handler()->get_body()) {
            if (auto* block = dynamic_cast<ast::BlockStmt*>(sub->get_handler()->get_body())) {
                visit_block(block);
            } else {
                visit_statement(static_cast<ast::Statement*>(sub->get_handler()->get_body()));
            }
        }
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
    (void)bin;
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
    }
}

void SemanticAnalyzer::visit_identifier(ast::IdentifierExpr* ident) {
    Symbol* sym = symbol_table_->lookup(ident->get_name());
    if (!sym) {
        report_error("undefined identifier: " + ident->get_name(), ident->get_span());
        return;
    }
    
    // Check initialization for variables
    if (sym->kind == SymbolKind::Variable && !sym->is_initialized) {
        if (!symbol_table_->current_scope()->is_initialized(ident->get_name())) {
            report_warning("variable used before initialization: " + ident->get_name(), ident->get_span());
        }
    }
}

void SemanticAnalyzer::visit_literal(ast::LiteralExpr* lit) {
    // Literals are self-typed, no analysis needed
    (void)lit;
}

void SemanticAnalyzer::visit_binary(ast::BinaryExpr* bin) {
    // Analyze operands
    if (bin->get_left()) visit_expression(bin->get_left());
    if (bin->get_right()) visit_expression(bin->get_right());
}

void SemanticAnalyzer::visit_unary(ast::UnaryExpr* un) {
    if (un->get_operand()) {
        visit_expression(un->get_operand());
    }
}

void SemanticAnalyzer::visit_call(ast::CallExpr* call) {
    // Analyze function being called
    if (call->get_callee()) {
        visit_expression(call->get_callee());
    }
    
    // Analyze arguments
    for (auto& arg : call->get_arguments()) {
        visit_expression(arg.get());
    }
    
    // Check that function exists
    if (auto* ident = dynamic_cast<ast::IdentifierExpr*>(call->get_callee())) {
        Symbol* sym = symbol_table_->lookup(ident->get_name());
        if (!sym || (sym->kind != SymbolKind::Function && sym->kind != SymbolKind::Parameter)) {
            report_warning("calling undefined function: " + ident->get_name(), call->get_span());
        }
    }
}

void SemanticAnalyzer::visit_index(ast::IndexExpr* idx) {
    if (idx->get_object()) {
        visit_expression(idx->get_object());
    }
    if (idx->get_index()) {
        visit_expression(idx->get_index());
    }
}

void SemanticAnalyzer::visit_member(ast::MemberExpr* member) {
    if (member->get_object()) {
        visit_expression(member->get_object());
    }
}

void SemanticAnalyzer::visit_lambda(ast::LambdaExpr* lambda) {
    symbol_table_->enter_scope("lambda");
    
    // Analyze parameters
    for (auto& param : lambda->get_params()) {
        claw::type::TypePtr param_type = claw::type::Type::unknown();
        symbol_table_->define(param.first, SymbolKind::Parameter, param_type, nullptr, false);
        symbol_table_->current_scope()->mark_initialized(param.first);
    }
    
    // Analyze body
    if (lambda->get_body()) {
        if (auto* block = dynamic_cast<ast::BlockStmt*>(lambda->get_body())) {
            visit_block(block);
        } else {
            visit_statement(static_cast<ast::Statement*>(lambda->get_body()));
        }
    }
    
    symbol_table_->exit_scope();
}

void SemanticAnalyzer::visit_array(ast::ArrayExpr* arr) {
    for (auto& elem : arr->get_elements()) {
        visit_expression(elem.get());
    }
}

void SemanticAnalyzer::visit_tuple(ast::TupleExpr* tup) {
    for (auto& elem : tup->get_elements()) {
        visit_expression(elem.get());
    }
}

// ============================================================================
// Type Checking Helpers
// ============================================================================

claw::type::TypePtr SemanticAnalyzer::infer_expression_type(ast::Expression* expr) {
    if (!expr) return nullptr;
    
    // Infer type based on expression type
    if (auto* lit = dynamic_cast<ast::LiteralExpr*>(expr)) {
        const auto& val = lit->get_value();
        if (std::holds_alternative<int64_t>(val)) {
            return claw::type::Type::int64();
        } else if (std::holds_alternative<double>(val)) {
            return claw::type::Type::float64();
        } else if (std::holds_alternative<std::string>(val)) {
            return claw::type::Type::string();
        } else if (std::holds_alternative<bool>(val)) {
            return claw::type::Type::boolean();
        } else if (std::holds_alternative<char>(val)) {
            return claw::type::Type::unknown();
        }
        return claw::type::Type::unknown();
    }
    
    if (auto* ident = dynamic_cast<ast::IdentifierExpr*>(expr)) {
        Symbol* sym = symbol_table_->lookup(ident->get_name());
        if (sym) return sym->type;
        return nullptr;
    }
    
    if (auto* bin = dynamic_cast<ast::BinaryExpr*>(expr)) {
        // Binary operators return numeric types for arithmetic
        return claw::type::Type::int64();
    }
    
    if (auto* un = dynamic_cast<ast::UnaryExpr*>(expr)) {
        return infer_expression_type(un->get_operand());
    }
    
    if (auto* call = dynamic_cast<ast::CallExpr*>(expr)) {
        if (auto* ident = dynamic_cast<ast::IdentifierExpr*>(call->get_callee())) {
            Symbol* sym = symbol_table_->lookup(ident->get_name());
            if (sym) return sym->type;
        }
        return claw::type::Type::unknown();
    }
    
    if (dynamic_cast<ast::ArrayExpr*>(expr)) {
        return claw::type::Type::unknown();
    }
    
    if (dynamic_cast<ast::IndexExpr*>(expr)) {
        return claw::type::Type::unknown();
    }
    
    return claw::type::Type::unknown();
}

bool SemanticAnalyzer::check_assignment(claw::type::TypePtr target, claw::type::TypePtr source, const SourceSpan& span) {
    return check_compatibility(target, source, span);
}

bool SemanticAnalyzer::check_compatibility(claw::type::TypePtr expected, claw::type::TypePtr actual, const SourceSpan& span) {
    if (!expected || !actual) return true;  // Allow if types unknown
    
    // Same type is compatible
    if (expected->kind == actual->kind) return true;
    
    // Unit is compatible with anything (void return)
    if (expected->kind == claw::type::TypeKind::UNIT) return true;
    if (actual->kind == claw::type::TypeKind::UNIT) return true;
    
    // Numeric types are loosely compatible
    bool expected_numeric = expected->is_numeric();
    bool actual_numeric = actual->is_numeric();
    if (expected_numeric && actual_numeric) return true;
    
    // Any type accepts anything - check by name
    if (expected->name == "any" || actual->name == "any") return true;
    
    (void)span;
    return false;
}

} // namespace semantic
} // namespace claw
