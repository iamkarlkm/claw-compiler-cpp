// TensorIR.cpp - Tensor Intermediate Representation Implementation
#include "tensor_ir.h"
#include <sstream>
#include <algorithm>
#include <stdexcept>

namespace claw {
namespace tensorir {

// ========== Range Implementation ==========

std::string Range::to_string() const {
    std::ostringstream oss;
    oss << "range(" << dim_list_to_string({min}) << ", " 
        << dim_list_to_string({extent}) << ")";
    return oss.str();
}

// ========== IterVar Implementation ==========

std::string IterVar::to_string() const {
    std::ostringstream oss;
    oss << name << " in " << range.to_string();
    switch (kind) {
        case VarKind::DataPar: oss << " (data parallel)"; break;
        case VarKind::ReducPar: oss << " (reduction parallel)"; break;
        case VarKind::Reduc: oss << " (reduction)"; break;
        case VarKind::Data: oss << " (data)"; break;
    }
    return oss.str();
}

// ========== StorageScope Implementation ==========

std::string to_string(StorageScope scope) {
    switch (scope) {
        case StorageScope::Local: return "local";
        case StorageScope::Shared: return "shared";
        case StorageScope::Global: return "global";
        case StorageScope::Constant: return "constant";
        case StorageScope::Texture: return "texture";
    }
    return "unknown";
}

// ========== Buffer Implementation ==========

std::string Buffer::to_string() const {
    std::ostringstream oss;
    oss << "buffer(" << name << ": " << elem_type 
        << "[" << dim_list_to_string(shape) << "], scope=" 
        << ")";
    return oss.str();
}

// ========== BufferAccess Implementation ==========

std::string BufferAccess::to_string() const {
    if (!buffer) return "buffer_access(null)";
    
    std::ostringstream oss;
    oss << buffer->name << "[";
    for (size_t i = 0; i < indices.size(); ++i) {
        if (i > 0) oss << ", ";
        std::visit([&oss](auto&& idx) {
            using T = std::decay_t<decltype(idx)>;
            if constexpr (std::is_same_v<T, IterVar*>) {
                oss << idx->name;
            } else {
                oss << idx;
            }
        }, indices[i]);
    }
    oss << "]";
    return oss.str();
}

// ========== TensorOp Implementation ==========

std::string TensorOp::to_string() const {
    std::ostringstream oss;
    oss << "TensorOp(" << name << ", kind=";
    switch (kind) {
        case OpKind::Compute: oss << "compute"; break;
        case OpKind::Matmul: oss << "matmul"; break;
        case OpKind::Conv2d: oss << "conv2d"; break;
        case OpKind::Pool2d: oss << "pool2d"; break;
        case OpKind::Reduce: oss << "reduce"; break;
        case OpKind::Cast: oss << "cast"; break;
        case OpKind::Broadcast: oss << "broadcast"; break;
        case OpKind::Slice: oss << "slice"; break;
    }
    oss << ")\n  inputs: ";
    for (size_t i = 0; i < inputs.size(); ++i) {
        if (i) oss << ", ";
        oss << (inputs[i] ? inputs[i]->name : "null");
    }
    oss << "\n  outputs: ";
    for (size_t i = 0; i < outputs.size(); ++i) {
        if (i) oss << ", ";
        oss << (outputs[i] ? outputs[i]->name : "null");
    }
    oss << "\n  iter_vars: ";
    for (size_t i = 0; i < iter_vars.size(); ++i) {
        if (i) oss << ", ";
        oss << (iter_vars[i] ? iter_vars[i]->name : "null");
    }
    return oss.str();
}

// ========== ComputeOp Implementation ==========

std::string ComputeOp::to_string() const {
    std::ostringstream oss;
    oss << "ComputeOp(" << name << ")\n";
    oss << "  body: " << body_expr << "\n";
    oss << TensorOp::to_string();
    return oss.str();
}

// ========== MatmulOp Implementation ==========

std::string MatmulOp::to_string() const {
    std::ostringstream oss;
    oss << "MatmulOp(" << name << ")";
    if (trans_a || trans_b) {
        oss << " (";
        if (trans_a) oss << "T";
        if (trans_b) oss << "T";
        oss << ")";
    }
    return oss.str();
}

// ========== Conv2dOp Implementation ==========

std::string Conv2dOp::to_string() const {
    std::ostringstream oss;
    oss << "Conv2dOp(" << name << ")\n";
    oss << "  strides: [";
    for (size_t i = 0; i < strides.size(); ++i) {
        if (i) oss << ", ";
        oss << strides[i];
    }
    oss << "]\n  padding: [";
    for (size_t i = 0; i < padding.size(); ++i) {
        if (i) oss << ", ";
        oss << padding[i];
    }
    oss << "]";
    return oss.str();
}

// ========== ReduceOp Implementation ==========

std::string ReduceOp::to_string() const {
    std::ostringstream oss;
    oss << "ReduceOp(" << name << ", type=";
    switch (reduce_type) {
        case ReduceType::Sum: oss << "sum"; break;
        case ReduceType::Mean: oss << "mean"; break;
        case ReduceType::Max: oss << "max"; break;
        case ReduceType::Min: oss << "min"; break;
        case ReduceType::Prod: oss << "prod"; break;
    }
    oss << ", axes=[";
    for (size_t i = 0; i < reduce_axes.size(); ++i) {
        if (i) oss << ", ";
        oss << reduce_axes[i];
    }
    oss << "])";
    return oss.str();
}

// ========== Schedule Primitives Implementation ==========

std::string Schedule::Prim::to_string() const {
    std::ostringstream oss;
    oss << "Schedule::";
    switch (type) {
        case PrimType::Tile:
            oss << "tile(";
            for (size_t i = 0; i < iter_names.size(); ++i) {
                if (i) oss << ", ";
                oss << iter_names[i];
            }
            if (std::holds_alternative<std::vector<int64_t>>(params)) {
                oss << ", [";
                auto& v = std::get<std::vector<int64_t>>(params);
                for (size_t i = 0; i < v.size(); ++i) {
                    if (i) oss << ", ";
                    oss << v[i];
                }
                oss << "]";
            }
            oss << ")";
            break;
        case PrimType::Fuse:
            oss << "fuse(" << iter_names[0] << ", " << iter_names[1] 
                << " -> " << std::get<std::string>(params) << ")";
            break;
        case PrimType::Split:
            oss << "split(" << iter_names[0] << ", factor=" 
                << std::get<int64_t>(params) << ")";
            break;
        case PrimType::Reorder:
            oss << "reorder(";
            for (size_t i = 0; i < iter_names.size(); ++i) {
                if (i) oss << ", ";
                oss << iter_names[i];
            }
            oss << ")";
            break;
        case PrimType::Vectorize:
            oss << "vectorize(" << iter_names[0] << ")";
            break;
        case PrimType::Unroll:
            oss << "unroll(" << iter_names[0] << ")";
            break;
        case PrimType::Parallel:
            oss << "parallel(" << iter_names[0] << ")";
            break;
        case PrimType::Bind:
            oss << "bind(" << iter_names[0] << ", " 
                << std::get<std::string>(params) << ")";
            break;
        case PrimType::CacheRead:
            oss << "cache_read(" << iter_names[0] << ", " 
                << std::get<std::string>(params) << ")";
            break;
        case PrimType::CacheWrite:
            oss << "cache_write(" << iter_names[0] << ", " 
                << std::get<std::string>(params) << ")";
            break;
        case PrimType::ComputeAt:
            oss << "compute_at(" << iter_names[0] << ")";
            break;
        case PrimType::Inline:
            oss << "inline(" << iter_names[0] << ")";
            break;
    }
    return oss.str();
}

void Schedule::tile(const std::vector<std::string>& axes, 
                    const std::vector<int64_t>& tile_sizes) {
    Prim p;
    p.type = PrimType::Tile;
    p.iter_names = axes;
    p.params = tile_sizes;
    prims.push_back(p);
}

void Schedule::fuse(const std::string& iter1, const std::string& iter2,
                    const std::string& fused) {
    Prim p;
    p.type = PrimType::Fuse;
    p.iter_names = {iter1, iter2};
    p.params = fused;
    prims.push_back(p);
}

void Schedule::split(const std::string& iter, int64_t factor) {
    Prim p;
    p.type = PrimType::Split;
    p.iter_names = {iter};
    p.params = factor;
    prims.push_back(p);
}

void Schedule::reorder(const std::vector<std::string>& order) {
    Prim p;
    p.type = PrimType::Reorder;
    p.iter_names = order;
    prims.push_back(p);
}

void Schedule::vectorize(const std::string& iter) {
    Prim p;
    p.type = PrimType::Vectorize;
    p.iter_names = {iter};
    prims.push_back(p);
}

void Schedule::unroll(const std::string& iter) {
    Prim p;
    p.type = PrimType::Unroll;
    p.iter_names = {iter};
    prims.push_back(p);
}

void Schedule::parallel(const std::string& iter) {
    Prim p;
    p.type = PrimType::Parallel;
    p.iter_names = {iter};
    prims.push_back(p);
}

void Schedule::bind(const std::string& iter, const std::string& thread_axis) {
    Prim p;
    p.type = PrimType::Bind;
    p.iter_names = {iter};
    p.params = thread_axis;
    prims.push_back(p);
}

void Schedule::cache_read(const std::string& buffer, const std::string& scope) {
    Prim p;
    p.type = PrimType::CacheRead;
    p.iter_names = {buffer};
    p.params = scope;
    prims.push_back(p);
}

void Schedule::cache_write(const std::string& buffer, const std::string& scope) {
    Prim p;
    p.type = PrimType::CacheWrite;
    p.iter_names = {buffer};
    p.params = scope;
    prims.push_back(p);
}

void Schedule::compute_at(Schedule& parent, const std::string& iter) {
    Prim p;
    p.type = PrimType::ComputeAt;
    p.iter_names = {iter};
    // Note: parent reference cannot be stored in variant directly
    prims.push_back(p);
}

void Schedule::inline_op() {
    Prim p;
    p.type = PrimType::Inline;
    prims.push_back(p);
}

std::string Schedule::to_string() const {
    std::ostringstream oss;
    oss << "Schedule for " << (op ? op->name : "null") << ":\n";
    for (const auto& p : prims) {
        oss << "  " << p.to_string() << "\n";
    }
    return oss.str();
}

// ========== TensorIRModule Implementation ==========

Buffer* TensorIRModule::declare_buffer(const std::string& name,
                                       const std::string& elem_type,
                                       const DimList& shape,
                                       StorageScope scope) {
    auto buf = std::make_unique<Buffer>(name, elem_type, shape);
    buf->scope = scope;
    Buffer* ptr = buf.get();
    buffers.push_back(std::move(buf));
    return ptr;
}

TensorOp* TensorIRModule::create_compute(const std::string& name,
                                         const std::vector<Buffer*>& inputs,
                                         const std::vector<IterVar*>& iter_vars,
                                         const std::string& body_expr) {
    auto op = std::make_unique<ComputeOp>(name);
    op->inputs = inputs;
    op->iter_vars = iter_vars;
    op->body_expr = body_expr;
    
    // Create output buffer based on iter_vars
    DimList output_shape;
    for (auto* iter : iter_vars) {
        if (iter->kind == IterVar::VarKind::DataPar) {
            output_shape.push_back(iter->range.extent);
        }
    }
    
    if (!inputs.empty() && !inputs[0]->shape.empty()) {
        // Infer output shape from first input if available
        output_shape = inputs[0]->shape;
    }
    
    std::string out_name = name + "_out";
    auto* out_buf = declare_buffer(out_name, inputs.empty() ? "f32" : inputs[0]->elem_type,
                                    output_shape);
    op->outputs.push_back(out_buf);
    
    TensorOp* ptr = op.get();
    operations.push_back(std::move(op));
    return ptr;
}

TensorOp* TensorIRModule::create_matmul(Buffer* a, Buffer* b, Buffer* c,
                                        bool trans_a, bool trans_b) {
    auto op = std::make_unique<MatmulOp>();
    op->inputs = {a, b};
    op->outputs = {c};
    op->trans_a = trans_a;
    op->trans_b = trans_b;
    
    TensorOp* ptr = op.get();
    operations.push_back(std::move(op));
    return ptr;
}

TensorOp* TensorIRModule::create_conv2d(Buffer* input, Buffer* weight, Buffer* output,
                                        const std::vector<int64_t>& strides,
                                        const std::vector<int64_t>& padding) {
    auto op = std::make_unique<Conv2dOp>();
    op->inputs = {input, weight};
    op->outputs = {output};
    op->strides = strides;
    op->padding = padding;
    
    TensorOp* ptr = op.get();
    operations.push_back(std::move(op));
    return ptr;
}

Schedule& TensorIRModule::get_schedule(TensorOp* op) {
    if (schedules.find(op) == schedules.end()) {
        schedules[op] = std::make_unique<Schedule>(op);
    }
    return *schedules[op].get();
}

std::string TensorIRModule::to_string() const {
    std::ostringstream oss;
    oss << "=== TensorIR Module: " << name << " ===\n\n";
    
    oss << "Buffers:\n";
    for (const auto& buf : buffers) {
        oss << "  " << buf->to_string() << "\n";
    }
    
    oss << "\nOperations:\n";
    for (const auto& op : operations) {
        oss << "  " << op->to_string() << "\n";
        
        // Find and print schedule if exists
        auto it = schedules.find(op.get());
        if (it != schedules.end()) {
            oss << it->second->to_string() << "\n";
        }
    }
    
    return oss.str();
}

// ========== TensorIRBuilder Implementation ==========

TensorIRBuilder::TensorIRBuilder(const std::string& name) {
    module.name = name;
}

Buffer* TensorIRBuilder::buffer(const std::string& name, const std::string& type,
                                const std::vector<int64_t>& shape,
                                StorageScope scope) {
    DimList dims;
    for (auto d : shape) {
        dims.push_back(d);
    }
    auto* buf = module.declare_buffer(name, type, dims, scope);
    buffer_map[name] = buf;
    return buf;
}

IterVar* TensorIRBuilder::var(const std::string& name, IterVar::VarKind kind, 
                              Range range) {
    auto* iter = new IterVar(name, kind, range);
    iter_var_map[name] = iter;
    return iter;
}

IterVar* TensorIRBuilder::var(const std::string& name, int64_t extent) {
    return var(name, IterVar::VarKind::DataPar, Range(0, extent));
}

ComputeOp* TensorIRBuilder::compute(const std::string& name,
                                    const std::vector<Buffer*>& inputs,
                                    const std::vector<IterVar*>& iters,
                                    const std::string& body) {
    auto* op = module.create_compute(name, inputs, iters, body);
    return static_cast<ComputeOp*>(op);
}

MatmulOp* TensorIRBuilder::matmul(Buffer* a, Buffer* b, Buffer* c) {
    auto* op = module.create_matmul(a, b, c);
    return static_cast<MatmulOp*>(op);
}

Conv2dOp* TensorIRBuilder::conv2d(Buffer* input, Buffer* weight, Buffer* output,
                                  const std::vector<int64_t>& strides,
                                  const std::vector<int64_t>& padding) {
    auto* op = module.create_conv2d(input, weight, output, strides, padding);
    return static_cast<Conv2dOp*>(op);
}

Schedule& TensorIRBuilder::schedule(TensorOp* op) {
    return module.get_schedule(op);
}

// ========== Utility Functions ==========

std::string dim_list_to_string(const DimList& dims) {
    std::ostringstream oss;
    for (size_t i = 0; i < dims.size(); ++i) {
        if (i > 0) oss << ", ";
        std::visit([&oss](auto&& d) {
            using T = std::decay_t<decltype(d)>;
            if constexpr (std::is_same_v<T, int64_t>) {
                oss << d;
            } else {
                oss << d;
            }
        }, dims[i]);
    }
    return oss.str();
}

DimVar simplify_dim(const DimVar& d) {
    // Placeholder for symbolic computation
    return d;
}

bool is_equal_dim(const DimVar& a, const DimVar& b) {
    if (a.index() != b.index()) return false;
    if (std::holds_alternative<int64_t>(a)) {
        return std::get<int64_t>(a) == std::get<int64_t>(b);
    }
    return std::get<std::string>(a) == std::get<std::string>(b);
}

DimVar eval_dim_expr(const std::string& expr, 
                     const std::unordered_map<std::string, int64_t>& env) {
    // Try to parse as integer first
    try {
        size_t pos;
        int64_t val = std::stoll(expr, &pos);
        if (pos == expr.size()) {
            return val;
        }
    } catch (...) {}
    
    // Otherwise treat as symbolic variable
    auto it = env.find(expr);
    if (it != env.end()) {
        return it->second;
    }
    return expr;
}

} // namespace tensorir
} // namespace claw
