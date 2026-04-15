// Claw Compiler - Text-based LLVM IR Generator (No LLVM dependency)
// Generates .ll files without linking LLVM library

#ifndef CLAW_SIMPLE_CODEGEN_H
#define CLAW_SIMPLE_CODEGEN_H

#include <string>
#include <memory>
#include <map>
#include <vector>
#include <sstream>
#include "ast/ast.h"

namespace claw {
namespace codegen {

class SimpleCodeGenerator {
private:
    std::ostringstream ir_;
    int label_counter_ = 0;
    int string_counter_ = 0;
    std::map<std::string, std::string> variables_; // name -> LLVM register
    std::map<std::string, std::string> string_constants_;
    
public:
    SimpleCodeGenerator() = default;
    
    // Generate LLVM IR from AST
    bool generate(ast::Program* program) {
        ir_ << "; ModuleID = 'claw_module'\n";
        ir_ << "source_filename = \"claw_module\"\n\n";
        
        // String constants
        ir_ << "; String constants\n";
        
        // Declare printf
        ir_ << "declare i32 @printf(i8* nocapture, ...) #0\n";
        ir_ << "declare i32 @puts(i8* nocapture) #0\n\n";
        
        ir_ << "attributes #0 = { \"noinline\" \"nounwind\" }\n\n";
        
        // Generate functions
        for (const auto& decl : program->get_declarations()) {
            if (decl->get_kind() == ast::Statement::Kind::Function) {
                auto* fn = static_cast<ast::FunctionStmt*>(decl.get());
                generate_function(fn);
            } else if (decl->get_kind() == ast::Statement::Kind::SerialProcess) {
                auto* proc = static_cast<ast::SerialProcessStmt*>(decl.get());
                // Skip serial processes for now - they're event handlers
            }
        }

        return true;
    }
    
    std::string get_ir() const {
        return ir_.str();
    }
    
private:
    void generate_function(ast::FunctionStmt* fn) {
        std::string fn_name = fn->get_name();
        
        // Generate function signature
        ir_ << "define i32 @" << fn_name << "() #0 {\n";
        ir_ << "entry:\n";
        
        variables_.clear();
        
        // Generate function body
        if (fn->get_body()) {
            generate_block(fn->get_body());
        }
        
        ir_ << "  ret i32 0\n";
        ir_ << "}\n\n";
    }
    
    void generate_block(ast::ASTNode* node) {
        if (!node) return;
        
        // Try to cast to Statement and check if it's a Block
        auto* stmt = dynamic_cast<ast::Statement*>(node);
        if (stmt && stmt->get_kind() == ast::Statement::Kind::Block) {
            auto* block = static_cast<ast::BlockStmt*>(node);
            for (const auto& s : block->get_statements()) {
                generate_statement(s.get());
            }
        }
    }
    
    void generate_statement(ast::Statement* stmt) {
        if (!stmt) return;
        
        switch (stmt->get_kind()) {
            case ast::Statement::Kind::Let:
                generate_let(static_cast<ast::LetStmt*>(stmt));
                break;
                
            case ast::Statement::Kind::Assign:
                generate_assign(static_cast<ast::AssignStmt*>(stmt));
                break;
                
            case ast::Statement::Kind::Return:
                generate_return(static_cast<ast::ReturnStmt*>(stmt));
                break;
                
            case ast::Statement::Kind::If:
                generate_if(static_cast<ast::IfStmt*>(stmt));
                break;
                
            case ast::Statement::Kind::For:
                generate_for(static_cast<ast::ForStmt*>(stmt));
                break;
                
            case ast::Statement::Kind::While:
                generate_while(static_cast<ast::WhileStmt*>(stmt));
                break;
                
            case ast::Statement::Kind::Block:
                generate_block(stmt);
                break;
                
            case ast::Statement::Kind::Expression: {
                auto* expr_stmt = static_cast<ast::ExprStmt*>(stmt);
                generate_expression(expr_stmt->get_expr());
                break;
            }
            
            default:
                ir_ << "; Unsupported statement: " << static_cast<int>(stmt->get_kind()) << "\n";
                break;
        }
    }
    
    void generate_let(ast::LetStmt* let) {
        std::string name = let->get_name();
        std::string type = let->get_type();
        
        std::string llvm_type = get_llvm_type(type);
        std::string reg = "%" + name;
        
        ir_ << "  " << reg << " = alloca " << llvm_type << ", align 4\n";
        
        // Initialize if there's an initializer
        if (let->get_initializer()) {
            auto* init = let->get_initializer();
            if (init->get_kind() == ast::Expression::Kind::Literal) {
                auto* lit = static_cast<ast::LiteralExpr*>(init);
                auto& val = lit->get_value();
                
                std::string init_val;
                if (std::holds_alternative<int64_t>(val)) {
                    init_val = std::to_string(std::get<int64_t>(val));
                } else if (std::holds_alternative<double>(val)) {
                    init_val = std::to_string(std::get<double>(val));
                } else {
                    init_val = "0";
                }
                
                ir_ << "  store " << llvm_type << " " << init_val << ", " 
                    << llvm_type << "* " << reg << ", align 4\n";
            }
        }
        
        variables_[name] = reg;
    }
    
    void generate_assign(ast::AssignStmt* assign) {
        auto* target = assign->get_target();
        auto* value = assign->get_value();
        
        if (target->get_kind() == ast::Expression::Kind::Identifier) {
            auto* ident = static_cast<ast::IdentifierExpr*>(target);
            std::string name = ident->get_name();
            
            if (variables_.count(name)) {
                std::string reg = variables_[name];
                std::string val_reg = generate_expression(value);
                ir_ << "  store " << val_reg << ", " 
                    << "* " << reg << ", align 4\n";
            }
        } else if (target->get_kind() == ast::Expression::Kind::Index) {
            // Handle array/tensor index assignment
            auto* index_expr = static_cast<ast::IndexExpr*>(target);
            std::string val_reg = generate_expression(value);
            ir_ << "  ; Index assignment: " << val_reg << "\n";
        }
    }
    
    void generate_return(ast::ReturnStmt* ret) {
        if (ret->get_value()) {
            std::string val_reg = generate_expression(ret->get_value());
            ir_ << "  ret i32 " << val_reg << "\n";
        } else {
            ir_ << "  ret i32 0\n";
        }
    }
    
    void generate_if(ast::IfStmt* if_stmt) {
        std::string else_label = "else_" + std::to_string(label_counter_++);
        std::string end_label = "endif_" + std::to_string(label_counter_++);
        
        // Generate condition
        auto& conds = if_stmt->get_conditions();
        auto& bodies = if_stmt->get_bodies();
        
        if (!conds.empty()) {
            std::string cond_reg = generate_expression(conds[0].get());
            ir_ << "  br i1 " << cond_reg << ", label %" << else_label 
                << ", label %" << end_label << "\n";
            ir_ << else_label << ":\n";
        }
        
        if (!bodies.empty() && bodies[0]) {
            if (bodies[0]) generate_block(bodies[0].get());
        }
        
        ir_ << "  br label %" << end_label << "\n";
        ir_ << end_label << ":\n";
    }
    
    void generate_for(ast::ForStmt* for_stmt) {
        std::string loop_label = "loop_" + std::to_string(label_counter_++);
        std::string end_label = "endloop_" + std::to_string(label_counter_++);
        std::string var_name = for_stmt->get_variable();
        
        // For now, generate a simple loop placeholder
        ir_ << "  ; for " << var_name << " in ...\n";
        ir_ << "  br label %" << loop_label << "\n";
        ir_ << loop_label << ":\n";
        
        // Generate body
        if (for_stmt->get_body()) {
            generate_block(for_stmt->get_body());
        }
        
        ir_ << "  br label %" << loop_label << "\n";
        ir_ << end_label << ":\n";
    }
    
    void generate_while(ast::WhileStmt* while_stmt) {
        std::string loop_label = "while_loop_" + std::to_string(label_counter_++);
        std::string end_label = "endwhile_" + std::to_string(label_counter_++);
        
        ir_ << "  br label %" << loop_label << "\n";
        ir_ << loop_label << ":\n";
        
        if (while_stmt->get_condition()) {
            std::string cond_reg = generate_expression(while_stmt->get_condition());
            ir_ << "  br i1 " << cond_reg << ", label %" << end_label 
                << ", label %" << end_label << "\n";
        }
        
        if (while_stmt->get_body()) {
            generate_block(while_stmt->get_body());
        }
        
        ir_ << "  br label %" << loop_label << "\n";
        ir_ << end_label << ":\n";
    }
    
    std::string generate_expression(ast::Expression* expr) {
        if (!expr) return "0";
        
        std::string result_reg = "%tmp_" + std::to_string(label_counter_++);
        
        switch (expr->get_kind()) {
            case ast::Expression::Kind::Literal: {
                auto* lit = static_cast<ast::LiteralExpr*>(expr);
                auto& val = lit->get_value();
                
                if (std::holds_alternative<int64_t>(val)) {
                    ir_ << "  " << result_reg << " = add i32 " 
                        << std::get<int64_t>(val) << ", 0\n";
                } else if (std::holds_alternative<double>(val)) {
                    double d = std::get<double>(val);
                    ir_ << "  " << result_reg << " = fadd double " 
                        << d << ", 0.0\n";
                } else if (std::holds_alternative<std::string>(val)) {
                    // String literal - create constant
                    std::string str = std::get<std::string>(val);
                    std::string const_name = generate_string_constant(str);
                    ir_ << "  " << result_reg << " = bitcast [" 
                        << (str.length() + 1) << " x i8]* @" << const_name 
                        << " to i8*\n";
                } else {
                    ir_ << "  " << result_reg << " = add i32 0, 0\n";
                }
                break;
            }
            
            case ast::Expression::Kind::Identifier: {
                auto* ident = static_cast<ast::IdentifierExpr*>(expr);
                std::string name = ident->get_name();
                
                // Check if it's a variable
                if (variables_.count(name)) {
                    std::string var_reg = variables_[name];
                    ir_ << "  " << result_reg << " = load i32, i32* " 
                        << var_reg << ", align 4\n";
                } else {
                    // Assume it's a global or function
                    ir_ << "  ; Identifier: " << name << "\n";
                    ir_ << "  " << result_reg << " = add i32 0, 0\n";
                }
                break;
            }
            
            case ast::Expression::Kind::Binary: {
                auto* binary = static_cast<ast::BinaryExpr*>(expr);
                std::string lhs = generate_expression(binary->get_left());
                std::string rhs = generate_expression(binary->get_right());
                std::string op = claw::token_type_to_string(binary->get_operator());
                
                if (op == "+") {
                    ir_ << "  " << result_reg << " = add i32 " << lhs << ", " << rhs << "\n";
                } else if (op == "-") {
                    ir_ << "  " << result_reg << " = sub i32 " << lhs << ", " << rhs << "\n";
                } else if (op == "*") {
                    ir_ << "  " << result_reg << " = mul i32 " << lhs << ", " << rhs << "\n";
                } else if (op == "/") {
                    ir_ << "  " << result_reg << " = sdiv i32 " << lhs << ", " << rhs << "\n";
                } else if (op == "%") {
                    ir_ << "  " << result_reg << " = srem i32 " << lhs << ", " << rhs << "\n";
                } else {
                    ir_ << "  " << result_reg << " = add i32 " << lhs << ", " << rhs << "\n";
                }
                break;
            }
            
            case ast::Expression::Kind::Unary: {
                auto* unary = static_cast<ast::UnaryExpr*>(expr);
                std::string operand = generate_expression(unary->get_operand());
                std::string op = claw::token_type_to_string(unary->get_operator());
                
                if (op == "-") {
                    ir_ << "  " << result_reg << " = sub i32 0, " << operand << "\n";
                } else if (op == "!") {
                    ir_ << "  " << result_reg << " = xor i32 " << operand << ", 1\n";
                } else {
                    ir_ << "  " << result_reg << " = add i32 " << operand << ", 0\n";
                }
                break;
            }
            
            case ast::Expression::Kind::Call: {
                auto* call = static_cast<ast::CallExpr*>(expr);
                result_reg = generate_call(call);
                break;
            }
            
            case ast::Expression::Kind::Index: {
                auto* index = static_cast<ast::IndexExpr*>(expr);
                // Handle array/tensor index
                ir_ << "  ; Index expression\n";
                ir_ << "  " << result_reg << " = add i32 0, 0\n";
                break;
            }
            
            default:
                ir_ << "  ; Unknown expression: " << static_cast<int>(expr->get_kind()) << "\n";
                ir_ << "  " << result_reg << " = add i32 0, 0\n";
                break;
        }
        
        return result_reg;
    }
    
    std::string generate_call(ast::CallExpr* call) {
        auto* callee = call->get_callee();
        std::string func_name;
        
        if (callee->get_kind() == ast::Expression::Kind::Identifier) {
            func_name = static_cast<ast::IdentifierExpr*>(callee)->get_name();
        }
        
        std::string result_reg = "%call_" + std::to_string(label_counter_++);
        
        // Handle built-in functions
        if (func_name == "println") {
            // Generate printf call
            if (!call->get_arguments().empty()) {
                auto* first_arg = call->get_arguments()[0].get();
                
                if (first_arg->get_kind() == ast::Expression::Kind::Literal) {
                    auto* lit = static_cast<ast::LiteralExpr*>(first_arg);
                    auto& val = lit->get_value();
                    
                    if (std::holds_alternative<std::string>(val)) {
                        std::string str = std::get<std::string>(val);
                        std::string const_name = generate_string_constant(str + "\n");
                        ir_ << "  " << result_reg << " = call i32 @printf(i8* getelementptr "
                            << "([" << (str.length() + 2) << " x i8], ["
                            << (str.length() + 2) << " x i8]* @" << const_name 
                            << ", i32 0, i32 0))\n";
                    } else {
                        // Print number
                        std::string num_str = generate_string_constant("%d\n");
                        std::string val_reg = generate_expression(first_arg);
                        ir_ << "  " << result_reg << " = call i32 @printf(i8* getelementptr "
                            << "([4 x i8], [4 x i8]* @" << num_str 
                            << ", i32 0, i32 0), i32 " << val_reg << ")\n";
                    }
                } else {
                    // Expression - print as number
                    std::string val_reg = generate_expression(first_arg);
                    std::string fmt = generate_string_constant("%d\n");
                    ir_ << "  " << result_reg << " = call i32 @printf(i8* getelementptr "
                        << "([4 x i8], [4 x i8]* @" << fmt 
                        << ", i32 0, i32 0), i32 " << val_reg << ")\n";
                }
            }
        } else {
            // Regular function call
            ir_ << "  " << result_reg << " = call i32 @" << func_name << "()\n";
        }
        
        return result_reg;
    }
    
    std::string generate_string_constant(const std::string& str) {
        std::string name = ".str_" + std::to_string(string_counter_++);
        
        // Escape the string
        std::string escaped;
        for (char c : str) {
            if (c == '\n') escaped += "\\0A";
            else if (c == '\t') escaped += "\\09";
            else if (c == '"') escaped += "\\22";
            else if (c == '\\') escaped += "\\\\";
            else escaped += c;
        }
        
        ir_ << "@" << name << " = private constant [" << (str.length() + 1) 
            << " x i8] c\"" << escaped << "\\00\"\n";
        
        string_constants_[str] = name;
        return name;
    }
    
    std::string get_llvm_type(const std::string& claw_type) {
        if (claw_type.empty() || claw_type == "i32" || claw_type == "u32") {
            return "i32";
        } else if (claw_type == "i64" || claw_type == "u64") {
            return "i64";
        } else if (claw_type == "f32") {
            return "float";
        } else if (claw_type == "f64") {
            return "double";
        } else if (claw_type == "bool") {
            return "i1";
        } else if (claw_type == "string") {
            return "i8*";
        } else if (claw_type.find("[") != std::string::npos) {
            // Array type - return pointer
            return "i32*";
        }
        
        return "i32";
    }
};

} // namespace codegen
} // namespace claw

#endif // CLAW_SIMPLE_CODEGEN_H
