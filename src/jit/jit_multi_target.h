// jit/jit_multi_target.h - 多目标 JIT 编译器统一接口
// 支持 x86_64, ARM64, RISC-V 三种架构

#ifndef CLAW_JIT_MULTI_TARGET_H
#define CLAW_JIT_MULTI_TARGET_H

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "../bytecode/bytecode.h"

namespace claw {
namespace jit {

// ============================================================================
// 目标架构枚举
// ============================================================================

enum class TargetArchitecture {
    X86_64,    // AMD64 / x86_64
    ARM64,     // ARM64 / AArch64
    RISC_V64   // RISC-V 64-bit (RV64GC)
};

// ============================================================================
// 平台检测
// ============================================================================

namespace platform {

// 检测当前运行平台
TargetArchitecture detect_host_architecture();

// 获取架构名称
const char* get_architecture_name(TargetArchitecture arch);

// 检查是否支持某架构
bool is_architecture_supported(TargetArchitecture arch);

} // namespace platform

// ============================================================================
// 多目标 JIT 编译器接口
// ============================================================================

class IMultiTargetJITCompiler {
public:
    virtual ~IMultiTargetJITCompiler() = default;
    
    // 编译函数
    virtual bool compile(const bytecode::Function& func) = 0;
    
    // 获取编译后的代码
    virtual const uint8_t* get_code() const = 0;
    virtual size_t get_code_size() const = 0;
    
    // 获取错误信息
    virtual const std::string& get_error() const = 0;
    virtual bool is_success() const = 0;
    
    // 获取目标架构
    virtual TargetArchitecture get_architecture() const = 0;
    
    // 运行时函数注册
    virtual void register_runtime_function(const std::string& name, void* addr) = 0;
    
    // 清理缓存
    virtual void clear_cache() = 0;
};

// ============================================================================
// 多目标 JIT 编译器工厂
// ============================================================================

class MultiTargetJITFactory {
public:
    // 创建指定目标的 JIT 编译器
    static std::unique_ptr<IMultiTargetJITCompiler> create(TargetArchitecture arch);
    
    // 创建当前主机的 JIT 编译器
    static std::unique_ptr<IMultiTargetJITCompiler> create_for_host();
    
    // 获取支持的目标列表
    static std::vector<TargetArchitecture> get_supported_architectures();
    
    // 检查是否支持某目标
    static bool is_supported(TargetArchitecture arch);
};

// ============================================================================
// 多目标运行时函数注册表
// ============================================================================

class MultiTargetRuntimeRegistry {
public:
    static MultiTargetRuntimeRegistry& instance();
    
    // 注册运行时函数 (指定目标)
    void register_function(TargetArchitecture arch, const std::string& name, void* addr);
    
    // 查找运行时函数
    void* lookup(TargetArchitecture arch, const std::string& name) const;
    
    // 为所有目标注册相同函数
    void register_for_all(const std::string& name, void* addr);
    
    // 批量注册
    template<typename... Args>
    void register_batch(TargetArchitecture arch, Args... args) {
        register_function(arch, args...);
    }
    
    // 检查是否已注册
    bool is_registered(TargetArchitecture arch, const std::string& name) const;
    
    // 打印已注册函数 (调试)
    std::string dump_registry() const;

private:
    MultiTargetRuntimeRegistry() = default;
    
    std::unordered_map<TargetArchitecture, std::unordered_map<std::string, void*>> registries_;
};

// ============================================================================
// 便捷函数
// ============================================================================

// 创建 JIT 编译器
std::unique_ptr<IMultiTargetJITCompiler> create_jit_compiler(TargetArchitecture arch);
std::unique_ptr<IMultiTargetJITCompiler> create_jit_compiler_for_host();

// 获取运行时函数
void* get_runtime_function(TargetArchitecture arch, const std::string& name);

// 注册运行时函数
void register_runtime_function(TargetArchitecture arch, const std::string& name, void* addr);

} // namespace jit
} // namespace claw

#endif // CLAW_JIT_MULTI_TARGET_H
