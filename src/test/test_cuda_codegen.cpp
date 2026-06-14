// test/test_cuda_codegen.cpp - CUDA 代码生成器测试
// Phase 24: CUDA Backend Code Generation

#include <iostream>
#include <stdexcept>
#include "backend/cuda_codegen.h"
#include "tensorir/tensor_ir.h"

using namespace claw;
using namespace claw::backend;
using namespace claw::tensorir;

// 简单的测试辅助宏
#define TEST(name) void test_##name()
#define RUN_TEST(name) \
    do { \
        std::cout << "  Running " << #name << " ... " << std::flush; \
        try { \
            test_##name(); \
            std::cout << "PASSED\n"; \
            passed++; \
        } catch (const std::exception& e) { \
            std::cout << "FAILED: " << e.what() << "\n"; \
            failed++; \
        } \
    } while(0)

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            throw std::runtime_error("Assertion failed: " #cond); \
        } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            throw std::runtime_error("Assertion failed: " #a " == " #b); \
        } \
    } while(0)

// ============================================================================
// 辅助函数：创建简单的 MatMul TensorOp
// ============================================================================

std::unique_ptr<TensorIRModule> create_matmul_module() {
    auto module = std::make_unique<TensorIRModule>();
    module->name = "matmul_test";
    
    auto* A = module->declare_buffer("A", "f32", {1024, 1024});
    auto* B = module->declare_buffer("B", "f32", {1024, 1024});
    auto* C = module->declare_buffer("C", "f32", {1024, 1024});
    
    auto* matmul = module->create_matmul(A, B, C);
    
    // Add iter_vars to the matmul op for testing
    auto* i = new IterVar("i", IterVar::VarKind::DataPar, Range(0, 1024));
    auto* j = new IterVar("j", IterVar::VarKind::DataPar, Range(0, 1024));
    auto* k = new IterVar("k", IterVar::VarKind::Reduc, Range(0, 1024));
    matmul->iter_vars = {i, j, k};
    
    return module;
}

// ============================================================================
// 测试: CUDAKernelCodegen 基本创建
// ============================================================================

TEST(CUDAKernelCodegen_Create) {
    CUDAKernelCodegen codegen;
    ASSERT_TRUE(codegen.get_kernel_meta().kernel_name.empty());
}

// ============================================================================
// 测试: 生成简单 MatMul 内核
// ============================================================================

TEST(CUDAKernelCodegen_MatMulKernel) {
    auto module = create_matmul_module();
    ASSERT_TRUE(!module->operations.empty());
    
    auto* op = module->operations[0].get();
    Schedule sched(op);
    
    CUDAKernelCodegen codegen;
    std::string kernel = codegen.generate_kernel(*op, sched);
    
    ASSERT_TRUE(!kernel.empty());
    ASSERT_TRUE(kernel.find("__global__") != std::string::npos);
    ASSERT_TRUE(kernel.find("matmul_cuda_kernel") != std::string::npos);
    ASSERT_TRUE(kernel.find("A") != std::string::npos);
    ASSERT_TRUE(kernel.find("B") != std::string::npos);
    ASSERT_TRUE(kernel.find("C") != std::string::npos);
}

// ============================================================================
// 测试: 生成带有 Bind 调度的内核
// ============================================================================

TEST(CUDAKernelCodegen_BindKernel) {
    auto module = create_matmul_module();
    auto* op = module->operations[0].get();
    
    Schedule sched(op);
    sched.bind("i", "blockIdx.x");
    sched.bind("j", "threadIdx.x");
    
    CUDAKernelCodegen codegen;
    std::string kernel = codegen.generate_kernel(*op, sched);
    
    ASSERT_TRUE(!kernel.empty());
    ASSERT_TRUE(kernel.find("blockIdx") != std::string::npos);
    ASSERT_TRUE(kernel.find("threadIdx") != std::string::npos);
    
    auto meta = codegen.get_kernel_meta();
    ASSERT_TRUE(meta.grid_dim_x > 0);
    ASSERT_TRUE(meta.block_dim_x > 0);
}

// ============================================================================
// 测试: 生成带有共享内存缓存的内核
// ============================================================================

TEST(CUDAKernelCodegen_SharedMemory) {
    auto module = create_matmul_module();
    auto* op = module->operations[0].get();
    
    Schedule sched(op);
    sched.cache_read("A", "shared");
    sched.cache_write("C", "shared");
    
    CUDACodegenConfig config;
    config.use_shared_memory = true;
    CUDAKernelCodegen codegen(config);
    std::string kernel = codegen.generate_kernel(*op, sched);
    
    ASSERT_TRUE(!kernel.empty());
}

// ============================================================================
// 测试: 生成主机端包装函数
// ============================================================================

TEST(CUDAHostCodegen_Wrapper) {
    auto module = create_matmul_module();
    auto* op = module->operations[0].get();
    
    CUDAKernelMeta meta;
    meta.kernel_name = "matmul_cuda_kernel";
    meta.arg_names = {"A", "B", "C"};
    meta.arg_types = {"float*", "float*", "float*"};
    meta.block_dim_x = 256;
    meta.grid_dim_x = 4;
    
    CUDAHostCodegen host_codegen;
    std::string wrapper = host_codegen.generate_host_wrapper(*op, meta);
    
    ASSERT_TRUE(!wrapper.empty());
    ASSERT_TRUE(wrapper.find("extern \"C\"") != std::string::npos);
    ASSERT_TRUE(wrapper.find("cudaMalloc") != std::string::npos);
    ASSERT_TRUE(wrapper.find("cudaMemcpy") != std::string::npos);
    ASSERT_TRUE(wrapper.find("cudaFree") != std::string::npos);
    ASSERT_TRUE(wrapper.find("matmul_cuda_kernel") != std::string::npos);
}

// ============================================================================
// 测试: 完整模块生成
// ============================================================================

TEST(CUDACodeGenerator_Module) {
    auto module = create_matmul_module();
    
    CUDACodeGenerator generator;
    std::string code = generator.generate_module(*module);
    
    ASSERT_TRUE(!code.empty());
    ASSERT_TRUE(code.find("#include <cuda_runtime.h>") != std::string::npos);
    ASSERT_TRUE(code.find("__global__") != std::string::npos);
    ASSERT_TRUE(code.find("extern \"C\"") != std::string::npos);
    
    auto metas = generator.get_kernel_metas();
    ASSERT_TRUE(metas.size() == 1);
    ASSERT_EQ(metas[0].kernel_name, "matmul_cuda_kernel");
}

// ============================================================================
// 测试: 内核元数据正确性
// ============================================================================

TEST(CUDAKernelCodegen_KernelMeta) {
    auto module = create_matmul_module();
    auto* op = module->operations[0].get();
    
    Schedule sched(op);
    sched.bind("i", "blockIdx.x");
    sched.bind("j", "threadIdx.x");
    sched.tile({"i", "j"}, {32, 32});
    
    CUDAKernelCodegen codegen;
    codegen.generate_kernel(*op, sched);
    
    auto meta = codegen.get_kernel_meta();
    ASSERT_EQ(meta.kernel_name, "matmul_cuda_kernel");
    ASSERT_TRUE(meta.arg_names.size() == 3);
    ASSERT_TRUE(meta.block_dim_x > 0);
    ASSERT_TRUE(meta.grid_dim_x > 0);
}

// ============================================================================
// 测试: 便捷函数
// ============================================================================

TEST(CUDACodegen_ConvenienceFunction) {
    auto module = create_matmul_module();
    auto* op = module->operations[0].get();
    Schedule sched(op);
    
    std::string code = generate_cuda_code(*op, sched);
    
    ASSERT_TRUE(!code.empty());
    ASSERT_TRUE(code.find("__global__") != std::string::npos);
    ASSERT_TRUE(code.find("extern \"C\"") != std::string::npos);
}

// ============================================================================
// 测试: 生成内核启动代码
// ============================================================================

TEST(CUDACodegen_LaunchCode) {
    CUDAKernelMeta meta;
    meta.kernel_name = "test_kernel";
    meta.block_dim_x = 256;
    meta.grid_dim_x = 4;
    
    std::string launch = generate_cuda_launch_code(meta, {"d_A", "d_B", "d_C"});
    
    ASSERT_TRUE(!launch.empty());
    ASSERT_TRUE(launch.find("dim3 block") != std::string::npos);
    ASSERT_TRUE(launch.find("dim3 grid") != std::string::npos);
    ASSERT_TRUE(launch.find("test_kernel") != std::string::npos);
    ASSERT_TRUE(launch.find("cudaDeviceSynchronize()") != std::string::npos);
}

// ============================================================================
// 测试: 配置选项
// ============================================================================

TEST(CUDACodegenConfig_Defaults) {
    CUDACodegenConfig config;
    
    ASSERT_TRUE(config.max_threads_per_block == 256);
    ASSERT_TRUE(config.use_shared_memory == true);
    ASSERT_TRUE(config.use_tensor_cores == false);
    ASSERT_EQ(config.target_arch, "sm_70");
}

// ============================================================================
// 测试: 不同 block/thread 配置
// ============================================================================

TEST(CUDAKernelCodegen_2DGrid) {
    auto module = create_matmul_module();
    auto* op = module->operations[0].get();
    
    Schedule sched(op);
    sched.bind("i", "blockIdx.x");
    sched.bind("j", "blockIdx.y");
    sched.tile({"i", "j"}, {16, 16});
    
    CUDAKernelCodegen codegen;
    std::string kernel = codegen.generate_kernel(*op, sched);
    
    auto meta = codegen.get_kernel_meta();
    ASSERT_TRUE(meta.grid_dim_x > 0);
    ASSERT_TRUE(meta.grid_dim_y > 0);
}

// ============================================================================
// 测试: 向量化支持
// ============================================================================

TEST(CUDAKernelCodegen_Vectorize) {
    auto module = create_matmul_module();
    auto* op = module->operations[0].get();
    
    Schedule sched(op);
    sched.vectorize("k");
    
    CUDAKernelCodegen codegen;
    std::string kernel = codegen.generate_kernel(*op, sched);
    
    ASSERT_TRUE(!kernel.empty());
    ASSERT_TRUE(kernel.find("Vectorized") != std::string::npos);
}

// ============================================================================
// 测试: 循环展开
// ============================================================================

TEST(CUDAKernelCodegen_Unroll) {
    auto module = create_matmul_module();
    auto* op = module->operations[0].get();
    
    Schedule sched(op);
    sched.unroll("k");
    
    CUDAKernelCodegen codegen;
    std::string kernel = codegen.generate_kernel(*op, sched);
    
    ASSERT_TRUE(!kernel.empty());
    ASSERT_TRUE(kernel.find("#pragma unroll") != std::string::npos);
}

// ============================================================================
// 测试: Conv2D 内核生成
// ============================================================================

TEST(CUDAKernelCodegen_Conv2D) {
    auto module = std::make_unique<TensorIRModule>();
    module->name = "conv2d_test";
    
    auto* input = module->declare_buffer("input", "f32", {1, 3, 28, 28});
    auto* weight = module->declare_buffer("weight", "f32", {64, 3, 3, 3});
    auto* output = module->declare_buffer("output", "f32", {1, 64, 28, 28});
    
    auto* conv = module->create_conv2d(input, weight, output, {1, 1}, {0, 0});
    
    Schedule sched(conv);
    CUDAKernelCodegen codegen;
    std::string kernel = codegen.generate_kernel(*conv, sched);
    
    ASSERT_TRUE(!kernel.empty());
    ASSERT_TRUE(kernel.find("conv2d_cuda_kernel") != std::string::npos);
}

// ============================================================================
// 测试: Reduction 内核生成
// ============================================================================

TEST(CUDAKernelCodegen_Reduction) {
    auto module = std::make_unique<TensorIRModule>();
    module->name = "reduce_test";
    
    auto* src = module->declare_buffer("src", "f32", {1024, 1024});
    auto* dst = module->declare_buffer("dst", "f32", {1024});
    
    auto* reduce = dynamic_cast<ReduceOp*>(module->create_compute("reduce", {src, dst}, {}, ""));
    if (reduce) {
        reduce->reduce_type = ReduceOp::ReduceType::Sum;
    }
    
    auto* op = module->operations[0].get();
    Schedule sched(op);
    CUDAKernelCodegen codegen;
    std::string kernel = codegen.generate_kernel(*op, sched);
    
    ASSERT_TRUE(!kernel.empty());
}

// ============================================================================
// 测试: 内存管理代码生成
// ============================================================================

TEST(CUDAHostCodegen_MemoryManagement) {
    auto module = create_matmul_module();
    auto* op = module->operations[0].get();
    
    CUDAHostCodegen codegen;
    std::string mem_mgmt = codegen.generate_memory_management(*op);
    
    ASSERT_TRUE(!mem_mgmt.empty());
    ASSERT_TRUE(mem_mgmt.find("cuda_alloc") != std::string::npos);
    ASSERT_TRUE(mem_mgmt.find("cuda_free") != std::string::npos);
}

// ============================================================================
// 测试: 错误检查宏生成
// ============================================================================

TEST(CUDAHostCodegen_ErrorCheck) {
    tensorir::TensorIRModule module;
    
    CUDAHostCodegen codegen;
    std::string host_module = codegen.generate_host_module(module, {});
    
    ASSERT_TRUE(!host_module.empty());
    ASSERT_TRUE(host_module.find("CUDA_CHECK") != std::string::npos);
    ASSERT_TRUE(host_module.find("cudaGetErrorString") != std::string::npos);
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    std::cout << "========================================\n";
    std::cout << "Claw CUDA Codegen Unit Tests\n";
    std::cout << "========================================\n\n";
    
    int passed = 0;
    int failed = 0;
    
    RUN_TEST(CUDAKernelCodegen_Create);
    RUN_TEST(CUDAKernelCodegen_MatMulKernel);
    RUN_TEST(CUDAKernelCodegen_BindKernel);
    RUN_TEST(CUDAKernelCodegen_SharedMemory);
    RUN_TEST(CUDAHostCodegen_Wrapper);
    RUN_TEST(CUDACodeGenerator_Module);
    RUN_TEST(CUDAKernelCodegen_KernelMeta);
    RUN_TEST(CUDACodegen_ConvenienceFunction);
    RUN_TEST(CUDACodegen_LaunchCode);
    RUN_TEST(CUDACodegenConfig_Defaults);
    RUN_TEST(CUDAKernelCodegen_2DGrid);
    RUN_TEST(CUDAKernelCodegen_Vectorize);
    RUN_TEST(CUDAKernelCodegen_Unroll);
    RUN_TEST(CUDAKernelCodegen_Conv2D);
    RUN_TEST(CUDAKernelCodegen_Reduction);
    RUN_TEST(CUDAHostCodegen_MemoryManagement);
    RUN_TEST(CUDAHostCodegen_ErrorCheck);
    
    std::cout << "\n========================================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";
    std::cout << "========================================\n";
    
    return failed > 0 ? 1 : 0;
}
