// AST Compatibility Layer - Bridge between new AST API and old code
#ifndef CLAW_AST_COMPAT_H
#define CLAW_AST_COMPAT_H

#include "ast.h"

namespace claw {
namespace ast {

// 提供旧API兼容函数
inline Statement::Kind Statement::type() const { return get_kind(); }
inline Expression::Kind Expression::type() const { return get_kind(); }

// NodeType 枚举兼容 - 映射到 Kind
enum class NodeType {
    // Statements
    Let = 1,
    Const,
    Assign,
    If,
    Match,
    For,
    While,
    Loop,
    Return,
    Break,
    Continue,
    Block,
    Expression,
    Publish,
    Subscribe,
    // Expressions
    Literal = 100,
    Identifier,
    Binary,
    Unary,
    Call,
    Index,
    Slice,
    Tuple,
    Array,
    Member,
    Lambda,
    Function,
};

// 全局 NodeType 映射函数
inline NodeType kind_to_node_type(Statement::Kind k) {
    switch (k) {
        case Statement::Kind::Let: return NodeType::Let;
        case Statement::Kind::Const: return NodeType::Const;
        case Statement::Kind::Assign: return NodeType::Assign;
        case Statement::Kind::If: return NodeType::If;
        case Statement::Kind::Match: return NodeType::Match;
        case Statement::Kind::For: return NodeType::For;
        case Statement::Kind::While: return NodeType::While;
        case Statement::Kind::Loop: return NodeType::Loop;
        case Statement::Kind::Return: return NodeType::Return;
        case Statement::Kind::Break: return NodeType::Break;
        case Statement::Kind::Continue: return NodeType::Continue;
        case Statement::Kind::Block: return NodeType::Block;
        case Statement::Kind::Expression: return NodeType::Expression;
        case Statement::Kind::Publish: return NodeType::Publish;
        case Statement::Kind::Subscribe: return NodeType::Subscribe;
        default: return NodeType::Expression;
    }
}

inline NodeType kind_to_node_type(Expression::Kind k) {
    switch (k) {
        case Expression::Kind::Literal: return NodeType::Literal;
        case Expression::Kind::Identifier: return NodeType::Identifier;
        case Expression::Kind::Binary: return NodeType::Binary;
        case Expression::Kind::Unary: return NodeType::Unary;
        case Expression::Kind::Call: return NodeType::Call;
        case Expression::Kind::Index: return NodeType::Index;
        case Expression::Kind::Slice: return NodeType::Slice;
        case Expression::Kind::Tuple: return NodeType::Tuple;
        case Expression::Kind::Array: return NodeType::Array;
        case Expression::Kind::Member: return NodeType::Member;
        case Expression::Kind::Lambda: return NodeType::Lambda;
        default: return NodeType::Identifier;
    }
}

// Program 兼容属性
struct ModuleCompat {
    std::string name;
    std::vector<std::unique_ptr<Statement>> statements;
};

// 转换函数
inline ModuleCompat program_to_module(const Program& prog) {
    ModuleCompat mod;
    mod.name = "main";
    for (auto& decl : prog.get_declarations()) {
        mod.statements.push_back(std::move(decl));
    }
    return mod;
}

} // namespace ast
} // namespace claw

#endif // CLAW_AST_COMPAT_H
