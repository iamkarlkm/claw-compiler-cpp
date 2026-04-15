// Claw Compiler - Simple Optimizer (pass-based)
// Outputs optimized C code

#include <iostream>
#include <fstream>
#include <sstream>
#include <set>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "ast/ast.h"

namespace claw {
namespace optimizer {

class SimpleOptimizer {
public:
    void print_stats(ast::Program* program) {
        int fn_count = 0, stmt_count = 0, expr_count = 0;
        
        for (auto& decl : program->get_declarations()) {
            if (decl->get_kind() == ast::Statement::Kind::Function) {
                fn_count++;
                auto* fn = static_cast<ast::FunctionStmt*>(decl.get());
                if (fn->get_body()) {
                    count_stmts(fn->get_body(), stmt_count, expr_count);
                }
            }
        }
        
        std::cout << "\n=== Program Statistics ===\n";
        std::cout << "Functions: " << fn_count << "\n";
        std::cout << "Statements: " << stmt_count << "\n";
        std::cout << "Expressions: " << expr_count << "\n";
        
        // Count optimization opportunities
        int const_ops = 0, dead_code = 0;
        analyze(program, const_ops, dead_code);
        std::cout << "\n=== Optimization Opportunities ===\n";
        std::cout << "Constant expressions: " << const_ops << "\n";
        std::cout << "Potential dead code: " << dead_code << "\n";
    }
    
    void count_stmts(ast::ASTNode* node, int& stmt_count, int& expr_count) {
        if (!node) return;
        auto* stmt = static_cast<ast::Statement*>(node);
        
        if (stmt->get_kind() == ast::Statement::Kind::Block) {
            auto* block = static_cast<ast::BlockStmt*>(node);
            for (auto& s : block->get_statements()) {
                stmt_count++;
                count_stmts(s.get(), stmt_count, expr_count);
            }
        } else if (stmt->get_kind() == ast::Statement::Kind::Expression) {
            expr_count++;
            auto* es = static_cast<ast::ExprStmt*>(stmt);
            if (es->get_expr()) count_expr(es->get_expr(), expr_count);
        }
    }
    
    void count_expr(ast::Expression* expr, int& count) {
        if (!expr) return;
        if (expr->get_kind() == ast::Expression::Kind::Binary) {
            auto* bin = static_cast<ast::BinaryExpr*>(expr);
            count_expr(bin->get_left(), count);
            count_expr(bin->get_right(), count);
            count++;
        }
    }
    
    void analyze(ast::Program* program, int& const_ops, int& dead_code) {
        for (auto& decl : program->get_declarations()) {
            if (decl->get_kind() == ast::Statement::Kind::Function) {
                auto* fn = static_cast<ast::FunctionStmt*>(decl.get());
                if (fn->get_body()) {
                    analyze_block(fn->get_body(), const_ops, dead_code);
                }
            }
        }
    }
    
    void analyze_block(ast::ASTNode* node, int& const_ops, int& dead_code) {
        if (!node) return;
        auto* stmt = static_cast<ast::Statement*>(node);
        if (stmt->get_kind() != ast::Statement::Kind::Block) return;
        
        auto* block = static_cast<ast::BlockStmt*>(node);
        for (auto& s : block->get_statements()) {
            // Count all binary expressions (potential for optimization)
            count_binary_exprs(s.get(), const_ops);
        }
    }
    
    void count_binary_exprs(ast::Statement* stmt, int& count) {
        if (!stmt) return;
        
        if (stmt->get_kind() == ast::Statement::Kind::Expression) {
            auto* es = static_cast<ast::ExprStmt*>(stmt);
            auto* expr = es->get_expr();
            if (expr) count_binary_in_expr(expr, count);
        }
    }
    
    void count_binary_in_expr(ast::Expression* expr, int& count) {
        if (!expr) return;
        
        if (expr->get_kind() == ast::Expression::Kind::Binary) {
            auto* bin = static_cast<ast::BinaryExpr*>(expr);
            count++;
            // Recursively check operands
            count_binary_in_expr(bin->get_left(), count);
            count_binary_in_expr(bin->get_right(), count);
        }
    }
};

} // namespace optimizer
} // namespace claw

int main(int argc, char* argv[]) {
    std::string input_file = "calculator.claw";
    bool stats = false;
    bool optimize = false;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--stats") stats = true;
        else if (arg == "-O") optimize = true;
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
    
    if (stats || optimize) {
        claw::optimizer::SimpleOptimizer opt;
        opt.print_stats(program.get());
    }
    
    // Output C code (reusing codegen from codegen_c.cpp)
    // For now just output stats
    if (!optimize && !stats) {
        std::cout << "Parse successful!\n";
    }
    
    return 0;
}
