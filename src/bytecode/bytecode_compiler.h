#ifndef CLAW_BYTECODE_COMPILER_H
#define CLAW_BYTECODE_COMPILER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <stack>
#include <memory>
#include "../ast/ast.h"
#include "bytecode.h"
#include "../vm/claw_vm.h"

namespace claw {

// 类型别名
using BytecodeModule = bytecode::Module;

/**
 * @brief 字节码编译器 - AST → Bytecode (API兼容版本)
 */
class BytecodeCompiler {
public:
    BytecodeCompiler();
    ~BytecodeCompiler();
    
    /**
     * @brief 编译 AST 程序到字节码
     */
    std::shared_ptr<BytecodeModule> compile(const ast::Program& module);
    
    /**
     * @brief 获取编译错误
     */
    const std::string& getLastError() const { return lastError_; }
    
    /**
     * @brief 是否启用调试信息
     */
    void setDebugInfo(bool enable) { debugInfo_ = enable; }

private:
    // 内部类型别名
    using Stmt = ast::Statement;
    using Expr = ast::Expression;
    
    // 循环上下文
    struct LoopContext {
        int breakJumpIdx;
        int continueJumpIdx;
        int scopeDepth;
    };
    
    // 跳转修补信息
    struct JumpPatch {
        int jumpInstIdx;
        int targetIdx;
        bool isForward;
    };
    
    // 编译上下文
    struct CompilationContext {
        std::shared_ptr<bytecode::Function> currentFunction;
        std::vector<std::unordered_map<std::string, int>> scopeStack;
        std::vector<std::string> upvalues;
        std::vector<LoopContext> loopStack;
        std::vector<JumpPatch> pendingJumps;
        int scopeDepth = 0;
        bool isClosure = false;
        int nextSlot = 0;  // Flat slot counter across all scopes in function
    };
    
    // 编译方法
    void compileModule(const ast::Program& module);
    void compileFunction(const ast::FunctionStmt& func);
    void compileStatement(const Stmt& stmt);
    void compileExpression(const Expr& expr);
    
    // 语句编译
    void compileLetStmt(const ast::LetStmt& stmt);
    void compileAssignStmt(const ast::AssignStmt& stmt);
    void compileIfStmt(const ast::IfStmt& stmt);
    void compileMatchStmt(const ast::MatchStmt& stmt);
    void compileForStmt(const ast::ForStmt& stmt);
    void compileWhileStmt(const ast::WhileStmt& stmt);
    void compileReturnStmt(const ast::ReturnStmt& stmt);
    void compileBreakStmt(const ast::BreakStmt& stmt);
    void compileContinueStmt(const ast::ContinueStmt& stmt);
    void compileBlockStmt(const ast::BlockStmt& block);
    void compileExprStmt(const ast::ExprStmt& stmt);
    void compilePublishStmt(const ast::PublishStmt& stmt);
    void compileSubscribeStmt(const ast::SubscribeStmt& stmt);
    
    // 表达式编译
    void compileLiteralExpr(const ast::LiteralExpr& expr);
    void compileIdentifierExpr(const ast::IdentifierExpr& expr);
    void compileBinaryExpr(const ast::BinaryExpr& expr);
    void compileUnaryExpr(const ast::UnaryExpr& expr);
    void compileCallExpr(const ast::CallExpr& expr);
    void compileIndexExpr(const ast::IndexExpr& expr);
    void compileFieldExpr(const ast::MemberExpr& expr);
    void compileArrayExpr(const ast::ArrayExpr& expr);
    void compileTupleExpr(const ast::TupleExpr& expr);
    void compileLambdaExpr(const ast::LambdaExpr& expr);
    
    // 作用域管理
    void enterScope();
    void exitScope();
    int resolveVariable(const std::string& name);
    int allocateLocal(const std::string& name);
    
    // 指令生成辅助
    void emitOp(bytecode::OpCode op);
    void emitOp1(bytecode::OpCode op, int operand);
    void emitOp2(bytecode::OpCode op, int operand1, int operand2);
    void emitOpF(bytecode::OpCode op, double operand);
    void emitOpS(bytecode::OpCode op, const std::string& operand);
    void emitJump(bytecode::OpCode op);
    void patchJump(int jumpIdx, int targetIdx);
    void emitLoadLocal(int slot);
    void emitStoreLocal(int slot);
    void emitLoadGlobal(const std::string& name);
    void emitStoreGlobal(const std::string& name);
    void emitConst(int value);
    void emitConst(double value);
    void emitConst(const std::string& value);
    
    // 常量池
    int addConstant(const bytecode::Value& value);
    int findOrAddString(const std::string& s);
    
    // 错误处理
    void error(const std::string& msg);
    void errorf(const char* fmt, ...);
    
    // 成员变量
    std::shared_ptr<BytecodeModule> module_;
    std::unique_ptr<CompilationContext> ctx_;
    std::string lastError_;
    bool debugInfo_ = false;
    int nextGlobalSlot_ = 0;
    std::unordered_map<std::string, int> globalVars_;
};

} // namespace claw

#endif // CLAW_BYTECODE_COMPILER_H
