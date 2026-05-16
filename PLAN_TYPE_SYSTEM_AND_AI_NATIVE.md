# Claw 编译器增强计划：类型系统、精准错误处理、零开销迭代器与 AI 原生设计

> 参考文档：`/Users/mac/.qclaw/workspace/moonbit-compiler-kb.md`
> 目标：将 MoonBit 的先进设计理念系统性引入 Claw 编译器

---

## 一、现状分析

### 1.1 Claw 编译器当前能力

| 模块 | 现状 | 关键文件 |
|------|------|---------|
| **类型系统** | 基础类型完备（primitive/array/tuple/optional/result/function/struct/enum/generic），TypeChecker 已实现两趟检查，但泛型仅停留在 AST 层面，未做单态化 | `src/type/type_system.h`, `src/type/type_checker.cpp` |
| **模式匹配** | AST 支持 `MatchStmt`，但 pattern 只是普通 `Expression`（仅限 literal/identifier），无穷尽性检查 | `src/ast/ast.h:594-621` |
| **错误处理** | 完整的 try/catch/throw 管道（AST → 字节码 → VM → 解释器），但无编译期错误传播追踪 | `src/ast/ast.h:712-783`, `src/bytecode/bytecode_compiler.cpp:834-883` |
| **迭代器** | VM 级实现（array/range/enumerate/zip），`for` 循环语法糖，但无编译期内联优化 | `src/bytecode/bytecode.h:194-202`, `src/vm/claw_vm.h:71-140` |
| **优化管线** | 8-pass 管线已落地：常量折叠 → 代数简化 → 控制流简化 → DCE → TCO → 函数内联 → Tree Shaking → Peephole | `src/main.cpp`, `src/optimizer/*.cpp` |

### 1.2 与 MoonBit 的差距

| MoonBit 特性 | Claw 差距 |
|-------------|----------|
| 泛型单态化（零开销） | 泛型参数仅在 AST 存储，TypeChecker 未实例化，字节码直接执行泛型函数 |
| 模式匹配穷尽性检查 | MatchStmt 的 pattern 只是 Expression，无专用 Pattern AST，无法做穷尽性分析 |
| 编译期错误传播追踪 | 只有运行时 try/catch/throw，函数签名无 `raise`/`noraise` 标记 |
| 零开销迭代器 | VM 用 IteratorValue 堆对象表示迭代器状态，存在 boxing/unboxing 开销 |
| AI 原生设计 | 无专门面向 AI 代码生成的类型推断增强或错误信息优化 |

---

## 二、总体实施策略

**核心原则**：
1. **渐进增强**：每个阶段独立可测试，不破坏现有功能
2. **编译期为主**：MoonBit 的精髓是"编译期做尽可能多的工作"，Claw 应在编译期/优化期消除开销
3. **测试先行**：每新增一个特性，配套单元测试 + 字节码端到端测试

**实施顺序**：
```
Phase 1: 模式匹配增强 + 穷尽性检查（为类型系统打基础）
Phase 2: 泛型单态化（Monomorphization）
Phase 3: 精准错误处理（Error Effect Tracking）
Phase 4: 零开销迭代器（编译期内联 + desugaring）
Phase 5: AI 原生设计（智能错误信息 + 上下文精简）
```

---

## 三、Phase 1：模式匹配增强与穷尽性检查

### 3.1 目标
- 引入专用 `Pattern` AST 层级，替代当前用 `Expression` 充当 pattern 的做法
- 实现穷尽性检查（exhaustiveness checking），确保 match 覆盖所有可能分支
- 为后续泛型单态化和错误处理提供模式匹配基础设施

### 3.2 技术方案

#### 3.2.1 新建 `Pattern` AST 层级

```cpp
// src/ast/pattern.h
namespace claw { namespace ast {

class Pattern : public ASTNode {
public:
    enum class Kind {
        Wildcard,        // _
        Variable,        // x
        Literal,         // 42, true, "hello"
        Constructor,     // Some(x), None, Cons(head, tail)
        Tuple,           // (a, b, c)
        Array,           // [a, b, c]
        Rest,            // ..rest (spread in array pattern)
        Or,              // A | B
        Range,           // 1..=10
        Binding,         // x @ Some(_)
    };
    // ...
};

class ConstructorPattern : public Pattern {
    std::string constructor_name_;
    std::vector<std::unique_ptr<Pattern>> fields_;
};

}} // namespace
```

#### 3.2.2 修改 `MatchStmt`

```cpp
// src/ast/ast.h
class MatchStmt : public Statement {
    std::unique_ptr<Expression> expr_;
    std::vector<std::unique_ptr<Pattern>> patterns_;  // 从 Expression* 改为 Pattern*
    std::vector<std::unique_ptr<ASTNode>> bodies_;
};
```

#### 3.2.3 穷尽性检查算法

基于 **Wadler/Leijen 的 pattern coverage algorithm**（GHC/Rust/Swift 均采用此算法）：

```cpp
// src/type/pattern_checker.h
class PatternChecker {
    // 将类型展开为一组"用例"（useful values）
    // 例如：Option<Int> → [None, Some(_)]
    // 例如：enum Color { Red, Green, Blue } → [Red, Green, Blue]
    
    // 核心函数
    bool is_exhaustive(const std::vector<std::unique_ptr<Pattern>>& patterns, 
                       const Type& scrutinee_type);
    
    // 返回未被覆盖的分支（用于错误报告）
    std::vector<std::string> find_uncovered_patterns(
        const std::vector<std::unique_ptr<Pattern>>& patterns,
        const Type& scrutinee_type);
};
```

**算法步骤**：
1. 从 scrutinee 类型构造初始 "matrix"（一组未覆盖的构造子组合）
2. 对每个 pattern，从 matrix 中"消去"已被覆盖的组合
3. 若最终 matrix 非空，则报告未覆盖的分支

#### 3.2.4 Parser 修改

扩展 parser 以识别新的 pattern 语法：
- `Some(x)` → `ConstructorPattern`
- `(a, b)` → `TuplePattern`
- `[a, ..rest]` → `ArrayPattern`
- `x @ Some(_)` → `BindingPattern`
- `1 | 2 | 3` → `OrPattern`

### 3.3 修改文件清单

| 文件 | 修改内容 |
|------|---------|
| `src/ast/pattern.h` (新建) | Pattern 基类及所有子类 |
| `src/ast/ast.h` | MatchStmt 改用 `std::unique_ptr<Pattern>` |
| `src/ast/clone.cpp` | 添加 Pattern 的 clone 支持 |
| `src/ast/ast_serializer.cpp` | 添加 Pattern 的序列化/反序列化 |
| `src/parser/parser.h` | 扩展 pattern 解析规则 |
| `src/type/pattern_checker.h/.cpp` (新建) | 穷尽性检查实现 |
| `src/type/type_checker.cpp` | Match 语句类型检查改用 Pattern |
| `src/bytecode/bytecode_compiler.cpp` | 编译新的 MatchStmt |
| `src/interpreter/interpreter.h` | 解释执行新的 MatchStmt |
| `src/ir/ir_generator.cpp` | IR 生成适配新 MatchStmt |

### 3.4 测试计划

- **单元测试**：`src/test/test_pattern_checker.cpp`
  - `exhaustive_bool`：`match true/false` 覆盖全部
  - `exhaustive_option`：`Some(x) / None` 覆盖全部
  - `exhaustive_enum`：Color 三 variant 全覆盖
  - `non_exhaustive_missing_variant`：遗漏 Green
  - `non_exhaustive_wildcard_fallback`：`_` 通配符确保穷尽
  - `nested_pattern_exhaustive`：`Some(Some(x)) / Some(None) / None`
  - `tuple_pattern_exhaustive`：`(true, false) / (false, true) / ...`
  
- **字节码端到端测试**：`make test-bytecode-opt` 新增 match 测试用例

---

## 四、Phase 2：泛型单态化（Monomorphization）

### 4.1 目标
- 实现编译期泛型单态化，将 `fn<T> id(x: T) -> T` 在调用点实例化为具体类型版本
- 达到"零开销泛型"：单态化后的代码与手写具体类型代码性能一致
- 编译速度优化：避免 C++ 模板式的编译爆炸，采用延迟单态化 + 缓存

### 4.2 技术方案

#### 4.2.1 单态化管线设计

```
AST  with Generics
       ↓
┌─────────────────────┐
│  Monomorphizer      │  ← 新增 Pass，位于 TypeCheck 之后
│  - 收集所有泛型调用点│
│  - 生成具体类型版本 │
│  - 替换原调用为实例 │
└─────────────────────┘
       ↓
AST without Generics (所有泛型已展开)
       ↓
现有优化管线（常量折叠、内联、TCO...）
```

#### 4.2.2 `Monomorphizer` 实现

```cpp
// src/optimizer/monomorphizer.h
class Monomorphizer {
public:
    struct InstanceKey {
        std::string function_name;
        std::vector<Type> type_args;
        bool operator==(const InstanceKey& other) const;
    };
    
    struct InstanceKeyHash { ... };
    
    bool monomorphize(ast::Program& program);
    
private:
    // 已实例化的函数缓存
    std::unordered_map<InstanceKey, std::string, InstanceKeyHash> instances_;
    
    // 泛型函数定义表
    std::unordered_map<std::string, ast::FunctionStmt*> generic_functions_;
    
    // 核心方法
    void collect_generic_functions(ast::Program& program);
    void collect_instantiation_sites(ast::Program& program);
    std::unique_ptr<ast::FunctionStmt> instantiate_function(
        const ast::FunctionStmt& generic_fn,
        const std::vector<Type>& type_args);
    void substitute_type_vars(ast::Statement& stmt, 
                              const std::unordered_map<std::string, Type>& subst);
    void substitute_type_vars(ast::Expression& expr,
                              const std::unordered_map<std::string, Type>& subst);
};
```

#### 4.2.3 单态化流程

**Step 1: 收集泛型函数**
扫描所有 `FunctionStmt`，将 `has_type_params() == true` 的函数存入 `generic_functions_`。

**Step 2: 收集实例化点**
遍历所有 `CallExpr`，若 callee 是泛型函数调用（如 `id<Int>(42)` 或根据参数推断的 `id(42)`），记录 `InstanceKey`。

**Step 3: 生成实例**
对每个唯一的 `InstanceKey`：
1. 深拷贝原泛型函数的 AST（使用 `clone_stmt`）
2. 建立类型参数到具体类型的映射（如 `T → Int`）
3. 递归遍历函数体，替换所有涉及类型参数的地方：
   - `LetStmt` 的类型注解
   - `FunctionStmt` 的参数类型和返回类型
   - `StructStmt` / `EnumStmt` 的泛型实例化
4. 生成新的函数名：`id__Int`（name mangling）
5. 将新函数添加到 Program 的 declarations 中

**Step 4: 替换调用点**
将所有泛型调用替换为对实例化后函数的调用：`id(42)` → `id__Int(42)`

#### 4.2.4 Name Mangling 规则

```cpp
std::string mangle_name(const std::string& base, const std::vector<Type>& type_args) {
    // id<Int>       → id__Int
    // map<Int, Str> → map__Int__Str
    // pair<Array<Int>, Bool> → pair__Array_Int__Bool
    std::string result = base;
    for (const auto& t : type_args) {
        result += "__" + mangle_type(t);
    }
    return result;
}
```

#### 4.2.5 类型变量替换（Type Substitution）

```cpp
void Monomorphizer::substitute_type_vars(ast::Expression& expr, 
                                         const Substitution& subst) {
    switch (expr.get_kind()) {
        case Expression::Kind::Call: {
            auto& call = static_cast<ast::CallExpr&>(expr);
            // 替换泛型参数类型标注
            for (auto& arg : call.mutable_arguments()) {
                substitute_type_vars(*arg, subst);
            }
            break;
        }
        // ... 其他表达式类型
    }
}
```

对于 `LetStmt` 和 `FunctionStmt` 的类型注解，需要在 AST 中新增/暴露类型字段的修改接口。

### 4.3 修改文件清单

| 文件 | 修改内容 |
|------|---------|
| `src/optimizer/monomorphizer.h/.cpp` (新建) | 单态化核心实现 |
| `src/ast/ast.h` | 暴露类型注解的可修改接口（若尚不存在） |
| `src/type/type_system.h` | 添加 `substitute_type_vars` 工具函数 |
| `src/main.cpp` | 在 TypeCheck 之后插入 Monomorphizer Pass |
| `src/test/test_monomorphizer.cpp` (新建) | 单元测试 |

### 4.4 测试计划

- **单元测试**：`src/test/test_monomorphizer.cpp`
  - `simple_identity`：`id<Int>(5)` 单态化为 `id__Int`
  - `generic_struct`：`Box<Int>{ value: 42 }` 实例化
  - `nested_generic`：`Array<Option<Int>>` 正确展开
  - `multiple_type_params`：`pair<Int, String>(1, "a")`
  - `deduction_from_arg`：根据实参推断泛型参数
  - `no_duplicate_instances`：同一类型只生成一个实例
  - `recursive_generic`：泛型函数递归调用自身

- **字节码端到端测试**：
  ```claw
  fn id<T>(x: T) -> T { return x; }
  fn main() { print(id(42)); print(id("hello")); }
  ```
  期望输出：42 和 hello

---

## 五、Phase 3：精准错误处理（Error Effect Tracking）

### 5.1 目标
- 在编译期追踪函数的"错误效应"（error effect），替代运行时的 try/catch 作为首要错误处理心智模型
- 函数签名标注 `raise` / `noraise`，编译器自动推导错误传播路径
- 支持 `try?` 语法将可能出错的表达式转换为 `Result<T, Error>`
- 支持错误多态性（`raise?`）：高阶函数根据传入的闭包是否抛错决定自身是否抛错

### 5.2 技术方案

#### 5.2.1 错误效应类型系统

```cpp
// src/type/error_effect.h
enum class ErrorEffect {
    NoError,        // noraise — 保证不抛错
    ConcreteError,  // raise SomeError — 可能抛出具体错误类型
    GenericError,   // raise? — 错误效应由参数决定（多态）
    UnknownError,   // 未标注，待推断
};

struct ErrorEffectInfo {
    ErrorEffect kind;
    Type error_type;           // ConcreteError 时的具体错误类型
    std::string polymorphic_var; // GenericError 时的约束变量名
};
```

#### 5.2.2 函数签名扩展

```cpp
// src/ast/ast.h — FunctionStmt 扩展
class FunctionStmt : public Statement {
    // ... 现有字段 ...
    ErrorEffectInfo error_effect_;  // 新增
    
public:
    void set_error_effect(const ErrorEffectInfo& info);
    const ErrorEffectInfo& get_error_effect() const;
    bool is_noraise() const;
    bool can_raise() const;
};
```

Parser 扩展语法：
```
fn div(x: Int, y: Int) -> Int raise DivError { ... }
fn add(a: Int, b: Int) -> Int noraise { ... }
fn map<T>(arr: Array<T>, f: (T) -> T raise?) -> Array<T> raise? { ... }
```

#### 5.2.3 错误效应推导（Error Effect Inference）

基于控制流分析（Control Flow Analysis）的推导规则：

| 表达式/语句 | 推导规则 |
|-----------|---------|
| `raise E` | effect = `ConcreteError(E)` |
| `f()` where `f: raise E` | effect = `ConcreteError(E)` |
| `f()` where `f: noraise` | effect = `NoError` |
| `if cond { A } else { B }` | effect = `union(A.effect, B.effect)` |
| `try { A } catch { ... }` | 若 catch 全覆盖，effect = `NoError`；否则保留未捕获的错误 |
| `try? expr` | effect = `NoError`（错误被转换为 Result） |

实现为一个独立的 TypeChecker 子遍历器：

```cpp
// src/type/error_effect_analyzer.h
class ErrorEffectAnalyzer {
public:
    void analyze_program(ast::Program& program);
    
private:
    ErrorEffectInfo analyze_stmt(ast::Statement& stmt);
    ErrorEffectInfo analyze_expr(ast::Expression& expr);
    
    // 合并两个 error effect（取并集）
    ErrorEffectInfo union_effects(const ErrorEffectInfo& a, 
                                   const ErrorEffectInfo& b);
    
    // 检查函数声明的 effect 标注与实际推导是否一致
    void check_function_consistency(ast::FunctionStmt& fn);
};
```

#### 5.2.4 `try?` → `Result` 转换（编译期脱糖）

```cpp
// src/optimizer/error_desugarer.h
class ErrorDesugarer {
    // 将 try? expr 脱糖为：
    // match try_expr_result {
    //   Ok(v) => v,
    //   Err(e) => e  // 作为 Result 返回
    // }
    
    // 实际上更简单：在 AST 层面将 try? expr 替换为 Result 构造
    void desugar_try_question(ast::Program& program);
};
```

**脱糖示例**：
```moonbit
// 源码
let res = try? div(6, 0)

// 脱糖后（内部 AST 表示）
let res = match div(6, 0) {
    Ok(v) => Ok(v),
    Err(e) => Err(e)
}
// 或者直接保留为 TryQuestionExpr，由字节码编译器生成 Result
```

更简洁的做法：新增 `TryQuestionExpr` AST 节点，字节码编译器直接生成 `Result` 构造逻辑。

#### 5.2.5 错误多态性（Error Polymorphism）

高阶函数的 `raise?` 标注意味着其错误效应取决于参数：

```cpp
// 推导规则
fn map<T>(arr: Array<T>, f: (T) -> T raise?) -> Array<T> raise? {
    // 若 f 是 noraise，则 map 的 effect = NoError
    // 若 f 是 raise E，则 map 的 effect = ConcreteError(E)
}
```

实现方式：在 ErrorEffectAnalyzer 中，遇到 `raise?` 函数时，根据调用点的实参类型实例化错误效应。

### 5.3 修改文件清单

| 文件 | 修改内容 |
|------|---------|
| `src/type/error_effect.h` (新建) | ErrorEffect、ErrorEffectInfo 定义 |
| `src/ast/ast.h` | FunctionStmt 增加 error_effect_ 字段 |
| `src/parser/parser.h` | 解析 `raise` / `noraise` / `raise?` 语法 |
| `src/type/error_effect_analyzer.h/.cpp` (新建) | 错误效应推导与检查 |
| `src/optimizer/error_desugarer.h/.cpp` (新建) | `try?` 脱糖为 Result |
| `src/bytecode/bytecode_compiler.cpp` | 编译 `TryQuestionExpr`（若采用此方案） |
| `src/main.cpp` | 在 TypeCheck 之后插入 ErrorEffectAnalyzer |
| `src/test/test_error_effect.cpp` (新建) | 单元测试 |

### 5.4 测试计划

- **单元测试**：`src/test/test_error_effect.cpp`
  - `noraise_function_no_raise`：标注 noraise 的函数若 raise 则编译错误
  - `raise_propagation`：调用 raise 函数自动继承 effect
  - `try_catch_eliminates_effect`：try/catch 后effect变为 NoError
  - `try_question_to_result`：`try?` 表达式类型为 `Result<T, E>`
  - `error_polymorphism_map`：`map` 根据闭包参数推导 effect
  - `union_if_branches`：if 分支合并错误效应

- **字节码端到端测试**：
  ```claw
  suberror DivError { DivError(String) }
  fn div(x: Int, y: Int) -> Int raise DivError {
      if y == 0 { raise DivError("division by zero"); }
      return x / y;
  }
  fn main() {
      let res = try? div(6, 0);
      match res {
          Ok(v) => print(v),
          Err(e) => print(999),
      }
  }
  ```

---

## 六、Phase 4：零开销迭代器

### 6.1 目标
- 将 `for x in iterable { body }` 在编译期脱糖为手写循环，消除 IteratorValue 堆对象分配
- 内联迭代器闭包，避免函数调用开销
- 保持与手写 `while` 循环几乎一致的执行效率

### 6.2 技术方案

#### 6.2.1 迭代器脱糖（Iterator Desugaring）

**当前 VM 实现（有开销）**：
```
ITER_CREATE      // 在堆上分配 IteratorValue
ITER_HAS_NEXT    // 虚函数式调用
ITER_NEXT        // 虚函数式调用
JMP_IF_NOT       // 结束循环
... body ...
JMP              // 回到 ITER_HAS_NEXT
ITER_RESET       // 清理
```

**优化后（零开销）**：
```
// for x in arr { body }
// 脱糖为：
// let _i = 0;
// loop {
//     if _i >= arr.len { break; }
//     let x = arr[_i];
//     ... body ...
//     _i = _i + 1;
// }
```

```cpp
// src/optimizer/iterator_desugarer.h
class IteratorDesugarer {
public:
    bool desugar(ast::Program& program);
    
private:
    void desugar_for_stmt(ast::ForStmt& for_stmt);
    
    // 根据 iterable 类型生成不同的脱糖逻辑
    std::unique_ptr<ast::Statement> desugar_array_iteration(
        const ast::ForStmt& for_stmt);
    
    std::unique_ptr<ast::Statement> desugar_range_iteration(
        const ast::ForStmt& for_stmt);
    
    std::unique_ptr<ast::Statement> desugar_enumerate_iteration(
        const ast::ForStmt& for_stmt);
};
```

#### 6.2.2 数组迭代脱糖

```cpp
std::unique_ptr<ast::Statement> IteratorDesugarer::desugar_array_iteration(
    const ast::ForStmt& for_stmt) {
    
    auto span = for_stmt.get_span();
    auto index_var = "_i_" + for_stmt.get_variable();  // 生成唯一索引变量名
    
    // let _i = 0;
    auto init_index = std::make_unique<ast::LetStmt>(index_var, span);
    init_index->set_initializer(int_lit(0));
    
    // arr[_i]
    auto index_expr = std::make_unique<ast::IndexExpr>(
        clone_expr(*for_stmt.get_iterable()),
        std::make_unique<ast::IdentifierExpr>(index_var, span),
        span);
    
    // let x = arr[_i];
    auto init_elem = std::make_unique<ast::LetStmt>(for_stmt.get_variable(), span);
    init_elem->set_initializer(std::move(index_expr));
    
    // _i = _i + 1
    auto increment = std::make_unique<ast::AssignStmt>(
        std::make_unique<ast::IdentifierExpr>(index_var, span),
        bin(TokenType::Op_plus, id(index_var), int_lit(1)),
        span);
    
    // loop body block: { let x = arr[_i]; ...body...; _i = _i + 1; }
    auto loop_body = std::make_unique<ast::BlockStmt>(span);
    loop_body->add_statement(std::move(init_elem));
    
    // 将原 for body 加入
    auto orig_body = for_stmt.release_body();
    if (auto* stmt_body = dynamic_cast<ast::Statement*>(orig_body.get())) {
        loop_body->add_statement(clone_stmt(*stmt_body));
    }
    loop_body->add_statement(std::move(increment));
    
    // loop { ... }
    auto loop = std::make_unique<ast::LoopStmt>(std::move(loop_body), span);
    
    // 外部 block: { let _i = 0; loop { ... } }
    auto outer = std::make_unique<ast::BlockStmt>(span);
    outer->add_statement(std::move(init_index));
    outer->add_statement(std::move(loop));
    
    return outer;
}
```

**注意**：需要在 loop 开头添加条件判断和 `break`：
```
if _i >= arr.len { break; }
```

但当前 AST 可能无 `BreakStmt` 的条件break支持，需扩展或生成 `IfStmt` + `BreakStmt`。

#### 6.2.3 Range 迭代脱糖

```claw
for i in 0..10 { body }
// 脱糖为：
// let _start = 0;
// let _end = 10;
// loop {
//     if _start >= _end { break; }
//     let i = _start;
//     ... body ...
//     _start = _start + 1;
// }
```

#### 6.2.4 闭包迭代器内联

对于高阶迭代模式（如 `arr.map(f).filter(g)`），当前 Claw 无此方法链语法。若未来添加，内联策略为：
- 在优化管线中，将 `arr.map(f)` 识别为 `for x in arr { f(x) }` 模式
- 函数内联 Pass（已存在）负责内联 `f` 和 `g`
- 最终合并为单个循环

### 6.3 修改文件清单

| 文件 | 修改内容 |
|------|---------|
| `src/optimizer/iterator_desugarer.h/.cpp` (新建) | 迭代器脱糖实现 |
| `src/ast/ast.h` | 确认 `BreakStmt` 已存在（已存在） |
| `src/main.cpp` | 在 TCO 之前插入 IteratorDesugarer Pass |
| `src/test/test_iterator_desugarer.cpp` (新建) | 单元测试 |

### 6.4 测试计划

- **单元测试**：`src/test/test_iterator_desugarer.cpp`
  - `array_iteration`：`for x in [1,2,3] { print(x); }` 正确脱糖
  - `range_iteration`：`for i in 0..3 { print(i); }` 正确脱糖
  - `string_iteration`：`for c in "abc" { print(c); }` 正确脱糖
  - `nested_loop`：嵌套 for 循环各自生成唯一索引变量
  - `loop_body_is_block`：原 body 为 BlockStmt 时正确处理

- **性能对比测试**（字节码级别）：
  - 测试脱糖后的 `for` 循环与手写 `loop` 循环生成的字节码数量对比
  - 期望：字节码数量差异 < 10%

- **字节码端到端测试**：
  ```claw
  fn main() {
      let sum = 0;
      for x in [1, 2, 3, 4, 5] {
          sum = sum + x;
      }
      print(sum);
  }
  ```
  期望输出：15

---

## 七、Phase 5：AI 原生设计

### 7.1 目标
- **语义明确**：增强类型推断和错误信息，使 AI 生成的代码能被编译器严格检查并给出精准反馈
- **上下文精简**：优化 AST 序列化格式，减少 AI 上下文窗口中的 token 占用
- **IDE/AI 友好输出**：编译错误信息结构化、可机器解析

### 7.2 技术方案

#### 7.2.1 结构化错误信息（Structured Diagnostics）

当前错误输出为纯文本字符串。改为结构化格式（JSON / 类 LSP Diagnostic）：

```cpp
// src/diagnostics/diagnostic.h (新建)
struct Diagnostic {
    enum class Severity { Error, Warning, Note, Help };
    
    Severity severity;
    SourceSpan span;
    std::string message;
    std::string code;           // 错误码，如 "E0001"
    std::vector<Diagnostic> related; // 关联信息（如"变量在此定义"）
    std::vector<std::string> suggestions; // 修复建议
};

class DiagnosticEmitter {
public:
    void emit(const Diagnostic& diag);
    void emit_error(const SourceSpan& span, const std::string& code, 
                    const std::string& message);
    void emit_type_mismatch(const SourceSpan& span, 
                            const Type& expected, const Type& actual);
    
    // 输出为 JSON（供 AI/IDE 消费）
    std::string to_json() const;
    
    // 输出为人类可读格式（终端）
    std::string to_string() const;
    
private:
    std::vector<Diagnostic> diagnostics_;
};
```

**示例错误输出**：
```json
{
  "severity": "error",
  "code": "E0308",
  "message": "类型不匹配：期望 `Int`，实际得到 `String`",
  "span": { "file": "main.claw", "line": 5, "column": 14 },
  "suggestions": [
    "尝试使用 `.to_int()` 转换",
    "或者将变量类型改为 `String`"
  ]
}
```

#### 7.2.2 增强类型推断（Enhanced Type Inference）

当前 `get_inferred_type()` 为 stub。实现基于 Hindley-Milner 的局部类型推断：

```cpp
// src/type/type_inference.h (新建)
class TypeInference {
public:
    // 统一两个类型（unification）
    bool unify(Type& a, Type& b);
    
    // 推断表达式类型，返回约束集
    Type infer_expr(ast::Expression& expr);
    
    // 泛型参数推断：从实参类型推断泛型参数
    std::vector<Type> infer_generic_args(
        const GenericFunctionType& generic_fn,
        const std::vector<Type>& arg_types);
};
```

**应用场景**：
```claw
fn id<T>(x: T) -> T { return x; }
let a = id(42);      // 当前可能需要显式标注，增强后自动推断 T = Int
let b = id("hello"); // 自动推断 T = String
```

#### 7.2.3 AST 轻量序列化（AI Context Compression）

为 AI 代码生成场景设计紧凑的 AST 文本表示：

```cpp
// src/ast/ast_compact_repr.h (新建)
class CompactASTRepr {
public:
    // 将 AST 转为紧凑的 S-表达式风格文本
    // 例如：
    // (fn main () (block 
    //   (let x Int 42)
    //   (if (> x 0) (print "positive"))
    // ))
    std::string to_compact(const ast::Program& program);
    
    // Token 计数优化：比完整源码节省 30-50% tokens
    size_t estimate_tokens(const std::string& repr);
};
```

这类似于 MoonBit 的"上下文精简"理念：在 AI 的有限上下文窗口中，紧凑的 AST 表示比源码文本携带更多语义信息。

### 7.3 修改文件清单

| 文件 | 修改内容 |
|------|---------|
| `src/diagnostics/diagnostic.h/.cpp` (新建) | 结构化诊断系统 |
| `src/type/type_inference.h/.cpp` (新建) | 增强类型推断 |
| `src/ast/ast_compact_repr.h/.cpp` (新建) | 紧凑 AST 表示 |
| `src/main.cpp` | 支持 `--diagnostics-json` 输出模式 |
| `src/test/test_diagnostics.cpp` (新建) | 诊断系统测试 |
| `src/test/test_type_inference.cpp` (新建) | 类型推断测试 |

### 7.4 测试计划

- **单元测试**：
  - `diagnostic_json_output`：错误信息正确序列化为 JSON
  - `type_mismatch_suggestion`：类型错误提供修复建议
  - `infer_generic_from_arg`：从实参推断泛型参数
  - `compact_ast_size`：紧凑表示比源码节省 tokens

---

## 八、整体时间线与里程碑

| 阶段 | 预估工作量 | 关键交付物 |
|------|-----------|-----------|
| **Phase 1** | 3-4 天 | Pattern AST + 穷尽性检查 + 12 个单元测试 |
| **Phase 2** | 4-5 天 | Monomorphizer + Name Mangling + 10 个单元测试 |
| **Phase 3** | 4-5 天 | Error Effect 系统 + try? 脱糖 + 8 个单元测试 |
| **Phase 4** | 2-3 天 | Iterator Desugarer + 性能对比测试 |
| **Phase 5** | 2-3 天 | 结构化诊断 + 类型推断增强 |
| **集成测试** | 1-2 天 | 全量 `make test` 通过，无回归 |

**总计**：约 16-22 天（按每天 6-8 小时有效工作计算）

---

## 九、风险评估与回退策略

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| Pattern AST 改动面大 | 高 | 保持旧 `Expression` pattern 解析路径作为 fallback，逐步迁移 |
| 单态化导致代码膨胀 | 中 | 实现单例缓存（InstanceKey 去重），限制递归泛型深度 |
| Error Effect 与现有 try/catch 语义冲突 | 中 | 作为**新增层**而非替换：旧 try/catch 仍可用，新 `raise` 标注提供编译期检查 |
| 迭代器脱糖破坏 VM 迭代器调试 | 低 | 保留 `-O0` 下的原始 VM 迭代器行为，仅在 `-O1` 以上脱糖 |

---

## 十、需要用户确认的问题

1. **Phase 优先级**：是否按 `1→2→3→4→5` 顺序执行？或希望调整优先级（如先做 Phase 3 错误处理，因其对现有代码影响最小）？

2. **Pattern AST 设计**：当前 Claw 的 pattern 只是 Expression。是选择 (A) 新建独立的 Pattern 层级，还是 (B) 在 Expression 中扩展 pattern 语义？推荐 (A)，但改动面更大。

3. **错误处理兼容性**：MoonBit 的 `raise` 与 Claw 现有的 `throw` 是互补还是替代关系？建议**共存**：`throw` 保持运行时语义，`raise` 增加编译期追踪。

4. **迭代器脱糖时机**：建议在哪个优化 Pass 之后进行？当前计划放在 TCO 之前，因为脱糖后的 `loop` + `break` 可被 TCO 进一步优化。

5. **AI 原生设计的范围**：Phase 5 的紧凑 AST 表示是否需要在本次实施，还是可以延后？

---

*计划文档版本：v1.0*
*生成时间：2026-05-16*
*待确认后方可进入实施阶段*
