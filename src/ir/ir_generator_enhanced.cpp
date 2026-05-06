// ir_generator_enhanced.cpp - AST 到 IR 的转换器增强版
// 完善 TODO 功能：GEP 实现、张量操作、数组/张量迭代等

#include "ir/ir_generator.h"
#include <stdexcept>
#include <iostream>

namespace claw {

// ============================================================================
// IRGenerator 增强实现
// ============================================================================

// 增强的字面量处理 (适配 LiteralExpr::get_value() variant API)
std::shared_ptr<ir::Value> IRGenerator::generate_literal_enhanced(
    ast::LiteralExpr* lit) {
    if (!lit) return nullptr;

    auto& val = lit->get_value();
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
            std::cerr << "Warning: null literal" << std::endl;
            return nullptr;
        }
    }, val);
}

// 增强的下标表达式处理（正确的 GEP 实现）
std::shared_ptr<ir::Value> IRGenerator::generate_index_enhanced(
    ast::IndexExpr* idx) {
    if (!idx) return nullptr;

    auto base = generate_expression(idx->get_object());
    if (!base) return nullptr;

    auto index = generate_expression(idx->get_index());
    if (!index) return nullptr;

    // 确定基类型
    auto base_type = base->type;

    // 指针类型：使用 GEP
    if (auto* ptr_type = dynamic_cast<ir::PointerType*>(base_type.get())) {
        auto elem_type = ptr_type->pointee;
        auto elem_ptr = builder->create_gep(base, {index}, elem_type, ".elem_ptr");
        return builder->create_load(elem_ptr, ".elem");
    }

    // 数组类型：先获取数组指针，再使用 GEP
    if (auto* arr_type = dynamic_cast<ir::ArrayType*>(base_type.get())) {
        auto elem_type = arr_type->element_type;

        // 确保 base 是指针类型
        auto ptr_type = builder->get_pointer_type(elem_type);
        auto array_ptr = base;

        // 使用 GEP 计算元素地址
        auto elem_ptr = builder->create_gep(array_ptr, {index}, elem_type, ".arr_elem_ptr");
        return builder->create_load(elem_ptr, ".arr_elem");
    }

    // 张量类型：使用 TensorLoad
    if (auto* tensor_type = dynamic_cast<ir::TensorType*>(base_type.get())) {
    (void)tensor_type;
        return builder->create_tensor_load(base, {index});
    }

    std::cerr << "Warning: index on unsupported type" << std::endl;
    return base;
}

// 数组字面量转换
std::shared_ptr<ir::Value> IRGenerator::generate_array_literal(
    ast::ArrayExpr* arr) {
    if (!arr) return nullptr;

    auto& elements = arr->get_elements();
    if (elements.empty()) {
        auto elem_type = builder->get_primitive_type(ir::PrimitiveTypeKind::Int32);
        return builder->create_alloca(builder->get_array_type(elem_type, 0), 1, ".empty_arr");
    }

    // 推断元素类型
    auto first_elem = generate_expression(elements[0].get());
    if (!first_elem) return nullptr;

    auto elem_type = first_elem->type;
    auto array_type = builder->get_array_type(elem_type, elements.size());

    // 在栈上分配数组
    auto array_ptr = builder->create_alloca(array_type, 1, ".arr");

    // 存储每个元素
    for (size_t i = 0; i < elements.size(); ++i) {
        auto elem_val = generate_expression(elements[i].get());
        if (!elem_val) continue;

        auto idx_val = builder->create_constant(static_cast<int64_t>(i));
        auto elem_ptr = builder->create_gep(array_ptr, {idx_val}, elem_type, ".arr_elem_ptr");
        builder->create_store(elem_val, elem_ptr);
    }

    return array_ptr;
}

// 元组字面量转换
std::shared_ptr<ir::Value> IRGenerator::generate_tuple_literal(
    ast::TupleExpr* tup) {
    if (!tup) return nullptr;

    auto& elements = tup->get_elements();
    if (elements.empty()) {
        return builder->create_alloca(
            builder->get_primitive_type(ir::PrimitiveTypeKind::Void), 1, ".empty_tuple");
    }

    // 生成每个元素
    std::vector<std::shared_ptr<ir::Value>> elem_vals;
    std::vector<std::shared_ptr<ir::Type>> elem_types;

    for (auto& elem : elements) {
        auto val = generate_expression(elem.get());
        if (val) {
            elem_vals.push_back(val);
            elem_types.push_back(val->type);
        }
    }

    // 创建元组类型 (使用结构体表示)
    // 简化：使用数组存储
    if (!elem_types.empty()) {
        auto tuple_type = builder->get_array_type(elem_types[0], elem_vals.size());
        auto tuple_ptr = builder->create_alloca(tuple_type, 1, ".tuple");

        for (size_t i = 0; i < elem_vals.size(); ++i) {
            auto idx_val = builder->create_constant(static_cast<int64_t>(i));
            auto elem_ptr = builder->create_gep(tuple_ptr, {idx_val}, elem_types[i], ".tuple_elem_ptr");
            builder->create_store(elem_vals[i], elem_ptr);
        }

        return tuple_ptr;
    }

    return nullptr;
}

// Lambda 表达式转换
std::shared_ptr<ir::Value> IRGenerator::generate_lambda(
    ast::LambdaExpr* lambda) {
    if (!lambda) return nullptr;

    // 创建匿名函数
    std::string lambda_name = ".lambda_" + std::to_string(reinterpret_cast<uintptr_t>(lambda));
    auto ret_type = map_ast_type(lambda->get_return_type());
    auto func = builder->create_function(lambda_name, ret_type);

    // 保存当前函数和块
    auto saved_func = current_function;
    auto saved_block = current_block;

    // 设置当前函数
    current_function = func;

    // 创建入口块
    entry_block = builder->create_block("entry");
    builder->set_insert_point(entry_block);

    // 处理参数
    enter_scope();
    for (size_t i = 0; i < lambda->get_params().size(); ++i) {
        auto& param = lambda->get_params()[i];
        auto param_type = map_ast_type(param.second);

        auto alloca = builder->create_alloca(param_type, 1, param.first);
        declare_variable(param.first, alloca);
    }

    // 生成函数体
    if (lambda->get_body()) {
        // Body is an ASTNode; try to cast to Statement
        if (auto* stmt = dynamic_cast<ast::Statement*>(lambda->get_body())) {
            generate_statement(stmt);
        }
    }

    // 确保有返回
    if (!current_block || !current_block->terminator) {
        builder->create_ret_void();
    }

    exit_scope();

    // 恢复上下文
    current_function = saved_func;
    current_block = saved_block;
    if (current_function) {
        builder->set_insert_point(current_block);
    }

    // 返回函数指针
    auto func_ptr_type = builder->get_pointer_type(
        builder->get_function_type(ret_type, {}));
    auto func_ptr = std::make_shared<ir::Value>(lambda_name, func_ptr_type);
    func_ptr->is_constant = true;

    return func_ptr;
}

// 字段访问表达式转换 (using MemberExpr instead of FieldExpr)
std::shared_ptr<ir::Value> IRGenerator::generate_field_access(
    ast::MemberExpr* field) {
    if (!field) return nullptr;

    auto object = generate_expression(field->get_object());
    if (!object) return nullptr;

    // 简化实现：假设字段通过名称访问
    // 实际需要通过类型信息确定字段偏移
    std::string field_name = field->get_member();

    // 查找变量（可能是一个结构体指针）
    auto field_ptr = lookup_variable(field_name);
    if (field_ptr) {
        return builder->create_load(field_ptr, field_name);
    }

    std::cerr << "Warning: field access not fully implemented for: " << field_name << std::endl;
    return object;
}

// 增强的 for 循环（支持数组/张量迭代）
void IRGenerator::generate_for_enhanced(ast::ForStmt* for_loop) {
    if (!for_loop) return;

    std::string loop_var = for_loop->get_variable();
    auto iterable = for_loop->get_iterable();
    auto body = for_loop->get_body();

    if (!iterable || !body) return;

    // 生成迭代器表达式
    auto iter_val = generate_expression(iterable);
    if (!iter_val) return;

    auto iter_type = iter_val->type;

    // 判断迭代类型
    if (auto* ptr_type = dynamic_cast<ir::PointerType*>(iter_type.get())) {
        auto pointee_type = ptr_type->pointee;

        // 检查是否是数组类型
        if (auto* arr_type = dynamic_cast<ir::ArrayType*>(pointee_type.get())) {
            generate_for_array(loop_var, iter_val, arr_type, body);
            return;
        }

        // 检查是否是张量类型
        if (auto* tensor_type = dynamic_cast<ir::TensorType*>(pointee_type.get())) {
            generate_for_tensor(loop_var, iter_val, tensor_type, body);
            return;
        }
    }

    // 回退到范围迭代
    generate_for(for_loop);
}

// 数组迭代 for 循环
void IRGenerator::generate_for_array(const std::string& loop_var,
                                      std::shared_ptr<ir::Value> array_ptr,
                                      ir::ArrayType* arr_type,
                                      ast::ASTNode* body) {
    auto elem_type = arr_type->element_type;
    auto array_size = arr_type->size;

    // 创建循环块
    auto init_block = create_block("for.init");
    auto cond_block = create_block("for.cond");
    auto body_block = create_block("for.body");
    auto after_block = create_block("for.after");

    // 保存循环上下文
    LoopContext ctx{cond_block, body_block, after_block};
    loop_stack.push_back(ctx);

    // 跳转到初始化块
    builder->create_br(init_block);
    builder->set_insert_point(init_block);

    // 创建循环计数器
    auto i32_type = builder->get_primitive_type(ir::PrimitiveTypeKind::Int32);
    auto counter = builder->create_alloca(i32_type, 1, loop_var + ".idx");
    auto zero = builder->create_constant(int64_t(0));
    builder->create_store(zero, counter);

    builder->create_br(cond_block);

    // 条件块
    builder->set_insert_point(cond_block);
    auto count_val = builder->create_load(counter, loop_var + ".count");
    auto size_val = builder->create_constant(static_cast<int64_t>(array_size));
    auto cond = builder->create_cmp(ir::OpCode::Lt, count_val, size_val, ".lt");
    builder->create_cond_br(cond, body_block, after_block);

    // 循环体
    builder->set_insert_point(body_block);
    enter_scope();

    // 加载当前元素
    auto curr_idx = builder->create_load(counter, ".idx");
    auto elem_ptr = builder->create_gep(array_ptr, {curr_idx}, elem_type, ".elem_ptr");
    auto elem_val = builder->create_load(elem_ptr, loop_var);
    declare_variable(loop_var, elem_val);

    // 生成循环体语句
    if (auto* stmt = dynamic_cast<ast::Statement*>(body)) {
        generate_statement(stmt);
    }

    // 递增计数器
    if (!current_block->terminator) {
        auto count_val2 = builder->create_load(counter, ".count2");
        auto one = builder->create_constant(int64_t(1));
        auto next = builder->create_add(count_val2, one, ".next");
        builder->create_store(next, counter);
        builder->create_br(cond_block);
    }

    exit_scope();
    loop_stack.pop_back();

    // 设置 after 块
    builder->set_insert_point(after_block);
}

// 张量迭代 for 循环
void IRGenerator::generate_for_tensor(const std::string& loop_var,
                                       std::shared_ptr<ir::Value> tensor_ptr,
                                       ir::TensorType* tensor_type,
                                       ast::ASTNode* body) {
    auto elem_type = tensor_type->element_type;
    auto shape = tensor_type->shape;

    // 计算总元素数
    int64_t total_elements = 1;
    for (auto dim : shape) {
        if (dim > 0) total_elements *= dim;
        else {
            total_elements = -1;  // 动态形状
            break;
        }
    }

    // 创建循环块
    auto init_block = create_block("for_tensor.init");
    auto cond_block = create_block("for_tensor.cond");
    auto body_block = create_block("for_tensor.body");
    auto after_block = create_block("for_tensor.after");

    // 保存循环上下文
    LoopContext ctx{cond_block, body_block, after_block};
    loop_stack.push_back(ctx);

    builder->create_br(init_block);
    builder->set_insert_point(init_block);

    // 创建线性索引
    auto i64_type = builder->get_primitive_type(ir::PrimitiveTypeKind::Int64);
    auto linear_idx = builder->create_alloca(i64_type, 1, loop_var + ".linear");
    auto zero = builder->create_constant(int64_t(0));
    builder->create_store(zero, linear_idx);

    builder->create_br(cond_block);

    // 条件块
    builder->set_insert_point(cond_block);
    auto curr_idx = builder->create_load(linear_idx, ".curr_idx");

    std::shared_ptr<ir::Value> cond;
    if (total_elements > 0) {
        auto total = builder->create_constant(total_elements);
        cond = builder->create_cmp(ir::OpCode::Lt, curr_idx, total, ".lt");
    } else {
        // 动态形状：使用运行时获取大小
        auto size_call = builder->create_call("__claw_tensor_size", {tensor_ptr}, ".size");
        cond = builder->create_cmp(ir::OpCode::Lt, curr_idx, size_call, ".lt");
    }

    builder->create_cond_br(cond, body_block, after_block);

    // 循环体
    builder->set_insert_point(body_block);
    enter_scope();

    // 加载当前元素（使用线性索引）
    auto idx_val = builder->create_load(linear_idx, ".idx_val");
    auto elem_val = builder->create_tensor_load(tensor_ptr, {idx_val});
    declare_variable(loop_var, elem_val);

    // 生成循环体
    if (auto* stmt = dynamic_cast<ast::Statement*>(body)) {
        generate_statement(stmt);
    }

    // 递增索引
    if (!current_block->terminator) {
        auto curr = builder->create_load(linear_idx, ".curr");
        auto one = builder->create_constant(int64_t(1));
        auto next = builder->create_add(curr, one, ".next");
        builder->create_store(next, linear_idx);
        builder->create_br(cond_block);
    }

    exit_scope();
    loop_stack.pop_back();

    builder->set_insert_point(after_block);
}

// 增强的表达式分发（支持更多表达式类型）
std::shared_ptr<ir::Value> IRGenerator::generate_expression_enhanced(
    ast::Expression* expr) {
    if (!expr) return nullptr;

    // 字面量
    if (auto* lit = dynamic_cast<ast::LiteralExpr*>(expr)) {
        return generate_literal_enhanced(lit);
    }

    // 标识符
    if (auto* id = dynamic_cast<ast::IdentifierExpr*>(expr)) {
        return generate_identifier(id);
    }

    // 二元表达式
    if (auto* bin = dynamic_cast<ast::BinaryExpr*>(expr)) {
        return generate_binary_expr(bin);
    }

    // 一元表达式
    if (auto* un = dynamic_cast<ast::UnaryExpr*>(expr)) {
        return generate_unary_expr(un);
    }

    // 函数调用
    if (auto* call = dynamic_cast<ast::CallExpr*>(expr)) {
        return generate_call(call);
    }

    // 下标表达式（增强版）
    if (auto* idx = dynamic_cast<ast::IndexExpr*>(expr)) {
        return generate_index_enhanced(idx);
    }

    // 字段访问 (MemberExpr)
    if (auto* field = dynamic_cast<ast::MemberExpr*>(expr)) {
        return generate_field_access(field);
    }

    // 数组字面量
    if (auto* arr = dynamic_cast<ast::ArrayExpr*>(expr)) {
        return generate_array_literal(arr);
    }

    // 元组字面量
    if (auto* tup = dynamic_cast<ast::TupleExpr*>(expr)) {
        return generate_tuple_literal(tup);
    }

    // Lambda 表达式
    if (auto* lambda = dynamic_cast<ast::LambdaExpr*>(expr)) {
        return generate_lambda(lambda);
    }

    std::cerr << "Warning: unsupported expression type" << std::endl;
    return nullptr;
}

// 增强的语句分发（支持更多语句类型）
void IRGenerator::generate_statement_enhanced(ast::Statement* stmt) {
    if (!stmt) return;

    if (auto* fn = dynamic_cast<ast::FunctionStmt*>(stmt)) {
        generate_function_decl(fn);
    } else if (auto* let_stmt = dynamic_cast<ast::LetStmt*>(stmt)) {
        generate_let(let_stmt);
    } else if (auto* assign = dynamic_cast<ast::AssignStmt*>(stmt)) {
        generate_assign(assign);
    } else if (auto* ret = dynamic_cast<ast::ReturnStmt*>(stmt)) {
        generate_return(ret);
    } else if (auto* if_stmt = dynamic_cast<ast::IfStmt*>(stmt)) {
        generate_if(if_stmt);
    } else if (auto* block = dynamic_cast<ast::BlockStmt*>(stmt)) {
        generate_block(block);
    } else if (auto* expr_stmt = dynamic_cast<ast::ExprStmt*>(stmt)) {
        generate_expr_stmt(expr_stmt);
    } else if (auto* match = dynamic_cast<ast::MatchStmt*>(stmt)) {
        generate_match(match);
    } else if (auto* for_stmt = dynamic_cast<ast::ForStmt*>(stmt)) {
        generate_for_enhanced(for_stmt);
    } else if (auto* while_stmt = dynamic_cast<ast::WhileStmt*>(stmt)) {
        generate_while(while_stmt);
    } else if (auto* break_stmt = dynamic_cast<ast::BreakStmt*>(stmt)) {
    (void)break_stmt;
        generate_break();
    } else if (auto* cont_stmt = dynamic_cast<ast::ContinueStmt*>(stmt)) {
    (void)cont_stmt;
        generate_continue();
    } else if (auto* publish = dynamic_cast<ast::PublishStmt*>(stmt)) {
        generate_publish(publish);
    } else if (auto* sub = dynamic_cast<ast::SubscribeStmt*>(stmt)) {
        generate_subscribe(sub);
    } else {
        std::cerr << "Warning: unsupported statement type" << std::endl;
    }
}

} // namespace claw
