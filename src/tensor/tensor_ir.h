// tensor/tensor_ir.h - TensorIR: 张量中间表示和调度抽象
// 基于 Apache TVM TensorIR 设计

#ifndef CLAW_TENSOR_IR_H
#define CLAW_TENSOR_IR_H

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <optional>
#include <functional>
#include "../common/common.h"
#include "../type/type_system.h"

namespace claw {
namespace tensor {

// ============================================================================
// TensorIR 节点类型
// ============================================================================

enum class TensorIROp {
    // 张量运算
    TLOAD, TSTORE, TMATMUL, TCONV2D, TTRANSPOSE, TRESHAPE, TBROADCAST,
    // 归约
    TSUM, TMEAN, TMAX, TMIN, TARGMAX, TARGMIN, TPOOL2D,
    // 元素级
    TADD, TSUB, TMUL, TDIV,
    // 控制流
    LOOP, BLOCK, CALL
};

// ============================================================================
// 索引表达式
// ============================================================================

struct IndexExpr;
using IndexExprPtr = std::shared_ptr<IndexExpr>;

struct IndexExpr {
    virtual ~IndexExpr() = default;
    virtual std::string to_string() const = 0;
    virtual bool is_static() const { return false; }
    virtual std::optional<int64_t> static_value() const { return std::nullopt; }
};

struct VarIndex : IndexExpr {
    std::string name;
    std::optional<int64_t> bound;
    explicit VarIndex(const std::string& n, int64_t b = -1) : name(n), bound(b >= 0 ? std::optional<int64_t>(b) : std::nullopt) {}
    std::string to_string() const override { return name + (bound ? "[" + std::to_string(*bound) + "]" : ""); }
};

struct ConstIndex : IndexExpr {
    int64_t value;
    explicit ConstIndex(int64_t v) : value(v) {}
    std::string to_string() const override { return std::to_string(value); }
    bool is_static() const override { return true; }
    std::optional<int64_t> static_value() const override { return value; }
};

// ============================================================================
// TensorIR 节点
// ============================================================================

struct TensorIRNode {
    TensorIROp op;
    claw::type::TypePtr result_type;
    SourceSpan span;
    TensorIRNode(TensorIROp o, claw::type::TypePtr t, SourceSpan s) : op(o), result_type(t), span(s) {}
    virtual ~TensorIRNode() = default;
    virtual std::string to_string() const = 0;
};

struct MatmulNode : TensorIRNode {
    std::string a_buf, b_buf, c_buf;
    std::vector<IndexExprPtr> a_idx, b_idx, c_idx;
    MatmulNode(const std::string& a, const std::string& b, const std::string& c,
               const std::vector<IndexExprPtr>& ai, const std::vector<IndexExprPtr>& bi,
               const std::vector<IndexExprPtr>& ci, claw::type::TypePtr t, SourceSpan s)
        : TensorIRNode(TensorIROp::TMATMUL, t, s), a_buf(a), b_buf(b), c_buf(c), a_idx(ai), b_idx(bi), c_idx(ci) {}
    std::string to_string() const override {
        return "TMATMUL " + c_buf + " += " + a_buf + " * " + b_buf;
    }
};

struct Conv2DNode : TensorIRNode {
    std::string in_buf, ker_buf, out_buf;
    std::vector<int64_t> stride, padding;
    Conv2DNode(const std::string& in, const std::string& ker, const std::string& out,
               const std::vector<int64_t>& str, const std::vector<int64_t>& pad,
               claw::type::TypePtr t, SourceSpan s)
        : TensorIRNode(TensorIROp::TCONV2D, t, s), in_buf(in), ker_buf(ker), out_buf(out), stride(str), padding(pad) {}
    std::string to_string() const override {
        return "TCONV2D " + out_buf + " = conv2d(" + in_buf + ", " + ker_buf + ")";
    }
};

enum class RedOp { SUM, MEAN, MAX, MIN };

struct ReductionNode : TensorIRNode {
    RedOp op;
    std::string src_buf, dst_buf;
    std::vector<IndexExprPtr> src_idx, dst_idx;
    std::vector<int> axes;
    ReductionNode(RedOp o, const std::string& src, const std::string& dst,
                  const std::vector<IndexExprPtr>& si, const std::vector<IndexExprPtr>& di,
                  const std::vector<int>& ax, claw::type::TypePtr t, SourceSpan s)
        : TensorIRNode(TensorIROp::TSUM, t, s), op(o), src_buf(src), dst_buf(dst), src_idx(si), dst_idx(di), axes(ax) {}
    std::string to_string() const override {
        return "TREDUCE " + dst_buf + " <- " + src_buf;
    }
};

struct BlockNode : TensorIRNode {
    std::string name;
    std::vector<std::string> iters;
    std::vector<IndexExprPtr> domains;
    std::vector<std::shared_ptr<TensorIRNode>> body;
    bool is_parallel = false, is_unrolled = false, is_vectorized = false;
    std::vector<int> tile_factors;
    
    BlockNode(const std::string& n, claw::type::TypePtr t, SourceSpan s)
        : TensorIRNode(TensorIROp::BLOCK, t, s), name(n) {}
    std::string to_string() const override {
        return "block " + name + " { iters: " + std::to_string(iters.size()) + " }";
    }
};

struct ComputeNode : TensorIRNode {
    std::string name;
    std::vector<std::string> args;
    std::vector<std::pair<std::string, std::vector<IndexExprPtr>>> buffers;
    std::shared_ptr<BlockNode> body;
    
    ComputeNode(const std::string& n, const std::vector<std::string>& a,
                const std::vector<std::pair<std::string, std::vector<IndexExprPtr>>>& buf,
                std::shared_ptr<BlockNode> b, claw::type::TypePtr t, SourceSpan s)
        : TensorIRNode(TensorIROp::CALL, t, s), name(n), args(a), buffers(buf), body(b) {}
    std::string to_string() const override {
        return "compute " + name + "(args: " + std::to_string(args.size()) + ")";
    }
};

// ============================================================================
// TensorIR Builder
// ============================================================================

class TensorIRBuilder {
public:
    IndexExprPtr var(const std::string& n, int64_t b = -1) { return std::make_shared<VarIndex>(n, b); }
    IndexExprPtr constant(int64_t v) { return std::make_shared<ConstIndex>(v); }
    
    std::shared_ptr<MatmulNode> create_matmul(const std::string& a, const std::string& b, const std::string& c,
                                               const std::vector<IndexExprPtr>& ai,
                                               const std::vector<IndexExprPtr>& bi,
                                               const std::vector<IndexExprPtr>& ci,
                                               claw::type::TypePtr t, SourceSpan s) {
        return std::make_shared<MatmulNode>(a, b, c, ai, bi, ci, t, s);
    }
    
    std::shared_ptr<Conv2DNode> create_conv2d(const std::string& in, const std::string& ker, const std::string& out,
                                               const std::vector<int64_t>& str, const std::vector<int64_t>& pad,
                                               claw::type::TypePtr t, SourceSpan s) {
        return std::make_shared<Conv2DNode>(in, ker, out, str, pad, t, s);
    }
    
    std::shared_ptr<ReductionNode> create_reduction(RedOp op, const std::string& src, const std::string& dst,
                                                     const std::vector<IndexExprPtr>& si,
                                                     const std::vector<IndexExprPtr>& di,
                                                     const std::vector<int>& ax,
                                                     claw::type::TypePtr t, SourceSpan s) {
        return std::make_shared<ReductionNode>(op, src, dst, si, di, ax, t, s);
    }
    
    std::shared_ptr<BlockNode> create_block(const std::string& name,
                                             const std::vector<std::string>& iters,
                                             const std::vector<IndexExprPtr>& domains,
                                             const std::vector<std::shared_ptr<TensorIRNode>>& body,
                                             claw::type::TypePtr t, SourceSpan s) {
        auto block = std::make_shared<BlockNode>(name, t, s);
        block->iters = iters;
        block->domains = domains;
        block->body = body;
        return block;
    }
    
    std::shared_ptr<ComputeNode> create_compute(const std::string& name,
                                                 const std::vector<std::string>& args,
                                                 const std::vector<std::pair<std::string, std::vector<IndexExprPtr>>>& buffers,
                                                 std::shared_ptr<BlockNode> body,
                                                 claw::type::TypePtr t, SourceSpan s) {
        return std::make_shared<ComputeNode>(name, args, buffers, body, t, s);
    }
};

// ============================================================================
// TensorIR Scheduler
// ============================================================================

struct ScheduleStats {
    size_t num_tiles = 0, num_parallel = 0, num_unrolls = 0, num_vectorize = 0;
};

class TensorIRScheduler {
public:
    explicit TensorIRScheduler(std::shared_ptr<ComputeNode> compute) : compute_(compute) {}
    
    void tile(const std::string& var, const std::vector<int>& factors) {
        stats_.num_tiles++;
        if (compute_->body) {
            compute_->body->tile_factors = factors;
        }
    }
    void parallel(const std::string& var) {
        stats_.num_parallel++;
        if (compute_->body) compute_->body->is_parallel = true;
    }
    void unroll(const std::string& var) {
        stats_.num_unrolls++;
        if (compute_->body) compute_->body->is_unrolled = true;
    }
    void vectorize(const std::string& var) {
        stats_.num_vectorize++;
        if (compute_->body) compute_->body->is_vectorized = true;
    }
    
    ScheduleStats get_stats() const { return stats_; }
    std::string to_string() const {
        return "Schedule: tiles=" + std::to_string(stats_.num_tiles) + 
               ", parallel=" + std::to_string(stats_.num_parallel) +
               ", unroll=" + std::to_string(stats_.num_unrolls) +
               ", vectorize=" + std::to_string(stats_.num_vectorize);
    }
    
private:
    std::shared_ptr<ComputeNode> compute_;
    ScheduleStats stats_;
};

// ============================================================================
// CodeGen
// ============================================================================

class TensorIRCodegen {
public:
    std::string generate_cpu(std::shared_ptr<ComputeNode> compute) {
        std::string code = "void " + compute->name + "_cpu(";
        for (size_t i = 0; i < compute->args.size(); ++i) {
            if (i) code += ", ";
            code += "float* " + compute->args[i];
        }
        code += ") {\n";
        code += "  // TensorIR compute: " + compute->name + "\n";
        code += "}\n";
        return code;
    }
    
    std::string generate_cuda(std::shared_ptr<ComputeNode> compute) {
        std::string code = "__global__ void " + compute->name + "_cuda(";
        for (size_t i = 0; i < compute->args.size(); ++i) {
            if (i) code += ", ";
            code += "float* " + compute->args[i];
        }
        code += ") {\n";
        code += "  int idx = blockIdx.x * blockDim.x + threadIdx.x;\n";
        code += "}\n";
        return code;
    }
};

} // namespace tensor
} // namespace claw

#endif
