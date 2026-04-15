#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "common/common.h"
#include "ast/ast.h"

std::string read_file(const std::string& path) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void print_ast(claw::ast::ASTNode* node, int indent = 0) {
    std::string prefix(indent * 2, ' ');
    
    if (!node) {
        std::cout << prefix << "NULL\n";
        return;
    }
    
    // Try to cast to statement
    auto* stmt = dynamic_cast<claw::ast::Statement*>(node);
    if (stmt) {
        std::cout << prefix << "Statement kind: " << (int)stmt->get_kind() << "\n";
        
        if (stmt->get_kind() == claw::ast::Statement::Kind::Function) {
            auto* fn = static_cast<claw::ast::FunctionStmt*>(node);
            std::cout << prefix << "  Function name: '" << fn->get_name() << "'\n";
            std::cout << prefix << "  Body ptr: " << fn->get_body() << "\n";
            if (fn->get_body()) {
                print_ast(fn->get_body(), indent + 2);
            }
            return;
        }
        
        if (stmt->get_kind() == claw::ast::Statement::Kind::Block) {
            auto* block = static_cast<claw::ast::BlockStmt*>(node);
            std::cout << prefix << "  Block statements:\n";
            for (const auto& s : block->get_statements()) {
                print_ast(s.get(), indent + 2);
            }
            return;
        }
        
        std::cout << prefix << "  (other statement)\n";
        return;
    }
    
    std::cout << prefix << "Other node\n";
}

int main() {
    std::string source = read_file("calculator.claw");
    claw::DiagnosticReporter reporter;
    
    claw::Lexer lexer(source, "calculator.claw");
    lexer.set_reporter(&reporter);
    auto tokens = lexer.scan_all();
    
    claw::Parser parser(tokens);
    parser.set_reporter(&reporter);
    
    auto program = parser.parse();
    
    if (reporter.has_errors()) {
        reporter.print_diagnostics();
        return 1;
    }
    
    std::cout << "Declarations count: " << program->get_declarations().size() << "\n";
    
    for (const auto& decl : program->get_declarations()) {
        print_ast(decl.get(), 0);
        std::cout << "---\n";
    }
    
    return 0;
}
