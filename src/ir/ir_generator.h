// ir_generator.h - AST 到 IR 的转换器
// 将 Claw AST 转换为 SSA 形式的中间表示

#ifndef CLAW_IR_GENERATOR_H
#define CLAW_IR_GENERATOR_H

#include <memory>
#include <unordered_map>
#include <stack>
#include <string>
#include <vector>
#include "ir.h"
#include "../ast/ast.h"

namespace claw {

// AST → IR 转换器
class IRGenerator {
public:
    // 构造函数
    IRGenerator();
    
    // 主转换入口
    std::shared_ptr<ir::Module> generate(ast::Program* program);
    
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
    
    // AST type → IR type mapping (string-based, since ast has no Type hierarchy)
    std::shared_ptr<ir::Type> map_ast_type(const std::string& type_name);
    
    // 表达式转换
    std::shared_ptr<ir::Value> generate_expression(ast::Expression* expr);
    
    // 字面量转换
    std::shared_ptr<ir::Value> generate_literal(ast::LiteralExpr* lit);
    
    // 标识符转换
    std::shared_ptr<ir::Value> generate_identifier(ast::IdentifierExpr* id);
    
    // 二元表达式转换
    std::shared_ptr<ir::Value> generate_binary_expr(ast::BinaryExpr* bin);
    
    // 一元表达式转换
    std::shared_ptr<ir::Value> generate_unary_expr(ast::UnaryExpr* un);
    
    // 调用表达式转换
    std::shared_ptr<ir::Value> generate_call(ast::CallExpr* call);
    
    // 下标表达式转换
    std::shared_ptr<ir::Value> generate_index(ast::IndexExpr* idx);
    
    // 成员访问表达式转换
    std::shared_ptr<ir::Value> generate_member(ast::MemberExpr* member);
    
    // 数组字面量
    std::shared_ptr<ir::Value> generate_array_literal(ast::ArrayExpr* arr);
    
    // 元组字面量
    std::shared_ptr<ir::Value> generate_tuple_literal(ast::TupleExpr* tup);
    
    // Lambda 表达式
    std::shared_ptr<ir::Value> generate_lambda(ast::LambdaExpr* lambda);
    
    // 语句转换
    void generate_statement(ast::Statement* stmt);
    
    // 函数声明转换
    void generate_function_decl(ast::FunctionStmt* decl);
    
    // 返回语句转换
    void generate_return(ast::ReturnStmt* ret);
    
    // 变量声明转换
    void generate_let(ast::LetStmt* let);
    
    // 赋值语句转换
    void generate_assign(ast::AssignStmt* assign);
    
    // 条件语句转换
    void generate_if(ast::IfStmt* if_stmt);
    
    // 循环语句转换
    void generate_for(ast::ForStmt* for_loop);
    void generate_while(ast::WhileStmt* while_loop);
    
    // Break/Continue 转换
    void generate_break();
    void generate_continue();
    
    // 块语句转换
    void generate_block(ast::BlockStmt* block);
    
    // 表达式语句转换
    void generate_expr_stmt(ast::ExprStmt* expr_stmt);
    
    // 匹配语句转换
    void generate_match(ast::MatchStmt* match);
    
    // 事件系统转换
    void generate_publish(ast::PublishStmt* publish);
    void generate_subscribe(ast::SubscribeStmt* sub);
    std::shared_ptr<ir::Function> generate_function_handler(
        ast::FunctionStmt* handler, 
        const std::string& event_name);
    
    // 操作码映射 (TokenType-based, matching current AST API)
    ir::OpCode map_binary_op(TokenType op);
    ir::OpCode map_unary_op(TokenType op);
    ir::OpCode map_comparison_op(TokenType op);
    
    // 基本块创建辅助
    std::shared_ptr<ir::BasicBlock> create_block(const std::string& name);

    // ====== 增强版方法 ======

    // 增强版字面量 (适配 variant API)
    std::shared_ptr<ir::Value> generate_literal_enhanced(ast::LiteralExpr* lit);

    // 增强版下标 (GEP)
    std::shared_ptr<ir::Value> generate_index_enhanced(ast::IndexExpr* idx);

    // 字段访问 (MemberExpr)
    std::shared_ptr<ir::Value> generate_field_access(ast::MemberExpr* field);

    // 增强版 for 循环 (支持数组/张量迭代)
    void generate_for_enhanced(ast::ForStmt* for_loop);
    void generate_for_array(const std::string& loop_var,
                            std::shared_ptr<ir::Value> array_ptr,
                            ir::ArrayType* arr_type,
                            ast::ASTNode* body);
    void generate_for_tensor(const std::string& loop_var,
                             std::shared_ptr<ir::Value> tensor_ptr,
                             ir::TensorType* tensor_type,
                             ast::ASTNode* body);

    // 增强版表达式分发
    std::shared_ptr<ir::Value> generate_expression_enhanced(ast::Expression* expr);

    // 增强版语句分发
    void generate_statement_enhanced(ast::Statement* stmt);
};

} // namespace claw

#endif // CLAW_IR_GENERATOR_H
