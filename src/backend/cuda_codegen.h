// backend/cuda_codegen.h - CUDA 代码生成器
// 将 TensorIR (claw::tensorir) + Schedule 转换为 CUDA C++ 代码

#ifndef CLAW_CUDA_CODEGEN_H
#define CLAW_CUDA_CODEGEN_H

#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include "../tensorir/tensor_ir.h"

namespace claw {
namespace backend {

// ============================================================================
// CUDA 代码生成配置
// ============================================================================

struct CUDACodegenConfig {
    int max_threads_per_block = 256;
    int max_threads_per_warp = 32;
    int max_shared_memory_per_block = 48 * 1024;  // 48KB (sm_70+)
    int max_registers_per_thread = 255;
    
    bool use_shared_memory = true;
    bool use_tensor_cores = false;
    bool use_cooperative_groups = false;
    
    int target_compute_capability_major = 7;
    int target_compute_capability_minor = 0;
    std::string target_arch = "sm_70";
};

// ============================================================================
// CUDA 内核元数据
// ============================================================================

struct CUDAKernelMeta {
    std::string kernel_name;
    std::vector<std::string> arg_names;
    std::vector<std::string> arg_types;
    std::vector<std::vector<int64_t>> arg_shapes;
    
    int block_dim_x = 1, block_dim_y = 1, block_dim_z = 1;
    int grid_dim_x = 1, grid_dim_y = 1, grid_dim_z = 1;
    int shared_memory_size = 0;
    int num_registers = 0;
};

// ============================================================================
// 循环到 CUDA 线程映射
// ============================================================================

struct ThreadMapping {
    std::string iter_name;
    std::string mapping_type;  // "blockIdx.x", "threadIdx.x", "serial"
    int tile_size = 1;
    int unroll_factor = 1;
    bool vectorized = false;
    int vector_width = 1;
};

// ============================================================================
// 共享内存缓存信息
// ============================================================================

struct SharedMemoryCache {
    std::string buffer_name;
    std::vector<int64_t> tile_shape;
    std::string element_type = "float";
    size_t total_size_bytes = 0;
    std::string shared_var_name;
};

// ============================================================================
// CUDA 内核代码生成器
// ============================================================================

class CUDAKernelCodegen {
public:
    explicit CUDAKernelCodegen(const CUDACodegenConfig& config = {});
    
    // 生成单个内核
    std::string generate_kernel(const tensorir::TensorOp& op,
                                 const tensorir::Schedule& schedule);
    
    CUDAKernelMeta get_kernel_meta() const { return kernel_meta_; }
    
private:
    CUDACodegenConfig config_;
    CUDAKernelMeta kernel_meta_;
    std::stringstream code_;
    int indent_level_ = 0;
    
    std::unordered_map<std::string, ThreadMapping> thread_mappings_;
    std::vector<SharedMemoryCache> shared_caches_;
    std::unordered_set<std::string> generated_loops_;
    
    void emit_line(const std::string& line);
    void emit_indent();
    void increase_indent() { indent_level_++; }
    void decrease_indent() { indent_level_--; }
    
    void generate_kernel_signature(const tensorir::TensorOp& op);
    void generate_shared_memory_declarations();
    void generate_loop_nest(const tensorir::TensorOp& op,
                            const tensorir::Schedule& schedule);
    void generate_loop(const std::string& iter_name,
                        int64_t extent,
                        const ThreadMapping& mapping);
    void generate_loop_body(const tensorir::TensorOp& op);
    void generate_node(const tensorir::TensorOp& op);
    void generate_matmul(const tensorir::MatmulOp& op);
    void generate_conv2d(const tensorir::Conv2dOp& op);
    void generate_reduction(const tensorir::ReduceOp& op);
    void generate_compute(const tensorir::ComputeOp& op);
    
    std::string generate_buffer_access(const tensorir::BufferAccess& access);
    std::string generate_index_expr(const std::variant<tensorir::IterVar*, int64_t>& idx);
    
    void compute_thread_mappings(const tensorir::TensorOp& op,
                                  const tensorir::Schedule& schedule);
    void analyze_shared_memory(const tensorir::TensorOp& op,
                                const tensorir::Schedule& schedule);
    
    std::string get_element_type(const tensorir::Buffer* buf) const;
    int64_t compute_product(const std::vector<int64_t>& dims) const;
    int64_t get_iter_extent(const tensorir::IterVar* iter) const;
};

// ============================================================================
// CUDA 主机端代码生成器
// ============================================================================

class CUDAHostCodegen {
public:
    explicit CUDAHostCodegen(const CUDACodegenConfig& config = {});
    
    std::string generate_host_wrapper(const tensorir::TensorOp& op,
                                       const CUDAKernelMeta& kernel_meta);
    std::string generate_memory_management(const tensorir::TensorOp& op);
    std::string generate_host_module(const tensorir::TensorIRModule& module,
                                      const std::vector<CUDAKernelMeta>& kernel_metas);
    
private:
    CUDACodegenConfig config_;
    std::stringstream code_;
    int indent_level_ = 0;
    
    void emit_line(const std::string& line);
    void emit_indent();
    void increase_indent() { indent_level_++; }
    void decrease_indent() { indent_level_--; }
    
    void generate_error_check_macro();
    void generate_device_alloc(const std::string& name,
                                const std::string& type,
                                size_t size);
    void generate_memcpy_h2d(const std::string& host_ptr,
                              const std::string& device_ptr,
                              size_t size);
    void generate_memcpy_d2h(const std::string& device_ptr,
                              const std::string& host_ptr,
                              size_t size);
    void generate_kernel_launch(const CUDAKernelMeta& meta,
                                 const std::vector<std::string>& arg_names);
    void generate_device_free(const std::string& name);
    std::string get_element_type(const tensorir::Buffer* buf) const;
};

// ============================================================================
// 完整 CUDA 代码生成器
// ============================================================================

class CUDACodeGenerator {
public:
    explicit CUDACodeGenerator(const CUDACodegenConfig& config = {});
    
    std::string generate_module(const tensorir::TensorIRModule& module);
    std::string generate_kernel_with_host(const tensorir::TensorOp& op,
                                           const tensorir::Schedule& schedule);
    
    const std::vector<CUDAKernelMeta>& get_kernel_metas() const { return kernel_metas_; }
    
private:
    CUDACodegenConfig config_;
    CUDAKernelCodegen kernel_codegen_;
    CUDAHostCodegen host_codegen_;
    std::vector<CUDAKernelMeta> kernel_metas_;
};

// ============================================================================
// 便捷函数
// ============================================================================

std::string generate_cuda_code(const tensorir::TensorOp& op,
                                const tensorir::Schedule& schedule,
                                const CUDACodegenConfig& config = {});

std::string generate_cuda_launch_code(const CUDAKernelMeta& meta,
                                       const std::vector<std::string>& arg_names);

} // namespace backend
} // namespace claw

#endif // CLAW_CUDA_CODEGEN_H
