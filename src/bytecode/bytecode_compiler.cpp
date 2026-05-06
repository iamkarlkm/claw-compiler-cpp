#include "bytecode_compiler.h"
#include "lexer/lexer.h"
#include "../ast_compat.h"
#include <cstdarg>
#include <algorithm>
#include <iostream>
#include <unordered_set>

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

    // 第二遍: 编译非函数语句到隐式顶层函数
    bool hasTopLevel = false;
    for (const auto& stmt : decls) {
        if (stmt->get_kind() != ast::Statement::Kind::Function) {
            if (!hasTopLevel) {
                hasTopLevel = true;
                ctx_->currentFunction = std::make_shared<bytecode::Function>();
                ctx_->currentFunction->name = "__top_level";
            }
            compileStatement(*stmt);
        }
    }

    if (hasTopLevel) {
        emitOp(bytecode::OpCode::RET_NULL);
        ctx_->currentFunction->local_count = static_cast<uint32_t>(ctx_->nextSlot);
        module_->functions.push_back(*ctx_->currentFunction);
        ctx_->currentFunction.reset();
    }
}

void BytecodeCompiler::compileFunction(const ast::FunctionStmt& func) {
    bytecode::Function byteFunc;
    byteFunc.name = func.get_name();
    byteFunc.arity = static_cast<uint32_t>(func.get_params().size());
    byteFunc.local_count = byteFunc.arity;

    // Record parameter types for JIT
    for (const auto& param : func.get_params()) {
        const std::string& type_name = param.second;
        if (type_name == "f64" || type_name == "f32" || type_name == "float") {
            byteFunc.param_types.push_back(bytecode::ValueType::F64);
        } else if (type_name == "string" || type_name == "str") {
            byteFunc.param_types.push_back(bytecode::ValueType::STRING);
        } else if (type_name == "bool") {
            byteFunc.param_types.push_back(bytecode::ValueType::BOOL);
        } else {
            byteFunc.param_types.push_back(bytecode::ValueType::I64);
        }
    }

    // Record return type for JIT
    const std::string& ret_type = func.get_return_type();
    if (ret_type == "f64" || ret_type == "f32" || ret_type == "float") {
        byteFunc.return_type = bytecode::ValueType::F64;
    } else if (ret_type == "string" || ret_type == "str") {
        byteFunc.return_type = bytecode::ValueType::STRING;
    } else if (ret_type == "bool") {
        byteFunc.return_type = bytecode::ValueType::BOOL;
    } else {
        byteFunc.return_type = bytecode::ValueType::I64;
    }

    // 保存旧上下文
    auto prevCtx = std::move(ctx_);
    ctx_ = std::make_unique<CompilationContext>();
    ctx_->currentFunction = std::make_shared<bytecode::Function>(byteFunc);
    ctx_->isClosure = false;
    ctx_->scopeStack.emplace_back();
    ctx_->nextSlot = 0;

    // 分配参数槽位
    int slot = 0;
    for (const auto& param : func.get_params()) {
        ctx_->scopeStack.back()[param.first] = slot++;
    }
    ctx_->nextSlot = slot;

    // 编译函数体
    if (func.get_body()) {
        compileBlockStmt(*static_cast<const ast::BlockStmt*>(func.get_body()));
    }
    
    // 如果没有显式返回，添加 null 返回
    if (ctx_->currentFunction->code.empty() ||
        ctx_->currentFunction->code.back().op != bytecode::OpCode::RET) {
        emitOp(bytecode::OpCode::RET_NULL);
    }
    
    // Update local_count to reflect all allocated locals
    ctx_->currentFunction->local_count = std::max(
        ctx_->currentFunction->local_count,
        static_cast<uint32_t>(ctx_->nextSlot)
    );

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
    // Tuple destructuring: let (a, b) = expr
    if (stmt.is_tuple_destructuring()) {
        auto* init = stmt.get_initializer();
        if (!init) {
            error("Tuple destructuring requires an initializer");
            return;
        }
        compileExpression(*init);

        bool isGlobalScope = ctx_->currentFunction && ctx_->currentFunction->name == "__top_level";
        const auto& names = stmt.get_tuple_names();
        for (size_t i = 0; i < names.size(); i++) {
            if (names[i] == "_") continue; // skip discard placeholder
            emitOp(bytecode::OpCode::DUP);          // duplicate tuple
            emitConst(static_cast<int>(i));         // push index (via constant pool)
            emitOp(bytecode::OpCode::LOAD_INDEX);   // load element
            if (isGlobalScope) {
                int globalSlot = nextGlobalSlot_++;
                globalVars_[names[i]] = globalSlot;
                emitOp1(bytecode::OpCode::DEFINE_GLOBAL, findOrAddString(names[i]));
            } else {
                int slot = allocateLocal(names[i]);
                emitOp1(bytecode::OpCode::STORE_LOCAL, slot);
            }
        }
        // pop the original tuple
        emitOp(bytecode::OpCode::POP);
        return;
    }

    auto* init = stmt.get_initializer();
    if (init) {
        compileExpression(*init);
    } else {
        emitOp(bytecode::OpCode::PUSH);
        emitOp1(bytecode::OpCode::PUSH, 0);
    }

    // Global scope is only the __top_level implicit function
    bool isGlobalScope = ctx_->currentFunction && ctx_->currentFunction->name == "__top_level";
    if (isGlobalScope) {
        int globalSlot = nextGlobalSlot_++;
        globalVars_[stmt.get_name()] = globalSlot;
        emitOp1(bytecode::OpCode::DEFINE_GLOBAL, findOrAddString(stmt.get_name()));
    } else {
        int slot = allocateLocal(stmt.get_name());
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
            emitStoreGlobal(name);
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
    auto* iterable = stmt.get_iterable();
    auto* body = stmt.get_body();
    if (!iterable) return;

    // Setup loop context
    LoopContext loopCtx;
    loopCtx.breakJumpIdx = -1;
    loopCtx.continueJumpIdx = -1;
    loopCtx.scopeDepth = ctx_->scopeDepth;
    ctx_->loopStack.push_back(loopCtx);

    // Range expression: for i in start..end { body }
    if (iterable->get_kind() == ast::Expression::Kind::Binary) {
        auto* bin = static_cast<const ast::BinaryExpr*>(iterable);
        if (bin->get_operator() == TokenType::Op_range) {
            int varSlot = allocateLocal(stmt.get_variable());

            // start_expr
            compileExpression(*bin->get_left());
            emitOp1(bytecode::OpCode::STORE_LOCAL, varSlot);

            // end_expr
            compileExpression(*bin->get_right());
            int endSlot = allocateLocal("__end");
            emitOp1(bytecode::OpCode::STORE_LOCAL, endSlot);

            // loop start
            int loopStartIdx = static_cast<int>(ctx_->currentFunction->code.size());

            // if i > end, break
            emitOp1(bytecode::OpCode::LOAD_LOCAL, varSlot);
            emitOp1(bytecode::OpCode::LOAD_LOCAL, endSlot);
            emitOp(bytecode::OpCode::IGT);

            int exitJumpIdx = static_cast<int>(ctx_->currentFunction->code.size());
            emitOp(bytecode::OpCode::JMP_IF);
            ctx_->pendingJumps.push_back({exitJumpIdx, 0, true});

            // body
            enterScope();
            if (body) {
                if (auto* block = dynamic_cast<const ast::BlockStmt*>(body)) {
                    compileBlockStmt(*block);
                } else if (auto* stmtNode = dynamic_cast<ast::Statement*>(body)) {
                    compileStatement(*stmtNode);
                }
            }
            exitScope();

            // continue target: increment section
            int continueTargetIdx = static_cast<int>(ctx_->currentFunction->code.size());

            // increment: i = i + 1
            emitOp1(bytecode::OpCode::LOAD_LOCAL, varSlot);
            emitConst(1);
            emitOp(bytecode::OpCode::IADD);
            emitOp1(bytecode::OpCode::STORE_LOCAL, varSlot);

            // jump back
            int backOffset = loopStartIdx - (static_cast<int>(ctx_->currentFunction->code.size()) + 1);
            emitOp1(bytecode::OpCode::JMP, backOffset);

            // patch loop exit jump to here
            patchJump(exitJumpIdx, static_cast<int>(ctx_->currentFunction->code.size()));

            // patch any break jumps inside the body
            if (ctx_->loopStack.back().breakJumpIdx >= 0) {
                patchJump(ctx_->loopStack.back().breakJumpIdx, static_cast<int>(ctx_->currentFunction->code.size()));
            }

            // patch any continue jumps inside the body to the increment section
            if (ctx_->loopStack.back().continueJumpIdx >= 0) {
                patchJump(ctx_->loopStack.back().continueJumpIdx, continueTargetIdx);
            }

            ctx_->loopStack.pop_back();
            return;
        }
    }

    // Integer literal: for i in 5  =>  for i in 1..5
    if (iterable->get_kind() == ast::Expression::Kind::Literal) {
        auto* lit = static_cast<const ast::LiteralExpr*>(iterable);
        auto val = lit->get_value();
        if (std::holds_alternative<int64_t>(val)) {
            int64_t end = std::get<int64_t>(val);
            int varSlot = allocateLocal(stmt.get_variable());

            // i = 1
            emitConst(1);
            emitOp1(bytecode::OpCode::STORE_LOCAL, varSlot);

            // end
            emitConst(static_cast<int>(end));
            int endSlot = allocateLocal("__end");
            emitOp1(bytecode::OpCode::STORE_LOCAL, endSlot);

            int loopStartIdx = static_cast<int>(ctx_->currentFunction->code.size());

            emitOp1(bytecode::OpCode::LOAD_LOCAL, varSlot);
            emitOp1(bytecode::OpCode::LOAD_LOCAL, endSlot);
            emitOp(bytecode::OpCode::IGT);

            int exitJumpIdx = static_cast<int>(ctx_->currentFunction->code.size());
            emitOp(bytecode::OpCode::JMP_IF);
            ctx_->pendingJumps.push_back({exitJumpIdx, 0, true});

            enterScope();
            if (body) {
                if (auto* block = dynamic_cast<const ast::BlockStmt*>(body)) {
                    compileBlockStmt(*block);
                } else if (auto* stmtNode = dynamic_cast<ast::Statement*>(body)) {
                    compileStatement(*stmtNode);
                }
            }
            exitScope();

            int continueTargetIdx = static_cast<int>(ctx_->currentFunction->code.size());

            emitOp1(bytecode::OpCode::LOAD_LOCAL, varSlot);
            emitConst(1);
            emitOp(bytecode::OpCode::IADD);
            emitOp1(bytecode::OpCode::STORE_LOCAL, varSlot);

            int backOffset = loopStartIdx - (static_cast<int>(ctx_->currentFunction->code.size()) + 1);
            emitOp1(bytecode::OpCode::JMP, backOffset);

            patchJump(exitJumpIdx, static_cast<int>(ctx_->currentFunction->code.size()));

            if (ctx_->loopStack.back().breakJumpIdx >= 0) {
                patchJump(ctx_->loopStack.back().breakJumpIdx, static_cast<int>(ctx_->currentFunction->code.size()));
            }

            if (ctx_->loopStack.back().continueJumpIdx >= 0) {
                patchJump(ctx_->loopStack.back().continueJumpIdx, continueTargetIdx);
            }

            ctx_->loopStack.pop_back();
            return;
        }
    }

    // TODO: support array and string iteration
    error("For-loop iterable type not yet supported in bytecode compiler");
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

    // Jump back to loop start
    int backOffset = loopStartIdx - (static_cast<int>(ctx_->currentFunction->code.size()) + 1);
    emitOp1(bytecode::OpCode::JMP, backOffset);

    // Patch the condition jump to jump past the backward JMP
    patchJump(condJumpIdx, ctx_->currentFunction->code.size());

    // Patch any break jumps inside the body
    if (ctx_->loopStack.back().breakJumpIdx >= 0) {
        patchJump(ctx_->loopStack.back().breakJumpIdx, static_cast<int>(ctx_->currentFunction->code.size()));
    }

    // Patch any continue jumps inside the body to the loop start (condition check)
    if (ctx_->loopStack.back().continueJumpIdx >= 0) {
        patchJump(ctx_->loopStack.back().continueJumpIdx, loopStartIdx);
    }

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
    (void)stmt;
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
    (void)stmt;
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
    if (!expr) return;

    // Handle assignment expressions: x = 5 (parsed as BinaryExpr with Op_eq_assign)
    if (expr->get_kind() == ast::Expression::Kind::Binary) {
        auto* bin = static_cast<const ast::BinaryExpr*>(expr);
        if (bin->get_operator() == TokenType::Op_eq_assign) {
            auto* target = bin->get_left();
            auto* value = bin->get_right();
            if (target && value) {
                compileExpression(*value);
                if (target->get_kind() == ast::Expression::Kind::Identifier) {
                    const auto& name = static_cast<const ast::IdentifierExpr&>(*target).get_name();
                    int slot = resolveVariable(name);
                    if (slot >= 0) {
                        emitOp1(bytecode::OpCode::STORE_LOCAL, slot);
                    } else {
                        emitStoreGlobal(name);
                    }
                }
            }
            return;
        }
    }

    compileExpression(*expr);
    emitOp(bytecode::OpCode::POP);
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
        case TokenType::Op_range:
            // Range expressions are not first-class values.
            // compileForStmt handles them directly in for-loop context.
            // In other contexts, discard operands and push nil.
            emitOp(bytecode::OpCode::POP); // pop right (end)
            emitOp(bytecode::OpCode::POP); // pop left (start)
            emitOp(bytecode::OpCode::PUSH);
            emitOp1(bytecode::OpCode::PUSH, 0); // nil
            break;
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
    static const std::unordered_set<std::string> builtins = {
        "print", "println", "len", "type", "int", "float", "string", "bool", "input", "array", "range", "panic"
    };

    if (expr.get_callee()->get_kind() == ast::Expression::Kind::Identifier) {
        const auto& name = static_cast<const ast::IdentifierExpr&>(*expr.get_callee()).get_name();
        if (builtins.find(name) != builtins.end()) {
            for (auto it = expr.get_arguments().begin(); it != expr.get_arguments().end(); ++it) {
                compileExpression(**it);
            }
            int str_idx = findOrAddString(name);
            int arg_count = static_cast<int>(expr.get_arguments().size());
            emitOp2(bytecode::OpCode::CALL_EXT, str_idx, arg_count);
            return;
        }
    }

    for (auto it = expr.get_arguments().begin(); it != expr.get_arguments().end(); ++it) {
        compileExpression(**it);
    }
    compileExpression(*expr.get_callee());
    emitOp1(bytecode::OpCode::CALL, static_cast<int>(expr.get_arguments().size()));
}

void BytecodeCompiler::compileIndexExpr(const ast::IndexExpr& expr) {
    compileExpression(*expr.get_object());
    compileExpression(*expr.get_index());
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
    for (auto it = expr.get_elements().begin(); it != expr.get_elements().end(); ++it) {
        compileExpression(**it);
    }
    emitOp1(bytecode::OpCode::CREATE_TUPLE, static_cast<int>(expr.get_elements().size()));
}

void BytecodeCompiler::compileLambdaExpr(const ast::LambdaExpr& expr) {
    (void)expr;
    bytecode::Function lambdaFunc;
    lambdaFunc.name = "";
    lambdaFunc.arity = 0;
    int func_idx = static_cast<int>(module_->functions.size());
    module_->functions.push_back(lambdaFunc);
    emitOp1(bytecode::OpCode::CLOSURE, func_idx);
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
    int slot = ctx_->nextSlot++;
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
        int offset = targetIdx - (jumpIdx + 1);
        ctx_->currentFunction->code[jumpIdx].operand = static_cast<uint32_t>(offset);
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
    emitOpS(bytecode::OpCode::LOAD_GLOBAL, name);
}

void BytecodeCompiler::emitStoreGlobal(const std::string& name) {
    emitOpS(bytecode::OpCode::STORE_GLOBAL, name);
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
    // 统一使用 values 向量，简化 VM 查找
    module_->constants.values.push_back(value);
    return static_cast<int>(module_->constants.values.size()) - 1;
}

int BytecodeCompiler::findOrAddString(const std::string& s) {
    // 统一使用 values 向量
    for (size_t i = 0; i < module_->constants.values.size(); ++i) {
        if (module_->constants.values[i].type == bytecode::ValueType::STRING &&
            module_->constants.values[i].str == s) {
            return static_cast<int>(i);
        }
    }
    module_->constants.values.push_back(bytecode::Value::string(s));
    return static_cast<int>(module_->constants.values.size()) - 1;
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
