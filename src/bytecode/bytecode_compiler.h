#ifndef CLAW_BYTECODE_COMPILER_H
#define CLAW_BYTECODE_COMPILER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <stack>
#include <memory>
#include "../ast/ast.h"
#include "bytecode.h"

namespace claw {

/**
 * @brief 字节码编译器 - AST → Bytecode
 * 
 * 将 Claw AST 转换为可执行的字节码序列
 */
class BytecodeCompiler {
public:
    BytecodeCompiler();
    ~BytecodeCompiler();
    
    /**
     * @brief 编译 AST 模块到字节码
     * @param module AST 模块节点
     * @return 编译后的字节码模块
     */
    std::shared_ptr<BytecodeModule> compile(const ast::Module& module);
    
    /**
     * @brief 获取编译错误
     */
    const std::string& getLastError() const { return lastError_; }
    
    /**
     * @brief 是否启用调试信息
     */
    void setDebugInfo(bool enable) { debugInfo_ = enable; }

private:
    // 编译上下文
    struct CompilationContext {
        // 当前编译的函数
        std::shared_ptr<BytecodeFunction> currentFunction;
        
        // 作用域栈 - 变量名 -> 槽位索引
        std::vector<std::unordered_map<std::string, int>> scopeStack;
        
        // Upvalue 列表
        std::vector<std::string> upvalues;
        
        // 循环栈 (用于 break/continue)
        std::vector<LoopContext> loopStack;
        
        // 待回填的跳转指令
        std::vector<JumpPatch> pendingJumps;
        
        // 当前作用域深度
        int scopeDepth = 0;
        
        // 函数是否为闭包
        bool isClosure = false;
        
        // 当前函数类型
        enum FunctionType { 
            FT_NORMAL,    // 普通函数
            FT_METHOD,    // 方法
            FT_CONSTRUCTOR // 构造函数
        } functionType = FT_NORMAL;
    };
    
    struct LoopContext {
        int breakJumpIdx;      // break 指令索引
        int continueJumpIdx;   // continue 指令索引
        int scopeDepth;        // 循环所在作用域深度
    };
    
    struct JumpPatch {
        int jumpInstIdx;       // 跳转指令索引
        int targetIdx;         // 目标位置
        bool isForward;        // 是否是前向跳转
    };

    // 编译方法
    void compileModule(const ast::Module& module);
    void compileFunction(const ast::FunctionStmt& func);
    void compileStatement(const ast::Stmt& stmt);
    void compileExpression(const ast::Expr& expr);
    
    // 语句编译
    void compileLetStmt(const ast::LetStmt& stmt);
    void compileAssignStmt(const ast::AssignStmt& stmt);
    void compileIfStmt(const ast::IfStmt& stmt);
    void compileMatchStmt(const ast::MatchStmt& stmt);
    void compileForStmt(const ast::ForStmt& stmt);
    void compileWhileStmt(const ast::WhileStmt& stmt);
    void compileLoopStmt(const ast::LoopStmt& stmt);
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
    void compileFieldExpr(const ast::FieldExpr& expr);
    void compileArrayExpr(const ast::ArrayExpr& expr);
    void compileTupleExpr(const ast::TupleExpr& expr);
    void compileLambdaExpr(const ast::LambdaExpr& expr);
    void compileTensorExpr(const ast::TensorExpr& expr);
    
    // 作用域管理
    void enterScope();
    void exitScope();
    int resolveVariable(const std::string& name);
    int allocateLocal(const std::string& name);
    
    // 指令生成辅助
    void emitOp(OpCode op);
    void emitOp1(OpCode op, int operand);
    void emitOp2(OpCode op, int operand1, int operand2);
    void emitOpF(OpCode op, double operand);
    void emitOpS(OpCode op, const std::string& operand);
    void emitJump(OpCode op);  // 返回跳转指令索引
    void patchJump(int jumpIdx, int targetIdx);
    void emitLoadLocal(int slot);
    void emitStoreLocal(int slot);
    void emitLoadGlobal(const std::string& name);
    void emitStoreGlobal(const std::string& name);
    void emitConst(int value);
    void emitConst(double value);
    void emitConst(const std::string& value);
    
    // 类型相关
    std::string getTypeName(const ast::TypeExpr& typeExpr);
    
    // 错误处理
    void error(const std::string& msg);
    void errorf(const char* fmt, ...);
    
    // 调试信息
    void addDebugInfo(const std::string& name, int line);
    
    // 常量池管理
    int addConstant(Value value);
    int findOrAddString(const std::string& str);
    
    // 成员变量
    std::shared_ptr<BytecodeModule> module_;
    std::unique_ptr<CompilationContext> ctx_;
    std::string lastError_;
    bool debugInfo_ = false;
    
    // 全局变量表
    std::unordered_map<std::string, int> globalVars_;
    int nextGlobalSlot_ = 0;
};

} // namespace claw

#endif // CLAW_BYTECODE_COMPILER_H
