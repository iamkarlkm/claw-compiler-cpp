// bytecode_compiler_simple.h - Simplified Bytecode Compiler
// Provides AST → Bytecode compilation for ExecutionPipeline

#ifndef CLAW_BYTECODE_COMPILER_SIMPLE_H
#define CLAW_BYTECODE_COMPILER_SIMPLE_H

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "../ast/ast.h"
#include "bytecode.h"

namespace claw {

/**
 * @brief Simplified Bytecode Compiler - AST to Bytecode
 * 
 * A clean implementation that compiles Claw AST to executable bytecode.
 * Supports: functions, variables, control flow, expressions, closures.
 */
class SimpleBytecodeCompiler {
public:
    SimpleBytecodeCompiler();
    ~SimpleBytecodeCompiler();
    
    /**
     * @brief Compile AST program to bytecode module
     * @param ast AST program to compile
     * @return Compiled bytecode module (owned by caller)
     */
    std::unique_ptr<bytecode::Module> compile(std::shared_ptr<ast::Program> ast);
    
    /**
     * @brief Get last error message
     */
    const std::string& getLastError() const { return lastError_; }
    
    /**
     * @brief Enable debug info
     */
    void setDebugInfo(bool enable) { debugInfo_ = enable; }

private:
    // Compilation context
    struct CompileContext {
        bytecode::Function* currentFunction = nullptr;
        std::vector<std::unordered_map<std::string, int>> scopeStack;  // variable -> slot
        std::vector<int> loopStartStack;   // loop start instruction index
        std::vector<std::vector<int>> loopBreakStack;  // break jump positions
        std::vector<std::vector<int>> loopContinueStack;  // continue jump positions
        int localSlotCounter = 0;
        bool inLoop = false;
    };
    
    // State
    std::unique_ptr<bytecode::Module> module_;
    std::unique_ptr<CompileContext> ctx_;
    std::string lastError_;
    bool debugInfo_ = false;
    
    // Compilation methods - Program level
    void compileProgram(const ast::Program& program);
    void compileFunction(const ast::FunctionStmt& func);
    
    // Statement compilation
    void compileStatement(const ast::Statement& stmt);
    void compileLet(const ast::LetStmt& stmt);
    void compileAssign(const ast::AssignStmt& stmt);
    void compileIf(const ast::IfStmt& stmt);
    void compileWhile(const ast::WhileStmt& stmt);
    void compileFor(const ast::ForStmt& stmt);
    void compileLoop(const ast::WhileStmt& stmt);
    void compileReturn(const ast::ReturnStmt& stmt);
    void compileBreak(const ast::BreakStmt& stmt);
    void compileContinue(const ast::ContinueStmt& stmt);
    void compileBlock(const ast::BlockStmt& block);
    void compileExprStmt(const ast::ExprStmt& stmt);
    
    // Expression compilation
    void compileExpression(const ast::Expression& expr);
    void compileLiteral(const ast::LiteralExpr& expr);
    void compileIdentifier(const ast::IdentifierExpr& expr);
    void compileBinary(const ast::BinaryExpr& expr);
    void compileUnary(const ast::UnaryExpr& expr);
    void compileCall(const ast::CallExpr& expr);
    void compileIndex(const ast::IndexExpr& expr);
    void compileArray(const ast::ArrayExpr& expr);
    void compileTuple(const ast::TupleExpr& expr);
    void compileLambda(const ast::LambdaExpr& expr);
    
    // Scope management
    void enterScope();
    void exitScope();
    int resolveVariable(const std::string& name);
    int allocateLocal(const std::string& name);
    
    // Instruction emission helpers
    void emit(bytecode::OpCode op);
    void emitI(bytecode::OpCode op, int32_t operand);
    void emitS(bytecode::OpCode op, const std::string& operand);
    void emitF(bytecode::OpCode op, double operand);
    int emitJump(bytecode::OpCode op);  // returns instruction index
    void patchJump(int jumpIdx, int targetIdx);
    
    // Constant pool
    int addConstant(const bytecode::Value& value);
    
    // Error handling
    void setError(const std::string& msg);
    
    // Utility
    bytecode::ValueType inferValueType(const ast::Expression& expr);
};

} // namespace claw

#endif // CLAW_BYTECODE_COMPILER_SIMPLE_H
