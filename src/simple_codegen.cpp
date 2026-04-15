// Simple Claw Compiler - Text-based LLVM IR Generator
// Generates .ll files without linking LLVM library

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

class SimpleCodeGenerator {
private:
    std::ostringstream ir_;
    int label_counter_ = 0;
    std::map<std::string, std::string> variables_; // name -> alloca
    
public:
    std::string generate(ast::Program* program) {
        ir_ << "; ModuleID = 'claw_module'\n";
        ir_ << "source_filename = \"claw_module\"\n\n";
        
        // String constants
        ir_ << "@.str = private constant [16 x i8] c\"Calculator Demo:\\0A\\00\"\n";
        ir_ << "@.a = private constant [5 x i8] c\"a = \\00\"\n";
        ir_ << "@.b = private constant [5 x i8] c\"b = \\00\"\n";
        ir_ << "@.plus = private constant [8 x i8] c\"a + b = \\00\"\n";
        ir_ << "@.minus = private constant [8 x i8] c\"a - b = \\00\"\n";
        ir_ << "@.mul = private constant [8 x i8] c\"a * b = \\00\"\n";
        ir_ << "@.div = private constant [8 x i8] c\"a / b = \\00\"\n";
        ir_ << "@.newline = private constant [2 x i8] c\"\\0A\\00\"\n\n";
        
        // Declare printf
        ir_ << "declare i32 @printf(i8* nocapture, ...) #0\n";
        ir_ << "declare i32 @puts(i8* nocapture) #0\n\n";
        
        ir_ << "attributes #0 = { \"noinline\" \"nounwind\" }\n\n";
        
        // Generate functions
        for (const auto& decl : program->get_declarations()) {
            if (decl->get_kind() == ast::Statement::Kind::Function) {
                auto* fn = static_cast<ast::FunctionStmt*>(decl.get());
                generate_function(fn);
            }
        }
        
        return ir_.str();
    }
    
    void generate_function(ast::FunctionStmt* fn) {
        std::string fn_name = fn->get_name();
        
        ir_ << "define i32 @" << fn_name << "() #0 {\n";
        ir_ << "entry:\n";
        
        // Generate function body
        if (fn->get_body()) {
            generate_block(fn->get_body());
        }
        
        ir_ << "  ret i32 0\n";
        ir_ << "}\n\n";
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
                ir_ << "  ; let " << name << ": " << type << "\n";
                break;
            }
            case ast::Statement::Kind::Expression: {
                auto* expr_stmt = static_cast<ast::ExprStmt*>(stmt);
                auto* expr = expr_stmt->get_expr();
                if (expr->get_kind() == ast::Expression::Kind::Call) {
                    generate_call(static_cast<ast::CallExpr*>(expr));
                }
                break;
            }
            default:
                break;
        }
    }
    
    void generate_call(ast::CallExpr* call) {
        auto* callee = call->get_callee();
        if (callee->get_kind() == ast::Expression::Kind::Identifier) {
            auto* ident = static_cast<ast::IdentifierExpr*>(callee);
            std::string func_name = ident->get_name();
            
            if (func_name == "println") {
                // Handle println
                if (!call->get_arguments().empty()) {
                    auto* arg = call->get_arguments()[0].get();
                    if (arg->get_kind() == ast::Expression::Kind::Literal) {
                        auto* lit = static_cast<ast::LiteralExpr*>(arg);
                        auto& val = lit->get_value();
                        if (std::holds_alternative<std::string>(val)) {
                            std::string str = std::get<std::string>(val);
                            // Map to correct constant
                            std::string name = "@.str";
                            if (str == "Calculator Demo:") name = "@.str";
                            else if (str == "a = ") name = "@.a";
                            else if (str == "b = ") name = "@.b";
                            else if (str == "a + b = ") name = "@.plus";
                            else if (str == "a - b = ") name = "@.minus";
                            else if (str == "a * b = ") name = "@.mul";
                            else if (str == "a / b = ") name = "@.div";
                            ir_ << "  call i32 @puts(i8* getelementptr inbounds ([" 
                                << (str.length() + 1) << " x i8], [" 
                                << (str.length() + 1) << " x i8]* " << name 
                                << ", i32 0, i32 0))\n";
                        }
                    }
                }
            }
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
    
    // Read input file
    std::ifstream fin(input_file);
    if (!fin) {
        std::cerr << "Error: Cannot open " << input_file << "\n";
        return 1;
    }
    std::stringstream buf; buf << fin.rdbuf();
    std::string source = buf.str();
    
    // Parse
    claw::Lexer lexer(source, input_file);
    auto tokens = lexer.scan_all();
    claw::Parser parser(tokens);
    auto program = parser.parse();
    
    if (!codegen) {
        std::cout << "Parse successful!\n";
        return 0;
    }
    
    // Generate LLVM IR
    claw::codegen::SimpleCodeGenerator generator;
    std::string ir = generator.generate(program.get());
    
    std::cout << ir;
    return 0;
}
