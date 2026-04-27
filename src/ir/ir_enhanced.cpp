// ir_enhanced.cpp - IR 增强实现
// 完善 TODO 功能：张量创建/加载/存储、GEP 实现、类型转换等

#include "ir/ir.h"
#include <sstream>
#include <stdexcept>

namespace claw {
namespace ir {

// ============================================================================
// IRBuilder 增强实现
// ============================================================================

// GEP (GetElementPtr) 实现
std::shared_ptr<Value> IRBuilder::create_gep(
    std::shared_ptr<Value> base,
    std::vector<std::shared_ptr<Value>> indices,
    std::shared_ptr<Type> elem_type,
    std::string name) {
    
    if (name.empty()) {
        name = create_name("gep");
    }
    
    if (!base) {
        throw std::runtime_error("GEP base is null");
    }
    
    if (!elem_type) {
        throw std::runtime_error("GEP element type is null");
    }
    
    // 创建 GEP 指令
    auto ptr_type = get_pointer_type(elem_type);
    auto gep_inst = std::make_shared<GetElementPtrInst>(base, indices, ptr_type);
    gep_inst->name = name;
    gep_inst->loc = {};
    
    auto value = std::make_shared<Value>(name, ptr_type);
    value->defining_inst = gep_inst;
    
    if (current_block) {
        current_block->add_instruction(gep_inst);
    }
    
    return value;
}

// Add 指令便捷方法
std::shared_ptr<Value> IRBuilder::create_add(
    std::shared_ptr<Value> lhs,
    std::shared_ptr<Value> rhs,
    std::string name) {
    return create_binary_op(OpCode::Add, lhs, rhs, name);
}

// Sub 指令便捷方法
std::shared_ptr<Value> IRBuilder::create_sub(
    std::shared_ptr<Value> lhs,
    std::shared_ptr<Value> rhs,
    std::string name) {
    return create_binary_op(OpCode::Sub, lhs, rhs, name);
}

// Mul 指令便捷方法
std::shared_ptr<Value> IRBuilder::create_mul(
    std::shared_ptr<Value> lhs,
    std::shared_ptr<Value> rhs,
    std::string name) {
    return create_binary_op(OpCode::Mul, lhs, rhs, name);
}

// Div 指令便捷方法
std::shared_ptr<Value> IRBuilder::create_div(
    std::shared_ptr<Value> lhs,
    std::shared_ptr<Value> rhs,
    std::string name) {
    return create_binary_op(OpCode::Div, lhs, rhs, name);
}

// Rem 指令便捷方法
std::shared_ptr<Value> IRBuilder::create_rem(
    std::shared_ptr<Value> lhs,
    std::shared_ptr<Value> rhs,
    std::string name) {
    return create_binary_op(OpCode::Mod, lhs, rhs, name);
}

// 张量创建实现
std::shared_ptr<Value> IRBuilder::create_tensor_create(
    std::vector<int64_t> shape,
    std::shared_ptr<Type> elem_type,
    std::string name) {
    
    if (name.empty()) {
        name = create_name("tensor");
    }
    
    if (!elem_type) {
        elem_type = get_primitive_type(PrimitiveTypeKind::Float32);
    }
    
    // 创建张量类型
    auto tensor_type = std::make_shared<TensorType>(elem_type, shape);
    auto tensor_ptr_type = get_pointer_type(tensor_type);
    
    // 创建张量分配指令
    auto tensor_inst = std::make_shared<TensorCreateInst>(shape, elem_type, tensor_ptr_type);
    tensor_inst->name = name;
    tensor_inst->loc = {};
    
    auto value = std::make_shared<Value>(name, tensor_ptr_type);
    value->defining_inst = tensor_inst;
    
    if (current_block) {
        current_block->add_instruction(tensor_inst);
    }
    
    return value;
}

// 张量加载实现
std::shared_ptr<Value> IRBuilder::create_tensor_load(
    std::shared_ptr<Value> tensor,
    std::vector<std::shared_ptr<Value>> indices,
    std::string name) {
    
    if (name.empty()) {
        name = create_name("tload");
    }
    
    if (!tensor) {
        throw std::runtime_error("Tensor load: tensor is null");
    }
    
    // 获取元素类型
    std::shared_ptr<Type> elem_type;
    if (auto* ptr_type = dynamic_cast<PointerType*>(tensor->type.get())) {
        if (auto* tensor_type = dynamic_cast<TensorType*>(ptr_type->pointee.get())) {
            elem_type = tensor_type->element_type;
        }
    }
    
    if (!elem_type) {
        elem_type = get_primitive_type(PrimitiveTypeKind::Float32);
    }
    
    // 创建张量加载指令
    auto tload_inst = std::make_shared<TensorLoadInst>(tensor, indices, elem_type);
    tload_inst->name = name;
    tload_inst->loc = {};
    
    auto value = std::make_shared<Value>(name, elem_type);
    value->defining_inst = tload_inst;
    
    if (current_block) {
        current_block->add_instruction(tload_inst);
    }
    
    return value;
}

// 张量存储实现
void IRBuilder::create_tensor_store(
    std::shared_ptr<Value> tensor,
    std::vector<std::shared_ptr<Value>> indices,
    std::shared_ptr<Value> value) {
    
    if (!tensor) {
        throw std::runtime_error("Tensor store: tensor is null");
    }
    
    if (!value) {
        throw std::runtime_error("Tensor store: value is null");
    }
    
    // 创建张量存储指令
    auto tstore_inst = std::make_shared<TensorStoreInst>(tensor, indices, value);
    tstore_inst->loc = {};
    
    if (current_block) {
        current_block->add_instruction(tstore_inst);
    }
}

// 张量矩阵乘法实现
std::shared_ptr<Value> IRBuilder::create_tensor_matmul(
    std::shared_ptr<Value> lhs,
    std::shared_ptr<Value> rhs,
    std::string name) {
    
    if (name.empty()) {
        name = create_name("matmul");
    }
    
    if (!lhs || !rhs) {
        throw std::runtime_error("Matmul: operands are null");
    }
    
    // 推断结果类型
    std::shared_ptr<Type> result_type;
    if (auto* ptr_type = dynamic_cast<PointerType*>(lhs->type.get())) {
        if (auto* tensor_type = dynamic_cast<TensorType*>(ptr_type->pointee.get())) {
            result_type = tensor_type->element_type;
        }
    }
    
    if (!result_type) {
        result_type = get_primitive_type(PrimitiveTypeKind::Float32);
    }
    
    auto result_ptr_type = get_pointer_type(
        std::make_shared<TensorType>(result_type, std::vector<int64_t>{-1, -1}));
    
    // 创建矩阵乘法指令
    auto matmul_inst = std::make_shared<TensorMatmulInst>(lhs, rhs, result_ptr_type);
    matmul_inst->name = name;
    matmul_inst->loc = {};
    
    auto value = std::make_shared<Value>(name, result_ptr_type);
    value->defining_inst = matmul_inst;
    
    if (current_block) {
        current_block->add_instruction(matmul_inst);
    }
    
    return value;
}

// 张量 reshape 实现
std::shared_ptr<Value> IRBuilder::create_tensor_reshape(
    std::shared_ptr<Value> tensor,
    std::vector<int64_t> new_shape,
    std::string name) {
    
    if (name.empty()) {
        name = create_name("reshape");
    }
    
    if (!tensor) {
        throw std::runtime_error("Reshape: tensor is null");
    }
    
    // 推断元素类型
    std::shared_ptr<Type> elem_type;
    if (auto* ptr_type = dynamic_cast<PointerType*>(tensor->type.get())) {
        if (auto* tensor_type = dynamic_cast<TensorType*>(ptr_type->pointee.get())) {
            elem_type = tensor_type->element_type;
        }
    }
    
    if (!elem_type) {
        elem_type = get_primitive_type(PrimitiveTypeKind::Float32);
    }
    
    auto result_type = std::make_shared<TensorType>(elem_type, new_shape);
    auto result_ptr_type = get_pointer_type(result_type);
    
    // 创建 reshape 指令
    auto reshape_inst = std::make_shared<TensorReshapeInst>(tensor, new_shape, result_ptr_type);
    reshape_inst->name = name;
    reshape_inst->loc = {};
    
    auto value = std::make_shared<Value>(name, result_ptr_type);
    value->defining_inst = reshape_inst;
    
    if (current_block) {
        current_block->add_instruction(reshape_inst);
    }
    
    return value;
}

// Select 指令（条件选择）
std::shared_ptr<Value> IRBuilder::create_select(
    std::shared_ptr<Value> cond,
    std::shared_ptr<Value> true_val,
    std::shared_ptr<Value> false_val,
    std::string name) {
    
    if (name.empty()) {
        name = create_name("select");
    }
    
    if (!cond || !true_val || !false_val) {
        throw std::runtime_error("Select: operands are null");
    }
    
    auto result_type = true_val->type;
    auto select_inst = std::make_shared<SelectInst>(cond, true_val, false_val, result_type);
    select_inst->name = name;
    select_inst->loc = {};
    
    auto value = std::make_shared<Value>(name, result_type);
    value->defining_inst = select_inst;
    
    if (current_block) {
        current_block->add_instruction(select_inst);
    }
    
    return value;
}

// ExtractValue 指令（提取结构体/元组元素）
std::shared_ptr<Value> IRBuilder::create_extract_value(
    std::shared_ptr<Value> aggregate,
    std::vector<int64_t> indices,
    std::string name) {
    
    if (name.empty()) {
        name = create_name("extract");
    }
    
    if (!aggregate) {
        throw std::runtime_error("ExtractValue: aggregate is null");
    }
    
    // 简化：假设提取到元素类型
    auto elem_type = get_primitive_type(PrimitiveTypeKind::Int32);
    auto extract_inst = std::make_shared<ExtractValueInst>(aggregate, indices, elem_type);
    extract_inst->name = name;
    extract_inst->loc = {};
    
    auto value = std::make_shared<Value>(name, elem_type);
    value->defining_inst = extract_inst;
    
    if (current_block) {
        current_block->add_instruction(extract_inst);
    }
    
    return value;
}

// InsertValue 指令（插入结构体/元组元素）
std::shared_ptr<Value> IRBuilder::create_insert_value(
    std::shared_ptr<Value> aggregate,
    std::shared_ptr<Value> elem,
    std::vector<int64_t> indices,
    std::string name) {
    
    if (name.empty()) {
        name = create_name("insert");
    }
    
    if (!aggregate || !elem) {
        throw std::runtime_error("InsertValue: operands are null");
    }
    
    auto result_type = aggregate->type;
    auto insert_inst = std::make_shared<InsertValueInst>(aggregate, elem, indices, result_type);
    insert_inst->name = name;
    insert_inst->loc = {};
    
    auto value = std::make_shared<Value>(name, result_type);
    value->defining_inst = insert_inst;
    
    if (current_block) {
        current_block->add_instruction(insert_inst);
    }
    
    return value;
}

// Memcpy 指令
void IRBuilder::create_memcpy(
    std::shared_ptr<Value> dst,
    std::shared_ptr<Value> src,
    std::shared_ptr<Value> size) {
    
    if (!dst || !src || !size) {
        throw std::runtime_error("Memcpy: operands are null");
    }
    
    auto memcpy_inst = std::make_shared<MemcpyInst>(dst, src, size);
    memcpy_inst->loc = {};
    
    if (current_block) {
        current_block->add_instruction(memcpy_inst);
    }
}

// Memset 指令
void IRBuilder::create_memset(
    std::shared_ptr<Value> ptr,
    std::shared_ptr<Value> val,
    std::shared_ptr<Value> size) {
    
    if (!ptr || !val || !size) {
        throw std::runtime_error("Memset: operands are null");
    }
    
    auto memset_inst = std::make_shared<MemsetInst>(ptr, val, size);
    memset_inst->loc = {};
    
    if (current_block) {
        current_block->add_instruction(memset_inst);
    }
}

// ============================================================================
// 指令具体实现
// ============================================================================

// GetElementPtrInst
GetElementPtrInst::GetElementPtrInst(
    std::shared_ptr<Value> base,
    std::vector<std::shared_ptr<Value>> indices,
    std::shared_ptr<Type> result_type)
    : Instruction(OpCode::GetElementPtr, result_type), base(base), indices(indices) {
    operands.push_back(base);
    for (auto& idx : indices) {
        operands.push_back(idx);
    }
}

std::string GetElementPtrInst::to_string() const {
    std::ostringstream oss;
    if (!name.empty()) oss << "%" << name << " = ";
    oss << "getelementptr ";
    if (base) oss << base->type->to_string() << ", ";
    oss << "%" << (base ? base->name : "null") << ", ";
    for (size_t i = 0; i < indices.size(); ++i) {
        if (i > 0) oss << ", ";
        if (indices[i]) {
            oss << indices[i]->type->to_string() << " %" << indices[i]->name;
        }
    }
    return oss.str();
}

// TensorCreateInst
TensorCreateInst::TensorCreateInst(
    std::vector<int64_t> shape,
    std::shared_ptr<Type> elem_type,
    std::shared_ptr<Type> result_type)
    : Instruction(OpCode::TensorCreate, result_type), shape(shape), element_type(elem_type) {}

std::string TensorCreateInst::to_string() const {
    std::ostringstream oss;
    if (!name.empty()) oss << "%" << name << " = ";
    oss << "tensor.create <";
    for (size_t i = 0; i < shape.size(); ++i) {
        if (i > 0) oss << "x";
        oss << (shape[i] < 0 ? "?" : std::to_string(shape[i]));
    }
    oss << "> " << element_type->to_string();
    return oss.str();
}

// TensorLoadInst
TensorLoadInst::TensorLoadInst(
    std::shared_ptr<Value> tensor,
    std::vector<std::shared_ptr<Value>> indices,
    std::shared_ptr<Type> result_type)
    : Instruction(OpCode::TensorLoad, result_type), tensor(tensor), indices(indices) {
    operands.push_back(tensor);
    for (auto& idx : indices) {
        operands.push_back(idx);
    }
}

std::string TensorLoadInst::to_string() const {
    std::ostringstream oss;
    if (!name.empty()) oss << "%" << name << " = ";
    oss << "tensor.load ";
    if (tensor) oss << "%" << tensor->name;
    oss << "[";
    for (size_t i = 0; i < indices.size(); ++i) {
        if (i > 0) oss << ", ";
        if (indices[i]) oss << "%" << indices[i]->name;
    }
    oss << "]";
    return oss.str();
}

// TensorStoreInst
TensorStoreInst::TensorStoreInst(
    std::shared_ptr<Value> tensor,
    std::vector<std::shared_ptr<Value>> indices,
    std::shared_ptr<Value> value)
    : Instruction(OpCode::TensorStore, nullptr), tensor(tensor), indices(indices), stored_value(value) {
    operands.push_back(tensor);
    for (auto& idx : indices) {
        operands.push_back(idx);
    }
    operands.push_back(value);
}

std::string TensorStoreInst::to_string() const {
    std::ostringstream oss;
    oss << "tensor.store ";
    if (stored_value) oss << "%" << stored_value->name << ", ";
    if (tensor) oss << "%" << tensor->name;
    oss << "[";
    for (size_t i = 0; i < indices.size(); ++i) {
        if (i > 0) oss << ", ";
        if (indices[i]) oss << "%" << indices[i]->name;
    }
    oss << "]";
    return oss.str();
}

// TensorMatmulInst
TensorMatmulInst::TensorMatmulInst(
    std::shared_ptr<Value> lhs,
    std::shared_ptr<Value> rhs,
    std::shared_ptr<Type> result_type)
    : Instruction(OpCode::TensorMatmul, result_type), lhs(lhs), rhs(rhs) {
    operands.push_back(lhs);
    operands.push_back(rhs);
}

std::string TensorMatmulInst::to_string() const {
    std::ostringstream oss;
    if (!name.empty()) oss << "%" << name << " = ";
    oss << "tensor.matmul ";
    if (lhs) oss << "%" << lhs->name << ", ";
    if (rhs) oss << "%" << rhs->name;
    return oss.str();
}

// TensorReshapeInst
TensorReshapeInst::TensorReshapeInst(
    std::shared_ptr<Value> tensor,
    std::vector<int64_t> new_shape,
    std::shared_ptr<Type> result_type)
    : Instruction(OpCode::TensorReshape, result_type), tensor(tensor), new_shape(new_shape) {
    operands.push_back(tensor);
}

std::string TensorReshapeInst::to_string() const {
    std::ostringstream oss;
    if (!name.empty()) oss << "%" << name << " = ";
    oss << "tensor.reshape ";
    if (tensor) oss << "%" << tensor->name << ", <";
    for (size_t i = 0; i < new_shape.size(); ++i) {
        if (i > 0) oss << "x";
        oss << (new_shape[i] < 0 ? "?" : std::to_string(new_shape[i]));
    }
    oss << ">";
    return oss.str();
}

// SelectInst
SelectInst::SelectInst(
    std::shared_ptr<Value> cond,
    std::shared_ptr<Value> true_val,
    std::shared_ptr<Value> false_val,
    std::shared_ptr<Type> result_type)
    : Instruction(OpCode::Select, result_type), condition(cond), true_value(true_val), false_value(false_val) {
    operands.push_back(cond);
    operands.push_back(true_val);
    operands.push_back(false_val);
}

std::string SelectInst::to_string() const {
    std::ostringstream oss;
    if (!name.empty()) oss << "%" << name << " = ";
    oss << "select ";
    if (condition) oss << "%" << condition->name << ", ";
    if (true_value) oss << "%" << true_value->name << ", ";
    if (false_value) oss << "%" << false_value->name;
    return oss.str();
}

// ExtractValueInst
ExtractValueInst::ExtractValueInst(
    std::shared_ptr<Value> aggregate,
    std::vector<int64_t> indices,
    std::shared_ptr<Type> result_type)
    : Instruction(OpCode::ExtractValue, result_type), aggregate(aggregate), indices(indices) {
    operands.push_back(aggregate);
}

std::string ExtractValueInst::to_string() const {
    std::ostringstream oss;
    if (!name.empty()) oss << "%" << name << " = ";
    oss << "extractvalue ";
    if (aggregate) oss << "%" << aggregate->name;
    oss << ", ";
    for (size_t i = 0; i < indices.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << indices[i];
    }
    return oss.str();
}

// InsertValueInst
InsertValueInst::InsertValueInst(
    std::shared_ptr<Value> aggregate,
    std::shared_ptr<Value> elem,
    std::vector<int64_t> indices,
    std::shared_ptr<Type> result_type)
    : Instruction(OpCode::InsertValue, result_type), aggregate(aggregate), element(elem), indices(indices) {
    operands.push_back(aggregate);
    operands.push_back(elem);
}

std::string InsertValueInst::to_string() const {
    std::ostringstream oss;
    if (!name.empty()) oss << "%" << name << " = ";
    oss << "insertvalue ";
    if (aggregate) oss << "%" << aggregate->name << ", ";
    if (element) oss << "%" << element->name;
    oss << ", ";
    for (size_t i = 0; i < indices.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << indices[i];
    }
    return oss.str();
}

// MemcpyInst
MemcpyInst::MemcpyInst(
    std::shared_ptr<Value> dst,
    std::shared_ptr<Value> src,
    std::shared_ptr<Value> size)
    : Instruction(OpCode::Memcpy, nullptr), dst(dst), src(src), size(size) {
    operands.push_back(dst);
    operands.push_back(src);
    operands.push_back(size);
}

std::string MemcpyInst::to_string() const {
    std::ostringstream oss;
    oss << "memcpy ";
    if (dst) oss << "%" << dst->name << ", ";
    if (src) oss << "%" << src->name << ", ";
    if (size) oss << "%" << size->name;
    return oss.str();
}

// MemsetInst
MemsetInst::MemsetInst(
    std::shared_ptr<Value> ptr,
    std::shared_ptr<Value> val,
    std::shared_ptr<Value> size)
    : Instruction(OpCode::Memset, nullptr), ptr(ptr), val(val), size(size) {
    operands.push_back(ptr);
    operands.push_back(val);
    operands.push_back(size);
}

std::string MemsetInst::to_string() const {
    std::ostringstream oss;
    oss << "memset ";
    if (ptr) oss << "%" << ptr->name << ", ";
    if (val) oss << "%" << val->name << ", ";
    if (size) oss << "%" << size->name;
    return oss.str();
}

// ============================================================================
// TensorType 实现
// ============================================================================

TensorType::TensorType(std::shared_ptr<Type> elem, std::vector<int64_t> shape)
    : Type(), element_type(elem), shape(shape) {}

std::string TensorType::to_string() const {
    std::ostringstream oss;
    oss << "Tensor<";
    for (size_t i = 0; i < shape.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << (shape[i] < 0 ? "?" : std::to_string(shape[i]));
    }
    oss << ">";
    if (element_type) oss << "(" << element_type->to_string() << ")";
    return oss.str();
}

bool TensorType::equals(const Type& other) const {
    auto* other_tensor = dynamic_cast<const TensorType*>(&other);
    if (!other_tensor) return false;
    if (shape.size() != other_tensor->shape.size()) return false;
    for (size_t i = 0; i < shape.size(); ++i) {
        if (shape[i] != other_tensor->shape[i]) return false;
    }
    return element_type && other_tensor->element_type &&
           element_type->equals(*other_tensor->element_type);
}

int64_t TensorType::num_elements() const {
    if (shape.empty()) return 1;
    int64_t n = 1;
    for (auto d : shape) {
        if (d < 0) return -1;
        n *= d;
    }
    return n;
}

} // namespace ir
} // namespace claw
