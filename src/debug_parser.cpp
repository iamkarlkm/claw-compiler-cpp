#include <iostream>
#include <sstream>
#include "lexer/lexer.h"
#include "parser/parser.h"

class DebugParser : public claw::Parser {
public:
    using claw::Parser::Parser;
    
    std::unique_ptr<claw::ast::Statement> parse_expression_statement() {
        std::cout << ">>> parse_expression_statement called\n";
        std::cout << "    current token: " << claw::token_type_to_string(peek().type) << "\n";
        
        auto expr = parse_expression();
        std::cout << "    after parse_expression, current: " << claw::token_type_to_string(peek().type) << "\n";
        std::cout << "    expr type: " << (int)expr->get_kind() << "\n";
        
        // Check for assignment to index (a[1] = 42)
        if (check(claw::TokenType::Op_eq_assign)) {
            std::cout << "    FOUND '='! Creating assignment\n";
            advance(); // consume '='
            auto value = parse_expression();
            
            auto assign = std::make_unique<claw::ast::AssignStmt>(
                std::move(expr), std::move(value), span_from(previous())
            );
            
            // Consume optional semicolon
            if (check(claw::TokenType::Semicolon)) {
                advance();
            }
            
            return assign;
        }
        
        std::cout << "    Not an assignment, creating expression statement\n";
        
        // Consume optional semicolon for expression statements (like function calls)
        if (check(claw::TokenType::Semicolon)) {
            advance();
        }
        
        return std::make_unique<claw::ast::ExprStmt>(std::move(expr));
    }
};

int main() {
    std::string source = "fn main() { a[1] = 42 }";
    
    claw::Lexer lexer(source, "test.claw");
    auto tokens = lexer.scan_all();
    
    DebugParser parser(tokens);
    auto program = parser.parse();
    
    std::cout << "\nAST: " << program->to_string() << "\n";
    
    return 0;
}
