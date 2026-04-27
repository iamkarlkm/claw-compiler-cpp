#ifndef CLAW_AST_SERIALIZER_H
#define CLAW_AST_SERIALIZER_H

#include <string>
#include <memory>
#include <vector>
#include <variant>
#include <optional>
#include <map>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <functional>

namespace claw {
namespace ast {

// ============================================================================
// Forward Declarations
// ============================================================================

class Node;
class Expression;
class Statement;
class Program;
class FunctionStmt;
class LetStmt;
class AssignStmt;
class IfStmt;
class MatchStmt;
class ForStmt;
class WhileStmt;
class LoopStmt;
class ReturnStmt;
class BreakStmt;
class ContinueStmt;
class BlockStmt;
class PublishStmt;
class SubscribeStmt;
class ExprStmt;
class IntegerLiteral;
class FloatLiteral;
class StringLiteral;
class Identifier;
class BinaryExpr;
class UnaryExpr;
class CallExpr;
class IndexExpr;
class FieldExpr;
class ArrayExpr;
class TupleExpr;
class LambdaExpr;
class TensorLiteral;
class Type;

// ============================================================================
// Node Type Enum (mirrored from ast.h)
// ============================================================================

enum class NodeType {
    // Expressions
    INTEGER_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,
    IDENTIFIER,
    BINARY_EXPR,
    UNARY_EXPR,
    CALL_EXPR,
    INDEX_EXPR,
    FIELD_EXPR,
    ARRAY_EXPR,
    TUPLE_EXPR,
    LAMBDA_EXPR,
    TENSOR_LITERAL,
    
    // Statements
    EXPRESSION_STMT,
    LET_STMT,
    ASSIGN_STMT,
    IF_STMT,
    MATCH_STMT,
    FOR_STMT,
    WHILE_STMT,
    LOOP_STMT,
    RETURN_STMT,
    BREAK_STMT,
    CONTINUE_STMT,
    BLOCK,
    FUNCTION,
    SERIAL_PROCESS,
    PUBLISH_STMT,
    SUBSCRIBE_STMT,
    
    // Module
    MODULE
};

// ============================================================================
// JSON Serialization Options
// ============================================================================

struct SerializeOptions {
    bool pretty_print = true;
    int indent_size = 2;
    bool include_location = true;
    bool include_comments = false;
    bool compact_arrays = false;
    
    SerializeOptions() = default;
    SerializeOptions(bool pretty) : pretty_print(pretty) {}
};

// ============================================================================
// ASTSerializer - Main Serialization Class
// ============================================================================

class ASTSerializer {
public:
    explicit ASTSerializer(const SerializeOptions& opts = {});
    ~ASTSerializer() = default;
    
    // ========== JSON Serialization ==========
    std::string serialize(const Program& program);
    std::string serialize(const Statement& stmt);
    std::string serialize(const Expression& expr);
    
    // ========== JSON Deserialization (stub) ==========
    std::unique_ptr<Program> deserializeModule(const std::string& json);
    std::unique_ptr<Statement> deserializeStatement(const std::string& json);
    std::unique_ptr<Expression> deserializeExpression(const std::string& json);
    
    // ========== Utility Methods ==========
    std::string toJson(const Node& node);
    bool fromJson(const std::string& json, std::unique_ptr<Node>& node);
    
    // ========== Statistics ==========
    size_t getSerializedSize() const { return serialized_size_; }
    size_t getNodeCount() const { return node_count_; }
    void resetStats();
    
    // ========== Error Handling ==========
    const std::string& getLastError() const { return last_error_; }
    
private:
    SerializeOptions options_;
    size_t serialized_size_ = 0;
    size_t node_count_ = 0;
    int current_depth_ = 0;
    std::string last_error_;
    
    // ========== JSON Building Helpers ==========
    std::string indent();
    std::string quote(const std::string& s);
    std::string boolStr(bool b);
    
    // ========== Serialization Methods ==========
    std::string serializeNode(const Node& node);
    std::string serializeProgram(const Program& program);
    std::string serializeFunction(const FunctionStmt& func);
    std::string serializeLetStmt(const LetStmt& let);
    std::string serializeAssignStmt(const AssignStmt& assign);
    std::string serializeIfStmt(const IfStmt& ifStmt);
    std::string serializeMatchStmt(const MatchStmt& match);
    std::string serializeForStmt(const ForStmt& forStmt);
    std::string serializeWhileStmt(const WhileStmt& whileStmt);
    std::string serializeLoopStmt(const LoopStmt& loop);
    std::string serializeReturnStmt(const ReturnStmt& ret);
    std::string serializeBreakStmt(const BreakStmt& brk);
    std::string serializeContinueStmt(const ContinueStmt& cont);
    std::string serializeBlock(const BlockStmt& block);
    std::string serializePublishStmt(const PublishStmt& pub);
    std::string serializeSubscribeStmt(const SubscribeStmt& sub);
    std::string serializeExpressionStmt(const ExprStmt& exprStmt);
    
    std::string serializeExpr(const Expression& expr);
    std::string serializeIntegerLiteral(const IntegerLiteral& lit);
    std::string serializeFloatLiteral(const FloatLiteral& lit);
    std::string serializeStringLiteral(const StringLiteral& lit);
    std::string serializeIdentifier(const Identifier& ident);
    std::string serializeBinaryExpr(const BinaryExpr& expr);
    std::string serializeUnaryExpr(const UnaryExpr& expr);
    std::string serializeCallExpr(const CallExpr& call);
    std::string serializeIndexExpr(const IndexExpr& index);
    std::string serializeFieldExpr(const FieldExpr& field);
    std::string serializeArrayExpr(const ArrayExpr& arr);
    std::string serializeTupleExpr(const TupleExpr& tuple);
    std::string serializeLambdaExpr(const LambdaExpr& lambda);
    std::string serializeTensorLiteral(const TensorLiteral& tensor);
    
    std::string serializeLocation(const SourceLocation& loc);
    std::string serializeType(const Type& type);
};

// ============================================================================
// Stream Operators
// ============================================================================

inline std::ostream& operator<<(std::ostream& os, const ASTSerializer& serializer) {
    return os;
}

// ============================================================================
// Convenience Functions
// ============================================================================

inline std::string programToJson(const Program& program, bool pretty = true) {
    SerializeOptions opts(pretty);
    ASTSerializer serializer(opts);
    return serializer.serialize(program);
}

inline std::unique_ptr<Program> programFromJson(const std::string& json) {
    SerializeOptions opts;
    ASTSerializer serializer(opts);
    return serializer.deserializeModule(json);
}

} // namespace ast
} // namespace claw

#endif // CLAW_AST_SERIALIZER_H
