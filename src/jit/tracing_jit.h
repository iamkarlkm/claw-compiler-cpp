/**
 * @file tracing_jit.h
 * @brief Tracing JIT 编译器 - 基于轨迹的即时编译
 * 
 * Tracing JIT 通过记录热循环的执行轨迹，将循环内的指令序列
 * 编译成优化后的机器码，实现比 Method JIT 更高的性能。
 */

#ifndef CLAW_TRACING_JIT_H
#define CLAW_TRACING_JIT_H

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <queue>
#include <stack>
#include <set>
#include "jit/jit_compiler.h"
#include "jit/hot_spot.h"
#include "bytecode/bytecode.h"

namespace claw {
namespace jit {

// 前向声明
class TracingJIT;
class Trace;
class TraceBuilder;
class TraceRecorder;
class TraceCompiler;

/**
 * @brief 轨迹节点类型
 */
enum class TraceNodeType {
    bytecode,      // 普通字节码指令
    loop_entry,    // 循环入口
    loop_exit,     // 循环出口
    side_exit,     // 侧出口（条件跳转退出）
    join_point     // 汇合点
};

/**
 * @brief 轨迹节点 - 轨迹中的单个指令
 */
struct TraceNode {
    TraceNodeType type;
    size_t bytecode_offset;    // 对应字节码偏移
    size_t instruction_index;  // 指令索引
    uint8_t opcode;            // 操作码
    std::vector<std::variant<int64_t, double, std::string>> args;  // 参数
    
    // 控制流信息
    size_t next_node = UINT64_MAX;    // 下一个节点
    std::vector<size_t> exits;        // 出口节点（条件跳转）
    
    // 类型信息（用于优化）
    bytecode::ValueType inferred_type = bytecode::ValueType::NIL;
    bool is_constant = false;
    std::variant<int64_t, double, std::string> constant_value;
    
    TraceNode() : type(TraceNodeType::bytecode), bytecode_offset(0), instruction_index(0), opcode(0) {}
    
    TraceNode(TraceNodeType t, size_t offset, size_t idx, uint8_t op)
        : type(t), bytecode_offset(offset), instruction_index(idx), opcode(op) {}
};

/**
 * @brief 轨迹 - 记录热路径的指令序列
 */
class Trace {
public:
    std::string id;                           // 轨迹唯一标识
    std::vector<TraceNode> nodes;             // 轨迹节点序列
    std::unordered_set<size_t> bytecode_covered;  // 覆盖的字节码偏移
    
    // 入口/出口信息
    size_t entry_offset = 0;                  // 入口字节码偏移
    std::vector<size_t> loop_exits;           // 循环退出点
    std::vector<size_t> side_exits;           // 侧出口点
    
    // 性能信息
    uint64_t execution_count = 0;
    uint64_t total_cycles = 0;
    double avg_cycles = 0.0;
    
    // 编译信息
    void* compiled_code = nullptr;
    size_t code_size = 0;
    bool is_valid = true;
    std::string invalid_reason;
    
    // 汇合点映射（用于trace stitching）
    std::unordered_map<size_t, size_t> merge_points;
    
    Trace() = default;
    
    explicit Trace(const std::string& trace_id) : id(trace_id) {}
    
    void add_node(const TraceNode& node) {
        nodes.push_back(node);
        bytecode_covered.insert(node.bytecode_offset);
    }
    
    size_t size() const { return nodes.size(); }
    
    bool contains_offset(size_t offset) const {
        return bytecode_covered.count(offset) > 0;
    }
    
    void invalidate(const std::string& reason) {
        is_valid = false;
        invalid_reason = reason;
    }
};

/**
 * @brief 轨迹缓冲区 - 记录执行轨迹
 */
class TraceBuffer {
public:
    static constexpr size_t MAX_BUFFER_SIZE = 1024;
    static constexpr size_t MAX_TRACE_LENGTH = 256;
    
private:
    std::vector<TraceNode> buffer_;
    size_t current_trace_start_ = 0;
    bool in_loop_ = false;
    size_t loop_entry_offset_ = 0;
    std::stack<size_t> loop_stack_;
    std::unordered_map<size_t, uint64_t> offset_counts_;
    
public:
    void start_recording();
    void record_node(const TraceNode& node);
    void record_loop_entry(size_t offset);
    void record_loop_exit(size_t offset);
    void record_side_exit(size_t offset, size_t target);
    
    bool is_recording() const { return !buffer_.empty(); }
    bool is_in_loop() const { return in_loop_; }
    size_t loop_entry_offset() const { return loop_entry_offset_; }
    
    Trace finish_trace(const std::string& id);
    void reset();
    void clear();
    
    const std::vector<TraceNode>& buffer() const { return buffer_; }
    const std::unordered_map<size_t, uint64_t>& offset_counts() const { return offset_counts_; }
};

/**
 * @brief 轨迹记录器 - 负责在运行时收集轨迹
 */
class TraceRecorder {
public:
    TraceRecorder(TracingJIT* jit);
    
    // 轨迹记录
    void on_execution(size_t offset, uint8_t opcode);
    void on_loop_begin(size_t offset);
    void on_loop_end(size_t offset);
    void on_branch(size_t offset, size_t target, bool taken);
    void on_call(size_t offset);
    void on_return(size_t offset);
    
    // 轨迹管理
    std::vector<Trace> get_completed_traces();
    void clear_traces();
    
    // 统计
    size_t total_recorded() const { return total_recorded_; }
    size_t total_traces() const { return completed_traces_.size(); }
    
private:
    TracingJIT* jit_;
    TraceBuffer trace_buffer_;
    std::vector<Trace> completed_traces_;
    size_t total_recorded_ = 0;
    
    bool should_start_trace();
    bool should_terminate_trace();
    void flush_buffer_to_trace();
};

/**
 * @brief 轨迹构建器 - 将原始记录转换为优化后的轨迹
 */
class TraceBuilder {
public:
    explicit TraceBuilder(TracingJIT* jit);
    
    // 构建轨迹
    std::unique_ptr<Trace> build(const Trace& raw_trace);
    
    // 轨迹优化
    void optimize_trace(Trace& trace);
    void remove_redundant_nodes(Trace& trace);
    void merge_constant_operations(Trace& trace);
    void identify_loop_exits(Trace& trace);
    void identify_merge_points(Trace& trace);
    
    // 类型推断
    void infer_types(Trace& trace);
    bytecode::ValueType infer_node_type(const TraceNode& node);
    
private:
    TracingJIT* jit_;
    
    // 辅助方法
    bool is_redundant(const TraceNode& node) const;
    bool can_merge_with_previous(const TraceNode& a, const TraceNode& b) const;
    bool is_loop_exit(const TraceNode& node) const;
    bool is_merge_point(size_t offset) const;
    
    // 优化 passes
    void apply_copy_propagation(Trace& trace);
    void apply_constant_folding(Trace& trace);
    void apply_dead_code_elimination(Trace& trace);
};

/**
 * @brief 轨迹编译器 - 将轨迹编译为机器码
 */
class TraceCompiler {
public:
    explicit TraceCompiler(TracingJIT* jit);
    
    // 编译轨迹
    CompilationResult compile_trace(Trace& trace);
    
    // 代码生成
    void* generate_code(const Trace& trace);
    size_t estimate_code_size(const Trace& trace);
    
    // 汇编优化
    void optimize_assembly(void* code, size_t size);
    
private:
    TracingJIT* jit_;
    
    // 编译辅助
    void emit_trace_prologue(Trace& trace);
    void emit_trace_node(const TraceNode& node);
    void emit_trace_epilogue(Trace& trace);
    void emit_trace_stitch(Trace& trace, size_t exit_idx, void* target);
    
    // 跳转目标解析
    void resolve_jump_targets(Trace& trace);
    size_t calculate_jump_offset(const TraceNode& from, const TraceNode& to);
    
    // 错误处理
    void handle_compilation_error(Trace& trace, const std::string& error);
};

/**
 * @brief Tracing JIT 主类
 */
class TracingJIT {
public:
    explicit TracingJIT(std::unique_ptr<JITCompiler> method_jit);
    ~TracingJIT();
    
    // 配置
    void configure(const JITConfig& config);
    const JITConfig& config() const { return config_; }
    
    // 编译入口
    CompilationResult compile(const bytecode::Function& func);
    void* get_compiled_code(const std::string& func_name);
    
    // 轨迹 JIT 特有接口
    void enable_tracing();
    void disable_tracing();
    bool is_tracing_enabled() const { return tracing_enabled_; }
    
    // 轨迹管理
    void record_execution(size_t offset, uint8_t opcode);
    std::vector<Trace> get_all_traces() const;
    void invalidate_traces_for_function(const std::string& func_name);
    
    // 编译调度
    void schedule_trace_compilation(Trace& trace);
    void compile_queued_traces();
    
    // 统计
    struct TracingStats {
        uint64_t total_executions = 0;
        uint64_t trace_attempts = 0;
        uint64_t trace_completed = 0;
        uint64_t trace_compiled = 0;
        uint64_t trace_invalidated = 0;
        size_t total_code_generated = 0;
        double avg_trace_length = 0.0;
    };
    
    TracingStats get_stats() const;
    void reset_stats();
    void print_stats() const;
    
    // 内部组件访问
    JITCompiler* method_jit() { return method_jit_.get(); }
    HotSpotDetector* hot_spot_detector() { return hot_spot_.get(); }
    TraceRecorder* recorder() { return recorder_.get(); }
    TraceBuilder* builder() { return builder_.get(); }
    TraceCompiler* compiler() { return compiler_.get(); }
    
    // 运行时函数
    void* get_runtime_function(const std::string& name);
    void register_runtime_function(const std::string& name, void* addr);
    
private:
    JITConfig config_;
    std::unique_ptr<JITCompiler> method_jit_;
    std::unique_ptr<HotSpotDetector> hot_spot_;
    
    // Tracing 组件
    std::unique_ptr<TraceRecorder> recorder_;
    std::unique_ptr<TraceBuilder> builder_;
    std::unique_ptr<TraceCompiler> compiler_;
    
    // 轨迹缓存
    std::unordered_map<std::string, std::vector<std::unique_ptr<Trace>>> function_traces_;
    std::queue<std::reference_wrapper<Trace>> trace_compilation_queue_;
    
    // 编译缓存
    std::unordered_map<std::string, void*> compiled_code_cache_;
    
    // 运行时函数表
    std::unordered_map<std::string, void*> runtime_functions_;
    
    // 状态
    bool tracing_enabled_ = true;
    bool compilation_in_progress_ = false;
    
    // 统计
    TracingStats stats_;
    
    // 互斥锁
    mutable std::mutex trace_mutex_;
    
    // 辅助方法
    void initialize_components();
    void setup_runtime_functions();
    std::string generate_trace_id(const std::string& func_name, size_t offset);
    bool should_compile_trace(const Trace& trace);
    void update_stats(const Trace& trace);
};

// 便捷函数
std::unique_ptr<TracingJIT> create_tracing_jit();
std::unique_ptr<TracingJIT> create_tracing_jit(const JITConfig& config);

} // namespace jit
} // namespace claw

#endif // CLAW_TRACING_JIT_H
