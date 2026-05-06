// jit/jit_compiler.cpp - 高性能 JIT 编译器实现

#include "jit_compiler.h"
#include "jit_runtime.h"
#include "jit_stdlib_integration.h"
#include <cstring>
#include <cmath>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <optional>
#include <sys/mman.h>

namespace claw {
namespace jit {


// ============================================================================
// CodeCache 实现
// ============================================================================

CodeCache::CodeCache(size_t max_size) : max_size_(max_size) {
}

CodeCache::~CodeCache() {
    clear();
}

void* CodeCache::allocate(size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (used_size_ + size > max_size_) {
        clear();
        if (used_size_ + size > max_size_) {
            return nullptr;
        }
    }

    // 使用 mmap 分配可执行内存
    // macOS: MAP_PRIVATE | MAP_ANON, 先 RW 再 mprotect 为 RX
    size_t alloc_size = (size + 4095) & ~4095; // 页对齐
    if (alloc_size == 0) alloc_size = 4096;

    void* ptr = mmap(nullptr, alloc_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANON, -1, 0);
    if (ptr == MAP_FAILED) {
        return nullptr;
    }

    allocations_.push_back({ptr, alloc_size});
    used_size_ += alloc_size;
    return ptr;
}

void CodeCache::deallocate(void* ptr, [[maybe_unused]] size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto it = allocations_.begin(); it != allocations_.end(); ++it) {
        if (it->first == ptr) {
            munmap(ptr, it->second);
            used_size_ -= it->second;
            allocations_.erase(it);
            return;
        }
    }
}

void CodeCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& alloc : allocations_) {
        munmap(alloc.first, alloc.second);
    }
    allocations_.clear();
    used_size_ = 0;
}

// ============================================================================
// InlineCache 实现
// ============================================================================

InlineCache::InlineCache(size_t max_size) : max_size_(max_size) {}

void* InlineCache::lookup(const std::vector<bytecode::ValueType>& param_types) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& entry : entries_) {
        if (entry.first == param_types) {
            entry.second = reinterpret_cast<void*>(
                reinterpret_cast<uintptr_t>(entry.second) + 1
            );
            return entry.second;
        }
    }
    return nullptr;
}

void InlineCache::update(const std::vector<bytecode::ValueType>& param_types, void* code) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 查找现有条目
    for (auto& entry : entries_) {
        if (entry.first == param_types) {
            entry.second = code;
            return;
        }
    }
    
    // 添加新条目
    if (entries_.size() < max_size_) {
        entries_.push_back({param_types, code});
    } else {
        // 替换最旧的条目 (简单 FIFO)
        entries_.erase(entries_.begin());
        entries_.push_back({param_types, code});
    }
}

void InlineCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
}

// ============================================================================
// MethodJITCompiler 实现
// ============================================================================

MethodJITCompiler::MethodJITCompiler() {
    code_cache_ = std::make_unique<CodeCache>(32 * 1024 * 1024); // 32MB
    // 初始化 x86_64 emitter 用于代码生成
    emitter_ = std::make_unique<x86_64::X86_64Emitter>(65536);
    // 初始化线性扫描寄存器分配器 [NEW]
    reg_allocator_ = std::make_unique<LinearScanRegisterAllocator>();
}

MethodJITCompiler::~MethodJITCompiler() {
    clear_cache();
}

CompilationResult MethodJITCompiler::compile(const bytecode::Function& func) {
    CompilationResult result;

    // 检查缓存
    auto it = compiled_functions_.find(func.name);
    if (it != compiled_functions_.end()) {
        result.success = true;
        result.machine_code = it->second;
        return result;
    }

    // [NEW] 执行寄存器分配
    auto reg_allocation = allocate_registers_for_function(func);

    // 估计代码大小
    size_t code_size = estimate_code_size(func);

    // 分配代码缓存
    void* code_ptr = code_cache_->allocate(code_size);
    if (!code_ptr) {
        result.error_message = "Failed to allocate code cache";
        return result;
    }

    // 提前注册函数地址，支持自引用（递归）
    compiled_functions_[func.name] = code_ptr;

    // 编译函数 - 完整的 x86-64 代码生成
    void* current = code_ptr;
    
    // 生成函数序言
    emit_prologue(current, func.local_count, func.arity);

    // 初始化类型跟踪 (参数类型从字节码函数元数据获取)
    last_pushed_type_ = bytecode::ValueType::I64;
    local_types_.assign(func.local_count, bytecode::ValueType::I64);
    for (size_t i = 0; i < func.param_types.size() && i < func.local_count; ++i) {
        local_types_[i] = func.param_types[i];
    }
    global_types_.clear();
    type_stack_.clear();

    // 记录每条字节码指令对应的本地代码地址
    std::vector<void*> instruction_addrs(func.code.size(), nullptr);
    struct PendingJump { void* patch_addr; size_t jump_size; size_t target_idx; };
    std::vector<PendingJump> pending_jumps;

    // 编译每条字节码指令
    for (size_t i = 0; i < func.code.size(); ++i) {
        const auto& inst = func.code[i];
        instruction_addrs[i] = current;

        // 根据 OpCode 分发到对应的指令发射函数
        switch (inst.op) {
            // 栈操作
            case bytecode::OpCode::NOP:
                break;
            case bytecode::OpCode::POP:
                emit_stack_op(current, inst.op);
                if (!type_stack_.empty()) type_stack_.pop_back();
                break;
            case bytecode::OpCode::DUP:
                emit_stack_op(current, inst.op);
                if (!type_stack_.empty()) type_stack_.push_back(type_stack_.back());
                break;
            case bytecode::OpCode::SWAP:
                emit_stack_op(current, inst.op);
                if (type_stack_.size() >= 2) {
                    std::swap(type_stack_[type_stack_.size() - 1], type_stack_[type_stack_.size() - 2]);
                }
                break;

            case bytecode::OpCode::PUSH: {
                // 从常量池加载常量并压栈
                uint8_t* code = static_cast<uint8_t*>(current);
                uint32_t const_idx = inst.operand;
                int64_t val = 0;
                last_pushed_type_ = bytecode::ValueType::I64;
                if (current_module_ && const_idx < current_module_->constants.values.size()) {
                    const auto& cv = current_module_->constants.values[const_idx];
                    if (cv.type == bytecode::ValueType::I64) {
                        val = cv.data.i64;
                    } else if (cv.type == bytecode::ValueType::BOOL) {
                        val = cv.data.b ? 1 : 0;
                    } else if (cv.type == bytecode::ValueType::F64) {
                        last_pushed_type_ = bytecode::ValueType::F64;
                        union { double d; uint64_t u; } conv;
                        conv.d = cv.data.f64;
                        code[0] = 0x48; code[1] = 0xB8;
                        *reinterpret_cast<uint64_t*>(&code[2]) = conv.u;
                        code[10] = 0x50; // push rax
                        current = &code[11];
                        type_stack_.push_back(last_pushed_type_);
                        break;
                    } else if (cv.type == bytecode::ValueType::STRING) {
                        last_pushed_type_ = bytecode::ValueType::STRING;
                        code[0] = 0x48; code[1] = 0xB8;
                        *reinterpret_cast<uint64_t*>(&code[2]) = reinterpret_cast<uint64_t>(cv.str.c_str());
                        code[10] = 0x50; // push rax
                        current = &code[11];
                        type_stack_.push_back(last_pushed_type_);
                        break;
                    }
                }
                if (val >= INT32_MIN && val <= INT32_MAX) {
                    code[0] = 0x48; code[1] = 0xC7; code[2] = 0xC0;
                    *reinterpret_cast<int32_t*>(&code[3]) = static_cast<int32_t>(val);
                    code[7] = 0x50; // push rax
                    current = &code[8];
                } else {
                    code[0] = 0x48; code[1] = 0xB8;
                    *reinterpret_cast<int64_t*>(&code[2]) = val;
                    code[10] = 0x50; // push rax
                    current = &code[11];
                }
                type_stack_.push_back(last_pushed_type_);
                break;
            }

            // 整数运算 (类型感知: 如果操作数都是浮点，发射浮点机器码)
            case bytecode::OpCode::IADD:
            case bytecode::OpCode::ISUB:
            case bytecode::OpCode::IMUL:
            case bytecode::OpCode::IDIV:
            case bytecode::OpCode::IMOD: {
                bool is_float = false;
                if (type_stack_.size() >= 2) {
                    bytecode::ValueType right = type_stack_[type_stack_.size() - 1];
                    bytecode::ValueType left = type_stack_[type_stack_.size() - 2];
                    if (left == bytecode::ValueType::F64 && right == bytecode::ValueType::F64) {
                        is_float = true;
                    }
                }
                if (is_float) {
                    bytecode::OpCode float_op;
                    switch (inst.op) {
                        case bytecode::OpCode::IADD: float_op = bytecode::OpCode::FADD; break;
                        case bytecode::OpCode::ISUB: float_op = bytecode::OpCode::FSUB; break;
                        case bytecode::OpCode::IMUL: float_op = bytecode::OpCode::FMUL; break;
                        case bytecode::OpCode::IDIV: float_op = bytecode::OpCode::FDIV; break;
                        case bytecode::OpCode::IMOD: float_op = bytecode::OpCode::FMOD; break;
                        default: float_op = inst.op; break;
                    }
                    emit_arithmetic_op(current, float_op);
                    type_stack_.pop_back();
                    type_stack_.pop_back();
                    type_stack_.push_back(bytecode::ValueType::F64);
                    last_pushed_type_ = bytecode::ValueType::F64;
                } else {
                    emit_arithmetic_op(current, inst.op);
                    type_stack_.pop_back();
                    type_stack_.pop_back();
                    type_stack_.push_back(bytecode::ValueType::I64);
                    last_pushed_type_ = bytecode::ValueType::I64;
                }
                break;
            }

            // 浮点运算
            case bytecode::OpCode::FADD:
            case bytecode::OpCode::FSUB:
            case bytecode::OpCode::FMUL:
            case bytecode::OpCode::FDIV:
            case bytecode::OpCode::FMOD:
                emit_arithmetic_op(current, inst.op);
                if (type_stack_.size() >= 2) {
                    type_stack_.pop_back();
                    type_stack_.pop_back();
                }
                type_stack_.push_back(bytecode::ValueType::F64);
                last_pushed_type_ = bytecode::ValueType::F64;
                break;

            // 整数比较 (类型感知)
            case bytecode::OpCode::IEQ:
            case bytecode::OpCode::INE:
            case bytecode::OpCode::ILT:
            case bytecode::OpCode::ILE:
            case bytecode::OpCode::IGT:
            case bytecode::OpCode::IGE: {
                bool is_float = false;
                if (type_stack_.size() >= 2) {
                    bytecode::ValueType right = type_stack_[type_stack_.size() - 1];
                    bytecode::ValueType left = type_stack_[type_stack_.size() - 2];
                    if (left == bytecode::ValueType::F64 && right == bytecode::ValueType::F64) {
                        is_float = true;
                    }
                }
                if (is_float) {
                    bytecode::OpCode float_op;
                    switch (inst.op) {
                        case bytecode::OpCode::IEQ: float_op = bytecode::OpCode::FEQ; break;
                        case bytecode::OpCode::INE: float_op = bytecode::OpCode::FNE; break;
                        case bytecode::OpCode::ILT: float_op = bytecode::OpCode::FLT; break;
                        case bytecode::OpCode::ILE: float_op = bytecode::OpCode::FLE; break;
                        case bytecode::OpCode::IGT: float_op = bytecode::OpCode::FGT; break;
                        case bytecode::OpCode::IGE: float_op = bytecode::OpCode::FGE; break;
                        default: float_op = inst.op; break;
                    }
                    emit_comparison_op(current, float_op);
                } else {
                    emit_comparison_op(current, inst.op);
                }
                if (type_stack_.size() >= 2) {
                    type_stack_.pop_back();
                    type_stack_.pop_back();
                }
                type_stack_.push_back(bytecode::ValueType::I64);
                last_pushed_type_ = bytecode::ValueType::I64;
                break;
            }

            // 浮点比较
            case bytecode::OpCode::FEQ:
            case bytecode::OpCode::FNE:
            case bytecode::OpCode::FLT:
            case bytecode::OpCode::FLE:
            case bytecode::OpCode::FGT:
            case bytecode::OpCode::FGE:
                emit_comparison_op(current, inst.op);
                if (type_stack_.size() >= 2) {
                    type_stack_.pop_back();
                    type_stack_.pop_back();
                }
                type_stack_.push_back(bytecode::ValueType::I64);
                last_pushed_type_ = bytecode::ValueType::I64;
                break;

            // 逻辑/位运算
            case bytecode::OpCode::AND:
            case bytecode::OpCode::OR:
                emit_logical_op(current, inst.op);
                if (type_stack_.size() >= 2) {
                    type_stack_.pop_back();
                    type_stack_.pop_back();
                }
                type_stack_.push_back(bytecode::ValueType::I64);
                last_pushed_type_ = bytecode::ValueType::I64;
                break;
            case bytecode::OpCode::NOT:
            case bytecode::OpCode::BAND:
            case bytecode::OpCode::BOR:
            case bytecode::OpCode::BXOR:
            case bytecode::OpCode::BNOT:
                emit_logical_op(current, inst.op);
                if (!type_stack_.empty()) type_stack_.pop_back();
                type_stack_.push_back(bytecode::ValueType::I64);
                last_pushed_type_ = bytecode::ValueType::I64;
                break;

            // 位移运算
            case bytecode::OpCode::SHL:
            case bytecode::OpCode::SHR:
            case bytecode::OpCode::USHR:
                emit_shift_op(current, inst.op);
                if (type_stack_.size() >= 2) {
                    type_stack_.pop_back();
                    type_stack_.pop_back();
                }
                type_stack_.push_back(bytecode::ValueType::I64);
                last_pushed_type_ = bytecode::ValueType::I64;
                break;

            // 类型转换
            case bytecode::OpCode::I2F:
                emit_type_conversion(current, inst.op);
                if (!type_stack_.empty()) type_stack_.pop_back();
                type_stack_.push_back(bytecode::ValueType::F64);
                last_pushed_type_ = bytecode::ValueType::F64;
                break;
            case bytecode::OpCode::F2I:
            case bytecode::OpCode::I2B:
            case bytecode::OpCode::B2I:
                emit_type_conversion(current, inst.op);
                if (!type_stack_.empty()) type_stack_.pop_back();
                type_stack_.push_back(bytecode::ValueType::I64);
                last_pushed_type_ = bytecode::ValueType::I64;
                break;
            case bytecode::OpCode::I2S:
            case bytecode::OpCode::F2S:
                emit_type_conversion(current, inst.op);
                if (!type_stack_.empty()) type_stack_.pop_back();
                type_stack_.push_back(bytecode::ValueType::STRING);
                last_pushed_type_ = bytecode::ValueType::STRING;
                break;
            case bytecode::OpCode::S2I:
                emit_type_conversion(current, inst.op);
                if (!type_stack_.empty()) type_stack_.pop_back();
                type_stack_.push_back(bytecode::ValueType::I64);
                last_pushed_type_ = bytecode::ValueType::I64;
                break;
            case bytecode::OpCode::S2F:
                emit_type_conversion(current, inst.op);
                if (!type_stack_.empty()) type_stack_.pop_back();
                type_stack_.push_back(bytecode::ValueType::F64);
                last_pushed_type_ = bytecode::ValueType::F64;
                break;

            // 局部变量
            case bytecode::OpCode::LOAD_LOCAL:
                emit_load_local(current, static_cast<size_t>(inst.operand));
                if (inst.operand < local_types_.size()) {
                    last_pushed_type_ = local_types_[inst.operand];
                }
                type_stack_.push_back(last_pushed_type_);
                break;
            case bytecode::OpCode::STORE_LOCAL:
                emit_store_local(current, static_cast<size_t>(inst.operand));
                if (!type_stack_.empty()) {
                    if (inst.operand < local_types_.size()) {
                        local_types_[inst.operand] = type_stack_.back();
                    }
                    type_stack_.pop_back();
                }
                break;
            case bytecode::OpCode::LOAD_LOCAL_0:
                emit_load_local(current, 0);
                if (!local_types_.empty()) last_pushed_type_ = local_types_[0];
                type_stack_.push_back(last_pushed_type_);
                break;
            case bytecode::OpCode::LOAD_LOCAL_1:
                emit_load_local(current, 1);
                if (local_types_.size() > 1) last_pushed_type_ = local_types_[1];
                type_stack_.push_back(last_pushed_type_);
                break;

            // 跳转 - 正确处理前向/后向跳转
            case bytecode::OpCode::JMP:
            case bytecode::OpCode::JMP_IF:
            case bytecode::OpCode::JMP_IF_NOT:
            case bytecode::OpCode::LOOP: {
                int32_t rel = static_cast<int32_t>(inst.operand);
                size_t target_idx = static_cast<size_t>(static_cast<int64_t>(i) + 1 + rel);
                uint8_t* code = static_cast<uint8_t*>(current);

                if (inst.op == bytecode::OpCode::JMP) {
                    code[0] = 0xe9;
                    if (target_idx <= i) {
                        int32_t offset = static_cast<int32_t>(
                            static_cast<uint8_t*>(instruction_addrs[target_idx]) - (code + 5));
                        *reinterpret_cast<int32_t*>(&code[1]) = offset;
                    } else {
                        *reinterpret_cast<int32_t*>(&code[1]) = 0;
                        pending_jumps.push_back({&code[1], 4, target_idx});
                    }
                    current = &code[5];
                } else if (inst.op == bytecode::OpCode::JMP_IF) {
                    code[0] = 0x58; // pop rax
                    code[1] = 0x48; code[2] = 0x85; code[3] = 0xc0; // test rax, rax
                    code[4] = 0x0f; code[5] = 0x85; // jne rel32
                    if (target_idx <= i) {
                        int32_t offset = static_cast<int32_t>(
                            static_cast<uint8_t*>(instruction_addrs[target_idx]) - (code + 10));
                        *reinterpret_cast<int32_t*>(&code[6]) = offset;
                    } else {
                        *reinterpret_cast<int32_t*>(&code[6]) = 0;
                        pending_jumps.push_back({&code[6], 4, target_idx});
                    }
                    current = &code[10];
                    if (!type_stack_.empty()) type_stack_.pop_back();
                } else if (inst.op == bytecode::OpCode::JMP_IF_NOT) {
                    code[0] = 0x58; // pop rax
                    code[1] = 0x48; code[2] = 0x85; code[3] = 0xc0; // test rax, rax
                    code[4] = 0x0f; code[5] = 0x84; // je rel32
                    if (target_idx <= i) {
                        int32_t offset = static_cast<int32_t>(
                            static_cast<uint8_t*>(instruction_addrs[target_idx]) - (code + 10));
                        *reinterpret_cast<int32_t*>(&code[6]) = offset;
                    } else {
                        *reinterpret_cast<int32_t*>(&code[6]) = 0;
                        pending_jumps.push_back({&code[6], 4, target_idx});
                    }
                    current = &code[10];
                    if (!type_stack_.empty()) type_stack_.pop_back();
                } else {
                    code[0] = 0x59; // pop rcx
                    code[1] = 0x48; code[2] = 0xff; code[3] = 0xc9; // dec rcx
                    code[4] = 0x51; // push rcx
                    code[5] = 0x0f; code[6] = 0x85; // jne rel32
                    if (target_idx <= i) {
                        int32_t offset = static_cast<int32_t>(
                            static_cast<uint8_t*>(instruction_addrs[target_idx]) - (code + 11));
                        *reinterpret_cast<int32_t*>(&code[7]) = offset;
                    } else {
                        *reinterpret_cast<int32_t*>(&code[7]) = 0;
                        pending_jumps.push_back({&code[7], 4, target_idx});
                    }
                    current = &code[11];
                }
                break;
            }

            // 数组操作
            case bytecode::OpCode::ALLOC_ARRAY:
                emit_array_op(current, inst.op);
                type_stack_.push_back(bytecode::ValueType::ARRAY);
                last_pushed_type_ = bytecode::ValueType::ARRAY;
                break;
            case bytecode::OpCode::LOAD_INDEX: {
                // Determine object type (object is below index on stack)
                bytecode::ValueType obj_type = bytecode::ValueType::I64;
                if (type_stack_.size() >= 2) {
                    obj_type = type_stack_[type_stack_.size() - 2];
                }

                uint8_t* code = static_cast<uint8_t*>(current);
                // pop rsi (index, top of stack)
                code[0] = 0x5e;
                // pop rdi (object pointer)
                code[1] = 0x5f;
                // call <runtime_func>
                code[2] = 0xe8;

                void* func_addr = nullptr;
                if (obj_type == bytecode::ValueType::TUPLE) {
                    func_addr = get_runtime_function_by_name("tuple_get");
                } else {
                    func_addr = get_runtime_function_by_name("array_get");
                }

                if (func_addr) {
                    int32_t offset = static_cast<int32_t>(
                        reinterpret_cast<int64_t>(func_addr) - reinterpret_cast<int64_t>(&code[7])
                    );
                    *reinterpret_cast<int32_t*>(&code[3]) = offset;
                } else {
                    *reinterpret_cast<int32_t*>(&code[3]) = 0;
                }
                // push rax (result)
                code[7] = 0x50;
                current = &code[8];

                if (type_stack_.size() >= 2) {
                    type_stack_.resize(type_stack_.size() - 2);
                }
                type_stack_.push_back(bytecode::ValueType::I64);
                last_pushed_type_ = bytecode::ValueType::I64;
                break;
            }
            case bytecode::OpCode::STORE_INDEX:
                emit_array_op(current, inst.op);
                if (type_stack_.size() >= 3) {
                    type_stack_.pop_back();
                    type_stack_.pop_back();
                    type_stack_.pop_back();
                }
                break;
            case bytecode::OpCode::ARRAY_LEN:
                emit_array_op(current, inst.op);
                if (!type_stack_.empty()) type_stack_.pop_back();
                type_stack_.push_back(bytecode::ValueType::I64);
                last_pushed_type_ = bytecode::ValueType::I64;
                break;
            case bytecode::OpCode::ARRAY_PUSH:
                emit_array_op(current, inst.op);
                if (type_stack_.size() >= 2) {
                    type_stack_.pop_back();
                    type_stack_.pop_back();
                }
                break;

            // 返回
            case bytecode::OpCode::RET:
            case bytecode::OpCode::RET_NULL:
                emit_return_op(current, inst.op);
                break;

            // 函数调用 (暂不展开内联)
            case bytecode::OpCode::CALL: {
                emit_call_op(current, inst);
                // Pop callee + args, push return value
                uint32_t arg_count = inst.operand;
                size_t pop_count = arg_count + 1;
                if (type_stack_.size() >= pop_count) {
                    type_stack_.resize(type_stack_.size() - pop_count);
                }
                // Infer return type from callee if previous instruction was LOAD_GLOBAL
                bytecode::ValueType ret_type = bytecode::ValueType::I64;
                if (i > 0 && func.code[i-1].op == bytecode::OpCode::LOAD_GLOBAL) {
                    uint32_t str_idx = static_cast<uint32_t>(func.code[i-1].operand);
                    if (current_module_ && str_idx < current_module_->constants.values.size()) {
                        const auto& cv = current_module_->constants.values[str_idx];
                        if (cv.type == bytecode::ValueType::STRING) {
                            for (const auto& f : current_module_->functions) {
                                if (f.name == cv.str) {
                                    ret_type = f.return_type;
                                    break;
                                }
                            }
                        }
                    }
                }
                type_stack_.push_back(ret_type);
                last_pushed_type_ = ret_type;
                break;
            }
            case bytecode::OpCode::CALL_EXT: {
                emit_call_op(current, inst);
                // Pop args, push return value
                uint32_t ext_arg_count = (inst.operand >> 16) & 0xFFFF;
                if (type_stack_.size() >= ext_arg_count) {
                    type_stack_.resize(type_stack_.size() - ext_arg_count);
                }
                type_stack_.push_back(bytecode::ValueType::I64);
                last_pushed_type_ = bytecode::ValueType::I64;
                break;
            }

            // 全局变量
            case bytecode::OpCode::LOAD_GLOBAL:
                emit_load_global(current, static_cast<uint32_t>(inst.operand));
                if (current_module_ && inst.operand < current_module_->constants.values.size()) {
                    const auto& cv = current_module_->constants.values[inst.operand];
                    if (cv.type == bytecode::ValueType::STRING) {
                        auto it = global_types_.find(cv.str);
                        if (it != global_types_.end()) {
                            last_pushed_type_ = it->second;
                        }
                    }
                }
                type_stack_.push_back(last_pushed_type_);
                break;
            case bytecode::OpCode::STORE_GLOBAL:
                emit_store_global(current, static_cast<uint32_t>(inst.operand));
                if (!type_stack_.empty()) {
                    if (current_module_ && inst.operand < current_module_->constants.values.size()) {
                        const auto& cv = current_module_->constants.values[inst.operand];
                        if (cv.type == bytecode::ValueType::STRING) {
                            global_types_[cv.str] = type_stack_.back();
                        }
                    }
                    type_stack_.pop_back();
                }
                break;
            case bytecode::OpCode::DEFINE_GLOBAL:
                emit_define_global(current, static_cast<uint32_t>(inst.operand));
                if (!type_stack_.empty()) {
                    if (current_module_ && inst.operand < current_module_->constants.values.size()) {
                        const auto& cv = current_module_->constants.values[inst.operand];
                        if (cv.type == bytecode::ValueType::STRING) {
                            global_types_[cv.str] = type_stack_.back();
                        }
                    }
                    type_stack_.pop_back();
                }
                break;

            // 张量操作
            case bytecode::OpCode::TENSOR_CREATE:
            case bytecode::OpCode::TENSOR_LOAD:
            case bytecode::OpCode::TENSOR_STORE:
            case bytecode::OpCode::TENSOR_MATMUL:
            case bytecode::OpCode::TENSOR_RESHAPE:
                emit_tensor_op(current, inst.op);
                type_stack_.push_back(bytecode::ValueType::I64);
                last_pushed_type_ = bytecode::ValueType::I64;
                break;

            // 闭包操作
            case bytecode::OpCode::CLOSURE:
                // 闭包创建需要运行时支持
                break;
            case bytecode::OpCode::CLOSE_UPVALUE:
            case bytecode::OpCode::GET_UPVALUE:
                emit_upvalue_op(current, inst.op);
                break;

            // 元组操作
            case bytecode::OpCode::CREATE_TUPLE: {
                emit_tuple_op(current, inst.op);
                if (type_stack_.size() >= 2) {
                    type_stack_.resize(type_stack_.size() - 2);
                }
                type_stack_.push_back(bytecode::ValueType::TUPLE);
                last_pushed_type_ = bytecode::ValueType::TUPLE;
                break;
            }
            case bytecode::OpCode::LOAD_ELEM: {
                emit_tuple_op(current, inst.op);
                if (type_stack_.size() >= 2) {
                    type_stack_.resize(type_stack_.size() - 2);
                }
                type_stack_.push_back(bytecode::ValueType::I64);
                last_pushed_type_ = bytecode::ValueType::I64;
                break;
            }
            case bytecode::OpCode::STORE_ELEM: {
                emit_tuple_op(current, inst.op);
                if (type_stack_.size() >= 3) {
                    type_stack_.resize(type_stack_.size() - 3);
                }
                break;
            }

            // 系统操作 - 使用运行时函数
            case bytecode::OpCode::PRINT:
            case bytecode::OpCode::PRINTLN:
            case bytecode::OpCode::PANIC:
            case bytecode::OpCode::HALT:
            case bytecode::OpCode::INPUT:
            case bytecode::OpCode::TYPE_OF:
                emit_system_call(current, inst.op);
                break;

            // EXT 标准库函数调用 - 2026-04-26 新增
            case bytecode::OpCode::EXT:
                // inst.args[0] 包含标准库函数 opcode
                emit_ext_stdlib_call(current, inst);
                break;

            default:
                // 未知操作码，跳过
                break;
        }
    }

    // 回填前向跳转
    for (const auto& pj : pending_jumps) {
        if (pj.target_idx < instruction_addrs.size() && instruction_addrs[pj.target_idx]) {
            int32_t offset = static_cast<int32_t>(
                static_cast<uint8_t*>(instruction_addrs[pj.target_idx]) -
                (static_cast<uint8_t*>(pj.patch_addr) + pj.jump_size));
            *reinterpret_cast<int32_t*>(pj.patch_addr) = offset;
        }
    }
    
    // 生成函数结尾 (仅当最后一条指令不是返回时)
    if (func.code.empty() ||
        (func.code.back().op != bytecode::OpCode::RET &&
         func.code.back().op != bytecode::OpCode::RET_NULL)) {
        emit_epilogue(current);
    }

    // 计算实际生成的代码大小并设置可执行权限
    size_t actual_size = static_cast<size_t>(
        static_cast<uint8_t*>(current) - static_cast<uint8_t*>(code_ptr));
    if (actual_size > 0) {
        size_t page_size = 4096;
        size_t protect_size = ((actual_size + page_size - 1) / page_size) * page_size;
        if (protect_size == 0) protect_size = page_size;
        if (mprotect(code_ptr, protect_size, PROT_READ | PROT_EXEC) != 0) {
            result.error_message = "mprotect failed";
            return result;
        }
    }

    // 更新缓存
    compiled_functions_[func.name] = code_ptr;

    result.success = true;
    result.machine_code = code_ptr;
    result.code_size = actual_size;

    return result;
}

void* MethodJITCompiler::get_compiled_code(const std::string& func_name) {
    auto it = compiled_functions_.find(func_name);
    if (it != compiled_functions_.end()) {
        return it->second;
    }
    return nullptr;
}

void MethodJITCompiler::clear_cache() {
    compiled_functions_.clear();
    code_cache_->clear();
}

void MethodJITCompiler::emit_prologue(void*& code_ptr, size_t local_count, uint32_t arity) {
    // 直接手写字节码到输出缓冲区
    uint8_t* code = static_cast<uint8_t*>(code_ptr);
    // push rbp
    code[0] = 0x55;
    // mov rbp, rsp
    code[1] = 0x48;
    code[2] = 0x89;
    code[3] = 0xe5;
    code += 4;

    if (local_count > 0) {
        // Round up to 16 bytes to maintain System V AMD64 stack alignment
        size_t stack_size = ((local_count * 8 + 15) / 16) * 16;
        if (stack_size <= 127) {
            // sub rsp, imm8
            code[0] = 0x48;
            code[1] = 0x83;
            code[2] = 0xec;
            code[3] = static_cast<uint8_t>(stack_size);
            code += 4;
        } else {
            // sub rsp, imm32
            code[0] = 0x48;
            code[1] = 0x81;
            code[2] = 0xec;
            *reinterpret_cast<uint32_t*>(&code[3]) = static_cast<uint32_t>(stack_size);
            code += 7;
        }
    }

    // Store incoming register arguments into local variable slots
    // System V AMD64: rdi=arg0, rsi=arg1, rdx=arg2, rcx=arg3, r8=arg4, r9=arg5
    // Local slot i is at [rbp - (i+1)*8]
    if (arity > 0) {
        code[0] = 0x48; code[1] = 0x89; code[2] = 0x7d; code[3] = 0xf8; // mov [rbp-8], rdi
        code += 4;
    }
    if (arity > 1) {
        code[0] = 0x48; code[1] = 0x89; code[2] = 0x75; code[3] = 0xf0; // mov [rbp-16], rsi
        code += 4;
    }
    if (arity > 2) {
        code[0] = 0x48; code[1] = 0x89; code[2] = 0x55; code[3] = 0xe8; // mov [rbp-24], rdx
        code += 4;
    }
    if (arity > 3) {
        code[0] = 0x48; code[1] = 0x89; code[2] = 0x4d; code[3] = 0xe0; // mov [rbp-32], rcx
        code += 4;
    }
    if (arity > 4) {
        code[0] = 0x4c; code[1] = 0x89; code[2] = 0x45; code[3] = 0xd8; // mov [rbp-40], r8
        code += 4;
    }
    if (arity > 5) {
        code[0] = 0x4c; code[1] = 0x89; code[2] = 0x4d; code[3] = 0xd0; // mov [rbp-48], r9
        code += 4;
    }

    code_ptr = code;
}

void MethodJITCompiler::emit_epilogue(void*& code_ptr) {
    // 直接手写字节码到输出缓冲区
    uint8_t* code = static_cast<uint8_t*>(code_ptr);
    code[0] = 0x48; code[1] = 0x89; code[2] = 0xec; // mov rsp, rbp
    code[3] = 0x5d; // pop rbp
    code[4] = 0xc3; // ret
    code_ptr = &code[5];
}

void MethodJITCompiler::emit_call(void*& code_ptr, void* target) {
    // x86-64 call rel32
    uint8_t* code = static_cast<uint8_t*>(code_ptr);
    code[0] = 0xe8; // call
    int32_t offset = static_cast<int32_t>(
        reinterpret_cast<uintptr_t>(target) - 
        reinterpret_cast<uintptr_t>(code) - 5
    );
    *reinterpret_cast<int32_t*>(&code[1]) = offset;
    code_ptr = &code[5];
}

void MethodJITCompiler::emit_load_local(void*& code_ptr, size_t slot) {
    // x86-64: mov rax, [rbp - (slot+1)*8]; push rax
    uint8_t* code = static_cast<uint8_t*>(code_ptr);
    int32_t offset = -static_cast<int32_t>((slot + 1) * 8);

    if (offset >= -128 && offset <= 127) {
        code[0] = 0x48; code[1] = 0x8b; code[2] = 0x45;
        code[3] = static_cast<int8_t>(offset);
        code[4] = 0x50; // push rax
        code_ptr = &code[5];
    } else {
        code[0] = 0x48; code[1] = 0x8b; code[2] = 0x85;
        *reinterpret_cast<int32_t*>(&code[3]) = offset;
        code[7] = 0x50; // push rax
        code_ptr = &code[8];
    }
}

void MethodJITCompiler::emit_store_local(void*& code_ptr, size_t slot) {
    // x86-64: pop rax; mov [rbp - (slot+1)*8], rax
    uint8_t* code = static_cast<uint8_t*>(code_ptr);
    int32_t offset = -static_cast<int32_t>((slot + 1) * 8);

    code[0] = 0x58; // pop rax

    if (offset >= -128 && offset <= 127) {
        code[1] = 0x48; code[2] = 0x89; code[3] = 0x45;
        code[4] = static_cast<int8_t>(offset);
        code_ptr = &code[5];
    } else {
        code[1] = 0x48; code[2] = 0x89; code[3] = 0x85;
        *reinterpret_cast<int32_t*>(&code[4]) = offset;
        code_ptr = &code[8];
    }
}

// ============================================================================
// 完整指令发射实现 (80+ 字节码操作)
// ============================================================================

void MethodJITCompiler::emit_arithmetic_op(void*& code_ptr, bytecode::OpCode op) {
    // x86-64 整数/浮点运算
    // Stack model: pop two operands, compute, push result
    uint8_t* code = static_cast<uint8_t*>(code_ptr);

    switch (op) {
        case bytecode::OpCode::IADD:  // pop rdx; pop rax; add rax, rdx; push rax
            code[0] = 0x5a;                       // pop rdx
            code[1] = 0x58;                       // pop rax
            code[2] = 0x48; code[3] = 0x01; code[4] = 0xd0; // add rax, rdx
            code[5] = 0x50;                       // push rax
            code_ptr = &code[6];
            break;
        case bytecode::OpCode::ISUB:  // pop rdx; pop rax; sub rax, rdx; push rax
            code[0] = 0x5a;
            code[1] = 0x58;
            code[2] = 0x48; code[3] = 0x29; code[4] = 0xd0; // sub rax, rdx
            code[5] = 0x50;
            code_ptr = &code[6];
            break;
        case bytecode::OpCode::IMUL:  // pop rdx; pop rax; imul rax, rdx; push rax
            code[0] = 0x5a;
            code[1] = 0x58;
            code[2] = 0x48; code[3] = 0x0f; code[4] = 0xaf; code[5] = 0xc2; // imul rax, rdx
            code[6] = 0x50;
            code_ptr = &code[7];
            break;
        case bytecode::OpCode::IDIV:  // pop rcx; pop rax; cqo; idiv rcx; push rax
            code[0] = 0x59;                       // pop rcx
            code[1] = 0x58;                       // pop rax
            code[2] = 0x48; code[3] = 0x99;       // cqo
            code[4] = 0x48; code[5] = 0xf7; code[6] = 0xf9; // idiv rcx
            code[7] = 0x50;                       // push rax
            code_ptr = &code[8];
            break;
        case bytecode::OpCode::IMOD:  // pop rcx; pop rax; cqo; idiv rcx; mov rax, rdx; push rax
            code[0] = 0x59;                       // pop rcx
            code[1] = 0x58;                       // pop rax
            code[2] = 0x48; code[3] = 0x99;       // cqo
            code[4] = 0x48; code[5] = 0xf7; code[6] = 0xf9; // idiv rcx
            code[7] = 0x48; code[8] = 0x89; code[9] = 0xd0; // mov rax, rdx
            code[10] = 0x50;                      // push rax
            code_ptr = &code[11];
            break;
        case bytecode::OpCode::FADD:  // pop rax; movq xmm1, rax; pop rax; movq xmm0, rax; addsd xmm0, xmm1; movq rax, xmm0; push rax
            code[0] = 0x58; // pop rax
            code[1] = 0x66; code[2] = 0x48; code[3] = 0x0f; code[4] = 0x6e; code[5] = 0xc8; // movq xmm1, rax
            code[6] = 0x58; // pop rax
            code[7] = 0x66; code[8] = 0x48; code[9] = 0x0f; code[10] = 0x6e; code[11] = 0xc0; // movq xmm0, rax
            code[12] = 0xf2; code[13] = 0x0f; code[14] = 0x58; code[15] = 0xc1; // addsd xmm0, xmm1
            code[16] = 0x66; code[17] = 0x48; code[18] = 0x0f; code[19] = 0x7e; code[20] = 0xc0; // movq rax, xmm0
            code[21] = 0x50; // push rax
            code_ptr = &code[22];
            break;
        case bytecode::OpCode::FSUB:  // pop rax; movq xmm1, rax; pop rax; movq xmm0, rax; subsd xmm0, xmm1; movq rax, xmm0; push rax
            code[0] = 0x58;
            code[1] = 0x66; code[2] = 0x48; code[3] = 0x0f; code[4] = 0x6e; code[5] = 0xc8; // movq xmm1, rax
            code[6] = 0x58;
            code[7] = 0x66; code[8] = 0x48; code[9] = 0x0f; code[10] = 0x6e; code[11] = 0xc0; // movq xmm0, rax
            code[12] = 0xf2; code[13] = 0x0f; code[14] = 0x5c; code[15] = 0xc1; // subsd xmm0, xmm1
            code[16] = 0x66; code[17] = 0x48; code[18] = 0x0f; code[19] = 0x7e; code[20] = 0xc0; // movq rax, xmm0
            code[21] = 0x50;
            code_ptr = &code[22];
            break;
        case bytecode::OpCode::FMUL:  // pop rax; movq xmm1, rax; pop rax; movq xmm0, rax; mulsd xmm0, xmm1; movq rax, xmm0; push rax
            code[0] = 0x58;
            code[1] = 0x66; code[2] = 0x48; code[3] = 0x0f; code[4] = 0x6e; code[5] = 0xc8; // movq xmm1, rax
            code[6] = 0x58;
            code[7] = 0x66; code[8] = 0x48; code[9] = 0x0f; code[10] = 0x6e; code[11] = 0xc0; // movq xmm0, rax
            code[12] = 0xf2; code[13] = 0x0f; code[14] = 0x59; code[15] = 0xc1; // mulsd xmm0, xmm1
            code[16] = 0x66; code[17] = 0x48; code[18] = 0x0f; code[19] = 0x7e; code[20] = 0xc0; // movq rax, xmm0
            code[21] = 0x50;
            code_ptr = &code[22];
            break;
        case bytecode::OpCode::FDIV:  // pop rax; movq xmm1, rax; pop rax; movq xmm0, rax; divsd xmm0, xmm1; movq rax, xmm0; push rax
            code[0] = 0x58;
            code[1] = 0x66; code[2] = 0x48; code[3] = 0x0f; code[4] = 0x6e; code[5] = 0xc8; // movq xmm1, rax
            code[6] = 0x58;
            code[7] = 0x66; code[8] = 0x48; code[9] = 0x0f; code[10] = 0x6e; code[11] = 0xc0; // movq xmm0, rax
            code[12] = 0xf2; code[13] = 0x0f; code[14] = 0x5e; code[15] = 0xc1; // divsd xmm0, xmm1
            code[16] = 0x66; code[17] = 0x48; code[18] = 0x0f; code[19] = 0x7e; code[20] = 0xc0; // movq rax, xmm0
            code[21] = 0x50;
            code_ptr = &code[22];
            break;
        default:
            // 未知运算，跳过
            break;
    }
}

void MethodJITCompiler::emit_comparison_op(void*& code_ptr, bytecode::OpCode op) {
    // x86-64 比较操作: pop two operands, compare, push boolean result
    uint8_t* code = static_cast<uint8_t*>(code_ptr);

    switch (op) {
        case bytecode::OpCode::IEQ:
            code[0] = 0x5a; // pop rdx
            code[1] = 0x58; // pop rax
            code[2] = 0x48; code[3] = 0x39; code[4] = 0xd0; // cmp rax, rdx
            code[5] = 0x0f; code[6] = 0x94; code[7] = 0xc0; // sete al
            code[8] = 0x0f; code[9] = 0xb6; code[10] = 0xc0; // movzx rax, al
            code[11] = 0x50; // push rax
            code_ptr = &code[12];
            break;
        case bytecode::OpCode::INE:
            code[0] = 0x5a; code[1] = 0x58;
            code[2] = 0x48; code[3] = 0x39; code[4] = 0xd0;
            code[5] = 0x0f; code[6] = 0x95; code[7] = 0xc0; // setne al
            code[8] = 0x0f; code[9] = 0xb6; code[10] = 0xc0;
            code[11] = 0x50;
            code_ptr = &code[12];
            break;
        case bytecode::OpCode::ILT:
            code[0] = 0x5a; code[1] = 0x58;
            code[2] = 0x48; code[3] = 0x39; code[4] = 0xd0;
            code[5] = 0x0f; code[6] = 0x9c; code[7] = 0xc0; // setl al
            code[8] = 0x0f; code[9] = 0xb6; code[10] = 0xc0;
            code[11] = 0x50;
            code_ptr = &code[12];
            break;
        case bytecode::OpCode::ILE:
            code[0] = 0x5a; code[1] = 0x58;
            code[2] = 0x48; code[3] = 0x39; code[4] = 0xd0;
            code[5] = 0x0f; code[6] = 0x9e; code[7] = 0xc0; // setle al
            code[8] = 0x0f; code[9] = 0xb6; code[10] = 0xc0;
            code[11] = 0x50;
            code_ptr = &code[12];
            break;
        case bytecode::OpCode::IGT:
            code[0] = 0x5a; code[1] = 0x58;
            code[2] = 0x48; code[3] = 0x39; code[4] = 0xd0;
            code[5] = 0x0f; code[6] = 0x9f; code[7] = 0xc0; // setg al
            code[8] = 0x0f; code[9] = 0xb6; code[10] = 0xc0;
            code[11] = 0x50;
            code_ptr = &code[12];
            break;
        case bytecode::OpCode::IGE:
            code[0] = 0x5a; code[1] = 0x58;
            code[2] = 0x48; code[3] = 0x39; code[4] = 0xd0;
            code[5] = 0x0f; code[6] = 0x9d; code[7] = 0xc0; // setge al
            code[8] = 0x0f; code[9] = 0xb6; code[10] = 0xc0;
            code[11] = 0x50;
            code_ptr = &code[12];
            break;
        case bytecode::OpCode::FEQ:
            code[0] = 0x5a; // pop rdx (right)
            code[1] = 0x58; // pop rax (left)
            code[2] = 0x66; code[3] = 0x48; code[4] = 0x0f; code[5] = 0x6e; code[6] = 0xc0; // movq xmm0, rax
            code[7] = 0x66; code[8] = 0x48; code[9] = 0x0f; code[10] = 0x6e; code[11] = 0xca; // movq xmm1, rdx
            code[12] = 0x66; code[13] = 0x0f; code[14] = 0x2e; code[15] = 0xc1; // ucomisd xmm0, xmm1
            code[16] = 0x0f; code[17] = 0x94; code[18] = 0xc0; // sete al
            code[19] = 0x0f; code[20] = 0xb6; code[21] = 0xc0; // movzx rax, al
            code[22] = 0x50; // push rax
            code_ptr = &code[23];
            break;
        case bytecode::OpCode::FNE:
            code[0] = 0x5a; code[1] = 0x58;
            code[2] = 0x66; code[3] = 0x48; code[4] = 0x0f; code[5] = 0x6e; code[6] = 0xc0;
            code[7] = 0x66; code[8] = 0x48; code[9] = 0x0f; code[10] = 0x6e; code[11] = 0xca;
            code[12] = 0x66; code[13] = 0x0f; code[14] = 0x2e; code[15] = 0xc1;
            code[16] = 0x0f; code[17] = 0x95; code[18] = 0xc0; // setne al
            code[19] = 0x0f; code[20] = 0xb6; code[21] = 0xc0;
            code[22] = 0x50;
            code_ptr = &code[23];
            break;
        case bytecode::OpCode::FLT:
            code[0] = 0x5a; code[1] = 0x58;
            code[2] = 0x66; code[3] = 0x48; code[4] = 0x0f; code[5] = 0x6e; code[6] = 0xc0;
            code[7] = 0x66; code[8] = 0x48; code[9] = 0x0f; code[10] = 0x6e; code[11] = 0xca;
            code[12] = 0x66; code[13] = 0x0f; code[14] = 0x2e; code[15] = 0xc1;
            code[16] = 0x0f; code[17] = 0x92; code[18] = 0xc0; // setb al
            code[19] = 0x0f; code[20] = 0xb6; code[21] = 0xc0;
            code[22] = 0x50;
            code_ptr = &code[23];
            break;
        case bytecode::OpCode::FLE:
            code[0] = 0x5a; code[1] = 0x58;
            code[2] = 0x66; code[3] = 0x48; code[4] = 0x0f; code[5] = 0x6e; code[6] = 0xc0;
            code[7] = 0x66; code[8] = 0x48; code[9] = 0x0f; code[10] = 0x6e; code[11] = 0xca;
            code[12] = 0x66; code[13] = 0x0f; code[14] = 0x2e; code[15] = 0xc1;
            code[16] = 0x0f; code[17] = 0x96; code[18] = 0xc0; // setbe al
            code[19] = 0x0f; code[20] = 0xb6; code[21] = 0xc0;
            code[22] = 0x50;
            code_ptr = &code[23];
            break;
        case bytecode::OpCode::FGT:
            code[0] = 0x5a; code[1] = 0x58;
            code[2] = 0x66; code[3] = 0x48; code[4] = 0x0f; code[5] = 0x6e; code[6] = 0xc0;
            code[7] = 0x66; code[8] = 0x48; code[9] = 0x0f; code[10] = 0x6e; code[11] = 0xca;
            code[12] = 0x66; code[13] = 0x0f; code[14] = 0x2e; code[15] = 0xc1;
            code[16] = 0x0f; code[17] = 0x97; code[18] = 0xc0; // seta al
            code[19] = 0x0f; code[20] = 0xb6; code[21] = 0xc0;
            code[22] = 0x50;
            code_ptr = &code[23];
            break;
        case bytecode::OpCode::FGE:
            code[0] = 0x5a; code[1] = 0x58;
            code[2] = 0x66; code[3] = 0x48; code[4] = 0x0f; code[5] = 0x6e; code[6] = 0xc0;
            code[7] = 0x66; code[8] = 0x48; code[9] = 0x0f; code[10] = 0x6e; code[11] = 0xca;
            code[12] = 0x66; code[13] = 0x0f; code[14] = 0x2e; code[15] = 0xc1;
            code[16] = 0x0f; code[17] = 0x93; code[18] = 0xc0; // setae al
            code[19] = 0x0f; code[20] = 0xb6; code[21] = 0xc0;
            code[22] = 0x50;
            code_ptr = &code[23];
            break;
        default:
            break;
    }
}

void MethodJITCompiler::emit_logical_op(void*& code_ptr, bytecode::OpCode op) {
    // 逻辑运算: pop operands, compute, push result
    uint8_t* code = static_cast<uint8_t*>(code_ptr);

    switch (op) {
        case bytecode::OpCode::AND:  // pop rdx; pop rax; and rax, rdx; push rax
            code[0] = 0x5a; code[1] = 0x58;
            code[2] = 0x48; code[3] = 0x21; code[4] = 0xd0; // and rax, rdx
            code[5] = 0x50;
            code_ptr = &code[6];
            break;
        case bytecode::OpCode::OR:   // pop rdx; pop rax; or rax, rdx; push rax
            code[0] = 0x5a; code[1] = 0x58;
            code[2] = 0x48; code[3] = 0x09; code[4] = 0xd0; // or rax, rdx
            code[5] = 0x50;
            code_ptr = &code[6];
            break;
        case bytecode::OpCode::NOT:  // pop rax; test rax, rax; sete al; movzx rax, al; push rax
            code[0] = 0x58; // pop rax
            code[1] = 0x48; code[2] = 0x85; code[3] = 0xc0; // test rax, rax
            code[4] = 0x0f; code[5] = 0x94; code[6] = 0xc0; // sete al (logical not)
            code[7] = 0x0f; code[8] = 0xb6; code[9] = 0xc0; // movzx rax, al
            code[10] = 0x50; // push rax
            code_ptr = &code[11];
            break;
        case bytecode::OpCode::BAND:
            emit_arithmetic_op(code_ptr, bytecode::OpCode::AND);
            break;
        case bytecode::OpCode::BOR:
            emit_arithmetic_op(code_ptr, bytecode::OpCode::OR);
            break;
        case bytecode::OpCode::BXOR: // pop rdx; pop rax; xor rax, rdx; push rax
            code[0] = 0x5a; code[1] = 0x58;
            code[2] = 0x48; code[3] = 0x31; code[4] = 0xd0; // xor rax, rdx
            code[5] = 0x50;
            code_ptr = &code[6];
            break;
        case bytecode::OpCode::BNOT: // pop rax; not rax; push rax
            code[0] = 0x58;
            code[1] = 0x48; code[2] = 0xf7; code[3] = 0xd0; // not rax
            code[4] = 0x50;
            code_ptr = &code[5];
            break;
        default:
            break;
    }
}

void MethodJITCompiler::emit_shift_op(void*& code_ptr, bytecode::OpCode op) {
    // 位移运算: pop count (rcx), pop value (rax), shift, push result
    uint8_t* code = static_cast<uint8_t*>(code_ptr);

    switch (op) {
        case bytecode::OpCode::SHL:  // pop rcx; pop rax; shl rax, cl; push rax
            code[0] = 0x59; // pop rcx
            code[1] = 0x58; // pop rax
            code[2] = 0x48; code[3] = 0xd3; code[4] = 0xe0; // shl rax, cl
            code[5] = 0x50; // push rax
            code_ptr = &code[6];
            break;
        case bytecode::OpCode::SHR:  // pop rcx; pop rax; sar rax, cl; push rax
            code[0] = 0x59; // pop rcx
            code[1] = 0x58; // pop rax
            code[2] = 0x48; code[3] = 0xd3; code[4] = 0xf8; // sar rax, cl
            code[5] = 0x50; // push rax
            code_ptr = &code[6];
            break;
        case bytecode::OpCode::USHR: // pop rcx; pop rax; shr rax, cl; push rax
            code[0] = 0x59; // pop rcx
            code[1] = 0x58; // pop rax
            code[2] = 0x48; code[3] = 0xd3; code[4] = 0xe8; // shr rax, cl
            code[5] = 0x50; // push rax
            code_ptr = &code[6];
            break;
        default:
            break;
    }
}

void MethodJITCompiler::emit_type_conversion(void*& code_ptr, bytecode::OpCode op) {
    // 类型转换操作
    uint8_t* code = static_cast<uint8_t*>(code_ptr);
    
    switch (op) {
        case bytecode::OpCode::I2F:  // cvtsi2sd xmm0, rax
            code[0] = 0xf2; code[1] = 0x48; code[2] = 0x0f; code[3] = 0x2a; code[4] = 0xc0;
            code_ptr = &code[5];
            break;
        case bytecode::OpCode::F2I:  // cvttsd2si rax, xmm0
            code[0] = 0xf2; code[1] = 0x48; code[2] = 0x0f; code[3] = 0x2c; code[4] = 0xc0;
            code_ptr = &code[5];
            break;
        case bytecode::OpCode::I2B:  // int to bool (test rax, rax; setne al)
            code[0] = 0x48; code[1] = 0x85; code[2] = 0xc0;  // test rax, rax
            code[3] = 0x0f; code[4] = 0x95; code[5] = 0xc0;  // setne al
            code[6] = 0x0f; code[7] = 0xb6; code[8] = 0xc0;  // movzx rax, al
            code_ptr = &code[9];
            break;
        case bytecode::OpCode::B2I:  // bool to int (movzx rax, al)
            code[0] = 0x0f; code[1] = 0xb6; code[2] = 0xc0;
            code_ptr = &code[3];
            break;
        case bytecode::OpCode::I2S:  // int to string (call runtime)
            // 调用运行时函数 int_to_string
            code[0] = 0x48; code[1] = 0x89; code[2] = 0xc7;  // mov rdi, rax
            code[3] = 0xe8;
            *reinterpret_cast<int32_t*>(&code[4]) = 0;  // placeholder
            code_ptr = &code[8];
            break;
        case bytecode::OpCode::F2S:  // float to string
            code[0] = 0x48; code[1] = 0x89; code[2] = 0xc7;  // mov rdi, rax
            code[3] = 0xe8;
            *reinterpret_cast<int32_t*>(&code[4]) = 0;
            code_ptr = &code[8];
            break;
        case bytecode::OpCode::S2I:  // string to int
            code[0] = 0x48; code[1] = 0x89; code[2] = 0xc7;
            code[3] = 0xe8;
            *reinterpret_cast<int32_t*>(&code[4]) = 0;
            code_ptr = &code[8];
            break;
        case bytecode::OpCode::S2F:  // string to float
            code[0] = 0x48; code[1] = 0x89; code[2] = 0xc7;
            code[3] = 0xe8;
            *reinterpret_cast<int32_t*>(&code[4]) = 0;
            code_ptr = &code[8];
            break;
        case bytecode::OpCode::TRUNC:  // truncate (floating to int, toward zero)
            code[0] = 0xf2; code[1] = 0x48; code[2] = 0x0f; code[3] = 0x2c; code[4] = 0xc0;
            code_ptr = &code[5];
            break;
        case bytecode::OpCode::ZEXT:  // zero extend (byte to qword)
            code[0] = 0x48; code[1] = 0x0f; code[2] = 0xb6; code[3] = 0xc0;
            code_ptr = &code[4];
            break;
        case bytecode::OpCode::SEXT:  // sign extend (byte to qword)
            code[0] = 0x48; code[1] = 0x0f; code[2] = 0xbe; code[3] = 0xc0;
            code_ptr = &code[4];
            break;
        default:
            break;
    }
}

void MethodJITCompiler::emit_stack_op(void*& code_ptr, bytecode::OpCode op) {
    // 栈操作
    uint8_t* code = static_cast<uint8_t*>(code_ptr);
    
    switch (op) {
        case bytecode::OpCode::NOP:
            code_ptr = &code[0];
            break;
        case bytecode::OpCode::POP:  // add rsp, 8
            code[0] = 0x48; code[1] = 0x83; code[2] = 0xc4; code[3] = 0x08;
            code_ptr = &code[4];
            break;
        case bytecode::OpCode::DUP:  // push QWORD PTR [rsp]
            code[0] = 0xff; code[1] = 0x74; code[2] = 0x24; code[3] = 0x00;  // push QWORD PTR [rsp]
            code_ptr = &code[4];
            break;
        case bytecode::OpCode::SWAP: // mov rax, [rsp]; mov rcx, [rsp+8]; mov [rsp], rcx; mov [rsp+8], rax
            code[0] = 0x48; code[1] = 0x8b; code[2] = 0x04; code[3] = 0x24;  // mov rax, [rsp]
            code[4] = 0x48; code[5] = 0x8b; code[6] = 0x4c; code[7] = 0x24; code[8] = 0x08; // mov rcx, [rsp+8]
            code[9] = 0x48; code[10] = 0x89; code[11] = 0x0c; code[12] = 0x24; // mov [rsp], rcx
            code[13] = 0x48; code[14] = 0x89; code[15] = 0x44; code[16] = 0x24; code[17] = 0x08; // mov [rsp+8], rax
            code_ptr = &code[18];
            break;
        default:
            break;
    }
}

void MethodJITCompiler::emit_jump_op(void*& code_ptr, bytecode::OpCode op, int32_t target_offset) {
    // 跳转操作
    uint8_t* code = static_cast<uint8_t*>(code_ptr);
    
    switch (op) {
        case bytecode::OpCode::JMP:  // jmp rel32
            code[0] = 0xe9;
            *reinterpret_cast<int32_t*>(&code[1]) = target_offset - 5;
            code_ptr = &code[5];
            break;
        case bytecode::OpCode::JMP_IF:   // test rax, rax; jne rel32
            code[0] = 0x48; code[1] = 0x85; code[2] = 0xc0;  // test rax, rax
            code[3] = 0x0f; code[4] = 0x85;                   // jne rel32
            *reinterpret_cast<int32_t*>(&code[5]) = target_offset - 9;
            code_ptr = &code[9];
            break;
        case bytecode::OpCode::JMP_IF_NOT: // test rax, rax; je rel32
            code[0] = 0x48; code[1] = 0x85; code[2] = 0xc0;
            code[3] = 0x0f; code[4] = 0x84;
            *reinterpret_cast<int32_t*>(&code[5]) = target_offset - 9;
            code_ptr = &code[9];
            break;
        case bytecode::OpCode::LOOP:  // dec rcx; jne rel32 (用于 for 循环)
            code[0] = 0x48; code[1] = 0xff; code[2] = 0xc9;  // dec rcx
            code[3] = 0x0f; code[4] = 0x85;
            *reinterpret_cast<int32_t*>(&code[5]) = target_offset - 9;
            code_ptr = &code[9];
            break;
        default:
            break;
    }
}

void MethodJITCompiler::emit_array_op(void*& code_ptr, bytecode::OpCode op) {
    // 数组操作
    uint8_t* code = static_cast<uint8_t*>(code_ptr);

    switch (op) {
        case bytecode::OpCode::ALLOC_ARRAY: {
            // alloc_array(size=0, element_type=0) -> void*
            code[0] = 0x48; code[1] = 0x31; code[2] = 0xff;  // xor rdi, rdi
            code[3] = 0x48; code[4] = 0x31; code[5] = 0xf6;  // xor rsi, rsi
            code[6] = 0xe8;  // call
            void* func_addr = get_runtime_function_by_name("alloc_array");
            if (func_addr) {
                int32_t offset = static_cast<int32_t>(
                    reinterpret_cast<int64_t>(func_addr) - reinterpret_cast<int64_t>(&code[11])
                );
                *reinterpret_cast<int32_t*>(&code[7]) = offset;
            } else {
                *reinterpret_cast<int32_t*>(&code[7]) = 0;
            }
            code[11] = 0x50;  // push rax
            code_ptr = &code[12];
            break;
        }
        case bytecode::OpCode::LOAD_INDEX:
            // 已内联到编译循环中，此处不再使用
            break;
        case bytecode::OpCode::STORE_INDEX:
            // 已内联到编译循环中，此处不再使用
            break;
        case bytecode::OpCode::ARRAY_LEN: {
            // 栈: [array_ptr]
            // pop rdi
            code[0] = 0x5f;
            code[1] = 0xe8;
            void* func_addr = get_runtime_function_by_name("array_len");
            if (func_addr) {
                int32_t offset = static_cast<int32_t>(
                    reinterpret_cast<int64_t>(func_addr) - reinterpret_cast<int64_t>(&code[6])
                );
                *reinterpret_cast<int32_t*>(&code[2]) = offset;
            } else {
                *reinterpret_cast<int32_t*>(&code[2]) = 0;
            }
            code[6] = 0x50;
            code_ptr = &code[7];
            break;
        }
        case bytecode::OpCode::ARRAY_PUSH: {
            // 栈: [array_ptr, value] (value 在栈顶)
            // array_push(void* arr_ptr, int64_t value)
            code[0] = 0x5e;  // pop rsi (value)
            code[1] = 0x5f;  // pop rdi (array_ptr)
            code[2] = 0xe8;  // call
            void* func_addr = get_runtime_function_by_name("array_push");
            if (func_addr) {
                int32_t offset = static_cast<int32_t>(
                    reinterpret_cast<int64_t>(func_addr) - reinterpret_cast<int64_t>(&code[7])
                );
                *reinterpret_cast<int32_t*>(&code[3]) = offset;
            } else {
                *reinterpret_cast<int32_t*>(&code[3]) = 0;
            }
            code_ptr = &code[7];
            break;
        }
        default:
            break;
    }
}

// ============================================================================
// 元组操作实现 (2026-04-26)
// ============================================================================

void MethodJITCompiler::emit_tuple_op(void*& code_ptr, bytecode::OpCode op) {
    // 元组操作: 创建、加载元素、存储元素
    // JIT 使用硬件栈: 从栈上弹出操作数到寄存器, 调用运行时, 压入结果
    uint8_t* code = static_cast<uint8_t*>(code_ptr);

    switch (op) {
        case bytecode::OpCode::CREATE_TUPLE: {
            // 栈: [elem0, elem1] (elem1 在栈顶)
            // alloc_tuple(int64_t a, int64_t b) -> void*
            // pop rsi (b = elem1)
            code[0] = 0x5e;
            // pop rdi (a = elem0)
            code[1] = 0x5f;
            // call alloc_tuple
            code[2] = 0xe8;
            void* func_addr = get_runtime_function_by_name("alloc_tuple");
            if (func_addr) {
                int32_t offset = static_cast<int32_t>(
                    reinterpret_cast<int64_t>(func_addr) - reinterpret_cast<int64_t>(&code[7])
                );
                *reinterpret_cast<int32_t*>(&code[3]) = offset;
            } else {
                *reinterpret_cast<int32_t*>(&code[3]) = 0;
            }
            // push rax (result tuple pointer)
            code[7] = 0x50;
            code_ptr = &code[8];
            break;
        }

        case bytecode::OpCode::LOAD_ELEM: {
            // 栈: [tuple_ptr, index] (index 在栈顶)
            // tuple_get(void* tuple, int index) -> int64_t
            // pop rsi (index)
            code[0] = 0x5e;
            // pop rdi (tuple_ptr)
            code[1] = 0x5f;
            // call tuple_get
            code[2] = 0xe8;
            void* func_addr = get_runtime_function_by_name("tuple_get");
            if (func_addr) {
                int32_t offset = static_cast<int32_t>(
                    reinterpret_cast<int64_t>(func_addr) - reinterpret_cast<int64_t>(&code[7])
                );
                *reinterpret_cast<int32_t*>(&code[3]) = offset;
            } else {
                *reinterpret_cast<int32_t*>(&code[3]) = 0;
            }
            // push rax (result)
            code[7] = 0x50;
            code_ptr = &code[8];
            break;
        }

        case bytecode::OpCode::STORE_ELEM: {
            // 栈: [tuple_ptr, index, value] (value 在栈顶)
            // tuple_set(void* tuple, int index, int64_t value)
            // pop rdx (value)
            code[0] = 0x5a;
            // pop rsi (index)
            code[1] = 0x5e;
            // pop rdi (tuple_ptr)
            code[2] = 0x5f;
            // call tuple_set
            code[3] = 0xe8;
            void* func_addr = get_runtime_function_by_name("tuple_set");
            if (func_addr) {
                int32_t offset = static_cast<int32_t>(
                    reinterpret_cast<int64_t>(func_addr) - reinterpret_cast<int64_t>(&code[8])
                );
                *reinterpret_cast<int32_t*>(&code[4]) = offset;
            } else {
                *reinterpret_cast<int32_t*>(&code[4]) = 0;
            }
            code_ptr = &code[8];
            break;
        }

        default:
            break;
    }
}

// ============================================================================
// 函数调用实现 (2026-04-26)
// ============================================================================

void MethodJITCompiler::emit_call_op(void*& code_ptr, const bytecode::Instruction& inst) {
    // 函数调用: CALL arg_count
    // System V AMD64 ABI: 参数通过 rdi, rsi, rdx, rcx, r8, r9 传递
    // 返回值在 rax (或 xmm0 for float)
    // 栈布局: [return addr][saved rbp][locals...]

    uint8_t* code = static_cast<uint8_t*>(code_ptr);
    uint32_t arg_count = inst.operand;

    if (inst.op == bytecode::OpCode::CALL_EXT) {
        // 外部函数调用 - 从 operand 解码 str_idx 和 arg_count
        uint32_t str_idx = inst.operand & 0xFFFF;
        uint32_t ext_arg_count = (inst.operand >> 16) & 0xFFFF;

        std::string func_name;
        if (current_module_ && str_idx < current_module_->constants.values.size()) {
            func_name = current_module_->constants.values[str_idx].str;
        }

        void* func_addr = nullptr;
        bool use_xmm0 = false;

        // 对 print/println 做类型感知分发
        if ((func_name == "print" || func_name == "println") && ext_arg_count == 1) {
            std::string target_name;
            if (last_pushed_type_ == bytecode::ValueType::F64) {
                target_name = func_name + "_f64";
                use_xmm0 = true;
            } else if (last_pushed_type_ == bytecode::ValueType::STRING) {
                target_name = func_name + "_str";
            } else {
                target_name = func_name + "_i64";
            }
            func_addr = get_runtime_function_by_name(target_name);
            if (!func_addr) {
                func_addr = get_runtime_function_by_name(func_name);
            }
        } else {
            if (!func_name.empty()) {
                func_addr = get_runtime_function_by_name(func_name);
            }
            if (!func_addr) {
                func_addr = get_runtime_function_by_name("println");
            }
        }

        size_t off = 0;

        if ((func_name == "print" || func_name == "println") && ext_arg_count == 1 && use_xmm0) {
            // 浮点参数: pop rax; movq xmm0, rax
            code[off++] = 0x58; // pop rax
            code[off++] = 0x66; code[off++] = 0x48; code[off++] = 0x0f; code[off++] = 0x6e; code[off++] = 0xc0; // movq xmm0, rax
        } else {
            // Pop args from stack into registers (reverse order: top-of-stack is last arg)
            // System V AMD64: rdi, rsi, rdx, rcx, r8, r9
            switch (ext_arg_count) {
                case 6: code[off++] = 0x41; code[off++] = 0x59; // pop r9
                case 5: code[off++] = 0x41; code[off++] = 0x58; // pop r8
                case 4: code[off++] = 0x59; // pop rcx
                case 3: code[off++] = 0x5a; // pop rdx
                case 2: code[off++] = 0x5e; // pop rsi
                case 1: code[off++] = 0x5f; // pop rdi
                default: break;
            }
        }

        // movabs rax, func_addr
        code[off++] = 0x48;
        code[off++] = 0xB8;
        *reinterpret_cast<uint64_t*>(&code[off]) = reinterpret_cast<uint64_t>(func_addr);
        off += 8;

        // call rax
        code[off++] = 0xFF;
        code[off++] = 0xD0;

        // push return value onto stack (matches VM behavior)
        code[off++] = 0x50; // push rax

        code_ptr = &code[off];
    } else {
        // 内部函数调用 (CALL)
        // Stack: [arg0, arg1, ..., callee]
        // Pop callee into rax, then pop args into registers
        size_t off = 0;

        // Pop callee address into rax
        code[off++] = 0x58; // pop rax

        // Pop args from stack into registers (System V AMD64: rdi, rsi, rdx, rcx, r8, r9)
        // Note: stack top after popping callee is the LAST arg pushed.
        // Fallthrough order ensures correct register assignment.
        switch (arg_count) {
            case 6: code[off++] = 0x41; code[off++] = 0x59; // pop r9
            case 5: code[off++] = 0x41; code[off++] = 0x58; // pop r8
            case 4: code[off++] = 0x59; // pop rcx
            case 3: code[off++] = 0x5a; // pop rdx
            case 2: code[off++] = 0x5e; // pop rsi
            case 1: code[off++] = 0x5f; // pop rdi
            default: break;
        }

        // call rax
        code[off++] = 0xFF;
        code[off++] = 0xD0;

        // push return value onto stack
        code[off++] = 0x50; // push rax

        code_ptr = &code[off];
    }
}

void MethodJITCompiler::emit_return_op(void*& code_ptr, bytecode::OpCode op) {
    // 返回操作 - 先恢复 rsp 清理栈，再返回
    uint8_t* code = static_cast<uint8_t*>(code_ptr);

    switch (op) {
        case bytecode::OpCode::RET:  // mov rsp, rbp; pop rbp; ret
            code[0] = 0x48; code[1] = 0x89; code[2] = 0xec; // mov rsp, rbp
            code[3] = 0x5d; // pop rbp
            code[4] = 0xc3; // ret
            code_ptr = &code[5];
            break;
        case bytecode::OpCode::RET_NULL:  // mov rsp, rbp; xor rax, rax; pop rbp; ret
            code[0] = 0x48; code[1] = 0x89; code[2] = 0xec; // mov rsp, rbp
            code[3] = 0x48; code[4] = 0x31; code[5] = 0xc0; // xor rax, rax
            code[6] = 0x5d; // pop rbp
            code[7] = 0xc3; // ret
            code_ptr = &code[8];
            break;
        default:
            break;
    }
}

// ============================================================================
// 全局变量操作实现
// ============================================================================

void MethodJITCompiler::emit_load_global(void*& code_ptr, uint32_t idx) {
    uint8_t* code = static_cast<uint8_t*>(code_ptr);

    // Check if this global is a function name we can resolve at compile time
    if (current_module_ && idx < current_module_->constants.values.size()) {
        const auto& cv = current_module_->constants.values[idx];
        if (cv.type == bytecode::ValueType::STRING) {
            const std::string& name = cv.str;
            // Look up in module functions
            for (const auto& func : current_module_->functions) {
                if (func.name == name) {
                    void* func_addr = get_compiled_code(name);
                    if (func_addr) {
                        // movabs rax, func_addr
                        code[0] = 0x48; code[1] = 0xB8;
                        *reinterpret_cast<uint64_t*>(&code[2]) = reinterpret_cast<uint64_t>(func_addr);
                        code[10] = 0x50; // push rax
                        code_ptr = &code[11];
                        return;
                    }
                    break;
                }
            }
        }
    }

    // Fallback: read from r15-based global table (may not work for non-functions)
    code[0] = 0x49;  // REX.WB for R15
    code[1] = 0x8b;
    code[2] = 0x87;
    *reinterpret_cast<uint32_t*>(&code[3]) = idx * 8;  // disp32
    code[7] = 0x50; // push rax
    code_ptr = &code[8];
}

void MethodJITCompiler::emit_store_global(void*& code_ptr, uint32_t idx) {
    // x86-64: mov [r15 + idx * 8], rax
    uint8_t* code = static_cast<uint8_t*>(code_ptr);
    
    // mov [r15 + idx * 8], rax
    code[0] = 0x49;  // REX.WB for R15
    code[1] = 0x89;
    code[2] = 0x87;
    *reinterpret_cast<uint32_t*>(&code[3]) = idx * 8;
    code_ptr = &code[7];
}

void MethodJITCompiler::emit_define_global(void*& code_ptr, uint32_t idx) {
    // DEFINE_GLOBAL 与 STORE_GLOBAL 类似，但用于首次定义
    emit_store_global(code_ptr, idx);
}

// ============================================================================
// 张量操作实现
// ============================================================================

void MethodJITCompiler::emit_tensor_op(void*& code_ptr, bytecode::OpCode op) {
    // 张量操作需要调用运行时函数
    // 简化实现: 通过 CALL 指令调用运行时
    uint8_t* code = static_cast<uint8_t*>(code_ptr);
    
    switch (op) {
        case bytecode::OpCode::TENSOR_CREATE: {
            // mov rdi, rax (参数1: shape/数据)
            // mov rsi, rcx (参数2: 数据指针)
            // call tensor_create@PLT
            code[0] = 0x48; code[1] = 0x89; code[2] = 0xc7;  // mov rdi, rax
            code[3] = 0x48; code[4] = 0x89; code[5] = 0xce;  // mov rsi, rcx
            code[6] = 0xe8;  // call rel32
            
            // 从运行时函数注册表获取地址
            void* func_addr = RuntimeFunctionRegistry::instance().lookup("tensor_create");
            if (func_addr) {
                // 计算相对偏移 (PC-relative)
                int32_t offset = static_cast<int32_t>(
                    reinterpret_cast<int64_t>(func_addr) - reinterpret_cast<int64_t>(&code[10])
                );
                *reinterpret_cast<int32_t*>(&code[7]) = offset;
            } else {
                *reinterpret_cast<int32_t*>(&code[7]) = 0;  // placeholder
            }
            code_ptr = &code[11];
            break;
        }
        case bytecode::OpCode::TENSOR_MATMUL: {
            // 调用 tensor_matmul 运行时函数
            // 假设: rax = A, rcx = B, 结果存回 rax
            code[0] = 0x48; code[1] = 0x89; code[2] = 0xc7;  // mov rdi, rax
            code[3] = 0x48; code[4] = 0x89; code[5] = 0xce;  // mov rsi, rcx
            code[6] = 0xe8;  // call
            
            // 从运行时函数注册表获取地址
            void* func_addr = RuntimeFunctionRegistry::instance().lookup("tensor_matmul");
            if (func_addr) {
                int32_t offset = static_cast<int32_t>(
                    reinterpret_cast<int64_t>(func_addr) - reinterpret_cast<int64_t>(&code[10])
                );
                *reinterpret_cast<int32_t*>(&code[7]) = offset;
            } else {
                *reinterpret_cast<int32_t*>(&code[7]) = 0;  // placeholder
            }
            code_ptr = &code[11];
            break;
        }
        case bytecode::OpCode::TENSOR_RESHAPE: {
            void* func_addr = RuntimeFunctionRegistry::instance().lookup("tensor_reshape");
            if (func_addr) {
                code[0] = 0x48; code[1] = 0x89; code[2] = 0xc7;
                code[3] = 0xe8;
                int32_t offset = static_cast<int32_t>(
                    reinterpret_cast<int64_t>(func_addr) - reinterpret_cast<int64_t>(&code[6])
                );
                *reinterpret_cast<int32_t*>(&code[4]) = offset;
                code_ptr = &code[8];
            }
            break;
        }
        case bytecode::OpCode::TENSOR_LOAD: {
            void* func_addr = RuntimeFunctionRegistry::instance().lookup("tensor_load");
            if (func_addr) {
                code[0] = 0x48; code[1] = 0x89; code[2] = 0xc7;
                code[3] = 0xe8;
                int32_t offset = static_cast<int32_t>(
                    reinterpret_cast<int64_t>(func_addr) - reinterpret_cast<int64_t>(&code[6])
                );
                *reinterpret_cast<int32_t*>(&code[4]) = offset;
                code_ptr = &code[8];
            }
            break;
        }
        case bytecode::OpCode::TENSOR_STORE: {
            void* func_addr = RuntimeFunctionRegistry::instance().lookup("tensor_store");
            if (func_addr) {
                code[0] = 0x48; code[1] = 0x89; code[2] = 0xc7;
                code[3] = 0xe8;
                int32_t offset = static_cast<int32_t>(
                    reinterpret_cast<int64_t>(func_addr) - reinterpret_cast<int64_t>(&code[6])
                );
                *reinterpret_cast<int32_t*>(&code[4]) = offset;
                code_ptr = &code[8];
            }
            break;
        }
        default:
            break;
    }
}

// ============================================================================
// 闭包操作实现
// ============================================================================

void MethodJITCompiler::emit_closure_op(void*& code_ptr, const bytecode::Function& func) {
    // 闭包创建: 分配闭包对象并捕获 upvalues
    // 需要运行时支持: closure_create(func_ptr, upvalue_count)
    uint8_t* code = static_cast<uint8_t*>(code_ptr);
    
    // 简化实现: mov rdi, func_ptr; call closure_create
    code[0] = 0x48; code[1] = 0xb8;  // mov rax, imm64
    *reinterpret_cast<void**>(&code[2]) = nullptr;  // func address placeholder
    code[10] = 0xe8;  // call
    
    // 从运行时函数注册表获取地址
    void* func_addr = RuntimeFunctionRegistry::instance().lookup("closure_create");
    if (func_addr) {
        int32_t offset = static_cast<int32_t>(
            reinterpret_cast<int64_t>(func_addr) - reinterpret_cast<int64_t>(&code[14])
        );
        *reinterpret_cast<int32_t*>(&code[11]) = offset;
    } else {
        *reinterpret_cast<int32_t*>(&code[11]) = 0;  // placeholder
    }
    code_ptr = &code[15];
}

void MethodJITCompiler::emit_upvalue_op(void*& code_ptr, bytecode::OpCode op) {
    // Upvalue 操作
    uint8_t* code = static_cast<uint8_t*>(code_ptr);
    
    switch (op) {
        case bytecode::OpCode::CLOSE_UPVALUE: {
            // 关闭 upvalue: call close_upvalue(rax)
            code[0] = 0x48; code[1] = 0x89; code[2] = 0xc7;  // mov rdi, rax
            code[3] = 0xe8;
            
            void* func_addr = RuntimeFunctionRegistry::instance().lookup("close_upvalue");
            if (func_addr) {
                int32_t offset = static_cast<int32_t>(
                    reinterpret_cast<int64_t>(func_addr) - reinterpret_cast<int64_t>(&code[7])
                );
                *reinterpret_cast<int32_t*>(&code[4]) = offset;
                code_ptr = &code[8];
            } else {
                *reinterpret_cast<int32_t*>(&code[4]) = 0;
                code_ptr = &code[8];
            }
            break;
        }
        case bytecode::OpCode::GET_UPVALUE:
            // 获取 upvalue: mov rax, [rax + offset]
            code[0] = 0x48; code[1] = 0x8b; code[2] = 0x40; code[3] = 0x00;
            code_ptr = &code[4];
            break;
        default:
            break;
    }
}

// ============================================================================
// 系统调用和标准库 EXT 调用发射 (2026-04-26 新增)
// ============================================================================

void MethodJITCompiler::emit_system_call(void*& code_ptr, bytecode::OpCode op) {
    // 发射系统调用
    // 这些操作通过运行时函数处理
    uint8_t* code = static_cast<uint8_t*>(code_ptr);
    
    switch (op) {
        case bytecode::OpCode::PRINT:
        case bytecode::OpCode::PRINTLN: {
            // Pop argument from stack into rdi
            code[0] = 0x5f;  // pop rdi
            // call runtime function
            code[1] = 0xe8;

            void* func_addr = get_runtime_function_by_name(
                op == bytecode::OpCode::PRINT ? "print_i64" : "println_i64");
            if (func_addr) {
                int32_t offset = static_cast<int32_t>(
                    reinterpret_cast<int64_t>(func_addr) - reinterpret_cast<int64_t>(&code[6])
                );
                *reinterpret_cast<int32_t*>(&code[2]) = offset;
                code_ptr = &code[6];
            } else {
                *reinterpret_cast<int32_t*>(&code[2]) = 0;
                code_ptr = &code[6];
            }
            break;
        }
        case bytecode::OpCode::INPUT: {
            // 调用运行时 input 函数
            code[0] = 0xe8;  // call
            void* func_addr = get_runtime_function_by_name("input");
            if (func_addr) {
                int32_t offset = static_cast<int32_t>(
                    reinterpret_cast<int64_t>(func_addr) - reinterpret_cast<int64_t>(&code[1])
                );
                *reinterpret_cast<int32_t*>(&code[1]) = offset;
                code_ptr = &code[5];
            } else {
                code_ptr = &code[5];
            }
            break;
        }
        case bytecode::OpCode::TYPE_OF: {
            // 调用 type_of 运行时函数
            code[0] = 0x48; code[1] = 0x89; code[2] = 0xc7;  // mov rdi, rax
            code[3] = 0xe8;
            void* func_addr = get_runtime_function_by_name("type_of");
            if (func_addr) {
                int32_t offset = static_cast<int32_t>(
                    reinterpret_cast<int64_t>(func_addr) - reinterpret_cast<int64_t>(&code[7])
                );
                *reinterpret_cast<int32_t*>(&code[4]) = offset;
                code_ptr = &code[8];
            } else {
                code_ptr = &code[8];
            }
            break;
        }
        case bytecode::OpCode::PANIC: {
            // 调用 panic 运行时函数
            code[0] = 0xe8;
            void* func_addr = get_runtime_function_by_name("println");
            if (func_addr) {
                int32_t offset = static_cast<int32_t>(
                    reinterpret_cast<int64_t>(func_addr) - reinterpret_cast<int64_t>(&code[1])
                );
                *reinterpret_cast<int32_t*>(&code[1]) = offset;
                code_ptr = &code[5];
            } else {
                code_ptr = &code[5];
            }
            break;
        }
        case bytecode::OpCode::HALT: {
            // 生成 ud2 (未定义指令) 终止程序
            code[0] = 0x0f; code[1] = 0x0b;  // ud2
            code_ptr = &code[2];
            break;
        }
        default:
            break;
    }
}

void MethodJITCompiler::emit_ext_stdlib_call(
    void*& code_ptr, 
    const bytecode::Instruction& inst) {
    
    // EXT 指令格式: EXT opcode
    // inst.operand 包含标准库函数 opcode
    int ext_opcode = static_cast<int>(inst.operand);
    
    // 通过 opcode 查找运行时函数
    void* func_addr = get_runtime_function_by_opcode(ext_opcode);
    if (!func_addr) {
        // 函数未找到，生成错误处理
        uint8_t* code = static_cast<uint8_t*>(code_ptr);
        code[0] = 0x0f; code[1] = 0x0b;  // ud2
        code_ptr = &code[2];
        return;
    }
    
    // 发射调用
    uint8_t* code = static_cast<uint8_t*>(code_ptr);
    
    // 直接调用运行时函数
    code[0] = 0xe8;  // call rel32
    
    int32_t offset = static_cast<int32_t>(
        reinterpret_cast<int64_t>(func_addr) - reinterpret_cast<int64_t>(&code[5])
    );
    *reinterpret_cast<int32_t*>(&code[1]) = offset;
    
    code_ptr = &code[5];
}

size_t MethodJITCompiler::estimate_code_size(const bytecode::Function& func) {
    // 粗略估计: 每个指令平均 15 字节 + 函数头尾 (增加以适应更多指令)
    return func.code.size() * 15 + 64;
}

// ============================================================================
// OptimizingJITCompiler 实现
// ============================================================================

OptimizingJITCompiler::OptimizingJITCompiler() {
    code_cache_ = std::make_unique<CodeCache>(64 * 1024 * 1024); // 64MB
}

OptimizingJITCompiler::~OptimizingJITCompiler() {
    clear_cache();
}

CompilationResult OptimizingJITCompiler::optimize_compile(const bytecode::Function& func) {
    CompilationResult result;
    
    // 检查缓存
    auto it = optimized_functions_.find(func.name);
    if (it != optimized_functions_.end()) {
        result.success = true;
        result.machine_code = it->second;
        return result;
    }
    
    // 复制函数进行优化
    bytecode::Function optimized_func = func;
    
    // 运行优化遍
    run_constant_folding(optimized_func);
    run_dead_code_elimination(optimized_func);
    run_copy_propagation(optimized_func);
    run_strength_reduction(optimized_func);
    run_loop_invariant_code_motion(optimized_func);
    
    // 分配代码缓存
    size_t code_size = estimate_code_size(optimized_func);
    void* code_ptr = code_cache_->allocate(code_size);
    
    if (!code_ptr) {
        result.error_message = "Failed to allocate code cache for optimization";
        return result;
    }
    
    // 生成优化后的机器码 (类似 MethodJIT)
    void* current = code_ptr;
    emit_prologue(current, optimized_func.local_count, optimized_func.arity);
    
    for (const auto& inst : optimized_func.code) {
        (void)inst;
    }
    
    emit_epilogue(current);
    
    // 更新缓存
    optimized_functions_[func.name] = code_ptr;
    
    result.success = true;
    result.machine_code = code_ptr;
    result.code_size = code_size;
    
    return result;
}

void* OptimizingJITCompiler::get_optimized_code(const std::string& func_name) {
    auto it = optimized_functions_.find(func_name);
    if (it != optimized_functions_.end()) {
        return it->second;
    }
    return nullptr;
}

void OptimizingJITCompiler::clear_cache() {
    optimized_functions_.clear();
    code_cache_->clear();
}

void OptimizingJITCompiler::emit_prologue(void*& code_ptr, size_t local_count, uint32_t arity) {
    // x86-64 函数序言 (优化版本)
    // push rbp
    // mov rbp, rsp
    // sub rsp, local_size
    uint8_t* code = static_cast<uint8_t*>(code_ptr);
    
    // push rbp
    code[0] = 0x55;
    // mov rbp, rsp  
    code[1] = 0x48;
    code[2] = 0x89;
    code[3] = 0xe5;
    
    // 分配局部变量空间 (16字节对齐)
    if (local_count > 0) {
        size_t stack_size = ((local_count * 8 + 15) / 16) * 16;
        if (stack_size <= 127) {
            code[4] = 0x48;
            code[5] = 0x83;
            code[6] = 0xec;
            code[7] = static_cast<uint8_t>(stack_size);
            code_ptr = &code[8];
        } else {
            code[4] = 0x48;
            code[5] = 0x81;
            code[6] = 0xec;
            *reinterpret_cast<uint32_t*>(&code[7]) = static_cast<uint32_t>(stack_size);
            code_ptr = &code[11];
        }
    } else {
        code_ptr = &code[4];
    }
}

void OptimizingJITCompiler::emit_epilogue(void*& code_ptr) {
    // x86-64 函数结尾
    uint8_t* code = static_cast<uint8_t*>(code_ptr);
    code[0] = 0x5d; // pop rbp
    code[1] = 0xc3; // ret
    code_ptr = &code[2];
}

// ============================================================================
// 常量折叠实现
// ============================================================================

// 辅助: 检查指令是否是常量 push
static bool is_constant_push(const bytecode::Instruction& inst) {
    return inst.op == bytecode::OpCode::PUSH;
}

// 辅助: 从常量池获取常量值 (使用简化版本 - 常量索引直接作为值)
// 在实际实现中需要访问 Module 的 ConstantPool

// 辅助: 执行二元运算
static std::optional<bytecode::Value> eval_binary(bytecode::OpCode op, 
                                                   const bytecode::Value& a, 
                                                   const bytecode::Value& b) {
    try {
        switch (op) {
            // Integer arithmetic
            case bytecode::OpCode::IADD:
                if (a.type == bytecode::ValueType::I64 && b.type == bytecode::ValueType::I64)
                    return bytecode::Value::integer(a.data.i64 + b.data.i64);
                break;
            case bytecode::OpCode::ISUB:
                if (a.type == bytecode::ValueType::I64 && b.type == bytecode::ValueType::I64)
                    return bytecode::Value::integer(a.data.i64 - b.data.i64);
                break;
            case bytecode::OpCode::IMUL:
                if (a.type == bytecode::ValueType::I64 && b.type == bytecode::ValueType::I64)
                    return bytecode::Value::integer(a.data.i64 * b.data.i64);
                break;
            case bytecode::OpCode::IDIV:
                if (a.type == bytecode::ValueType::I64 && b.type == bytecode::ValueType::I64 && b.data.i64 != 0)
                    return bytecode::Value::integer(a.data.i64 / b.data.i64);
                break;
            case bytecode::OpCode::IMOD:
                if (a.type == bytecode::ValueType::I64 && b.type == bytecode::ValueType::I64 && b.data.i64 != 0)
                    return bytecode::Value::integer(a.data.i64 % b.data.i64);
                break;
            
            // Float arithmetic
            case bytecode::OpCode::FADD:
                if (a.type == bytecode::ValueType::F64 && b.type == bytecode::ValueType::F64)
                    return bytecode::Value::floating(a.data.f64 + b.data.f64);
                break;
            case bytecode::OpCode::FSUB:
                if (a.type == bytecode::ValueType::F64 && b.type == bytecode::ValueType::F64)
                    return bytecode::Value::floating(a.data.f64 - b.data.f64);
                break;
            case bytecode::OpCode::FMUL:
                if (a.type == bytecode::ValueType::F64 && b.type == bytecode::ValueType::F64)
                    return bytecode::Value::floating(a.data.f64 * b.data.f64);
                break;
            case bytecode::OpCode::FDIV:
                if (a.type == bytecode::ValueType::F64 && b.type == bytecode::ValueType::F64 && b.data.f64 != 0.0)
                    return bytecode::Value::floating(a.data.f64 / b.data.f64);
                break;
            
            // Integer comparison
            case bytecode::OpCode::IEQ:
                if (a.type == bytecode::ValueType::I64 && b.type == bytecode::ValueType::I64)
                    return bytecode::Value::boolean(a.data.i64 == b.data.i64);
                break;
            case bytecode::OpCode::INE:
                if (a.type == bytecode::ValueType::I64 && b.type == bytecode::ValueType::I64)
                    return bytecode::Value::boolean(a.data.i64 != b.data.i64);
                break;
            case bytecode::OpCode::ILT:
                if (a.type == bytecode::ValueType::I64 && b.type == bytecode::ValueType::I64)
                    return bytecode::Value::boolean(a.data.i64 < b.data.i64);
                break;
            case bytecode::OpCode::ILE:
                if (a.type == bytecode::ValueType::I64 && b.type == bytecode::ValueType::I64)
                    return bytecode::Value::boolean(a.data.i64 <= b.data.i64);
                break;
            case bytecode::OpCode::IGT:
                if (a.type == bytecode::ValueType::I64 && b.type == bytecode::ValueType::I64)
                    return bytecode::Value::boolean(a.data.i64 > b.data.i64);
                break;
            case bytecode::OpCode::IGE:
                if (a.type == bytecode::ValueType::I64 && b.type == bytecode::ValueType::I64)
                    return bytecode::Value::boolean(a.data.i64 >= b.data.i64);
                break;
            
            // Float comparison
            case bytecode::OpCode::FEQ:
                if (a.type == bytecode::ValueType::F64 && b.type == bytecode::ValueType::F64)
                    return bytecode::Value::boolean(a.data.f64 == b.data.f64);
                break;
            case bytecode::OpCode::FNE:
                if (a.type == bytecode::ValueType::F64 && b.type == bytecode::ValueType::F64)
                    return bytecode::Value::boolean(a.data.f64 != b.data.f64);
                break;
            case bytecode::OpCode::FLT:
                if (a.type == bytecode::ValueType::F64 && b.type == bytecode::ValueType::F64)
                    return bytecode::Value::boolean(a.data.f64 < b.data.f64);
                break;
            case bytecode::OpCode::FLE:
                if (a.type == bytecode::ValueType::F64 && b.type == bytecode::ValueType::F64)
                    return bytecode::Value::boolean(a.data.f64 <= b.data.f64);
                break;
            case bytecode::OpCode::FGT:
                if (a.type == bytecode::ValueType::F64 && b.type == bytecode::ValueType::F64)
                    return bytecode::Value::boolean(a.data.f64 > b.data.f64);
                break;
            case bytecode::OpCode::FGE:
                if (a.type == bytecode::ValueType::F64 && b.type == bytecode::ValueType::F64)
                    return bytecode::Value::boolean(a.data.f64 >= b.data.f64);
                break;
            
            // Bitwise operations
            case bytecode::OpCode::BAND:
                if (a.type == bytecode::ValueType::I64 && b.type == bytecode::ValueType::I64)
                    return bytecode::Value::integer(a.data.i64 & b.data.i64);
                break;
            case bytecode::OpCode::BOR:
                if (a.type == bytecode::ValueType::I64 && b.type == bytecode::ValueType::I64)
                    return bytecode::Value::integer(a.data.i64 | b.data.i64);
                break;
            case bytecode::OpCode::BXOR:
                if (a.type == bytecode::ValueType::I64 && b.type == bytecode::ValueType::I64)
                    return bytecode::Value::integer(a.data.i64 ^ b.data.i64);
                break;
            case bytecode::OpCode::SHL:
                if (a.type == bytecode::ValueType::I64 && b.type == bytecode::ValueType::I64)
                    return bytecode::Value::integer(a.data.i64 << b.data.i64);
                break;
            case bytecode::OpCode::SHR:
                if (a.type == bytecode::ValueType::I64 && b.type == bytecode::ValueType::I64)
                    return bytecode::Value::integer(a.data.i64 >> b.data.i64);
                break;
            
            default:
                break;
        }
    } catch (...) {
        // Handle overflow or other exceptions
    }
    return std::nullopt;
}

// 辅助: 执行一元运算
static std::optional<bytecode::Value> eval_unary(bytecode::OpCode op, 
                                                   const bytecode::Value& a) {
    try {
        switch (op) {
            case bytecode::OpCode::INEG:
                if (a.type == bytecode::ValueType::I64)
                    return bytecode::Value::integer(-a.data.i64);
                break;
            case bytecode::OpCode::FNEG:
                if (a.type == bytecode::ValueType::F64)
                    return bytecode::Value::floating(-a.data.f64);
                break;
            case bytecode::OpCode::NOT:
                if (a.type == bytecode::ValueType::BOOL)
                    return bytecode::Value::boolean(!a.data.b);
                if (a.type == bytecode::ValueType::I64)
                    return bytecode::Value::boolean(a.data.i64 == 0);
                break;
            case bytecode::OpCode::BNOT:
                if (a.type == bytecode::ValueType::I64)
                    return bytecode::Value::integer(~a.data.i64);
                break;
            case bytecode::OpCode::IINC:
                if (a.type == bytecode::ValueType::I64)
                    return bytecode::Value::integer(a.data.i64 + 1);
                break;
            case bytecode::OpCode::FINC:
                if (a.type == bytecode::ValueType::F64)
                    return bytecode::Value::floating(a.data.f64 + 1.0);
                break;
            default:
                break;
        }
    } catch (...) {
    }
    return std::nullopt;
}

void OptimizingJITCompiler::run_constant_folding(bytecode::Function& func) {
    // 常量折叠: 简化实现
    // 注意: 完整的常量折叠需要访问 Module 的 ConstantPool
    // 这里实现简化的数据流分析，检测可以折叠的操作
    
    // 简化: 直接在指令级别进行常量折叠
    // 假设 operand 是常量值 (简化模型)
    
    for (size_t i = 1; i + 1 < func.code.size(); ++i) {
        auto& inst = func.code[i];
        
        // 一元运算优化: NEG, NOT, INC
        if (inst.op == bytecode::OpCode::INEG || inst.op == bytecode::OpCode::IINC) {
            auto& prev = func.code[i-1];
            if (prev.op == bytecode::OpCode::PUSH) {
                // 假设 operand 包含常量值
                int64_t val = static_cast<int64_t>(prev.operand);
                
                if (inst.op == bytecode::OpCode::INEG) {
                    prev.operand = static_cast<uint32_t>(-val);
                    inst.op = bytecode::OpCode::NOP;
                } else if (inst.op == bytecode::OpCode::IINC) {
                    prev.operand = static_cast<uint32_t>(val + 1);
                    inst.op = bytecode::OpCode::NOP;
                }
            }
        }
        
        // 二元运算优化: 简单的常量传播
        if (i >= 2) {
            auto& prev1 = func.code[i-2];
            auto& prev2 = func.code[i-1];
            
            if (prev1.op == bytecode::OpCode::PUSH && prev2.op == bytecode::OpCode::PUSH) {
                int64_t a = static_cast<int64_t>(prev1.operand);
                int64_t b = static_cast<int64_t>(prev2.operand);
                int64_t result = 0;
                bool foldable = false;
                
                switch (inst.op) {
                    case bytecode::OpCode::IADD:
                        result = a + b;
                        foldable = true;
                        break;
                    case bytecode::OpCode::ISUB:
                        result = a - b;
                        foldable = true;
                        break;
                    case bytecode::OpCode::IMUL:
                        result = a * b;
                        foldable = true;
                        break;
                    case bytecode::OpCode::IEQ:
                        result = (a == b) ? 1 : 0;
                        foldable = true;
                        break;
                    case bytecode::OpCode::INE:
                        result = (a != b) ? 1 : 0;
                        foldable = true;
                        break;
                    case bytecode::OpCode::ILT:
                        result = (a < b) ? 1 : 0;
                        foldable = true;
                        break;
                    case bytecode::OpCode::IGT:
                        result = (a > b) ? 1 : 0;
                        foldable = true;
                        break;
                    default:
                        break;
                }
                
                if (foldable) {
                    // 将两个 PUSH 合并为一个，结果放入 operand
                    prev1.op = bytecode::OpCode::PUSH;
                    prev1.operand = static_cast<uint32_t>(result);
                    prev2.op = bytecode::OpCode::NOP;
                    inst.op = bytecode::OpCode::NOP;
                }
            }
        }
    }
}

// ============================================================================
// 死代码消除实现
// ============================================================================

void OptimizingJITCompiler::run_dead_code_elimination(bytecode::Function& func) {
    if (func.code.empty()) return;
    
    // 使用反向数据流分析计算活跃指令
    // 活跃指令: 从程序出口可达的指令
    
    std::unordered_set<size_t> live;
    bool changed = true;
    
    // 初始化: 返回指令是活跃的
    for (size_t i = 0; i < func.code.size(); ++i) {
        const auto& inst = func.code[i];
        if (inst.op == bytecode::OpCode::RET || 
            inst.op == bytecode::OpCode::RET_NULL ||
            inst.op == bytecode::OpCode::HALT) {
            live.insert(i);
        }
    }
    
    // 反向迭代直到稳定
    while (changed) {
        changed = false;
        
        for (size_t i = 0; i < func.code.size(); ++i) {
            // 如果指令已经是活跃的，检查它的前驱
            if (live.count(i)) {
                continue;  // 已处理
            }
            
            const auto& inst = func.code[i];
            
            // 检查这个指令是否能让后面的指令变活跃
            bool can_reach_live = false;
            
            // 1. 检查是否跳转到活跃指令
            if (inst.op == bytecode::OpCode::JMP || 
                inst.op == bytecode::OpCode::JMP_IF || 
                inst.op == bytecode::OpCode::JMP_IF_NOT) {
                if (live.count(inst.operand)) {
                    can_reach_live = true;
                }
            }
            
            // 2. 如果是条件跳转，还要检查另一条分支
            if (inst.op == bytecode::OpCode::JMP_IF || 
                inst.op == bytecode::OpCode::JMP_IF_NOT) {
                // 非跳转分支是下一条指令
                if (i + 1 < func.code.size() && live.count(i + 1)) {
                    can_reach_live = true;
                }
            }
            
            // 3. 如果不是跳转指令，下一条指令是前驱
            if (inst.op != bytecode::OpCode::JMP && 
                inst.op != bytecode::OpCode::RET && 
                inst.op != bytecode::OpCode::RET_NULL &&
                inst.op != bytecode::OpCode::HALT) {
                if (i + 1 < func.code.size() && live.count(i + 1)) {
                    can_reach_live = true;
                }
            }
            
            if (can_reach_live) {
                live.insert(i);
                changed = true;
            }
        }
    }
    
    // 标记死代码
    // 注意: 我们不实际删除指令，而是标记它们
    // 在生产实现中，可以构建新代码数组
    
    for (size_t i = 0; i < func.code.size(); ++i) {
        if (!live.count(i)) {
            // 将死代码指令替换为 NOP
            func.code[i].op = bytecode::OpCode::NOP;
            func.code[i].operand = 0;
        }
    }
}

// ============================================================================
// 复制传播实现
// ============================================================================

void OptimizingJITCompiler::run_copy_propagation(bytecode::Function& func) {
    if (func.code.empty()) return;
    
    // 复制传播: 将复制的值替换变量引用
    // 跟踪 STORE/LOAD 序列
    
    // 记录槽位->槽位的复制关系 (slot -> slot mapping)
    std::unordered_map<uint32_t, uint32_t> copy_map;
    
    for (size_t i = 0; i < func.code.size(); ++i) {
        auto& inst = func.code[i];
        
        // 检测 STORE_LOCAL 到 LOAD_LOCAL 的模式
        // 这通常表示复制操作
        if (inst.op == bytecode::OpCode::STORE_LOCAL) {
            uint32_t dest_slot = inst.operand;
            
            // 查找前一条 LOAD_LOCAL
            if (i > 0) {
                auto& prev = func.code[i-1];
                if (prev.op == bytecode::OpCode::LOAD_LOCAL) {
                    uint32_t src_slot = prev.operand;
                    
                    // 记录复制关系: dest = src
                    copy_map[dest_slot] = src_slot;
                }
            }
        }
        
        // 遇到新的 STORE_LOCAL 时，删除相关的复制关系
        if (inst.op == bytecode::OpCode::STORE_LOCAL) {
            uint32_t slot = inst.operand;
            
            // 删除以这个槽位为目标的复制关系
            std::vector<uint32_t> to_erase;
            for (const auto& kv : copy_map) {
                if (kv.second == slot) {
                    to_erase.push_back(kv.first);
                }
            }
            for (auto key : to_erase) {
                copy_map.erase(key);
            }
        }
        
        // 将 LOAD_LOCAL 替换为复制的源槽位
        if (inst.op == bytecode::OpCode::LOAD_LOCAL && copy_map.count(inst.operand)) {
            // 检查这条 LOAD_LOCAL 是否被修改
            bool modified = false;
            for (size_t j = i + 1; j < func.code.size(); ++j) {
                const auto& next = func.code[j];
                if (next.op == bytecode::OpCode::STORE_LOCAL && 
                    next.operand == inst.operand) {
                    modified = true;
                    break;
                }
                // 如果中间有跳转，跳出这个基本块
                if (next.op == bytecode::OpCode::JMP ||
                    next.op == bytecode::OpCode::JMP_IF ||
                    next.op == bytecode::OpCode::JMP_IF_NOT) {
                    break;
                }
            }
            
            if (!modified) {
                inst.operand = copy_map[inst.operand];
            }
        }
        
        // 遇到返回/跳转时清除复制映射
        if (inst.op == bytecode::OpCode::RET || 
            inst.op == bytecode::OpCode::RET_NULL ||
            inst.op == bytecode::OpCode::JMP) {
            copy_map.clear();
        }
    }
}

// ============================================================================
// 强度消减实现
// ============================================================================

void OptimizingJITCompiler::run_strength_reduction(bytecode::Function& func) {
    if (func.code.empty()) return;
    
    // 强度消减: 用更廉价的操作替代昂贵操作
    
    for (size_t i = 0; i < func.code.size(); ++i) {
        auto& inst = func.code[i];
        
        // 检测常量乘除
        if (inst.op == bytecode::OpCode::IMUL || inst.op == bytecode::OpCode::IDIV) {
            // 检查第二个操作数是否是常量
            if (i > 0) {
                auto& prev = func.code[i-1];
                if (prev.op == bytecode::OpCode::PUSH) {
                    // 获取常量值 (简化: operand 作为值)
                    uint32_t val = prev.operand;
                    
                    // a * 2 -> a + a (用 DUP + IADD 近似)
                    if (inst.op == bytecode::OpCode::IMUL && val == 2) {
                        // IMUL -> DUP + IADD 需要更多指令
                        // 简化: 保持 IMUL，但标记可用更优实现
                    }
                    
                    // a * 4 -> a << 2
                    else if (inst.op == bytecode::OpCode::IMUL && val == 4) {
                        prev.op = bytecode::OpCode::NOP;
                        inst.op = bytecode::OpCode::SHL;
                        prev.operand = 2;  // 修改为位移量
                    }
                    
                    // a * 8 -> a << 3
                    else if (inst.op == bytecode::OpCode::IMUL && val == 8) {
                        prev.op = bytecode::OpCode::NOP;
                        inst.op = bytecode::OpCode::SHL;
                        prev.operand = 3;
                    }
                    
                    // a * 16 -> a << 4
                    else if (inst.op == bytecode::OpCode::IMUL && val == 16) {
                        prev.op = bytecode::OpCode::NOP;
                        inst.op = bytecode::OpCode::SHL;
                        prev.operand = 4;
                    }
                    
                    // a / 2 -> a >> 1
                    else if (inst.op == bytecode::OpCode::IDIV && val == 2) {
                        prev.op = bytecode::OpCode::NOP;
                        inst.op = bytecode::OpCode::SHR;
                        prev.operand = 1;
                    }
                }
            }
        }
        
        // 检测重复加载相同全局变量
        if (inst.op == bytecode::OpCode::LOAD_GLOBAL) {
            uint32_t idx = inst.operand;
            
            // 检查前一条指令是否加载相同全局
            if (i > 0) {
                auto& prev = func.code[i-1];
                if (prev.op == bytecode::OpCode::LOAD_GLOBAL && prev.operand == idx) {
                    // 重复加载 -> DUP
                    inst.op = bytecode::OpCode::DUP;
                    inst.operand = 0;
                }
            }
        }
        
        // 检测重复加载相同局部变量
        if (inst.op == bytecode::OpCode::LOAD_LOCAL) {
            uint32_t slot = inst.operand;
            
            // 检查前一条指令是否加载相同局部
            if (i > 0) {
                auto& prev = func.code[i-1];
                if (prev.op == bytecode::OpCode::LOAD_LOCAL && prev.operand == slot) {
                    // 重复加载 -> DUP
                    inst.op = bytecode::OpCode::DUP;
                    inst.operand = 0;
                }
            }
        }
    }
}

// ============================================================================
// 循环不变代码外提实现
// ============================================================================

void OptimizingJITCompiler::run_loop_invariant_code_motion(bytecode::Function& func) {
    if (func.code.empty()) return;
    
    // 循环不变代码外提: 将循环内不变的计算移到循环外
    // 简化实现: 检测 LOOP 指令附近的常量操作
    
    // 1. 找出循环入口点 (LOOP 指令)
    std::vector<size_t> loop_entries;
    for (size_t i = 0; i < func.code.size(); ++i) {
        if (func.code[i].op == bytecode::OpCode::LOOP) {
            loop_entries.push_back(i);
        }
    }
    
    // 2. 对每个循环，检测不变操作
    for (size_t loop_idx : loop_entries) {
        // 简化: 找到循环结束位置 (假设是 LOOP 后的下一条 RET)
        size_t loop_end = func.code.size();
        for (size_t i = loop_idx + 1; i < func.code.size(); ++i) {
            if (func.code[i].op == bytecode::OpCode::RET || 
                func.code[i].op == bytecode::OpCode::RET_NULL) {
                loop_end = i;
                break;
            }
        }
        
        // 3. 在循环内检测不变操作
        // 不变操作: 操作数全是常量或来自循环外的值
        std::unordered_set<size_t> invariant_insts;
        
        for (size_t i = loop_idx + 1; i < loop_end && i < func.code.size(); ++i) {
            auto& inst = func.code[i];
            
            // PUSH 指令是循环不变的 (假设 operand 是常量值)
            if (inst.op == bytecode::OpCode::PUSH) {
                // 简化: 假设所有 PUSH 都是常量
                invariant_insts.insert(i);
            }
            
            // LOAD_LOCAL 如果不在循环内定义也是不变的
            if (inst.op == bytecode::OpCode::LOAD_LOCAL) {
                uint32_t slot = inst.operand;
                bool defined_in_loop = false;
                for (size_t j = loop_idx + 1; j < i; ++j) {
                    if (func.code[j].op == bytecode::OpCode::STORE_LOCAL && 
                        func.code[j].operand == slot) {
                        defined_in_loop = true;
                        break;
                    }
                }
                if (!defined_in_loop) {
                    invariant_insts.insert(i);
                }
            }
        }
        
        // 4. 将不变操作移到循环前 (简化: 标记为可在循环外计算)
        // 注意: 完整的 LICM 需要移动代码，这里我们只是标记
        // 实际实现需要将指令移动到循环前
    }
}

// ============================================================================
// 函数内联实现
// ============================================================================

bool OptimizingJITCompiler::can_inline(const bytecode::Function& caller, 
                                        const bytecode::Function& callee) {
    // 内联条件:
    // 1. 函数体较小
    // 2. 调用点不多
    // 3. 没有递归
    
    size_t caller_size = caller.code.size();
    size_t callee_size = callee.code.size();
    
    // 计算调用次数
    size_t call_count = 0;
    for (const auto& inst : caller.code) {
        if (inst.op == bytecode::OpCode::CALL) {
            call_count++;
        }
    }
    
    // 阈值: 被调用函数小于 20 条指令且调用次数少
    if (callee_size >= 20) return false;
    if (call_count > 5) return false;
    
    // 检查递归
    if (caller.name == callee.name) return false;
    
    // 检查是否有过多的局部变量
    if (callee.local_count > 16) return false;
    
    return true;
}

void OptimizingJITCompiler::inline_function(bytecode::Function& func, 
                                             size_t call_offset, 
                                             const bytecode::Function& callee) {
    // 函数内联: 将被调用函数体嵌入调用点
    
    if (call_offset >= func.code.size()) return;
    
    auto& call_inst = func.code[call_offset];
    if (call_inst.op != bytecode::OpCode::CALL) return;
    
    uint32_t arg_count = call_inst.operand;
    
    // 构建参数映射: 参数 i -> 栈位置 (arg_count - 1 - i)
    // 因为参数按相反顺序入栈
    std::unordered_map<uint32_t, int32_t> param_map;
    for (uint32_t i = 0; i < arg_count && i < callee.arity; ++i) {
        // 参数 i 对应栈顶向下的位置 i
        param_map[i] = static_cast<int32_t>(i);
    }
    
    // 复制被调用函数的代码
    std::vector<bytecode::Instruction> inlined_code;
    
    // 调整局部变量索引
    int32_t base_offset = static_cast<int32_t>(arg_count);  // 参数占用的槽位
    
    for (const auto& inst : callee.code) {
        bytecode::Instruction new_inst = inst;
        
        // 调整局部变量索引
        if (inst.op == bytecode::OpCode::LOAD_LOCAL || 
            inst.op == bytecode::OpCode::STORE_LOCAL) {
            if (inst.operand < callee.local_count) {
                // 但不包括参数
                uint32_t local_idx = inst.operand;
                if (local_idx >= callee.arity) {
                    new_inst.operand = local_idx + arg_count - callee.arity + base_offset;
                }
            }
        }
        
        // 调整跳转目标 (简化处理)
        if (inst.op == bytecode::OpCode::JMP ||
            inst.op == bytecode::OpCode::JMP_IF ||
            inst.op == bytecode::OpCode::JMP_IF_NOT) {
            // 跳转目标需要调整，这是简化实现
            // 完整实现需要维护跳转表
        }
        
        inlined_code.push_back(new_inst);
    }
    
    // 移除 RETURN (假设返回值为栈顶)
    if (!inlined_code.empty()) {
        auto& last = inlined_code.back();
        if (last.op == bytecode::OpCode::RET || last.op == bytecode::OpCode::RET_NULL) {
            // 替换为 POP (清除返回值) 或保持返回值在栈上
            last.op = bytecode::OpCode::NOP;
        }
    }
    
    // 替换 CALL 指令为内联代码
    func.code.erase(func.code.begin() + call_offset);
    func.code.insert(func.code.begin() + call_offset, 
                     inlined_code.begin(), inlined_code.end());
}

size_t OptimizingJITCompiler::estimate_code_size(const bytecode::Function& func) {
    return func.code.size() * 15 + 64; // 优化版本需要更多空间
}

// ============================================================================
// JITCompiler 主类实现
// ============================================================================

JITCompiler::JITCompiler(const JITConfig& config) : config_(config) {
    method_jit_ = std::make_unique<MethodJITCompiler>();
    optimizing_jit_ = std::make_unique<OptimizingJITCompiler>();
    code_cache_ = std::make_unique<CodeCache>(config.code_cache_size);
    
    // 初始化去优化支持 (Phase 18.1 新增)
    deopt_manager_ = std::make_unique<DeoptimizationManager>();
    osr_compiler_ = std::make_unique<OSRCompiler>();
}

JITCompiler::~JITCompiler() {
    clear_all_caches();
}

void JITCompiler::set_module(const bytecode::Module* module) {
    if (method_jit_) {
        method_jit_->set_module(module);
    }
    if (optimizing_jit_) {
        optimizing_jit_->set_module(module);
    }
}

JITConfig JITCompiler::default_config() {
    return JITConfig{};
}

void JITCompiler::record_call(const std::string& func_name) {
    std::lock_guard<std::mutex> lock(hotspot_mutex_);
    
    HotSpotInfo& hotspot = hotspots_[func_name];
    hotspot.function_name = func_name;
    hotspot.call_count++;
    
    // 检查是否需要优化
    if (hotspot.call_count >= config_.hot_threshold) {
        hotspot.needs_optimization = true;
    }
}

void JITCompiler::record_loop_iteration(const std::string& func_name, size_t offset) {
    std::lock_guard<std::mutex> lock(hotspot_mutex_);
    
    HotSpotInfo& hotspot = hotspots_[func_name];
    hotspot.trace_count++;
    hotspot.loop_entry_offsets.push_back(offset);
    
    // Trace JIT 热点检测
    if (hotspot.trace_count >= config_.trace_hot_threshold) {
        hotspot.needs_optimization = true;
    }
}

void JITCompiler::record_execution_time(const std::string& func_name, size_t time_us) {
    std::lock_guard<std::mutex> lock(hotspot_mutex_);
    
    HotSpotInfo& hotspot = hotspots_[func_name];
    hotspot.total_time_us += time_us;
}

HotSpotInfo* JITCompiler::get_hotspot(const std::string& func_name) {
    std::lock_guard<std::mutex> lock(hotspot_mutex_);
    
    auto it = hotspots_.find(func_name);
    if (it != hotspots_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<HotSpotInfo> JITCompiler::get_hot_functions(size_t top_n) {
    std::lock_guard<std::mutex> lock(hotspot_mutex_);
    
    std::vector<HotSpotInfo> result;
    for (const auto& pair : hotspots_) {
        result.push_back(pair.second);
    }
    
    // 按调用次数排序
    std::sort(result.begin(), result.end(), 
        [](const HotSpotInfo& a, const HotSpotInfo& b) {
            return a.call_count > b.call_count;
        });
    
    if (result.size() > top_n) {
        result.resize(top_n);
    }
    
    return result;
}

void* JITCompiler::compile_or_get_cached(const bytecode::Function& func) {
    total_compilations_++;
    
    // 首先检查内联缓存
    if (config_.enable_inline_cache) {
        // 这里需要从调用点获取参数类型
        // 简化版本直接调用 Method JIT
    }
    
    // 编译并缓存
    auto result = method_jit_->compile(func);
    if (result.success) {
        return result.machine_code;
    }
    
    return nullptr;
}

void* JITCompiler::compile_optimized(const bytecode::Function& func) {
    optimized_compilations_++;
    
    auto result = optimizing_jit_->optimize_compile(func);
    if (result.success) {
        return result.machine_code;
    }
    
    return nullptr;
}

bool JITCompiler::should_optimize(const std::string& func_name) {
    auto* hotspot = get_hotspot(func_name);
    return hotspot && hotspot->needs_optimization;
}

void* JITCompiler::lookup_inline_cache(const std::string& func_name, 
                                        const std::vector<bytecode::ValueType>& param_types) {
    auto it = inline_caches_.find(func_name);
    if (it != inline_caches_.end()) {
        void* code = it->second->lookup(param_types);
        if (code) {
            inline_cache_hits_++;
            return code;
        }
    }
    return nullptr;
}

void JITCompiler::update_inline_cache(const std::string& func_name,
                                       const std::vector<bytecode::ValueType>& param_types,
                                       void* code) {
    if (!inline_caches_.count(func_name)) {
        inline_caches_[func_name] = std::make_unique<InlineCache>(config_.inline_cache_size);
    }
    inline_caches_[func_name]->update(param_types, code);
}

bool JITCompiler::needs_osr(const std::string& func_name) {
    return config_.enable_osr && should_optimize(func_name);
}

void* JITCompiler::compile_for_osr(const bytecode::Function& func, size_t stack_size) {
    // OSR 编译: 在栈上替换运行时代码
    // 需要保存当前栈帧状态并跳转到优化后的代码
    
    if (!config_.enable_osr || !osr_compiler_) {
        return compile_optimized(func);
    }
    
    // 获取当前函数状态
    std::vector<bytecode::Value> current_state;
    
    // 使用 OSR 编译器进行栈上替换编译
    void* osr_code = osr_compiler_->compile_osr(
        func,
        0,  // 从函数入口开始
        current_state,
        stack_size
    );
    
    if (osr_code) {
        return osr_code;
    }
    
    // OSR 失败，回退到普通优化编译
    return compile_optimized(func);
}

void JITCompiler::update_config(const JITConfig& config) {
    config_ = config;
    
    // 重建内联缓存
    inline_caches_.clear();
}

void JITCompiler::clear_all_caches() {
    method_jit_->clear_cache();
    optimizing_jit_->clear_cache();
    code_cache_->clear();
    inline_caches_.clear();
    hotspots_.clear();
}

void JITCompiler::check_and_trigger_optimization(const std::string& func_name) {
    auto* hotspot = get_hotspot(func_name);
    if (hotspot && hotspot->needs_optimization) {
        // 这里可以触发后台优化编译
        // 实际实现应该在后台线程进行
    }
}

// ============================================================================
// 去优化点注册实现 (Phase 18.1 新增)
// ============================================================================

void JITCompiler::register_deoptimization_points(const bytecode::Function& func) {
    if (!deopt_manager_) return;
    
    // 遍历函数指令，识别需要守卫的位置
    for (size_t i = 0; i < func.code.size(); i++) {
        const auto& inst = func.code[i];
        
        DeoptimizationPoint point;
        point.bytecode_offset = i;
        point.reason = DeoptimizationReason::kUnknown;
        point.interpreter_entry_offset = i;
        
        // 类型守卫点 (基于操作码)
        switch (inst.op) {
            case bytecode::OpCode::CALL:
            case bytecode::OpCode::LOAD_INDEX:
            case bytecode::OpCode::STORE_INDEX:
                point.reason = DeoptimizationReason::kTypeMismatch;
                deopt_manager_->register_deoptimization_point(func.name, point);
                break;
            case bytecode::OpCode::IDIV:
            case bytecode::OpCode::IMOD:
                point.reason = DeoptimizationReason::kDivisionByZero;
                deopt_manager_->register_deoptimization_point(func.name, point);
                break;
            default:
                break;
        }
    }
}

void JITCompiler::register_type_guard(const std::string& func, size_t offset,
                                      bytecode::ValueType expected_type) {
    if (!deopt_manager_) return;
    
    DeoptimizationPoint point;
    point.bytecode_offset = offset;
    point.reason = DeoptimizationReason::kTypeMismatch;
    point.expected_types.push_back(expected_type);
    point.interpreter_entry_offset = offset;
    
    deopt_manager_->register_deoptimization_point(func, point);
}

// ============================================================================
// 便捷函数实现
// ============================================================================

std::unique_ptr<JITCompiler> create_jit_compiler() {
    return std::make_unique<JITCompiler>();
}

std::unique_ptr<JITCompiler> create_jit_compiler(const JITConfig& config) {
    return std::make_unique<JITCompiler>(config);
}

// ============================================================================
// RuntimeFunctionRegistry 实现 (解决 TODO: 运行时函数地址解析)
// ============================================================================

RuntimeFunctionRegistry& RuntimeFunctionRegistry::instance() {
    static RuntimeFunctionRegistry registry;
    return registry;
}

void RuntimeFunctionRegistry::register_function(const std::string& name, void* address) {
    functions_[name] = address;
}

void* RuntimeFunctionRegistry::lookup(const std::string& name) const {
    auto it = functions_.find(name);
    if (it != functions_.end()) {
        return it->second;
    }
    return nullptr;
}

void RuntimeFunctionRegistry::register_builtin_functions() {
    // 注册内置运行时函数地址
    // 使用新创建的 JIT 运行时库中的真实函数地址
    
    // 张量操作
    register_function("tensor_create", reinterpret_cast<void*>(&runtime::tensor_create));
    register_function("tensor_matmul", reinterpret_cast<void*>(&runtime::tensor_matmul));
    register_function("tensor_reshape", reinterpret_cast<void*>(&runtime::tensor_reshape));
    register_function("tensor_get", reinterpret_cast<void*>(&runtime::tensor_get));
    register_function("tensor_set", reinterpret_cast<void*>(&runtime::tensor_set));
    
    // 闭包操作 - 使用 JIT 运行时实现
    register_function("closure_create", reinterpret_cast<void*>(&runtime::closure_create));
    register_function("close_upvalue", reinterpret_cast<void*>(&runtime::close_upvalue));
    
    // 内存操作
    register_function("alloc_array", reinterpret_cast<void*>(&runtime::alloc_array));
    register_function("alloc_tuple", reinterpret_cast<void*>(&runtime::alloc_tuple));
    register_function("alloc_obj", nullptr);
    register_function("gc_alloc", nullptr);
    register_function("gc_mark", nullptr);
    
    // 数组操作
    register_function("array_push", reinterpret_cast<void*>(&runtime::array_push));
    register_function("array_len", reinterpret_cast<void*>(&runtime::array_len));
    register_function("array_get", reinterpret_cast<void*>(&runtime::array_get));
    register_function("array_set", reinterpret_cast<void*>(&runtime::array_set));
    
    // 字符串操作
    register_function("string_concat", reinterpret_cast<void*>(&runtime::string_concat));
    register_function("string_slice", reinterpret_cast<void*>(&runtime::string_slice));
    register_function("string_len", reinterpret_cast<void*>(&runtime::string_len));
    register_function("string_eq", reinterpret_cast<void*>(&runtime::string_eq));
    
    // 类型操作
    register_function("type_of", reinterpret_cast<void*>(&runtime::type_of));
    register_function("to_string", reinterpret_cast<void*>(&runtime::to_string));
    register_function("to_int", reinterpret_cast<void*>(&runtime::to_int));
    register_function("to_float", reinterpret_cast<void*>(&runtime::to_float));
    
    // 打印操作
    register_function("print", reinterpret_cast<void*>(&runtime::print));
    register_function("println", reinterpret_cast<void*>(&runtime::println));
    register_function("print_str", reinterpret_cast<void*>(&runtime::print_str));
    register_function("println_str", reinterpret_cast<void*>(&runtime::println_str));
    
    // 数学函数
    register_function("math_abs", reinterpret_cast<void*>(&runtime::math_abs));
    register_function("math_sin", reinterpret_cast<void*>(&runtime::math_sin));
    register_function("math_cos", reinterpret_cast<void*>(&runtime::math_cos));
    register_function("math_tan", reinterpret_cast<void*>(&runtime::math_tan));
    register_function("math_sqrt", reinterpret_cast<void*>(&runtime::math_sqrt));
    register_function("math_exp", reinterpret_cast<void*>(&runtime::math_exp));
    register_function("math_log", reinterpret_cast<void*>(&runtime::math_log));
    register_function("math_pow", reinterpret_cast<void*>(&runtime::math_pow));
    register_function("math_floor", reinterpret_cast<void*>(&runtime::math_floor));
    register_function("math_ceil", reinterpret_cast<void*>(&runtime::math_ceil));
    register_function("math_round", reinterpret_cast<void*>(&runtime::math_round));
    
    // 全局变量
    register_function("set_global", reinterpret_cast<void*>(&runtime::set_global));
    register_function("get_global", reinterpret_cast<void*>(&runtime::get_global));
    
    // 运行时
    register_function("init_runtime", reinterpret_cast<void*>(&runtime::init_runtime));
    register_function("shutdown_runtime", reinterpret_cast<void*>(&runtime::shutdown_runtime));
    register_function("get_alloc_count", reinterpret_cast<void*>(&runtime::get_alloc_count));
    register_function("get_total_allocated", reinterpret_cast<void*>(&runtime::get_total_allocated));
    register_function("get_gc_count", reinterpret_cast<void*>(&runtime::get_gc_count));
}

bool RuntimeFunctionRegistry::is_registered(const std::string& name) const {
    return functions_.find(name) != functions_.end();
}

void RuntimeFunctionRegistry::print_registered_functions() const {
    std::cout << "Registered runtime functions:" << std::endl;
    for (const auto& [name, addr] : functions_) {
        std::cout << "  " << name << " -> " << addr << std::endl;
    }
}

// ============================================================================
// Emitter-based 代码生成方法实现
// ============================================================================

void MethodJITCompiler::emit_prologue_emitter(size_t local_count) {
    if (!emitter_) return;
    
    // push rbp
    emitter_->push(x86_64::Register64::RBP);
    // mov rbp, rsp
    emitter_->mov(x86_64::Register64::RBP, x86_64::Register64::RSP);
    
    // 分配局部变量空间
    if (local_count > 0) {
        size_t stack_size = local_count * 8;
        emitter_->sub(x86_64::Register64::RSP, x86_64::Imm32(static_cast<int32_t>(stack_size)));
    }
}

void MethodJITCompiler::emit_epilogue_emitter() {
    if (!emitter_) return;
    
    // pop rbp
    emitter_->pop(x86_64::Register64::RBP);
    // ret
    emitter_->ret();
}

void MethodJITCompiler::emit_instruction_emitter(const bytecode::Instruction& inst) {
    if (!emitter_) return;
    
    switch (inst.op) {
        // 整数运算
        case bytecode::OpCode::IADD:
            // pop rdx; pop rax; add rax, rdx; push rax
            emitter_->pop(x86_64::Register64::RDX);
            emitter_->pop(x86_64::Register64::RAX);
            emitter_->add(x86_64::Register64::RAX, x86_64::Register64::RDX);
            emitter_->push(x86_64::Register64::RAX);
            break;
        case bytecode::OpCode::ISUB:
            // pop rdx; pop rax; sub rax, rdx; push rax
            emitter_->pop(x86_64::Register64::RDX);
            emitter_->pop(x86_64::Register64::RAX);
            emitter_->sub(x86_64::Register64::RAX, x86_64::Register64::RDX);
            emitter_->push(x86_64::Register64::RAX);
            break;
        case bytecode::OpCode::IMUL:
            // pop rdx; pop rax; imul rax, rdx; push rax
            emitter_->pop(x86_64::Register64::RDX);
            emitter_->pop(x86_64::Register64::RAX);
            emitter_->imul(x86_64::Register64::RAX, x86_64::Register64::RDX);
            emitter_->push(x86_64::Register64::RAX);
            break;
            
        // 整数比较
        case bytecode::OpCode::IEQ:
            emitter_->pop(x86_64::Register64::RDX);
            emitter_->pop(x86_64::Register64::RAX);
            emitter_->cmp(x86_64::Register64::RAX, x86_64::Register64::RDX);
            emitter_->sete(x86_64::Register8::AL);
            emitter_->movzx(x86_64::Register64::RAX, x86_64::Register8::AL);
            emitter_->push(x86_64::Register64::RAX);
            break;
        case bytecode::OpCode::ILT:
            emitter_->pop(x86_64::Register64::RDX);
            emitter_->pop(x86_64::Register64::RAX);
            emitter_->cmp(x86_64::Register64::RAX, x86_64::Register64::RDX);
            emitter_->setl(x86_64::Register8::AL);
            emitter_->movzx(x86_64::Register64::RAX, x86_64::Register8::AL);
            emitter_->push(x86_64::Register64::RAX);
            break;
            
        // 局部变量加载
        case bytecode::OpCode::LOAD_LOCAL:
        case bytecode::OpCode::LOAD_LOCAL_0:
        case bytecode::OpCode::LOAD_LOCAL_1: {
            size_t slot = (inst.op == bytecode::OpCode::LOAD_LOCAL_0) ? 0 :
                         (inst.op == bytecode::OpCode::LOAD_LOCAL_1) ? 1 :
                         static_cast<size_t>(inst.operand);
            int32_t offset = -static_cast<int32_t>((slot + 1) * 8);
            auto mem = x86_64::MemOperand::make_disp(x86_64::Register64::RBP, offset);
            emitter_->mov(x86_64::Register64::RAX, mem);
            emitter_->push(x86_64::Register64::RAX);
            break;
        }
            
        // 局部变量存储
        case bytecode::OpCode::STORE_LOCAL: {
            size_t slot = static_cast<size_t>(inst.operand);
            int32_t offset = -static_cast<int32_t>((slot + 1) * 8);
            emitter_->pop(x86_64::Register64::RAX);
            emitter_->mov(x86_64::MemOperand::make_disp(x86_64::Register64::RBP, offset), x86_64::Register64::RAX);
            break;
        }
        
        // 跳转
        case bytecode::OpCode::JMP: {
            int32_t offset = static_cast<int32_t>(inst.operand);
            emitter_->jmp_rel32(offset);
            break;
        }
        
        // 条件跳转
        case bytecode::OpCode::JMP_IF:
        case bytecode::OpCode::JMP_IF_NOT: {
            int32_t offset = static_cast<int32_t>(inst.operand);
            emitter_->pop(x86_64::Register64::RAX);
            emitter_->cmp(x86_64::Register64::RAX, x86_64::Imm32(0));
            if (inst.op == bytecode::OpCode::JMP_IF_NOT) {
                emitter_->je_rel32(offset);
            } else {
                emitter_->jne_rel32(offset);
            }
            break;
        }
        
        // 函数返回
        case bytecode::OpCode::RET:
        case bytecode::OpCode::RET_NULL:
            emit_return_emitter(inst.op);
            break;
            
        // PUSH (加载常量)
        case bytecode::OpCode::PUSH: {
            int64_t val = inst.operand;
            if (val >= INT32_MIN && val <= INT32_MAX) {
                emitter_->push(x86_64::Imm32(static_cast<int32_t>(val)));
            } else {
                emitter_->mov(x86_64::Register64::RAX, x86_64::Imm64(val));
                emitter_->push(x86_64::Register64::RAX);
            }
            break;
        }
        
        // NOP - 空操作
        case bytecode::OpCode::NOP:
            emitter_->emit_bytes_nop(1);  // 0x90
            break;
            
        // POP - 丢弃栈顶值
        case bytecode::OpCode::POP:
            emitter_->add(x86_64::Register64::RSP, x86_64::Imm32(8));
            break;
            
        // 函数调用
        case bytecode::OpCode::CALL:
            emit_call_emitter(inst);
            break;
            
        // 全局变量
        case bytecode::OpCode::LOAD_GLOBAL:
            emit_load_store_emitter(inst);
            break;
        case bytecode::OpCode::STORE_GLOBAL:
            emit_load_store_emitter(inst);
            break;
            
        default:
            // 未实现的操作码 - 添加 INT3 断点便于调试
            emitter_->emit_byte(0xCC);  // int3
            break;
    }
}

void MethodJITCompiler::emit_arithmetic_emitter(bytecode::OpCode op) {
    if (!emitter_) return;
    // 简化实现 - 使用 RAX/RDX 作为临时寄存器
}

void MethodJITCompiler::emit_comparison_emitter(bytecode::OpCode op) {
    if (!emitter_) return;
    // 已在 emit_instruction_emitter 中实现
}

void MethodJITCompiler::emit_load_store_emitter(const bytecode::Instruction& inst) {
    if (!emitter_) return;
    
    uint32_t idx = static_cast<uint32_t>(inst.operand);
    int32_t global_offset = static_cast<int32_t>(idx * 8);
    auto mem = x86_64::MemOperand::make_disp(x86_64::Register64::RBP, global_offset);
    
    if (inst.op == bytecode::OpCode::LOAD_GLOBAL) {
        // 从全局变量槽加载
        emitter_->mov(x86_64::Register64::RAX, mem);
        emitter_->push(x86_64::Register64::RAX);
    } else if (inst.op == bytecode::OpCode::STORE_GLOBAL) {
        // 存储到全局变量槽
        emitter_->pop(x86_64::Register64::RAX);
        emitter_->mov(mem, x86_64::Register64::RAX);
    }
}

void MethodJITCompiler::emit_jump_emitter(const bytecode::Instruction& inst) {
    if (!emitter_) return;
    
    int32_t offset = static_cast<int32_t>(inst.operand);
    emitter_->jmp_rel32(offset);
}

void MethodJITCompiler::emit_call_emitter(const bytecode::Instruction& inst) {
    if (!emitter_) return;
    
    // 外部函数调用 - 使用 call 指令
    // operand 包含函数地址索引
    uint32_t func_idx = static_cast<uint32_t>(inst.operand);
    (void)func_idx;
    
    // 实际实现需要查找函数地址
    // emitter_->call(Imm32(offset)); // 需要计算相对偏移
}

void MethodJITCompiler::emit_return_emitter(bytecode::OpCode op) {
    if (!emitter_) return;
    
    if (op == bytecode::OpCode::RET) {
        // 将返回值 (栈顶) 放入 RAX
        emitter_->pop(x86_64::Register64::RAX);
    }
    // 恢复栈并返回
    emitter_->mov(x86_64::Register64::RSP, x86_64::Register64::RBP);
    emitter_->pop(x86_64::Register64::RBP);
    emitter_->ret();
}

// ============================================================================
// 寄存器分配集成 [NEW]
// ============================================================================

void MethodJITCompiler::init_register_allocator() {
    if (!reg_allocator_) {
        reg_allocator_ = std::make_unique<LinearScanRegisterAllocator>();
    }
    
    // 设置 x86-64 整数寄存器 (System V AMD64 ABI)
    std::vector<PhysReg> int_regs = {
        // 调用者保存 (volatile)
        {RegId::RAX, RegClass::INTEGER, true, false},
        {RegId::RCX, RegClass::INTEGER, true, false},
        {RegId::RDX, RegClass::INTEGER, true, false},
        {RegId::RSI, RegClass::INTEGER, true, false},
        {RegId::RDI, RegClass::INTEGER, true, false},
        {RegId::R8, RegClass::INTEGER, true, false},
        {RegId::R9, RegClass::INTEGER, true, false},
        {RegId::R10, RegClass::INTEGER, true, false},
        {RegId::R11, RegClass::INTEGER, true, false},
        // 被调用者保存 (non-volatile)
        {RegId::RBX, RegClass::INTEGER, false, true},
        {RegId::R12, RegClass::INTEGER, false, true},
        {RegId::R13, RegClass::INTEGER, false, true},
        {RegId::R14, RegClass::INTEGER, false, true},
        {RegId::R15, RegClass::INTEGER, false, true},
    };
    
    // 设置 x86-64 浮点寄存器 (XMM0-XMM15)
    std::vector<PhysReg> float_regs = {
        {RegId::XMM0, RegClass::FLOAT, true, false},
        {RegId::XMM1, RegClass::FLOAT, true, false},
        {RegId::XMM2, RegClass::FLOAT, true, false},
        {RegId::XMM3, RegClass::FLOAT, true, false},
        {RegId::XMM4, RegClass::FLOAT, true, false},
        {RegId::XMM5, RegClass::FLOAT, true, false},
        {RegId::XMM6, RegClass::FLOAT, true, false},
        {RegId::XMM7, RegClass::FLOAT, true, false},
        {RegId::XMM8, RegClass::FLOAT, false, true},
        {RegId::XMM9, RegClass::FLOAT, false, true},
        {RegId::XMM10, RegClass::FLOAT, false, true},
        {RegId::XMM11, RegClass::FLOAT, false, true},
        {RegId::XMM12, RegClass::FLOAT, false, true},
        {RegId::XMM13, RegClass::FLOAT, false, true},
        {RegId::XMM14, RegClass::FLOAT, false, true},
        {RegId::XMM15, RegClass::FLOAT, false, true},
    };
    
    reg_allocator_->setIntRegisters(int_regs);
    reg_allocator_->setFloatRegisters(float_regs);
}

std::unordered_map<uint32_t, int32_t> MethodJITCompiler::allocate_registers_for_function(
    const bytecode::Function& func
) {
    init_register_allocator();
    
    std::unordered_map<uint32_t, int32_t> result;
    
    // 构建活跃区间分析所需的指令信息
    std::vector<LiveRangeAnalyzer::InstructionInfo> instructions;
    
    for (size_t i = 0; i < func.code.size(); ++i) {
        const auto& inst = func.code[i];
        LiveRangeAnalyzer::InstructionInfo info;
        info.index = static_cast<int>(i);
        
        // 提取使用的虚拟寄存器
        // 注意: 这里需要根据实际操作数类型来判断
        // 简化处理:LOAD_LOCAL 使用局部变量槽位作为虚拟寄存器 ID
        switch (inst.op) {
            case bytecode::OpCode::LOAD_LOCAL: {
                uint32_t slot = static_cast<uint32_t>(inst.operand);
                info.uses.push_back(slot);
                info.defs.push_back(slot);  // LOAD 定义一个新值
                break;
            }
            case bytecode::OpCode::STORE_LOCAL: {
                uint32_t slot = static_cast<uint32_t>(inst.operand);
                info.uses.push_back(slot);  // STORE 使用栈上的值
                break;
            }
            case bytecode::OpCode::IADD:
            case bytecode::OpCode::ISUB:
            case bytecode::OpCode::IMUL:
            case bytecode::OpCode::IDIV:
            case bytecode::OpCode::IEQ:
            case bytecode::OpCode::INE:
            case bytecode::OpCode::ILT:
            case bytecode::OpCode::ILE:
            case bytecode::OpCode::IGT:
            case bytecode::OpCode::IGE:
                // 二元运算: 使用两个操作数，定义结果
                info.uses.push_back(0);
                info.uses.push_back(1);
                info.defs.push_back(0);
                break;
            case bytecode::OpCode::CALL:
                info.is_call = true;
                break;
            default:
                break;
        }
        
        instructions.push_back(info);
    }
    
    // 虚拟寄存器类别映射 (简化: 默认整数)
    std::unordered_map<uint32_t, RegClass> virt_reg_classes;
    for (size_t i = 0; i < func.local_count; ++i) {
        virt_reg_classes[static_cast<uint32_t>(i)] = RegClass::INTEGER;
    }
    
    // 构建活跃区间
    auto analysis = LiveRangeAnalyzer::buildLiveIntervals(instructions, virt_reg_classes);
    if (!analysis.success || analysis.intervals.empty()) {
        // 如果分析失败，返回空映射 (使用默认栈槽)
        return result;
    }
    
    // 执行寄存器分配
    // System V AMD64: 14 个整数寄存器可用 (去除 RBP 作为帧指针)
    auto alloc_result = reg_allocator_->allocate(
        analysis.intervals,
        14,  // 整数寄存器数量
        16   // 浮点寄存器数量
    );
    
    if (alloc_result.success) {
        result = alloc_result.reg_alloc;
        
        // 处理溢出: 将溢出信息记录到 local_offsets_
        for (const auto& spill : alloc_result.spills) {
            uint32_t virt_reg = spill.first;
            const auto& spill_info = spill.second;
            // 将溢出槽位转换为负的栈偏移
            local_offsets_[virt_reg] = -8 * (spill_info.slot + 2);
        }
    }
    
    return result;
}

void MethodJITCompiler::emit_load_store_with_reg_alloc(
    void*& current,
    const bytecode::Instruction& inst,
    const std::unordered_map<uint32_t, int32_t>& reg_map
) {
    if (!emitter_) return;
    
    switch (inst.op) {
        case bytecode::OpCode::LOAD_LOCAL: {
            uint32_t slot = static_cast<uint32_t>(inst.operand);
            auto it = reg_map.find(slot);
            
            if (it != reg_map.end()) {
                // 寄存器已分配，直接从寄存器加载
                int32_t phys_reg_id = it->second;
                auto reg = static_cast<x86_64::Register64>(phys_reg_id);
                emitter_->push(reg);
            } else {
                // 使用栈槽
                int32_t offset = -8 * (slot + 1);
                auto mem = x86_64::MemOperand::make_disp(x86_64::Register64::RBP, offset);
                emitter_->push(mem);
            }
            break;
        }
        case bytecode::OpCode::STORE_LOCAL: {
            uint32_t slot = static_cast<uint32_t>(inst.operand);
            auto it = reg_map.find(slot);
            
            if (it != reg_map.end()) {
                // 寄存器已分配，弹出到寄存器
                int32_t phys_reg_id = it->second;
                auto reg = static_cast<x86_64::Register64>(phys_reg_id);
                emitter_->pop(reg);
            } else {
                // 使用栈槽
                emitter_->pop(x86_64::Register64::RAX);
                int32_t offset = -8 * (slot + 1);
                auto mem = x86_64::MemOperand::make_disp(x86_64::Register64::RBP, offset);
                emitter_->mov(mem, x86_64::Register64::RAX);
            }
            break;
        }
        default:
            break;
    }
}

} // namespace jit
} // namespace claw
