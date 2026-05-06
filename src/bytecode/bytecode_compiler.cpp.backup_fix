#include "bytecode_compiler.h"
#include <cstdarg>
#include <algorithm>
#include <iostream>

namespace claw {

// ========== 构造函数 ==========

BytecodeCompiler::BytecodeCompiler() 
    : module_(std::make_shared<BytecodeModule>()) {
    ctx_ = std::make_unique<CompilationContext>();
}

BytecodeCompiler::~BytecodeCompiler() = default;

// ========== 主编译入口 ==========

std::shared_ptr<BytecodeModule> BytecodeCompiler::compile(const ast::Module& module) {
    try {
        compileModule(module);
        return module_;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return nullptr;
    }
}

// ========== 模块编译 ==========

void BytecodeCompiler::compileModule(const ast::Module& module) {
    module_->name = module.name;
    
    // 编译所有函数定义
    for (const auto& stmt : module.statements) {
        if (stmt->type() == ast::NodeType::FUNCTION) {
            compileFunction(static_cast<const ast::FunctionStmt&>(*stmt));
        } else if (stmt->type() == ast::NodeType::LET) {
            // 全局 let 声明
            compileLetStmt(*stmt);
        }
    }
    
    // 编译主语句块 (非函数定义)
    for (const auto& stmt : module.statements) {
        if (stmt->type() != ast::NodeType::FUNCTION) {
            compileStatement(*stmt);
        }
    }
    
    // 添加 HALT 指令
    emitOp(OpCode::HALT);
}

void BytecodeCompiler::compileFunction(const ast::FunctionStmt& func) {
    // 创建新函数
    auto byteFunc = std::make_shared<BytecodeFunction>();
    byteFunc->name = func.name;
    byteFunc->arity = func.params.size();
    byteFunc->upvalueCount = 0;
    byteFunc->localCount = func.params.size();
    
    // 保存旧上下文并创建新的
    auto prevCtx = std::move(ctx_);
    ctx_ = std::make_unique<CompilationContext>();
    ctx_->currentFunction = byteFunc;
    ctx_->isClosure = false;
    
    // 记录返回值类型
    if (func.returnType) {
        byteFunc->returnType = getTypeName(*func.returnType);
    }
    
    // 分配参数槽位
    int slot = 0;
    for (const auto& param : func.params) {
        ctx_->scopeStack.back()[param.name] = slot++;
    }
    
    // 编译函数体
    enterScope();
    compileBlockStmt(func.body);
    exitScope();
    
    // 如果没有显式返回，添加 null 返回
    if (ctx_->currentFunction->instructions.empty() ||
        ctx_->currentFunction->instructions.back().op != OpCode::RET) {
        emitOp(OpCode::RET_NULL);
    }
    
    // 更新 upvalue 数量
    byteFunc->upvalueCount = ctx_->upvalues.size();
    
    // 添加到模块
    module_->functions.push_back(byteFunc);
    
    // 恢复旧上下文
    ctx_ = std::move(prevCtx);
}

// ========== 语句编译 ==========

void BytecodeCompiler::compileStatement(const ast::Stmt& stmt) {
    switch (stmt.type()) {
        case ast::NodeType::LET:
            compileLetStmt(static_cast<const ast::LetStmt&>(stmt));
            break;
        case ast::NodeType::ASSIGN:
            compileAssignStmt(static_cast<const ast::AssignStmt&>(stmt));
            break;
        case ast::NodeType::IF:
            compileIfStmt(static_cast<const ast::IfStmt&>(stmt));
            break;
        case ast::NodeType::MATCH:
            compileMatchStmt(static_cast<const ast::MatchStmt&>(stmt));
            break;
        case ast::NodeType::FOR:
            compileForStmt(static_cast<const ast::ForStmt&>(stmt));
            break;
        case ast::NodeType::WHILE:
            compileWhileStmt(static_cast<const ast::WhileStmt&>(stmt));
            break;
        case ast::NodeType::LOOP:
            compileLoopStmt(static_cast<const ast::LoopStmt&>(stmt));
            break;
        case ast::NodeType::RETURN:
            compileReturnStmt(static_cast<const ast::ReturnStmt&>(stmt));
            break;
        case ast::NodeType::BREAK:
            compileBreakStmt(static_cast<const ast::BreakStmt&>(stmt));
            break;
        case ast::NodeType::CONTINUE:
            compileContinueStmt(static_cast<const ast::ContinueStmt&>(stmt));
            break;
        case ast::NodeType::BLOCK:
            compileBlockStmt(static_cast<const ast::BlockStmt&>(stmt));
            break;
        case ast::NodeType::EXPR_STMT:
            compileExprStmt(static_cast<const ast::ExprStmt&>(stmt));
            break;
        case ast::NodeType::PUBLISH:
            compilePublishStmt(static_cast<const ast::PublishStmt&>(stmt));
            break;
        case ast::NodeType::SUBSCRIBE:
            compileSubscribeStmt(static_cast<const ast::SubscribeStmt&>(stmt));
            break;
        default:
            errorf("Unknown statement type: %d", (int)stmt.type());
    }
}

void BytecodeCompiler::compileLetStmt(const ast::LetStmt& stmt) {
    // 编译右侧表达式
    if (stmt.initializer) {
        compileExpression(*stmt.initializer);
    } else {
        emitOp(OpCode::PUSH);
        emitOp1(OpCode::PUSH, 0); // null
    }
    
    // 分配局部变量
    int slot = allocateLocal(stmt.name);
    
    // 如果是全局作用域，生成全局变量定义
    if (ctx_->scopeStack.size() == 1) {
        int globalSlot = nextGlobalSlot_++;
        globalVars_[stmt.name] = globalSlot;
        emitOp1(OpCode::DEFINE_GLOBAL, findOrAddString(stmt.name));
        emitOp(OpCode::STORE_GLOBAL);
    } else {
        emitOp(OpCode::STORE_LOCAL);
        emitOp1(OpCode::STORE_LOCAL, slot);
    }
}

void BytecodeCompiler::compileAssignStmt(const ast::AssignStmt& stmt) {
    compileExpression(*stmt.value);
    
    // 解析目标变量
    int slot = resolveVariable(stmt.target);
    if (slot >= 0) {
        // 检查是否需要加载 self (对于方法中的字段赋值)
        emitOp1(OpCode::STORE_LOCAL, slot);
    } else {
        // 全局变量
        auto it = globalVars_.find(stmt.target);
        if (it != globalVars_.end()) {
            emitOp1(OpCode::STORE_GLOBAL, it->second);
        } else {
            // 索引赋值或字段赋值
            compileExpression(*stmt.target);
            emitOp(OpCode::STORE_INDEX);
        }
    }
}

void BytecodeCompiler::compileIfStmt(const ast::IfStmt& stmt) {
    std::vector<int> endJumpIdxs;
    
    // 编译条件
    compileExpression(*stmt.condition);
    
    // 跳转到 else 分支 (如果条件为 false)
    int elseJumpIdx = ctx_->currentFunction->instructions.size();
    emitOp(OpCode::JMP_IF_NOT);
    ctx_->pendingJumps.push_back({elseJumpIdx, 0, true});
    
    // 编译 then 分支
    enterScope();
    compileBlockStmt(stmt.thenBranch);
    exitScope();
    
    // 跳到 if 结束处 (then 分支执行完后)
    int afterThenIdx = ctx_->currentFunction->instructions.size();
    emitOp(OpCode::JMP);
    endJumpIdxs.push_back(ctx_->currentFunction->instructions.size() - 1);
    ctx_->pendingJumps.push_back({afterThenIdx, 0, true});
    
    // 回填 else 跳转目标
    patchJump(elseJumpIdx, ctx_->currentFunction->instructions.size());
    
    // 编译 else 分支
    if (stmt.elseBranch) {
        enterScope();
        compileBlockStmt(*stmt.elseBranch);
        exitScope();
    }
    
    // 回填所有结束跳转
    for (size_t i = 0; i < endJumpIdxs.size(); ++i) {
        patchJump(endJumpIdxs[i], ctx_->currentFunction->instructions.size());
    }
}

void BytecodeCompiler::compileMatchStmt(const ast::MatchStmt& stmt) {
    // 编译被匹配的值
    compileExpression(*stmt.expr);
    
    // 为每个 case 生成跳转
    std::vector<int> caseEndIdxs;
    int defaultCaseIdx = -1;
    
    for (size_t i = 0; i < stmt.cases.size(); ++i) {
        const auto& matchCase = stmt.cases[i];
        
        if (matchCase.pattern == "_") {
            // 默认 case
            defaultCaseIdx = i;
            continue;
        }
        
        // 复制要匹配的值到栈顶
        emitOp(OpCode::DUP);
        
        // 编译模式并比较
        if (matchCase.pattern == "true" || matchCase.pattern == "false") {
            emitConst(matchCase.pattern == "true" ? 1 : 0);
            emitOp(OpCode::IEQ);
        } else {
            // 尝试解析为整数
            try {
                int intVal = std::stoi(matchCase.pattern);
                emitConst(intVal);
                emitOp(OpCode::IEQ);
            } catch (...) {
                // 字符串比较
                emitConst(matchCase.pattern);
                emitOp(OpCode::IEQ);
            }
        }
        
        // 如果匹配，跳转到对应 case
        int caseJumpIdx = ctx_->currentFunction->instructions.size();
        emitOp(OpCode::JMP_IF);
        ctx_->pendingJumps.push_back({caseJumpIdx, 0, true});
        
        // 编译 case 体
        enterScope();
        compileBlockStmt(matchCase.body);
        exitScope();
        
        // 跳到 match 结束
        int endJumpIdx = ctx_->currentFunction->instructions.size();
        emitOp(OpCode::JMP);
        caseEndIdxs.push_back(ctx_->currentFunction->instructions.size() - 1);
        ctx_->pendingJumps.push_back({endJumpIdx, 0, true});
        
        // 回填 case 跳转
        patchJump(caseJumpIdx, ctx_->currentFunction->instructions.size());
    }
    
    // 回填所有 case 结束跳转
    for (int idx : caseEndIdxs) {
        patchJump(idx, ctx_->currentFunction->instructions.size());
    }
}

void BytecodeCompiler::compileForStmt(const ast::ForStmt& stmt) {
    // for i in range(0, 10) { ... }
    // 编译初始化
    if (stmt.init) {
        compileExpression(*stmt.init);
    }
    
    // 循环开始位置
    int loopStartIdx = ctx_->currentFunction->instructions.size();
    
    // 保存循环上下文
    LoopContext loopCtx;
    loopCtx.breakJumpIdx = -1;
    loopCtx.continueJumpIdx = -1;
    loopCtx.scopeDepth = ctx_->scopeDepth;
    ctx_->loopStack.push_back(loopCtx);
    
    // 编译条件
    if (stmt.condition) {
        compileExpression(*stmt.condition);
        int condJumpIdx = ctx_->currentFunction->instructions.size();
        emitOp(OpCode::JMP_IF_NOT);
        ctx_->pendingJumps.push_back({condJumpIdx, 0, true});
        patchJump(condJumpIdx, ctx_->currentFunction->instructions.size());
    }
    
    // 编译循环体
    enterScope();
    // 为迭代变量分配槽位
    if (!stmt.iterVar.empty()) {
        allocateLocal(stmt.iterVar);
    }
    compileBlockStmt(stmt.body);
    exitScope();
    
    // 编译迭代表达式
    if (stmt.increment) {
        compileExpression(*stmt.increment);
        emitOp(OpCode::POP); // 丢弃迭代结果
    }
    
    // 跳回循环开始
    emitOp1(OpCode::JMP, loopStartIdx);
    
    // 回填 break 跳转
    if (!ctx_->loopStack.empty() && ctx_->loopStack.back().breakJumpIdx >= 0) {
        patchJump(ctx_->loopStack.back().breakJumpIdx, 
                  ctx_->currentFunction->instructions.size());
    }
    
    ctx_->loopStack.pop_back();
}

void BytecodeCompiler::compileWhileStmt(const ast::WhileStmt& stmt) {
    int loopStartIdx = ctx_->currentFunction->instructions.size();
    
    // 保存循环上下文
    LoopContext loopCtx;
    loopCtx.breakJumpIdx = -1;
    loopCtx.continueJumpIdx = -1;
    loopCtx.scopeDepth = ctx_->scopeDepth;
    ctx_->loopStack.push_back(loopCtx);
    
    // 编译条件
    compileExpression(*stmt.condition);
    int condJumpIdx = ctx_->currentFunction->instructions.size();
    emitOp(OpCode::JMP_IF_NOT);
    ctx_->pendingJumps.push_back({condJumpIdx, 0, true});
    patchJump(condJumpIdx, ctx_->currentFunction->instructions.size());
    
    // 编译循环体
    enterScope();
    compileBlockStmt(stmt.body);
    exitScope();
    
    // 跳回循环开始
    emitOp1(OpCode::JMP, loopStartIdx);
    
    // 回填 break 跳转
    if (!ctx_->loopStack.empty() && ctx_->loopStack.back().breakJumpIdx >= 0) {
        patchJump(ctx_->loopStack.back().breakJumpIdx,
                  ctx_->currentFunction->instructions.size());
    }
    
    ctx_->loopStack.pop_back();
}

void BytecodeCompiler::compileLoopStmt(const ast::LoopStmt& stmt) {
    int loopStartIdx = ctx_->currentFunction->instructions.size();
    
    // 保存循环上下文
    LoopContext loopCtx;
    loopCtx.breakJumpIdx = -1;
    loopCtx.continueJumpIdx = -1;
    loopCtx.scopeDepth = ctx_->scopeDepth;
    ctx_->loopStack.push_back(loopCtx);
    
    // 编译循环体
    enterScope();
    compileBlockStmt(stmt.body);
    exitScope();
    
    // 无条件跳回循环开始
    emitOp1(OpCode::JMP, loopStartIdx);
    
    // 回填 break 跳转
    if (!ctx_->loopStack.empty() && ctx_->loopStack.back().breakJumpIdx >= 0) {
        patchJump(ctx_->loopStack.back().breakJumpIdx,
                  ctx_->currentFunction->instructions.size());
    }
    
    ctx_->loopStack.pop_back();
}

void BytecodeCompiler::compileReturnStmt(const ast::ReturnStmt& stmt) {
    if (stmt.value) {
        compileExpression(*stmt.value);
    } else {
        emitOp(OpCode::PUSH);
        emitOp1(OpCode::PUSH, 0); // null
    }
    emitOp(OpCode::RET);
}

void BytecodeCompiler::compileBreakStmt(const ast::BreakStmt& stmt) {
    if (ctx_->loopStack.empty()) {
        error("break outside of loop");
        return;
    }
    
    int jumpIdx = ctx_->currentFunction->instructions.size();
    emitOp(OpCode::JMP);
    
    // 记录待回填的 break 跳转
    ctx_->loopStack.back().breakJumpIdx = jumpIdx;
    ctx_->pendingJumps.push_back({jumpIdx, 0, true});
}

void BytecodeCompiler::compileContinueStmt(const ast::ContinueStmt& stmt) {
    if (ctx_->loopStack.empty()) {
        error("continue outside of loop");
        return;
    }
    
    // 找到最近的循环开始位置
    int loopStartIdx = -1;
    for (auto it = ctx_->loopStack.rbegin(); it != ctx_->loopStack.rend(); ++it) {
        // 需要在正确的作用域中查找
        // 这里简化处理，直接跳转到最后一条指令位置
        break;
    }
    
    // 简化的 continue 实现：跳转到当前函数的最后
    // 实际的实现需要追踪 continue 目标
    emitOp(OpCode::JMP);
    int jumpIdx = ctx_->currentFunction->instructions.size() - 1;
    ctx_->loopStack.back().continueJumpIdx = jumpIdx;
}

void BytecodeCompiler::compileBlockStmt(const ast::BlockStmt& block) {
    for (const auto& stmt : block.statements) {
        compileStatement(*stmt);
    }
}

void BytecodeCompiler::compileExprStmt(const ast::ExprStmt& stmt) {
    compileExpression(*stmt.expr);
    emitOp(OpCode::POP); // 丢弃表达式结果
}

void BytecodeCompiler::compilePublishStmt(const ast::PublishStmt& stmt) {
    // 编译事件名称
    emitConst(stmt.eventName);
    
    // 编译载荷
    if (stmt.payload) {
        compileExpression(*stmt.payload);
    } else {
        emitOp(OpCode::PUSH);
        emitOp1(OpCode::PUSH, 0);
    }
    
    emitOp(OpCode::EXT);
}

void BytecodeCompiler::compileSubscribeStmt(const ast::SubscribeStmt& stmt) {
    // 编译事件处理器 (lambda)
    if (stmt.handler) {
        compileExpression(*stmt.handler);
    }
    
    // 编译事件名称
    emitConst(stmt.eventName);
    
    emitOp(OpCode::EXT);
}

// ========== 表达式编译 ==========

void BytecodeCompiler::compileExpression(const ast::Expr& expr) {
    switch (expr.type()) {
        case ast::NodeType::LITERAL:
            compileLiteralExpr(static_cast<const ast::LiteralExpr&>(expr));
            break;
        case ast::NodeType::IDENTIFIER:
            compileIdentifierExpr(static_cast<const ast::IdentifierExpr&>(expr));
            break;
        case ast::NodeType::BINARY:
            compileBinaryExpr(static_cast<const ast::BinaryExpr&>(expr));
            break;
        case ast::NodeType::UNARY:
            compileUnaryExpr(static_cast<const ast::UnaryExpr&>(expr));
            break;
        case ast::NodeType::CALL:
            compileCallExpr(static_cast<const ast::CallExpr&>(expr));
            break;
        case ast::NodeType::INDEX:
            compileIndexExpr(static_cast<const ast::IndexExpr&>(expr));
            break;
        case ast::NodeType::FIELD:
            compileFieldExpr(static_cast<const ast::FieldExpr&>(expr));
            break;
        case ast::NodeType::ARRAY:
            compileArrayExpr(static_cast<const ast::ArrayExpr&>(expr));
            break;
        case ast::NodeType::TUPLE:
            compileTupleExpr(static_cast<const ast::TupleExpr&>(expr));
            break;
        case ast::NodeType::LAMBDA:
            compileLambdaExpr(static_cast<const ast::LambdaExpr&>(expr));
            break;
        case ast::NodeType::TENSOR:
            compileTensorExpr(static_cast<const ast::TensorExpr&>(expr));
            break;
        default:
            errorf("Unknown expression type: %d", (int)expr.type());
    }
}

void BytecodeCompiler::compileLiteralExpr(const ast::LiteralExpr& expr) {
    switch (expr.litType) {
        case ast::LiteralType::INTEGER:
            emitConst(expr.intValue);
            break;
        case ast::LiteralType::FLOAT:
            emitConst(expr.floatValue);
            break;
        case ast::LiteralType::STRING:
            emitConst(expr.stringValue);
            break;
        case ast::LiteralType::BOOLEAN:
            emitConst(expr.boolValue ? 1 : 0);
            break;
        case ast::LiteralType::BYTE:
            emitConst((int)expr.byteValue);
            break;
        case ast::LiteralType::CHAR:
            emitConst((int)expr.charValue);
            break;
        case ast::LiteralType::NIL:
            emitOp(OpCode::PUSH);
            emitOp1(OpCode::PUSH, 0);
            break;
    }
}

void BytecodeCompiler::compileIdentifierExpr(const ast::IdentifierExpr& expr) {
    int slot = resolveVariable(expr.name);
    if (slot >= 0) {
        emitLoadLocal(slot);
    } else {
        emitLoadGlobal(expr.name);
    }
}

void BytecodeCompiler::compileBinaryExpr(const ast::BinaryExpr& expr) {
    compileExpression(*expr.left);
    compileExpression(*expr.right);
    
    switch (expr.op) {
        case ast::BinaryOp::ADD:    emitOp(OpCode::IADD); break;
        case ast::BinaryOp::SUB:    emitOp(OpCode::ISUB); break;
        case ast::BinaryOp::MUL:    emitOp(OpCode::IMUL); break;
        case ast::BinaryOp::DIV:    emitOp(OpCode::IDIV); break;
        case ast::BinaryOp::MOD:    emitOp(OpCode::IMOD); break;
        case ast::BinaryOp::FADD:   emitOp(OpCode::FADD); break;
        case ast::BinaryOp::FSUB:   emitOp(OpCode::FSUB); break;
        case ast::BinaryOp::FMUL:   emitOp(OpCode::FMUL); break;
        case ast::BinaryOp::FDIV:   emitOp(OpCode::FDIV); break;
        case ast::BinaryOp::FMOD:   emitOp(OpCode::FMOD); break;
        case ast::BinaryOp::EQ:     emitOp(OpCode::IEQ); break;
        case ast::BinaryOp::NEQ:    emitOp(OpCode::INE); break;
        case ast::BinaryOp::LT:     emitOp(OpCode::ILT); break;
        case ast::BinaryOp::LE:     emitOp(OpCode::ILE); break;
        case ast::BinaryOp::GT:     emitOp(OpCode::IGT); break;
        case ast::BinaryOp::GE:     emitOp(OpCode::IGE); break;
        case ast::BinaryOp::AND:    emitOp(OpCode::AND); break;
        case ast::BinaryOp::OR:     emitOp(OpCode::OR); break;
        case ast::BinaryOp::BAND:   emitOp(OpCode::BAND); break;
        case ast::BinaryOp::BOR:    emitOp(OpCode::BOR); break;
        case ast::BinaryOp::BXOR:   emitOp(OpCode::BXOR); break;
        case ast::BinaryOp::SHL:    emitOp(OpCode::SHL); break;
        case ast::BinaryOp::SHR:    emitOp(OpCode::SHR); break;
        default:
            errorf("Unknown binary operator: %d", (int)expr.op);
    }
}

void BytecodeCompiler::compileUnaryExpr(const ast::UnaryExpr& expr) {
    compileExpression(*expr.operand);
    
    switch (expr.op) {
        case ast::UnaryOp::NEG:
            emitOp(OpCode::INEG);
            break;
        case ast::UnaryOp::FNEG:
            emitOp(OpCode::FNEG);
            break;
        case ast::UnaryOp::NOT:
            emitOp(OpCode::NOT);
            break;
        case ast::UnaryOp::BNOT:
            emitOp(OpCode::BNOT);
            break;
    }
}

void BytecodeCompiler::compileCallExpr(const ast::CallExpr& expr) {
    // 编译参数 (从后往前，以便栈顺序正确)
    for (auto it = expr.args.rbegin(); it != expr.args.rend(); ++it) {
        compileExpression(**it);
    }
    
    // 编译函数表达式
    compileExpression(*expr.callee);
    
    // 调用指令
    emitOp1(OpCode::CALL, expr.args.size());
}

void BytecodeCompiler::compileIndexExpr(const ast::IndexExpr& expr) {
    // 加载索引
    compileExpression(*expr.index);
    
    // 加载数组
    compileExpression(*expr.object);
    
    emitOp(OpCode::LOAD_INDEX);
}

void BytecodeCompiler::compileFieldExpr(const ast::FieldExpr& expr) {
    // 加载对象
    compileExpression(*expr.object);
    
    // 字段名作为常量
    emitConst(expr.field);
    
    emitOp(OpCode::LOAD_FIELD);
}

void BytecodeCompiler::compileArrayExpr(const ast::ArrayExpr& expr) {
    // 创建数组
    emitOp1(OpCode::ALLOC_ARRAY, expr.elements.size());
    
    // 添加元素
    for (const auto& elem : expr.elements) {
        compileExpression(*elem);
        emitOp(OpCode::ARRAY_PUSH);
    }
}

void BytecodeCompiler::compileTupleExpr(const ast::TupleExpr& expr) {
    // 从后往前编译元素
    for (auto it = expr.elements.rbegin(); it != expr.elements.rend(); ++it) {
        compileExpression(**it);
    }
    
    emitOp1(OpCode::CREATE_TUPLE, expr.elements.size());
}

void BytecodeCompiler::compileLambdaExpr(const ast::LambdaExpr& expr) {
    // 创建闭包
    emitOp(OpCode::CLOSURE);
    int closureIdx = ctx_->currentFunction->instructions.size();
    
    // 创建新的编译上下文用于 lambda
    auto prevCtx = std::move(ctx_);
    ctx_ = std::make_unique<CompilationContext>();
    ctx_->currentFunction = std::make_shared<BytecodeFunction>();
    ctx_->isClosure = true;
    ctx_->scopeDepth = prevCtx->scopeDepth + 1;
    
    // 添加参数槽位
    for (size_t i = 0; i < expr.params.size(); ++i) {
        ctx_->scopeStack.back()[expr.params[i]] = i;
    }
    
    // 编译 lambda 体
    if (expr.body) {
        compileBlockStmt(*expr.body);
    }
    
    // 默认返回 null
    if (ctx_->currentFunction->instructions.empty() ||
        ctx_->currentFunction->instructions.back().op != OpCode::RET) {
        emitOp(OpCode::RET_NULL);
    }
    
    // 复制指令到外层函数
    auto lambdaFunc = ctx_->currentFunction;
    prevCtx->currentFunction->instructions.insert(
        prevCtx->currentFunction->instructions.end(),
        lambdaFunc->instructions.begin(),
        lambdaFunc->instructions.end()
    );
    
    // 恢复上下文
    ctx_ = std::move(prevCtx);
}

void BytecodeCompiler::compileTensorExpr(const ast::TensorExpr& expr) {
    // 张量创建
    emitOp(OpCode::TENSOR_CREATE);
    
    // 编译形状
    for (const auto& dim : expr.shape) {
        if (dim) {
            compileExpression(*dim);
        } else {
            emitConst(0); // 动态维度
        }
    }
    
    // 初始化数据
    if (!expr.data.empty()) {
        for (const auto& val : expr.data) {
            if (val.type() == ast::NodeType::LITERAL) {
                const auto& lit = static_cast<const ast::LiteralExpr&>(*val);
                if (lit.litType == ast::LiteralType::INTEGER) {
                    emitConst(lit.intValue);
                } else if (lit.litType == ast::LiteralType::FLOAT) {
                    emitConst(lit.floatValue);
                }
            }
        }
    }
    
    emitOp(OpCode::TENSOR_RESHAPE);
}

// ========== 作用域管理 ==========

void BytecodeCompiler::enterScope() {
    ctx_->scopeStack.emplace_back();
    ctx_->scopeDepth++;
}

void BytecodeCompiler::exitScope() {
    if (!ctx_->scopeStack.empty()) {
        // 恢复 upvalues
        for (const auto& uv : ctx_->upvalues) {
            // upvalue 关闭逻辑
        }
        ctx_->scopeStack.pop_back();
        ctx_->scopeDepth--;
    }
}

int BytecodeCompiler::resolveVariable(const std::string& name) {
    // 从内层作用域向外层查找
    for (auto it = ctx_->scopeStack.rbegin(); it != ctx_->scopeStack.rend(); ++it) {
        auto varIt = it->find(name);
        if (varIt != it->end()) {
            return varIt->second;
        }
    }
    return -1;
}

int BytecodeCompiler::allocateLocal(const std::string& name) {
    if (ctx_->scopeStack.empty()) {
        ctx_->scopeStack.emplace_back();
    }
    
    int slot = ctx_->scopeStack.back().size();
    ctx_->scopeStack.back()[name] = slot;
    return slot;
}

// ========== 指令生成辅助 ==========

void BytecodeCompiler::emitOp(OpCode op) {
    Instruction inst;
    inst.op = op;
    ctx_->currentFunction->instructions.push_back(inst);
}

void BytecodeCompiler::emitOp1(OpCode op, int operand) {
    Instruction inst;
    inst.op = op;
    inst.operand1 = operand;
    ctx_->currentFunction->instructions.push_back(inst);
}

void BytecodeCompiler::emitOp2(OpCode op, int operand1, int operand2) {
    Instruction inst;
    inst.op = op;
    inst.operand1 = operand1;
    inst.operand2 = operand2;
    ctx_->currentFunction->instructions.push_back(inst);
}

void BytecodeCompiler::emitOpF(OpCode op, double operand) {
    Instruction inst;
    inst.op = op;
    inst.floatVal = operand;
    ctx_->currentFunction->instructions.push_back(inst);
}

void BytecodeCompiler::emitOpS(OpCode op, const std::string& operand) {
    int constIdx = findOrAddString(operand);
    emitOp1(op, constIdx);
}

void BytecodeCompiler::emitJump(OpCode op) {
    Instruction inst;
    inst.op = op;
    ctx_->currentFunction->instructions.push_back(inst);
}

void BytecodeCompiler::patchJump(int jumpIdx, int targetIdx) {
    if (jumpIdx >= 0 && jumpIdx < (int)ctx_->currentFunction->instructions.size()) {
        ctx_->currentFunction->instructions[jumpIdx].operand1 = targetIdx;
    }
}

void BytecodeCompiler::emitLoadLocal(int slot) {
    if (slot == 0) {
        emitOp(OpCode::LOAD_LOCAL_0);
    } else if (slot == 1) {
        emitOp(OpCode::LOAD_LOCAL_1);
    } else {
        emitOp1(OpCode::LOAD_LOCAL, slot);
    }
}

void BytecodeCompiler::emitStoreLocal(int slot) {
    if (slot == 0) {
        emitOp1(OpCode::STORE_LOCAL, 0);
    } else if (slot == 1) {
        emitOp1(OpCode::STORE_LOCAL, 1);
    } else {
        emitOp1(OpCode::STORE_LOCAL, slot);
    }
}

void BytecodeCompiler::emitLoadGlobal(const std::string& name) {
    auto it = globalVars_.find(name);
    if (it != globalVars_.end()) {
        emitOp1(OpCode::LOAD_GLOBAL, it->second);
    } else {
        // 动态全局变量
        emitOpS(OpCode::LOAD_GLOBAL, name);
    }
}

void BytecodeCompiler::emitStoreGlobal(const std::string& name) {
    auto it = globalVars_.find(name);
    if (it != globalVars_.end()) {
        emitOp1(OpCode::STORE_GLOBAL, it->second);
    } else {
        // 动态全局变量
        emitOpS(OpCode::STORE_GLOBAL, name);
    }
}

void BytecodeCompiler::emitConst(int value) {
    int idx = addConstant(Value::fromInt(value));
    emitOp1(OpCode::PUSH, idx);
}

void BytecodeCompiler::emitConst(double value) {
    int idx = addConstant(Value::fromDouble(value));
    emitOp1(OpCode::PUSH, idx);
}

void BytecodeCompiler::emitConst(const std::string& value) {
    int idx = findOrAddString(value);
    emitOp1(OpCode::PUSH, idx);
}

// ========== 类型相关 ==========

std::string BytecodeCompiler::getTypeName(const ast::TypeExpr& typeExpr) {
    if (typeExpr.baseType == "int" || typeExpr.baseType == "i8" || 
        typeExpr.baseType == "i16" || typeExpr.baseType == "i32" || 
        typeExpr.baseType == "i64") {
        return "int";
    } else if (typeExpr.baseType == "float" || typeExpr.baseType == "f32") {
        return "float";
    } else if (typeExpr.baseType == "double" || typeExpr.baseType == "f64") {
        return "double";
    } else if (typeExpr.baseType == "bool") {
        return "bool";
    } else if (typeExpr.baseType == "string") {
        return "string";
    }
    return typeExpr.baseType;
}

// ========== 错误处理 ==========

void BytecodeCompiler::error(const std::string& msg) {
    lastError_ = msg;
    throw std::runtime_error(msg);
}

void BytecodeCompiler::errorf(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    error(buf);
}

// ========== 调试信息 ==========

void BytecodeCompiler::addDebugInfo(const std::string& name, int line) {
    if (debugInfo_) {
        DebugInfo info;
        info.name = name;
        info.line = line;
        ctx_->currentFunction->debugInfo.push_back(info);
    }
}

// ========== 常量池管理 ==========

int BytecodeCompiler::addConstant(Value value) {
    // 查找现有常量
    for (size_t i = 0; i < module_->constants.size(); ++i) {
        if (module_->constants[i] == value) {
            return i;
        }
    }
    
    int idx = module_->constants.size();
    module_->constants.push_back(value);
    return idx;
}

int BytecodeCompiler::findOrAddString(const std::string& str) {
    return addConstant(Value::fromString(str));
}

} // namespace claw
