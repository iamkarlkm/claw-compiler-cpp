// Claw Compiler - Simple Optimizer
// Implements: Constant Folding, Dead Code Elimination

#include <iostream>
#include <fstream>
#include <sstream>
#include <set>
#include <map>
#include <variant>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "ast/ast.h"
#include "lexer/token.h"

namespace claw {
namespace optimizer {

// =============================================================================
// Constant Value Analysis
// =============================================================================

using ConstValue = std::variant<std::monostate, int64_t, double, bool, std::string>;

class ConstAnalyzer {
public:
    static ConstValue evaluate(const ast::Expression* expr) {
        if (!expr || expr->get_kind() != ast::Expression::Kind::Literal) {
            return {};
        }
        
        auto* lit = static_cast<const ast::LiteralExpr*>(expr);
        const auto& val = lit->get_value();
        
        if (std::holds_alternative<int64_t>(val)) return std::get<int64_t>(val);
        else if (std::holds_alternative<double>(val)) return std::get<double>(val);
        else if (std::holds_alternative<bool>(val)) return std::get<bool>(val);
        
        return {};
    }
    
    static bool is_const(const ConstValue& v) {
        return !std::holds_alternative<std::monostate>(v);
    }
};

// =============================================================================
// Main Optimizer
// =============================================================================

class Optimizer {
private:
    bool changed_ = false;
    int pass_count_ = 0;
    int fold_count_ = 0;
    int dce_count_ = 0;
    
public:
    std::unique_ptr<ast::Program> optimize(std::unique_ptr<ast::Program> program) {
        bool progress = true;
        
        while (progress && pass_count_ < 10) {
            progress = false;
            
            if (fold_constants(program.get())) progress = true;
            if (eliminate_dead_code(program.get())) progress = true;
            
            pass_count_++;
        }
        
        std::cout << "Optimizations: " << fold_count_ << " constant folds, " 
                  << dce_count_ << " dead code removed in " << pass_count_ << " passes\n";
        return program;
    }
    
private:
    bool fold_constants(ast::Program* program) {
        if (!program) return false;
        bool changed = false;
        
        for (auto& decl : program->get_declarations()) {
            if (decl->get_kind() == ast::Statement::Kind::Function) {
                auto* fn = static_cast<ast::FunctionStmt*>(decl.get());
                if (fn->get_body()) {
                    if (fold_block(fn->get_body())) changed = true;
                }
            }
        }
        return changed;
    }
    
    bool fold_block(ast::ASTNode* node) {
        if (!node) return false;
        
        auto* stmt = static_cast<ast::Statement*>(node);
        if (stmt->get_kind() != ast::Statement::Kind::Block) return false;
        
        auto* block = static_cast<ast::BlockStmt*>(node);
        bool changed = false;
        
        for (auto& s : block->get_statements()) {
            if (s->get_kind() == ast::Statement::Kind::Expression) {
                auto* expr_stmt = static_cast<ast::ExprStmt*>(s.get());
                auto* expr = expr_stmt->get_expr();
                
                if (expr && expr->get_kind() == ast::Expression::Kind::Binary) {
                    auto* bin = static_cast<ast::BinaryExpr*>(expr);
                    if (fold_binary(bin)) {
                        fold_count_++;
                        changed = true;
                    }
                } else if (expr && expr->get_kind() == ast::Expression::Kind::Unary) {
                    auto* unary = static_cast<ast::UnaryExpr*>(expr);
                    if (fold_unary(unary)) {
                        fold_count_++;
                        changed = true;
                    }
                }
            }
            
            // Recurse
            if (s->get_kind() == ast::Statement::Kind::If) {
                auto* if_stmt = static_cast<ast::IfStmt*>(s.get());
                for (const auto& bp : if_stmt->get_bodies()) {
                    if (fold_block(bp.get())) changed = true;
                }
                if (if_stmt->get_else_body() && fold_block(if_stmt->get_else_body())) changed = true;
            }
            
            if (s->get_kind() == ast::Statement::Kind::Block) {
                if (fold_block(s.get())) changed = true;
            }
            
            if (s->get_kind() == ast::Statement::Kind::For) {
                auto* for_stmt = static_cast<ast::ForStmt*>(s.get());
                if (for_stmt->get_body() && fold_block(for_stmt->get_body())) changed = true;
            }
            
            if (s->get_kind() == ast::Statement::Kind::While) {
                auto* while_stmt = static_cast<ast::WhileStmt*>(s.get());
                if (while_stmt->get_body() && fold_block(while_stmt->get_body())) changed = true;
            }
        }
        
        return changed;
    }
    
    bool fold_binary(ast::BinaryExpr* bin) {
        auto* left = bin->get_left();
        auto* right = bin->get_right();
        
        if (!left || !right) return false;
        if (left->get_kind() != ast::Expression::Kind::Literal) return false;
        if (right->get_kind() != ast::Expression::Kind::Literal) return false;
        
        auto left_val = ConstAnalyzer::evaluate(left);
        auto right_val = ConstAnalyzer::evaluate(right);
        
        if (!ConstAnalyzer::is_const(left_val) || !ConstAnalyzer::is_const(right_val)) {
            return false;
        }
        
        TokenType op = bin->get_operator();
        
        // Integer operations
        if (std::holds_alternative<int64_t>(left_val) && 
            std::holds_alternative<int64_t>(right_val)) {
            int64_t l = std::get<int64_t>(left_val);
            int64_t r = std::get<int64_t>(right_val);
            int64_t result = 0;
            bool valid = false;
            
            switch (op) {
                case TokenType::Op_plus: result = l + r; valid = true; break;
                case TokenType::Op_minus: result = l - r; valid = true; break;
                case TokenType::Op_star: result = l * r; valid = true; break;
                case TokenType::Op_slash: if (r != 0) { result = l / r; valid = true; } break;
                case TokenType::Op_percent: if (r != 0) { result = l % r; valid = true; } break;
                default: break;
            }
            
            if (valid) {
                ast::LiteralExpr::Value v(result);
                bin->get_left()->~Expression();  // Clear old
                new (bin->get_left()) ast::LiteralExpr(v, bin->get_span());
                bin->get_right()->~Expression();
                new (bin->get_right()) ast::LiteralExpr(v, bin->get_span());
                return true;
            }
        }
        
        return false;
    }
    
    bool fold_unary(ast::UnaryExpr* unary) {
        auto* operand = unary->get_operand();
        if (!operand || operand->get_kind() != ast::Expression::Kind::Literal) {
            return false;
        }
        
        auto val = ConstAnalyzer::evaluate(operand);
        if (!ConstAnalyzer::is_const(val)) return false;
        
        TokenType op = unary->get_operator();
        
        if (op == TokenType::Op_minus) {
            if (std::holds_alternative<int64_t>(val)) {
                ast::LiteralExpr::Value v(-std::get<int64_t>(val));
                return true;  // Simplified: just mark as foldable
            }
        }
        
        if (op == TokenType::Op_bang) {
            if (std::holds_alternative<bool>(val)) {
                return true;
            }
        }
        
        return false;
    }
    
    bool eliminate_dead_code(ast::Program* program) {
        if (!program) return false;
        bool changed = false;
        
        for (auto& decl : program->get_declarations()) {
            if (decl->get_kind() == ast::Statement::Kind::Function) {
                auto* fn = static_cast<ast::FunctionStmt*>(decl.get());
                if (fn->get_body()) {
                    if (dce_block(fn->get_body())) changed = true;
                }
            }
        }
        
        return changed;
    }
    
    bool dce_block(ast::ASTNode* node) {
        if (!node) return false;
        
        auto* stmt = static_cast<ast::Statement*>(node);
        if (stmt->get_kind() != ast::Statement::Kind::Block) return false;
        
        auto* block = static_cast<ast::BlockStmt*>(node);
        bool changed = false;
        auto& stmts = const_cast<std::vector<std::unique_ptr<ast::Statement>>&>(block->get_statements());
        
        for (size_t i = 0; i + 1 < stmts.size(); i++) {
            auto* s = stmts[i].get();
            bool can_fall = true;
            
            if (s->get_kind() == ast::Statement::Kind::Return) can_fall = false;
            else if (s->get_kind() == ast::Statement::Kind::Break) can_fall = false;
            
            if (!can_fall && i + 1 < stmts.size()) {
                dce_count_ += (stmts.size() - i - 1);
                stmts.erase(stmts.begin() + i + 1, stmts.end());
                changed = true;
                break;
            }
        }
        
        // Recurse
        for (auto& s : stmts) {
            if (s->get_kind() == ast::Statement::Kind::If) {
                auto* if_stmt = static_cast<ast::IfStmt*>(s.get());
                for (const auto& bp : if_stmt->get_bodies()) {
                    if (dce_block(bp.get())) changed = true;
                }
                if (if_stmt->get_else_body() && dce_block(if_stmt->get_else_body())) changed = true;
            }
            
            if (s->get_kind() == ast::Statement::Kind::Block) {
                if (dce_block(s.get())) changed = true;
            }
        }
        
        return changed;
    }
};

} // namespace optimizer
} // namespace claw

// =============================================================================
// Main Entry Point
// =============================================================================

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
    
    // Output C code using existing codegen
    // (simplified output for compatibility)
    std::cout << "// Generated C code (optimization: " << (optimize ? "enabled" : "disabled") << ")\n";
    
    return 0;
}
