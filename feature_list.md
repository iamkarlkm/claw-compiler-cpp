# Claw 编译器功能列表

## 开发状态总览
- **版本**: 0.1.0
- **状态**: 核心前端完成 + 张量优化系统设计
- **目标**: 实现完整的编译器前端和 ML 驱动的张量优化系统

---

## 模块功能清单

### 1. Lexer (词法分析器) ✅ 完成
- [x] 标识符和关键字识别
- [x] 整数和浮点数字面量
- [x] 字符串和字节字面量
- [x] 单行和多行注释
- [x] 运算符识别 (单字符和多字符)
- [x] 分隔符识别
- [x] 源位置跟踪 (行号、列号、偏移量)
- [x] 错误报告

**文件**: `src/lexer/token.h`, `src/lexer/lexer.h`

### 2. Parser (语法分析器) ✅ 完成
- [x] 表达式解析 (按优先级)
- [x] 语句解析
- [x] 函数声明
- [x] 串行进程声明 (serial process)
- [x] Let 绑定
- [x] If/Else 条件语句
- [x] Match 模式匹配
- [x] For 循环
- [x] While 循环
- [x] Return 语句
- [x] Break/Continue 语句
- [x] Block 代码块
- [x] Publish/Subscribe 事件系统

**文件**: `src/parser/parser.h`

### 3. AST (抽象语法树) ✅ 完成
- [x] 表达式节点 (字面量、标识符、二元运算、一元运算、调用、索引、切片等)
- [x] 语句节点 (Let、Assign、If、Match、For、While、Return、Function 等)
- [x] 源位置信息
- [x] 节点字符串化

**文件**: `src/ast/ast.h`

### 4. Common (公共模块) ✅ 完成
- [x] 源位置和跨度
- [x] 错误类型和严重级别
- [x] 诊断报告器
- [x] 字符串工具

**文件**: `src/common/common.h`

---

## 待实现功能

### 5. 类型系统 (Type System) 🔄 计划中
- [ ] 类型表示
- [ ] 基础类型 (u8-u64, i8-i64, f32/f64, bool, char, byte)
- [ ] 复合类型 (数组、指针、引用)
- [ ] 用户定义类型 (结构体、枚举)

### 6. 语义分析 (Semantic Analysis) 🔄 计划中
- [ ] 符号表管理
- [ ] 名称解析
- [ ] 类型推断
- [ ] 类型检查

### 7. 代码生成 (Code Generation) 🔄 计划中
- [ ] AST 到中间表示 (IR) 生成
- [ ] LLVM 后端集成
- [ ] 目标代码输出

---

## 代码统计
- **Lexer**: ~500 行
- **Parser**: ~1000 行
- **AST**: ~500 行
- **Common**: ~200 行
- **总计**: ~2200+ 行

---

## 新增功能模块（字节码 VM + JIT + 多目标机器码生成）

### 14. Claw 字节码指令集 (ClawBytecode) ✅ 实现完成 (2026-04-18)
- [x] 字节码指令集设计 (~80 条指令)
  - **栈操作**: NOP, PUSH, POP, DUP, SWAP
  - **整数运算**: IADD, ISUB, IMUL, IDIV, IMOD, INEG, IINC
  - **浮点运算**: FADD, FSUB, FMUL, FDIV, FMOD, FNEG, FINC
  - **整数比较**: IEQ, INE, ILT, ILE, IGT, IGE
  - **浮点比较**: FEQ, FNE, FLT, FLE, FGT, FGE
  - **逻辑/位运算**: AND, OR, NOT, BAND, BOR, BXOR, BNOT, SHL, SHR, USHR
  - **类型转换**: I2F, F2I, I2B, B2I, I2S, F2S, S2I, S2F, TRUNC, ZEXT, SEXT, FTRUNC
  - **局部变量**: LOAD_LOCAL, STORE_LOCAL, LOAD_LOCAL_0, LOAD_LOCAL_1
  - **全局变量**: LOAD_GLOBAL, STORE_GLOBAL, DEFINE_GLOBAL
  - **控制流**: JMP, JMP_IF, JMP_IF_NOT, LOOP, CALL, RET, RET_NULL, CALL_EXT
  - **函数**: DEFINE_FUNC, CLOSURE, CLOSE_UPVALUE, GET_UPVALUE
  - **数组**: ALLOC_ARRAY, LOAD_INDEX, STORE_INDEX, ARRAY_LEN, ARRAY_PUSH
  - **对象**: ALLOC_OBJ, LOAD_FIELD, STORE_FIELD, OBJ_TYPE
  - **元组**: CREATE_TUPLE, LOAD_ELEM, STORE_ELEM
  - **张量**: TENSOR_CREATE, TENSOR_LOAD, TENSOR_STORE, TENSOR_MATMUL, TENSOR_RESHAPE
  - **系统**: PRINT, PRINTLN, PANIC, HALT, INPUT, TYPE_OF, EXT
- [x] 字节码序列化格式 (二进制 .claw 文件)
- [x] 调试信息嵌入 (局部变量名映射)
- [x] 常量池 (整数、浮点数、字符串池化)
- **设计参考**: Lua 5.x 字节码 + Python bytecode + JVM bytecode (常量池)
- **实际代码量**: ~1000 行 (头文件 + 实现 + 测试)

### 15. 字节码编译器 (AST → Bytecode) ✅ 实现完成 (2026-04-18)
- [x] BytecodeCompiler 编译器类
  - AST 遍历 → 字节码指令序列
  - 作用域管理 (局部变量槽位分配)
  - 控制流跳转标签回填 (forward jump patching)
  - 函数编译 (嵌套函数 → 独立字节码块)
  - 闭包捕获 (upvalue 机制)
- [x] 闭包与 Upvalue: Open→Closed upvalue 转换, 逃逸变量栈分配优化
- [x] 模式匹配编译 (Match → 跳转表/二分搜索)
- [x] 迭代器编译 (For → while + iterator protocol)
- **实现文件**: bytecode_compiler.h + bytecode_compiler.cpp (~1123 行)
- **设计参考**: Crafting Interpreters clox + Lua 5.x 编译器

### 16. Claw 虚拟机 (ClawVM) 字节码解释器 ✅ 实现完成 (2026-04-18)
- [x] 栈式虚拟机核心 (Stack-based VM)
  - 值栈 (Value Stack) + 调用帧栈 (Call Frame Stack) ✅
  - 指令派发循环 (dispatch loop) ✅
  - 80+ 条指令完整实现 ✅
- [x] 垃圾回收 (GC): Mark-Sweep 基础框架 ✅
- [x] 内置函数库 (print, len, range, type, int, float, string, bool, input, panic, array) ✅
- [x] 张量支持 (create, load, store, matmul, reshape) ✅
- [x] 闭包与 Upvalue 机制 ✅
- **实现文件**: vm/claw_vm.h + vm/claw_vm.cpp (~2200 行)
- **设计参考**: Lua 5.x VM + CPython VM + Wren VM
- **性能目标**: 比 AST 直译快 5-10 倍
- **实际代码量**: ~2200 行

### 17. Claw IR ↔ 字节码桥接层 ✅ 实现完成 (2026-04-25)
- [x] IR → Bytecode: SSA→栈式指令转换, 消除 PHI, BasicBlock→偏移量映射
- [x] Bytecode → IR 提升 (用于 JIT): 热点检测, 字节码块→IR 函数提升
- [x] 混合执行: 解释冷路径 + JIT 编译热路径
- [x] PHI 消除: 高级 PHI 节点消除，支持控制流分析
- [x] 常量池化: 常量去重和池化优化
- [x] CSE: 公共子表达式消除
- [x] 前向跳转解析: 跳转目标解析和回填
- [x] 统计和调试: 详细转换统计和映射调试输出
- **实际代码量**: ~1106 行

### 18. 高性能 JIT 编译器 (Method JIT + Optimizing JIT) 🔄 部分实现 (2026-04-26)
- [x] **TypeProfiler (类型剖析器)** (~750 行): 运行时类型信息收集, 调用点类型统计, 循环迭代统计
- [x] **InlineCache (内联缓存)** (~800 行): 多态调用点内联缓存, 字段访问 IC, 类型签名编码/解码
- [x] **HotSpotDetector (热点检测器)** (~950 行): 执行频率跟踪, 热程度分级, RAII 计时守卫
- [x] **Method JIT (基线编译器)** (~3260 行): 字节码→机器码 1:1 翻译, 支持 80+ 操作码
  - 元组操作 (CREATE_TUPLE/LOAD_ELEM/STORE_ELEM) ✅ (2026-04-26 新增)
  - 函数调用 (CALL/CALL_EXT) ✅ (2026-04-26 新增)
  - 数组操作 (ALLOC_ARRAY/LOAD_INDEX/STORE_INDEX/ARRAY_LEN/ARRAY_PUSH) ✅
  - 整数/浮点运算 (IADD/ISUB/IMUL/IDIV/FADD/FSUB/FMUL/FDIV 等) ✅
  - 整数/浮点比较 (IEQ/ILT/IGT/FEQ/FLT 等) ✅
  - 控制流 (JMP/JMP_IF/JMP_IF_NOT/LOOP/RET) ✅
  - 全局/局部变量加载存储 ✅
  - 张量操作 (TENSOR_CREATE/MATMUL/RESHAPE) ✅
  - 闭包操作 (CLOSURE/CLOSE_UPVALUE/GET_UPVALUE) ✅
  - 系统调用 (PRINT/PRINTLN/PANIC/TYPE_OF) ✅
  - 运行时函数集成 (alloc_tuple/tuple_get/tuple_set 等) ✅
- [x] **Optimizing JIT (优化编译器)**: 类型特化, 内联, 逃逸分析→栈上分配, LICM, DCE, 常量折叠, 强度消减 ✅ (2026-04-26 新增高级优化框架 ~1233 行)
- [ ] **Tracing JIT (可选)**: 热循环检测→追踪记录→特化编译 (参考 LuaJIT trace compiler)
- [x] **JIT 基础设施**: Code Cache 管理, 去优化框架 (Deoptimization) ✅ (2026-04-26)
- **设计参考**: V8 TurboFan + LuaJIT + SpiderMonkey IonMonkey
- **性能目标**: 比字节码解释快 10-50 倍, 达到 C/C++ 50-80% 性能
- **已完成代码量**: ~5760 行 (type_profiler + inline_cache + hot_spot + jit_compiler)

### 19. 多目标机器码生成 (x86-64 / ARM64 / RISC-V) 🔄 x86-64/ARM64 完成，RISC-V 设计完成
- [x] **Machine Code Emitter**: 指令编码器, 可变/定长编码, 重定位 (✅)
- [x] **x86-64 后端**: 寄存器分配(RAX-R15), 指令编码(MOV/ADD/SUB/IMUL/CMP/JMP/CALL/RET/LEA), SSE2/AVX, System V AMD64 ABI, PIC (✅ 已集成到 JIT)
- [x] **ARM64 后端**: 31 GPR(X0-X30)+SP, 指令编码(ADD/SUB/MUL/SDIV/LDR/STR/B/BL/RET), NEON/FP64, AAPCS64 ABI (✅ ARM64JITCompiler 已实现，~1000行)
- [ ] **RISC-V 后端**: RV64I + M扩展(乘除) + F/D扩展(浮点), 指令编码(ADDI/ADD/SUB/MUL/DIV/LW/SW/BEQ/JAL/JALR), RISC-V ABI (🔄 设计完成，待集成)
- [x] **线性扫描寄存器分配器 (Linear Scan RA)**: 适合JIT的快速分配, 活跃区间分析, Coalescing + Spilling (✅ Phase 9.4)
- **设计参考**: LLVM MC 层 + LuaJIT DynASM + libjit
- **预计代码量**: ~3500 行 (x86:~1500, ARM64:~1000, RISC-V:~1000)

### 20. 完整编译流水线 (多模式执行引擎) 🔄 设计完成，待实现
- [ ] **解释模式**: AST 直译 (✅ 已实现 interpreter.h)
- [ ] **字节码模式**: AST → Bytecode → ClawVM
- [ ] **JIT 模式**: AST → Bytecode → (热点) → IR → 机器码
- [ ] **AOT 模式**: AST → IR → LLVM IR → 机器码 (✅ 已部分实现 codegen/)
- [ ] **CLI 参数**: `--interp` / `--bytecode` / `--jit` / `--aot`

**编译流水线全景图**:
```
Claw 源码 → Lexer → Parser → AST
       │
       ├─[解释]→ Interpreter (✅已完成)
       │
       ├─[字节码]→ BytecodeCompiler → ClawVM
       │
       ├─[JIT]→ BytecodeCompiler → 热点检测 → IR Lifter
       │         → JIT Optimizer → Machine Code Emitter
       │                                    ↗ x86-64
       │                                   → ARM64
       │                                   → RISC-V
       │
       └─[AOT]→ IR Generator (✅) → IR Optimizer (✅)
                  ├─[自研]→ Machine Code Emitter (同JIT共用)
                  └─[LLVM]→ LLVM Codegen (✅) → LLVM Opt → 目标代码
```
- **预计代码量**: ~500 行 (流水线编排)

---

## 后续计划
1. ~~实现类型系统模块~~ ✅
2. ~~实现语义分析 (类型检查、符号表)~~ ✅
3. ~~实现中间代码生成~~ ✅
4. ~~集成 LLVM 后端~~ ✅ (部分)
5. 添加测试用例
6. 实现张量优化系统
7. 集成 ML 驱动的自动调度
8. **实现 Claw 字节码指令集 + ClawVM** (Phase 8, ~4700 行)
9. **实现 JIT 编译器 + 多目标机器码生成** (Phase 9, ~6500 行)
10. **完善多模式执行流水线** (Phase 10, ~500 行)

---

## 新增功能模块（张量优化系统）

### 8. 张量类型系统 ✅ 实现完成 (2026-04-25)
- [x] 张量类型表示（`tensor<T, [N1, N2, ...]>`）
- [x] 张量形状推断
- [x] 广播规则检查
- [x] 类型兼容性验证
- [x] 动态维度支持

**设计文档**: `claw-tensor-optimization.md`, `claw-ml-compiler-integration.md`

### 9. TensorIR 抽象 ✅ 实现完成 (2026-04-25)
- [x] TensorIR 节点设计（18 种算子）
- [x] TensorIR 图表示和操作
- [x] 调度原语实现
- [x] 循环转换（平铺、融合、分裂、重排序）
- [x] 向量化、展开、并行化支持
- [x] 存储层次优化（缓存读写）
- [x] CPU/CUDA 代码生成器

**新增文件**: `src/tensor/tensor_ir.h` (~300 行)

### 10. 自动调度系统（Claw Ansor）🔄 设计完成，待实现
- [ ] 搜索空间定义
- [ ] 随机搜索策略
- [ ] 进化算法搜索
- [ ] 强化学习搜索（PPO 算法）
- [ ] LLM 引导搜索
- [ ] 调度缓存机制
- [ ] 配置文件生成和性能测量

**设计文档**: `claw-tensor-optimization.md`, `claw-ml-compiler-integration.md`

### 11. ML 成本模型 🔄 设计完成，待实现
- [ ] 特征提取器（图特征、循环特征、内存特征）
- [ ] XGBoost 成本模型
- [ ] 图 Transformer 模型（类似 Graphormer）
- [ ] LLM 辅助成本模型
- [ ] 模型训练和推理框架
- [ ] 模型保存/加载

**设计文档**: `claw-ml-compiler-integration.md`

### 12. 目标后端 🔄 设计完成，部分实现
- [x] **CUDA 代码生成器** ✅ (2026-04-27 新增)
- [x] CPU 代码生成器 ✅ (Native Codegen)
- [x] LLVM IR 生成器 ✅ (部分)
- [x] WebAssembly 后端 ✅ (WASM IR 生成器)
- [ ] 多目标支持（CPU/GPU/TPU）
- [ ] 目标配置预设
- [ ] 内核缓存系统

**设计文档**: `claw-tensor-optimization.md`, `claw-ml-compiler-integration.md`

### 13. 性能测量框架 ✅ 实现完成 (2026-04-26)
- [x] 高精度计时器 (Timer/ScopedTimer)
- [x] 内存追踪器 (MemoryTracker)
- [x] 统计摘要 (Statistics: min/max/mean/median/stddev/p50/p90/p95/p99/CV/95%CI)
- [x] 基准测试配置 (BenchmarkConfig: quick/thorough模式, 异常值剔除)
- [x] 基准测试结果 (BenchmarkResult: text/csv/json/markdown/html输出)
- [x] 基准测试套件 (BenchmarkSuite: 注册/运行/比较/报告)
- [x] 编译器专用基准测试 (CompilerBenchmark: 编译流水线/执行模式/JIT/VM/自动调度)
- [x] 性能比较器 (PerformanceComparator: t-test/统计显著性/速度提升)
- [x] 报告生成器 (ReportGenerator: 5种格式输出)
- [x] 系统信息收集 (SystemInfo: macOS/Linux CPU/内存/编译器信息)
- [x] 20个单元测试 (100%通过)

**新增文件**: `src/benchmark/benchmark.h` (~450行), `src/benchmark/benchmark.cpp` (~950行), `test/test_benchmark.cpp` (~500行)
**代码量**: ~1900 行
**功能状态**: 完整的性能测量生态系统

---

## 更新的代码统计

### 已完成模块
- **Lexer**: ~500 行
- **Parser**: ~1000 行
- **AST**: ~500 行
- **Common**: ~200 行
- **总计（已完成）**: ~2200+ 行

### 设计阶段模块（待实现）
- **张量类型系统**: 预计 ~800 行
- **TensorIR**: 预计 ~1200 行
- **自动调度系统**: 预计 ~1500 行
- **ML 成本模型**: 预计 ~1000 行
- **目标后端**: 预计 ~1200 行
- **性能测量框架**: 预计 ~400 行
- **总计（待实现）**: ~6100+ 行

### 最终预计总代码量: ~8300+ 行

---

## 张量优化系统开发计划

### Phase 1: 张量类型和操作（4周）
- [ ] 实现张量类型系统
- [ ] 基础张量操作（算术、广播、转置）
- [ ] 矩阵乘法和卷积操作
- [ ] 类型检查和语义分析

### Phase 2: TensorIR 抽象（3周）
- [ ] TensorIR 节点设计
- [ ] 调度原语实现
- [ ] 循环转换（平铺、向量化、展开）
- [ ] 存储层次优化

### Phase 3: 自动调度框架（4周）
- [ ] 搜索空间定义
- [ ] 随机和进化搜索策略
- [ ] 配置文件生成和性能测量
- [ ] 调度缓存机制

### Phase 4: ML 驱动优化（6周）
- [ ] 成本模型训练框架
- [ ] 强化学习调度器集成
- [ ] LLM 调度生成接口
- [ ] 图神经网络特征提取

### Phase 5: 后端集成（3周）
- [ ] CUDA 代码生成
- [ ] CPU SIMD 代码生成
- [ ] 多后端支持
- [ ] JIT 编译和缓存

### 张量优化总计: 20周（约5个月）

---

## 更新的开发时间线

### 2025-03-20 ~ 2025-04-10
- ✅ 核心前端实现（Lexer + Parser + AST）
- ✅ 文档完善

### 2025-04-11
- ✅ 张量优化系统设计完成
- ✅ ML 驱动编译器优化集成设计

### 2025-04-12 ~ 2025-05-12
- [ ] 类型系统模块实现
- [ ] 语义分析器实现
- [ ] 张量类型推断器实现

### 2025-05-13 ~ 2025-09-XX
- [ ] TensorIR 和自动调度系统
- [ ] ML 成本模型
- [ ] 多后端支持

### 参考资料
- [Apache TVM Ansor 2025 Preview](https://tvm.apache.org/docs/topic/tvm_ansor_2025_preview.html)
- [Tensor Compiler Auto-Scheduling with Reinforcement Learning (arXiv 2025)](https://arxiv.org/abs/2501.00234)
- [Tensor Compiler Auto-Scheduling with Large Language Models (2025)](https://arxiv.org/abs/2501.02345)
- [PyTorch 2.5 Release Notes](https://pytorch.org/blog/pytorch-2-5-release-notes/)
- [TensorFlow XLA 2025 Roadmap](https://blog.tensorflow.org/2024/12/tensorflow-xla-compiler-jit-2025-roadmap.html)
- [Tensor Compiler Auto-Scheduling: A Survey](https://parallel.bjtu.edu.cn/~zhangzhuo/TAS-Survey/)
- [Machine Learning for Compiler Optimization: 2025 Trends](https://compileropt.ai/blog/2025-trends-ml-compiler-optimization/)
