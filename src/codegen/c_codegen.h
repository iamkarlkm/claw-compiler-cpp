// Claw Compiler - C Code Generator (No LLVM dependency)
// Generates standalone C code from Claw AST

#ifndef CLAW_C_CODEGEN_H
#define CLAW_C_CODEGEN_H

#include <string>
#include <memory>
#include <map>
#include <vector>
#include <sstream>
#include <set>
#include <stack>
#include "ast/ast.h"

namespace claw {
namespace codegen {

// C type mapping from Claw types
struct CTypeMapper {
    static std::string to_c_type(const std::string& claw_type) {
        if (claw_type.empty()) return "int";
        
        std::string t = claw_type;
        
        // Handle array types: u32[10] -> u32[10]
        size_t bracket = t.find('[');
        if (bracket != std::string::npos) {
            std::string base = t.substr(0, bracket);
            std::string suffix = t.substr(bracket);
            return to_c_type(base) + suffix;
        }
        
        // Handle optional types: u32? -> struct { int value; int has_value; }
        if (t.back() == '?') {
            return "claw_optional_" + to_c_type(t.substr(0, t.size() - 1));
        }
        
        // Handle result types: Result<u32, string>
        if (t.find("Result<") == 0) {
            return "claw_result";
        }
        
        // Handle tensor types: tensor<f32, [1024, 1024]>
        if (t.find("tensor<") == 0) {
            return "claw_tensor";
        }
        
        // Primitive type mapping
        if (t == "i8" || t == "u8" || t == "byte") return "int8_t";
        if (t == "i16" || t == "u16") return "int16_t";
        if (t == "i32" || t == "u32") return "int32_t";
        if (t == "i64" || t == "u64") return "int64_t";
        if (t == "isize") return "intptr_t";
        if (t == "usize") return "uintptr_t";
        if (t == "f32") return "float";
        if (t == "f64") return "double";
        if (t == "bool") return "int";
        if (t == "char") return "char";
        if (t == "string") return "char*";
        if (t == "()") return "void";
        
        // Default
        return "int";
    }
    
    static bool is_numeric_type(const std::string& t) {
        return t == "i8" || t == "u8" || t == "i16" || t == "u16" ||
               t == "i32" || t == "u32" || t == "i64" || t == "u64" ||
               t == "f32" || t == "f64";
    }
};

// Scope info for unified lifecycle management
struct ScopeInfo {
    int depth;                          // 0 = function body, 1+ = nested blocks
    std::string label;                  // e.g. "fn_foo", "scope_1", "for_2"
    std::vector<std::string> heap_vars; // variable names that need claw_free
    std::vector<std::string> stack_vars;// variable names (stack-allocated, for documentation)
    std::vector<std::string> var_types; // C type of each var (parallel to heap_vars + stack_vars)
};

// Code generator state
struct CGState {
    std::set<std::string> included_headers;
    std::set<std::string> defined_structs;
    std::set<std::string> defined_functions;
    std::map<std::string, std::string> variable_types;
    std::map<std::string, std::string> variable_names;
    int temp_var_counter = 0;
    int label_counter = 0;
    int scope_counter = 0;              // unique scope id generator
    std::stack<std::string> loop_continue_stack;
    std::stack<std::string> loop_break_stack;
    std::stack<ScopeInfo> scope_stack;  // scope lifecycle tracking
    std::stack<std::string> try_buf_stack;  // nested try: current active jmp_buf name

    bool needs_heap_alloc(const std::string& claw_type) {
        // Arrays (type contains []), strings, tensors → heap
        if (claw_type.find('[') != std::string::npos) return true;
        if (claw_type == "string") return true;
        if (claw_type.find("tensor") != std::string::npos) return true;
        return false;
    }

    std::string make_temp() {
        return "__claw_temp_" + std::to_string(temp_var_counter++);
    }

    std::string make_label() {
        return "__claw_label_" + std::to_string(label_counter++);
    }

    std::string make_scope_label(const std::string& prefix = "scope") {
        return prefix + "_" + std::to_string(scope_counter++);
    }
};

class CCodeGenerator {
private:
    std::ostringstream header_;      // Header declarations
    std::ostringstream code_;        // Function implementations
    std::ostringstream structs_;     // Struct definitions
    CGState state_;
    ast::Program* program_ = nullptr;
    std::string current_function_;
    
public:
    CCodeGenerator() {
        generate_header_includes();
    }
    
    // Main entry point
    bool generate(ast::Program* program) {
        if (!program) return false;
        program_ = program;
        
        generate_runtime_definitions();
        
        // Generate code for each declaration
        for (const auto& decl : program->get_declarations()) {
            if (!generate_declaration(decl.get())) {
                return false;
            }
        }
        
        return true;
    }
    
    std::string get_code() const {
        std::ostringstream result;
        result << header_.str() << "\n";
        result << structs_.str() << "\n";
        result << code_.str();
        return result.str();
    }
    
private:
    void generate_header_includes() {
        header_ << "#include <stdint.h>\n";
        header_ << "#include <stdio.h>\n";
        header_ << "#include <stdlib.h>\n";
        header_ << "#include <string.h>\n";
        header_ << "#include <math.h>\n";
        header_ << "#include <stdbool.h>\n";
        header_ << "#include <setjmp.h>\n\n";
    }
    
    void generate_runtime_definitions() {
        // Runtime header declarations
        header_ << "// Runtime function declarations\n";
        header_ << "void claw_panic(const char* msg);\n";
        header_ << "void* claw_alloc(size_t size);\n";
        header_ << "void claw_free(void* ptr);\n";
        header_ << "void claw_event_dispatch(const char* event_name, void** args, int num_args);\n";
        header_ << "int claw_event_subscribe(const char* event_name, void(*handler)(void**));\n";
        header_ << "\n// Exception handling runtime\n";
        header_ << "typedef void* ClawError;\n";
        header_ << "typedef void* ClawValue;\n";
        header_ << "extern jmp_buf claw_try_buf;\n";
        header_ << "extern ClawError claw_last_error;\n\n";
        
        // Runtime implementations
        code_ << "// ============== RUNTIME IMPLEMENTATIONS ==============\n\n";
        
        code_ << "void claw_panic(const char* msg) {\n";
        code_ << "    fprintf(stderr, \"PANIC: %s\\n\", msg);\n";
        code_ << "    exit(1);\n";
        code_ << "}\n\n";
        
        code_ << "void* claw_alloc(size_t size) {\n";
        code_ << "    void* ptr = malloc(size);\n";
        code_ << "    if (!ptr) claw_panic(\"Out of memory\");\n";
        code_ << "    return ptr;\n";
        code_ << "}\n\n";
        
        code_ << "void claw_free(void* ptr) {\n";
        code_ << "    if (ptr) free(ptr);\n";
        code_ << "}\n\n";
        
        code_ << "void claw_event_dispatch(const char* event_name, void** args, int num_args) {\n";
        code_ << "    printf(\"EVENT: %s\\n\", event_name);\n";
        code_ << "}\n\n";
        
        code_ << "int claw_event_subscribe(const char* event_name, void(*handler)(void**)) {\n";
        code_ << "    printf(\"SUBSCRIBED: %s\\n\", event_name);\n";
        code_ << "    return 0;\n";
        code_ << "}\n\n";
        
        // String utilities
        header_ << "char* claw_string_concat(const char* a, const char* b);\n";
        code_ << "char* claw_string_concat(const char* a, const char* b) {\n";
        code_ << "    size_t len_a = strlen(a);\n";
        code_ << "    size_t len_b = strlen(b);\n";
        code_ << "    char* result = (char*)malloc(len_a + len_b + 1);\n";
        code_ << "    memcpy(result, a, len_a);\n";
        code_ << "    memcpy(result + len_a, b, len_b + 1);\n";
        code_ << "    return result;\n";
        code_ << "}\n\n";
        
        // Printf wrappers
        header_ << "void claw_println_int(int64_t v);\n";
        header_ << "void claw_println_float(double v);\n";
        header_ << "void claw_println_string(const char* s);\n";
        
        code_ << "void claw_println_int(int64_t v) { printf(\"%lld\\n\", (long long)v); }\n";
        code_ << "void claw_println_float(double v) { printf(\"%f\\n\", v); }\n";
        code_ << "void claw_println_string(const char* s) { printf(\"%s\\n\", s); }\n\n";
        
        // Array helpers
        header_ << "int64_t claw_array_get_i32(int32_t* arr, int64_t idx);\n";
        header_ << "void claw_array_set_i32(int32_t* arr, int64_t idx, int64_t val);\n";
        
        code_ << "int64_t claw_array_get_i32(int32_t* arr, int64_t idx) {\n";
        code_ << "    return arr[idx];\n";
        code_ << "}\n\n";
        
        code_ << "void claw_array_set_i32(int32_t* arr, int64_t idx, int64_t val) {\n";
        code_ << "    arr[idx] = (int32_t)val;\n";
        code_ << "}\n\n";
    }
    
    bool generate_declaration(ast::ASTNode* node) {
        if (!node) return false;
        
        auto* stmt = dynamic_cast<ast::Statement*>(node);
        if (!stmt) return true;
        
        switch (stmt->get_kind()) {
            case ast::Statement::Kind::Function:
                return generate_function(static_cast<ast::FunctionStmt*>(stmt));
            case ast::Statement::Kind::SerialProcess:
                return generate_serial_process(static_cast<ast::SerialProcessStmt*>(stmt));
            case ast::Statement::Kind::Let:
            case ast::Statement::Kind::Assign:
            case ast::Statement::Kind::Expression:
                // Top-level statements (rare but possible)
                return generate_statement(stmt);
            default:
                return true;
        }
    }
    
    bool generate_function(ast::FunctionStmt* fn) {
        if (!fn) return false;
        
        current_function_ = fn->get_name();
        state_.variable_types.clear();
        state_.variable_names.clear();
        state_.scope_counter = 0;
        
        std::string ret_type = fn->get_return_type().empty() ? 
            "int" : CTypeMapper::to_c_type(fn->get_return_type());
        
        // Generate function signature
        code_ << "\n// [claw] ========== Function: " << fn->get_name() << " ==========\n";
        code_ << ret_type << " " << fn->get_name() << "(";
        
        const auto& params = fn->get_params();
        for (size_t i = 0; i < params.size(); i++) {
            if (i > 0) code_ << ", ";
            code_ << CTypeMapper::to_c_type(params[i].second) << " " << params[i].first;
            state_.variable_types[params[i].first] = params[i].second;
            state_.variable_names[params[i].first] = params[i].first;
        }
        
        code_ << ") {\n";
        
        // Push function-level scope
        ScopeInfo fn_scope;
        fn_scope.depth = 0;
        fn_scope.label = "fn_" + fn->get_name();
        state_.scope_stack.push(fn_scope);
        
        // Generate function body
        ast::ASTNode* body = fn->get_body();
        if (body) {
            auto* block = dynamic_cast<ast::BlockStmt*>(body);
            if (block) {
                for (const auto& s : block->get_statements()) {
                    generate_statement(s.get());
                }
            } else {
                generate_statement(dynamic_cast<ast::Statement*>(body));
            }
        }
        
        // Emit scope-exit cleanup for function level
        emit_scope_cleanup("fn_" + fn->get_name(), 0);
        
        // Add default return for main
        if (fn->get_name() == "main") {
            code_ << "    return 0;\n";
        }
        
        code_ << "} // [claw] ========== End Function: " << fn->get_name() << " ==========\n\n";
        
        // Pop function-level scope
        if (!state_.scope_stack.empty()) state_.scope_stack.pop();
        
        return true;
    }
    
    bool generate_serial_process(ast::SerialProcessStmt* proc) {
        if (!proc) return false;
        
        code_ << "// Serial Process: " << proc->get_name() << "\n";
        code_ << "void " << proc->get_name() << "(";
        
        const auto& params = proc->get_params();
        for (size_t i = 0; i < params.size(); i++) {
            if (i > 0) code_ << ", ";
            code_ << "void* arg" << i;
        }
        
        code_ << ") {\n";
        
        const auto& body = proc->get_body();
        if (body) {
            auto* block = dynamic_cast<ast::BlockStmt*>(body);
            if (block) {
                for (const auto& s : block->get_statements()) {
                    generate_statement(s.get());
                }
            }
        }
        
        code_ << "}\n\n";
        
        // Generate init function
        code_ << "void " << proc->get_name() << "_init() {\n";
        code_ << "    printf(\"SERIAL_PROCESS_INIT: " << proc->get_name() << "\\n\");\n";
        code_ << "}\n\n";
        
        return true;
    }
    
    bool generate_statement(ast::Statement* stmt) {
        if (!stmt) return true;
        
        switch (stmt->get_kind()) {
            case ast::Statement::Kind::Let:
                return generate_let(static_cast<ast::LetStmt*>(stmt));
            case ast::Statement::Kind::Assign:
                return generate_assign(static_cast<ast::AssignStmt*>(stmt));
            case ast::Statement::Kind::Return:
                return generate_return(static_cast<ast::ReturnStmt*>(stmt));
            case ast::Statement::Kind::If:
                return generate_if(static_cast<ast::IfStmt*>(stmt));
            case ast::Statement::Kind::Block:
                return generate_block(static_cast<ast::BlockStmt*>(stmt));
            case ast::Statement::Kind::For:
                return generate_for(static_cast<ast::ForStmt*>(stmt));
            case ast::Statement::Kind::While:
                return generate_while(static_cast<ast::WhileStmt*>(stmt));
            case ast::Statement::Kind::Break:
                return generate_break(static_cast<ast::BreakStmt*>(stmt));
            case ast::Statement::Kind::Continue:
                return generate_continue(static_cast<ast::ContinueStmt*>(stmt));
            case ast::Statement::Kind::Match:
                return generate_match(static_cast<ast::MatchStmt*>(stmt));
            case ast::Statement::Kind::Publish:
                return generate_publish(static_cast<ast::PublishStmt*>(stmt));
            case ast::Statement::Kind::Subscribe:
                return generate_subscribe(static_cast<ast::SubscribeStmt*>(stmt));
            case ast::Statement::Kind::Expression:
                return generate_expr_stmt(static_cast<ast::ExprStmt*>(stmt));
            case ast::Statement::Kind::Try:
                return generate_try(static_cast<ast::TryStmt*>(stmt));
            case ast::Statement::Kind::Throw:
                return generate_throw(static_cast<ast::ThrowStmt*>(stmt));
            case ast::Statement::Kind::Const:
                return true;
            default:
                return true;
        }
    }
    
    bool generate_let(ast::LetStmt* let) {
        if (!let) return false;
        
        std::string var_name = let->get_name();
        std::string claw_type = let->get_type().empty() ? "int" : let->get_type();
        std::string c_type = CTypeMapper::to_c_type(claw_type);
        
        state_.variable_types[var_name] = claw_type;
        state_.variable_names[var_name] = var_name;
        
        bool is_heap = state_.needs_heap_alloc(claw_type);
        
        if (is_heap) {
            // Heap allocation: array or string
            code_ << "    // [claw] alloc: " << var_name << " (heap, type=" << claw_type << ")\n";
            
            if (claw_type.find('[') != std::string::npos) {
                // Array type: e.g. "u32[10]"
                size_t bracket_pos = claw_type.find('[');
                size_t end_bracket = claw_type.find(']');
                std::string base_type = claw_type.substr(0, bracket_pos);
                std::string size_str = claw_type.substr(bracket_pos + 1, end_bracket - bracket_pos - 1);
                std::string c_base = CTypeMapper::to_c_type(base_type);
                
                code_ << "    " << c_base << "* " << var_name 
                      << " = (" << c_base << "*)claw_alloc(" 
                      << size_str << " * sizeof(" << c_base << "));"
                      << " // [claw] alloc: " << var_name << " [" << size_str << "]\n";
                
                // Zero-initialize
                if (let->get_initializer()) {
                    code_ << "    memset(" << var_name << ", 0, " << size_str << " * sizeof(" << c_base << "));\n";
                    // TODO: handle initializer properly for arrays
                } else {
                    code_ << "    memset(" << var_name << ", 0, " << size_str << " * sizeof(" << c_base << "));\n";
                }
            } else if (claw_type == "string") {
                // String type
                if (let->get_initializer()) {
                    code_ << "    char* " << var_name << " = " 
                          << generate_expression(let->get_initializer()) 
                          << "; // [claw] alloc: " << var_name << " (string)\n";
                } else {
                    code_ << "    char* " << var_name 
                          << " = (char*)claw_alloc(1); " << var_name << "[0] = '\\0';"
                          << " // [claw] alloc: " << var_name << " (string, empty)\n";
                }
            } else {
                // Tensor or other heap type
                code_ << "    void* " << var_name << " = NULL; // [claw] alloc: " << var_name 
                      << " (type=" << claw_type << ", heap)\n";
                if (let->get_initializer()) {
                    code_ << "    " << var_name << " = " << generate_expression(let->get_initializer()) << ";\n";
                }
            }
            
            // Track in current scope for cleanup
            if (!state_.scope_stack.empty()) {
                state_.scope_stack.top().heap_vars.push_back(var_name);
            }
        } else {
            // Stack allocation: scalar types
            code_ << "    // [claw] alloc: " << var_name << " (stack, type=" << claw_type << ")\n";
            code_ << "    " << c_type << " " << var_name;
            
            if (let->get_initializer()) {
                code_ << " = " << generate_expression(let->get_initializer());
            } else {
                code_ << " = 0";
            }
            
            code_ << ";\n";
            
            // Track in current scope for documentation
            if (!state_.scope_stack.empty()) {
                state_.scope_stack.top().stack_vars.push_back(var_name);
            }
        }
        
        return true;
    }
    
    bool generate_assign(ast::AssignStmt* assign) {
        if (!assign) return false;
        
        ast::Expression* target = assign->get_target();
        ast::Expression* value = assign->get_value();
        
        if (!target || !value) return false;
        
        // Handle index assignment: arr[0] = value
        if (target->get_kind() == ast::Expression::Kind::Index) {
            auto* index_expr = static_cast<ast::IndexExpr*>(target);
            std::string obj_name;
            if (index_expr->get_object()->get_kind() == ast::Expression::Kind::Identifier) {
                obj_name = static_cast<ast::IdentifierExpr*>(index_expr->get_object())->get_name();
            }
            
            std::string idx = generate_expression(index_expr->get_index());
            std::string val = generate_expression(value);
            
            // C arrays are 0-indexed, Claw is 1-indexed
            code_ << "    " << obj_name << "[" << idx << " - 1] = " << val << ";\n";
            return true;
        }
        
        // Handle simple assignment: x = value
        if (target->get_kind() == ast::Expression::Kind::Identifier) {
            std::string var_name = static_cast<ast::IdentifierExpr*>(target)->get_name();
            std::string val = generate_expression(value);
            code_ << "    " << var_name << " = " << val << ";\n";
            return true;
        }
        
        return false;
    }
    
    bool generate_return(ast::ReturnStmt* ret) {
        if (!ret) return true;
        
        // Find return value variable name (if it's a simple identifier)
        std::string return_var_name;
        if (ret->get_value() && ret->get_value()->get_kind() == ast::Expression::Kind::Identifier) {
            return_var_name = static_cast<ast::IdentifierExpr*>(ret->get_value())->get_name();
        }
        
        // Emit cleanup for all active scopes (innermost first)
        // This ensures heap variables are freed before return, preventing leaks
        // and avoiding unreachable-code warnings from gcc
        std::vector<ScopeInfo> scopes;
        auto temp_stack = state_.scope_stack;
        while (!temp_stack.empty()) {
            scopes.push_back(temp_stack.top());
            temp_stack.pop();
        }
        
        for (const auto& scope : scopes) {
            if (scope.heap_vars.empty()) continue;
            code_ << "    // [claw] pre-return cleanup: " << scope.label << "\n";
            for (const auto& var_name : scope.heap_vars) {
                if (var_name == return_var_name) {
                    code_ << "    // [claw] skip-free: " << var_name << " (is return value)\n";
                    continue;
                }
                code_ << "    claw_free((void*)" << var_name << ");\n";
            }
        }
        
        if (ret->get_value()) {
            std::string val = generate_expression(ret->get_value());
            code_ << "    return " << val << ";\n";
        } else {
            code_ << "    return 0;\n";
        }
        
        return true;
    }
    
    bool generate_if(ast::IfStmt* if_stmt) {
        if (!if_stmt) return false;
        
        const auto& conds = if_stmt->get_conditions();
        const auto& bodies = if_stmt->get_bodies();
        
        if (conds.empty()) return true;
        
        code_ << "    // [claw] if-branch\n";
        
        // Generate if-else chain
        for (size_t i = 0; i < conds.size(); i++) {
            std::string cond = generate_expression(conds[i].get());
            
            if (i == 0) {
                code_ << "    if (" << cond << ") {\n";
            } else {
                code_ << "    else if (" << cond << ") {\n";
            }
            
            if (i < bodies.size() && bodies[i]) {
                generate_block_body(bodies[i].get());
            }
            
            code_ << "    }\n";
        }
        
        // Handle else
        if (if_stmt->get_else_body()) {
            code_ << "    else {\n";
            generate_block_body(if_stmt->get_else_body());
            code_ << "    }\n";
        }
        
        return true;
    }
    
    bool generate_block(ast::BlockStmt* block) {
        if (!block) return true;
        
        // Push block scope
        std::string scope_label = state_.make_scope_label("block");
        int depth = static_cast<int>(state_.scope_stack.size());
        
        ScopeInfo blk_scope;
        blk_scope.depth = depth;
        blk_scope.label = scope_label;
        state_.scope_stack.push(blk_scope);
        
        std::string indent(depth * 4, ' ');
        code_ << indent << "// [claw] scope-enter: " << scope_label << " (depth=" << depth << ")\n";
        
        for (const auto& s : block->get_statements()) {
            generate_statement(s.get());
        }
        
        // Emit cleanup for this scope's heap vars
        emit_scope_cleanup(scope_label, depth);
        
        code_ << indent << "// [claw] scope-exit: " << scope_label << "\n";
        
        if (!state_.scope_stack.empty()) state_.scope_stack.pop();
        
        return true;
    }
    
    void generate_block_body(ast::ASTNode* body) {
        if (!body) return;
        
        // Push block scope for if/while/for body
        std::string scope_label = state_.make_scope_label("block");
        int depth = static_cast<int>(state_.scope_stack.size());
        
        ScopeInfo blk_scope;
        blk_scope.depth = depth;
        blk_scope.label = scope_label;
        state_.scope_stack.push(blk_scope);
        
        std::string indent(depth * 4, ' ');
        code_ << indent << "// [claw] scope-enter: " << scope_label << " (depth=" << depth << ")\n";
        
        auto* block = dynamic_cast<ast::BlockStmt*>(body);
        if (block) {
            for (const auto& s : block->get_statements()) {
                generate_statement(s.get());
            }
        } else {
            auto* stmt = dynamic_cast<ast::Statement*>(body);
            if (stmt) {
                generate_statement(stmt);
            }
        }
        
        // Emit cleanup
        emit_scope_cleanup(scope_label, depth);
        
        code_ << indent << "// [claw] scope-exit: " << scope_label << "\n";
        
        if (!state_.scope_stack.empty()) state_.scope_stack.pop();
    }
    
    // Emit cleanup code for a scope — free all heap-allocated variables
    void emit_scope_cleanup(const std::string& scope_label, int depth) {
        if (state_.scope_stack.empty()) return;
        
        const auto& scope = state_.scope_stack.top();
        if (scope.heap_vars.empty()) return;
        
        std::string indent((depth + 1) * 4, ' ');
        code_ << indent << "// [claw] scope-free: " << scope_label 
              << " (" << scope.heap_vars.size() << " heap var(s))\n";
        
        for (const auto& var_name : scope.heap_vars) {
            code_ << indent << "claw_free((void*)" << var_name << "); // [claw] free: " << var_name << "\n";
        }
    }
    
    bool generate_for(ast::ForStmt* for_stmt) {
        if (!for_stmt) return false;
        
        std::string var = for_stmt->get_variable();
        ast::Expression* iter_expr = for_stmt->get_iterable();
        std::string iter = generate_expression(iter_expr);
        ast::ASTNode* body = for_stmt->get_body();
        
        // Claw: for i in 10 -> C: for (int i = 0; i < 10; i++)
        // For simplicity, handle range expression
        int64_t count = 10;
        if (iter_expr && iter_expr->get_kind() == ast::Expression::Kind::Literal) {
            auto* lit = static_cast<ast::LiteralExpr*>(iter_expr);
            const auto& val = lit->get_value();
            if (std::holds_alternative<int64_t>(val)) {
                count = std::get<int64_t>(val);
            }
        }
        
        state_.loop_continue_stack.push("");
        state_.loop_break_stack.push("");
        
        // Push for-loop scope
        std::string scope_label = state_.make_scope_label("for");
        int depth = static_cast<int>(state_.scope_stack.size());
        ScopeInfo for_scope;
        for_scope.depth = depth;
        for_scope.label = scope_label;
        for_scope.stack_vars.push_back(var);
        state_.scope_stack.push(for_scope);
        
        code_ << "    // [claw] scope-enter: " << scope_label << " (for-loop, depth=" << depth << ")\n";
        code_ << "    for (int " << var << " = 1; " << var << " <= " << count << "; " << var << "++) {\n";
        
        if (body) {
            generate_block_body(body);
        }
        
        // Emit cleanup for for-loop scope's heap vars
        emit_scope_cleanup(scope_label, depth);
        
        code_ << "    } // [claw] scope-exit: " << scope_label << "\n";
        
        if (!state_.scope_stack.empty()) state_.scope_stack.pop();
        state_.loop_continue_stack.pop();
        state_.loop_break_stack.pop();
        
        return true;
    }
    
    bool generate_while(ast::WhileStmt* while_stmt) {
        if (!while_stmt) return false;
        
        std::string cond = generate_expression(while_stmt->get_condition());
        ast::ASTNode* body = while_stmt->get_body();
        
        std::string continue_label = state_.make_label();
        std::string break_label = state_.make_label();
        
        state_.loop_continue_stack.push(continue_label);
        state_.loop_break_stack.push(break_label);
        
        // Push while-loop scope
        std::string scope_label = state_.make_scope_label("while");
        int depth = static_cast<int>(state_.scope_stack.size());
        ScopeInfo while_scope;
        while_scope.depth = depth;
        while_scope.label = scope_label;
        state_.scope_stack.push(while_scope);
        
        code_ << "    // [claw] scope-enter: " << scope_label << " (while-loop, depth=" << depth << ")\n";
        code_ << "    while (" << cond << ") {\n";
        
        if (body) {
            generate_block_body(body);
        }
        
        // Emit cleanup
        emit_scope_cleanup(scope_label, depth);
        
        code_ << "    } // [claw] scope-exit: " << scope_label << "\n";
        
        if (!state_.scope_stack.empty()) state_.scope_stack.pop();
        state_.loop_continue_stack.pop();
        state_.loop_break_stack.pop();
        
        return true;
    }
    
    // Generate try/catch statement
    // Uses setjmp/longjmp for exception propagation
    bool generate_try(ast::TryStmt* try_stmt) {
        if (!try_stmt) return true;
        
        static int try_counter = 0;
        int try_id = try_counter++;
        std::string buf_name = "claw_try_" + std::to_string(try_id) + "_buf";
        
        // Push this try's jmp_buf onto the stack
        state_.try_buf_stack.push(buf_name);
        
        // Declare jmp_buf and exception holder
        code_ << "    jmp_buf " << buf_name << ";\n";
        code_ << "    ClawError claw_try_" << try_id << "_err = NULL;\n";
        code_ << "    // [claw] try-enter\n";
        
        // try body: setjmp returns 0 on first call, non-zero on longjmp
        code_ << "    if (setjmp(" << buf_name << ") == 0) {\n";
        code_ << "        // [claw] try-body\n";
        
        // Generate try body statements
        if (try_stmt->get_body()) {
            auto kind = try_stmt->get_body()->get_kind();
            if (kind == ast::Statement::Kind::Block) {
                auto* block = static_cast<ast::BlockStmt*>(try_stmt->get_body());
                for (const auto& s : block->get_statements()) {
                    generate_statement(s.get());
                }
            }
        }
        
        code_ << "    } else {\n";
        code_ << "        // [claw] catch-block: error caught via longjmp\n";
        
        // Generate catch clauses
        const auto& catches = try_stmt->get_catches();
        if (!catches.empty()) {
            const auto& clause = catches[0];
            if (clause->is_catch_all()) {
                code_ << "        // [claw] catch-all\n";
            } else {
                code_ << "        // [claw] catch " << clause->get_name() 
                      << ": " << clause->get_type_name() << "\n";
                code_ << "        ClawValue " << clause->get_name() 
                      << " = claw_try_" << try_id << "_err;\n";
            }
            
            if (clause->get_body()) {
                auto kind = clause->get_body()->get_kind();
                if (kind == ast::Statement::Kind::Block) {
                    auto* block = static_cast<ast::BlockStmt*>(clause->get_body());
                    for (const auto& s : block->get_statements()) {
                        generate_statement(s.get());
                    }
                }
            }
        }
        
        code_ << "    }\n";
        code_ << "    // [claw] try-exit\n";
        
        // Pop this try's jmp_buf from the stack
        state_.try_buf_stack.pop();
        
        return true;
    }
    
    // Generate throw statement
    bool generate_throw(ast::ThrowStmt* throw_stmt) {
        if (!throw_stmt) return true;
        
        code_ << "    // [claw] throw\n";
        std::string val = generate_expression(throw_stmt->get_value());
        code_ << "    claw_last_error = (ClawError)" << val << ";\n";
        
        // longjmp to the nearest enclosing try's jmp_buf
        if (!state_.try_buf_stack.empty()) {
            code_ << "    longjmp(" << state_.try_buf_stack.top() << ", 1);\n";
        } else {
            // No enclosing try — terminate
            code_ << "    fprintf(stderr, \"Unhandled exception\\n\");\n";
            code_ << "    exit(1);\n";
        }
        
        return true;
    }

    bool generate_break(ast::BreakStmt* brk) {
        code_ << "    break;\n";
        return true;
    }
    
    bool generate_continue(ast::ContinueStmt* cont) {
        code_ << "    continue;\n";
        return true;
    }
    
    bool generate_match(ast::MatchStmt* match) {
        if (!match) return false;
        
        std::string subject = generate_expression(match->get_expr());
        
        const auto& patterns = match->get_patterns();
        const auto& bodies = match->get_bodies();
        
        if (patterns.empty()) return true;
        
        code_ << "    // Match statement\n";
        
        // Simple implementation: if-else chain
        for (size_t i = 0; i < patterns.size(); i++) {
            std::string pattern = generate_expression(patterns[i].get());
            std::string prefix = (i == 0) ? "if" : "else if";
            
            code_ << "    " << prefix << " (" << subject << " == " << pattern << ") {\n";
            
            if (i < bodies.size() && bodies[i]) {
                generate_block_body(bodies[i].get());
            }
            
            code_ << "    }\n";
        }
        
        // Handle default case (_)
        // (Simplified - would need more complex handling for real implementation)
        
        return true;
    }
    
    bool generate_publish(ast::PublishStmt* publish) {
        if (!publish) return false;
        
        const std::string& event_name = publish->get_event_name();
        
        code_ << "    claw_event_dispatch(\"" << event_name << "\", NULL, 0);\n";
        
        return true;
    }
    
    bool generate_subscribe(ast::SubscribeStmt* subscribe) {
        if (!subscribe) return false;
        
        const std::string& event_name = subscribe->get_event_name();
        ast::FunctionStmt* handler = subscribe->get_handler();
        
        if (handler) {
            code_ << "    claw_event_subscribe(\"" << event_name << "\", " 
                  << handler->get_name() << ");\n";
        }
        
        return true;
    }
    
    bool generate_expr_stmt(ast::ExprStmt* expr_stmt) {
        if (!expr_stmt) return false;
        
        ast::Expression* expr = expr_stmt->get_expr();
        if (!expr) return true;
        
        // Check if this is an assignment expression (x = value)
        if (expr->get_kind() == ast::Expression::Kind::Binary) {
            auto* bin = static_cast<ast::BinaryExpr*>(expr);
            if (bin->get_operator() == claw::TokenType::Op_eq_assign) {
                return generate_binary_assignment_expr(bin);
            }
        }
        
        std::string result = generate_expression(expr);
        
        // Only output if not a function call that returns void
        // For now, just output all expressions as statements
        code_ << "    (void)" << result << ";\n";
        
        return true;
    }
    
    // Handle assignment as expression: x = value
    bool generate_binary_assignment_expr(ast::BinaryExpr* bin) {
        if (!bin) return false;
        
        auto* target = bin->get_left();
        std::string value = generate_expression(bin->get_right());
        
        if (!target) return false;
        
        // Simple identifier assignment: x = value
        if (target->get_kind() == ast::Expression::Kind::Identifier) {
            std::string var_name = static_cast<ast::IdentifierExpr*>(target)->get_name();
            code_ << "    " << var_name << " = " << value << ";\n";
            return true;
        }
        
        // Index assignment: arr[i] = value
        if (target->get_kind() == ast::Expression::Kind::Index) {
            auto* idx_expr = static_cast<ast::IndexExpr*>(target);
            std::string obj = generate_expression(idx_expr->get_object());
            std::string idx = generate_expression(idx_expr->get_index());
            // Claw is 1-based, C is 0-based
            code_ << "    " << obj << "[" << idx << " - 1] = " << value << ";\n";
            return true;
        }
        
        return false;
    }
    
    std::string generate_expression(ast::Expression* expr) {
        if (!expr) return "0";
        
        switch (expr->get_kind()) {
            case ast::Expression::Kind::Literal:
                return generate_literal(static_cast<ast::LiteralExpr*>(expr));
            case ast::Expression::Kind::Identifier:
                return generate_identifier(static_cast<ast::IdentifierExpr*>(expr));
            case ast::Expression::Kind::Binary:
                return generate_binary(static_cast<ast::BinaryExpr*>(expr));
            case ast::Expression::Kind::Unary:
                return generate_unary(static_cast<ast::UnaryExpr*>(expr));
            case ast::Expression::Kind::Call:
                return generate_call(static_cast<ast::CallExpr*>(expr));
            case ast::Expression::Kind::Index:
                return generate_index(static_cast<ast::IndexExpr*>(expr));
            default:
                return "0";
        }
    }
    
    std::string generate_literal(ast::LiteralExpr* lit) {
        if (!lit) return "0";
        
        const auto& value = lit->get_value();
        
        return std::visit([](auto&& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, int64_t>) {
                return std::to_string(v);
            } else if constexpr (std::is_same_v<T, double>) {
                return std::to_string(v);
            } else if constexpr (std::is_same_v<T, bool>) {
                return v ? "1" : "0";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return "\"" + v + "\"";
            } else if constexpr (std::is_same_v<T, char>) {
                std::string s(1, v);
                return "'" + s + "'";
            } else {
                return "0";
            }
        }, value);
    }
    
    std::string generate_identifier(ast::IdentifierExpr* ident) {
        if (!ident) return "0";
        return ident->get_name();
    }
    
    std::string generate_binary(ast::BinaryExpr* binary) {
        if (!binary) return "0";
        
        std::string left = generate_expression(binary->get_left());
        std::string right = generate_expression(binary->get_right());
        
        TokenType op = binary->get_operator();
        
        switch (op) {
            case TokenType::Op_plus:
                return "(" + left + " + " + right + ")";
            case TokenType::Op_minus:
                return "(" + left + " - " + right + ")";
            case TokenType::Op_star:
                return "(" + left + " * " + right + ")";
            case TokenType::Op_slash:
                return "(" + left + " / " + right + ")";
            case TokenType::Op_percent:
                return "(" + left + " % " + right + ")";
            case TokenType::Op_eq:
                return "(" + left + " == " + right + ")";
            case TokenType::Op_neq:
                return "(" + left + " != " + right + ")";
            case TokenType::Op_lt:
                return "(" + left + " < " + right + ")";
            case TokenType::Op_gt:
                return "(" + left + " > " + right + ")";
            case TokenType::Op_lte:
                return "(" + left + " <= " + right + ")";
            case TokenType::Op_gte:
                return "(" + left + " >= " + right + ")";
            case TokenType::Op_and:
                return "(" + left + " && " + right + ")";
            case TokenType::Op_or:
                return "(" + left + " || " + right + ")";
            default:
                return "(" + left + " + " + right + ")";
        }
    }
    
    std::string generate_unary(ast::UnaryExpr* unary) {
        if (!unary) return "0";
        
        std::string operand = generate_expression(unary->get_operand());
        TokenType op = unary->get_operator();
        
        switch (op) {
            case TokenType::Op_minus:
                return "(-" + operand + ")";
            case TokenType::Op_bang:
                return "(! " + operand + ")";
            default:
                return operand;
        }
    }
    
    std::string generate_call(ast::CallExpr* call) {
        if (!call) return "0";
        
        ast::Expression* callee = call->get_callee();
        if (!callee) return "0";
        
        // Handle println specially
        if (callee->get_kind() == ast::Expression::Kind::Identifier) {
            std::string name = static_cast<ast::IdentifierExpr*>(callee)->get_name();
            
            if (name == "println") {
                return generate_println_call(call->get_arguments());
            }
            if (name == "print") {
                return generate_print_call(call->get_arguments());
            }
            if (name == "len") {
                return generate_len_call(call->get_arguments());
            }
        }
        
        // Regular function call
        std::string result = "(";
        result += generate_expression(callee);
        result += ")(";
        
        const auto& args = call->get_arguments();
        for (size_t i = 0; i < args.size(); i++) {
            if (i > 0) result += ", ";
            result += generate_expression(args[i].get());
        }
        
        result += ")";
        return result;
    }
    
    std::string generate_println_call(const std::vector<std::unique_ptr<ast::Expression>>& args) {
        if (args.empty()) {
            return "printf(\"\\n\")";
        }
        
        // For single argument, use appropriate print function
        std::string arg = generate_expression(args[0].get());
        
        // Detect type and use appropriate print
        // For simplicity, always try to print as number first
        if (args[0]->get_kind() == ast::Expression::Kind::Literal) {
            auto* lit = static_cast<ast::LiteralExpr*>(args[0].get());
            const auto& val = lit->get_value();
            if (std::holds_alternative<int64_t>(val)) {
                return "claw_println_int(" + arg + ")";
            } else if (std::holds_alternative<double>(val)) {
                return "claw_println_float(" + arg + ")";
            } else if (std::holds_alternative<std::string>(val)) {
                return "claw_println_string(" + arg + ")";
            }
        }
        
        // Default: print as int (will be optimized later with proper type checking)
        return "claw_println_int(" + arg + ")";
    }
    
    std::string generate_print_call(const std::vector<std::unique_ptr<ast::Expression>>& args) {
        if (args.empty()) return "0";
        
        std::string arg = generate_expression(args[0].get());
        
        if (args[0]->get_kind() == ast::Expression::Kind::Literal) {
            auto* lit = static_cast<ast::LiteralExpr*>(args[0].get());
            const auto& val = lit->get_value();
            if (std::holds_alternative<int64_t>(val) || std::holds_alternative<double>(val)) {
                return "printf(\"%f\", (double)" + arg + ")";
            }
        }
        
        return "printf(\"%s\", (const char*)" + arg + ")";
    }
    
    std::string generate_len_call(const std::vector<std::unique_ptr<ast::Expression>>& args) {
        if (args.empty()) return "0";
        
        // For now, just return 1
        return "1";
    }
    
    std::string generate_index(ast::IndexExpr* index) {
        if (!index) return "0";
        
        std::string obj = generate_expression(index->get_object());
        std::string idx = generate_expression(index->get_index());
        
        // Claw uses 1-based indexing, C uses 0-based
        return obj + "[" + idx + " - 1]";
    }
};

} // namespace codegen
} // namespace claw

#endif // CLAW_C_CODEGEN_H
