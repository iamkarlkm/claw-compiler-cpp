// native_codegen.cpp - 本地代码生成器实现

#include "native_codegen.h"
#include <cmath>
#include <sstream>

namespace claw {
namespace codegen {

// ============================================================================
// CTypeMapper 实现
// ============================================================================

std::string CTypeMapper::to_c_type(const ir::Type* type) {
    if (!type) return "void";
    
    switch (type->get_kind()) {
        case ir::TypeKind::VOID:     return "void";
        case ir::TypeKind::INT1:     return "bool";
        case ir::TypeKind::INT8:     return "int8_t";
        case ir::TypeKind::INT16:    return "int16_t";
        case ir::TypeKind::INT32:    return "int32_t";
        case ir::TypeKind::INT64:    return "int64_t";
        case ir::TypeKind::UINT8:    return "uint8_t";
        case ir::TypeKind::UINT16:   return "uint16_t";
        case ir::TypeKind::UINT32:   return "uint32_t";
        case ir::TypeKind::UINT64:   return "uint64_t";
        case ir::TypeKind::FLOAT32:  return "float";
        case ir::TypeKind::FLOAT64:  return "double";
        case ir::TypeKind::STRING:   return "char*";
        case ir::TypeKind::ARRAY: {
            auto arr = static_cast<const ir::ArrayType*>(type);
            return to_c_type(arr->get_element_type()) + "*";
        }
        case ir::TypeKind::PTR: {
            auto ptr = static_cast<const ir::PointerType*>(type);
            return to_c_type(ptr->get_element_type()) + "*";
        }
        case ir::TypeKind::TUPLE:    return "claw_tuple_t";
        case ir::TypeKind::FUNCTION: return "claw_function_t";
        case ir::TypeKind::TENSOR:   return "claw_tensor_t";
        case ir::TypeKind::USTOM:    return type->get_name();
        default:                     return "claw_value_t";
    }
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
    auto kind = type->get_kind();
    return kind == ir::TypeKind::ARRAY || 
           kind == ir::TypeKind::STRING ||
           kind == ir::TypeKind::TUPLE ||
           kind == ir::TypeKind::TENSOR ||
           kind == ir::TypeKind::FUNCTION;
}

std::string CTypeMapper::get_type_name(const ir::Type* type) {
    if (!type) return "void";
    std::ostringstream oss;
    switch (type->get_kind()) {
        case ir::TypeKind::INT1:     oss << "bool"; break;
        case ir::TypeKind::INT8:     oss << "int8_t"; break;
        case ir::TypeKind::INT16:    oss << "int16_t"; break;
        case ir::TypeKind::INT32:    oss << "int32_t"; break;
        case ir::TypeKind::INT64:    oss << "int64_t"; break;
        case ir::TypeKind::UINT8:    oss << "uint8_t"; break;
        case ir::TypeKind::UINT16:   oss << "uint16_t"; break;
        case ir::TypeKind::UINT32:   oss << "uint32_t"; break;
        case ir::TypeKind::UINT64:   oss << "uint64_t"; break;
        case ir::TypeKind::FLOAT32:  oss << "float"; break;
        case ir::TypeKind::FLOAT64:  oss << "double"; break;
        case ir::TypeKind::STRING:   oss << "char_ptr"; break;
        case ir::TypeKind::PTR: {
            auto ptr = static_cast<const ir::PointerType*>(type);
            oss << get_type_name(ptr->get_element_type()) << "_ptr";
            break;
        }
        case ir::TypeKind::ARRAY: {
            auto arr = static_cast<const ir::ArrayType*>(type);
            oss << get_type_name(arr->get_element_type()) << "_array";
            break;
        }
        case ir::TypeKind::TUPLE:    oss << "tuple"; break;
        case ir::TypeKind::TENSOR:   oss << "tensor"; break;
        default:                     oss << "value"; break;
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
    std::string guard = config_.output_prefix + module.get_name() + "_H";
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
    
    for (const auto& func : module.get_functions()) {
        if (!func->is_extern()) {
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
    oss << "// 模块: " << module.get_name() << "\n";
    oss << "// 生成时间: " << __DATE__ << " " << __TIME__ << "\n";
    oss << "// ============================================================\n\n";
    
    oss << "#include \"" << config_.output_prefix << module.get_name() << ".h\"\n\n";
    
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
    
    for (const auto& func : module.get_functions()) {
        if (!func->is_extern() && !func->is_declaration()) {
            std::string func_code = generate_function(func.get());
            if (!func_code.empty()) {
                oss << func_code << "\n\n";
                stats_.num_functions++;
            }
        }
    }
    
    stats_.num_globals = module.get_globals().size();
    stats_.total_lines = 0; // 简化计算
    
    return oss.str();
}

std::string NativeCodegen::generate_type_definitions() {
    std::ostringstream oss;
    
    // 基本运行时类型
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
    
    oss << "// 元组类型\n";
    oss << "typedef struct claw_tuple {\n";
    oss << "    int64_t size;\n";
    oss << "    claw_value_t fields[];\n";
    oss << "} claw_tuple_t;\n\n";
    
    oss << "// 张量类型\n";
    oss << "typedef struct claw_tensor {\n";
    oss << "    void* data;\n";
    oss << "    int32_t* shape;\n";
    oss << "    int32_t ndim;\n";
    oss << "    int32_t dtype;  // 0:float32, 1:float64, 2:int32, 3:int64\n";
    oss << "} claw_tensor_t;\n\n";
    
    stats_.num_types = 5;
    return oss.str();
}

std::string NativeCodegen::generate_function_declarations() {
    std::ostringstream oss;
    for (const auto& func : current_module_->get_functions()) {
        if (func->is_extern()) {
            oss << generate_function_signature(func.get()) << ";\n";
        }
    }
    return oss.str();
}

std::string NativeCodegen::generate_global_declarations() {
    std::ostringstream oss;
    for (const auto& global : current_module_->get_globals()) {
        auto type = CTypeMapper::to_c_type(global->get_type());
        oss << "static " << type << " " << make_safe_name(global->get_name());
        if (global->has_initializer()) {
            auto init = generate_constant(global->get_initializer().get());
            oss << " = " << init;
        }
        oss << ";\n";
    }
    return oss.str();
}

std::string NativeCodegen::generate_struct_definitions() {
    return ""; // 已在 generate_type_definitions 中处理
}

std::string NativeCodegen::generate_function_signature(const ir::Function* func) {
    std::ostringstream oss;
    auto ret_type = CTypeMapper::to_c_type(func->get_return_type());
    oss << ret_type << " " << make_safe_name(func->get_name()) << "(";
    
    auto params = func->get_params();
    if (params.empty()) {
        oss << "void";
    } else {
        for (size_t i = 0; i < params.size(); i++) {
            if (i > 0) oss << ", ";
            auto param_type = CTypeMapper::to_c_type(params[i]->get_type());
            oss << param_type << " " << make_safe_name(params[i]->get_name());
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
        oss << "// 函数: " << func->get_name() << "\n";
    }
    
    oss << generate_function_signature(func) << " {\n";
    indent_level_++;
    
    oss << generate_function_body(func);
    
    indent_level_--;
    oss << "}\n";
    
    emitted_functions_.insert(func->get_name());
    return oss.str();
}

std::string NativeCodegen::generate_function_body(const ir::Function* func) {
    std::ostringstream oss;
    auto& blocks = func->get_blocks();
    
    if (blocks.empty()) {
        oss << indent() << "return CLAW_VALUE_NIL;\n";
        return oss.str();
    }
    
    // 为每个基本块生成标签和代码
    for (const auto& block : blocks) {
        if (config_.emit_comments) {
            oss << "\n" << indent() << "// 基本块: " << block->get_name() << "\n";
        }
        oss << make_safe_name(block->get_name()) << ":\n";
        indent_level_++;
        oss << generate_basic_block(block.get());
        indent_level_--;
    }
    
    return oss.str();
}

std::string NativeCodegen::generate_basic_block(const ir::BasicBlock* block) {
    std::ostringstream oss;
    
    for (const auto& inst : block->get_instructions()) {
        auto inst_code = generate_instruction(inst.get());
        if (!inst_code.empty()) {
            oss << inst_code << "\n";
        }
    }
    
    return oss.str();
}

std::string NativeCodegen::generate_instruction(const ir::Instruction* inst) {
    if (!inst) return "";
    
    auto op = inst->get_op_code();
    
    switch (op) {
        case ir::OpCode::ALLOCA:
            return generate_alloca(inst);
        case ir::OpCode::LOAD:
            return generate_load(inst);
        case ir::OpCode::STORE:
            return generate_store(inst);
        case ir::OpCode::RET:
        case ir::OpCode::RET_VOID:
            return generate_return(inst);
        case ir::OpCode::BR:
        case ir::OpCode::BR_COND:
            return generate_branch(inst);
        case ir::OpCode::SWITCH:
            return generate_switch(inst);
        case ir::OpCode::CALL:
            return generate_call(inst);
        case ir::OpCode::ADD:
        case ir::OpCode::SUB:
        case ir::OpCode::MUL:
        case ir::OpCode::DIV:
        case ir::OpCode::REM:
        case ir::OpCode::AND:
        case ir::OpCode::OR:
        case ir::OpCode::XOR:
        case ir::OpCode::SHL:
        case ir::OpCode::SHR:
            return generate_binary_op(inst);
        case ir::OpCode::NEG:
        case ir::OpCode::NOT:
            return generate_unary_op(inst);
        case ir::OpCode::ICMP:
        case ir::OpCode::FCMP:
            return generate_cmp(inst);
        case ir::OpCode::SELECT:
            return generate_select(inst);
        case ir::OpCode::ZEXT:
        case ir::OpCode::SEXT:
        case ir::OpCode::TRUNC:
        case ir::OpCode::FPTRUNC:
        case ir::OpCode::FPEXT:
        case ir::OpCode::BITCAST:
        case ir::OpCode::SITOFPI:
        case ir::OpCode::PTRTOINT:
        case ir::OpCode::INTTOPTR:
            return generate_cast(inst);
        case ir::OpCode::GEP:
            return generate_gep(inst);
        case ir::OpCode::PHI:
            return generate_phi(inst);
        default:
            if (config_.emit_comments) {
                return indent() + "// 未支持的指令: " + ir::op_code_to_string(op);
            }
            return "";
    }
}

std::string NativeCodegen::generate_binary_op(const ir::Instruction* inst) {
    auto result = get_value_name(inst);
    auto& ops = inst->get_operands();
    
    if (ops.size() < 2) return "";
    
    auto lhs = generate_operand(ops[0].get());
    auto rhs = generate_operand(ops[1].get());
    auto op_str = get_c_operator(inst->get_op_code());
    
    auto type = CTypeMapper::to_c_type(inst->get_type());
    
    return indent() + type + " " + result + " = " + lhs + " " + op_str + " " + rhs + ";";
}

std::string NativeCodegen::generate_unary_op(const ir::Instruction* inst) {
    auto result = get_value_name(inst);
    auto& ops = inst->get_operands();
    
    if (ops.empty()) return "";
    
    auto operand = generate_operand(ops[0].get());
    auto op_str = get_c_operator(inst->get_op_code());
    
    auto type = CTypeMapper::to_c_type(inst->get_type());
    
    return indent() + type + " " + result + " = " + op_str + operand + ";";
}

std::string NativeCodegen::generate_call(const ir::Instruction* inst) {
    std::ostringstream oss;
    auto& ops = inst->get_operands();
    
    if (ops.empty()) return "";
    
    auto callee = generate_operand(ops[0].get());
    std::string result = get_value_name(inst);
    auto type = CTypeMapper::to_c_type(inst->get_type());
    
    oss << indent();
    if (type != "void") {
        oss << type << " " << result << " = ";
    }
    oss << callee << "(";
    
    for (size_t i = 1; i < ops.size(); i++) {
        if (i > 1) oss << ", ";
        oss << generate_operand(ops[i].get());
    }
    oss << ");";
    
    return oss.str();
}

std::string NativeCodegen::generate_load(const ir::Instruction* inst) {
    auto result = get_value_name(inst);
    auto& ops = inst->get_operands();
    
    if (ops.empty()) return "";
    
    auto addr = generate_operand(ops[0].get());
    auto type = CTypeMapper::to_c_type(inst->get_type());
    
    return indent() + type + " " + result + " = *" + addr + ";";
}

std::string NativeCodegen::generate_store(const ir::Instruction* inst) {
    auto& ops = inst->get_operands();
    
    if (ops.size() < 2) return "";
    
    auto value = generate_operand(ops[0].get());
    auto addr = generate_operand(ops[1].get());
    
    return indent() + "*" + addr + " = " + value + ";";
}

std::string NativeCodegen::generate_alloca(const ir::Instruction* inst) {
    auto result = get_value_name(inst);
    auto type = CTypeMapper::to_c_type(inst->get_type());
    
    // 特殊处理数组/alloca 类型
    auto alloc_type = inst->get_type();
    if (alloc_type && alloc_type->get_kind() == ir::TypeKind::PTR) {
        auto ptr_type = static_cast<const ir::PointerType*>(alloc_type);
        auto pointee_type = CTypeMapper::to_c_type(ptr_type->get_element_type());
        return indent() + pointee_type + "* " + result + " = NULL;";
    }
    
    return indent() + type + " " + result + ";";
}

std::string NativeCodegen::generate_gep(const ir::Instruction* inst) {
    auto result = get_value_name(inst);
    auto& ops = inst->get_operands();
    
    if (ops.empty()) return "";
    
    auto base = generate_operand(ops[0].get());
    std::ostringstream oss;
    oss << indent() << "int64_t " << result << "_idx[] = {";
    
    for (size_t i = 1; i < ops.size(); i++) {
        if (i > 1) oss << ", ";
        oss << generate_operand(ops[i].get());
    }
    oss << "};\n";
    
    // 简化: 直接返回 base + offset
    oss << indent() << "void* " << result << " = (char*)" << base << " + sizeof(void*) * " 
        << (ops.size() > 1 ? generate_operand(ops[1].get()) : "0") << ";";
    
    return oss.str();
}

std::string NativeCodegen::generate_cmp(const ir::Instruction* inst) {
    auto result = get_value_name(inst);
    auto& ops = inst->get_operands();
    
    if (ops.size() < 2) return "";
    
    auto lhs = generate_operand(ops[0].get());
    auto rhs = generate_operand(ops[1].get());
    
    // 获取比较谓词
    auto pred = inst->get_cmp_predicate();
    std::string op_str;
    
    switch (pred) {
        case ir::CmpPredicate::EQ:  op_str = "=="; break;
        case ir::CmpPredicate::NE:  op_str = "!="; break;
        case ir::CmpPredicate::SLT: 
        case ir::CmpPredicate::OLT: op_str = "<"; break;
        case ir::CmpPredicate::SLE:
        case ir::CmpPredicate::OLE: op_str = "<="; break;
        case ir::CmpPredicate::SGT:
        case ir::CmpPredicate::OGT: op_str = ">"; break;
        case ir::CmpPredicate::SGE:
        case ir::CmpPredicate::OGE: op_str = ">="; break;
        default: op_str = "=="; break;
    }
    
    return indent() + "bool " + result + " = " + lhs + " " + op_str + " " + rhs + ";";
}

std::string NativeCodegen::generate_phi(const ir::Instruction* inst) {
    auto result = get_value_name(inst);
    auto type = CTypeMapper::to_c_type(inst->get_type());
    
    // PHI 节点需要特殊处理: 简化为临时变量
    // 实际代码中需要通过控制流分析确定正确值
    return indent() + type + " " + result + " = 0; // PHI placeholder";
}

std::string NativeCodegen::generate_select(const ir::Instruction* inst) {
    auto result = get_value_name(inst);
    auto& ops = inst->get_operands();
    
    if (ops.size() < 3) return "";
    
    auto cond = generate_operand(ops[0].get());
    auto true_val = generate_operand(ops[1].get());
    auto false_val = generate_operand(ops[2].get());
    auto type = CTypeMapper::to_c_type(inst->get_type());
    
    return indent() + type + " " + result + " = " + cond + " ? " + true_val + " : " + false_val + ";";
}

std::string NativeCodegen::generate_cast(const ir::Instruction* inst) {
    auto result = get_value_name(inst);
    auto& ops = inst->get_operands();
    
    if (ops.empty()) return "";
    
    auto operand = generate_operand(ops[0].get());
    auto type = CTypeMapper::to_c_type(inst->get_type());
    
    auto op = inst->get_op_code();
    std::string cast_expr;
    
    switch (op) {
        case ir::OpCode::ZEXT:
        case ir::OpCode::SEXT:
        case ir::OpCode::TRUNC:
            cast_expr = "(" + type + ")(" + operand + ")";
            break;
        case ir::OpCode::SITOFPI:
            cast_expr = "(double)(" + operand + ")";
            break;
        case ir::OpCode::FPTRUNC:
        case ir::OpCode::FPEXT:
            cast_expr = "(" + type + ")(" + operand + ")";
            break;
        default:
            cast_expr = "*(" + type + "*)&(" + operand + ")";
            break;
    }
    
    return indent() + type + " " + result + " = " + cast_expr + ";";
}

std::string NativeCodegen::generate_return(const ir::Instruction* inst) {
    auto& ops = inst->get_operands();
    
    if (ops.empty() || inst->get_op_code() == ir::OpCode::RET_VOID) {
        return indent() + "return;";
    }
    
    auto value = generate_operand(ops[0].get());
    return indent() + "return " + value + ";";
}

std::string NativeCodegen::generate_branch(const ir::Instruction* inst) {
    auto& ops = inst->get_operands();
    
    if (inst->get_op_code() == ir::OpCode::BR) {
        // 无条件跳转
        auto target = generate_operand(ops[0].get());
        return indent() + "goto " + make_safe_name(target) + ";";
    } else if (ops.size() >= 2) {
        // 条件跳转
        auto cond = generate_operand(ops[0].get());
        auto true_target = generate_operand(ops[1].get());
        auto false_target = ops.size() > 2 ? generate_operand(ops[2].get()) : "";
        
        std::ostringstream oss;
        oss << indent() << "if (" << cond << ") goto " << make_safe_name(true_target) << ";\n";
        if (!false_target.empty()) {
            oss << indent() << "else goto " << make_safe_name(false_target) << ";";
        }
        return oss.str();
    }
    
    return "";
}

std::string NativeCodegen::generate_switch(const ir::Instruction* inst) {
    // Switch 简化为 if-else 链
    return indent() + "// switch instruction (placeholder)";
}

std::string NativeCodegen::generate_constant(const ir::Constant* constant) {
    if (!constant) return "0";
    
    switch (constant->get_type()) {
        case ir::ConstantType::NULLPTR:
            return "NULL";
        case ir::ConstantType::BOOL:
            return constant->get_bool_value() ? "true" : "false";
        case ir::ConstantType::INT:
            return std::to_string(constant->get_int_value());
        case ir::ConstantType::FLOAT:
            return std::to_string(constant->get_float_value());
        case ir::ConstantType::STRING: {
            std::string s = constant->get_string_value();
            std::ostringstream oss;
            oss << "\"" << s << "\"";
            return oss.str();
        }
        default:
            return "0";
    }
}

std::string NativeCodegen::generate_constant_array(const ir::ConstantArray* arr) {
    if (!arr) return "{}";
    
    std::ostringstream oss;
    oss << "{";
    auto& elements = arr->get_elements();
    for (size_t i = 0; i < elements.size(); i++) {
        if (i > 0) oss << ", ";
        oss << generate_constant(elements[i].get());
    }
    oss << "}";
    return oss.str();
}

std::string NativeCodegen::generate_constant_tuple(const ir::ConstantTuple* tup) {
    if (!tup) return "{}";
    
    std::ostringstream oss;
    oss << "{";
    auto& elements = tup->get_elements();
    for (size_t i = 0; i < elements.size(); i++) {
        if (i > 0) oss << ", ";
        oss << generate_constant(elements[i].get());
    }
    oss << "}";
    return oss.str();
}

std::string NativeCodegen::generate_operand(const ir::Value* operand) {
    if (!operand) return "0";
    
    if (auto constant = dynamic_cast<const ir::Constant*>(operand)) {
        return generate_constant(constant);
    }
    
    if (auto inst = dynamic_cast<const ir::Instruction*>(operand)) {
        return get_value_name(inst);
    }
    
    if (auto arg = dynamic_cast<const ir::Argument*>(operand)) {
        return make_safe_name(arg->get_name());
    }
    
    return "/* unknown operand */";
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
    
    // 确保不以数字开头
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
    
    if (auto inst = dynamic_cast<const ir::Instruction*>(value)) {
        if (inst->get_name().empty()) {
            return make_temp_var();
        }
        return make_safe_name(inst->get_name());
    }
    
    if (auto arg = dynamic_cast<const ir::Argument*>(value)) {
        return make_safe_name(arg->get_name());
    }
    
    return "/* unknown */";
}

bool NativeCodegen::is_simple_type(const ir::Type* type) {
    if (!type) return true;
    
    auto kind = type->get_kind();
    return kind == ir::TypeKind::INT1 ||
           kind == ir::TypeKind::INT8 ||
           kind == ir::TypeKind::INT16 ||
           kind == ir::TypeKind::INT32 ||
           kind == ir::TypeKind::INT64 ||
           kind == ir::TypeKind::UINT8 ||
           kind == ir::TypeKind::UINT16 ||
           kind == ir::TypeKind::UINT32 ||
           kind == ir::TypeKind::UINT64 ||
           kind == ir::TypeKind::FLOAT32 ||
           kind == ir::TypeKind::FLOAT64;
}

std::string NativeCodegen::get_c_operator(const ir::OpCode op) {
    switch (op) {
        case ir::OpCode::ADD: return "+";
        case ir::OpCode::SUB: return "-";
        case ir::OpCode::MUL: return "*";
        case ir::OpCode::DIV: return "/";
        case ir::OpCode::REM: return "%";
        case ir::OpCode::AND: return "&&";
        case ir::OpCode::OR:  return "||";
        case ir::OpCode::XOR: return "^";
        case ir::OpCode::SHL: return "<<";
        case ir::OpCode::SHR: return ">>";
        case ir::OpCode::NEG: return "-";
        case ir::OpCode::NOT: return "!";
        default: return "/* unknown op */";
    }
}

void NativeCodegen::set_config(const NativeCodegenConfig& config) {
    config_ = config;
}

} // namespace codegen
} // namespace claw
