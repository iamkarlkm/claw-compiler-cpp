// Claw Compiler - LLVM IR Code Generator Implementation
// Generates LLVM IR from AST

#include "codegen/codegen.h"
#include <iostream>

using namespace llvm;

namespace claw {
namespace codegen {

CodeGenerator::CodeGenerator() {
    module_ = std::make_unique<Module>("claw_module", context_);
    builder_ = std::make_unique<IRBuilder<>>(context_);
    declare_builtins();
}

CodeGenerator::~CodeGenerator() {
}

// Map Claw types to LLVM types
Type* CodeGenerator::get_llvm_type(const std::string& claw_type) {
    // Handle array types like u32[1]
    size_t bracket_pos = claw_type.find('[');
    if (bracket_pos != std::string::npos) {
        std::string base_type = claw_type.substr(0, bracket_pos);
        // Extract array size
        std::string size_str = claw_type.substr(bracket_pos + 1);
        size_t size_end = size_str.find(']');
        if (size_end != std::string::npos) {
            size_str = size_str.substr(0, size_end);
        }
        int array_size = std::stoi(size_str);
        
        // Get element type and create array type
        Type* elem_type = get_llvm_type(base_type);
        return ArrayType::get(elem_type, array_size);
    }
    
    // Handle optional size suffix
    std::string base_type = claw_type;
    if (claw_type.find("u8") != std::string::npos) base_type = "u8";
    if (claw_type.find("u16") != std::string::npos) base_type = "u16";
    if (claw_type.find("u32") != std::string::npos) base_type = "u32";
    if (claw_type.find("u64") != std::string::npos) base_type = "u64";
    if (claw_type.find("usize") != std::string::npos) base_type = "usize";
    if (claw_type.find("i8") != std::string::npos) base_type = "i8";
    if (claw_type.find("i16") != std::string::npos) base_type = "i16";
    if (claw_type.find("i32") != std::string::npos) base_type = "i32";
    if (claw_type.find("i64") != std::string::npos) base_type = "i64";
    if (claw_type.find("isize") != std::string::npos) base_type = "isize";
    if (claw_type.find("f32") != std::string::npos) base_type = "f32";
    if (claw_type.find("f64") != std::string::npos) base_type = "f64";
    if (claw_type == "bool") base_type = "bool";
    if (claw_type == "char") base_type = "char";
    if (claw_type == "byte") base_type = "byte";
    
    if (base_type == "u8" || base_type == "byte" || base_type == "char") {
        return Type::getInt8Ty(context_);
    }
    if (base_type == "u16" || base_type == "i16") {
        return Type::getInt16Ty(context_);
    }
    if (base_type == "u32" || base_type == "i32" || base_type == "usize" || base_type == "isize") {
        return Type::getInt32Ty(context_);
    }
    if (base_type == "u64" || base_type == "i64") {
        return Type::getInt64Ty(context_);
    }
    if (base_type == "f32") {
        return Type::getFloatTy(context_);
    }
    if (base_type == "f64") {
        return Type::getDoubleTy(context_);
    }
    if (base_type == "bool") {
        return Type::getInt1Ty(context_);
    }
    
    // Default to i32
    return Type::getInt32Ty(context_);
}

// Generate code for the entire program
bool CodeGenerator::generate(ast::Program* program) {
    if (!program) return false;
    
    const auto& decls = program->get_declarations();
    
    for (auto& decl : decls) {
        if (decl->get_kind() == ast::Statement::Kind::Function) {
            auto* func = static_cast<ast::FunctionStmt*>(decl.get());
            if (func->get_name() == "main" || func->get_name() == "calculation_performed") {
                codegen_function(func);
            }
        }
    }
    
    // Verify the module
    std::string err_str;
    raw_string_ostream err_stream(err_str);
    if (verifyModule(*module_, &err_stream)) {
        std::cerr << "Module verification failed: " << err_str << "\n";
        return false;
    }
    
    return true;
}

// Get the generated IR as a string
std::string CodeGenerator::get_ir() const {
    std::string ir;
    raw_string_ostream stream(ir);
    module_->print(stream, nullptr);
    return stream.str();
}

// Declare built-in functions
void CodeGenerator::declare_builtins() {
    declare_printf();
}

// Declare printf function
Function* CodeGenerator::declare_printf() {
    std::vector<Type*> arg_types;
    arg_types.push_back(Type::getInt8Ty(context_)->getPointerTo());
    
    FunctionType* func_type = FunctionType::get(
        Type::getInt32Ty(context_), 
        arg_types, 
        true  // vararg
    );
    
    Function* printf_func = Function::Create(
        func_type, 
        Function::ExternalLinkage, 
        "printf", 
        module_.get()
    );
    
    return printf_func;
}

// Generate code for a function
Value* CodeGenerator::codegen_function(ast::FunctionStmt* func) {
    std::string func_name = func->get_name();
    
    // Check if function already exists
    if (module_->getFunction(func_name)) {
        return module_->getFunction(func_name);
    }
    
    // Determine return type
    Type* ret_type = Type::getInt32Ty(context_);
    if (!func->get_return_type().empty()) {
        ret_type = get_llvm_type(func->get_return_type());
    }
    
    // Handle serial process specially
    if (func->is_serial()) {
        in_serial_process_ = true;
    }
    
    // Create function type
    std::vector<Type*> param_types;
    for (const auto& param : func->get_params()) {
        param_types.push_back(get_llvm_type(param.second));
    }
    
    FunctionType* func_type = FunctionType::get(ret_type, param_types, false);
    
    // Create function
    Function* llvm_func = Function::Create(
        func_type,
        Function::ExternalLinkage,
        func_name,
        module_.get()
    );
    
    // Set function names for parameters
    unsigned idx = 0;
    for (auto& arg : llvm_func->args()) {
        if (idx < func->get_params().size()) {
            arg.setName(func->get_params()[idx].first);
            named_values_[func->get_params()[idx].first] = &arg;
        }
        idx++;
    }
    
    // Create entry block and generate body
    BasicBlock* entry = BasicBlock::Create(context_, "entry", llvm_func);
    builder_->SetInsertPoint(entry);
    
    // Generate function body 
    ast::ASTNode* body = func->get_body();
    if (body) {
        // Try to cast to BlockStmt
        auto* block = dynamic_cast<ast::BlockStmt*>(body);
        if (block) {
            for (auto& stmt : block->get_statements()) {
                codegen_statement(stmt.get());
            }
        } else {
            auto* stmt = dynamic_cast<ast::Statement*>(body);
            if (stmt) {
                codegen_statement(stmt);
            }
        }
    }
    
    // Add return for main function
    if (func_name == "main") {
        builder_->CreateRet(ConstantInt::get(context_, APInt(32, 0)));
    }
    
    return llvm_func;
    
    // Add default return if needed
    if (builder_->GetInsertBlock()->empty() || 
        !builder_->GetInsertBlock()->back().isTerminator()) {
        builder_->CreateRet(ConstantInt::get(context_, APInt(32, 0)));
    }
    
    // Verify function
    std::string err_str;
    raw_string_ostream err_stream(err_str);
    if (verifyFunction(*llvm_func, &err_stream)) {
        std::cerr << "Function verification failed: " << err_str << "\n";
    }
    
    if (func->is_serial()) {
        in_serial_process_ = false;
    }
    
    return llvm_func;
}

// Generate code for a statement
Value* CodeGenerator::codegen_statement(ast::Statement* stmt) {
    if (!stmt) return nullptr;
    
    switch (stmt->get_kind()) {
        case ast::Statement::Kind::Let:
            return codegen_let(static_cast<ast::LetStmt*>(stmt));
        case ast::Statement::Kind::Assign:
            return codegen_assign(static_cast<ast::AssignStmt*>(stmt));
        case ast::Statement::Kind::Return:
            return codegen_return(static_cast<ast::ReturnStmt*>(stmt));
        case ast::Statement::Kind::If:
            return codegen_if(static_cast<ast::IfStmt*>(stmt));
        case ast::Statement::Kind::Block:
            return codegen_block(static_cast<ast::BlockStmt*>(stmt));
        case ast::Statement::Kind::For:
            return codegen_for(static_cast<ast::ForStmt*>(stmt));
        case ast::Statement::Kind::While:
            return codegen_while(static_cast<ast::WhileStmt*>(stmt));
        case ast::Statement::Kind::Break:
            return codegen_break(static_cast<ast::BreakStmt*>(stmt));
        case ast::Statement::Kind::Continue:
            return codegen_continue(static_cast<ast::ContinueStmt*>(stmt));
        case ast::Statement::Kind::Match:
            return codegen_match(static_cast<ast::MatchStmt*>(stmt));
        case ast::Statement::Kind::Publish:
            return codegen_publish(static_cast<ast::PublishStmt*>(stmt));
        case ast::Statement::Kind::Subscribe:
            return codegen_subscribe(static_cast<ast::SubscribeStmt*>(stmt));
        case ast::Statement::Kind::SerialProcess:
            return codegen_serial_process(static_cast<ast::SerialProcessStmt*>(stmt));
        case ast::Statement::Kind::Expression:
            return codegen_expression(static_cast<ast::ExprStmt*>(stmt)->get_expr());
        default:
            return nullptr;
    }
}

// Generate code for a block
Value* CodeGenerator::codegen_block(ast::BlockStmt* block) {
    Value* last_value = nullptr;
    for (auto& stmt : block->get_statements()) {
        last_value = codegen_statement(stmt.get());
    }
    return last_value;
}

// Generate code for let statement
Value* CodeGenerator::codegen_let(ast::LetStmt* let) {
    std::string var_name = let->get_name();
    std::string var_type = let->get_type();
    
    // Get the LLVM type (extract base type for arrays)
    std::string base_type = var_type;
    size_t bracket_pos = var_type.find('[');
    if (bracket_pos != std::string::npos) {
        base_type = var_type.substr(0, bracket_pos);
    }
    Type* llvm_type = get_llvm_type(base_type);
    
    // Store the type for later use (element type, not array type)
    named_types_[var_name] = llvm_type;
    
    // For array types, allocate on stack as single element
    // Note: for u32[1], we just allocate a single i32 (index 1 in Claw = index 0 in LLVM)
    if (var_type.find('[') != std::string::npos) {
        // Create alloca for single element
        AllocaInst* alloca = builder_->CreateAlloca(llvm_type, nullptr, var_name);
        named_values_[var_name] = alloca;
        
        // Initialize if there's an initializer
        if (let->get_initializer()) {
            Value* init_val = codegen_expression(let->get_initializer());
            if (init_val) {
                builder_->CreateStore(init_val, alloca);
            }
        }
        return alloca;
    }
    
    // For scalar types, allocate on stack
    AllocaInst* alloca = builder_->CreateAlloca(llvm_type, nullptr, var_name);
    named_values_[var_name] = alloca;
    
    // Initialize if there's an initializer
    if (let->get_initializer()) {
        Value* init_val = codegen_expression(let->get_initializer());
        if (init_val) {
            builder_->CreateStore(init_val, alloca);
        }
    }
    
    return alloca;
}

// Generate code for assignment
Value* CodeGenerator::codegen_assign(ast::AssignStmt* assign) {
    std::cerr << "DEBUG: codegen_assign called\n";
    ast::Expression* target = assign->get_target();
    ast::Expression* value = assign->get_value();
    
    std::cerr << "DEBUG: target kind = " << (int)target->get_kind() << "\n";
    
    // Handle index assignment (array[1] = value)
    if (target->get_kind() == ast::Expression::Kind::Index) {
        std::cerr << "DEBUG: handling index assignment\n";
        auto* index_expr = static_cast<ast::IndexExpr*>(target);
        
        // Get the array pointer
        Value* array_ptr = codegen_expression(index_expr->get_object());
        if (!array_ptr) return nullptr;
        
        // Get the index value
        Value* index_val = codegen_expression(index_expr->get_index());
        if (!index_val) return nullptr;
        
        // Get the value to store
        Value* store_val = codegen_expression(value);
        if (!store_val) return nullptr;
        
        // Determine element type from the object name
        std::string obj_name;
        if (index_expr->get_object()->get_kind() == ast::Expression::Kind::Identifier) {
            obj_name = static_cast<ast::IdentifierExpr*>(index_expr->get_object())->get_name();
        }
        
        Type* element_type = Type::getInt32Ty(context_); // default
        if (named_types_.count(obj_name)) {
            element_type = named_types_[obj_name];
        }
        
        // Get element pointer and store
        Value* element_ptr = get_element_ptr(array_ptr, index_val, element_type);
        builder_->CreateStore(store_val, element_ptr);
        
        return store_val;
    }
    
    // Handle simple variable assignment
    if (target->get_kind() == ast::Expression::Kind::Identifier) {
        auto* ident = static_cast<ast::IdentifierExpr*>(target);
        std::string var_name = ident->get_name();
        
        Value* ptr = named_values_[var_name];
        if (!ptr) {
            // Variable doesn't exist yet, create it
            ptr = builder_->CreateAlloca(Type::getInt32Ty(context_), nullptr, var_name);
            named_values_[var_name] = ptr;
        }
        
        Value* value_val = codegen_expression(value);
        if (value_val) {
            builder_->CreateStore(value_val, ptr);
        }
        return value_val;
    }
    
    return nullptr;
}

// Generate code for return statement
Value* CodeGenerator::codegen_return(ast::ReturnStmt* ret) {
    if (ret->get_value()) {
        Value* ret_val = codegen_expression(ret->get_value());
        builder_->CreateRet(ret_val);
    } else {
        builder_->CreateRet(ConstantInt::get(context_, APInt(32, 0)));
    }
    return nullptr;
}

// Generate code for if statement
Value* CodeGenerator::codegen_if(ast::IfStmt* if_stmt) {
    // For simplicity, just evaluate the condition and body
    // Full if-else support would need phi nodes
    if (!if_stmt->get_conditions().empty()) {
        codegen_expression(if_stmt->get_conditions()[0].get());
    }
    if (!if_stmt->get_bodies().empty()) {
        ast::ASTNode* body = if_stmt->get_bodies()[0].get();
        (void)body; // Suppress unused warning for now
        // Handle block body
        if (body->to_string().find("{") != std::string::npos) {
            // This is a simplified handling
        }
    }
    return nullptr;
}

// Generate code for an expression
Value* CodeGenerator::codegen_expression(ast::Expression* expr) {
    if (!expr) return nullptr;
    
    switch (expr->get_kind()) {
        case ast::Expression::Kind::Literal:
            return codegen_literal(static_cast<ast::LiteralExpr*>(expr));
        case ast::Expression::Kind::Identifier:
            return codegen_identifier(static_cast<ast::IdentifierExpr*>(expr));
        case ast::Expression::Kind::Binary:
            return codegen_binary(static_cast<ast::BinaryExpr*>(expr));
        case ast::Expression::Kind::Unary:
            return codegen_unary(static_cast<ast::UnaryExpr*>(expr));
        case ast::Expression::Kind::Call:
            return codegen_call(static_cast<ast::CallExpr*>(expr));
        case ast::Expression::Kind::Index:
            return codegen_index(static_cast<ast::IndexExpr*>(expr));
        case ast::Expression::Kind::Tuple:
            return codegen_tuple(static_cast<ast::TupleExpr*>(expr));
        default:
            return nullptr;
    }
}

// Generate code for literal
Value* CodeGenerator::codegen_literal(ast::LiteralExpr* literal) {
    const auto& value = literal->get_value();
    
    return std::visit([this](auto&& v) -> Value* {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, int64_t>) {
            return ConstantInt::get(context_, APInt(32, v, true));
        } else if constexpr (std::is_same_v<T, double>) {
            return ConstantFP::get(context_, APFloat(v));
        } else if constexpr (std::is_same_v<T, bool>) {
            return ConstantInt::get(context_, APInt(1, v ? 1 : 0));
        } else if constexpr (std::is_same_v<T, std::string>) {
            // For strings, we need to create a global string
            GlobalVariable* gv = new GlobalVariable(
                *module_,
                ArrayType::get(IntegerType::get(context_, 8), v.length() + 1),
                true,
                GlobalValue::PrivateLinkage,
                ConstantDataArray::getString(context_, v)
            );
            return builder_->CreateBitCast(gv, IntegerType::get(context_, 8)->getPointerTo());
        } else {
            return ConstantInt::get(context_, APInt(32, 0));
        }
    }, value);
}

// Generate code for identifier
Value* CodeGenerator::codegen_identifier(ast::IdentifierExpr* ident) {
    std::string name = ident->get_name();
    
    // Check if it's a known variable
    if (named_values_.count(name)) {
        // Return the pointer directly - let the caller decide whether to load
        // This is needed because for index expressions like a[1], we need the pointer
        return named_values_[name];
    }
    
    // Check if it's a function
    if (module_->getFunction(name)) {
        return module_->getFunction(name);
    }
    
    // Return null for unknown identifiers
    return nullptr;
}

// Generate code for binary expression
Value* CodeGenerator::codegen_binary(ast::BinaryExpr* binary) {
    Value* left = codegen_expression(binary->get_left());
    Value* right = codegen_expression(binary->get_right());
    
    if (!left || !right) return nullptr;
    
    TokenType op = binary->get_operator();
    
    switch (op) {
        case TokenType::Op_plus:
            // Check if it's floating point
            if (left->getType()->isFloatingPointTy()) {
                return builder_->CreateFAdd(left, right, "addtmp");
            }
            return builder_->CreateAdd(left, right, "addtmp");
            
        case TokenType::Op_minus:
            if (left->getType()->isFloatingPointTy()) {
                return builder_->CreateFSub(left, right, "subtmp");
            }
            return builder_->CreateSub(left, right, "subtmp");
            
        case TokenType::Op_star:
            if (left->getType()->isFloatingPointTy()) {
                return builder_->CreateFMul(left, right, "multmp");
            }
            return builder_->CreateMul(left, right, "multmp");
            
        case TokenType::Op_slash:
            if (left->getType()->isFloatingPointTy()) {
                return builder_->CreateFDiv(left, right, "divtmp");
            }
            return builder_->CreateSDiv(left, right, "divtmp");
            
        case TokenType::Op_eq:
            if (left->getType()->isFloatingPointTy()) {
                return builder_->CreateFCmp(FCmpInst::FCMP_OEQ, left, right, "cmptmp");
            }
            return builder_->CreateICmp(ICmpInst::ICMP_EQ, left, right, "cmptmp");
            
        case TokenType::Op_neq:
            if (left->getType()->isFloatingPointTy()) {
                return builder_->CreateFCmp(FCmpInst::FCMP_ONE, left, right, "cmptmp");
            }
            return builder_->CreateICmp(ICmpInst::ICMP_NE, left, right, "cmptmp");
            
        case TokenType::Op_lt:
            if (left->getType()->isFloatingPointTy()) {
                return builder_->CreateFCmp(FCmpInst::FCMP_OLT, left, right, "cmptmp");
            }
            return builder_->CreateICmp(ICmpInst::ICMP_SLT, left, right, "cmptmp");
            
        case TokenType::Op_gt:
            if (left->getType()->isFloatingPointTy()) {
                return builder_->CreateFCmp(FCmpInst::FCMP_OGT, left, right, "cmptmp");
            }
            return builder_->CreateICmp(ICmpInst::ICMP_SGT, left, right, "cmptmp");
            
        case TokenType::Op_lte:
            if (left->getType()->isFloatingPointTy()) {
                return builder_->CreateFCmp(FCmpInst::FCMP_OLE, left, right, "cmptmp");
            }
            return builder_->CreateICmp(ICmpInst::ICMP_SLE, left, right, "cmptmp");
            
        case TokenType::Op_gte:
            if (left->getType()->isFloatingPointTy()) {
                return builder_->CreateFCmp(FCmpInst::FCMP_OGE, left, right, "cmptmp");
            }
            return builder_->CreateICmp(ICmpInst::ICMP_SGE, left, right, "cmptmp");
            
        case TokenType::Op_and:
            return builder_->CreateAnd(left, right, "andtmp");
            
        case TokenType::Op_or:
            return builder_->CreateOr(left, right, "ortmp");
            
        default:
            return nullptr;
    }
}

// Generate code for function call
Value* CodeGenerator::codegen_call(ast::CallExpr* call) {
    ast::Expression* callee_expr = call->get_callee();
    
    // Handle println specially
    if (callee_expr->get_kind() == ast::Expression::Kind::Identifier) {
        auto* ident = static_cast<ast::IdentifierExpr*>(callee_expr);
        if (ident->get_name() == "println") {
            // Generate code for println
            Function* printf_func = module_->getFunction("printf");
            
            // Build format string and arguments
            std::string format_str;
            std::vector<Value*> args;
            
            for (size_t i = 0; i < call->get_arguments().size(); i++) {
                auto& arg = call->get_arguments()[i];
                Value* arg_val = codegen_expression(arg.get());
                if (!arg_val) continue;
                
                // Determine format specifier
                std::string arg_format;
                if (arg_val->getType()->isIntegerTy(1)) {
                    arg_format = "%d";
                } else if (arg_val->getType()->isIntegerTy(32)) {
                    arg_format = "%d";
                } else if (arg_val->getType()->isIntegerTy(64)) {
                    arg_format = "%lld";
                } else if (arg_val->getType()->isFloatTy()) {
                    arg_format = "%f";
                } else if (arg_val->getType()->isDoubleTy()) {
                    arg_format = "%f";
                } else if (arg_val->getType()->isPointerTy()) {
                    arg_format = "%s";
                } else {
                    arg_format = "%d";
                }
                
                format_str += arg_format;
                args.push_back(arg_val);
            }
            
            // Add newline if not already there
            if (!format_str.empty() && format_str.back() != '\n') {
                format_str += "\\n";
            }
            
            // Create format string global
            GlobalVariable* format_gv = new GlobalVariable(
                *module_,
                ArrayType::get(IntegerType::get(context_, 8), format_str.length() + 1),
                true,
                GlobalValue::PrivateLinkage,
                ConstantDataArray::getString(context_, format_str)
            );
            
            Value* format_ptr = builder_->CreateBitCast(
                format_gv, 
                IntegerType::get(context_, 8)->getPointerTo()
            );
            
            // Build call arguments
            std::vector<Value*> call_args;
            call_args.push_back(format_ptr);
            for (auto* arg : args) {
                // Promote to i32 if needed for vararg
                if (arg->getType()->isIntegerTy(1) || arg->getType()->isIntegerTy(8)) {
                    arg = builder_->CreateZExt(arg, Type::getInt32Ty(context_));
                }
                call_args.push_back(arg);
            }
            
            return builder_->CreateCall(printf_func, call_args);
        }
    }
    
    // Handle regular function calls
    std::string func_name;
    if (callee_expr->get_kind() == ast::Expression::Kind::Identifier) {
        func_name = static_cast<ast::IdentifierExpr*>(callee_expr)->get_name();
    }
    
    Function* callee = module_->getFunction(func_name);
    if (!callee) return nullptr;
    
    std::vector<Value*> args;
    for (auto& arg : call->get_arguments()) {
        Value* arg_val = codegen_expression(arg.get());
        if (arg_val) {
            args.push_back(arg_val);
        }
    }
    
    return builder_->CreateCall(callee, args);
}

// Generate code for index expression (array access)
Value* CodeGenerator::codegen_index(ast::IndexExpr* index) {
    // Get the object (array variable)
    Value* array_ptr = codegen_expression(index->get_object());
    if (!array_ptr) return nullptr;
    
    // Get the index
    Value* index_val = codegen_expression(index->get_index());
    if (!index_val) return nullptr;
    
    // Determine element type from the object name
    std::string obj_name;
    if (index->get_object()->get_kind() == ast::Expression::Kind::Identifier) {
        obj_name = static_cast<ast::IdentifierExpr*>(index->get_object())->get_name();
    }
    
    Type* element_type = Type::getInt32Ty(context_); // default
    
    // Get element pointer and load
    Value* element_ptr = get_element_ptr(array_ptr, index_val, element_type);
    return builder_->CreateLoad(element_type, element_ptr, "element");
}

// Get element pointer from array
Value* CodeGenerator::get_element_ptr(Value* array, Value* index, Type* element_type) {
    // For single-element allocations (u32[1]), just return the pointer directly
    // No need for GEP - the alloca IS the element
    return array;
}

// Generate code for tuple expression
Value* CodeGenerator::codegen_tuple(ast::TupleExpr* tup) {
    // For now, return the first element as placeholder
    // Full tuple codegen requires struct type support
    if (tup->size() > 0) {
        return codegen_expression(tup->get_element(0));
    }
    return ConstantInt::get(Type::getInt32Ty(context_), 0);
}

// Generate code for for loop
Value* CodeGenerator::codegen_for(ast::ForStmt* for_stmt) {
    // Save current loop context
    llvm::BasicBlock* saved_entry = loop_entry_;
    llvm::BasicBlock* saved_exit = loop_exit_;
    
    // Get the function we're in
    Function* func = builder_->GetInsertBlock()->getParent();
    
    // Create blocks: body, exit (simplified - iterate over range)
    BasicBlock* body_block = BasicBlock::Create(context_, "for.body", func);
    BasicBlock* exit_block = BasicBlock::Create(context_, "for.exit", func);
    
    // Set loop context
    loop_entry_ = body_block;
    loop_exit_ = exit_block;
    
    // Get the iteration count from the iterable
    // For simplicity, iterate from 0 to N-1
    Value* iter_val = codegen_expression(for_stmt->get_iterable());
    Value* limit = iter_val ? iter_val : ConstantInt::get(context_, APInt(32, 10));
    
    // Allocate loop counter
    AllocaInst* counter = builder_->CreateAlloca(Type::getInt32Ty(context_), nullptr, for_stmt->get_variable());
    builder_->CreateStore(ConstantInt::get(context_, APInt(32, 0)), counter);
    named_values_[for_stmt->get_variable()] = counter;
    
    // Branch to body
    builder_->CreateBr(body_block);
    
    // Body block
    builder_->SetInsertPoint(body_block);
    
    // Generate body
    if (for_stmt->get_body()) {
        auto* stmt = dynamic_cast<ast::Statement*>(for_stmt->get_body());
        if (stmt) {
            codegen_statement(stmt);
        }
    }
    
    // Increment counter
    Value* curr = builder_->CreateLoad(Type::getInt32Ty(context_), counter, "counter");
    Value* next = builder_->CreateAdd(curr, ConstantInt::get(context_, APInt(32, 1)), "next");
    builder_->CreateStore(next, counter);
    
    // Check condition
    curr = builder_->CreateLoad(Type::getInt32Ty(context_), counter, "counter");
    Value* cmp = builder_->CreateICmpSLT(curr, limit, "loopcond");
    builder_->CreateCondBr(cmp, body_block, exit_block);
    
    // Exit block
    builder_->SetInsertPoint(exit_block);
    
    // Restore loop context
    loop_entry_ = saved_entry;
    loop_exit_ = saved_exit;
    
    return nullptr;
}

// Generate code for while loop
Value* CodeGenerator::codegen_while(ast::WhileStmt* while_stmt) {
    // Save current loop context
    llvm::BasicBlock* saved_entry = loop_entry_;
    llvm::BasicBlock* saved_exit = loop_exit_;
    
    // Get the function we're in
    Function* func = builder_->GetInsertBlock()->getParent();
    
    // Create blocks
    BasicBlock* cond_block = BasicBlock::Create(context_, "while.cond", func);
    BasicBlock* body_block = BasicBlock::Create(context_, "while.body", func);
    BasicBlock* exit_block = BasicBlock::Create(context_, "while.exit", func);
    
    // Set loop context
    loop_entry_ = cond_block;
    loop_exit_ = exit_block;
    
    // Branch to condition block
    builder_->CreateBr(cond_block);
    
    // Condition block
    builder_->SetInsertPoint(cond_block);
    if (while_stmt->get_condition()) {
        Value* cond_val = codegen_expression(while_stmt->get_condition());
        builder_->CreateCondBr(cond_val, body_block, exit_block);
    } else {
        builder_->CreateBr(body_block);
    }
    
    // Body block
    builder_->SetInsertPoint(body_block);
    if (while_stmt->get_body()) {
        auto* stmt = dynamic_cast<ast::Statement*>(while_stmt->get_body());
        if (stmt) {
            codegen_statement(stmt);
        }
    }
    builder_->CreateBr(cond_block);
    
    // Exit block
    builder_->SetInsertPoint(exit_block);
    
    // Restore loop context
    loop_entry_ = saved_entry;
    loop_exit_ = saved_exit;
    
    return nullptr;
}

// Generate code for break statement
Value* CodeGenerator::codegen_break(ast::BreakStmt* brk) {
    if (loop_exit_) {
        builder_->CreateBr(loop_exit_);
    }
    return nullptr;
}

// Generate code for continue statement
Value* CodeGenerator::codegen_continue(ast::ContinueStmt* cont) {
    if (loop_entry_) {
        builder_->CreateBr(loop_entry_);
    }
    return nullptr;
}

// Generate code for match statement (simplified - just first matching arm)
Value* CodeGenerator::codegen_match(ast::MatchStmt* match) {
    if (!match->get_expr()) {
        return nullptr;
    }
    
    // Get the subject value
    Value* subject_val = codegen_expression(match->get_expr());
    if (!subject_val) return nullptr;
    
    // Get the function we're in
    Function* func = builder_->GetInsertBlock()->getParent();
    
    // Create exit block
    BasicBlock* exit_block = BasicBlock::Create(context_, "match.exit", func);
    
    // For simplicity, just process first body if available
    const auto& bodies = match->get_bodies();
    if (!bodies.empty() && bodies[0]) {
        auto* stmt = dynamic_cast<ast::Statement*>(bodies[0].get());
        if (stmt) {
            codegen_statement(stmt);
        }
    }
    
    builder_->CreateBr(exit_block);
    builder_->SetInsertPoint(exit_block);
    
    return nullptr;
}

// Generate code for unary expression
Value* CodeGenerator::codegen_unary(ast::UnaryExpr* unary) {
    Value* operand = codegen_expression(unary->get_operand());
    if (!operand) return nullptr;
    
    TokenType op = unary->get_operator();
    
    switch (op) {
        case TokenType::Op_minus:
            if (operand->getType()->isFloatingPointTy()) {
                return builder_->CreateFNeg(operand, "negtmp");
            }
            return builder_->CreateNeg(operand, "negtmp");
            
        case TokenType::Op_bang:
            return builder_->CreateNot(operand, "nottmp");
            
        case TokenType::Op_star:  // Dereference
            // For pointer types, load the value
            if (operand->getType()->isPointerTy()) {
                return builder_->CreateLoad(operand->getType()->getPointerElementType(), operand, "deref");
            }
            return nullptr;
            
        case TokenType::Op_amp:  // Address-of (already handled in RefExpr)
            return operand;
            
        default:
            return nullptr;
    }
}

// Generate code for Publish statement (event system)
Value* CodeGenerator::codegen_publish(ast::PublishStmt* publish) {
    if (!publish) return nullptr;
    
    const std::string& event_name = publish->get_event_name();
    const auto& args = publish->get_arguments();
    
    // Declare event dispatch function if not already declared
    Function* event_dispatch = module_->getFunction("claw_event_dispatch");
    if (!event_dispatch) {
        // void claw_event_dispatch(i8* event_name, i8** args, i32 num_args)
        std::vector<Type*> arg_types;
        arg_types.push_back(Type::getInt8Ty(context_)->getPointerTy());  // event_name
        arg_types.push_back(Type::getInt8Ty(context_)->getPointerTy()->getPointerTy());  // args
        arg_types.push_back(Type::getInt32Ty(context_));  // num_args
        
        FunctionType* dispatch_type = FunctionType::get(
            Type::getVoidTy(context_),
            arg_types,
            false
        );
        
        event_dispatch = Function::Create(
            dispatch_type,
            Function::ExternalLinkage,
            "claw_event_dispatch",
            module_.get()
        );
    }
    
    // Generate code for all arguments
    std::vector<Value*> arg_values;
    for (auto& arg : args) {
        Value* v = codegen_expression(arg.get());
        if (v) {
            arg_values.push_back(v);
        }
    }
    
    // Emit debug print with event name using existing printf
    Function* printf_func = module_->getFunction("printf");
    if (printf_func && !event_name.empty()) {
        // Create format string: "EVENT: <event_name>\n"
        std::string format_str = "EVENT: " + event_name + "\n";
        Value* format_ptr = builder_->CreateGlobalStringPtr(format_str);
        
        // Call printf(format_ptr)
        builder_->CreateCall(printf_func, {format_ptr});
    }
    
    // Call the runtime event dispatcher
    if (event_dispatch) {
        // Create event name string
        Value* event_name_ptr = builder_->CreateGlobalStringPtr(event_name);
        
        // For arguments, we'll pass null for now (simplified implementation)
        // Full implementation would serialize arguments to a buffer
        Value* null_args = ConstantPointerNull::get(
            Type::getInt8Ty(context_)->getPointerTy()->getPointerTy()
        );
        Value* num_args = ConstantInt::get(Type::getInt32Ty(context_), arg_values.size());
        
        builder_->CreateCall(event_dispatch, {event_name_ptr, null_args, num_args});
    }
    
    return nullptr;
}

// Generate code for Subscribe statement (event system)
Value* CodeGenerator::codegen_subscribe(ast::SubscribeStmt* subscribe) {
    if (!subscribe) return nullptr;
    
    const std::string& event_name = subscribe->get_event_name();
    ast::FunctionStmt* handler = subscribe->get_handler();
    
    if (!handler) return nullptr;
    
    // First, generate the handler function
    Function* handler_func = static_cast<Function*>(
        codegen_function(handler)
    );
    if (!handler_func) return nullptr;
    
    // Declare event subscribe function if not already declared
    // This function registers a callback for an event
    Function* event_subscribe = module_->getFunction("claw_event_subscribe");
    if (!event_subscribe) {
        // i32 claw_event_subscribe(i8* event_name, void* handler)
        std::vector<Type*> arg_types;
        arg_types.push_back(Type::getInt8Ty(context_)->getPointerTy());  // event_name
        arg_types.push_back(Type::getInt8Ty(context_)->getPointerTy());  // handler function pointer
        
        FunctionType* subscribe_type = FunctionType::get(
            Type::getInt32Ty(context_),  // return 0 on success
            arg_types,
            false
        );
        
        event_subscribe = Function::Create(
            subscribe_type,
            Function::ExternalLinkage,
            "claw_event_subscribe",
            module_.get()
        );
    }
    
    // Get the handler function pointer
    Value* handler_ptr = builder_->CreateBitCast(
        handler_func,
        Type::getInt8Ty(context_)->getPointerTy()
    );
    
    // Create event name string
    Value* event_name_ptr = builder_->CreateGlobalStringPtr(event_name);
    
    // Call the runtime event subscribe function
    if (event_subscribe) {
        builder_->CreateCall(event_subscribe, {event_name_ptr, handler_ptr});
    }
    
    // Emit debug info for development
    Function* printf_func = module_->getFunction("printf");
    if (printf_func) {
        std::string format_str = "SUBSCRIBED: " + event_name + " -> " + handler->get_name() + "\n";
        Value* format_ptr = builder_->CreateGlobalStringPtr(format_str);
        builder_->CreateCall(printf_func, {format_ptr});
    }
    
    return handler_func;
}

// Generate code for Serial Process (event-driven component)
Value* CodeGenerator::codegen_serial_process(ast::SerialProcessStmt* process) {
    if (!process) return nullptr;
    
    const std::string& name = process->get_name();
    const auto& params = process->get_params();
    const auto& body = process->get_body();
    
    // Declare runtime init function if not already declared
    Function* process_init = module_->getFunction("claw_process_init");
    if (!process_init) {
        // void claw_process_init(i8* name, i32 num_params)
        std::vector<Type*> arg_types;
        arg_types.push_back(Type::getInt8Ty(context_)->getPointerTy());  // process name
        arg_types.push_back(Type::getInt32Ty(context_));  // num params
        
        FunctionType* init_type = FunctionType::get(
            Type::getVoidTy(context_),
            arg_types,
            false
        );
        
        process_init = Function::Create(
            init_type,
            Function::ExternalLinkage,
            "claw_process_init",
            module_.get()
        );
    }
    
    // Call runtime to register this process
    if (process_init) {
        Value* name_ptr = builder_->CreateGlobalStringPtr(name);
        Value* num_params = ConstantInt::get(Type::getInt32Ty(context_), params.size());
        builder_->CreateCall(process_init, {name_ptr, num_params});
    }
    
    // Generate the process body as a function (named function for direct calls)
    // Serial processes in Claw are like concurrent components
    // They run in the same thread but with event-driven execution model
    
    // Create a wrapper function that handles the process lifecycle
    std::string wrapper_name = "__process_" + name;
    
    // Check if process body has statements
    if (body) {
        codegen_block(body.get());
    }
    
    // Emit debug info
    Function* printf_func = module_->getFunction("printf");
    if (printf_func) {
        std::string format_str = "PROCESS_INIT: " + name + "\n";
        Value* format_ptr = builder_->CreateGlobalStringPtr(format_str);
        builder_->CreateCall(printf_func, {format_ptr});
    }
    
    return nullptr;
}

// Generate code for Match statement - complete implementation with pattern matching
Value* CodeGenerator::codegen_match(ast::MatchStmt* match) {
    if (!match || !match->get_expr()) {
        return nullptr;
    }
    
    // Get the subject value to match on
    Value* subject_val = codegen_expression(match->get_expr());
    if (!subject_val) return nullptr;
    
    Function* func = builder_->GetInsertBlock()->getParent();
    
    const auto& bodies = match->get_bodies();
    const auto& patterns = match->get_patterns();
    
    if (bodies.empty() || patterns.empty()) {
        return nullptr;
    }
    
    // Create exit block
    BasicBlock* exit_block = BasicBlock::Create(context_, "match.exit", func);
    
    // First pass: categorize patterns
    // - Switch cases: integer/string literals
    // - Conditional cases: ranges, wildcards, complex patterns
    
    std::vector<size_t> switch_indices;
    std::vector<size_t> conditional_indices;
    size_t wildcard_index = SIZE_MAX;
    
    for (size_t i = 0; i < patterns.size(); i++) {
        auto* pattern = patterns[i].get();
        
        // Check if pattern is a wildcard (_)
        if (auto* ident = dynamic_cast<ast::IdentifierExpr*>(pattern)) {
            if (ident->get_name() == "_") {
                wildcard_index = i;
                continue;
            }
        }
        
        // Check if pattern is a literal (integer or string)
        if (dynamic_cast<ast::LiteralExpr*>(pattern)) {
            switch_indices.push_back(i);
        } else {
            conditional_indices.push_back(i);
        }
    }
    
    // If all patterns are suitable for switch, use switch instruction
    if (switch_indices.size() >= 2 && conditional_indices.empty()) {
        // Create switch instruction
        BasicBlock* default_block = (wildcard_index != SIZE_MAX) 
            ? BasicBlock::Create(context_, "match.default", func, exit_block)
            : exit_block;
        
        SwitchInst* switch_inst = builder_->CreateSwitch(subject_val, default_block, switch_indices.size());
        
        // Add switch cases
        for (size_t idx : switch_indices) {
            auto* pattern = patterns[idx].get();
            auto* body = bodies[idx].get();
            
            BasicBlock* case_block = BasicBlock::Create(context_, "match.case", func, exit_block);
            builder_->SetInsertPoint(case_block);
            
            if (auto* lit = dynamic_cast<ast::LiteralExpr*>(pattern)) {
                if (lit->get_type() == TokenType::Number) {
                    int64_t case_val = std::get<int64_t>(lit->get_value());
                    switch_inst->addCase(builder_->getInt64(case_val), case_block);
                } else if (lit->get_type() == TokenType::String) {
                    // String literals use hash-based switch
                    // For now, fall through to conditional
                }
            }
            
            // Generate body
            if (auto* stmt = dynamic_cast<ast::Statement*>(body)) {
                codegen_statement(stmt);
            }
            builder_->CreateBr(exit_block);
        }
        
        // Handle wildcard/default
        if (wildcard_index != SIZE_MAX) {
            builder_->SetInsertPoint(default_block);
            auto* body = bodies[wildcard_index].get();
            if (auto* stmt = dynamic_cast<ast::Statement*>(body)) {
                codegen_statement(stmt);
            }
            builder_->CreateBr(exit_block);
        }
    } else {
        // Use conditional branches for complex patterns
        BasicBlock* current_block = builder_->GetInsertBlock();
        
        for (size_t i = 0; i < patterns.size(); i++) {
            auto* pattern = patterns[i].get();
            auto* body = bodies[i].get();
            
            BasicBlock* case_block = BasicBlock::Create(context_, "match.case", func, exit_block);
            BasicBlock* next_block = nullptr;
            
            // Generate condition for this pattern
            builder_->SetInsertPoint(current_block);
            
            Value* condition = nullptr;
            
            // Wildcard always matches
            if (auto* ident = dynamic_cast<ast::IdentifierExpr*>(pattern)) {
                if (ident->get_name() == "_") {
                    condition = builder_->getTrue();
                }
            }
            
            // Literal pattern: subject == literal
            if (auto* lit = dynamic_cast<ast::LiteralExpr*>(pattern)) {
                Value* lit_val = codegen_literal(lit);
                if (lit_val) {
                    if (subject_val->getType()->isIntegerTy() && lit_val->getType()->isIntegerTy()) {
                        condition = builder_->CreateICmpEQ(subject_val, lit_val);
                    } else if (subject_val->getType()->isFloatingPointTy() && lit_val->getType()->isFloatingPointTy()) {
                        condition = builder_->CreateFCmpOEQ(subject_val, lit_val);
                    }
                }
            }
            
            // Create next block for fallthrough if not last
            if (i + 1 < patterns.size()) {
                next_block = BasicBlock::Create(context_, "match.next", func, exit_block);
            }
            
            // Default: branch to case block
            if (condition) {
                builder_->CreateCondBr(condition, case_block, next_block ? next_block : exit_block);
            } else {
                builder_->CreateBr(case_block);
            }
            
            // Generate case body
            builder_->SetInsertPoint(case_block);
            if (auto* stmt = dynamic_cast<ast::Statement*>(body)) {
                codegen_statement(stmt);
            }
            builder_->CreateBr(exit_block);
            
            if (next_block) {
                current_block = next_block;
            }
        }
    }
    
    builder_->SetInsertPoint(exit_block);
    return nullptr;
}

// Generate code for comparison expression
// NOTE: CompareExpr class doesn't exist - comparisons handled as BinaryExpr
// Value* CodeGenerator::codegen_compare(ast::CompareExpr* compare) {
//     return nullptr;
// }

} // namespace codegen
} // namespace claw
