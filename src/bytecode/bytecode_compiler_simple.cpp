// bytecode_compiler_simple.cpp - Simplified Bytecode Compiler Implementation
// Minimal working version focused on core functionality

#include "bytecode_compiler_simple.h"
#include <cassert>
#include <iostream>

namespace claw {

// ============================================================================
// Constructor
// ============================================================================

SimpleBytecodeCompiler::SimpleBytecodeCompiler() 
    : module_(std::make_unique<bytecode::Module>()),
      ctx_(std::make_unique<CompileContext>()) {
    module_->name = "main";
}

SimpleBytecodeCompiler::~SimpleBytecodeCompiler() = default;

// ============================================================================
// Main Entry Point
// ============================================================================

std::unique_ptr<bytecode::Module> SimpleBytecodeCompiler::compile(
    std::shared_ptr<ast::Program> ast) {
    
    if (!ast) {
        setError("Null AST program");
        return nullptr;
    }
    
    try {
        // Create main function to hold top-level statements
        bytecode::Function mainFunc;
        mainFunc.name = "main";
        mainFunc.arity = 0;
        mainFunc.local_count = 0;
        
        // Set as current function
        ctx_->currentFunction = &mainFunc;
        ctx_->scopeStack.clear();
        ctx_->localSlotCounter = 0;
        
        // Enter global scope
        enterScope();
        
        // Compile all declarations
        compileProgram(*ast);
        
        // Add implicit HALT at end
        emit(bytecode::OpCode::HALT);
        
        // Exit global scope
        exitScope();
        
        // Set local count
        mainFunc.local_count = static_cast<uint32_t>(ctx_->localSlotCounter);
        
        // Add main function to module
        module_->functions.push_back(std::move(mainFunc));
        
        return std::move(module_);
        
    } catch (const std::exception& e) {
        setError(e.what());
        return nullptr;
    }
}

// ============================================================================
// Program Compilation
// ============================================================================

void SimpleBytecodeCompiler::compileProgram(const ast::Program& program) {
    const auto& decls = program.get_declarations();
    
    // First pass: compile function definitions
    for (const auto& stmt : decls) {
        if (stmt->get_kind() == ast::Statement::Kind::Function) {
            compileFunction(static_cast<const ast::FunctionStmt&>(*stmt));
        }
    }
    
    // Second pass: compile executable statements
    for (const auto& stmt : decls) {
        if (stmt->get_kind() != ast::Statement::Kind::Function) {
            compileStatement(*stmt);
        }
    }
}

void SimpleBytecodeCompiler::compileFunction(const ast::FunctionStmt& func) {
    // Create new function
    bytecode::Function byteFunc;
    byteFunc.name = func.get_name();
    byteFunc.arity = static_cast<uint32_t>(func.get_params().size());
    
    // Save current function context
    bytecode::Function* prevFunc = ctx_->currentFunction;
    int prevSlotCounter = ctx_->localSlotCounter;
    ctx_->currentFunction = &byteFunc;
    ctx_->localSlotCounter = 0;
    
    // Enter function scope
    enterScope();
    
    // Allocate parameter slots
    for (const auto& param : func.get_params()) {
        allocateLocal(param.first);
    }
    
    // Compile function body
    auto* bodyNode = func.get_body();
    if (bodyNode) {
        auto& stmt = static_cast<const ast::Statement&>(*bodyNode);
        if (stmt.get_kind() == ast::Statement::Kind::Block) {
            compileBlock(static_cast<const ast::BlockStmt&>(stmt));
        } else {
            // Single statement
            enterScope();
            compileStatement(stmt);
            exitScope();
        }
    }
    
    // Emit implicit return null if needed
    auto& code = ctx_->currentFunction->code;
    if (code.empty() || 
        (code.back().op != bytecode::OpCode::RET && 
         code.back().op != bytecode::OpCode::RET_NULL)) {
        emit(bytecode::OpCode::RET_NULL);
    }
    
    // Exit function scope
    exitScope();
    
    // Set local count
    byteFunc.local_count = static_cast<uint32_t>(ctx_->localSlotCounter);
    
    // Restore previous function context
    ctx_->currentFunction = prevFunc;
    ctx_->localSlotCounter = prevSlotCounter;
    
    // Add function to module
    module_->functions.push_back(std::move(byteFunc));
}

// ============================================================================
// Statement Compilation
// ============================================================================

void SimpleBytecodeCompiler::compileStatement(const ast::Statement& stmt) {
    switch (stmt.get_kind()) {
        case ast::Statement::Kind::Let:
            compileLet(static_cast<const ast::LetStmt&>(stmt));
            break;
        case ast::Statement::Kind::Assign:
            compileAssign(static_cast<const ast::AssignStmt&>(stmt));
            break;
        case ast::Statement::Kind::If:
            compileIf(static_cast<const ast::IfStmt&>(stmt));
            break;
        case ast::Statement::Kind::While:
            compileWhile(static_cast<const ast::WhileStmt&>(stmt));
            break;
        case ast::Statement::Kind::For:
            compileFor(static_cast<const ast::ForStmt&>(stmt));
            break;
        case ast::Statement::Kind::Return:
            compileReturn(static_cast<const ast::ReturnStmt&>(stmt));
            break;
        case ast::Statement::Kind::Break:
            compileBreak(static_cast<const ast::BreakStmt&>(stmt));
            break;
        case ast::Statement::Kind::Continue:
            compileContinue(static_cast<const ast::ContinueStmt&>(stmt));
            break;
        case ast::Statement::Kind::Block:
            compileBlock(static_cast<const ast::BlockStmt&>(stmt));
            break;
        default:
            break;
    }
}

void SimpleBytecodeCompiler::compileLet(const ast::LetStmt& stmt) {
    auto* init = stmt.get_initializer();
    if (init) {
        compileExpression(*init);
    } else {
        emit(bytecode::OpCode::PUSH);
        emitI(bytecode::OpCode::PUSH, 0);
    }
    
    int slot = allocateLocal(stmt.get_name());
    emitI(bytecode::OpCode::STORE_LOCAL, slot);
}

void SimpleBytecodeCompiler::compileAssign(const ast::AssignStmt& stmt) {
    compileExpression(*stmt.get_value());
    
    // Get target name - need to handle different target types
    std::string varName;
    auto* target = stmt.get_target();
    if (target && target->get_kind() == ast::Expression::Kind::Identifier) {
        varName = static_cast<const ast::IdentifierExpr&>(*target).get_name();
    }
    
    int slot = resolveVariable(varName);
    if (slot >= 0) {
        emitI(bytecode::OpCode::STORE_LOCAL, slot);
    } else {
        auto idx = static_cast<int32_t>(module_->global_names.size());
        module_->global_names.push_back(varName);
        emitI(bytecode::OpCode::STORE_GLOBAL, idx);
    }
}

// IfStmt has custom structure - multiple conditions/bodies
void SimpleBytecodeCompiler::compileIf(const ast::Statement& stmt) {
    const auto& ifStmt = static_cast<const ast::IfStmt&>(stmt);
    const auto& conds = ifStmt.get_conditions();
    const auto& bodies = ifStmt.get_bodies();
    
    if (conds.empty() || bodies.empty()) return;
    
    // Compile first condition
    compileExpression(*conds[0]);
    int elseJump = emitJump(bytecode::OpCode::JMP_IF_NOT);
    
    // Compile first body
    auto* bodyNode = bodies[0].get();
    if (stmt.get_kind() == ast::Statement::Kind::Block) {
        compileBlock(static_cast<const ast::BlockStmt&>(*bodyNode));
    }
    
    int endJump = emitJump(bytecode::OpCode::JMP);
    patchJump(elseJump, ctx_->currentFunction->code.size());
    
    // Else branch
    auto* elseBody = ifStmt.get_else_body();
    if (elseBody && elseBody->get_node_kind() == ast::ASTNode::Kind::Statement) {
        compileBlock(static_cast<const ast::BlockStmt&>(*elseBody));
    }
    
    patchJump(endJump, ctx_->currentFunction->code.size());
}

void SimpleBytecodeCompiler::compileWhile(const ast::WhileStmt& stmt) {
    int loopStart = ctx_->currentFunction->code.size();
    
    bool prevInLoop = ctx_->inLoop;
    ctx_->inLoop = true;
    ctx_->loopStartStack.push_back(loopStart);
    ctx_->loopBreakStack.push_back({});
    
    compileExpression(*stmt.get_condition());
    int exitJump = emitJump(bytecode::OpCode::JMP_IF_NOT);
    
    auto* bodyNode = stmt.get_body();
    if (bodyNode && stmt.get_kind() == ast::Statement::Kind::Block) {
        compileBlock(static_cast<const ast::BlockStmt&>(*bodyNode));
    }
    
    emitI(bytecode::OpCode::JMP, loopStart);
    patchJump(exitJump, ctx_->currentFunction->code.size());
    
    for (int breakIdx : ctx_->loopBreakStack.back()) {
        patchJump(breakIdx, ctx_->currentFunction->code.size());
    }
    
    ctx_->inLoop = prevInLoop;
    ctx_->loopStartStack.pop_back();
    ctx_->loopBreakStack.pop_back();
}

void SimpleBytecodeCompiler::compileFor(const ast::ForStmt& stmt) {
    if (stmt.get_initializer()) {
        compileStatement(*stmt.get_initializer());
    }
    
    int loopStart = ctx_->currentFunction->code.size();
    
    bool prevInLoop = ctx_->inLoop;
    ctx_->inLoop = true;
    ctx_->loopStartStack.push_back(loopStart);
    ctx_->loopBreakStack.push_back({});
    
    auto* cond = stmt.get_condition();
    if (cond) {
        compileExpression(*cond);
        int exitJump = emitJump(bytecode::OpCode::JMP_IF_NOT);
        
        auto* bodyNode = stmt.get_body();
        if (bodyNode && stmt.get_kind() == ast::Statement::Kind::Block) {
            compileBlock(static_cast<const ast::BlockStmt&>(*bodyNode));
        }
        
        auto* inc = stmt.get_increment();
        if (inc) {
            compileExpression(*inc);
            emit(bytecode::OpCode::POP);
        }
        
        emitI(bytecode::OpCode::JMP, loopStart);
        patchJump(exitJump, ctx_->currentFunction->code.size());
    }
    
    for (int breakIdx : ctx_->loopBreakStack.back()) {
        patchJump(breakIdx, ctx_->currentFunction->code.size());
    }
    
    ctx_->inLoop = prevInLoop;
    ctx_->loopStartStack.pop_back();
    ctx_->loopBreakStack.pop_back();
}

void SimpleBytecodeCompiler::compileReturn(const ast::ReturnStmt& stmt) {
    auto* val = stmt.get_value();
    if (val) {
        compileExpression(*val);
    }
    emit(bytecode::OpCode::RET);
}

void SimpleBytecodeCompiler::compileBreak(const ast::BreakStmt& stmt) {
    if (ctx_->inLoop && !ctx_->loopBreakStack.empty()) {
        int jumpIdx = emitJump(bytecode::OpCode::JMP);
        ctx_->loopBreakStack.back().push_back(jumpIdx);
    }
}

void SimpleBytecodeCompiler::compileContinue(const ast::ContinueStmt& stmt) {
    if (ctx_->inLoop && !ctx_->loopStartStack.empty()) {
        emitI(bytecode::OpCode::JMP, ctx_->loopStartStack.back());
    }
}

void SimpleBytecodeCompiler::compileBlock(const ast::BlockStmt& block) {
    enterScope();
    
    const auto& stmts = block.get_statements();
    if (stmts) {
        for (const auto& stmt : *stmts) {
            compileStatement(*stmt);
        }
    }
    
    exitScope();
}

// ============================================================================
// Expression Compilation
// ============================================================================

void SimpleBytecodeCompiler::compileExpression(const ast::Expression& expr) {
    switch (expr.get_kind()) {
        case ast::Expression::Kind::Literal:
            compileLiteral(static_cast<const ast::LiteralExpr&>(expr));
            break;
        case ast::Expression::Kind::Identifier:
            compileIdentifier(static_cast<const ast::IdentifierExpr&>(expr));
            break;
        case ast::Expression::Kind::Binary:
            compileBinary(static_cast<const ast::BinaryExpr&>(expr));
            break;
        case ast::Expression::Kind::Unary:
            compileUnary(static_cast<const ast::UnaryExpr&>(expr));
            break;
        case ast::Expression::Kind::Call:
            compileCall(static_cast<const ast::CallExpr&>(expr));
            break;
        case ast::Expression::Kind::Index:
            compileIndex(static_cast<const ast::IndexExpr&>(expr));
            break;
        case ast::Expression::Kind::Array:
            compileArray(static_cast<const ast::ArrayExpr&>(expr));
            break;
        case ast::Expression::Kind::Tuple:
            compileTuple(static_cast<const ast::TupleExpr&>(expr));
            break;
        default:
            emit(bytecode::OpCode::PUSH);
            emitI(bytecode::OpCode::PUSH, 0);
            break;
    }
}

void SimpleBytecodeCompiler::compileLiteral(const ast::LiteralExpr& expr) {
    emit(bytecode::OpCode::PUSH);
    
    switch (expr.get_lit_kind()) {
        case ast::LiteralExpr::Kind::Nil:
            emitI(bytecode::OpCode::PUSH, 0);
            break;
        case ast::LiteralExpr::Kind::Bool:
            emitI(bytecode::OpCode::PUSH, expr.get_bool_value() ? 1 : 0);
            break;
        case ast::LiteralExpr::Kind::Int:
            emitI(bytecode::OpCode::PUSH, static_cast<int32_t>(expr.get_int_value()));
            break;
        case ast::LiteralExpr::Kind::String: {
            int idx = addConstant(bytecode::Value(expr.get_string_value()));
            emitI(bytecode::OpCode::PUSH, idx);
            break;
        }
        default:
            emitI(bytecode::OpCode::PUSH, 0);
            break;
    }
}

void SimpleBytecodeCompiler::compileIdentifier(const ast::IdentifierExpr& expr) {
    int slot = resolveVariable(expr.get_name());
    if (slot >= 0) {
        emitI(bytecode::OpCode::LOAD_LOCAL, slot);
    } else {
        auto idx = static_cast<int32_t>(module_->global_names.size());
        bool found = false;
        for (size_t i = 0; i < module_->global_names.size(); i++) {
            if (module_->global_names[i] == expr.get_name()) {
                idx = static_cast<int32_t>(i);
                found = true;
                break;
            }
        }
        if (!found) {
            module_->global_names.push_back(expr.get_name());
        }
        emitI(bytecode::OpCode::LOAD_GLOBAL, idx);
    }
}

void SimpleBytecodeCompiler::compileBinary(const ast::BinaryExpr& expr) {
    compileExpression(*expr.get_left());
    compileExpression(*expr.get_right());
    
    switch (expr.get_op()) {
        case ast::BinaryExpr::Kind::Add: emit(bytecode::OpCode::IADD); break;
        case ast::BinaryExpr::Kind::Sub: emit(bytecode::OpCode::ISUB); break;
        case ast::BinaryExpr::Kind::Mul: emit(bytecode::OpCode::IMUL); break;
        case ast::BinaryExpr::Kind::Div: emit(bytecode::OpCode::IDIV); break;
        case ast::BinaryExpr::Kind::Mod: emit(bytecode::OpCode::IMOD); break;
        case ast::BinaryExpr::Kind::Eq: emit(bytecode::OpCode::IEQ); break;
        case ast::BinaryExpr::Kind::Ne: emit(bytecode::OpCode::INE); break;
        case ast::BinaryExpr::Kind::Lt: emit(bytecode::OpCode::ILT); break;
        case ast::BinaryExpr::Kind::Le: emit(bytecode::OpCode::ILE); break;
        case ast::BinaryExpr::Kind::Gt: emit(bytecode::OpCode::IGT); break;
        case ast::BinaryExpr::Kind::Ge: emit(bytecode::OpCode::IGE); break;
        case ast::BinaryExpr::Kind::And: emit(bytecode::OpCode::AND); break;
        case ast::BinaryExpr::Kind::Or: emit(bytecode::OpCode::OR); break;
        default: emit(bytecode::OpCode::IADD); break;
    }
}

void SimpleBytecodeCompiler::compileUnary(const ast::UnaryExpr& expr) {
    compileExpression(*expr.get_operand());
    switch (expr.get_op()) {
        case ast::UnaryExpr::Kind::Neg: emit(bytecode::OpCode::INEG); break;
        case ast::UnaryExpr::Kind::Not: emit(bytecode::OpCode::NOT); break;
        default: break;
    }
}

void SimpleBytecodeCompiler::compileCall(const ast::CallExpr& expr) {
    const auto& args = expr.get_arguments();
    for (auto it = args.rbegin(); it != args.rend(); ++it) {
        compileExpression(**it);
    }
    compileExpression(*expr.get_callee());
    emitI(bytecode::OpCode::CALL, static_cast<int32_t>(args.size()));
}

void SimpleBytecodeCompiler::compileIndex(const ast::IndexExpr& expr) {
    compileExpression(*expr.get_target());
    compileExpression(*expr.get_index());
    emit(bytecode::OpCode::LOAD_INDEX);
}

void SimpleBytecodeCompiler::compileArray(const ast::ArrayExpr& expr) {
    const auto& elems = expr.get_elements();
    for (const auto& elem : elems) {
        compileExpression(*elem);
    }
    emitI(bytecode::OpCode::ALLOC_ARRAY, static_cast<int32_t>(elems.size()));
}

void SimpleBytecodeCompiler::compileTuple(const ast::TupleExpr& expr) {
    const auto& elems = expr.get_elements();
    for (const auto& elem : elems) {
        compileExpression(*elem);
    }
    emitI(bytecode::OpCode::CREATE_TUPLE, static_cast<int32_t>(elems.size()));
}

// ============================================================================
// Scope Management
// ============================================================================

void SimpleBytecodeCompiler::enterScope() {
    ctx_->scopeStack.push_back({});
    ctx_->localSlotCounter++;
}

void SimpleBytecodeCompiler::exitScope() {
    if (!ctx_->scopeStack.empty()) {
        ctx_->scopeStack.pop_back();
    }
}

int SimpleBytecodeCompiler::resolveVariable(const std::string& name) {
    for (auto it = ctx_->scopeStack.rbegin(); it != ctx_->scopeStack.rend(); ++it) {
        auto findIt = it->find(name);
        if (findIt != it->end()) {
            return findIt->second;
        }
    }
    return -1;
}

int SimpleBytecodeCompiler::allocateLocal(const std::string& name) {
    if (ctx_->scopeStack.empty()) {
        enterScope();
    }
    int slot = ctx_->localSlotCounter++;
    ctx_->scopeStack.back()[name] = slot;
    return slot;
}

// ============================================================================
// Instruction Emission
// ============================================================================

void SimpleBytecodeCompiler::emit(bytecode::OpCode op) {
    bytecode::Instruction inst;
    inst.op = op;
    inst.operand1 = 0;
    inst.operand2 = 0;
    ctx_->currentFunction->code.push_back(inst);
}

void SimpleBytecodeCompiler::emitI(bytecode::OpCode op, int32_t operand) {
    bytecode::Instruction inst;
    inst.op = op;
    inst.operand1 = operand;
    inst.operand2 = 0;
    ctx_->currentFunction->code.push_back(inst);
}

void SimpleBytecodeCompiler::emitS(bytecode::OpCode op, const std::string& operand) {
    bytecode::Instruction inst;
    inst.op = op;
    inst.str_operand = operand;
    ctx_->currentFunction->code.push_back(inst);
}

void SimpleBytecodeCompiler::emitF(bytecode::OpCode op, double operand) {
    bytecode::Instruction inst;
    inst.op = op;
    ctx_->currentFunction->code.push_back(inst);
}

int SimpleBytecodeCompiler::emitJump(bytecode::OpCode op) {
    bytecode::Instruction inst;
    inst.op = op;
    inst.operand1 = 0;
    ctx_->currentFunction->code.push_back(inst);
    return static_cast<int>(ctx_->currentFunction->code.size()) - 1;
}

void SimpleBytecodeCompiler::patchJump(int jumpIdx, int targetIdx) {
    if (jumpIdx >= 0 && jumpIdx < static_cast<int>(ctx_->currentFunction->code.size())) {
        ctx_->currentFunction->code[jumpIdx].operand1 = targetIdx;
    }
}

// ============================================================================
// Constant Pool
// ============================================================================

int SimpleBytecodeCompiler::addConstant(const bytecode::Value& value) {
    module_->constants.push_back(value);
    return static_cast<int>(module_->constants.size()) - 1;
}

// ============================================================================
// Error Handling
// ============================================================================

void SimpleBytecodeCompiler::setError(const std::string& msg) {
    lastError_ = msg;
}

} // namespace claw
