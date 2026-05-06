/**
 * @file tracing_jit.cpp
 * @brief Tracing JIT 编译器实现
 * 
 * 基于轨迹的 JIT 编译器实现，通过记录热循环的执行轨迹，
 * 将轨迹编译成优化后的机器码，提高动态语言的执行性能。
 */

#include "jit/tracing_jit.h"
#include "jit/jit_compiler.h"
#include "bytecode/bytecode.h"
#include <algorithm>
#include <cstring>
#include <random>
#include <chrono>

namespace claw {
namespace jit {

// ============================================================================
// TraceBuffer 实现
// ============================================================================

void TraceBuffer::start_recording() {
    buffer_.clear();
    current_trace_start_ = 0;
    in_loop_ = false;
    loop_entry_offset_ = 0;
    while (!loop_stack_.empty()) loop_stack_.pop();
    offset_counts_.clear();
}

void TraceBuffer::record_node(const TraceNode& node) {
    if (buffer_.size() >= MAX_TRACE_LENGTH) {
        return;  // 轨迹太长，停止记录
    }
    
    buffer_.push_back(node);
    offset_counts_[node.bytecode_offset]++;
}

void TraceBuffer::record_loop_entry(size_t offset) {
    in_loop_ = true;
    loop_entry_offset_ = offset;
    loop_stack_.push(offset);
    
    // 添加循环入口节点
    TraceNode entry_node(TraceNodeType::loop_entry, offset, buffer_.size(), 0);
    buffer_.push_back(entry_node);
}

void TraceBuffer::record_loop_exit(size_t offset) {
    if (!loop_stack_.empty()) {
        loop_stack_.pop();
    }
    
    if (loop_stack_.empty()) {
        in_loop_ = false;
    }
    
    // 添加循环退出节点
    TraceNode exit_node(TraceNodeType::loop_exit, offset, buffer_.size(), 0);
    buffer_.push_back(exit_node);
}

void TraceBuffer::record_side_exit(size_t offset, size_t target) {
    // 记录侧出口（条件跳转退出）
    if (buffer_.size() > 0) {
        buffer_.back().exits.push_back(target);
    }
}

Trace TraceBuffer::finish_trace(const std::string& id) {
    Trace trace(id);
    trace.nodes = buffer_;
    trace.entry_offset = current_trace_start_;
    
    // 收集覆盖的字节码偏移
    for (const auto& node : buffer_) {
        trace.bytecode_covered.insert(node.bytecode_offset);
    }
    
    // 识别循环退出点
    for (size_t i = 0; i < buffer_.size(); i++) {
        if (buffer_[i].type == TraceNodeType::loop_exit) {
            trace.loop_exits.push_back(i);
        }
        if (!buffer_[i].exits.empty()) {
            trace.side_exits.push_back(i);
        }
    }
    
    // 计算执行统计
    for (const auto& [offset, count] : offset_counts_) {
        trace.execution_count += count;
    }
    
    return trace;
}

void TraceBuffer::reset() {
    buffer_.clear();
    current_trace_start_ = 0;
    in_loop_ = false;
    loop_entry_offset_ = 0;
    while (!loop_stack_.empty()) loop_stack_.pop();
    offset_counts_.clear();
}

void TraceBuffer::clear() {
    reset();
}

// ============================================================================
// TraceRecorder 实现
// ============================================================================

TraceRecorder::TraceRecorder(TracingJIT* jit) : jit_(jit), trace_buffer_() {
}

void TraceRecorder::on_execution(size_t offset, uint8_t opcode) {
    if (!jit_->is_tracing_enabled()) {
        return;
    }
    
    // 检查是否应该开始记录
    if (!trace_buffer_.is_recording()) {
        if (should_start_trace()) {
            trace_buffer_.start_recording();
        }
    }
    
    if (trace_buffer_.is_recording()) {
        TraceNode node(TraceNodeType::bytecode, offset, trace_buffer_.buffer().size(), opcode);
        trace_buffer_.record_node(node);
        total_recorded_++;
        
        // 检查是否应该终止轨迹
        if (should_terminate_trace()) {
            flush_buffer_to_trace();
        }
    }
}

void TraceRecorder::on_loop_begin(size_t offset) {
    if (!jit_->is_tracing_enabled()) return;
    
    if (!trace_buffer_.is_recording()) {
        // 检查是否应该开始记录
        auto& counts = trace_buffer_.offset_counts();
        auto it = counts.find(offset);
        if (it != counts.end() && it->second > jit_->config().trace_hot_threshold) {
            trace_buffer_.start_recording();
        }
    }
    
    if (trace_buffer_.is_recording()) {
        trace_buffer_.record_loop_entry(offset);
    }
}

void TraceRecorder::on_loop_end(size_t offset) {
    if (!jit_->is_tracing_enabled()) return;
    if (trace_buffer_.is_recording()) {
        trace_buffer_.record_loop_exit(offset);
    }
}

void TraceRecorder::on_branch(size_t offset, size_t target, bool taken) {
    if (!jit_->is_tracing_enabled()) return;
    
    if (trace_buffer_.is_recording()) {
        if (!taken) {
            // 记录侧出口
            trace_buffer_.record_side_exit(offset, target);
            
            // 非Taken分支可能终止轨迹
            if (should_terminate_trace()) {
                flush_buffer_to_trace();
            }
        }
    }
}

void TraceRecorder::on_call(size_t offset) {
    if (!jit_->is_tracing_enabled()) return;
    // 函数调用通常不记录到轨迹中
}

void TraceRecorder::on_return(size_t offset) {
    if (!jit_->is_tracing_enabled()) return;
    
    if (trace_buffer_.is_recording()) {
        // 返回终止轨迹
        flush_buffer_to_trace();
    }
}

std::vector<Trace> TraceRecorder::get_completed_traces() {
    std::vector<Trace> result;
    result.swap(completed_traces_);
    return result;
}

void TraceRecorder::clear_traces() {
    completed_traces_.clear();
    trace_buffer_.clear();
}

bool TraceRecorder::should_start_trace() {
    // 基于执行计数判断是否应该开始记录
    return true;
}

bool TraceRecorder::should_terminate_trace() {
    const auto& buffer = trace_buffer_.buffer();
    
    if (buffer.empty()) return false;
    
    // 轨迹太长，终止
    if (buffer.size() >= TraceBuffer::MAX_TRACE_LENGTH) {
        return true;
    }
    
    // 遇到返回指令
    if (!buffer.empty()) {
        uint8_t last_op = buffer.back().opcode;
        if (last_op == static_cast<uint8_t>(bytecode::OpCode::RET) ||
            last_op == static_cast<uint8_t>(bytecode::OpCode::RET_NULL)) {
            return true;
        }
    }
    
    // 循环退出（侧出口过多）
    size_t side_exits = 0;
    for (const auto& node : buffer) {
        if (node.type == TraceNodeType::loop_exit || !node.exits.empty()) {
            side_exits++;
        }
    }
    
    if (side_exits > 5) {
        return true;
    }
    
    return false;
}

void TraceRecorder::flush_buffer_to_trace() {
    if (trace_buffer_.buffer().empty()) {
        return;
    }
    
    std::string id = "trace_" + std::to_string(completed_traces_.size());
    Trace trace = trace_buffer_.finish_trace(id);
    
    if (trace.size() >= 8) {  // 最小轨迹长度
        completed_traces_.push_back(trace);
    }
    
    trace_buffer_.reset();
}

// ============================================================================
// TraceBuilder 实现
// ============================================================================

TraceBuilder::TraceBuilder(TracingJIT* jit) : jit_(jit) {
}

std::unique_ptr<Trace> TraceBuilder::build(const Trace& raw_trace) {
    auto trace = std::make_unique<Trace>(raw_trace.id);
    *trace = raw_trace;
    
    // 应用轨迹优化
    optimize_trace(*trace);
    
    // 类型推断
    infer_types(*trace);
    
    // 识别汇合点
    identify_merge_points(*trace);
    
    return trace;
}

void TraceBuilder::optimize_trace(Trace& trace) {
    remove_redundant_nodes(trace);
    merge_constant_operations(trace);
    apply_copy_propagation(trace);
    apply_constant_folding(trace);
    apply_dead_code_elimination(trace);
    identify_loop_exits(trace);
}

void TraceBuilder::remove_redundant_nodes(Trace& trace) {
    if (trace.nodes.empty()) return;
    
    std::vector<TraceNode> new_nodes;
    std::unordered_set<size_t> seen_offsets;
    
    for (const auto& node : trace.nodes) {
        // 跳过重复的 NOP
        if (node.opcode == static_cast<uint8_t>(bytecode::OpCode::NOP)) {
            continue;
        }
        
        // 跳过完全重复的节点
        if (seen_offsets.count(node.bytecode_offset) > 0) {
            continue;
        }
        
        seen_offsets.insert(node.bytecode_offset);
        new_nodes.push_back(node);
    }
    
    trace.nodes = std::move(new_nodes);
}

void TraceBuilder::merge_constant_operations(Trace& trace) {
    // 常量折叠：合并相邻的常量运算
}

void TraceBuilder::identify_loop_exits(Trace& trace) {
    trace.loop_exits.clear();
    
    for (size_t i = 0; i < trace.nodes.size(); i++) {
        const auto& node = trace.nodes[i];
        if (node.type == TraceNodeType::loop_exit) {
            trace.loop_exits.push_back(i);
        }
    }
}

void TraceBuilder::identify_merge_points(Trace& trace) {
    trace.merge_points.clear();
    
    std::unordered_map<size_t, std::vector<size_t>> offset_to_nodes;
    
    for (size_t i = 0; i < trace.nodes.size(); i++) {
        const auto& node = trace.nodes[i];
        
        for (size_t exit : node.exits) {
            offset_to_nodes[exit].push_back(i);
        }
    }
    
    for (const auto& [offset, sources] : offset_to_nodes) {
        if (sources.size() > 1) {
            trace.merge_points[offset] = sources[0];
        }
    }
}

void TraceBuilder::infer_types(Trace& trace) {
    for (auto& node : trace.nodes) {
        node.inferred_type = infer_node_type(node);
    }
}

bytecode::ValueType TraceBuilder::infer_node_type(const TraceNode& node) {
    using Op = bytecode::OpCode;
    uint8_t op = node.opcode;
    
    // 算术运算返回整数或浮点
    if (op >= static_cast<uint8_t>(Op::IADD) && op <= static_cast<uint8_t>(Op::IMOD)) {
        return bytecode::ValueType::I64;
    }
    if (op >= static_cast<uint8_t>(Op::FADD) && op <= static_cast<uint8_t>(Op::FMOD)) {
        return bytecode::ValueType::F64;
    }
    
    // 比较运算返回布尔
    if (op >= static_cast<uint8_t>(Op::IEQ) && op <= static_cast<uint8_t>(Op::FGE)) {
        return bytecode::ValueType::BOOL;
    }
    
    // 字符串操作
    if (op == static_cast<uint8_t>(Op::S2I) || op == static_cast<uint8_t>(Op::S2F)) {
        return bytecode::ValueType::I64;
    }
    if (op == static_cast<uint8_t>(Op::I2S) || op == static_cast<uint8_t>(Op::F2S)) {
        return bytecode::ValueType::STRING;
    }
    
    // 类型转换
    if (op == static_cast<uint8_t>(Op::I2F)) return bytecode::ValueType::F64;
    if (op == static_cast<uint8_t>(Op::F2I)) return bytecode::ValueType::I64;
    if (op == static_cast<uint8_t>(Op::I2B)) return bytecode::ValueType::BOOL;
    if (op == static_cast<uint8_t>(Op::B2I)) return bytecode::ValueType::I64;
    
    // 数组/元组操作
    if (op == static_cast<uint8_t>(Op::ARRAY_LEN)) return bytecode::ValueType::I64;
    if (op == static_cast<uint8_t>(Op::CREATE_TUPLE)) return bytecode::ValueType::TUPLE;
    if (op == static_cast<uint8_t>(Op::ALLOC_ARRAY)) return bytecode::ValueType::ARRAY;
    if (op == static_cast<uint8_t>(Op::TENSOR_CREATE)) return bytecode::ValueType::TENSOR;
    
    return bytecode::ValueType::NIL;
}

bool TraceBuilder::is_redundant(const TraceNode& node) const {
    if (node.opcode == static_cast<uint8_t>(bytecode::OpCode::NOP)) {
        return true;
    }
    return false;
}

bool TraceBuilder::can_merge_with_previous(const TraceNode& a, const TraceNode& b) const {
    return false;
}

bool TraceBuilder::is_loop_exit(const TraceNode& node) const {
    return node.type == TraceNodeType::loop_exit || !node.exits.empty();
}

bool TraceBuilder::is_merge_point(size_t offset) const {
    return false;
}

void TraceBuilder::apply_copy_propagation(Trace& trace) {
    // 简化实现
}

void TraceBuilder::apply_constant_folding(Trace& trace) {
    // 编译时常量计算
}

void TraceBuilder::apply_dead_code_elimination(Trace& trace) {
    // 移除不可达代码
}

// ============================================================================
// TraceCompiler 实现
// ============================================================================

TraceCompiler::TraceCompiler(TracingJIT* jit) : jit_(jit) {
}

CompilationResult TraceCompiler::compile_trace(Trace& trace) {
    CompilationResult result;
    result.success = false;
    
    // 估计代码大小
    size_t estimated_size = estimate_code_size(trace);
    if (estimated_size > jit_->config().code_cache_size) {
        trace.invalidate("Code too large");
        result.error_message = "Trace code exceeds maximum size";
        return result;
    }
    
    // 生成代码
    try {
        void* code = generate_code(trace);
        if (code) {
            trace.compiled_code = code;
            trace.code_size = estimated_size;
            result.success = true;
            result.machine_code = code;
            result.code_size = estimated_size;
        }
    } catch (const std::exception& e) {
        handle_compilation_error(trace, e.what());
        result.error_message = e.what();
    }
    
    return result;
}

void* TraceCompiler::generate_code(const Trace& trace) {
    if (trace.nodes.empty() || !trace.is_valid) {
        return nullptr;
    }
    
    // 轨迹代码生成需要特定的目标代码生成器
    // 简化实现：返回 nullptr，实际需要集成 Method JIT
    return nullptr;
}

size_t TraceCompiler::estimate_code_size(const Trace& trace) {
    return trace.nodes.size() * 8 + 64;
}

void TraceCompiler::optimize_assembly(void* code, size_t size) {
    // 轨迹特定的汇编优化
}

void TraceCompiler::emit_trace_prologue(Trace& trace) {
    // 保存寄存器，设置栈帧
}

void TraceCompiler::emit_trace_node(const TraceNode& node) {
    // 发射单个节点的代码
}

void TraceCompiler::emit_trace_epilogue(Trace& trace) {
    // 恢复寄存器，返回
}

void TraceCompiler::emit_trace_stitch(Trace& trace, size_t exit_idx, void* target) {
    // 轨迹连接
}

void TraceCompiler::resolve_jump_targets(Trace& trace) {
    // 解析跳转目标
}

size_t TraceCompiler::calculate_jump_offset(const TraceNode& from, const TraceNode& to) {
    return 0;
}

void TraceCompiler::handle_compilation_error(Trace& trace, const std::string& error) {
    trace.invalidate(error);
}

// ============================================================================
// TracingJIT 实现
// ============================================================================

TracingJIT::TracingJIT(std::unique_ptr<JITCompiler> method_jit)
    : method_jit_(std::move(method_jit)) {
    initialize_components();
    setup_runtime_functions();
}

TracingJIT::~TracingJIT() {
    for (auto& [name, code] : compiled_code_cache_) {
        (void)name;
        // 释放内存
    }
}

void TracingJIT::initialize_components() {
    HotSpotConfig hs_config;
    hot_spot_ = std::make_unique<HotSpotDetector>(hs_config);
    
    recorder_ = std::make_unique<TraceRecorder>(this);
    builder_ = std::make_unique<TraceBuilder>(this);
    compiler_ = std::make_unique<TraceCompiler>(this);
}

void TracingJIT::setup_runtime_functions() {
    // 从 Method JIT 复制运行时函数
}

void TracingJIT::configure(const JITConfig& config) {
    config_ = config;
}

CompilationResult TracingJIT::compile(const bytecode::Function& func) {
    CompilationResult result;
    result.success = false;
    result.error_message = "Use Method JIT for compilation";
    return result;
}

void* TracingJIT::get_compiled_code(const std::string& func_name) {
    auto it = compiled_code_cache_.find(func_name);
    if (it != compiled_code_cache_.end()) {
        return it->second;
    }
    return nullptr;
}

void TracingJIT::enable_tracing() {
    tracing_enabled_ = true;
}

void TracingJIT::disable_tracing() {
    tracing_enabled_ = false;
}

void TracingJIT::record_execution(size_t offset, uint8_t opcode) {
    if (!tracing_enabled_) return;
    
    stats_.total_executions++;
    
    if (recorder_) {
        recorder_->on_execution(offset, opcode);
    }
}

std::vector<Trace> TracingJIT::get_all_traces() const {
    std::vector<Trace> all_traces;
    
    for (const auto& [func, traces] : function_traces_) {
        for (const auto& trace : traces) {
            all_traces.push_back(*trace);
        }
    }
    
    return all_traces;
}

void TracingJIT::invalidate_traces_for_function(const std::string& func_name) {
    auto it = function_traces_.find(func_name);
    if (it != function_traces_.end()) {
        for (auto& trace : it->second) {
            trace->invalidate("Function invalidated");
        }
        function_traces_.erase(it);
    }
}

void TracingJIT::schedule_trace_compilation(Trace& trace) {
    std::lock_guard<std::mutex> lock(trace_mutex_);
    trace_compilation_queue_.push(std::ref(trace));
}

void TracingJIT::compile_queued_traces() {
    std::vector<Trace> new_traces;
    
    {
        std::lock_guard<std::mutex> lock(trace_mutex_);
        
        while (!trace_compilation_queue_.empty()) {
            new_traces.push_back(trace_compilation_queue_.front());
            trace_compilation_queue_.pop();
        }
    }
    
    for (auto& trace : new_traces) {
        if (should_compile_trace(trace)) {
            auto built_trace = builder_->build(trace);
            auto result = compiler_->compile_trace(*built_trace);
            
            if (result.success) {
                stats_.trace_compiled++;
                stats_.total_code_generated += result.code_size;
            } else {
                stats_.trace_invalidated++;
            }
        }
    }
}

TracingJIT::TracingStats TracingJIT::get_stats() const {
    TracingStats stats = stats_;
    
    size_t total_nodes = 0;
    size_t trace_count = 0;
    for (const auto& [func, traces] : function_traces_) {
        for (const auto& trace : traces) {
            total_nodes += trace->size();
            trace_count++;
        }
    }
    
    if (trace_count > 0) {
        stats.avg_trace_length = static_cast<double>(total_nodes) / trace_count;
    }
    
    return stats;
}

void TracingJIT::reset_stats() {
    stats_ = TracingStats();
}

void TracingJIT::print_stats() const {
    auto stats = get_stats();
    
    printf("=== Tracing JIT Stats ===\n");
    printf("Total executions: %llu\n", (unsigned long long)stats.total_executions);
    printf("Trace attempts: %llu\n", (unsigned long long)stats.trace_attempts);
    printf("Trace completed: %llu\n", (unsigned long long)stats.trace_completed);
    printf("Trace compiled: %llu\n", (unsigned long long)stats.trace_compiled);
    printf("Trace invalidated: %llu\n", (unsigned long long)stats.trace_invalidated);
    printf("Total code generated: %zu bytes\n", stats.total_code_generated);
    printf("Avg trace length: %.1f\n", stats.avg_trace_length);
    printf("=========================\n");
}

void* TracingJIT::get_runtime_function(const std::string& name) {
    auto it = runtime_functions_.find(name);
    if (it != runtime_functions_.end()) {
        return it->second;
    }
    return nullptr;
}

void TracingJIT::register_runtime_function(const std::string& name, void* addr) {
    runtime_functions_[name] = addr;
}

std::string TracingJIT::generate_trace_id(const std::string& func_name, size_t offset) {
    return func_name + "_" + std::to_string(offset);
}

bool TracingJIT::should_compile_trace(const Trace& trace) {
    if (trace.size() < 8) {
        return false;
    }
    
    if (trace.execution_count < config_.trace_hot_threshold) {
        return false;
    }
    
    return true;
}

void TracingJIT::update_stats(const Trace& trace) {
    stats_.trace_completed++;
    stats_.trace_attempts += trace.execution_count;
}

// ============================================================================
// 便捷函数
// ============================================================================

std::unique_ptr<TracingJIT> create_tracing_jit() {
    JITConfig config;
    auto method_jit = create_jit_compiler(config);
    return std::make_unique<TracingJIT>(std::move(method_jit));
}

std::unique_ptr<TracingJIT> create_tracing_jit(const JITConfig& config) {
    auto method_jit = create_jit_compiler(config);
    auto tracing_jit = std::make_unique<TracingJIT>(std::move(method_jit));
    tracing_jit->configure(config);
    return tracing_jit;
}

} // namespace jit
} // namespace claw
