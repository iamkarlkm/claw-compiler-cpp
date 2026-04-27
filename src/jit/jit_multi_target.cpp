// jit/jit_multi_target.cpp - 多目标 JIT 编译器统一接口实现

#include "jit_multi_target.h"
#include "jit_compiler.h"

#if defined(__x86_64__) || defined(_M_X64)
    #include "../emitter/x86_64_emitter.h"
#endif

// ARM64 编译器 (仅在 ARM64 平台上可用)
#if defined(__aarch64__) || defined(_M_ARM64)
    #include "../emitter/arm64_emitter.h"
    #include "arm64_jit_integration.h"
#else
    // 在非 ARM64 平台上，提供向前声明
    namespace claw { namespace jit {
        struct ARM64JITConfig;
        class ARM64JITCompiler;
    } }
#endif

#include "../emitter/riscv_emitter.h"
#include "jit_riscv_integration.h"

#include <cstring>
#include <algorithm>

namespace claw {
namespace jit {

// ============================================================================
// 平台检测实现
// ============================================================================

namespace platform {

#if defined(__x86_64__) || defined(_M_X64)
    TargetArchitecture detect_host_architecture() {
        return TargetArchitecture::X86_64;
    }
#elif defined(__aarch64__) || defined(_M_ARM64)
    TargetArchitecture detect_host_architecture() {
        return TargetArchitecture::ARM64;
    }
#elif defined(__riscv) && __riscv_xlen == 64
    TargetArchitecture detect_host_architecture() {
        return TargetArchitecture::RISC_V64;
    }
#else
    TargetArchitecture detect_host_architecture() {
        // 默认为 x86_64
        return TargetArchitecture::X86_64;
    }
#endif

const char* get_architecture_name(TargetArchitecture arch) {
    switch (arch) {
        case TargetArchitecture::X86_64:   return "x86_64";
        case TargetArchitecture::ARM64:    return "ARM64";
        case TargetArchitecture::RISC_V64: return "RISC-V 64-bit";
        default: return "Unknown";
    }
}

bool is_architecture_supported(TargetArchitecture arch) {
    // 所有架构都通过模拟支持
    return true;
}

} // namespace platform

// ============================================================================
// 内部适配器类
// ============================================================================

#if defined(__x86_64__) || defined(_M_X64)

// x86_64 适配器
class X86_64JITAdapter : public IMultiTargetJITCompiler {
public:
    X86_64JITAdapter() 
        : method_jit_(std::make_unique<MethodJITCompiler>())
        , success_(false) {}
    
    bool compile(const bytecode::Function& func) override {
        auto result = method_jit_->compile(func);
        success_ = result.success;
        error_ = result.error_message;
        compiled_code_ = result.machine_code;
        code_size_ = result.code_size;
        return success_;
    }
    
    const uint8_t* get_code() const override {
        return static_cast<const uint8_t*>(compiled_code_);
    }
    
    size_t get_code_size() const override {
        return code_size_;
    }
    
    const std::string& get_error() const override {
        return error_;
    }
    
    bool is_success() const override {
        return success_;
    }
    
    TargetArchitecture get_architecture() const override {
        return TargetArchitecture::X86_64;
    }
    
    void register_runtime_function(const std::string& name, void* addr) override {
        RuntimeFunctionRegistry::instance().register_function(name, addr);
    }
    
    void clear_cache() override {
        method_jit_->clear_cache();
    }

private:
    std::unique_ptr<MethodJITCompiler> method_jit_;
    bool success_;
    std::string error_;
    void* compiled_code_ = nullptr;
    size_t code_size_ = 0;
};

#endif // __x86_64__

// RISC-V 适配器 - 使用现有的 RISC-V JIT 编译器
class RISC_VJITAdapter : public IMultiTargetJITCompiler {
public:
    RISC_VJITAdapter() 
        : riscv_compiler_(std::make_unique<RISCVRISCVJITCompiler>())
        , success_(false)
        , constants_(nullptr)
        , compiled_code_(nullptr)
        , code_size_(0) {
        // 初始化 RISC-V JIT 编译器
    }
    
    bool compile(const bytecode::Function& func) override {
        // 设置常量池引用
        riscv_compiler_->set_constants(constants_);
        
        // 编译函数
        bool result = riscv_compiler_->compile(func, constants_);
        success_ = result;
        
        if (result) {
            error_ = "";
            compiled_code_ = const_cast<uint8_t*>(riscv_compiler_->get_code());
            code_size_ = riscv_compiler_->get_code_size();
        } else {
            error_ = riscv_compiler_->get_error();
            compiled_code_ = nullptr;
            code_size_ = 0;
        }
        
        return success_;
    }
    
    const uint8_t* get_code() const override {
        return compiled_code_;
    }
    
    size_t get_code_size() const override {
        return code_size_;
    }
    
    const std::string& get_error() const override {
        return error_;
    }
    
    bool is_success() const override {
        return success_;
    }
    
    TargetArchitecture get_architecture() const override {
        return TargetArchitecture::RISC_V64;
    }
    
    void register_runtime_function(const std::string& name, void* addr) override {
        riscv_compiler_->register_runtime_function(name, addr);
    }
    
    void clear_cache() override {
        // RISC-V 编译器缓存清理
        success_ = false;
        compiled_code_ = nullptr;
        code_size_ = 0;
    }
    
    // 设置常量池
    void set_constants(const bytecode::ConstantPool* constants) {
        constants_ = constants;
        riscv_compiler_->set_constants(constants);
    }

private:
    std::unique_ptr<RISCVRISCVJITCompiler> riscv_compiler_;
    bool success_;
    std::string error_;
    uint8_t* compiled_code_;
    size_t code_size_;
    const bytecode::ConstantPool* constants_;
};

// ARM64 适配器 - 使用现有的 ARM64 JIT 编译器
// 注意: 仅在 ARM64 平台上完整实现
#if defined(__aarch64__) || defined(_M_ARM64)

class ARM64JITAdapter : public IMultiTargetJITCompiler {
public:
    ARM64JITAdapter()
        : arm64_compiler_(std::make_unique<ARM64JITCompiler>())
        , success_(false)
        , constants_(nullptr)
        , compiled_code_(nullptr)
        , code_size_(0) {
        // 初始化 ARM64 JIT 编译器
    }
    
    bool compile(const bytecode::Function& func) override {
        // 编译函数
        auto result = arm64_compiler_->compile(func);
        success_ = result.success;
        
        if (result.success) {
            error_ = "";
            compiled_code_ = static_cast<uint8_t*>(result.machine_code);
            code_size_ = result.code_size;
        } else {
            error_ = result.error_message;
            compiled_code_ = nullptr;
            code_size_ = 0;
        }
        
        return success_;
    }
    
    const uint8_t* get_code() const override {
        return compiled_code_;
    }
    
    size_t get_code_size() const override {
        return code_size_;
    }
    
    const std::string& get_error() const override {
        return error_;
    }
    
    bool is_success() const override {
        return success_;
    }
    
    TargetArchitecture get_architecture() const override {
        return TargetArchitecture::ARM64;
    }
    
    void register_runtime_function(const std::string& name, void* addr) override {
        // ARM64 运行时函数注册 - 通过编译器内部处理
    }
    
    void clear_cache() override {
        arm64_compiler_->clear_cache();
        success_ = false;
        compiled_code_ = nullptr;
        code_size_ = 0;
    }
    
    // 设置常量池
    void set_constants(const bytecode::ConstantPool* constants) {
        constants_ = constants;
    }

private:
    std::unique_ptr<ARM64JITCompiler> arm64_compiler_;
    bool success_;
    std::string error_;
    uint8_t* compiled_code_;
    size_t code_size_;
    const bytecode::ConstantPool* constants_;
};

#else
// 在非 ARM64 平台上的存根实现
class ARM64JITAdapter : public IMultiTargetJITCompiler {
public:
    ARM64JITAdapter()
        : success_(false)
        , error_("ARM64 JIT not available on this platform") {
    }
    
    bool compile(const bytecode::Function& func) override {
        error_ = "ARM64 JIT compilation not available on this platform";
        success_ = false;
        return false;
    }
    
    const uint8_t* get_code() const override { return nullptr; }
    size_t get_code_size() const override { return 0; }
    const std::string& get_error() const override { return error_; }
    bool is_success() const override { return success_; }
    TargetArchitecture get_architecture() const override { return TargetArchitecture::ARM64; }
    
    void register_runtime_function(const std::string& name, void* addr) override {}
    void clear_cache() override {}

private:
    bool success_;
    std::string error_;
};

#endif

// ============================================================================
// 工厂实现
// ============================================================================

std::unique_ptr<IMultiTargetJITCompiler> MultiTargetJITFactory::create(TargetArchitecture arch) {
    switch (arch) {
#if defined(__x86_64__) || defined(_M_X64)
        case TargetArchitecture::X86_64:
            return std::make_unique<X86_64JITAdapter>();
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
        case TargetArchitecture::ARM64:
            return std::make_unique<ARM64JITAdapter>();
#endif

        case TargetArchitecture::RISC_V64:
            return std::make_unique<RISC_VJITAdapter>();

        default:
            // 默认返回 x86_64 (在模拟环境可能工作)
            return std::make_unique<RISC_VJITAdapter>();
    }
}

std::unique_ptr<IMultiTargetJITCompiler> MultiTargetJITFactory::create_for_host() {
    return create(platform::detect_host_architecture());
}

std::vector<TargetArchitecture> MultiTargetJITFactory::get_supported_architectures() {
    return {
        TargetArchitecture::X86_64,
        TargetArchitecture::ARM64,
        TargetArchitecture::RISC_V64
    };
}

bool MultiTargetJITFactory::is_supported(TargetArchitecture arch) {
    auto supported = get_supported_architectures();
    return std::find(supported.begin(), supported.end(), arch) != supported.end();
}

// ============================================================================
// 多目标运行时注册表实现
// ============================================================================

MultiTargetRuntimeRegistry& MultiTargetRuntimeRegistry::instance() {
    static MultiTargetRuntimeRegistry instance;
    return instance;
}

void MultiTargetRuntimeRegistry::register_function(TargetArchitecture arch, 
                                                     const std::string& name, 
                                                     void* addr) {
    registries_[arch][name] = addr;
}

void* MultiTargetRuntimeRegistry::lookup(TargetArchitecture arch, 
                                          const std::string& name) const {
    auto arch_it = registries_.find(arch);
    if (arch_it != registries_.end()) {
        auto func_it = arch_it->second.find(name);
        if (func_it != arch_it->second.end()) {
            return func_it->second;
        }
    }
    return nullptr;
}

void MultiTargetRuntimeRegistry::register_for_all(const std::string& name, void* addr) {
    for (auto arch : MultiTargetJITFactory::get_supported_architectures()) {
        registries_[arch][name] = addr;
    }
}

bool MultiTargetRuntimeRegistry::is_registered(TargetArchitecture arch, 
                                                 const std::string& name) const {
    return lookup(arch, name) != nullptr;
}

std::string MultiTargetRuntimeRegistry::dump_registry() const {
    std::string result;
    for (const auto& arch_pair : registries_) {
        result += platform::get_architecture_name(arch_pair.first);
        result += ":\n";
        for (const auto& func_pair : arch_pair.second) {
            result += "  " + func_pair.first + " -> " + 
                      std::to_string(reinterpret_cast<uintptr_t>(func_pair.second)) + "\n";
        }
    }
    return result;
}

// ============================================================================
// 便捷函数实现
// ============================================================================

std::unique_ptr<IMultiTargetJITCompiler> create_jit_compiler(TargetArchitecture arch) {
    return MultiTargetJITFactory::create(arch);
}

std::unique_ptr<IMultiTargetJITCompiler> create_jit_compiler_for_host() {
    return MultiTargetJITFactory::create_for_host();
}

void* get_runtime_function(TargetArchitecture arch, const std::string& name) {
    return MultiTargetRuntimeRegistry::instance().lookup(arch, name);
}

void register_runtime_function(TargetArchitecture arch, const std::string& name, void* addr) {
    MultiTargetRuntimeRegistry::instance().register_function(arch, name, addr);
}

} // namespace jit
} // namespace claw
