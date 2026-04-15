// Claw Compiler - LLVM IR Code Generator
// Generates LLVM IR from AST

#ifndef CLAW_CODEGEN_H
#define CLAW_CODEGEN_H

#include <string>
#include <memory>
#include <map>
#include <vector>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include "ast/ast.h"
#include "common/common.h"

namespace claw {
namespace codegen {

// Code generator class
class CodeGenerator {
public:
    CodeGenerator();
    ~CodeGenerator();
    
    // Generate LLVM IR from AST
    bool generate(ast::Program* program);
    
    // Get generated IR string
    std::string get_ir() const;
    
    // Get the LLVM module
    llvm::Module* get_module() { return module_.get(); }
    
private:
    llvm::LLVMContext context_;
    std::unique_ptr<llvm::Module> module_;
    std::unique_ptr<llvm::IRBuilder<>> builder_;
    
    // Symbol table for variables
    std::map<std::string, llvm::Value*> named_values_;
    std::map<std::string, llvm::Type*> named_types_;
    
    // Track if we're in a serial process (for println handling)
    bool in_serial_process_ = false;
    
    // Helper methods
    llvm::Type* get_llvm_type(const std::string& claw_type);
    llvm::Value* codegen_expression(ast::Expression* expr);
    llvm::Value* codegen_statement(ast::Statement* stmt);
    llvm::Value* codegen_block(ast::BlockStmt* block);
    llvm::Value* codegen_function(ast::FunctionStmt* func);
    llvm::Value* codegen_call(ast::CallExpr* call);
    llvm::Value* codegen_binary(ast::BinaryExpr* binary);
    llvm::Value* codegen_literal(ast::LiteralExpr* literal);
    llvm::Value* codegen_identifier(ast::IdentifierExpr* ident);
    llvm::Value* codegen_index(ast::IndexExpr* index);
    llvm::Value* codegen_let(ast::LetStmt* let);
    llvm::Value* codegen_assign(ast::AssignStmt* assign);
    llvm::Value* codegen_return(ast::ReturnStmt* ret);
    llvm::Value* codegen_if(ast::IfStmt* if_stmt);
    
    // Built-in functions
    void declare_builtins();
    llvm::Function* declare_printf();
    
    // Helper to get array element pointer
    llvm::Value* get_element_ptr(llvm::Value* array, llvm::Value* index, llvm::Type* element_type);
    
    // Loop support
    llvm::Value* codegen_for(ast::ForStmt* for_stmt);
    llvm::Value* codegen_while(ast::WhileStmt* while_stmt);
    llvm::Value* codegen_break(ast::BreakStmt* brk);
    llvm::Value* codegen_continue(ast::ContinueStmt* cont);
    llvm::Value* codegen_match(ast::MatchStmt* match);
    
    // Extended expression support
    llvm::Value* codegen_unary(ast::UnaryExpr* unary);
    // NOTE: CompareExpr doesn't exist - comparisons handled as BinaryExpr
    // llvm::Value* codegen_compare(ast::CompareExpr* compare);
    
    // Event system support
    llvm::Value* codegen_publish(ast::PublishStmt* publish);
    llvm::Value* codegen_subscribe(ast::SubscribeStmt* subscribe);
    llvm::Value* codegen_serial_process(ast::SerialProcessStmt* process);
    
    // Current control flow context
    llvm::BasicBlock* loop_entry_ = nullptr;
    llvm::BasicBlock* loop_exit_ = nullptr;
};

} // namespace codegen
} // namespace claw

#endif // CLAW_CODEGEN_H
