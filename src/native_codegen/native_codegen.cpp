// native_codegen.cpp - 本地代码生成器实现
// Rewritten to match actual ir.h API (PrimitiveTypeKind, dynamic_cast, field access)

#include "native_codegen.h"
#include <cmath>
#include <sstream>
#include <algorithm>

namespace claw {
namespace codegen {

// ============================================================================
// CTypeMapper 实现
// ============================================================================

std::string CTypeMapper::to_c_type(const ir::Type* type) {
    if (!type) return "void";
    
    if (auto prim = dynamic_cast<const ir::PrimitiveType*>(type)) {
        switch (prim->kind) {
            case ir::PrimitiveTypeKind::Void:    return "void";
            case ir::PrimitiveTypeKind::Bool:    return "bool";
            case ir::PrimitiveTypeKind::Int8:    return "int8_t";
            case ir::PrimitiveTypeKind::Int16:   return "int16_t";
            case ir::PrimitiveTypeKind::Int32:   return "int32_t";
            case ir::PrimitiveTypeKind::Int64:   return "int64_t";
            case ir::PrimitiveTypeKind::UInt8:   return "uint8_t";
            case ir::PrimitiveTypeKind::UInt16:  return "uint16_t";
            case ir::PrimitiveTypeKind::UInt32:  return "uint32_t";
            case ir::PrimitiveTypeKind::UInt64:  return "uint64_t";
            case ir::PrimitiveTypeKind::Float32: return "float";
            case ir::PrimitiveTypeKind::Float64: return "double";
            case ir::PrimitiveTypeKind::String:  return "char*";
            case ir::PrimitiveTypeKind::Bytes:   return "uint8_t*";
            default: return "claw_value_t";
        }
    }
    if (auto arr = dynamic_cast<const ir::ArrayType*>(type)) {
        return to_c_type(arr->element_type.get()) + "*";
    }
    if (auto ptr = dynamic_cast<const ir::PointerType*>(type)) {
        return to_c_type(ptr->pointee.get()) + "*";
    }
    if (auto tup = (false)) {
        (void)tup;
        return "claw_tuple_t";
    }
    if (auto fn = dynamic_cast<const ir::FunctionType*>(type)) {
        (void)fn;
        return "claw_function_t";
    }
    if (auto tensor = dynamic_cast<const ir::TensorType*>(type)) {
        (void)tensor;
        return "claw_tensor_t";
    }
    return "claw_value_t";
}

std::string CTypeMapper::to_c_type(const std::string& claw_type) {
    if (claw_type == "int" || claw_type == "i32") return "int32_t";
    if (claw_type == "i64") return "int64_t";
    if (claw_type == "i8") return "int8_t";
    if (claw_type == "i16") return "int16_t";
    if (claw_type == "u32") return "uint32_t";
    if (claw_type == "u64") return "uint64_t";
    if (claw_type == "f32") return "float";
    if (claw_type == "f64") return "double";
    if (claw_type == "bool") return "bool";
    if (claw_type == "string") return "char*";
    if (claw_type == "void") return "void";
    return claw_type;
}

bool CTypeMapper::needs_pointer(const ir::Type* type) {
    if (!type) return false;
    return dynamic_cast<const ir::ArrayType*>(type) ||
           (false) ||
           dynamic_cast<const ir::TensorType*>(type) ||
           dynamic_cast<const ir::FunctionType*>(type) ||
           (dynamic_cast<const ir::PrimitiveType*>(type) &&
            static_cast<const ir::PrimitiveType*>(type)->kind == ir::PrimitiveTypeKind::String);
}

std::string CTypeMapper::get_type_name(const ir::Type* type) {
    if (!type) return "void";
    std::ostringstream oss;
    if (auto prim = dynamic_cast<const ir::PrimitiveType*>(type)) {
        switch (prim->kind) {
            case ir::PrimitiveTypeKind::Bool:    oss << "bool"; break;
            case ir::PrimitiveTypeKind::Int8:    oss << "int8_t"; break;
            case ir::PrimitiveTypeKind::Int16:   oss << "int16_t"; break;
            case ir::PrimitiveTypeKind::Int32:   oss << "int32_t"; break;
            case ir::PrimitiveTypeKind::Int64:   oss << "int64_t"; break;
            case ir::PrimitiveTypeKind::UInt8:   oss << "uint8_t"; break;
            case ir::PrimitiveTypeKind::UInt16:  oss << "uint16_t"; break;
            case ir::PrimitiveTypeKind::UInt32:  oss << "uint32_t"; break;
            case ir::PrimitiveTypeKind::UInt64:  oss << "uint64_t"; break;
            case ir::PrimitiveTypeKind::Float32: oss << "float"; break;
            case ir::PrimitiveTypeKind::Float64: oss << "double"; break;
            case ir::PrimitiveTypeKind::String:  oss << "char_ptr"; break;
            default: oss << "value"; break;
        }
    } else if (auto ptr = dynamic_cast<const ir::PointerType*>(type)) {
        oss << get_type_name(ptr->pointee.get()) << "_ptr";
    } else if (auto arr = dynamic_cast<const ir::ArrayType*>(type)) {
        oss << get_type_name(arr->element_type.get()) << "_array";
    } else if ((false)) {
        oss << "tuple";
    } else if (dynamic_cast<const ir::TensorType*>(type)) {
        oss << "tensor";
    } else {
        oss << "value";
    }
    return oss.str();
}

// ============================================================================
// NativeCodegen 实现
// ============================================================================

NativeCodegen::NativeCodegen(const NativeCodegenConfig& config) 
    : config_(config), indent_level_(0) {
}

std::string NativeCodegen::generate(const ir::Module& module) {
    current_module_ = &module;
    std::string result;
    
    if (config_.emit_headers) {
        result += generate_header(module);
        result += "\n\n";
    }
    result += generate_source(module);
    
    return result;
}

std::string NativeCodegen::generate_header(const ir::Module& module) {
    std::ostringstream oss;
    std::string guard = config_.output_prefix + module.name + "_H";
    std::transform(guard.begin(), guard.end(), guard.begin(), ::toupper);
    
    oss << "#ifndef " << guard << "\n";
    oss << "#define " << guard << "\n\n";
    
    oss << "#include <stdint.h>\n";
    oss << "#include <stdbool.h>\n";
    oss << "#include <stddef.h>\n\n";
    
    if (config_.emit_type_definitions) {
        oss << "// ============================================================\n";
        oss << "// 类型定义\n";
        oss << "// ============================================================\n\n";
        oss << generate_type_definitions();
    }
    
    oss << "// ============================================================\n";
    oss << "// 函数声明\n";
    oss << "// ============================================================\n\n";
    
    for (const auto& func : module.functions) {
        if (!func->is_extern) {
            oss << generate_function_signature(func.get()) << ";\n";
        }
    }
    
    oss << "\n#endif // " << guard << "\n";
    return oss.str();
}

std::string NativeCodegen::generate_source(const ir::Module& module) {
    std::ostringstream oss;
    
    oss << "// ============================================================\n";
    oss << "// Claw 编译器生成的 C 代码\n";
    oss << "// 模块: " << module.name << "\n";
    oss << "// 生成时间: " << __DATE__ << " " << __TIME__ << "\n";
    oss << "// ============================================================\n\n";
    
    oss << "#include \"" << config_.output_prefix << module.name << ".h\"\n\n";
    
    oss << "#include <stdio.h>\n";
    oss << "#include <stdlib.h>\n";
    oss << "#include <string.h>\n\n";
    
    if (config_.emit_type_definitions) {
        oss << generate_struct_definitions();
    }
    
    // 生成全局变量
    auto globals = generate_global_declarations();
    if (!globals.empty()) {
        oss << "// 全局变量\n";
        oss << globals << "\n";
    }
    
    // 生成函数定义
    oss << "// ============================================================\n";
    oss << "// 函数定义\n";
    oss << "// ============================================================\n\n";
    
    for (const auto& func : module.functions) {
        if (!func->is_extern && func->blocks.size() > 1) {
            // Only generate if it has body (more than entry block or has instructions)
            std::string func_code = generate_function(func.get());
            if (!func_code.empty()) {
                oss << func_code << "\n\n";
                stats_.num_functions++;
            }
        }
    }
    
    stats_.num_globals = module.globals.size();
    stats_.total_lines = 0;
    
    return oss.str();
}

std::string NativeCodegen::generate_type_definitions() {
    std::ostringstream oss;
    
    oss << "// 运行时值类型\n";
    oss << "typedef enum claw_value_tag {\n";
    oss << "    CLAW_VALUE_NIL,\n";
    oss << "    CLAW_VALUE_BOOL,\n";
    oss << "    CLAW_VALUE_INT,\n";
    oss << "    CLAW_VALUE_FLOAT,\n";
    oss << "    CLAW_VALUE_STRING,\n";
    oss << "    CLAW_VALUE_ARRAY,\n";
    oss << "    CLAW_VALUE_TUPLE,\n";
    oss << "    CLAW_VALUE_TENSOR,\n";
    oss << "    CLAW_VALUE_FUNCTION,\n";
    oss << "    CLAW_VALUE_POINTER\n";
    oss << "} claw_value_tag_t;\n\n";
    
    oss << "typedef struct claw_value {\n";
    oss << "    claw_value_tag_t tag;\n";
    oss << "    union {\n";
    oss << "        bool b;\n";
    oss << "        int64_t i;\n";
    oss << "        double f;\n";
    oss << "        char* s;\n";
    oss << "        void* p;\n";
    oss << "        struct { void* data; int64_t len; } arr;\n";
    oss << "        struct { void** fields; int64_t len; } tup;\n";
    oss << "        struct { void* data; int* shape; int32_t ndim; } tensor;\n";
    oss << "        struct { void* fn; void* env; } fn;\n";
    oss << "    } u;\n";
    oss << "} claw_value_t;\n\n";
    
    oss << "typedef claw_value_t (*claw_function_t)(void* ctx, int nargs, claw_value_t* args);\n\n";
    
    oss << "typedef struct claw_tuple {\n";
    oss << "    int64_t size;\n";
    oss << "    claw_value_t fields[];\n";
    oss << "} claw_tuple_t;\n\n";
    
    oss << "typedef struct claw_tensor {\n";
    oss << "    void* data;\n";
    oss << "    int32_t* shape;\n";
    oss << "    int32_t ndim;\n";
    oss << "    int32_t dtype;\n";
    oss << "} claw_tensor_t;\n\n";
    
    stats_.num_types = 5;
    return oss.str();
}

std::string NativeCodegen::generate_function_declarations() {
    std::ostringstream oss;
    for (const auto& func : current_module_->functions) {
        if (func->is_extern) {
            oss << generate_function_signature(func.get()) << ";\n";
        }
    }
    return oss.str();
}

std::string NativeCodegen::generate_global_declarations() {
    std::ostringstream oss;
    for (const auto& [gname, gval] : current_module_->globals) {
        auto type = gval->type ? CTypeMapper::to_c_type(gval->type.get()) : "claw_value_t";
        oss << "static " << type << " " << make_safe_name(gname) << ";\n";
    }
    return oss.str();
}

std::string NativeCodegen::generate_struct_definitions() {
    return "";
}

std::string NativeCodegen::generate_function_signature(const ir::Function* func) {
    std::ostringstream oss;
    auto ret_type = CTypeMapper::to_c_type(func->return_type.get());
    oss << ret_type << " " << make_safe_name(func->name) << "(";
    
    if (func->arguments.empty()) {
        oss << "void";
    } else {
        for (size_t i = 0; i < func->arguments.size(); i++) {
            if (i > 0) oss << ", ";
            auto param_type = CTypeMapper::to_c_type(func->arguments[i]->type.get());
            oss << param_type << " " << make_safe_name(func->arguments[i]->name);
        }
    }
    oss << ")";
    return oss.str();
}

std::string NativeCodegen::generate_function(const ir::Function* func) {
    current_function_ = func;
    local_vars_.clear();
    temp_var_counter_ = 0;
    
    std::ostringstream oss;
    
    if (config_.emit_comments) {
        oss << "// 函数: " << func->name << "\n";
    }
    
    oss << generate_function_signature(func) << " {\n";
    indent_level_++;
    
    oss << generate_function_body(func);
    
    indent_level_--;
    oss << "}\n";
    
    emitted_functions_.insert(func->name);
    return oss.str();
}

std::string NativeCodegen::generate_function_body(const ir::Function* func) {
    std::ostringstream oss;
    auto& blocks = func->blocks;
    
    if (blocks.empty()) {
        oss << indent() << "return;\n";
        return oss.str();
    }
    
    for (const auto& block : blocks) {
        if (config_.emit_comments) {
            oss << "\n" << indent() << "// 基本块: " << block->name << "\n";
        }
        oss << make_safe_name(block->name) << ":\n";
        indent_level_++;
        oss << generate_basic_block(block.get());
        indent_level_--;
    }
    
    return oss.str();
}

std::string NativeCodegen::generate_basic_block(const ir::BasicBlock* block) {
    std::ostringstream oss;
    
    for (const auto& inst : block->instructions) {
        auto inst_code = generate_instruction(inst.get());
        if (!inst_code.empty()) {
            oss << inst_code << "\n";
        }
    }
    
    return oss.str();
}

std::string NativeCodegen::generate_instruction(const ir::Instruction* inst) {
    if (!inst) return "";
    
    switch (inst->opcode) {
        case ir::OpCode::Alloca:    return generate_alloca(inst);
        case ir::OpCode::Load:      return generate_load(inst);
        case ir::OpCode::Store:     return generate_store(inst);
        case ir::OpCode::Ret:       return generate_return(inst);
        case ir::OpCode::Br:        return generate_branch(inst);
        case ir::OpCode::CondBr:    return generate_branch(inst);
        case ir::OpCode::Call:      return generate_call(inst);
        case ir::OpCode::Add:
        case ir::OpCode::Sub:
        case ir::OpCode::Mul:
        case ir::OpCode::Div:
        case ir::OpCode::Mod:
        case ir::OpCode::And:
        case ir::OpCode::Or:
        case ir::OpCode::BitAnd:
        case ir::OpCode::BitOr:
        case ir::OpCode::BitXor:
        case ir::OpCode::Shl:
        case ir::OpCode::Shr:
            return generate_binary_op(inst);
        case ir::OpCode::Not:
        case ir::OpCode::BitNot:
            return generate_unary_op(inst);
        case ir::OpCode::Eq:
        case ir::OpCode::Ne:
        case ir::OpCode::Lt:
        case ir::OpCode::Le:
        case ir::OpCode::Gt:
        case ir::OpCode::Ge:
            return generate_cmp(inst);
        case ir::OpCode::Select:
            return generate_select(inst);
        case ir::OpCode::Trunc:
        case ir::OpCode::ZExt:
        case ir::OpCode::SExt:
        case ir::OpCode::FPTrunc:
        case ir::OpCode::FPExt:
        case ir::OpCode::FPToSI:
        case ir::OpCode::SIToFP:
            return generate_cast(inst);
        case ir::OpCode::GetElementPtr:
            return generate_gep(inst);
        case ir::OpCode::Phi:
            return generate_phi(inst);
        default:
            if (config_.emit_comments) {
                return indent() + "// 未支持的指令: " + inst->get_op_name();
            }
            return "";
    }
}

std::string NativeCodegen::generate_binary_op(const ir::Instruction* inst) {
    auto result = make_safe_name(inst->name.empty() ? make_temp_var() : inst->name);
    auto& ops = inst->operands;
    
    if (ops.size() < 2) return "";
    
    auto lhs = generate_operand(ops[0].get());
    auto rhs = generate_operand(ops[1].get());
    auto op_str = get_c_operator(inst->opcode);
    
    auto type = CTypeMapper::to_c_type(inst->type.get());
    
    return indent() + type + " " + result + " = " + lhs + " " + op_str + " " + rhs + ";";
}

std::string NativeCodegen::generate_unary_op(const ir::Instruction* inst) {
    auto result = make_safe_name(inst->name.empty() ? make_temp_var() : inst->name);
    auto& ops = inst->operands;
    
    if (ops.empty()) return "";
    
    auto operand = generate_operand(ops[0].get());
    auto op_str = get_c_operator(inst->opcode);
    
    auto type = CTypeMapper::to_c_type(inst->type.get());
    
    return indent() + type + " " + result + " = " + op_str + operand + ";";
}

std::string NativeCodegen::generate_call(const ir::Instruction* inst) {
    std::ostringstream oss;
    auto& ops = inst->operands;
    
    if (ops.empty()) return "";
    
    // Try to get callee name from CallInst
    std::string callee;
    if (auto callInst = dynamic_cast<const ir::CallInst*>(inst)) {
        callee = callInst->callee_name;
    }
    if (callee.empty()) {
        callee = generate_operand(ops[0].get());
    }
    
    std::string result = make_safe_name(inst->name.empty() ? make_temp_var() : inst->name);
    auto type = CTypeMapper::to_c_type(inst->type.get());
    
    oss << indent();
    if (type != "void") {
        oss << type << " " << result << " = ";
    }
    oss << callee << "(";
    
    size_t start = callee.empty() ? 0 : 1;
    (void)start;
    // If callee_name was used, operands may start with args
    for (size_t i = (callee.empty() ? 1 : 0); i < ops.size(); i++) {
        if (i > (callee.empty() ? 1 : 0)) oss << ", ";
        oss << generate_operand(ops[i].get());
    }
    oss << ");";
    
    return oss.str();
}

std::string NativeCodegen::generate_load(const ir::Instruction* inst) {
    auto result = make_safe_name(inst->name.empty() ? make_temp_var() : inst->name);
    auto& ops = inst->operands;
    
    if (ops.empty()) return "";
    
    if (auto loadInst = dynamic_cast<const ir::LoadInst*>(inst)) {
        auto addr = generate_operand(loadInst->address.get());
        auto type = CTypeMapper::to_c_type(inst->type.get());
        return indent() + type + " " + result + " = *" + addr + ";";
    }
    
    auto addr = generate_operand(ops[0].get());
    auto type = CTypeMapper::to_c_type(inst->type.get());
    return indent() + type + " " + result + " = *" + addr + ";";
}

std::string NativeCodegen::generate_store(const ir::Instruction* inst) {
    if (auto storeInst = dynamic_cast<const ir::StoreInst*>(inst)) {
        auto value = generate_operand(storeInst->value.get());
        auto addr = generate_operand(storeInst->address.get());
        return indent() + "*" + addr + " = " + value + ";";
    }
    auto& ops = inst->operands;
    if (ops.size() < 2) return "";
    auto value = generate_operand(ops[0].get());
    auto addr = generate_operand(ops[1].get());
    return indent() + "*" + addr + " = " + value + ";";
}

std::string NativeCodegen::generate_alloca(const ir::Instruction* inst) {
    auto result = make_safe_name(inst->name.empty() ? make_temp_var() : inst->name);
    auto type = CTypeMapper::to_c_type(inst->type.get());
    
    if (auto ptr_type = dynamic_cast<const ir::PointerType*>(inst->type.get())) {
        auto pointee_type = CTypeMapper::to_c_type(ptr_type->pointee.get());
        return indent() + pointee_type + "* " + result + " = NULL;";
    }
    
    return indent() + type + " " + result + ";";
}

std::string NativeCodegen::generate_gep(const ir::Instruction* inst) {
    auto result = make_safe_name(inst->name.empty() ? make_temp_var() : inst->name);
    auto& ops = inst->operands;
    
    if (ops.empty()) return "";
    
    auto base = generate_operand(ops[0].get());
    std::ostringstream oss;
    oss << indent() << "void* " << result << " = (char*)" << base;
    if (ops.size() > 1) {
        oss << " + sizeof(void*) * " << generate_operand(ops[1].get());
    }
    oss << ";";
    
    return oss.str();
}

std::string NativeCodegen::generate_cmp(const ir::Instruction* inst) {
    auto result = make_safe_name(inst->name.empty() ? make_temp_var() : inst->name);
    auto& ops = inst->operands;
    
    if (ops.size() < 2) return "";
    
    auto lhs = generate_operand(ops[0].get());
    auto rhs = generate_operand(ops[1].get());
    
    std::string op_str;
    switch (inst->opcode) {
        case ir::OpCode::Eq: op_str = "=="; break;
        case ir::OpCode::Ne: op_str = "!="; break;
        case ir::OpCode::Lt: op_str = "<"; break;
        case ir::OpCode::Le: op_str = "<="; break;
        case ir::OpCode::Gt: op_str = ">"; break;
        case ir::OpCode::Ge: op_str = ">="; break;
        default: op_str = "=="; break;
    }
    
    return indent() + "bool " + result + " = " + lhs + " " + op_str + " " + rhs + ";";
}

std::string NativeCodegen::generate_phi(const ir::Instruction* inst) {
    auto result = make_safe_name(inst->name.empty() ? make_temp_var() : inst->name);
    auto type = CTypeMapper::to_c_type(inst->type.get());
    return indent() + type + " " + result + " = 0; // PHI placeholder";
}

std::string NativeCodegen::generate_select(const ir::Instruction* inst) {
    auto result = make_safe_name(inst->name.empty() ? make_temp_var() : inst->name);
    auto& ops = inst->operands;
    
    if (ops.size() < 3) return "";
    
    auto cond = generate_operand(ops[0].get());
    auto true_val = generate_operand(ops[1].get());
    auto false_val = generate_operand(ops[2].get());
    auto type = CTypeMapper::to_c_type(inst->type.get());
    
    return indent() + type + " " + result + " = " + cond + " ? " + true_val + " : " + false_val + ";";
}

std::string NativeCodegen::generate_cast(const ir::Instruction* inst) {
    auto result = make_safe_name(inst->name.empty() ? make_temp_var() : inst->name);
    auto& ops = inst->operands;
    
    if (ops.empty()) return "";
    
    auto operand = generate_operand(ops[0].get());
    auto type = CTypeMapper::to_c_type(inst->type.get());
    
    return indent() + type + " " + result + " = (" + type + ")(" + operand + ");";
}

std::string NativeCodegen::generate_return(const ir::Instruction* inst) {
    if (inst->operands.empty()) {
        return indent() + "return;";
    }
    auto value = generate_operand(inst->operands[0].get());
    return indent() + "return " + value + ";";
}

std::string NativeCodegen::generate_branch(const ir::Instruction* inst) {
    if (auto brInst = dynamic_cast<const ir::BranchInst*>(inst)) {
        return indent() + "goto " + make_safe_name(brInst->target ? brInst->target->name : "unknown") + ";";
    }
    if (auto condBr = dynamic_cast<const ir::CondBranchInst*>(inst)) {
        auto cond = generate_operand(condBr->operands[0].get());
        auto true_bb = condBr->true_block;
        auto false_bb = condBr->false_block;
        
        std::ostringstream oss;
        oss << indent() << "if (" << cond << ") goto " 
            << make_safe_name(true_bb ? true_bb->name : "unknown") << ";\n";
        if (false_bb) {
            oss << indent() << "else goto " << make_safe_name(false_bb->name) << ";";
        }
        return oss.str();
    }
    return "";
}

std::string NativeCodegen::generate_operand(const ir::Value* operand) {
    if (!operand) return "0";
    return make_safe_name(operand->name.empty() ? "_unnamed" : operand->name);
}

std::string NativeCodegen::indent() const {
    return std::string(indent_level_ * config_.indent_size, ' ');
}

std::string NativeCodegen::make_safe_name(const std::string& name) {
    if (name.empty()) return "_unnamed";
    
    std::string result;
    for (char c : name) {
        if (isalnum(c) || c == '_') {
            result += c;
        } else {
            result += '_';
        }
    }
    
    if (!result.empty() && isdigit(result[0])) {
        result = "_" + result;
    }
    
    return result;
}

std::string NativeCodegen::make_temp_var() {
    std::ostringstream oss;
    oss << "_tmp_" << temp_var_counter_++;
    return oss.str();
}

std::string NativeCodegen::get_value_name(const ir::Value* value) {
    if (!value) return "null";
    return make_safe_name(value->name.empty() ? make_temp_var() : value->name);
}

bool NativeCodegen::is_simple_type(const ir::Type* type) {
    if (!type) return true;
    auto prim = dynamic_cast<const ir::PrimitiveType*>(type);
    if (!prim) return false;
    switch (prim->kind) {
        case ir::PrimitiveTypeKind::Bool:
        case ir::PrimitiveTypeKind::Int8:
        case ir::PrimitiveTypeKind::Int16:
        case ir::PrimitiveTypeKind::Int32:
        case ir::PrimitiveTypeKind::Int64:
        case ir::PrimitiveTypeKind::UInt8:
        case ir::PrimitiveTypeKind::UInt16:
        case ir::PrimitiveTypeKind::UInt32:
        case ir::PrimitiveTypeKind::UInt64:
        case ir::PrimitiveTypeKind::Float32:
        case ir::PrimitiveTypeKind::Float64:
            return true;
        default:
            return false;
    }
}

std::string NativeCodegen::get_c_operator(const ir::OpCode op) {
    switch (op) {
        case ir::OpCode::Add:    return "+";
        case ir::OpCode::Sub:    return "-";
        case ir::OpCode::Mul:    return "*";
        case ir::OpCode::Div:    return "/";
        case ir::OpCode::Mod:    return "%";
        case ir::OpCode::And:    return "&&";
        case ir::OpCode::Or:     return "||";
        case ir::OpCode::BitAnd: return "&";
        case ir::OpCode::BitOr:  return "|";
        case ir::OpCode::BitXor: return "^";
        case ir::OpCode::Shl:    return "<<";
        case ir::OpCode::Shr:    return ">>";
        case ir::OpCode::Not:    return "!";
        case ir::OpCode::BitNot: return "~";
        default: return "/* unknown op */";
    }
}

void NativeCodegen::set_config(const NativeCodegenConfig& config) {
    config_ = config;
}

} // namespace codegen
} // namespace claw
