// ast/ast.cpp - AST node implementations requiring complete types

#include "ast.h"
#include "pattern.h"

namespace claw {
namespace ast {

MatchStmt::MatchStmt(std::unique_ptr<Expression> expr, const SourceSpan& span)
    : Statement(Kind::Match, span), expr_(std::move(expr)) {}

MatchStmt::~MatchStmt() = default;

void MatchStmt::add_case(std::unique_ptr<Pattern> pattern, std::unique_ptr<ASTNode> body) {
    patterns_.push_back(std::move(pattern));
    bodies_.push_back(std::move(body));
}

} // namespace ast
} // namespace claw
