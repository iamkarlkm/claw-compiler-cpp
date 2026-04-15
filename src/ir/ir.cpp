// ir.cpp - Claw 中间表示 (IR) 实现
#include "ir/ir.h"
#include <sstream>
#include <stdexcept>

namespace claw {
namespace ir {

// ============================================================================
// Type Implementations
// ============================================================================

std::string PrimitiveType::to_string() const {
    switch (kind) {
        case PrimitiveTypeKind::Void: return "void";
        case PrimitiveTypeKind::Bool: return "bool";
        case PrimitiveTypeKind::Int8: return "i8";
        case PrimitiveTypeKind::Int16: return "i16";
        case PrimitiveTypeKind::Int32: return "i32";
        case PrimitiveTypeKind::Int64: return "i64";
        case PrimitiveTypeKind::UInt8: return "u8";
        case PrimitiveTypeKind::UInt16: return "u16";
        case PrimitiveTypeKind::UInt32: return "u32";
        case PrimitiveTypeKind::UInt64: return "u64";
        case PrimitiveTypeKind::Float32: return "f32";
        case PrimitiveTypeKind::Float64: return "f64";
        case PrimitiveTypeKind::String: return "string";
        case PrimitiveTypeKind::Bytes: return "bytes";
        default: return "unknown";
    }
}

bool PrimitiveType::equals(const Type& other) const {
    auto* other_prim = dynamic_cast<const PrimitiveType*>(&other);
    return other_prim && other_prim->kind == kind;
}

std::string PointerType::to_string() const {
    return pointee->to_string() + "*";
}

bool PointerType::equals(const Type& other) const {
    auto* other_ptr = dynamic_cast<const PointerType*>(&other);
    return other_ptr && pointee->equals(*other_ptr->pointee);
}

std::string ArrayType::to_string() const {
    return element_type->to_string() + "[" + std::to_string(size) + "]";
}

bool ArrayType::equals(const Type& other) const {
    auto* other_arr = dynamic_cast<const ArrayType*>(&other);
    return other_arr && element_type->equals(*other_arr->element_type) 
           && size == other_arr->size;
}

std::string FunctionType::to_string() const {
    std::ostringstream oss;
    oss << return_type->to_string() << " (";
    for (size_t i = 0; i < param_types.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << param_types[i]->to_string();
    }
    oss << ")";
    return oss.str();
}

bool FunctionType::equals(const Type& other) const {
    auto* other_fn = dynamic_cast<const FunctionType*>(&other);
    if (!other_fn || param_types.size() != other_fn->param_types.size())
        return false;
    if (!return_type->equals(*other_fn->return_type))
        return false;
    for (size_t i = 0; i < param_types.size(); ++i) {
        if (!param_types[i]->equals(*other_fn->param_types[i]))
            return false;
    }
    return true;
}

// ============================================================================
// Instruction Implementations
// ============================================================================

std::string Instruction::get_op_name() const {
    switch (opcode) {
        case OpCode::Add: return "add";
        case OpCode::Sub: return "sub";
        case OpCode::Mul: return "mul";
        case OpCode::Div: return "div";
        case OpCode::Mod: return "mod";
        case OpCode::Eq: return "eq";
        case OpCode::Ne: return "ne";
        case OpCode::Lt: return "lt";
        case OpCode::Le: return "le";
        case OpCode::Gt: return "gt";
        case OpCode::Ge: return "ge";
        case OpCode::And: return "and";
        case OpCode::Or: return "or";
        case OpCode::Not: return "not";
        case OpCode::BitAnd: return "and";
        case OpCode::BitOr: return "or";
        case OpCode::BitXor: return "xor";
        case OpCode::BitNot: return "not";
        case OpCode::Shl: return "shl";
        case OpCode::Shr: return "shr";
        case OpCode::Alloca: return "alloca";
        case OpCode::Load: return "load";
        case OpCode::Store: return "store";
        case OpCode::GetElementPtr: return "getelementptr";
        case OpCode::Trunc: return "trunc";
        case OpCode::ZExt: return "zext";
        case OpCode::SExt: return "sext";
        case OpCode::FPTrunc: return "fptrunc";
        case OpCode::FPExt: return "fpext";
        case OpCode::FPToSI: return "fptosi";
        case OpCode::SIToFP: return "sitofp";
        case OpCode::Br: return "br";
        case OpCode::CondBr: return "br";
        case OpCode::Switch: return "switch";
        case OpCode::Call: return "call";
        case OpCode::Ret: return "ret";
        case OpCode::Phi: return "phi";
        case OpCode::TensorCreate: return "tensor.create";
        case OpCode::TensorLoad: return "tensor.load";
        case OpCode::TensorStore: return "tensor.store";
        case OpCode::TensorMatmul: return "tensor.matmul";
        case OpCode::Print: return "print";
        case OpCode::Panic: return "panic";
        case OpCode::Unreachable: return "unreachable";
        default: return "unknown";
    }
}

std::string Instruction::to_string() const {
    std::ostringstream oss;
    if (!name.empty()) {
        oss << "%" << name << " = ";
    }
    oss << get_op_name();
    
    if (!operands.empty()) {
        oss << " ";
        for (size_t i = 0; i < operands.size(); ++i) {
            if (i > 0) oss << ", ";
            if (operands[i]) {
                if (operands[i]->is_constant) {
                    // 输出常量值
                    if (operands[i]->constant_value.index() == 0) {
                        oss << std::get<int64_t>(operands[i]->constant_value);
                    } else if (operands[i]->constant_value.index() == 1) {
                        oss << std::get<double>(operands[i]->constant_value);
                    } else if (operands[i]->constant_value.index() == 3) {
                        oss << (std::get<bool>(operands[i]->constant_value) ? "true" : "false");
                    } else if (operands[i]->constant_value.index() == 2) {
                        oss << "\"" << std::get<std::string>(operands[i]->constant_value) << "\"";
                    }
                } else {
                    oss << "%" << operands[i]->name;
                }
            } else {
                oss << "null";
            }
        }
    }
    
    if (type && opcode != OpCode::Store && opcode != OpCode::Br && 
        opcode != OpCode::CondBr && opcode != OpCode::Ret) {
        oss << " : " << type->to_string();
    }
    
    return oss.str();
}

// ============================================================================
// BasicBlock Implementations
// ============================================================================

void BasicBlock::add_instruction(std::shared_ptr<Instruction> inst) {
    instructions.push_back(inst);
}

void BasicBlock::set_terminator(std::shared_ptr<Instruction> term) {
    terminator = std::move(term);
}

std::vector<std::string> BasicBlock::get_live_variables() const {
    std::vector<std::string> live;
    // 简单实现：从所有指令中收集使用的变量
    for (auto& inst : instructions) {
        for (auto& op : inst->operands) {
            if (op && !op->is_constant && !op->name.empty()) {
                live.push_back(op->name);
            }
        }
    }
    return live;
}

// ============================================================================
// Function Implementations
// ============================================================================

std::shared_ptr<BasicBlock> Function::create_block(std::string name) {
    auto bb = std::make_shared<BasicBlock>(std::move(name));
    bb->parent = shared_from_this();
    blocks.push_back(bb);
    return bb;
}

std::shared_ptr<BasicBlock> Function::get_entry_block() const {
    return blocks.empty() ? nullptr : blocks.front();
}

void Function::add_block(std::shared_ptr<BasicBlock> bb) {
    bb->parent = shared_from_this();
    blocks.push_back(std::move(bb));
}

// ============================================================================
// Module Implementations
// ============================================================================

void Module::add_function(std::shared_ptr<Function> func) {
    functions.push_back(func);
    function_map[func->name] = func;
}

std::shared_ptr<Function> Module::get_function(const std::string& name) const {
    auto it = function_map.find(name);
    return (it != function_map.end()) ? it->second : nullptr;
}

void Module::add_global(std::string name, std::shared_ptr<Value> val) {
    globals[std::move(name)] = std::move(val);
}

std::string Module::add_string_constant(const std::string& str) {
    std::string name = ".str." + std::to_string(string_constants.size());
    string_constants.emplace_back(name, str);
    return name;
}

// ============================================================================
// IRBuilder Implementations
// ============================================================================

IRBuilder::IRBuilder() 
    : module(std::make_shared<Module>("main")) {}

std::shared_ptr<Type> IRBuilder::get_primitive_type(PrimitiveTypeKind kind) {
    return std::make_shared<PrimitiveType>(kind);
}

std::shared_ptr<Type> IRBuilder::get_pointer_type(std::shared_ptr<Type> pointee) {
    return std::make_shared<PointerType>(std::move(pointee));
}

std::shared_ptr<Type> IRBuilder::get_array_type(std::shared_ptr<Type> elem, int64_t size) {
    return std::make_shared<ArrayType>(std::move(elem), size);
}

std::shared_ptr<Type> IRBuilder::get_function_type(
    std::shared_ptr<Type> ret, std::vector<std::shared_ptr<Type>> params) {
    return std::make_shared<FunctionType>(std::move(ret), std::move(params));
}

std::string IRBuilder::create_name(const std::string& prefix) {
    return prefix + "." + std::to_string(name_counter++);
}

std::string IRBuilder::create_block_name(const std::string& prefix) {
    return prefix + "." + std::to_string(name_counter++);
}

std::shared_ptr<Function> IRBuilder::create_function(
    std::string name, std::shared_ptr<Type> ret_type) {
    auto func = std::make_shared<Function>(std::move(name), std::move(ret_type));
    current_function = func;
    module->add_function(func);
    return func;
}

std::shared_ptr<BasicBlock> IRBuilder::create_block(std::string name) {
    if (!current_function) {
        throw std::runtime_error("Cannot create block without a function");
    }
    return current_function->create_block(std::move(name));
}

void IRBuilder::set_insert_point(std::shared_ptr<BasicBlock> bb) {
    current_block = std::move(bb);
}

std::shared_ptr<Value> IRBuilder::create_alloca(
    std::shared_ptr<Type> elem_type, int64_t count, std::string name) {
    if (name.empty()) {
        name = create_name("alloca");
    }
    
    auto ptr_type = get_pointer_type(elem_type);
    auto alloca = std::make_shared<AllocaInst>(elem_type, count, ptr_type);
    alloca->name = name;
    alloca->loc = {};  // TODO: 设置源位置
    
    auto value = std::make_shared<Value>(name, ptr_type);
    value->defining_inst = alloca;
    
    if (current_block) {
        current_block->add_instruction(alloca);
    }
    
    return value;
}

std::shared_ptr<Value> IRBuilder::create_load(
    std::shared_ptr<Value> addr, std::string name) {
    if (name.empty()) {
        name = create_name("load");
    }
    
    // 从指针类型获取加载类型
    auto* ptr_ty = dynamic_cast<PointerType*>(addr->type.get());
    if (!ptr_ty) {
        throw std::runtime_error("Cannot load from non-pointer type");
    }
    
    auto load = std::make_shared<LoadInst>(addr, ptr_ty->pointee);
    load->name = name;
    load->loc = {};
    
    auto value = std::make_shared<Value>(name, ptr_ty->pointee);
    value->defining_inst = load;
    
    if (current_block) {
        current_block->add_instruction(load);
    }
    
    return value;
}

void IRBuilder::create_store(std::shared_ptr<Value> value, std::shared_ptr<Value> addr) {
    auto store = std::make_shared<StoreInst>(addr, value);
    store->loc = {};
    
    if (current_block) {
        current_block->add_instruction(store);
    }
}

std::shared_ptr<Value> IRBuilder::create_binary_op(
    OpCode op, std::shared_ptr<Value> lhs, std::shared_ptr<Value> rhs, std::string name) {
    if (name.empty()) {
        name = create_name("binop");
    }
    
    auto inst = std::make_shared<BinaryInst>(op, lhs, rhs, lhs->type);
    inst->name = name;
    inst->loc = {};
    
    auto value = std::make_shared<Value>(name, lhs->type);
    value->defining_inst = inst;
    
    if (current_block) {
        current_block->add_instruction(inst);
    }
    
    return value;
}

std::shared_ptr<Value> IRBuilder::create_unary_op(
    OpCode op, std::shared_ptr<Value> operand, std::string name) {
    if (name.empty()) {
        name = create_name("unop");
    }
    
    auto inst = std::make_shared<UnaryInst>(op, operand, operand->type);
    inst->name = name;
    inst->loc = {};
    
    auto value = std::make_shared<Value>(name, operand->type);
    value->defining_inst = inst;
    
    if (current_block) {
        current_block->add_instruction(inst);
    }
    
    return value;
}

std::shared_ptr<Value> IRBuilder::create_call(
    std::string callee, std::vector<std::shared_ptr<Value>> args, std::string name) {
    if (name.empty()) {
        name = create_name("call");
    }
    
    // TODO: 从函数签名获取返回类型
    auto ret_type = get_primitive_type(PrimitiveTypeKind::Int32);
    auto call = std::make_shared<CallInst>(callee, std::move(args), ret_type);
    call->name = name;
    call->loc = {};
    
    auto value = std::make_shared<Value>(name, ret_type);
    value->defining_inst = call;
    
    if (current_block) {
        current_block->add_instruction(call);
    }
    
    return value;
}

std::shared_ptr<Value> IRBuilder::create_cmp(
    OpCode cmp_op, std::shared_ptr<Value> lhs, std::shared_ptr<Value> rhs, std::string name) {
    if (name.empty()) {
        name = create_name("cmp");
    }
    
    auto bool_type = get_primitive_type(PrimitiveTypeKind::Bool);
    auto inst = std::make_shared<BinaryInst>(cmp_op, lhs, rhs, bool_type);
    inst->name = name;
    inst->loc = {};
    
    auto value = std::make_shared<Value>(name, bool_type);
    value->defining_inst = inst;
    
    if (current_block) {
        current_block->add_instruction(inst);
    }
    
    return value;
}

void IRBuilder::create_ret(std::shared_ptr<Value> value) {
    auto ret = std::make_shared<ReturnInst>(value);
    ret->loc = {};
    
    if (current_block) {
        current_block->set_terminator(ret);
    }
}

void IRBuilder::create_ret_void() {
    create_ret(nullptr);
}

void IRBuilder::create_br(std::shared_ptr<BasicBlock> target) {
    auto br = std::make_shared<BranchInst>(target);
    br->loc = {};
    
    if (current_block) {
        current_block->set_terminator(br);
        target->predecessors.push_back(current_block);
    }
}

void IRBuilder::create_cond_br(
    std::shared_ptr<Value> cond,
    std::shared_ptr<BasicBlock> true_block,
    std::shared_ptr<BasicBlock> false_block) {
    auto br = std::make_shared<CondBranchInst>(cond, true_block, false_block);
    br->loc = {};
    
    if (current_block) {
        current_block->set_terminator(br);
        true_block->predecessors.push_back(current_block);
        false_block->predecessors.push_back(current_block);
    }
}

std::shared_ptr<Value> IRBuilder::create_phi(std::string name) {
    if (name.empty()) {
        name = create_name("phi");
    }
    
    auto phi_type = get_primitive_type(PrimitiveTypeKind::Int32);  // TODO: 推导类型
    auto phi = std::make_shared<PhiInst>(phi_type);
    phi->name = name;
    phi->loc = {};
    
    auto value = std::make_shared<Value>(name, phi_type);
    value->defining_inst = phi;
    
    if (current_block) {
        current_block->add_instruction(phi);
    }
    
    return value;
}

std::shared_ptr<Value> IRBuilder::create_constant(int64_t val) {
    auto type = get_primitive_type(PrimitiveTypeKind::Int64);
    auto value = std::make_shared<Value>("", type);
    value->is_constant = true;
    value->constant_value = val;
    return value;
}

std::shared_ptr<Value> IRBuilder::create_constant(double val) {
    auto type = get_primitive_type(PrimitiveTypeKind::Float64);
    auto value = std::make_shared<Value>("", type);
    value->is_constant = true;
    value->constant_value = val;
    return value;
}

std::shared_ptr<Value> IRBuilder::create_constant(bool val) {
    auto type = get_primitive_type(PrimitiveTypeKind::Bool);
    auto value = std::make_shared<Value>("", type);
    value->is_constant = true;
    value->constant_value = val;
    return value;
}

std::shared_ptr<Value> IRBuilder::create_constant(const std::string& val) {
    auto type = get_primitive_type(PrimitiveTypeKind::String);
    auto value = std::make_shared<Value>("", type);
    value->is_constant = true;
    value->constant_value = val;
    return value;
}

std::shared_ptr<Value> IRBuilder::create_cast(
    OpCode cast_op, std::shared_ptr<Value> value,
    std::shared_ptr<Type> target_type, std::string name) {
    if (name.empty()) {
        name = create_name("cast");
    }
    
    std::vector<std::shared_ptr<Value>> ops = {value};
    auto inst = std::make_shared<Instruction>(cast_op, target_type);
    inst->operands = std::move(ops);
    inst->name = name;
    inst->loc = {};
    
    auto result = std::make_shared<Value>(name, target_type);
    result->defining_inst = inst;
    
    if (current_block) {
        current_block->add_instruction(inst);
    }
    
    return result;
}

std::shared_ptr<Value> IRBuilder::create_tensor_create(
    std::vector<int64_t> shape, std::shared_ptr<Type> elem_type) {
    // TODO: 实现张量创建
    return nullptr;
}

std::shared_ptr<Value> IRBuilder::create_tensor_load(
    std::shared_ptr<Value> tensor, std::vector<std::shared_ptr<Value>> indices) {
    // TODO: 实现张量加载
    return nullptr;
}

void IRBuilder::create_tensor_store(
    std::shared_ptr<Value> tensor, std::vector<std::shared_ptr<Value>> indices,
    std::shared_ptr<Value> value) {
    // TODO: 实现张量存储
}

void IRBuilder::create_panic(std::string message) {
    auto panic_inst = std::make_shared<Instruction>(OpCode::Panic, nullptr);
    panic_inst->loc = {};
    
    if (current_block) {
        current_block->add_instruction(panic_inst);
    }
}

void IRBuilder::create_print(std::shared_ptr<Value> value) {
    auto print_inst = std::make_shared<Instruction>(OpCode::Print, nullptr);
    print_inst->operands.push_back(value);
    print_inst->loc = {};
    
    if (current_block) {
        current_block->add_instruction(print_inst);
    }
}

std::shared_ptr<Type> IRBuilder::map_claw_type_to_ir(const std::string& claw_type) {
    // 简单映射实现
    if (claw_type == "i32" || claw_type == "int") {
        return get_primitive_type(PrimitiveTypeKind::Int32);
    } else if (claw_type == "i64" || claw_type == "long") {
        return get_primitive_type(PrimitiveTypeKind::Int64);
    } else if (claw_type == "f32" || claw_type == "float") {
        return get_primitive_type(PrimitiveTypeKind::Float32);
    } else if (claw_type == "f64" || claw_type == "double") {
        return get_primitive_type(PrimitiveTypeKind::Float64);
    } else if (claw_type == "bool") {
        return get_primitive_type(PrimitiveTypeKind::Bool);
    } else if (claw_type == "string") {
        return get_primitive_type(PrimitiveTypeKind::String);
    } else if (claw_type == "void") {
        return get_primitive_type(PrimitiveTypeKind::Void);
    }
    
    // 默认返回 i32
    return get_primitive_type(PrimitiveTypeKind::Int32);
}

} // namespace ir
} // namespace claw
