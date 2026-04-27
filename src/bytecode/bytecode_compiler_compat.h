// BytecodeCompiler 修复版本 - 兼容 shared_ptr<Program>
// 
// 修复: BytecodeCompiler::compile() 接受 const ast::Program&
// 但 main.cpp 中的 parse() 返回 std::shared_ptr<ast::Program>
// 需要添加兼容的 compile 入口

#ifndef CLAW_BYTECODE_COMPILER_COMPAT_H
#define CLAW_BYTECODE_COMPILER_COMPAT_H

#include <memory>
#include "bytecode_compiler.h"

namespace claw {

/**
 * @brief 兼容层 - 允许从 shared_ptr<Program> 编译
 */
class BytecodeCompilerCompat {
public:
    BytecodeCompilerCompat() : compiler_(std::make_unique<BytecodeCompiler>()) {}
    
    /**
     * @brief 从 shared_ptr<Program> 编译 (主要入口)
     */
    std::shared_ptr<BytecodeModule> compile(std::shared_ptr<ast::Program> program) {
        if (!program) {
            lastError_ = "Null program pointer";
            return nullptr;
        }
        
        try {
            // 转换为引用传递
            return compiler_->compile(*program);
        } catch (const std::exception& e) {
            lastError_ = e.what();
            return nullptr;
        }
    }
    
    /**
     * @brief 从原始指针编译 (兼容旧API)
     */
    std::shared_ptr<BytecodeModule> compile(ast::Program* program) {
        if (!program) {
            lastError_ = "Null program pointer";
            return nullptr;
        }
        return compile(std::shared_ptr<ast::Program>(program, [](ast::Program*){}));
    }
    
    /**
     * @brief 从引用编译 (兼容旧API)
     */
    std::shared_ptr<BytecodeModule> compile(ast::Program& program) {
        try {
            return compiler_->compile(program);
        } catch (const std::exception& e) {
            lastError_ = e.what();
            return nullptr;
        }
    }
    
    /**
     * @brief 从 unique_ptr<Program> 编译
     */
    std::shared_ptr<BytecodeModule> compile(std::unique_ptr<ast::Program> program) {
        return compile(program.release());
    }
    
    const std::string& getLastError() const { return lastError_; }
    void setDebugInfo(bool enable) { compiler_->setDebugInfo(enable); }

private:
    std::unique_ptr<BytecodeCompiler> compiler_;
    std::string lastError_;
};

} // namespace claw

#endif // CLAW_BYTECODE_COMPILER_COMPAT_H
