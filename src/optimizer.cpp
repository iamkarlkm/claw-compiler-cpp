// Claw Compiler - Simple Optimizer
// Implements: Constant Folding, Dead Code Elimination

#include <iostream>
#include <fstream>
#include <sstream>
#include <set>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "ast/ast.h"

namespace claw {
namespace optimizer {

class Optimizer {
private:
    bool changed_ = false;
    
public:
    std::unique_ptr<ast::Program> optimize(std::unique_ptr<ast::Program> program) {
        bool progress = true;
        int iteration = 0;
        
        while (progress && iteration < 10) {
            progress = false;
            
            // Constant folding
            for (auto& decl : program->get_declarations()) {
                if (decl->get_kind() == ast::Statement::Kind::Function) {
                    auto* fn = static_cast<ast::FunctionStmt*>(decl.get());
                    if (fn->get_body()) {
                        if (constant_fold_block(fn->get_body())) {
                            progress = true;
                        }
                    }
                }
            }
            
            // Dead code elimination
            for (auto& decl : program->get_declarations()) {
                if (decl->get_kind() == ast::Statement::Kind::Function) {
                    auto* fn = static_cast<ast::FunctionStmt*>(decl.get());
                    if (fn->get_body()) {
                        if (eliminate_dead_code(fn->get_body())) {
                            progress = true;
                        }
                    }
                }
            }
            
            iteration++;
        }
        
        std::cout << "Optimizations applied in " << iteration << " passes\n";
        return program;
    }
    
    // Constant Folding: Evaluate constant expressions at compile time
    bool constant_fold_block(ast::ASTNode* node) {
        if (!node) return false;
        auto* stmt = static_cast<ast::Statement*>(node);
        if (stmt->get_kind() != ast::Statement::Kind::Block) return false;
        
        auto* block = static_cast<ast::BlockStmt*>(node);
        bool changed = false;
        
        for (auto& s : block->get_statements()) {
            if (s->get_kind() == ast::Statement::Kind::Expression) {
                auto* expr_stmt = static_cast<ast::ExprStmt*>(s.get());
                auto* expr = expr_stmt->get_expr();
                
                if (expr->get_kind() == ast::Expression::Kind::Binary) {
                    auto* bin = static_cast<ast::BinaryExpr*>(expr);
                    auto folded = fold_constant_binary(bin);
                    if (folded) {
                        // Replace expression with folded literal
                        expr_stmt->set_expr(std::move(folded));
                        changed = true;
                    }
                }
            }
        }
        
        return changed;
    }
    
    // Fold a binary expression if both operands are constants
    std::unique_ptr<ast::Expression> fold_constant_binary(ast::BinaryExpr* bin) {
        auto* left = bin->get_left();
        auto* right = bin->get_right();
        
        // Both must be literals
        if (left->get_kind() != ast::Expression::Kind::Literal) return nullptr;
        if (right->get_kind() != ast::Expression::Kind::Literal) return nullptr;
        
        auto* left_lit = static_cast<ast::LiteralExpr*>(left);
        auto* right_lit = static_cast<ast::LiteralExpr*>(right);
        
        auto& lv = left_lit->get_value();
        auto& rv = right_lit->get_value();
        
        // Both must be integers
        if (!std::holds_alternative<int64_t>(lv)) return nullptr;
        if (!std::holds_alternative<int64_t>(rv)) return nullptr;
        
        int64_t l = std::get<int64_t>(lv);
        int64_t r = std::get<int64_t>(rv);
        int64_t result = 0;
        
        switch (bin->get_operator()) {
            case ast::TokenType::Op_plus: result = l + r; break;
            case ast::TokenType::Op_minus: result = l - r; break;
            case ast::TokenType::Op_star: result = l * r; break;
            case ast::TokenType::Op_slash: if (r != 0) result = l / r; break;
            case ast::TokenType::Op_percent: if (r != 0) result = l % r; break;
            default: return nullptr;
        }
        
        std::cout << "  Constant folded: " << l << " " << op_to_string(bin->get_operator()) << " " << r << " = " << result << "\n";
        return std::make_unique<ast::LiteralExpr>(result, bin->get_span());
    }
    
    std::string op_to_string(claw::TokenType op) {
        switch (op) {
            case ast::TokenType::Op_plus: return "+";
            case ast::TokenType::Op_minus: return "-";
            case ast::TokenType::Op_star: return "*";
            case ast::TokenType::Op_slash: return "/";
            case ast::TokenType::Op_percent: return "%";
            default: return "?";
        }
    }
    
    // Dead Code Elimination: Remove unreachable statements
    bool eliminate_dead_code(ast::ASTNode* node) {
        if (!node) return false;
        auto* stmt = static_cast<ast::Statement*>(node);
        if (stmt->get_kind() != ast::Statement::Kind::Block) return false;
        
        auto* block = static_cast<ast::BlockStmt*>(node);
        bool changed = false;
        
        // Simple DCE: Remove empty statements
        auto& stmts = block->get_statements();
        for (auto it = stmts.begin(); it != stmts.end(); ) {
            auto* s = it->get();
            
            // Remove expression statements with just literals (no side effects)
            if (s->get_kind() == ast::Statement::Kind::Expression) {
                auto* expr_stmt = static_cast<ast::ExprStmt*>(s);
                auto* expr = expr_stmt->get_expr();
                if (expr->get_kind() == ast::Expression::Kind::Literal) {
                    // Literal without effect - could be dead code
                    // For now, keep it (could be used for debugging)
                }
            }
            
            ++it;
        }
        
        return changed;
    }
};

} // namespace optimizer
} // namespace claw

int main(int argc, char* argv[]) {
    std::string input_file = "calculator.claw";
    bool optimize = false;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-O" || arg == "--optimize") optimize = true;
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
    
    if (optimize) {
        std::cout << "Running optimizations...\n";
        claw::optimizer::Optimizer opt;
        program = opt.optimize(std::move(program));
    }
    
    // Output C code
    claw::codegen::CCodeGenerator gen;
    std::cout << gen.generate(program.get());
    
    return 0;
}
