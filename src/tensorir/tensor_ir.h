// TensorIR.h - Tensor Intermediate Representation
#ifndef CLAW_TENSOR_IR_H
#define CLAW_TENSOR_IR_H

#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace claw {
namespace tensorir {

// ========== Type Definitions ==========

using DimVar = std::variant<int64_t, std::string>;  // concrete or symbolic
using DimList = std::vector<DimVar>;

// ========== Loop Iterators ==========

struct Range {
    DimVar min;
    DimVar extent;
    
    Range() : min(0), extent(1) {}
    Range(DimVar m, DimVar e) : min(m), extent(e) {}
    
    std::string to_string() const;
};

// 迭代变量（循环迭代器）
struct IterVar {
    enum class VarKind {
        DataPar,    // 数据并行维度
        ReducPar,   // 归约维度
        Reduc,      // 归约变量
        Data,       // 数据索引
    };
    
    std::string name;
    VarKind kind;
    Range range;
    
    IterVar(const std::string& n, VarKind k, Range r)
        : name(n), kind(k), range(r) {}
    
    std::string to_string() const;
};

// ========== Storage Scopes ==========

enum class StorageScope {
    Local,      // 寄存器/栈
    Shared,     // SMEM (GPU)
    Global,     // 全局内存
    Constant,   // 常量内存
    Texture,    // 纹理内存 (GPU)
};

std::string to_string(StorageScope scope);

// ========== Tensor Access ==========

struct Buffer {
    std::string name;
    std::string elem_type;  // "f32", "i32", etc.
    DimList shape;
    StorageScope scope;
    
    Buffer() : elem_type("f32"), scope(StorageScope::Local) {}
    Buffer(const std::string& n, const std::string& t, const DimList& s)
        : name(n), elem_type(t), shape(s), scope(StorageScope::Local) {}
    
    std::string to_string() const;
};

// ========== Buffer Access Patterns ==========

struct BufferAccess {
    Buffer* buffer;
    std::vector<std::variant<IterVar*, int64_t>> indices;
    
    BufferAccess() : buffer(nullptr) {}
    BufferAccess(Buffer* buf) : buffer(buf) {}
    
    std::string to_string() const;
};

// ========== TensorIR Operations ==========

// 基类
struct TensorOp {
    enum class OpKind {
        Compute,     // 通用计算
        Matmul,      // 矩阵乘法
        Conv2d,      // 卷积
        Pool2d,      // 池化
        Reduce,      // 归约
        Cast,        // 类型转换
        Broadcast,   // 广播
        Slice,       // 切片
    };
    
    OpKind kind;
    std::string name;
    std::vector<Buffer*> inputs;
    std::vector<Buffer*> outputs;
    std::vector<IterVar*> iter_vars;
    std::vector<BufferAccess> body;  // 计算体
    
    TensorOp(OpKind k, const std::string& n) : kind(k), name(n) {}
    virtual ~TensorOp() = default;
    
    virtual std::string to_string() const;
};

// Compute 操作 - 通用张量计算
struct ComputeOp : public TensorOp {
    std::string body_expr;  // 计算表达式
    
    ComputeOp(const std::string& name) 
        : TensorOp(OpKind::Compute, name) {}
    
    std::string to_string() const override;
};

// Matmul 操作
struct MatmulOp : public TensorOp {
    // A[M, K] @ B[K, N] -> C[M, N]
    bool trans_a = false;
    bool trans_b = false;
    
    MatmulOp() : TensorOp(OpKind::Matmul, "matmul") {}
    
    std::string to_string() const override;
};

// Conv2d 操作
struct Conv2dOp : TensorOp {
    // NCHW: input[N, C, H, W], weight[OC, IC, KH, KW] -> output[N, OC, H', W']
    std::vector<int64_t> strides;
    std::vector<int64_t> padding;
    std::vector<int64_t> dilation;
    int groups = 1;
    
    Conv2dOp() : TensorOp(OpKind::Conv2d, "conv2d") {
        strides = {1, 1};
        padding = {0, 0, 0, 0};
        dilation = {1, 1};
    }
    
    std::string to_string() const override;
};

// Reduce 操作
struct ReduceOp : TensorOp {
    enum class ReduceType {
        Sum,
        Mean,
        Max,
        Min,
        Prod,
    };
    
    ReduceType reduce_type = ReduceType::Sum;
    std::vector<std::string> reduce_axes;  // 归约的轴
    
    ReduceOp() : TensorOp(OpKind::Reduce, "reduce") {}
    
    std::string to_string() const override;
};

// ========== Schedule Primitives ==========

struct Schedule {
    // 调度原语操作
    enum class PrimType {
        Tile,         // 循环平铺
        Fuse,         // 循环融合
        Split,        // 循环分裂
        Reorder,      // 循环重排
        Vectorize,    // 向量化
        Unroll,       // 循环展开
        Parallel,     // 并行化
        Bind,         // 绑定到硬件线程
        CacheRead,    // 缓存读
        CacheWrite,   // 缓存写
        ComputeAt,    // 在某处计算
        Inline,       // 内联
    };
    
    struct Prim {
        PrimType type;
        std::vector<std::string> iter_names;  // 涉及的迭代变量
        std::variant<std::vector<int64_t>, std::string, int64_t> params;
        
        std::string to_string() const;
    };
    
    std::vector<Prim> prims;
    TensorOp* op;
    
    Schedule(TensorOp* operation) : op(operation) {}
    
    void tile(const std::vector<std::string>& axes, 
              const std::vector<int64_t>& tile_sizes);
    void fuse(const std::string& iter1, const std::string& iter2,
              const std::string& fused);
    void split(const std::string& iter, int64_t factor);
    void reorder(const std::vector<std::string>& order);
    void vectorize(const std::string& iter);
    void unroll(const std::string& iter);
    void parallel(const std::string& iter);
    void bind(const std::string& iter, const std::string& thread_axis);
    void cache_read(const std::string& buffer, const std::string& scope);
    void cache_write(const std::string& buffer, const std::string& scope);
    void compute_at(Schedule& parent, const std::string& iter);
    void inline_op();
    
    std::string to_string() const;
};

// ========== TensorIR Module ==========

struct TensorIRModule {
    std::string name;
    std::vector<std::unique_ptr<Buffer>> buffers;
    std::vector<std::unique_ptr<TensorOp>> operations;
    std::unordered_map<TensorOp*, std::unique_ptr<Schedule>> schedules;
    
    Buffer* declare_buffer(const std::string& name, 
                          const std::string& elem_type,
                          const DimList& shape,
                          StorageScope scope = StorageScope::Local);
    
    TensorOp* create_compute(const std::string& name,
                            const std::vector<Buffer*>& inputs,
                            const std::vector<IterVar*>& iter_vars,
                            const std::string& body_expr);
    
    TensorOp* create_matmul(Buffer* a, Buffer* b, Buffer* c,
                           bool trans_a = false, bool trans_b = false);
    
    TensorOp* create_conv2d(Buffer* input, Buffer* weight, Buffer* output,
                           const std::vector<int64_t>& strides,
                           const std::vector<int64_t>& padding);
    
    Schedule& get_schedule(TensorOp* op);
    
    std::string to_string() const;
};

// ========== TensorIR Builder ==========

class TensorIRBuilder {
public:
    TensorIRModule module;
    std::unordered_map<std::string, Buffer*> buffer_map;
    std::unordered_map<std::string, IterVar*> iter_var_map;
    
    TensorIRBuilder(const std::string& name = "module");
    
    // Buffer 工厂方法
    Buffer* buffer(const std::string& name, const std::string& type,
                   const std::vector<int64_t>& shape,
                   StorageScope scope = StorageScope::Local);
    
    // 迭代变量工厂方法
    IterVar* var(const std::string& name, IterVar::VarKind kind, Range range);
    IterVar* var(const std::string& name, int64_t extent);
    
    // 快捷方法：创建 [0, n) 的迭代变量
    IterVar* i(int64_t n) { 
        return var("i_" + std::to_string(iter_var_map.size()), 
                   IterVar::VarKind::DataPar, Range(0, n)); 
    }
    IterVar* j(int64_t n) { 
        return var("j_" + std::to_string(iter_var_map.size()), 
                   IterVar::VarKind::DataPar, Range(0, n)); 
    }
    IterVar* k(int64_t n) { 
        return var("k_" + std::to_string(iter_var_map.size()), 
                   IterVar::VarKind::Reduc, Range(0, n)); 
    }
    
    // 创建操作
    ComputeOp* compute(const std::string& name,
                       const std::vector<Buffer*>& inputs,
                       const std::vector<IterVar*>& iters,
                       const std::string& body);
    
    MatmulOp* matmul(Buffer* a, Buffer* b, Buffer* c);
    
    Conv2dOp* conv2d(Buffer* input, Buffer* weight, Buffer* output,
                    const std::vector<int64_t>& strides = {1, 1},
                    const std::vector<int64_t>& padding = {0, 0});
    
    // 调度
    Schedule& schedule(TensorOp* op);
    
    // 输出
    std::string dump() const { return module.to_string(); }
};

// ========== Utility Functions ==========

std::string dim_list_to_string(const DimList& dims);
DimVar simplify_dim(const DimVar& d);
bool is_equal_dim(const DimVar& a, const DimVar& b);
DimVar eval_dim_expr(const std::string& expr, 
                     const std::unordered_map<std::string, int64_t>& env);

} // namespace tensorir
} // namespace claw

#endif // CLAW_TENSOR_IR_H
