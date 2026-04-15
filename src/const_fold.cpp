// Claw Compiler - Constant Folding Optimization
// Folds constant expressions at compile time

#include <iostream>
#include <fstream>
#include <sstream>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "ast/ast.h"

namespace claw {
namespace optimizer {

class ConstantFolder {
public:
    // Fold constants in a binary expression tree
    // Returns new expression (may be a Literal if fully folded)
    std::unique_ptr<ast::Expression> fold(ast::Expression* expr) {
        if (!expr) return nullptr;
        
        if (expr->get_kind() == ast::Expression::Kind::Binary) {
            auto* bin = static_cast<ast::BinaryExpr*>(expr);
            return fold_binary(bin);
        }
        
        // Return clone for non-binary expressions
        return clone_expr(expr);
    }
    
private:
    std::unique_ptr<ast::Expression> fold_binary(ast::BinaryExpr* bin) {
        auto op = bin->get_operator();
        
        // Recursively fold operands
        auto left_folded = fold(bin->get_left());
        auto right_folded = fold(bin->get_right());
        
        // Check if both are now literals
        if (left_folded->get_kind() == ast::Expression::Kind::Literal &&
            right_folded->get_kind() == ast::Expression::Kind::Literal) {
            
            auto* left_lit = static_cast<ast::LiteralExpr*>(left_folded.get());
            auto* right_lit = static_cast<ast::LiteralExpr*>(right_folded.get());
            
            auto& lv = left_lit->get_value();
            auto& rv = right_lit->get_value();
            
            // Only fold integer operations
            if (std::holds_alternative<int64_t>(lv) && std::holds_alternative<int64_t>(rv)) {
                int64_t l = std::get<int64_t>(lv);
                int64_t r = std::get<int64_t>(rv);
                int64_t result = 0;
                bool can_fold = true;
                
                switch (op) {
                    case TokenType::Op_plus:  result = l + r; break;
                    case TokenType::Op_minus: result = l - r; break;
                    case TokenType::Op_star:  result = l * r; break;
                    case TokenType::Op_slash: 
                        if (r != 0) result = l / r; 
                        else can_fold = false;
                        break;
                    case TokenType::Op_percent:
                        if (r != 0) result = l % r;
                        else can_fold = false;
                        break;
                    default: can_fold = false;
                }
                
                if (can_fold) {
                    std::cout << "  Folded: " << l << " " << op_to_str(op) << " " << r << " = " << result << "\n";
                    ast::LiteralExpr::Value vresult = result;
                    return std::make_unique<ast::LiteralExpr>(vresult, bin->get_span());
                }
            }
        }
        
        // Can't fold - return new BinaryExpr with folded operands
        return std::make_unique<ast::BinaryExpr>(
            op, std::move(left_folded), std::move(right_folded), bin->get_span()
        );
    }
    
    std::unique_ptr<ast::Expression> clone_expr(ast::Expression* expr) {
        if (!expr) return nullptr;
        
        switch (expr->get_kind()) {
            case ast::Expression::Kind::Literal: {
                auto* lit = static_cast<ast::LiteralExpr*>(expr);
                return std::make_unique<ast::LiteralExpr>(lit->get_value(), lit->get_span());
            }
            case ast::Expression::Kind::Identifier: {
                auto* id = static_cast<ast::IdentifierExpr*>(expr);
                return std::make_unique<ast::IdentifierExpr>(id->get_name(), id->get_span());
            }
            default:
                return nullptr;
        }
    }
    
    const char* op_to_str(TokenType op) {
        switch (op) {
            case TokenType::Op_plus: return "+";
            case TokenType::Op_minus: return "-";
            case TokenType::Op_star: return "*";
            case TokenType::Op_slash: return "/";
            case TokenType::Op_percent: return "%";
            default: return "?";
        }
    }
};

// Demo: Test constant folding
void test_constant_folding() {
    std::cout << "=== Constant Folding Demo ===\n\n";
    
    // Create a simple expression: 10 + 20 * 3
    // Should fold to: 10 + 60 = 70
    
    ast::LiteralExpr::Value v10 = int64_t(10);
    ast::LiteralExpr::Value v20 = int64_t(20);
    ast::LiteralExpr::Value v3 = int64_t(3);
    auto left = std::make_unique<ast::LiteralExpr>(v10, SourceSpan());
    auto right_left = std::make_unique<ast::LiteralExpr>(v20, SourceSpan());
    auto right_right = std::make_unique<ast::LiteralExpr>(v3, SourceSpan());
    auto right = std::make_unique<ast::BinaryExpr>(
        TokenType::Op_star, std::move(right_left), std::move(right_right), SourceSpan()
    );
    auto expr = std::make_unique<ast::BinaryExpr>(
        TokenType::Op_plus, std::move(left), std::move(right), SourceSpan()
    );
    
    std::cout << "Expression: 10 + 20 * 3\n";
    ConstantFolder folder;
    auto result = folder.fold(expr.get());
    
    if (result->get_kind() == ast::Expression::Kind::Literal) {
        auto* lit = static_cast<ast::LiteralExpr*>(result.get());
        std::cout << "Result: " << std::get<int64_t>(lit->get_value()) << "\n";
    }
}

} // namespace optimizer
} // namespace claw

int main(int argc, char* argv[]) {
    // Demo constant folding
    claw::optimizer::test_constant_folding();
    
    // Also test with file if provided
    if (argc > 1) {
        std::string input_file = argv[1];
        std::ifstream fin(input_file);
        if (fin) {
            std::stringstream buf; buf << fin.rdbuf();
            std::string source = buf.str();
            
            claw::Lexer lexer(source, input_file);
            auto tokens = lexer.scan_all();
            claw::Parser parser(tokens);
            auto program = parser.parse();
            
            std::cout << "\nParsed " << input_file << " successfully!\n";
        }
    }
    
    return 0;
}
