// jit_emitter_integration.h - JIT Emitter 集成补充头文件
// 提供基于 X86_64Emitter 的代码生成方法

#ifndef CLAW_JIT_EMITTER_INTEGRATION_H
#define CLAW_JIT_EMITTER_INTEGRATION_H

#include "jit_compiler.h"
#include "../emitter/x86_64_emitter.h"

namespace claw {
namespace jit {

// 使用 emitter 的 JIT 编译函数
CompilationResult compile_with_emitter(
    MethodJITCompiler& compiler,
    const bytecode::Function& func
);

} // namespace jit
} // namespace claw

#endif
