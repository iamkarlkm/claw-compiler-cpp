#include <iostream>
#include "lexer/lexer.h"
#include "parser/parser.h"

void trace_index(claw::ast::Expression* expr, int depth = 0) {
    std::string indent(depth * 2, ' ');
    
    if (!expr) {
        std::cout << indent << "NULL\n";
        return;
    }
    
    switch (expr->get_kind()) {
        case claw::ast::Expression::Kind::Identifier: {
            auto* id = static_cast<claw::ast::IdentifierExpr*>(expr);
            std::cout << indent << "Identifier: '" << id->get_name() << "'\n";
            break;
        }
        case claw::ast::Expression::Kind::Literal: {
            auto* lit = static_cast<claw::ast::LiteralExpr*>(expr);
            std::cout << indent << "Literal: " << lit->to_string() << "\n";
            break;
        }
        case claw::ast::Expression::Kind::Index: {
            auto* idx = static_cast<claw::ast::IndexExpr*>(expr);
            std::cout << indent << "IndexExpr:\n";
            std::cout << indent << "  object:\n";
            trace_index(idx->get_object(), depth + 2);
            std::cout << indent << "  index:\n";
            trace_index(idx->get_index(), depth + 2);
            std::cout << indent << "  to_string: '" << idx->to_string() << "'\n";
            break;
        }
        default:
            std::cout << indent << "Other: " << (int)expr->get_kind() << "\n";
    }
}

int main() {
    std::string source = "fn main() { a[1] = 42 }";
    
    claw::Lexer lexer(source, "test.claw");
    auto tokens = lexer.scan_all();
    
    claw::Parser parser(tokens);
    auto program = parser.parse();
    
    auto* fn = static_cast<claw::ast::FunctionStmt*>(program->get_declarations()[0].get());
    auto* block = static_cast<claw::ast::BlockStmt*>(fn->get_body());
    
    auto* stmt = block->get_statements()[0].get();
    std::cout << "Statement kind: " << (int)stmt->get_kind() << "\n";
    std::cout << "Statement to_string: " << stmt->to_string() << "\n\n";
    
    if (stmt->get_kind() == claw::ast::Statement::Kind::Assign) {
        auto* assign = static_cast<claw::ast::AssignStmt*>(stmt);
        std::cout << "Assignment target:\n";
        trace_index(assign->get_target(), 1);
        std::cout << "\nAssignment value:\n";
        trace_index(assign->get_value(), 1);
    }
    
    return 0;
}
