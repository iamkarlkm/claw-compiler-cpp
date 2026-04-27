#include "bytecode_compiler.h"
#include "lexer/lexer.h"
#include "../ast_compat.h"
#include <cstdarg>
#include <algorithm>
#include <iostream>

namespace claw {

// ========== 构造函数 ==========

BytecodeCompiler::BytecodeCompiler() 
    : module_(std::make_shared<BytecodeModule>()) {
    ctx_ = std::make_unique<CompilationContext>();
    ctx_->scopeStack.emplace_back();  // 全局作用域
}

BytecodeCompiler::~BytecodeCompiler() = default;

// ========== 主编译入口 ==========

std::shared_ptr<BytecodeModule> BytecodeCompiler::compile(const ast::Program& module) {
    try {
        compileModule(module);
        return module_;
    } catch (const std::exception& e) {
        lastError_ = e.what();
        return nullptr;
    }
}

// ========== 模块编译 ==========

void BytecodeCompiler::compileModule(const ast::Program& module) {
    const auto& decls = module.get_declarations();
    
    // 第一遍: 编译所有函数定义
    for (const auto& stmt : decls) {
        if (stmt->get_kind() == ast::Statement::Kind::Function) {
            compileFunction(static_cast<const ast::FunctionStmt&>(*stmt));
        }
    }
    
    // 第二遍: 编译非函数语句
    for (const auto& stmt : decls) {
        if (stmt->get_kind() != ast::Statement::Kind::Function) {
            compileStatement(*stmt);
        }
    }
    
    // 添加 HALT 指令
    emitOp(bytecode::OpCode::HALT);
}

void BytecodeCompiler::compileFunction(const ast::FunctionStmt& func) {
    bytecode::Function byteFunc;
    byteFunc.name = func.get_name();
    byteFunc.arity = static_cast<uint32_t>(func.get_params().size());
    byteFunc.local_count = byteFunc.arity;
    
    // 保存旧上下文
    auto prevCtx = std::move(ctx_);
    ctx_ = std::make_unique<CompilationContext>();
    ctx_->currentFunction = std::make_shared<bytecode::Function>(byteFunc);
    ctx_->isClosure = false;
    ctx_->scopeStack.emplace_back();
    
    // 分配参数槽位
    int slot = 0;
    for (const auto& param : func.get_params()) {
        ctx_->scopeStack.back()[param.first] = slot++;
    }
    
    // 编译函数体
    if (func.get_body()) {
        compileBlockStmt(*static_cast<const ast::BlockStmt*>(func.get_body()));
    }
    
    // 如果没有显式返回，添加 null 返回
    if (ctx_->currentFunction->code.empty() ||
        ctx_->currentFunction->code.back().op != bytecode::OpCode::RET) {
        emitOp(bytecode::OpCode::RET_NULL);
    }
    
    // 添加到模块
    module_->functions.push_back(*ctx_->currentFunction);
    
    // 恢复旧上下文
    ctx_ = std::move(prevCtx);
}

// ========== 语句编译 ==========

void BytecodeCompiler::compileStatement(const Stmt& stmt) {
    switch (stmt.get_kind()) {
        case ast::Statement::Kind::Let:
            compileLetStmt(static_cast<const ast::LetStmt&>(stmt));
            break;
        case ast::Statement::Kind::Assign:
            compileAssignStmt(static_cast<const ast::AssignStmt&>(stmt));
            break;
        case ast::Statement::Kind::If:
            compileIfStmt(static_cast<const ast::IfStmt&>(stmt));
            break;
        case ast::Statement::Kind::Match:
            compileMatchStmt(static_cast<const ast::MatchStmt&>(stmt));
            break;
        case ast::Statement::Kind::For:
            compileForStmt(static_cast<const ast::ForStmt&>(stmt));
            break;
        case ast::Statement::Kind::While:
            compileWhileStmt(static_cast<const ast::WhileStmt&>(stmt));
            break;
        case ast::Statement::Kind::Return:
            compileReturnStmt(static_cast<const ast::ReturnStmt&>(stmt));
            break;
        case ast::Statement::Kind::Break:
            compileBreakStmt(static_cast<const ast::BreakStmt&>(stmt));
            break;
        case ast::Statement::Kind::Continue:
            compileContinueStmt(static_cast<const ast::ContinueStmt&>(stmt));
            break;
        case ast::Statement::Kind::Block:
            compileBlockStmt(static_cast<const ast::BlockStmt&>(stmt));
            break;
        case ast::Statement::Kind::Expression:
            compileExprStmt(static_cast<const ast::ExprStmt&>(stmt));
            break;
        case ast::Statement::Kind::Publish:
            compilePublishStmt(static_cast<const ast::PublishStmt&>(stmt));
            break;
        case ast::Statement::Kind::Subscribe:
            compileSubscribeStmt(static_cast<const ast::SubscribeStmt&>(stmt));
            break;
        default:
            errorf("Unknown statement type: %d", (int)stmt.get_kind());
    }
}

void BytecodeCompiler::compileLetStmt(const ast::LetStmt& stmt) {
    auto* init = stmt.get_initializer();
    if (init) {
        compileExpression(*init);
    } else {
        emitOp(bytecode::OpCode::PUSH);
        emitOp1(bytecode::OpCode::PUSH, 0);
    }
    
    int slot = allocateLocal(stmt.get_name());
    
    if (ctx_->scopeStack.size() == 1) {
        int globalSlot = nextGlobalSlot_++;
        globalVars_[stmt.get_name()] = globalSlot;
        emitOp1(bytecode::OpCode::DEFINE_GLOBAL, findOrAddString(stmt.get_name()));
        emitOp(bytecode::OpCode::STORE_GLOBAL);
    } else {
        emitOp1(bytecode::OpCode::STORE_LOCAL, slot);
    }
}

void BytecodeCompiler::compileAssignStmt(const ast::AssignStmt& stmt) {
    auto* value = stmt.get_value();
    if (value) {
        compileExpression(*value);
    }
    
    auto* target = stmt.get_target();
    if (target && target->get_kind() == ast::Expression::Kind::Identifier) {
        const auto& name = static_cast<const ast::IdentifierExpr&>(*target).get_name();
        int slot = resolveVariable(name);
        if (slot >= 0) {
            emitOp1(bytecode::OpCode::STORE_LOCAL, slot);
        } else {
            auto it = globalVars_.find(name);
            if (it != globalVars_.end()) {
                emitOp1(bytecode::OpCode::STORE_GLOBAL, it->second);
            }
        }
    }
}

void BytecodeCompiler::compileIfStmt(const ast::IfStmt& stmt) {
    // Handle multi-branch if statements: if cond1 { ... } else if cond2 { ... } else { ... }
    const auto& conditions = stmt.get_conditions();
    const auto& bodies = stmt.get_bodies();
    ast::ASTNode* elseBody = stmt.get_else_body();
    
    std::vector<int> elseJumpIdxs;
    std::vector<int> afterBranchIdxs;
    
    // Compile each branch condition and body
    for (size_t i = 0; i < conditions.size(); ++i) {
        // Compile condition
        if (conditions[i]) compileExpression(*conditions[i]);
        
        // Jump to next branch if condition is false
        int elseJumpIdx = ctx_->currentFunction->code.size();
        emitOp(bytecode::OpCode::JMP_IF_NOT);
        ctx_->pendingJumps.push_back({elseJumpIdx, 0, true});
        
        // Compile then body
        if (i < bodies.size() && bodies[i]) {
            // The body might be a BlockStmt or a single statement
            if (auto* block = dynamic_cast<const ast::BlockStmt*>(bodies[i].get())) {
                enterScope();
                compileBlockStmt(*block);
                exitScope();
            } else if (auto* stmtNode = dynamic_cast<ast::Statement*>(bodies[i].get())) {
                // Single statement
                enterScope();
                compileStatement(*stmtNode);
                exitScope();
            }
        }
        
        // Jump past all remaining branches
        int afterBranchIdx = ctx_->currentFunction->code.size();
        emitOp(bytecode::OpCode::JMP);
        ctx_->pendingJumps.push_back({afterBranchIdx, 0, true});
        afterBranchIdxs.push_back(afterBranchIdx);
        
        // Patch the else jump to here
        patchJump(elseJumpIdx, ctx_->currentFunction->code.size());
    }
    
    // Compile else body
    if (elseBody) {
        if (auto* block = dynamic_cast<const ast::BlockStmt*>(elseBody)) {
            enterScope();
            compileBlockStmt(*block);
            exitScope();
        } else if (auto* stmtNode = dynamic_cast<ast::Statement*>(elseBody)) {
            enterScope();
            compileStatement(*stmtNode);
            exitScope();
        }
    }
    
    // Patch all after-branch jumps
    for (size_t i = 0; i < afterBranchIdxs.size(); ++i) {
        patchJump(afterBranchIdxs[i], ctx_->currentFunction->code.size());
    }
}

void BytecodeCompiler::compileMatchStmt(const ast::MatchStmt& stmt) {
    auto* expr = stmt.get_expr();
    if (expr) compileExpression(*expr);
    
    const auto& patterns = stmt.get_patterns();
    const auto& bodies = stmt.get_bodies();
    
    if (patterns.empty()) return;
    
    // For now, handle the first case only (simplified implementation)
    // Full match compilation would generate a jump table
    if (patterns[0]) {
        emitOp(bytecode::OpCode::DUP);
        compileExpression(*patterns[0]);
        emitOp(bytecode::OpCode::IEQ);
    }
    
    // Jump to next case if no match
    int nextCaseIdx = ctx_->currentFunction->code.size();
    emitOp(bytecode::OpCode::JMP_IF_NOT);
    ctx_->pendingJumps.push_back({nextCaseIdx, 0, true});
    
    // Compile case body
    if (!bodies.empty() && bodies[0]) {
        if (auto* block = dynamic_cast<const ast::BlockStmt*>(bodies[0].get())) {
            enterScope();
            compileBlockStmt(*block);
            exitScope();
        } else if (auto* stmtNode = dynamic_cast<ast::Statement*>(bodies[0].get())) {
            enterScope();
            compileStatement(*stmtNode);
            exitScope();
        }
    }
    
    // Patch jump to next case
    patchJump(nextCaseIdx, ctx_->currentFunction->code.size());
}

void BytecodeCompiler::compileForStmt(const ast::ForStmt& stmt) {
    // for variable in iterable { body }
    // NEW IMPLEMENTATION: Proper iterator protocol (2026-04-26)
    const std::string& varName = stmt.get_variable();
    auto* iterable = stmt.get_iterable();
    auto* body = stmt.get_body();
    
    if (!iterable) return;
    
    // Allocate the loop variable
    int varSlot = allocateLocal(varName);
    
    // Setup loop context
    LoopContext loopCtx;
    loopCtx.breakJumpIdx = -1;
    loopCtx.continueJumpIdx = -1;
    loopCtx.scopeDepth = ctx_->scopeDepth;
    ctx_->loopStack.push_back(loopCtx);
    
    // Compile the iterable expression
    compileExpression(*iterable);
    
    // Create iterator from iterable using EXT opcode
    emitOp(bytecode::OpCode::EXT);
    emitOp1(bytecode::OpCode::EXT, static_cast<uint8_t>(bytecode::ExtOpCode::ITER_CREATE));
    
    // Store iterator in a local slot (slot 0 reserved for iterator)
    emitOp(bytecode::OpCode::STORE_LOCAL);
    emitOp1(bytecode::OpCode::STORE_LOCAL, 0);
    
    // Loop start
    int loopStartIdx = ctx_->currentFunction->code.size();
    
    // Check if iterator has next
    emitOp(bytecode::OpCode::LOAD_LOCAL);
    emitOp1(bytecode::OpCode::LOAD_LOCAL, 0);  // Load iterator
    emitOp(bytecode::OpCode::EXT);
    emitOp1(bytecode::OpCode::EXT, static_cast<uint8_t>(bytecode::ExtOpCode::ITER_HAS_NEXT));
    
    // If no more elements, jump to end
    int condJumpIdx = ctx_->currentFunction->code.size();
    emitOp(bytecode::OpCode::JMP_IF_NOT);
    ctx_->pendingJumps.push_back({condJumpIdx, 0, true});
    
    // Get next element
    emitOp(bytecode::OpCode::LOAD_LOCAL);
    emitOp1(bytecode::OpCode::LOAD_LOCAL, 0);  // Load iterator
    emitOp(bytecode::OpCode::EXT);
    emitOp1(bytecode::OpCode::EXT, static_cast<uint8_t>(bytecode::ExtOpCode::ITER_NEXT));
    
    // Stack now has: [value, done]
    // Pop done, keep value
    emitOp(bytecode::OpCode::POP);
    
    // Store value to loop variable
    emitOp(bytecode::OpCode::STORE_LOCAL);
    emitOp1(bytecode::OpCode::STORE_LOCAL, varSlot);
    
    // Compile body
    enterScope();
    if (body) {
        if (auto* block = dynamic_cast<const ast::BlockStmt*>(body)) {
            compileBlockStmt(*block);
        } else if (auto* stmtNode = dynamic_cast<ast::Statement*>(body)) {
            compileStatement(*stmtNode);
        }
    }
    exitScope();
    
    // Jump back to loop start
    emitOp1(bytecode::OpCode::JMP, loopStartIdx);
    
    // Patch the condition jump (break out of loop)
    patchJump(condJumpIdx, ctx_->currentFunction->code.size());
    
    // Pop the iterator from stack
    emitOp(bytecode::OpCode::LOAD_LOCAL);
    emitOp1(bytecode::OpCode::LOAD_LOCAL, 0);
    emitOp(bytecode::OpCode::POP);
    
    ctx_->loopStack.pop_back();
}

void BytecodeCompiler::compileWhileStmt(const ast::WhileStmt& stmt) {
    int loopStartIdx = ctx_->currentFunction->code.size();
    
    LoopContext loopCtx;
    loopCtx.breakJumpIdx = -1;
    loopCtx.continueJumpIdx = -1;
    loopCtx.scopeDepth = ctx_->scopeDepth;
    ctx_->loopStack.push_back(loopCtx);
    
    auto* cond = stmt.get_condition();
    if (cond) compileExpression(*cond);
    int condJumpIdx = ctx_->currentFunction->code.size();
    emitOp(bytecode::OpCode::JMP_IF_NOT);
    ctx_->pendingJumps.push_back({condJumpIdx, 0, true});
    patchJump(condJumpIdx, ctx_->currentFunction->code.size());
    
    enterScope();
    auto* body = stmt.get_body();
    if (body) {
        // Use dynamic_cast to check actual type
        if (auto* blockStmt = dynamic_cast<const ast::BlockStmt*>(body)) {
            compileBlockStmt(*blockStmt);
        } else if (auto* stmtNode = dynamic_cast<const ast::Statement*>(body)) {
            compileStatement(*stmtNode);
        }
    }
    exitScope();
    
    emitOp1(bytecode::OpCode::JMP, loopStartIdx);
    
    ctx_->loopStack.pop_back();
}

void BytecodeCompiler::compileReturnStmt(const ast::ReturnStmt& stmt) {
    auto* value = stmt.get_value();
    if (value) {
        compileExpression(*value);
    } else {
        emitOp(bytecode::OpCode::PUSH);
        emitOp1(bytecode::OpCode::PUSH, 0);
    }
    emitOp(bytecode::OpCode::RET);
}

void BytecodeCompiler::compileBreakStmt(const ast::BreakStmt& stmt) {
    if (ctx_->loopStack.empty()) {
        error("break outside of loop");
        return;
    }
    
    int jumpIdx = ctx_->currentFunction->code.size();
    emitOp(bytecode::OpCode::JMP);
    ctx_->loopStack.back().breakJumpIdx = jumpIdx;
    ctx_->pendingJumps.push_back({jumpIdx, 0, true});
}

void BytecodeCompiler::compileContinueStmt(const ast::ContinueStmt& stmt) {
    if (ctx_->loopStack.empty()) {
        error("continue outside of loop");
        return;
    }
    
    emitOp(bytecode::OpCode::JMP);
    int jumpIdx = ctx_->currentFunction->code.size() - 1;
    ctx_->loopStack.back().continueJumpIdx = jumpIdx;
}

void BytecodeCompiler::compileBlockStmt(const ast::BlockStmt& block) {
    for (auto& stmt : block.get_statements()) {
        compileStatement(*stmt);
    }
}

void BytecodeCompiler::compileExprStmt(const ast::ExprStmt& stmt) {
    auto* expr = stmt.get_expr();
    if (expr) {
        compileExpression(*expr);
        emitOp(bytecode::OpCode::POP);
    }
}

void BytecodeCompiler::compilePublishStmt(const ast::PublishStmt& stmt) {
    emitConst(stmt.get_event_name());
    // PublishStmt has get_arguments() - compile each argument
    const auto& args = stmt.get_arguments();
    // Push argument count
    emitOp(bytecode::OpCode::PUSH);
    emitOp1(bytecode::OpCode::PUSH, static_cast<int64_t>(args.size()));
    for (const auto& arg : args) {
        if (arg) compileExpression(*arg);
    }
    emitOp(bytecode::OpCode::EXT);
}

void BytecodeCompiler::compileSubscribeStmt(const ast::SubscribeStmt& stmt) {
    // SubscribeStmt handler is a FunctionStmt, not an Expression
    // We need to compile it as a closure or skip for now
    emitConst(stmt.get_event_name());
    emitOp(bytecode::OpCode::EXT);
}

// ========== 表达式编译 ==========

void BytecodeCompiler::compileExpression(const Expr& expr) {
    switch (expr.get_kind()) {
        case ast::Expression::Kind::Literal:
            compileLiteralExpr(static_cast<const ast::LiteralExpr&>(expr));
            break;
        case ast::Expression::Kind::Identifier:
            compileIdentifierExpr(static_cast<const ast::IdentifierExpr&>(expr));
            break;
        case ast::Expression::Kind::Binary:
            compileBinaryExpr(static_cast<const ast::BinaryExpr&>(expr));
            break;
        case ast::Expression::Kind::Unary:
            compileUnaryExpr(static_cast<const ast::UnaryExpr&>(expr));
            break;
        case ast::Expression::Kind::Call:
            compileCallExpr(static_cast<const ast::CallExpr&>(expr));
            break;
        case ast::Expression::Kind::Index:
            compileIndexExpr(static_cast<const ast::IndexExpr&>(expr));
            break;
        case ast::Expression::Kind::Member:
            compileFieldExpr(static_cast<const ast::MemberExpr&>(expr));
            break;
        case ast::Expression::Kind::Array:
            compileArrayExpr(static_cast<const ast::ArrayExpr&>(expr));
            break;
        case ast::Expression::Kind::Tuple:
            compileTupleExpr(static_cast<const ast::TupleExpr&>(expr));
            break;
        case ast::Expression::Kind::Lambda:
            compileLambdaExpr(static_cast<const ast::LambdaExpr&>(expr));
            break;
        default:
            errorf("Unknown expression type: %d", (int)expr.get_kind());
    }
}

void BytecodeCompiler::compileLiteralExpr(const ast::LiteralExpr& expr) {
    const auto& val = expr.get_value();
    std::visit([this](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, int64_t>) {
            emitConst((int)v);
        } else if constexpr (std::is_same_v<T, double>) {
            emitConst(v);
        } else if constexpr (std::is_same_v<T, std::string>) {
            emitConst(v);
        } else if constexpr (std::is_same_v<T, bool>) {
            emitConst(v ? 1 : 0);
        } else if constexpr (std::is_same_v<T, char>) {
            emitConst((int)v);
        } else {
            emitOp(bytecode::OpCode::PUSH);
            emitOp1(bytecode::OpCode::PUSH, 0);
        }
    }, val);
}

void BytecodeCompiler::compileIdentifierExpr(const ast::IdentifierExpr& expr) {
    int slot = resolveVariable(expr.get_name());
    if (slot >= 0) {
        emitLoadLocal(slot);
    } else {
        emitLoadGlobal(expr.get_name());
    }
}

void BytecodeCompiler::compileBinaryExpr(const ast::BinaryExpr& expr) {
    compileExpression(*expr.get_left());
    compileExpression(*expr.get_right());
    
    switch (expr.get_operator()) {
        case TokenType::Op_plus:     emitOp(bytecode::OpCode::IADD); break;
        case TokenType::Op_minus:    emitOp(bytecode::OpCode::ISUB); break;
        case TokenType::Op_star:     emitOp(bytecode::OpCode::IMUL); break;
        case TokenType::Op_slash:    emitOp(bytecode::OpCode::IDIV); break;
        case TokenType::Op_percent:  emitOp(bytecode::OpCode::IMOD); break;
        case TokenType::Op_eq:    emitOp(bytecode::OpCode::IEQ); break;
        case TokenType::Op_neq:  emitOp(bytecode::OpCode::INE); break;
        case TokenType::Op_lt:       emitOp(bytecode::OpCode::ILT); break;
        case TokenType::Op_lte:    emitOp(bytecode::OpCode::ILE); break;
        case TokenType::Op_gt:       emitOp(bytecode::OpCode::IGT); break;
        case TokenType::Op_gte:    emitOp(bytecode::OpCode::IGE); break;
        case TokenType::Op_and:  emitOp(bytecode::OpCode::AND); break;
        case TokenType::Op_or: emitOp(bytecode::OpCode::OR); break;
        default:
            errorf("Unknown binary operator: %d", (int)expr.get_operator());
    }
}

void BytecodeCompiler::compileUnaryExpr(const ast::UnaryExpr& expr) {
    compileExpression(*expr.get_operand());
    
    switch (expr.get_operator()) {
        case TokenType::Op_minus:    emitOp(bytecode::OpCode::INEG); break;
        case TokenType::Op_bang:     emitOp(bytecode::OpCode::NOT); break;
        default: break;
    }
}

void BytecodeCompiler::compileCallExpr(const ast::CallExpr& expr) {
    for (auto it = expr.get_arguments().rbegin(); it != expr.get_arguments().rend(); ++it) {
        compileExpression(**it);
    }
    compileExpression(*expr.get_callee());
    emitOp1(bytecode::OpCode::CALL, static_cast<int>(expr.get_arguments().size()));
}

void BytecodeCompiler::compileIndexExpr(const ast::IndexExpr& expr) {
    compileExpression(*expr.get_index());
    compileExpression(*expr.get_object());
    emitOp(bytecode::OpCode::LOAD_INDEX);
}

void BytecodeCompiler::compileFieldExpr(const ast::MemberExpr& expr) {
    compileExpression(*expr.get_object());
    emitConst(expr.get_member());
    emitOp(bytecode::OpCode::LOAD_FIELD);
}

void BytecodeCompiler::compileArrayExpr(const ast::ArrayExpr& expr) {
    emitOp1(bytecode::OpCode::ALLOC_ARRAY, static_cast<int>(expr.get_elements().size()));
    for (auto& elem : expr.get_elements()) {
        compileExpression(*elem);
        emitOp(bytecode::OpCode::ARRAY_PUSH);
    }
}

void BytecodeCompiler::compileTupleExpr(const ast::TupleExpr& expr) {
    for (auto it = expr.get_elements().rbegin(); it != expr.get_elements().rend(); ++it) {
        compileExpression(**it);
    }
    emitOp1(bytecode::OpCode::CREATE_TUPLE, static_cast<int>(expr.get_elements().size()));
}

void BytecodeCompiler::compileLambdaExpr(const ast::LambdaExpr& expr) {
    emitOp(bytecode::OpCode::CLOSURE);
    // 简化: 创建空函数
    bytecode::Function lambdaFunc;
    lambdaFunc.name = "";
    lambdaFunc.arity = 0;
    module_->functions.push_back(lambdaFunc);
}

// ========== 作用域管理 ==========

void BytecodeCompiler::enterScope() {
    ctx_->scopeStack.emplace_back();
    ctx_->scopeDepth++;
}

void BytecodeCompiler::exitScope() {
    if (!ctx_->scopeStack.empty()) {
        ctx_->scopeStack.pop_back();
        ctx_->scopeDepth--;
    }
}

int BytecodeCompiler::resolveVariable(const std::string& name) {
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
    int slot = static_cast<int>(ctx_->scopeStack.back().size());
    ctx_->scopeStack.back()[name] = slot;
    return slot;
}

// ========== 指令生成辅助 ==========

void BytecodeCompiler::emitOp(bytecode::OpCode op) {
    bytecode::Instruction inst;
    inst.op = op;
    ctx_->currentFunction->code.push_back(inst);
}

void BytecodeCompiler::emitOp1(bytecode::OpCode op, int operand) {
    bytecode::Instruction inst;
    inst.op = op;
    inst.operand = static_cast<uint32_t>(operand);
    ctx_->currentFunction->code.push_back(inst);
}

void BytecodeCompiler::emitOp2(bytecode::OpCode op, int operand1, int operand2) {
    bytecode::Instruction inst;
    inst.op = op;
    // For two-operand instructions, encode both in operand field
    // Lower 16 bits for operand1, upper 16 bits for operand2
    inst.operand = (static_cast<uint32_t>(operand2) << 16) | static_cast<uint32_t>(operand1);
    ctx_->currentFunction->code.push_back(inst);
}

void BytecodeCompiler::emitOpF(bytecode::OpCode op, double operand) {
    bytecode::Instruction inst;
    inst.op = op;
    // Encode double as bit pattern in operand
    union { double d; uint64_t i; } converter;
    converter.d = operand;
    inst.operand = static_cast<uint32_t>(converter.i & 0xFFFFFFFF);
    ctx_->currentFunction->code.push_back(inst);
}

void BytecodeCompiler::emitOpS(bytecode::OpCode op, const std::string& operand) {
    int constIdx = findOrAddString(operand);
    emitOp1(op, constIdx);
}

void BytecodeCompiler::emitJump(bytecode::OpCode op) {
    bytecode::Instruction inst;
    inst.op = op;
    ctx_->currentFunction->code.push_back(inst);
}

void BytecodeCompiler::patchJump(int jumpIdx, int targetIdx) {
    if (jumpIdx >= 0 && jumpIdx < (int)ctx_->currentFunction->code.size()) {
        ctx_->currentFunction->code[jumpIdx].operand = static_cast<uint32_t>(targetIdx);
    }
}

void BytecodeCompiler::emitLoadLocal(int slot) {
    if (slot == 0) {
        emitOp(bytecode::OpCode::LOAD_LOCAL_0);
    } else if (slot == 1) {
        emitOp(bytecode::OpCode::LOAD_LOCAL_1);
    } else {
        emitOp1(bytecode::OpCode::LOAD_LOCAL, slot);
    }
}

void BytecodeCompiler::emitStoreLocal(int slot) {
    if (slot == 0) {
        emitOp1(bytecode::OpCode::STORE_LOCAL, 0);
    } else if (slot == 1) {
        emitOp1(bytecode::OpCode::STORE_LOCAL, 1);
    } else {
        emitOp1(bytecode::OpCode::STORE_LOCAL, slot);
    }
}

void BytecodeCompiler::emitLoadGlobal(const std::string& name) {
    auto it = globalVars_.find(name);
    if (it != globalVars_.end()) {
        emitOp1(bytecode::OpCode::LOAD_GLOBAL, it->second);
    } else {
        emitOpS(bytecode::OpCode::LOAD_GLOBAL, name);
    }
}

void BytecodeCompiler::emitStoreGlobal(const std::string& name) {
    auto it = globalVars_.find(name);
    if (it != globalVars_.end()) {
        emitOp1(bytecode::OpCode::STORE_GLOBAL, it->second);
    } else {
        emitOpS(bytecode::OpCode::STORE_GLOBAL, name);
    }
}

void BytecodeCompiler::emitConst(int value) {
    int idx = addConstant(bytecode::Value::integer(value));
    emitOp1(bytecode::OpCode::PUSH, idx);
}

void BytecodeCompiler::emitConst(double value) {
    int idx = addConstant(bytecode::Value::floating(value));
    emitOp1(bytecode::OpCode::PUSH, idx);
}

void BytecodeCompiler::emitConst(const std::string& value) {
    int idx = findOrAddString(value);
    emitOp1(bytecode::OpCode::PUSH, idx);
}

// ========== 常量池 ==========

int BytecodeCompiler::addConstant(const bytecode::Value& value) {
    // 根据值类型添加到对应的常量池
    switch (value.type) {
        case bytecode::ValueType::I64:
            return module_->constants.add_integer(value.data.i64);
        case bytecode::ValueType::F64:
            return module_->constants.add_float(value.data.f64);
        case bytecode::ValueType::STRING:
            return module_->constants.add_string(value.str);
        default:
            // 对于其他类型，使用 values 向量
            module_->constants.values.push_back(value);
            return static_cast<int>(module_->constants.values.size()) - 1;
    }
}

int BytecodeCompiler::findOrAddString(const std::string& s) {
    for (size_t i = 0; i < module_->constants.strings.size(); ++i) {
        if (module_->constants.strings[i] == s) {
            return static_cast<int>(i);
        }
    }
    return module_->constants.add_string(s);
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

} // namespace claw
