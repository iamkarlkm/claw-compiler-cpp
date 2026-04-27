#include "ast_serializer.h"
#include "ast.h"
#include "common/common.h"
#include <cmath>

using namespace claw::ast;

namespace claw {
namespace ast {

// ============================================================================
// ASTSerializer Implementation
// ============================================================================

ASTSerializer::ASTSerializer(const SerializeOptions& opts) : options_(opts) {}

void ASTSerializer::resetStats() {
    serialized_size_ = 0;
    node_count_ = 0;
    current_depth_ = 0;
}

// ============================================================================
// JSON Building Helpers
// ============================================================================

std::string ASTSerializer::indent() {
    if (!options_.pretty_print) return "";
    return std::string(current_depth_ * options_.indent_size, ' ');
}

std::string ASTSerializer::quote(const std::string& s) {
    std::string result = "\"";
    for (char c : s) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    result += "\"";
    return result;
}

std::string ASTSerializer::boolStr(bool b) {
    return b ? "true" : "false";
}

// ============================================================================
// Main Serialization Entry Points
// ============================================================================

std::string ASTSerializer::serialize(const Program& program) {
    resetStats();
    return serializeProgram(program);
}

std::string ASTSerializer::serialize(const Statement& stmt) {
    return serializeStatement(stmt);
}

std::string ASTSerializer::serialize(const Expression& expr) {
    return serializeExpr(expr);
}

std::string ASTSerializer::toJson(const Node& node) {
    resetStats();
    return serializeNode(node);
}

bool ASTSerializer::fromJson(const std::string& json, std::unique_ptr<Node>& node) {
    try {
        node = deserializeModule(json);
        return node != nullptr;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return false;
    }
}

// ============================================================================
// Serialization Methods - Module
// ============================================================================

std::string ASTSerializer::serializeProgram(const Program& program) {
    node_count_++;
    std::ostringstream oss;
    
    oss << "{\n";
    current_depth_++;
    
    // Program type
    oss << indent() << quote("type") << ": " << quote("program") << ",\n";
    
    // Location
    if (options_.include_location) {
        oss << indent() << quote("location") << ": " << serializeLocation(program.location) << ",\n";
    }
    
    // Statements
    oss << indent() << quote("statements") << ": [\n";
    current_depth_++;
    
    for (size_t i = 0; i < program.statements.size(); i++) {
        oss << indent() << serializeStatement(*program.statements[i]);
        if (i < program.statements.size() - 1) oss << ",";
        oss << "\n";
    }
    
    current_depth_--;
    oss << indent() << "]\n";
    
    current_depth_--;
    oss << indent() << "}";
    
    serialized_size_ = oss.str().size();
    return oss.str();
}

// ============================================================================
// Serialization Methods - Statements
// ============================================================================

std::string ASTSerializer::serializeStatement(const Statement& stmt) {
    node_count_++;
    
    switch (stmt.type()) {
        case NodeType::FUNCTION:
            return serializeFunction(static_cast<const FunctionStmt&>(stmt));
        case NodeType::LET:
            return serializeLetStmt(static_cast<const LetStmt&>(stmt));
        case NodeType::ASSIGN:
            return serializeAssignStmt(static_cast<const AssignStmt&>(stmt));
        case NodeType::IF:
            return serializeIfStmt(static_cast<const IfStmt&>(stmt));
        case NodeType::MATCH:
            return serializeMatchStmt(static_cast<const MatchStmt&>(stmt));
        case NodeType::FOR:
            return serializeForStmt(static_cast<const ForStmt&>(stmt));
        case NodeType::WHILE:
            return serializeWhileStmt(static_cast<const WhileStmt&>(stmt));
        case NodeType::LOOP:
            return serializeLoopStmt(static_cast<const LoopStmt&>(stmt));
        case NodeType::RETURN:
            return serializeReturnStmt(static_cast<const ReturnStmt&>(stmt));
        case NodeType::BREAK:
            return serializeBreakStmt(static_cast<const BreakStmt&>(stmt));
        case NodeType::CONTINUE:
            return serializeContinueStmt(static_cast<const ContinueStmt&>(stmt));
        case NodeType::BLOCK:
            return serializeBlock(static_cast<const BlockStmt&>(stmt));
        case NodeType::SERIAL_PROCESS:
            return serializeFunction(static_cast<const FunctionStmt&>(stmt));
        case NodeType::PUBLISH_STMT:
            return serializePublishStmt(static_cast<const PublishStmt&>(stmt));
        case NodeType::SUBSCRIBE_STMT:
            return serializeSubscribeStmt(static_cast<const SubscribeStmt&>(stmt));
        case NodeType::EXPRESSION_STMT:
            return serializeExpressionStmt(static_cast<const ExprStmt&>(stmt));
        default:
            return quote("unknown_statement");
    }
}

std::string ASTSerializer::serializeFunction(const FunctionStmt& func) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("function") << ",\n";
    oss << indent() << quote("name") << ": " << quote(func.name) << ",\n";
    
    // Parameters
    oss << indent() << quote("params") << ": [";
    for (size_t i = 0; i < func.params.size(); i++) {
        oss << "{" << quote("name") << ": " << quote(func.params[i].name);
        if (func.params[i].type) {
            oss << ", " << quote("type") << ": " << serializeType(*func.params[i].type);
        }
        oss << "}";
        if (i < func.params.size() - 1) oss << ", ";
    }
    oss << "],\n";
    
    // Return type
    if (func.returnType) {
        oss << indent() << quote("returnType") << ": " << serializeType(*func.returnType) << ",\n";
    }
    
    // Body
    oss << indent() << quote("body") << ": " << serializeBlock(func.body) << ",\n";
    
    // Location
    if (options_.include_location) {
        oss << indent() << quote("location") << ": " << serializeLocation(func.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeLetStmt(const LetStmt& let) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("let") << ",\n";
    oss << indent() << quote("name") << ": " << quote(let.name) << ",\n";
    
    if (let.varType) {
        oss << indent() << quote("varType") << ": " << serializeType(*let.varType) << ",\n";
    }
    
    if (let.initializer) {
        oss << indent() << quote("initializer") << ": " << serializeExpr(*let.initializer);
    }
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(let.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeAssignStmt(const AssignStmt& assign) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("assign") << ",\n";
    oss << indent() << quote("target") << ": " << serializeExpr(*assign.target) << ",\n";
    oss << indent() << quote("value") << ": " << serializeExpr(*assign.value);
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(assign.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeIfStmt(const IfStmt& ifStmt) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("if") << ",\n";
    oss << indent() << quote("condition") << ": " << serializeExpr(*ifStmt.condition) << ",\n";
    oss << indent() << quote("thenBranch") << ": " << serializeBlock(ifStmt.thenBranch) << ",\n";
    
    if (!ifStmt.elseBranch.empty()) {
        oss << indent() << quote("elseBranch") << ": [";
        for (size_t i = 0; i < ifStmt.elseBranch.size(); i++) {
            oss << serializeStatement(*ifStmt.elseBranch[i]);
            if (i < ifStmt.elseBranch.size() - 1) oss << ", ";
        }
        oss << "]";
    }
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(ifStmt.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeMatchStmt(const MatchStmt& match) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("match") << ",\n";
    oss << indent() << quote("target") << ": " << serializeExpr(*match.target) << ",\n";
    
    oss << indent() << quote("cases") << ": [";
    for (size_t i = 0; i < match.cases.size(); i++) {
        oss << "{\n";
        current_depth_++;
        oss << indent() << quote("pattern") << ": " << serializeExpr(*match.cases[i].pattern) << ",\n";
        oss << indent() << quote("body") << ": " << serializeBlock(match.cases[i].body);
        current_depth_--;
        oss << "\n" << indent() << "}";
        if (i < match.cases.size() - 1) oss << ", ";
    }
    oss << "]";
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(match.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeForStmt(const ForStmt& forStmt) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("for") << ",\n";
    oss << indent() << quote("variable") << ": " << quote(forStmt.variable) << ",\n";
    oss << indent() << quote("iterable") << ": " << serializeExpr(*forStmt.iterable) << ",\n";
    oss << indent() << quote("body") << ": " << serializeBlock(forStmt.body);
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(forStmt.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeWhileStmt(const WhileStmt& whileStmt) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("while") << ",\n";
    oss << indent() << quote("condition") << ": " << serializeExpr(*whileStmt.condition) << ",\n";
    oss << indent() << quote("body") << ": " << serializeBlock(whileStmt.body);
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(whileStmt.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeLoopStmt(const LoopStmt& loop) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("loop") << ",\n";
    oss << indent() << quote("body") << ": " << serializeBlock(loop.body);
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(loop.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeReturnStmt(const ReturnStmt& ret) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("return") << ",\n";
    
    if (ret.value) {
        oss << indent() << quote("value") << ": " << serializeExpr(*ret.value);
    } else {
        oss << indent() << quote("value") << ": null";
    }
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(ret.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeBreakStmt(const BreakStmt& brk) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("break");
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(brk.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeContinueStmt(const ContinueStmt& cont) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("continue");
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(cont.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeBlock(const BlockStmt& block) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("block") << ",\n";
    oss << indent() << quote("statements") << ": [";
    
    for (size_t i = 0; i < block.statements.size(); i++) {
        oss << serializeStatement(*block.statements[i]);
        if (i < block.statements.size() - 1) oss << ", ";
    }
    
    oss << "]";
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(block.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializePublishStmt(const PublishStmt& pub) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("publish") << ",\n";
    oss << indent() << quote("channel") << ": " << quote(pub.channel) << ",\n";
    oss << indent() << quote("message") << ": " << serializeExpr(*pub.message);
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(pub.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeSubscribeStmt(const SubscribeStmt& sub) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("subscribe") << ",\n";
    oss << indent() << quote("channel") << ": " << quote(sub.channel) << ",\n";
    oss << indent() << quote("handler") << ": " << serializeBlock(sub.handler);
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(sub.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeExpressionStmt(const ExprStmt& exprStmt) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("expression") << ",\n";
    oss << indent() << quote("expression") << ": " << serializeExpr(*exprStmt.expression);
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(exprStmt.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

// ============================================================================
// Serialization Methods - Expressions
// ============================================================================

std::string ASTSerializer::serializeExpr(const Expression& expr) {
    node_count_++;
    
    switch (expr.type()) {
        case NodeType::INTEGER_LITERAL:
            return serializeIntegerLiteral(static_cast<const IntegerLiteral&>(expr));
        case NodeType::FLOAT_LITERAL:
            return serializeFloatLiteral(static_cast<const FloatLiteral&>(expr));
        case NodeType::STRING_LITERAL:
            return serializeStringLiteral(static_cast<const StringLiteral&>(expr));
        case NodeType::IDENTIFIER:
            return serializeIdentifier(static_cast<const Identifier&>(expr));
        case NodeType::BINARY_EXPR:
            return serializeBinaryExpr(static_cast<const BinaryExpr&>(expr));
        case NodeType::UNARY_EXPR:
            return serializeUnaryExpr(static_cast<const UnaryExpr&>(expr));
        case NodeType::CALL_EXPR:
            return serializeCallExpr(static_cast<const CallExpr&>(expr));
        case NodeType::INDEX_EXPR:
            return serializeIndexExpr(static_cast<const IndexExpr&>(expr));
        case NodeType::FIELD_EXPR:
            return serializeFieldExpr(static_cast<const FieldExpr&>(expr));
        case NodeType::ARRAY_EXPR:
            return serializeArrayExpr(static_cast<const ArrayExpr&>(expr));
        case NodeType::TUPLE_EXPR:
            return serializeTupleExpr(static_cast<const TupleExpr&>(expr));
        case NodeType::LAMBDA_EXPR:
            return serializeLambdaExpr(static_cast<const LambdaExpr&>(expr));
        case NodeType::TENSOR_LITERAL:
            return serializeTensorLiteral(static_cast<const TensorLiteral&>(expr));
        default:
            return quote("unknown_expression");
    }
}

std::string ASTSerializer::serializeIntegerLiteral(const IntegerLiteral& lit) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("integer") << ",\n";
    oss << indent() << quote("value") << ": " << lit.value;
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(lit.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeFloatLiteral(const FloatLiteral& lit) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("float") << ",\n";
    
    // Special handling for floating point numbers
    std::ostringstream val_oss;
    val_oss << std::fixed << std::setprecision(6) << lit.value;
    oss << indent() << quote("value") << ": " << val_oss.str();
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(lit.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeStringLiteral(const StringLiteral& lit) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("string") << ",\n";
    oss << indent() << quote("value") << ": " << quote(lit.value);
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(lit.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeIdentifier(const Identifier& ident) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("identifier") << ",\n";
    oss << indent() << quote("name") << ": " << quote(ident.name);
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(ident.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeBinaryExpr(const BinaryExpr& expr) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("binary") << ",\n";
    oss << indent() << quote("operator") << ": " << quote(expr.op) << ",\n";
    oss << indent() << quote("left") << ": " << serializeExpr(*expr.left) << ",\n";
    oss << indent() << quote("right") << ": " << serializeExpr(*expr.right);
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(expr.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeUnaryExpr(const UnaryExpr& expr) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("unary") << ",\n";
    oss << indent() << quote("operator") << ": " << quote(expr.op) << ",\n";
    oss << indent() << quote("operand") << ": " << serializeExpr(*expr.operand);
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(expr.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeCallExpr(const CallExpr& call) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("call") << ",\n";
    oss << indent() << quote("callee") << ": " << serializeExpr(*call.callee) << ",\n";
    
    oss << indent() << quote("arguments") << ": [";
    for (size_t i = 0; i < call.arguments.size(); i++) {
        oss << serializeExpr(*call.arguments[i]);
        if (i < call.arguments.size() - 1) oss << ", ";
    }
    oss << "]";
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(call.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeIndexExpr(const IndexExpr& index) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("index") << ",\n";
    oss << indent() << quote("object") << ": " << serializeExpr(*index.object) << ",\n";
    oss << indent() << quote("index") << ": " << serializeExpr(*index.index);
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(index.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeFieldExpr(const FieldExpr& field) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("field") << ",\n";
    oss << indent() << quote("object") << ": " << serializeExpr(*field.object) << ",\n";
    oss << indent() << quote("field") << ": " << quote(field.field);
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(field.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeArrayExpr(const ArrayExpr& arr) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("array") << ",\n";
    
    oss << indent() << quote("elements") << ": [";
    for (size_t i = 0; i < arr.elements.size(); i++) {
        oss << serializeExpr(*arr.elements[i]);
        if (i < arr.elements.size() - 1) oss << ", ";
    }
    oss << "]";
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(arr.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeTupleExpr(const TupleExpr& tuple) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("tuple") << ",\n";
    
    oss << indent() << quote("elements") << ": [";
    for (size_t i = 0; i < tuple.elements.size(); i++) {
        oss << serializeExpr(*tuple.elements[i]);
        if (i < tuple.elements.size() - 1) oss << ", ";
    }
    oss << "]";
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(tuple.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeLambdaExpr(const LambdaExpr& lambda) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("lambda") << ",\n";
    
    // Parameters
    oss << indent() << quote("params") << ": [";
    for (size_t i = 0; i < lambda.params.size(); i++) {
        oss << quote(lambda.params[i]);
        if (i < lambda.params.size() - 1) oss << ", ";
    }
    oss << "],\n";
    
    // Body
    oss << indent() << quote("body") << ": " << serializeBlock(lambda.body);
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(lambda.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeTensorLiteral(const TensorLiteral& tensor) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("type") << ": " << quote("tensor") << ",\n";
    
    // Shape
    oss << indent() << quote("shape") << ": [";
    for (size_t i = 0; i < tensor.shape.size(); i++) {
        oss << tensor.shape[i];
        if (i < tensor.shape.size() - 1) oss << ", ";
    }
    oss << "],\n";
    
    // Elements
    oss << indent() << quote("elements") << ": [";
    for (size_t i = 0; i < tensor.elements.size(); i++) {
        oss << serializeExpr(*tensor.elements[i]);
        if (i < tensor.elements.size() - 1) oss << ", ";
    }
    oss << "]";
    
    if (options_.include_location) {
        oss << ",\n" << indent() << quote("location") << ": " << serializeLocation(tensor.location);
    }
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

// ============================================================================
// Location and Type Serialization
// ============================================================================

std::string ASTSerializer::serializeLocation(const SourceLocation& loc) {
    std::ostringstream oss;
    oss << "{\n";
    current_depth_++;
    
    oss << indent() << quote("file") << ": " << quote(loc.file) << ",\n";
    oss << indent() << quote("line") << ": " << loc.line << ",\n";
    oss << indent() << quote("column") << ": " << loc.column << ",\n";
    oss << indent() << quote("offset") << ": " << loc.offset;
    
    current_depth_--;
    oss << "\n" << indent() << "}";
    
    return oss.str();
}

std::string ASTSerializer::serializeType(const Type& type) {
    std::ostringstream oss;
    oss << quote(type.toString());
    return oss.str();
}

std::string ASTSerializer::serializeNode(const Node& node) {
    if (node.type() == NodeType::MODULE) {
        return serialize(static_cast<const Module&>(node));
    }
    return serializeStatement(static_cast<const Statement&>(node));
}

// ============================================================================
// Deserialization Methods (Stub)
// ============================================================================

std::unique_ptr<Program> ASTSerializer::deserializeModule(const std::string& json) {
    last_error_ = "Deserialization not yet implemented";
    return nullptr;
}

std::unique_ptr<Statement> ASTSerializer::deserializeStatement(const std::string& json) {
    last_error_ = "Deserialization not yet implemented";
    return nullptr;
}

std::unique_ptr<Expression> ASTSerializer::deserializeExpression(const std::string& json) {
    last_error_ = "Deserialization not yet implemented";
    return nullptr;
}

} // namespace ast
} // namespace claw
