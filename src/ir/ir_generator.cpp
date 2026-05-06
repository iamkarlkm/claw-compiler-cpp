// ir_generator.cpp - AST 到 IR 的转换器实现
#include "ir_generator.h"
#include <stdexcept>
#include <iostream>

namespace claw {

IRGenerator::IRGenerator() {
    builder = std::make_shared<ir::IRBuilder>();
}

// ============================================================================
// 主转换入口
// ============================================================================

std::shared_ptr<ir::Module> IRGenerator::generate(ast::Program* program) {
    if (!program) return nullptr;
    
    // 遍历所有顶层声明并生成 IR
    for (const auto& stmt : program->get_declarations()) {
        generate_statement(stmt.get());
    }
    
    return builder->module;
}

// ============================================================================
// 作用域管理
// ============================================================================

void IRGenerator::enter_scope() {
    scope_stack.push({});
}

void IRGenerator::exit_scope() {
    if (!scope_stack.empty()) {
        scope_stack.pop();
    }
}

void IRGenerator::declare_variable(const std::string& name, std::shared_ptr<ir::Value> value) {
    if (!scope_stack.empty()) {
        scope_stack.top()[name] = value;
    }
    locals[name] = value;
}

std::shared_ptr<ir::Value> IRGenerator::lookup_variable(const std::string& name) {
    // 从当前作用域向上查找
    if (!scope_stack.empty()) {
        auto it = scope_stack.top().find(name);
        if (it != scope_stack.top().end()) {
            return it->second;
        }
    }
    // 查找全局 locals
    auto it = locals.find(name);
    if (it != locals.end()) {
        return it->second;
    }
    return nullptr;
}

// ============================================================================
// AST Type → IR Type 映射 (string-based)
// ============================================================================

std::shared_ptr<ir::Type> IRGenerator::map_ast_type(const std::string& type_name) {
    if (type_name.empty()) {
        return builder->get_primitive_type(ir::PrimitiveTypeKind::Void);
    }
    
    if (type_name == "int" || type_name == "i32") {
        return builder->get_primitive_type(ir::PrimitiveTypeKind::Int32);
    } else if (type_name == "i64" || type_name == "long") {
        return builder->get_primitive_type(ir::PrimitiveTypeKind::Int64);
    } else if (type_name == "i16") {
        return builder->get_primitive_type(ir::PrimitiveTypeKind::Int16);
    } else if (type_name == "i8") {
        return builder->get_primitive_type(ir::PrimitiveTypeKind::Int8);
    } else if (type_name == "u8" || type_name == "byte") {
        return builder->get_primitive_type(ir::PrimitiveTypeKind::UInt8);
    } else if (type_name == "u16") {
        return builder->get_primitive_type(ir::PrimitiveTypeKind::UInt16);
    } else if (type_name == "u32") {
        return builder->get_primitive_type(ir::PrimitiveTypeKind::UInt32);
    } else if (type_name == "u64") {
        return builder->get_primitive_type(ir::PrimitiveTypeKind::UInt64);
    } else if (type_name == "float" || type_name == "f32") {
        return builder->get_primitive_type(ir::PrimitiveTypeKind::Float32);
    } else if (type_name == "double" || type_name == "f64") {
        return builder->get_primitive_type(ir::PrimitiveTypeKind::Float64);
    } else if (type_name == "bool") {
        return builder->get_primitive_type(ir::PrimitiveTypeKind::Bool);
    } else if (type_name == "string") {
        return builder->get_primitive_type(ir::PrimitiveTypeKind::String);
    } else if (type_name == "void") {
        return builder->get_primitive_type(ir::PrimitiveTypeKind::Void);
    } else if (type_name == "char") {
        return builder->get_primitive_type(ir::PrimitiveTypeKind::Int8);
    }
    
    // 默认返回 i32
    return builder->get_primitive_type(ir::PrimitiveTypeKind::Int32);
}

// ============================================================================
// 表达式转换
// ============================================================================

std::shared_ptr<ir::Value> IRGenerator::generate_expression(ast::Expression* expr) {
    if (!expr) return nullptr;
    
    switch (expr->get_kind()) {
        case ast::Expression::Kind::Literal:
            return generate_literal(static_cast<ast::LiteralExpr*>(expr));
        case ast::Expression::Kind::Identifier:
            return generate_identifier(static_cast<ast::IdentifierExpr*>(expr));
        case ast::Expression::Kind::Binary:
            return generate_binary_expr(static_cast<ast::BinaryExpr*>(expr));
        case ast::Expression::Kind::Unary:
            return generate_unary_expr(static_cast<ast::UnaryExpr*>(expr));
        case ast::Expression::Kind::Call:
            return generate_call(static_cast<ast::CallExpr*>(expr));
        case ast::Expression::Kind::Index:
            return generate_index(static_cast<ast::IndexExpr*>(expr));
        case ast::Expression::Kind::Member:
            return generate_member(static_cast<ast::MemberExpr*>(expr));
        case ast::Expression::Kind::Array:
            return generate_array_literal(static_cast<ast::ArrayExpr*>(expr));
        case ast::Expression::Kind::Tuple:
            return generate_tuple_literal(static_cast<ast::TupleExpr*>(expr));
        case ast::Expression::Kind::Lambda:
            return generate_lambda(static_cast<ast::LambdaExpr*>(expr));
        default:
            // Slice, Cast, Range, Ref, MutRef, Borrow etc.
            std::cerr << "Warning: unhandled expression kind: "
                      << static_cast<int>(expr->get_kind()) << std::endl;
            return nullptr;
    }
}

std::shared_ptr<ir::Value> IRGenerator::generate_literal(ast::LiteralExpr* lit) {
    if (!lit) return nullptr;
    
    const auto& val = lit->get_value();
    
    return std::visit([this](auto&& v) -> std::shared_ptr<ir::Value> {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, int64_t>) {
            return builder->create_constant(v);
        } else if constexpr (std::is_same_v<T, double>) {
            return builder->create_constant(v);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return builder->create_constant(v);
        } else if constexpr (std::is_same_v<T, bool>) {
            return builder->create_constant(v);
        } else if constexpr (std::is_same_v<T, char>) {
            return builder->create_constant(static_cast<int64_t>(v));
        } else {
            // monostate (null)
            return builder->create_constant(int64_t(0));
        }
    }, val);
}

std::shared_ptr<ir::Value> IRGenerator::generate_identifier(ast::IdentifierExpr* id) {
    if (!id) return nullptr;
    
    auto value = lookup_variable(id->get_name());
    if (!value) {
        std::cerr << "Warning: undefined variable: " << id->get_name() << std::endl;
        return nullptr;
    }
    
    // 如果是指针类型，需要加载
    if (dynamic_cast<ir::PointerType*>(value->type.get())) {
        return builder->create_load(value, id->get_name());
    }
    
    return value;
}

std::shared_ptr<ir::Value> IRGenerator::generate_binary_expr(ast::BinaryExpr* bin) {
    if (!bin) return nullptr;
    
    auto lhs = generate_expression(bin->get_left());
    auto rhs = generate_expression(bin->get_right());
    
    if (!lhs || !rhs) return nullptr;
    
    TokenType op = bin->get_operator();
    
    // 比较运算
    if (op == TokenType::Op_eq || op == TokenType::Op_neq ||
        op == TokenType::Op_lt  || op == TokenType::Op_gt  ||
        op == TokenType::Op_lte || op == TokenType::Op_gte) {
        auto cmp_op = map_comparison_op(op);
        return builder->create_cmp(cmp_op, lhs, rhs);
    }
    
    // 逻辑运算
    if (op == TokenType::Op_and || op == TokenType::Op_or) {
        auto ir_op = map_binary_op(op);
        return builder->create_binary_op(ir_op, lhs, rhs);
    }
    
    // 算术/位运算
    auto ir_op = map_binary_op(op);
    return builder->create_binary_op(ir_op, lhs, rhs);
}

std::shared_ptr<ir::Value> IRGenerator::generate_unary_expr(ast::UnaryExpr* un) {
    if (!un) return nullptr;
    
    auto operand = generate_expression(un->get_operand());
    if (!operand) return nullptr;
    
    auto op = map_unary_op(un->get_operator());
    return builder->create_unary_op(op, operand);
}

std::shared_ptr<ir::Value> IRGenerator::generate_call(ast::CallExpr* call) {
    if (!call) return nullptr;
    
    std::vector<std::shared_ptr<ir::Value>> args;
    for (const auto& arg : call->get_arguments()) {
        auto arg_val = generate_expression(arg.get());
        if (arg_val) args.push_back(arg_val);
    }
    
    // 获取被调用函数名
    std::string callee_name;
    if (auto* callee_id = dynamic_cast<ast::IdentifierExpr*>(call->get_callee())) {
        callee_name = callee_id->get_name();
    } else if (auto* callee_member = dynamic_cast<ast::MemberExpr*>(call->get_callee())) {
        // method call: obj.method(args) — generate as "obj_method"
        // For now, use a simplified representation
        callee_name = callee_member->get_member();
    } else {
        callee_name = "__unknown_call";
    }
    
    return builder->create_call(callee_name, args);
}

std::shared_ptr<ir::Value> IRGenerator::generate_index(ast::IndexExpr* idx) {
    if (!idx) return nullptr;
    
    auto base = generate_expression(idx->get_object());
    if (!base) return nullptr;
    
    auto index = generate_expression(idx->get_index());
    if (!index) return nullptr;
    
    // 简化实现：假设 base 是指针，计算地址
    // TODO: 实现正确的 GEP
    return base;
}

std::shared_ptr<ir::Value> IRGenerator::generate_member(ast::MemberExpr* member) {
    if (!member) return nullptr;
    
    auto obj = generate_expression(member->get_object());
    if (!obj) return nullptr;
    
    // 简化处理：成员访问暂不实现完整 GEP
    // TODO: 实现 struct field access
    std::cerr << "Warning: member access not fully implemented: " 
              << member->get_member() << std::endl;
    return obj;
}

// generate_array_literal/generate_tuple_literal/generate_lambda moved to ir_generator_enhanced.cpp

// ============================================================================
// 语句转换
// ============================================================================

void IRGenerator::generate_statement(ast::Statement* stmt) {
    if (!stmt) return;
    
    switch (stmt->get_kind()) {
        case ast::Statement::Kind::Function:
            generate_function_decl(static_cast<ast::FunctionStmt*>(stmt));
            break;
        case ast::Statement::Kind::Let:
            generate_let(static_cast<ast::LetStmt*>(stmt));
            break;
        case ast::Statement::Kind::Const: {
            // Treat const like let
            auto* const_stmt = static_cast<ast::ConstStmt*>(stmt);
            if (const_stmt->get_initializer()) {
                auto value = generate_expression(const_stmt->get_initializer());
                if (value) {
                    std::shared_ptr<ir::Type> var_type = value->type;
                    if (!const_stmt->get_type().empty()) {
                        var_type = map_ast_type(const_stmt->get_type());
                    }
                    auto alloca = builder->create_alloca(var_type, 1, const_stmt->get_name());
                    builder->create_store(value, alloca);
                    declare_variable(const_stmt->get_name(), alloca);
                }
            }
            break;
        }
        case ast::Statement::Kind::Assign:
            generate_assign(static_cast<ast::AssignStmt*>(stmt));
            break;
        case ast::Statement::Kind::Return:
            generate_return(static_cast<ast::ReturnStmt*>(stmt));
            break;
        case ast::Statement::Kind::If:
            generate_if(static_cast<ast::IfStmt*>(stmt));
            break;
        case ast::Statement::Kind::Block:
            generate_block(static_cast<ast::BlockStmt*>(stmt));
            break;
        case ast::Statement::Kind::Expression:
            generate_expr_stmt(static_cast<ast::ExprStmt*>(stmt));
            break;
        case ast::Statement::Kind::Match:
            generate_match(static_cast<ast::MatchStmt*>(stmt));
            break;
        case ast::Statement::Kind::For:
            generate_for(static_cast<ast::ForStmt*>(stmt));
            break;
        case ast::Statement::Kind::While:
            generate_while(static_cast<ast::WhileStmt*>(stmt));
            break;
        case ast::Statement::Kind::Break:
            generate_break();
            break;
        case ast::Statement::Kind::Continue:
            generate_continue();
            break;
        case ast::Statement::Kind::Publish:
            generate_publish(static_cast<ast::PublishStmt*>(stmt));
            break;
        case ast::Statement::Kind::Subscribe:
            generate_subscribe(static_cast<ast::SubscribeStmt*>(stmt));
            break;
        case ast::Statement::Kind::Try: {
            // Simplified: just generate the try body
            auto* try_stmt = static_cast<ast::TryStmt*>(stmt);
            if (try_stmt->get_body()) {
                generate_statement(try_stmt->get_body());
            }
            break;
        }
        case ast::Statement::Kind::Throw: {
            auto* throw_stmt = static_cast<ast::ThrowStmt*>(stmt);
            if (throw_stmt->get_value()) {
                generate_expression(throw_stmt->get_value());
            }
            builder->create_panic("throw");
            break;
        }
        case ast::Statement::Kind::Import:
        case ast::Statement::Kind::Export:
        case ast::Statement::Kind::Module:
        case ast::Statement::Kind::Struct:
        case ast::Statement::Kind::Enum:
        case ast::Statement::Kind::Trait:
        case ast::Statement::Kind::Impl:
        case ast::Statement::Kind::TypeAlias:
        case ast::Statement::Kind::SerialProcess:
            // TODO: implement these statement types
            break;
        default:
            break;
    }
}

void IRGenerator::generate_function_decl(ast::FunctionStmt* decl) {
    if (!decl) return;
    
    // 创建函数类型
    auto ret_type = map_ast_type(decl->get_return_type());
    auto func = builder->create_function(decl->get_name(), ret_type);
    
    // 保存函数映射
    functions[decl->get_name()] = func;
    
    // 设置当前函数
    current_function = func;
    
    // 创建入口块
    entry_block = builder->create_block("entry");
    current_block = entry_block;
    builder->set_insert_point(entry_block);
    
    // 处理参数
    enter_scope();
    for (const auto& param : decl->get_params()) {
        const std::string& param_name = param.first;
        const std::string& param_type_name = param.second;
        auto param_type = map_ast_type(param_type_name);
        
        // 为参数创建 alloca
        auto alloca = builder->create_alloca(param_type, 1, param_name);
        declare_variable(param_name, alloca);
        
        // 将参数存储到 alloca
        // TODO: 正确处理参数传递
    }
    
    // 生成函数体
    if (decl->get_body()) {
        // Body is an ASTNode; if it's a BlockStmt, use generate_block
        if (auto* body_block = dynamic_cast<ast::BlockStmt*>(decl->get_body())) {
            generate_block(body_block);
        } else {
            // Body might be a single statement
            if (auto* body_stmt = dynamic_cast<ast::Statement*>(decl->get_body())) {
                generate_statement(body_stmt);
            }
        }
    }
    
    // 如果没有返回指令，添加 void return
    if (!current_block || !current_block->terminator) {
        builder->create_ret_void();
    }
    
    exit_scope();
}

void IRGenerator::generate_return(ast::ReturnStmt* ret) {
    if (!ret) return;
    
    std::shared_ptr<ir::Value> ret_val = nullptr;
    if (ret->get_value()) {
        ret_val = generate_expression(ret->get_value());
    }
    
    builder->create_ret(ret_val);
}

void IRGenerator::generate_let(ast::LetStmt* let) {
    if (!let) return;
    
    auto value = generate_expression(let->get_initializer());
    if (!value) return;
    
    // 确定类型
    std::shared_ptr<ir::Type> var_type = value->type;
    if (!let->get_type().empty()) {
        var_type = map_ast_type(let->get_type());
    }
    
    // 创建局部变量 (alloca)
    auto alloca = builder->create_alloca(var_type, 1, let->get_name());
    
    // 存储初始值
    builder->create_store(value, alloca);
    
    // 声明变量
    declare_variable(let->get_name(), alloca);
}

void IRGenerator::generate_assign(ast::AssignStmt* assign) {
    if (!assign) return;
    
    auto value = generate_expression(assign->get_value());
    if (!value) return;
    
    // 查找目标变量
    // target is an Expression — typically an IdentifierExpr
    auto* target_expr = assign->get_target();
    if (auto* ident = dynamic_cast<ast::IdentifierExpr*>(target_expr)) {
        auto target = lookup_variable(ident->get_name());
        if (!target) {
            std::cerr << "Error: assignment to undefined variable: " 
                      << ident->get_name() << std::endl;
            return;
        }
        builder->create_store(value, target);
    } else if (auto* idx_expr = dynamic_cast<ast::IndexExpr*>(target_expr)) {
        // Index assignment: arr[i] = value
        auto base = generate_expression(idx_expr->get_object());
        auto index = generate_expression(idx_expr->get_index());
        // TODO: implement indexed store
        std::cerr << "Warning: indexed assignment not fully implemented" << std::endl;
    } else if (dynamic_cast<ast::MemberExpr*>(target_expr)) {
        // Member assignment: obj.field = value
        // TODO: implement member store
        std::cerr << "Warning: member assignment not fully implemented" << std::endl;
    }
}

void IRGenerator::generate_if(ast::IfStmt* if_stmt) {
    if (!if_stmt) return;
    
    const auto& conditions = if_stmt->get_conditions();
    const auto& bodies = if_stmt->get_bodies();
    auto* else_body = if_stmt->get_else_body();
    
    if (conditions.empty()) return;
    
    auto merge_block = create_block("if.end");
    
    // Handle if / else if / else chain
    for (size_t i = 0; i < conditions.size(); ++i) {
        auto cond = generate_expression(conditions[i].get());
        if (!cond) {
            builder->create_br(merge_block);
            builder->set_insert_point(merge_block);
            return;
        }
        
        auto then_block = create_block("if.then." + std::to_string(i));
        auto next_block = (i < conditions.size() - 1 || else_body)
            ? create_block("if.next." + std::to_string(i))
            : merge_block;
        
        builder->create_cond_br(cond, then_block, next_block);
        
        // Generate then body
        builder->set_insert_point(then_block);
        enter_scope();
        if (auto* body_stmt = dynamic_cast<ast::BlockStmt*>(bodies[i].get())) {
            generate_block(body_stmt);
        } else if (auto* stmt = dynamic_cast<ast::Statement*>(bodies[i].get())) {
            generate_statement(stmt);
        }
        if (!current_block->terminator) {
            builder->create_br(merge_block);
        }
        exit_scope();
        
        builder->set_insert_point(next_block);
    }
    
    // Generate else body
    if (else_body) {
        enter_scope();
        if (auto* else_block = dynamic_cast<ast::BlockStmt*>(else_body)) {
            generate_block(else_block);
        } else if (auto* stmt = dynamic_cast<ast::Statement*>(else_body)) {
            generate_statement(stmt);
        }
        if (!current_block->terminator) {
            builder->create_br(merge_block);
        }
        exit_scope();
    }
    
    builder->set_insert_point(merge_block);
}

void IRGenerator::generate_for(ast::ForStmt* for_loop) {
    if (!for_loop) return;
    
    // for var in iterable { body }
    std::string loop_var = for_loop->get_variable();
    auto* iterable = for_loop->get_iterable();
    auto* body = for_loop->get_body();
    
    if (!iterable || !body) return;
    
    // 创建循环块
    auto cond_block = create_block("for.cond");
    auto body_block = create_block("for.body");
    auto after_block = create_block("for.after");
    
    // 获取迭代范围或数组
    auto iter_val = generate_expression(iterable);
    if (!iter_val) return;
    
    // 判断迭代类型：范围 (BinaryExpr with Op_range/Op_range_eq) 或其他
    if (auto* bin_expr = dynamic_cast<ast::BinaryExpr*>(iterable)) {
        TokenType op = bin_expr->get_operator();
        if (op == TokenType::Op_range || op == TokenType::Op_range_eq) {
            // 范围表达式: start..end 或 start..=end
            auto start_val = generate_expression(bin_expr->get_left());
            auto end_val = generate_expression(bin_expr->get_right());
            
            if (!start_val || !end_val) return;
            
            // 创建循环计数器
            auto i32_type = builder->get_primitive_type(ir::PrimitiveTypeKind::Int32);
            auto counter = builder->create_alloca(i32_type, 1, loop_var + ".ptr");
            
            // 存储起始值
            builder->create_store(start_val, counter);
            
            // 保存循环上下文
            LoopContext ctx{cond_block, body_block, after_block};
            loop_stack.push_back(ctx);
            
            // 跳转到条件块
            builder->create_br(cond_block);
            
            // 生成条件块
            builder->set_insert_point(cond_block);
            auto count_val = builder->create_load(counter, loop_var + ".current");
            
            // 比较: counter < end (或 <= end for inclusive)
            auto cmp_op = (op == TokenType::Op_range_eq) 
                ? ir::OpCode::Le : ir::OpCode::Lt;
            auto cond = builder->create_cmp(cmp_op, count_val, end_val);
            builder->create_cond_br(cond, body_block, after_block);
            
            // 生成循环体
            builder->set_insert_point(body_block);
            enter_scope();
            
            // 加载当前计数值并声明循环变量
            auto curr_val = builder->create_load(counter, loop_var);
            declare_variable(loop_var, curr_val);
            
            // 生成循环体语句
            if (auto* block_stmt = dynamic_cast<ast::BlockStmt*>(body)) {
                generate_block(block_stmt);
            }
            
            // 递增计数器
            if (!current_block->terminator) {
                auto count_val2 = builder->create_load(counter, loop_var + ".next");
                auto one = builder->create_constant(int64_t(1));
                auto next = builder->create_add(count_val2, one);
                builder->create_store(next, counter);
                builder->create_br(cond_block);
            }
            exit_scope();
            
            // 设置 after 块
            builder->set_insert_point(after_block);
            loop_stack.pop_back();
            return;
        }
    }
    
    // TODO: 支持数组/张量迭代 (需要运行时迭代器支持)
    std::cerr << "Warning: For loop with non-range iterable not fully implemented" << std::endl;
    
    // 简化处理: 跳过循环体
    builder->create_br(after_block);
    builder->set_insert_point(after_block);
}

void IRGenerator::generate_while(ast::WhileStmt* while_loop) {
    if (!while_loop) return;
    
    // while condition { body }
    auto* condition = while_loop->get_condition();
    auto* body = while_loop->get_body();
    
    if (!condition || !body) return;
    
    // 创建循环块
    auto cond_block = create_block("while.cond");
    auto body_block = create_block("while.body");
    auto after_block = create_block("while.after");
    
    // 保存循环上下文
    LoopContext ctx{cond_block, body_block, after_block};
    loop_stack.push_back(ctx);
    
    // 跳转到条件块
    builder->create_br(cond_block);
    
    // 生成条件
    builder->set_insert_point(cond_block);
    auto cond = generate_expression(condition);
    if (cond) {
        builder->create_cond_br(cond, body_block, after_block);
    } else {
        builder->create_br(after_block);
    }
    
    // 生成循环体
    builder->set_insert_point(body_block);
    enter_scope();
    
    if (auto* block_stmt = dynamic_cast<ast::BlockStmt*>(body)) {
        generate_block(block_stmt);
    }
    
    if (!current_block->terminator) {
        builder->create_br(cond_block);
    }
    exit_scope();
    
    // 设置 after 块
    builder->set_insert_point(after_block);
    loop_stack.pop_back();
}

void IRGenerator::generate_break() {
    if (loop_stack.empty()) {
        std::cerr << "Error: break outside loop" << std::endl;
        return;
    }
    builder->create_br(loop_stack.back().after_block);
}

void IRGenerator::generate_continue() {
    if (loop_stack.empty()) {
        std::cerr << "Error: continue outside loop" << std::endl;
        return;
    }
    builder->create_br(loop_stack.back().condition_block);
}

void IRGenerator::generate_block(ast::BlockStmt* block) {
    if (!block) return;
    
    enter_scope();
    for (const auto& stmt : block->get_statements()) {
        generate_statement(stmt.get());
        if (current_block && current_block->terminator) break;
    }
    exit_scope();
}

void IRGenerator::generate_expr_stmt(ast::ExprStmt* expr_stmt) {
    if (!expr_stmt) return;
    generate_expression(expr_stmt->get_expr());
}

void IRGenerator::generate_match(ast::MatchStmt* match) {
    if (!match) return;
    
    // match expr { pattern1 => body1, pattern2 => body2, _ => default }
    auto* match_expr = match->get_expr();
    const auto& patterns = match->get_patterns();
    const auto& bodies = match->get_bodies();
    
    if (!match_expr || patterns.empty()) return;
    
    // 生成匹配表达式
    auto expr_val = generate_expression(match_expr);
    if (!expr_val) return;
    
    // 创建块
    std::vector<std::shared_ptr<ir::BasicBlock>> case_blocks;
    std::shared_ptr<ir::BasicBlock> default_block = nullptr;
    std::shared_ptr<ir::BasicBlock> after_block = create_block("match.after");
    
    // 为每个 case 创建基本块
    for (size_t i = 0; i < patterns.size(); ++i) {
        // 检查是否是通配符模式 (_)
        bool is_wildcard = false;
        if (auto* ident = dynamic_cast<ast::IdentifierExpr*>(patterns[i].get())) {
            if (ident->get_name() == "_") {
                is_wildcard = true;
            }
        }
        
        if (is_wildcard) {
            default_block = create_block("match.default");
            case_blocks.push_back(default_block);
        } else {
            case_blocks.push_back(create_block("match.case." + std::to_string(i)));
        }
    }
    
    // 如果没有通配符，创建 after 块作为默认目标
    if (!default_block) {
        default_block = after_block;
    }
    
    // 生成条件分支链
    auto current_cond_block = create_block("match.start");
    builder->create_br(current_cond_block);
    builder->set_insert_point(current_cond_block);
    
    for (size_t i = 0; i < patterns.size(); ++i) {
        auto pattern_val = generate_expression(patterns[i].get());
        
        // 比较: expr == pattern
        auto cmp = builder->create_cmp(ir::OpCode::Eq, expr_val, pattern_val);
        
        // 决定目标块
        std::shared_ptr<ir::BasicBlock> next_target;
        if (i < patterns.size() - 1) {
            next_target = case_blocks[i + 1];
        } else {
            next_target = default_block;
        }
        
        builder->create_cond_br(cmp, case_blocks[i], next_target);
        
        // 生成 case 体
        builder->set_insert_point(case_blocks[i]);
        enter_scope();
        
        if (auto* block_stmt = dynamic_cast<ast::BlockStmt*>(bodies[i].get())) {
            generate_block(block_stmt);
        } else if (auto* stmt = dynamic_cast<ast::Statement*>(bodies[i].get())) {
            generate_statement(stmt);
        }
        
        if (!current_block->terminator) {
            builder->create_br(after_block);
        }
        exit_scope();
    }
    
    // 设置 after 块
    builder->set_insert_point(after_block);
}

// ============================================================================
// 类型和操作码映射
// ============================================================================

ir::OpCode IRGenerator::map_binary_op(TokenType op) {
    switch (op) {
        case TokenType::Op_plus:    return ir::OpCode::Add;
        case TokenType::Op_minus:   return ir::OpCode::Sub;
        case TokenType::Op_star:    return ir::OpCode::Mul;
        case TokenType::Op_slash:   return ir::OpCode::Div;
        case TokenType::Op_percent: return ir::OpCode::Mod;
        case TokenType::Op_amp:     return ir::OpCode::BitAnd;
        case TokenType::Op_pipe:    return ir::OpCode::BitOr;
        case TokenType::Op_caret:   return ir::OpCode::BitXor;
        case TokenType::Op_and:     return ir::OpCode::And;
        case TokenType::Op_or:      return ir::OpCode::Or;
        default:                    return ir::OpCode::Add;
    }
}

ir::OpCode IRGenerator::map_unary_op(TokenType op) {
    switch (op) {
        case TokenType::Op_minus: return ir::OpCode::Sub;
        case TokenType::Op_bang:  return ir::OpCode::Not;
        case TokenType::Op_tilde: return ir::OpCode::BitNot;
        default:                  return ir::OpCode::Not;
    }
}

ir::OpCode IRGenerator::map_comparison_op(TokenType op) {
    switch (op) {
        case TokenType::Op_eq:  return ir::OpCode::Eq;
        case TokenType::Op_neq: return ir::OpCode::Ne;
        case TokenType::Op_lt:  return ir::OpCode::Lt;
        case TokenType::Op_lte: return ir::OpCode::Le;
        case TokenType::Op_gt:  return ir::OpCode::Gt;
        case TokenType::Op_gte: return ir::OpCode::Ge;
        default:                return ir::OpCode::Eq;
    }
}

std::shared_ptr<ir::BasicBlock> IRGenerator::create_block(const std::string& name) {
    return builder->create_block(name);
}

// ============================================================================
// 事件系统支持
// ============================================================================

void IRGenerator::generate_publish(ast::PublishStmt* publish) {
    if (!publish) return;
    
    // publish event_name(arg1, arg2, ...)
    std::string event_name = publish->get_event_name();
    const auto& args = publish->get_arguments();
    
    // 生成参数值
    std::vector<std::shared_ptr<ir::Value>> arg_vals;
    for (const auto& arg : args) {
        auto val = generate_expression(arg.get());
        if (val) arg_vals.push_back(val);
    }
    
    // 创建事件调用 (通过运行时函数)
    std::string func_name = "__claw_event_publish_" + event_name;
    builder->create_call(func_name, arg_vals);
}

void IRGenerator::generate_subscribe(ast::SubscribeStmt* sub) {
    if (!sub) return;
    
    // subscribe event_name { handler }
    std::string event_name = sub->get_event_name();
    auto* handler = sub->get_handler();
    
    if (!handler) return;
    
    // 生成事件处理函数
    auto handler_func = generate_function_handler(handler, event_name);
    
    // 创建订阅调用 — passing function name as string
    std::string func_name = "__claw_event_subscribe_" + event_name;
    std::vector<std::shared_ptr<ir::Value>> args;
    // Pass handler name as a string constant
    args.push_back(builder->create_constant(handler_func->name));
    builder->create_call(func_name, args);
}

std::shared_ptr<ir::Function> IRGenerator::generate_function_handler(
    ast::FunctionStmt* handler, 
    const std::string& event_name) {
    if (!handler) return nullptr;
    
    // 为事件处理函数创建包装
    std::string handler_name = "__claw_handler_" + event_name;
    auto ret_type = builder->get_primitive_type(ir::PrimitiveTypeKind::Void);
    auto func = builder->create_function(handler_name, ret_type);
    
    auto entry = builder->create_block("entry");
    builder->set_insert_point(entry);
    
    // 生成处理函数体
    if (handler->get_body()) {
        if (auto* body_block = dynamic_cast<ast::BlockStmt*>(handler->get_body())) {
            generate_block(body_block);
        }
    }
    
    // 确保有返回
    if (!current_block || !current_block->terminator) {
        builder->create_ret_void();
    }
    
    return func;
}

} // namespace claw
