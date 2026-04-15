// Simple C Code Generator - outputs C code that can be compiled with clang

#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "ast/ast.h"

namespace claw {
namespace codegen {

class CCodeGenerator {
private:
    std::ostringstream code_;
    int var_counter_ = 0;
    std::map<std::string, std::string> var_map_;
    
public:
    std::string generate(ast::Program* program) {
        code_ << "#include <stdio.h>\n";
        code_ << "#include <stdint.h>\n\n";
        
        // Generate functions
        for (const auto& decl : program->get_declarations()) {
            if (decl->get_kind() == ast::Statement::Kind::Function) {
                auto* fn = static_cast<ast::FunctionStmt*>(decl.get());
                generate_function(fn);
            }
        }
        
        return code_.str();
    }
    
    void generate_function(ast::FunctionStmt* fn) {
        std::string fn_name = fn->get_name();
        code_ << "int " << fn_name << "() {\n";
        
        var_map_.clear();
        
        if (fn->get_body()) {
            generate_block(fn->get_body());
        }
        
        code_ << "    return 0;\n";
        code_ << "}\n\n";
    }
    
    void generate_block(ast::ASTNode* node) {
        if (!node) return;
        auto* stmt_ptr = static_cast<ast::Statement*>(node);
        if (stmt_ptr->get_kind() == ast::Statement::Kind::Block) {
            auto* block = static_cast<ast::BlockStmt*>(node);
            for (const auto& stmt : block->get_statements()) {
                generate_statement(stmt.get());
            }
        }
    }
    
    void generate_statement(ast::Statement* stmt) {
        if (!stmt) return;
        
        switch (stmt->get_kind()) {
            case ast::Statement::Kind::Let: {
                auto* let = static_cast<ast::LetStmt*>(stmt);
                std::string name = let->get_name();
                std::string type = let->get_type();
                std::string c_type = clawe_type_to_c(type);
                var_map_[name] = c_type;
                code_ << "    " << c_type << " " << name << ";\n";
                break;
            }
            case ast::Statement::Kind::Expression: {
                auto* expr_stmt = static_cast<ast::ExprStmt*>(stmt);
                auto* expr = expr_stmt->get_expr();
                if (expr->get_kind() == ast::Expression::Kind::Call) {
                    generate_call(static_cast<ast::CallExpr*>(expr));
                } else if (expr->get_kind() == ast::Expression::Kind::Binary) {
                    // Assignment
                    generate_assignment(static_cast<ast::BinaryExpr*>(expr));
                }
                break;
            }
            default:
                break;
        }
    }
    
    std::string clawe_type_to_c(const std::string& type) {
        if (type.find("u32") != std::string::npos) return "uint32_t";
        if (type.find("u64") != std::string::npos) return "uint64_t";
        if (type.find("i32") != std::string::npos) return "int32_t";
        if (type.find("i64") != std::string::npos) return "int64_t";
        if (type.find("f32") != std::string::npos) return "float";
        if (type.find("f64") != std::string::npos) return "double";
        return "int";
    }
    
    void generate_call(ast::CallExpr* call) {
        auto* callee = call->get_callee();
        if (callee->get_kind() == ast::Expression::Kind::Identifier) {
            auto* ident = static_cast<ast::IdentifierExpr*>(callee);
            std::string func_name = ident->get_name();
            
            if (func_name == "println") {
                // Handle each argument
                for (size_t i = 0; i < call->get_arguments().size(); i++) {
                    auto* arg = call->get_arguments()[i].get();
                    auto kind = arg->get_kind();
                    
                    if (kind == ast::Expression::Kind::Literal) {
                        auto* lit = static_cast<ast::LiteralExpr*>(arg);
                        auto& val = lit->get_value();
                        if (std::holds_alternative<std::string>(val)) {
                            std::string str = std::get<std::string>(val);
                            code_ << "    printf(\"" << str << "\\n\");\n";
                        } else if (std::holds_alternative<int64_t>(val)) {
                            int64_t n = std::get<int64_t>(val);
                            code_ << "    printf(\"%lld\\n\", (long long)" << n << ");\n";
                        }
                    } else if (kind == ast::Expression::Kind::Identifier) {
                        auto* id = static_cast<ast::IdentifierExpr*>(arg);
                        code_ << "    printf(\"%u\\n\", " << id->get_name() << ");\n";
                    } else if (kind == ast::Expression::Kind::Index) {
                        auto* idx = static_cast<ast::IndexExpr*>(arg);
                        auto* obj = idx->get_object();
                        if (obj->get_kind() == ast::Expression::Kind::Identifier) {
                            auto* id = static_cast<ast::IdentifierExpr*>(obj);
                            code_ << "    printf(\"%u\\n\", " << id->get_name() << ");\n";
                        }
                    } else if (kind == ast::Expression::Kind::Binary) {
                        auto* bin = static_cast<ast::BinaryExpr*>(arg);
                        code_ << "    printf(\"%u\\n\", " << generate_expr(bin) << ");\n";
                    }
                }
            }
        }
    }
    
    void generate_assignment(ast::BinaryExpr* bin) {
        auto* left = bin->get_left();
        auto* right = bin->get_right();
        
        // Handle IndexExpr on left (a[1] = ...)
        if (left->get_kind() == ast::Expression::Kind::Index) {
            auto* idx = static_cast<ast::IndexExpr*>(left);
            auto* obj = idx->get_object();
            if (obj->get_kind() == ast::Expression::Kind::Identifier) {
                auto* ident = static_cast<ast::IdentifierExpr*>(obj);
                std::string var_name = ident->get_name();
                code_ << "    " << var_name << " = " << generate_expr(right) << ";\n";
            }
        }
        // Handle direct Identifier on left (a = ...)
        else if (left->get_kind() == ast::Expression::Kind::Identifier) {
            auto* ident = static_cast<ast::IdentifierExpr*>(left);
            code_ << "    " << ident->get_name() << " = " << generate_expr(right) << ";\n";
        }
    }
    
    // Generate expression as string
    std::string generate_expr(ast::Expression* expr) {
        if (!expr) return "0";
        
        switch (expr->get_kind()) {
            case ast::Expression::Kind::Literal: {
                auto* lit = static_cast<ast::LiteralExpr*>(expr);
                auto& val = lit->get_value();
                if (std::holds_alternative<int64_t>(val)) {
                    return std::to_string(std::get<int64_t>(val));
                }
                return "0";
            }
            case ast::Expression::Kind::Identifier: {
                auto* ident = static_cast<ast::IdentifierExpr*>(expr);
                return ident->get_name();
            }
            case ast::Expression::Kind::Index: {
                auto* idx = static_cast<ast::IndexExpr*>(expr);
                auto* obj = idx->get_object();
                if (obj->get_kind() == ast::Expression::Kind::Identifier) {
                    auto* ident = static_cast<ast::IdentifierExpr*>(obj);
                    return ident->get_name();  // Just var name, ignore index
                }
                return "0";
            }
            case ast::Expression::Kind::Binary: {
                auto* bin = static_cast<ast::BinaryExpr*>(expr);
                std::string op;
                switch (bin->get_operator()) {
                    case claw::TokenType::Op_plus: op = "+"; break;
                    case claw::TokenType::Op_minus: op = "-"; break;
                    case claw::TokenType::Op_star: op = "*"; break;
                    case claw::TokenType::Op_slash: op = "/"; break;
                    default: op = "+";
                }
                return generate_expr(bin->get_left()) + " " + op + " " + generate_expr(bin->get_right());
            }
            default:
                return "0";
        }
    }
};

} // namespace codegen
} // namespace claw

int main(int argc, char* argv[]) {
    std::string input_file = "calculator.claw";
    bool codegen = false;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-c" || arg == "--codegen") codegen = true;
        else input_file = arg;
    }
    
    std::ifstream fin(input_file);
    if (!fin) {
        std::cerr << "Error: Cannot open " << input_file << "\n";
        return 1;
    }
    std::stringstream buf; buf << fin.rdbuf();
    std::string source = buf.str();
    
    claw::Lexer lexer(source, input_file);
    auto tokens = lexer.scan_all();
    claw::Parser parser(tokens);
    auto program = parser.parse();
    
    if (!codegen) {
        std::cout << "Parse successful!\n";
        return 0;
    }
    
    claw::codegen::CCodeGenerator generator;
    std::string code = generator.generate(program.get());
    
    std::cout << code;
    return 0;
}
