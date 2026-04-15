#include <iostream>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "common/common.h"

// Override Parser to add debugging
class DebugParser : public claw::Parser {
public:
    using claw::Parser::Parser;
    
    // Override parse_function_declaration
    std::unique_ptr<claw::ast::Statement> parse_function_declaration(bool is_serial = false) override {
        std::cout << ">>> parse_function_declaration called\n";
        
        auto fn = std::make_unique<claw::ast::FunctionStmt>("", span_from(peek()));
        
        std::cout << "  current before Kw_fn check: " << current << "\n";
        
        // Check for 'fn'
        std::cout << "  check(Kw_fn): " << check(claw::TokenType::Kw_fn) << "\n";
        
        if (!check(claw::TokenType::Kw_fn)) {
            std::cout << "  ERROR: Expected 'fn'\n";
            return nullptr;
        }
        
        advance(); // consume 'fn'
        std::cout << "  current after advance: " << current << "\n";
        
        // Get function name
        std::cout << "  check(Identifier): " << check(claw::TokenType::Identifier) << "\n";
        std::cout << "  peek().text: '" << peek().text << "'\n";
        
        if (!check(claw::TokenType::Identifier)) {
            std::cout << "  ERROR: Expected function name\n";
            return nullptr;
        }
        
        fn->set_name(previous().text);
        std::cout << "  set_name to: '" << fn->get_name() << "'\n";
        
        advance();
        std::cout << "  current after name: " << current << "\n";
        
        fn->set_params({});
        fn->set_body(parse_block());
        
        return fn;
    }
};

int main() {
    std::string source = "fn main() { }";
    
    claw::Lexer lexer(source, "test.claw");
    auto tokens = lexer.scan_all();
    
    DebugParser parser(tokens);
    auto program = parser.parse();
    
    for (const auto& decl : program->get_declarations()) {
        if (decl->get_kind() == claw::ast::Statement::Kind::Function) {
            auto* fn = static_cast<claw::ast::FunctionStmt*>(decl.get());
            std::cout << "\nFinal function name: '" << fn->get_name() << "'\n";
        }
    }
    
    return 0;
}
