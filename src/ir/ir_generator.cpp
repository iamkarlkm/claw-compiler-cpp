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

std::shared_ptr<ir::Module> IRGenerator::generate(std::shared_ptr<ast::Module> ast) {
    if (!ast) return nullptr;
    
    // 遍历所有语句并生成 IR
    for (auto& stmt : ast->statements) {
        if (auto fn = std::dynamic_pointer_cast<ast::FunctionDecl>(stmt)) {
            generate_function_decl(fn);
        } else if (auto let_stmt = std::dynamic_pointer_cast<ast::LetStmt>(stmt)) {
            generate_let(let_stmt);
        } else if (auto expr_stmt = std::dynamic_pointer_cast<ast::ExpressionStmt>(stmt)) {
            generate_expr_stmt(expr_stmt);
        }
        // TODO: 其他顶层语句类型
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
// 表达式转换
// ============================================================================

std::shared_ptr<ir::Value> IRGenerator::generate_expression(
    std::shared_ptr<ast::Expression> expr) {
    if (!expr) return nullptr;
    
    // 字面量
    if (auto lit = std::dynamic_pointer_cast<ast::LiteralExpr>(expr)) {
        return generate_literal(lit);
    }
    
    // 标识符
    if (auto id = std::dynamic_pointer_cast<ast::IdentifierExpr>(expr)) {
        return generate_identifier(id);
    }
    
    // 二元表达式
    if (auto bin = std::dynamic_pointer_cast<ast::BinaryExpr>(expr)) {
        return generate_binary_expr(bin);
    }
    
    // 一元表达式
    if (auto un = std::dynamic_pointer_cast<ast::UnaryExpr>(expr)) {
        return generate_unary_expr(un);
    }
    
    // 函数调用
    if (auto call = std::dynamic_pointer_cast<ast::CallExpr>(expr)) {
        return generate_call(call);
    }
    
    // 下标表达式
    if (auto idx = std::dynamic_pointer_cast<ast::IndexExpr>(expr)) {
        return generate_index(idx);
    }
    
    // TODO: 其他表达式类型
    
    return nullptr;
}

std::shared_ptr<ir::Value> IRGenerator::generate_literal(
    std::shared_ptr<ast::LiteralExpr> lit) {
    if (!lit) return nullptr;
    
    switch (lit->literal_type) {
        case ast::LiteralType::Integer:
            return builder->create_constant(lit->int_value);
        case ast::LiteralType::Float:
            return builder->create_constant(lit->float_value);
        case ast::LiteralType::String:
            return builder->create_constant(lit->string_value);
        case ast::LiteralType::Boolean:
            return builder->create_constant(lit->bool_value);
        case ast::LiteralType::Bytes:
            // TODO: 字节字面量
            break;
    }
    
    return nullptr;
}

std::shared_ptr<ir::Value> IRGenerator::generate_identifier(
    std::shared_ptr<ast::IdentifierExpr> id) {
    if (!id) return nullptr;
    
    auto value = lookup_variable(id->name);
    if (!value) {
        std::cerr << "Warning: undefined variable: " << id->name << std::endl;
        return nullptr;
    }
    
    // 如果是指针类型，需要加载
    if (dynamic_cast<ir::PointerType*>(value->type.get())) {
        return builder->create_load(value, id->name);
    }
    
    return value;
}

std::shared_ptr<ir::Value> IRGenerator::generate_binary_expr(
    std::shared_ptr<ast::BinaryExpr> bin) {
    if (!bin) return nullptr;
    
    auto lhs = generate_expression(bin->lhs);
    auto rhs = generate_expression(bin->rhs);
    
    if (!lhs || !rhs) return nullptr;
    
    // 比较运算
    if (bin->is_comparison) {
        auto cmp_op = map_comparison_op(bin->op);
        return builder->create_cmp(cmp_op, lhs, rhs);
    }
    
    // 算术/逻辑运算
    auto op = map_binary_op(bin->op);
    return builder->create_binary_op(op, lhs, rhs);
}

std::shared_ptr<ir::Value> IRGenerator::generate_unary_expr(
    std::shared_ptr<ast::UnaryExpr> un) {
    if (!un) return nullptr;
    
    auto operand = generate_expression(un->operand);
    if (!operand) return nullptr;
    
    auto op = map_unary_op(un->op);
    return builder->create_unary_op(op, operand);
}

std::shared_ptr<ir::Value> IRGenerator::generate_call(
    std::shared_ptr<ast::CallExpr> call) {
    if (!call) return nullptr;
    
    std::vector<std::shared_ptr<ir::Value>> args;
    for (auto& arg : call->arguments) {
        auto arg_val = generate_expression(arg);
        if (arg_val) args.push_back(arg_val);
    }
    
    return builder->create_call(call->function_name, args);
}

std::shared_ptr<ir::Value> IRGenerator::generate_index(
    std::shared_ptr<ast::IndexExpr> idx) {
    if (!idx) return nullptr;
    
    auto base = generate_expression(idx->base);
    if (!base) return nullptr;
    
    // 生成下标计算
    auto index = generate_expression(idx->index);
    if (!index) return nullptr;
    
    // 简化实现：假设 base 是指针，计算地址
    // 实际需要考虑元素大小
    // TODO: 实现正确的 GEP
    
    // 暂时返回 base（简化处理）
    return base;
}

// ============================================================================
// 语句转换
// ============================================================================

void IRGenerator::generate_statement(std::shared_ptr<ast::Statement> stmt) {
    if (!stmt) return;
    
    if (auto fn = std::dynamic_pointer_cast<ast::FunctionDecl>(stmt)) {
        generate_function_decl(fn);
    } else if (auto let_stmt = std::dynamic_pointer_cast<ast::LetStmt>(stmt)) {
        generate_let(let_stmt);
    } else if (auto assign = std::dynamic_pointer_cast<ast::AssignStmt>(stmt)) {
        generate_assign(assign);
    } else if (auto ret = std::dynamic_pointer_cast<ast::ReturnStmt>(stmt)) {
        generate_return(ret);
    } else if (auto if_stmt = std::dynamic_pointer_cast<ast::IfStmt>(stmt)) {
        generate_if(if_stmt);
    } else if (auto block = std::dynamic_pointer_cast<ast::BlockStmt>(stmt)) {
        generate_block(block);
    } else if (auto expr_stmt = std::dynamic_pointer_cast<ast::ExpressionStmt>(stmt)) {
        generate_expr_stmt(expr_stmt);
    } else if (auto match = std::dynamic_pointer_cast<ast::MatchStmt>(stmt)) {
        generate_match(match);
    } else if (auto for_stmt = std::dynamic_pointer_cast<ast::ForStmt>(stmt)) {
        generate_for(for_stmt);
    } else if (auto while_stmt = std::dynamic_pointer_cast<ast::WhileStmt>(stmt)) {
        generate_while(while_stmt);
    } else if (auto loop_stmt = std::dynamic_pointer_cast<ast::LoopStmt>(stmt)) {
        generate_loop(loop_stmt);
    } else if (auto break_stmt = std::dynamic_pointer_cast<ast::BreakStmt>(stmt)) {
        generate_break();
    } else if (auto cont_stmt = std::dynamic_pointer_cast<ast::ContinueStmt>(stmt)) {
        generate_continue();
    } else if (auto publish = std::dynamic_pointer_cast<ast::PublishStmt>(stmt)) {
        generate_publish(publish);
    } else if (auto sub = std::dynamic_pointer_cast<ast::SubscribeStmt>(stmt)) {
        generate_subscribe(sub);
    }
}

void IRGenerator::generate_function_decl(std::shared_ptr<ast::FunctionDecl> decl) {
    if (!decl) return;
    
    // 创建函数类型
    auto ret_type = map_ast_type(decl->return_type);
    auto func = builder->create_function(decl->name, ret_type);
    
    // 保存函数映射
    functions[decl->name] = func;
    
    // 设置当前函数
    current_function = func;
    
    // 创建入口块
    entry_block = builder->create_block("entry");
    builder->set_insert_point(entry_block);
    
    // 处理参数
    enter_scope();
    for (size_t i = 0; i < decl->params.size(); ++i) {
        auto& param = decl->params[i];
        auto param_type = map_ast_type(param.type);
        
        // 为参数创建 alloca
        auto alloca = builder->create_alloca(param_type, 1, param.name);
        declare_variable(param.name, alloca);
        
        // 将参数存储到 alloca
        // TODO: 正确处理参数传递
    }
    
    // 生成函数体
    if (decl->body) {
        generate_block(decl->body);
    }
    
    // 如果没有返回指令，添加 void return
    if (!current_block || !current_block->terminator) {
        builder->create_ret_void();
    }
    
    exit_scope();
}

void IRGenerator::generate_return(std::shared_ptr<ast::ReturnStmt> ret) {
    if (!ret) return;
    
    std::shared_ptr<ir::Value> ret_val = nullptr;
    if (ret->value) {
        ret_val = generate_expression(ret->value);
    }
    
    builder->create_ret(ret_val);
}

void IRGenerator::generate_let(std::shared_ptr<ast::LetStmt> let) {
    if (!let) return;
    
    auto value = generate_expression(let->value);
    if (!value) return;
    
    // 确定类型
    std::shared_ptr<ir::Type> var_type = value->type;
    if (let->type) {
        var_type = map_ast_type(let->type);
    }
    
    // 创建局部变量 (alloca)
    auto alloca = builder->create_alloca(var_type, 1, let->name);
    
    // 存储初始值
    builder->create_store(value, alloca);
    
    // 声明变量
    declare_variable(let->name, alloca);
}

void IRGenerator::generate_assign(std::shared_ptr<ast::AssignStmt> assign) {
    if (!assign) return;
    
    auto value = generate_expression(assign->value);
    if (!value) return;
    
    // 查找目标变量
    auto target = lookup_variable(assign->target->name);
    if (!target) {
        std::cerr << "Error: assignment to undefined variable: " 
                  << assign->target->name << std::endl;
        return;
    }
    
    // 存储值
    builder->create_store(value, target);
}

void IRGenerator::generate_if(std::shared_ptr<ast::IfStmt> if_stmt) {
    if (!if_stmt) return;
    
    auto cond = generate_expression(if_stmt->condition);
    if (!cond) return;
    
    // 创建基本块
    auto then_block = create_block("if.then");
    auto else_block = create_block("if.else");
    auto merge_block = create_block("if.end");
    
    // 条件分支
    builder->create_cond_br(cond, then_block, else_block);
    
    // 生成 then 分支
    builder->set_insert_point(then_block);
    if (if_stmt->then_branch) {
        generate_block(if_stmt->then_branch);
    }
    if (!current_block->terminator) {
        builder->create_br(merge_block);
    }
    
    // 生成 else 分支
    builder->set_insert_point(else_block);
    if (if_stmt->else_branch) {
        generate_block(if_stmt->else_branch);
    }
    if (!current_block->terminator) {
        builder->create_br(merge_block);
    }
    
    // 设置合并点
    builder->set_insert_point(merge_block);
}

void IRGenerator::generate_loop(std::shared_ptr<ast::LoopStmt> loop) {
    if (!loop) return;
    
    // 创建基本块
    auto cond_block = create_block("loop.cond");
    auto body_block = create_block("loop.body");
    auto after_block = create_block("loop.end");
    
    // 保存循环上下文
    LoopContext ctx{cond_block, body_block, after_block};
    loop_stack.push_back(ctx);
    
    // 跳转到条件块
    builder->create_br(cond_block);
    
    // 生成条件
    builder->set_insert_point(cond_block);
    if (loop->condition) {
        auto cond = generate_expression(loop->condition);
        builder->create_cond_br(cond, body_block, after_block);
    } else {
        // 无限循环
        builder->create_br(body_block);
    }
    
    // 生成循环体
    builder->set_insert_point(body_block);
    enter_scope();
    if (loop->body) {
        generate_block(loop->body);
    }
    if (!current_block->terminator) {
        builder->create_br(cond_block);
    }
    exit_scope();
    
    // 设置 after 块
    builder->set_insert_point(after_block);
    
    loop_stack.pop_back();
}

void IRGenerator::generate_for(std::shared_ptr<ast::ForStmt> for_loop) {
    if (!for_loop) return;
    
    // for var in iterable { body }
    // 支持 range: 0..n 或 0..=n (inclusive)
    // 支持数组/张量迭代
    
    std::string loop_var = for_loop->get_variable();
    auto iterable = for_loop->get_iterable();
    auto body = for_loop->get_body();
    
    if (!iterable || !body) return;
    
    // 创建循环块
    auto cond_block = create_block("for.cond");
    auto body_block = create_block("for.body");
    auto after_block = create_block("for.after");
    
    // 获取迭代范围或数组
    auto iter_val = generate_expression(iterable);
    if (!iter_val) return;
    
    // 判断迭代类型：范围 (BinaryExpr) 或其他
    if (auto bin_expr = std::dynamic_pointer_cast<ast::BinaryExpr>(
            std::const_pointer_cast<ast::Expression>(
                std::shared_ptr<ast::Expression>(iterable, iterable.get())))) {
        // 范围表达式: start..end 或 start..=end
        if (bin_expr->op == ast::BinaryOp::Range || 
            bin_expr->op == ast::BinaryOp::RangeInclusive) {
            
            auto start_val = generate_expression(bin_expr->left);
            auto end_val = generate_expression(bin_expr->right);
            
            if (!start_val || !end_val) return;
            
            // 创建循环计数器 (使用 i32)
            auto i32_type = builder->get_primitive_type(ir::PrimitiveTypeKind::Int32);
            auto counter = builder->create_alloca(i32_type, 1, loop_var + ".ptr");
            
            // 存储起始值
            auto start_i32 = builder->create_cast(start_val, i32_type, ir::OpCode::SIToFP);
            builder->create_store(counter, start_i32);
            
            // 保存循环上下文
            LoopContext ctx{cond_block, body_block, after_block};
            loop_stack.push_back(ctx);
            
            // 跳转到条件块
            builder->create_br(cond_block);
            
            // 生成条件块
            builder->set_insert_point(cond_block);
            auto count_val = builder->create_load(counter, i32_type);
            auto end_i32 = builder->create_cast(end_val, i32_type, ir::OpCode::SIToFP);
            
            // 比较: counter < end (或 <= end for inclusive)
            auto cmp_op = (bin_expr->op == ast::BinaryOp::RangeInclusive) 
                ? ir::OpCode::Le : ir::OpCode::Lt;
            auto cond = builder->create_binary(cmp_op, count_val, end_i32);
            builder->create_cond_br(cond, body_block, after_block);
            
            // 生成循环体
            builder->set_insert_point(body_block);
            enter_scope();
            
            // 加载当前计数值并声明循环变量
            auto curr_val = builder->create_load(counter, i32_type);
            declare_variable(loop_var, curr_val);
            
            // 生成循环体语句
            if (auto block_stmt = std::dynamic_pointer_cast<ast::BlockStmt>(
                    std::const_pointer_cast<ast::ASTNode>(
                        std::shared_ptr<ast::ASTNode>(body, body.get())))) {
                generate_block(block_stmt);
            }
            
            // 递增计数器
            if (!current_block->terminator) {
                auto count_val2 = builder->create_load(counter, i32_type);
                auto one = builder->create_constant(int64_t(1), i32_type);
                auto next = builder->create_binary(ir::OpCode::Add, count_val2, one);
                builder->create_store(counter, next);
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

void IRGenerator::generate_while(std::shared_ptr<ast::WhileStmt> while_loop) {
    if (!while_loop) return;
    
    // while condition { body }
    auto condition = while_loop->get_condition();
    auto body = while_loop->get_body();
    
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
        // 转换为 i1 进行条件分支
        auto i1_type = builder->get_primitive_type(ir::PrimitiveTypeKind::Bool);
        auto cond_bool = builder->create_cast(cond, i1_type, ir::OpCode::Trunc);
        builder->create_cond_br(cond_bool, body_block, after_block);
    } else {
        builder->create_br(after_block);
    }
    
    // 生成循环体
    builder->set_insert_point(body_block);
    enter_scope();
    
    if (auto block_stmt = std::dynamic_pointer_cast<ast::BlockStmt>(
            std::const_pointer_cast<ast::ASTNode>(
                std::shared_ptr<ast::ASTNode>(body, body.get())))) {
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

void IRGenerator::generate_block(std::shared_ptr<ast::BlockStmt> block) {
    if (!block) return;
    
    enter_scope();
    for (auto& stmt : block->statements) {
        generate_statement(stmt);
        if (current_block && current_block->terminator) break;
    }
    exit_scope();
}

void IRGenerator::generate_expr_stmt(std::shared_ptr<ast::ExpressionStmt> expr_stmt) {
    if (!expr_stmt) return;
    generate_expression(expr_stmt->expression);
}

void IRGenerator::generate_match(std::shared_ptr<ast::MatchStmt> match) {
    if (!match) return;
    
    // match expr { pattern1 => body1, pattern2 => body2, _ => default }
    auto match_expr = match->get_expr();
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
            if (ident->name == "_") {
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
        auto pattern_val = generate_expression(patterns[i]);
        
        // 比较: expr == pattern
        auto cmp = builder->create_binary(ir::OpCode::Eq, expr_val, pattern_val);
        
        // 决定目标块：如果是最后一个非通配符case，跳转到default/after
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
        
        if (auto block_stmt = std::dynamic_pointer_cast<ast::BlockStmt>(
                std::const_pointer_cast<ast::ASTNode>(
                    std::shared_ptr<ast::ASTNode>(bodies[i], bodies[i].get())))) {
            generate_block(block_stmt);
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

std::shared_ptr<ir::Type> IRGenerator::map_ast_type(
    const std::shared_ptr<ast::Type>& ast_type) {
    if (!ast_type) {
        return builder->get_primitive_type(ir::PrimitiveTypeKind::Int32);
    }
    
    // 基础类型映射
    if (auto prim = std::dynamic_pointer_cast<ast::PrimitiveType>(ast_type)) {
        if (prim->name == "int" || prim->name == "i32") {
            return builder->get_primitive_type(ir::PrimitiveTypeKind::Int32);
        } else if (prim->name == "i64" || prim->name == "long") {
            return builder->get_primitive_type(ir::PrimitiveTypeKind::Int64);
        } else if (prim->name == "float" || prim->name == "f32") {
            return builder->get_primitive_type(ir::PrimitiveTypeKind::Float32);
        } else if (prim->name == "double" || prim->name == "f64") {
            return builder->get_primitive_type(ir::PrimitiveTypeKind::Float64);
        } else if (prim->name == "bool") {
            return builder->get_primitive_type(ir::PrimitiveTypeKind::Bool);
        } else if (prim->name == "string") {
            return builder->get_primitive_type(ir::PrimitiveTypeKind::String);
        } else if (prim->name == "void") {
            return builder->get_primitive_type(ir::PrimitiveTypeKind::Void);
        }
    }
    
    // 数组类型
    if (auto arr = std::dynamic_pointer_cast<ast::ArrayType>(ast_type)) {
        auto elem_type = map_ast_type(arr->element_type);
        return builder->get_array_type(elem_type, arr->size);
    }
    
    // 函数类型
    if (auto fn = std::dynamic_pointer_cast<ast::FunctionType>(ast_type)) {
        auto ret_type = map_ast_type(fn->return_type);
        std::vector<std::shared_ptr<ir::Type>> param_types;
        for (auto& p : fn->param_types) {
            param_types.push_back(map_ast_type(p));
        }
        return builder->get_function_type(ret_type, param_types);
    }
    
    // 默认返回 i32
    return builder->get_primitive_type(ir::PrimitiveTypeKind::Int32);
}

ir::OpCode IRGenerator::map_binary_op(ast::BinaryOp op) {
    switch (op) {
        case ast::BinaryOp::Add: return ir::OpCode::Add;
        case ast::BinaryOp::Sub: return ir::OpCode::Sub;
        case ast::BinaryOp::Mul: return ir::OpCode::Mul;
        case ast::BinaryOp::Div: return ir::OpCode::Div;
        case ast::BinaryOp::Mod: return ir::OpCode::Mod;
        case ast::BinaryOp::And: return ir::OpCode::And;
        case ast::BinaryOp::Or: return ir::OpCode::Or;
        case ast::BinaryOp::BitAnd: return ir::OpCode::BitAnd;
        case ast::BinaryOp::BitOr: return ir::OpCode::BitOr;
        case ast::BinaryOp::BitXor: return ir::OpCode::BitXor;
        case ast::BinaryOp::Shl: return ir::OpCode::Shl;
        case ast::BinaryOp::Shr: return ir::OpCode::Shr;
        default: return ir::OpCode::Add;
    }
}

ir::OpCode IRGenerator::map_unary_op(ast::UnaryOp op) {
    switch (op) {
        case ast::UnaryOp::Neg: return ir::OpCode::Sub;
        case ast::UnaryOp::Not: return ir::OpCode::Not;
        case ast::UnaryOp::BitNot: return ir::OpCode::BitNot;
        default: return ir::OpCode::Not;
    }
}

ir::OpCode IRGenerator::map_comparison_op(ast::BinaryOp op) {
    switch (op) {
        case ast::BinaryOp::Eq: return ir::OpCode::Eq;
        case ast::BinaryOp::Ne: return ir::OpCode::Ne;
        case ast::BinaryOp::Lt: return ir::OpCode::Lt;
        case ast::BinaryOp::Le: return ir::OpCode::Le;
        case ast::BinaryOp::Gt: return ir::OpCode::Gt;
        case ast::BinaryOp::Ge: return ir::OpCode::Ge;
        default: return ir::OpCode::Eq;
    }
}

std::shared_ptr<ir::BasicBlock> IRGenerator::create_block(const std::string& name) {
    return builder->create_block(name);
}

// ============================================================================
// 事件系统支持
// ============================================================================

void IRGenerator::generate_publish(std::shared_ptr<ast::PublishStmt> publish) {
    if (!publish) return;
    
    // publish event_name(arg1, arg2, ...)
    std::string event_name = publish->get_event_name();
    const auto& args = publish->get_arguments();
    
    // 生成参数值
    std::vector<std::shared_ptr<ir::Value>> arg_vals;
    for (auto& arg : args) {
        auto val = generate_expression(arg);
        if (val) arg_vals.push_back(val);
    }
    
    // 创建事件调用 (通过运行时函数)
    std::string func_name = "__claw_event_publish_" + event_name;
    builder->create_call(func_name, arg_vals);
}

void IRGenerator::generate_subscribe(std::shared_ptr<ast::SubscribeStmt> sub) {
    if (!sub) return;
    
    // subscribe event_name { handler }
    std::string event_name = sub->get_event_name();
    auto handler = sub->get_handler();
    
    if (!handler) return;
    
    // 生成事件处理函数
    auto handler_func = generate_function_handler(handler, event_name);
    
    // 创建订阅调用
    std::string func_name = "__claw_event_subscribe_" + event_name;
    std::vector<std::shared_ptr<ir::Value>> args;
    args.push_back(handler_func);
    builder->create_call(func_name, args);
}

std::shared_ptr<ir::Function> IRGenerator::generate_function_handler(
    std::shared_ptr<ast::FunctionStmt> handler, 
    const std::string& event_name) {
    if (!handler) return nullptr;
    
    // 为事件处理函数创建包装
    std::string handler_name = "__claw_handler_" + event_name;
    auto ret_type = builder->get_primitive_type(ir::PrimitiveTypeKind::Void);
    auto func = builder->create_function(handler_name, ret_type);
    
    auto entry = builder->create_block("entry");
    builder->set_insert_point(entry);
    
    // 生成处理函数体
    if (handler->body) {
        generate_block(handler->body);
    }
    
    // 确保有返回
    if (!current_block || !current_block->terminator) {
        builder->create_ret(nullptr);
    }
    
    return func;
}

} // namespace claw
