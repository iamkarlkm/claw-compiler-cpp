// jit/deoptimization.cpp - 去优化 (Deoptimization) 实现
// 完整的去优化框架，用于 JIT 优化假设失效时安全回退

#include "deoptimization.h"
#include <sys/mman.h>
#include <cstring>
#include <algorithm>
#include <iostream>
#include "../emitter/x86_64_emitter.h"

namespace claw {
namespace jit {

// ============================================================================
// DeoptimizationManager 实现
// ============================================================================

DeoptimizationManager::DeoptimizationManager() {
    frame_pool_.reserve(64);
}

DeoptimizationManager::~DeoptimizationManager() {
    clear();
}

void DeoptimizationManager::register_deoptimization_point(
    const std::string& function,
    const DeoptimizationPoint& point
) {
    deopt_points_[function][point.bytecode_offset] = point;
}

const DeoptimizationPoint* DeoptimizationManager::find_deoptimization_point(
    const std::string& function,
    size_t offset
) const {
    auto func_it = deopt_points_.find(function);
    if (func_it == deopt_points_.end()) {
        return nullptr;
    }
    
    auto offset_it = func_it->second.find(offset);
    if (offset_it == func_it->second.end()) {
        return nullptr;
    }
    
    return &offset_it->second;
}

DeoptimizationTarget DeoptimizationManager::execute_deoptimization(
    const std::string& function,
    size_t offset,
    DeoptimizationReason reason,
    const std::vector<bytecode::Value>& guard_values
) {
    total_deoptimizations_++;
    deopt_counts_[reason]++;
    
    // 查找去优化点
    const auto* point = find_deoptimization_point(function, offset);
    
    DeoptimizationTarget target;
    target.bytecode_offset = point ? point->interpreter_entry_offset : offset;
    target.stack_size = point ? point->stack_frame_size : 0;
    target.restore_pc = true;
    target.restore_stack = true;
    target.restore_locals = true;
    
    // 如果有 guard 值，恢复它们
    if (!guard_values.empty()) {
        target.values = guard_values;
    }
    
#ifdef CLAW_JIT_DEBUG
    std::cerr << "[Deoptimization] " << function << " offset=" << offset
              << " reason=" << reason_to_string(reason) << std::endl;
#endif
    
    return target;
}

void* DeoptimizationManager::create_guard_check(
    const DeoptimizationPoint& point,
    void* original_code,
    void* deoptimized_code
) {
    // 创建类型守卫检查代码
    // 如果检查失败，跳转到去优化代码
    
    // 使用 x86_64 emitter 生成检查代码
    x86_64::X86_64Emitter emitter;
    
    // 计算跳转偏移
    int32_t jump_offset = reinterpret_cast<int64_t>(deoptimized_code) - 
                          reinterpret_cast<int64_t>(original_code);
    
    // 生成守卫检查代码 (简化版本)
    // 实际实现需要根据守卫类型生成不同的检查
    
    // TODO: 实现完整的守卫检查代码生成
    (void)point;
    (void)jump_offset;
    
    return original_code;
}

void DeoptimizationManager::cache_translation(
    const std::string& function,
    size_t offset,
    const TranslatedCode& translation
) {
    translation_cache_[function][offset] = translation;
}

const TranslatedCode* DeoptimizationManager::find_cached_translation(
    const std::string& function,
    size_t offset
) const {
    auto func_it = translation_cache_.find(function);
    if (func_it == translation_cache_.end()) {
        return nullptr;
    }
    
    auto offset_it = func_it->second.find(offset);
    if (offset_it == func_it->second.end()) {
        return nullptr;
    }
    
    return &offset_it->second;
}

void DeoptimizationManager::capture_stack_frame(StackFrameInfo& frame) {
    // 捕获当前栈帧信息
    // 实际实现需要读取 CPU 寄存器
    
    // TODO: 实现栈帧捕获
    (void)frame;
}

void DeoptimizationManager::unwind_stack(std::vector<StackFrameInfo>& frames) {
    // 展开调用栈
    // 从当前帧开始，依次向上追溯
    
    frames.clear();
    
    StackFrameInfo* current = allocate_frame();
    capture_stack_frame(*current);
    
    while (current) {
        frames.push_back(*current);
        current = current->caller;
    }
}

size_t DeoptimizationManager::deoptimizations_by_reason(
    DeoptimizationReason reason
) const {
    auto it = deopt_counts_.find(reason);
    return it != deopt_counts_.end() ? it->second : 0;
}

void DeoptimizationManager::clear() {
    deopt_points_.clear();
    translation_cache_.clear();
    deopt_counts_.clear();
    total_deoptimizations_ = 0;
    frame_pool_.clear();
    frame_pool_index_ = 0;
}

StackFrameInfo* DeoptimizationManager::allocate_frame() {
    if (frame_pool_index_ < frame_pool_.size()) {
        return &frame_pool_[frame_pool_index_++];
    }
    
    frame_pool_.emplace_back();
    frame_pool_index_++;
    return &frame_pool_.back();
}

void DeoptimizationManager::release_frame(StackFrameInfo* frame) {
    (void)frame;
    // 帧被重用，不需要显式释放
}

std::string DeoptimizationManager::reason_to_string(
    DeoptimizationReason reason
) const {
    switch (reason) {
        case DeoptimizationReason::kTypeMismatch:
            return "TypeMismatch";
        case DeoptimizationReason::kUnexpectedType:
            return "UnexpectedType";
        case DeoptimizationReason::kPolymorphicCall:
            return "PolymorphicCall";
        case DeoptimizationReason::kDivisionByZero:
            return "DivisionByZero";
        case DeoptimizationReason::kOverflow:
            return "Overflow";
        case DeoptimizationReason::kNaN:
            return "NaN";
        case DeoptimizationReason::kUnreachable:
            return "Unreachable";
        case DeoptimizationReason::kLoopCountMismatch:
            return "LoopCountMismatch";
        case DeoptimizationReason::kOutOfBounds:
            return "OutOfBounds";
        case DeoptimizationReason::kNullPointer:
            return "NullPointer";
        case DeoptimizationReason::kStackOverflow:
            return "StackOverflow";
        case DeoptimizationReason::kGC:
            return "GC";
        case DeoptimizationReason::kBreakpoint:
            return "Breakpoint";
        default:
            return "Unknown";
    }
}

// ============================================================================
// OSRCompiler 实现
// ============================================================================

OSRCompiler::OSRCompiler() {}

OSRCompiler::~OSRCompiler() {
    clear();
}

bool OSRCompiler::can_osr(
    const std::string& function,
    size_t loop_offset,
    const std::vector<bytecode::Value>& current_state
) const {
    // 检查是否可以进行 OSR
    // 条件:
    // 1. 函数存在
    // 2. 循环偏移有效
    // 3. 状态可以序列化
    
    if (function.empty() || loop_offset == 0) {
        return false;
    }
    
    // 检查是否有待处理的 OSR
    (void)current_state;
    
    return true;
}

void* OSRCompiler::compile_osr(
    const bytecode::Function& func,
    size_t entry_offset,
    const std::vector<bytecode::Value>& state,
    size_t stack_size
) {
    // 计算代码大小
    size_t code_size = estimate_osr_code_size(func, entry_offset);
    
    // 分配代码内存 (可执行)
    void* code = mmap(nullptr, code_size,
                      PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (code == MAP_FAILED) {
        return nullptr;
    }
    
    // 准备 OSR 状态
    std::vector<int64_t> stack_values;
    std::vector<int64_t> local_values;
    
    prepare_osr_state(func, entry_offset, state, stack_values, local_values);
    
    // 生成 OSR 前言
    void* code_ptr = emit_osr_prologue(code, stack_values, local_values, stack_size);
    
    // TODO: 生成实际字节码翻译代码
    // 这里简化处理，实际需要从 entry_offset 开始生成机器码
    
    // 缓存 OSR 代码
    osr_cache_[func.name][entry_offset] = code;
    entry_to_function_[code] = {func.name, entry_offset};
    
    return code;
}

void* OSRCompiler::get_osr_entry(
    const std::string& function,
    size_t offset
) const {
    auto func_it = osr_cache_.find(function);
    if (func_it == osr_cache_.end()) {
        return nullptr;
    }
    
    auto offset_it = func_it->second.find(offset);
    if (offset_it == func_it->second.end()) {
        return nullptr;
    }
    
    return offset_it->second;
}

bool OSRCompiler::trigger_osr(
    const std::string& function,
    size_t offset,
    const std::vector<bytecode::Value>& state,
    void*& osr_entry,
    DeoptimizationTarget& target
) {
    // 检查是否有缓存的 OSR 代码
    osr_entry = get_osr_entry(function, offset);
    
    if (osr_entry == nullptr) {
        // 需要编译 OSR 版本
        // TODO: 实际编译
        return false;
    }
    
    // 准备去优化目标
    target.bytecode_offset = offset;
    target.stack_size = state.size();
    target.values = state;
    target.restore_pc = true;
    target.restore_stack = true;
    target.restore_locals = true;
    
    return true;
}

void OSRCompiler::clear() {
    osr_cache_.clear();
    entry_to_function_.clear();
    
    std::lock_guard<std::mutex> lock(osr_queue_mutex_);
    pending_osr_.clear();
}

void OSRCompiler::prepare_osr_state(
    const bytecode::Function& func,
    size_t entry_offset,
    const std::vector<bytecode::Value>& state,
    std::vector<int64_t>& stack_values,
    std::vector<int64_t>& local_values
) {
    (void)func;
    (void)entry_offset;
    
    // 将 bytecode::Value 转换为 int64_t
    stack_values.clear();
    local_values.clear();
    
    for (const auto& val : state) {
        switch (val.type) {
            case bytecode::ValueType::NIL:
                stack_values.push_back(0);
                break;
            case bytecode::ValueType::BOOL:
            case bytecode::ValueType::I8:
            case bytecode::ValueType::I16:
            case bytecode::ValueType::I32:
            case bytecode::ValueType::I64:
                stack_values.push_back(val.data.i64);
                break;
            case bytecode::ValueType::F32:
            case bytecode::ValueType::F64:
                // 将浮点数转换为位模式
                stack_values.push_back(static_cast<int64_t>(val.data.f64));
                break;
            default:
                // 对于其他类型，使用指针表示
                stack_values.push_back(reinterpret_cast<int64_t>(val.data.i64));
                (void)val;
                break;
        }
    }
}

void* OSRCompiler::emit_osr_prologue(
    void* code_ptr,
    const std::vector<int64_t>& stack_values,
    const std::vector<int64_t>& local_values,
    size_t stack_size
) {
    // OSR 前言生成 - 简化版本
    // 实际代码生成应该在 JIT 编译器中完成
    
    (void)code_ptr;
    (void)stack_values;
    (void)local_values;
    (void)stack_size;
    
    // 返回代码指针
    // TODO: 实现实际的 OSR prologue 代码生成
    return code_ptr;
}

size_t OSRCompiler::estimate_osr_code_size(
    const bytecode::Function& func,
    size_t entry_offset
) {
    (void)func;
    (void)entry_offset;
    
    // 粗略估计: 前言 + 每个栈值的存储 (约 16 字节)
    return 4096;
}

// ============================================================================
// JITCompilerWithDeopt 实现
// ============================================================================

JITCompilerWithDeopt::JITCompilerWithDeopt() {}

JITCompilerWithDeopt::~JITCompilerWithDeopt() {}

void* JITCompilerWithDeopt::compile_with_deoptimization(
    const bytecode::Function& func,
    bool enable_optimization
) {
    // 查找缓存
    auto cache = enable_optimization ? optimized_cache_ : compiled_cache_;
    auto it = cache.find(func.name);
    
    if (it != cache.end()) {
        return it->second;
    }
    
    // TODO: 实际调用 JIT 编译器
    (void)func;
    
    return nullptr;
}

DeoptimizationTarget JITCompilerWithDeopt::deoptimize(
    const std::string& function,
    size_t offset,
    DeoptimizationReason reason,
    const std::vector<bytecode::Value>& guard_values
) {
    return deopt_manager_.execute_deoptimization(
        function, offset, reason, guard_values
    );
}

bool JITCompilerWithDeopt::perform_osr(
    const std::string& function,
    size_t loop_offset,
    const std::vector<bytecode::Value>& state,
    void*& osr_entry
) {
    DeoptimizationTarget target;
    
    bool success = osr_compiler_.trigger_osr(
        function, loop_offset, state, osr_entry, target
    );
    
    if (success) {
        total_osr_++;
    }
    
    return success;
}

void JITCompilerWithDeopt::add_deoptimization_point(
    const std::string& function,
    const DeoptimizationPoint& point
) {
    deopt_manager_.register_deoptimization_point(function, point);
}

bool JITCompilerWithDeopt::check_type_guard(
    bytecode::Value actual_value,
    bytecode::ValueType expected_type
) {
    // 检查实际类型是否匹配期望类型
    return actual_value.type == expected_type;
}

// ============================================================================
// 便捷函数实现
// ============================================================================

std::unique_ptr<DeoptimizationManager> create_deoptimization_manager() {
    return std::make_unique<DeoptimizationManager>();
}

std::unique_ptr<OSRCompiler> create_osr_compiler() {
    return std::make_unique<OSRCompiler>();
}

std::unique_ptr<JITCompilerWithDeopt> create_jit_compiler_with_deoptimization() {
    return std::make_unique<JITCompilerWithDeopt>();
}

} // namespace jit
} // namespace claw
