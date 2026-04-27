// ir_generator.h - AST 到 IR 的转换器
// 将 Claw AST 转换为 SSA 形式的中间表示

#ifndef CLAW_IR_GENERATOR_H
#define CLAW_IR_GENERATOR_H

#include <memory>
#include <unordered_map>
#include <stack>
#include <string>
#include <vector>
#include "ir/ir.h"
#include "../ast/ast.h"

namespace claw {

// AST → IR 转换器
class IRGenerator {
public:
    // 构造函数
    IRGenerator();
    
    // 主转换入口
    std::shared_ptr<ir::Module> generate(std::shared_ptr<ast::Module> ast);
    
private:
    // IR 构建器
    std::shared_ptr<ir::IRBuilder> builder;
    
    // 当前上下文
    std::shared_ptr<ir::Function> current_function;
    std::shared_ptr<ir::BasicBlock> current_block;
    std::shared_ptr<ir::BasicBlock> entry_block;
    
    // 作用域栈（用于变量映射）
    std::stack<std::unordered_map<std::string, std::shared_ptr<ir::Value>>> scope_stack;
    
    // 循环上下文（用于 break/continue）
    struct LoopContext {
        std::shared_ptr<ir::BasicBlock> condition_block;
        std::shared_ptr<ir::BasicBlock> body_block;
        std::shared_ptr<ir::BasicBlock> after_block;
    };
    std::vector<LoopContext> loop_stack;
    
    // 符号映射：AST 变量名 → IR Value
    std::unordered_map<std::string, std::shared_ptr<ir::Value>> locals;
    
    // 函数定义映射
    std::unordered_map<std::string, std::shared_ptr<ir::Function>> functions;
    
    // 工具函数
    void enter_scope();
    void exit_scope();
    void declare_variable(const std::string& name, std::shared_ptr<ir::Value> value);
    std::shared_ptr<ir::Value> lookup_variable(const std::string& name);
    
    // 表达式转换
    std::shared_ptr<ir::Value> generate_expression(std::shared_ptr<ast::Expression> expr);
    
    // 字面量转换
    std::shared_ptr<ir::Value> generate_literal(std::shared_ptr<ast::LiteralExpr> lit);
    
    // 标识符转换
    std::shared_ptr<ir::Value> generate_identifier(std::shared_ptr<ast::IdentifierExpr> id);
    
    // 二元表达式转换
    std::shared_ptr<ir::Value> generate_binary_expr(std::shared_ptr<ast::BinaryExpr> bin);
    
    // 一元表达式转换
    std::shared_ptr<ir::Value> generate_unary_expr(std::shared_ptr<ast::UnaryExpr> un);
    
    // 调用表达式转换
    std::shared_ptr<ir::Value> generate_call(std::shared_ptr<ast::CallExpr> call);
    
    // 下标表达式转换
    std::shared_ptr<ir::Value> generate_index(std::shared_ptr<ast::IndexExpr> idx);
    
    // 语句转换
    void generate_statement(std::shared_ptr<ast::Statement> stmt);
    
    // 函数声明转换
    void generate_function_decl(std::shared_ptr<ast::FunctionDecl> decl);
    
    // 返回语句转换
    void generate_return(std::shared_ptr<ast::ReturnStmt> ret);
    
    // 变量声明转换
    void generate_let(std::shared_ptr<ast::LetStmt> let);
    
    // 赋值语句转换
    void generate_assign(std::shared_ptr<ast::AssignStmt> assign);
    
    // 条件语句转换
    void generate_if(std::shared_ptr<ast::IfStmt> if_stmt);
    
    // 循环语句转换
    void generate_loop(std::shared_ptr<ast::LoopStmt> loop);
    void generate_for(std::shared_ptr<ast::ForStmt> for_loop);
    void generate_while(std::shared_ptr<ast::WhileStmt> while_loop);
    
    // Break/Continue 转换
    void generate_break();
    void generate_continue();
    
    // 块语句转换
    void generate_block(std::shared_ptr<ast::BlockStmt> block);
    
    // 表达式语句转换
    void generate_expr_stmt(std::shared_ptr<ast::ExpressionStmt> expr_stmt);
    
    // 匹配语句转换
    void generate_match(std::shared_ptr<ast::MatchStmt> match);
    
    // 事件系统转换
    void generate_publish(std::shared_ptr<ast::PublishStmt> publish);
    void generate_subscribe(std::shared_ptr<ast::SubscribeStmt> sub);
    std::shared_ptr<ir::Function> generate_function_handler(
        std::shared_ptr<ast::FunctionStmt> handler, 
        const std::string& event_name);
    
    // 增强版表达式分发
    std::shared_ptr<ir::Value> generate_expression_enhanced(std::shared_ptr<ast::Expression> expr);
    std::shared_ptr<ir::Value> generate_literal_enhanced(std::shared_ptr<ast::LiteralExpr> lit);
    std::shared_ptr<ir::Value> generate_bytes_literal(std::shared_ptr<ast::LiteralExpr> lit);
    std::shared_ptr<ir::Value> generate_index_enhanced(std::shared_ptr<ast::IndexExpr> idx);
    std::shared_ptr<ir::Value> generate_array_literal(std::shared_ptr<ast::ArrayExpr> arr);
    std::shared_ptr<ir::Value> generate_tuple_literal(std::shared_ptr<ast::TupleExpr> tup);
    std::shared_ptr<ir::Value> generate_lambda(std::shared_ptr<ast::LambdaExpr> lambda);
    std::shared_ptr<ir::Value> generate_field_access(std::shared_ptr<ast::FieldExpr> field);
    std::shared_ptr<ir::Value> generate_tensor_create(std::shared_ptr<ast::TensorExpr> tensor);
    std::shared_ptr<ir::Value> generate_tensor_op(std::shared_ptr<ast::TensorOpExpr> tensor_op);
    
    // 增强版语句分发
    void generate_statement_enhanced(std::shared_ptr<ast::Statement> stmt);
    void generate_for_enhanced(std::shared_ptr<ast::ForStmt> for_loop);
    void generate_for_array(const std::string& loop_var,
                             std::shared_ptr<ir::Value> array_ptr,
                             ir::ArrayType* arr_type,
                             std::shared_ptr<ast::Statement> body);
    void generate_for_tensor(const std::string& loop_var,
                              std::shared_ptr<ir::Value> tensor_ptr,
                              ir::TensorType* tensor_type,
                              std::shared_ptr<ast::Statement> body);
    
    // 操作码映射
    ir::OpCode map_binary_op(ast::BinaryOp op);
    ir::OpCode map_unary_op(ast::UnaryOp op);
    ir::OpCode map_comparison_op(ast::BinaryOp op);
    
    // 基本块创建辅助
    std::shared_ptr<ir::BasicBlock> create_block(const std::string& name);
};

} // namespace claw

#endif // CLAW_IR_GENERATOR_H
