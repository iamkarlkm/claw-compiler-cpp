#include <iostream>
#include <fstream>
#include <sstream>
#include "lexer/lexer.h"
#include "parser/parser.h"

void print_expr(claw::ast::Expression* expr, int indent = 0) {
    std::string prefix(indent * 2, ' ');
    if (!expr) {
        std::cout << prefix << "NULL expr\n";
        return;
    }
    switch (expr->get_kind()) {
        case claw::ast::Expression::Kind::Identifier: {
            auto* id = static_cast<claw::ast::IdentifierExpr*>(expr);
            std::cout << prefix << "Identifier: '" << id->get_name() << "'\n";
            break;
        }
        case claw::ast::Expression::Kind::Literal: {
            std::cout << prefix << "Literal\n";
            break;
        }
        case claw::ast::Expression::Kind::Index: {
            auto* idx = static_cast<claw::ast::IndexExpr*>(expr);
            std::cout << prefix << "IndexExpr:\n";
            std::cout << prefix << "  object:\n";
            print_expr(idx->get_object(), indent + 2);
            std::cout << prefix << "  index:\n";
            print_expr(idx->get_index(), indent + 2);
            break;
        }
        case claw::ast::Expression::Kind::Binary: {
            auto* bin = static_cast<claw::ast::BinaryExpr*>(expr);
            std::cout << prefix << "BinaryExpr\n";
            break;
        }
        default:
            std::cout << prefix << "Other expr kind=" << (int)expr->get_kind() << "\n";
    }
}

void print_stmt(claw::ast::Statement* stmt, int indent = 0) {
    std::string prefix(indent * 2, ' ');
    if (!stmt) {
        std::cout << prefix << "NULL stmt\n";
        return;
    }
    switch (stmt->get_kind()) {
        case claw::ast::Statement::Kind::Assign: {
            auto* ass = static_cast<claw::ast::AssignStmt*>(stmt);
            std::cout << prefix << "Assign:\n";
            std::cout << prefix << "  target:\n";
            print_expr(ass->get_target(), indent + 2);
            std::cout << prefix << "  value:\n";
            print_expr(ass->get_value(), indent + 2);
            break;
        }
        default:
            std::cout << prefix << "Other stmt kind=" << (int)stmt->get_kind() << "\n";
    }
}

int main() {
    std::string source = "a[1] = 42";
    
    claw::Lexer lexer(source, "test.claw");
    auto tokens = lexer.scan_all();
    
    claw::Parser parser(tokens);
    auto program = parser.parse();
    
    // Get first statement in first function if exists
    std::cout << "Declarations: " << program->get_declarations().size() << "\n";
    std::cout << "to_string: " << program->to_string() << "\n";
    
    if (!program->get_declarations().empty()) {
        auto* stmt = program->get_declarations()[0].get();
        std::cout << "\nParsed:\n";
        print_stmt(stmt, 0);
    }
    
    return 0;
}
