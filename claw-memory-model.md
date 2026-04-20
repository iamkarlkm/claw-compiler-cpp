# Claw Memory Ownership Model

> **核心原则：谁声明谁拥有，block 结束谁回收。不需要垃圾收集器。**

## 1. 设计哲学

Claw 的内存管理模型遵循一条规则：

**每个值有且仅有一个 owner（所有者），owner 的生命周期由声明它的 block 边界决定。**

没有引用计数，没有 mark-sweep，没有 move 语义，没有 copy 语义。
只有声明、借用、回收——全部在编译期确定，零运行时开销。

---

## 2. 三条规则

### 规则 1：`let` 声明 = 所有权

```
fn main() {
    let x: i64 = 10;      // x 的 owner 是 main 的 block
    let arr: u32[5];       // arr 的 owner 是 main 的 block
    // ...
}  // ← main block 结束，x 和 arr 在此处回收
```

**block 结束 = 该 block 内所有 `let` 变量的统一回收点。**
编译器在 block 的 `}` 处生成 `delete` 指令，回收所有局部变量。

### 规则 2：函数参数 = 借用

```
fn add(a: i64, b: i64) -> i64 {
    return a + b;
    // a 和 b 不归 add 的 block 所有，不回收
}

fn main() {
    let x: i64 = 3;
    let y: i64 = 4;
    let z: i64 = add(x, y);  // x, y 借给 add，add 不拥有
}  // 回收 x, y, z
```

**参数的生命周期由调用者的 block 决定，不由被调函数决定。**
被调函数只是借用，不负责回收。

### 规则 3：返回值 = 隐式所有权转移

```
fn create_arr() -> u32[3] {
    let tmp: u32[3];
    tmp[1] = 10;
    tmp[2] = 20;
    tmp[3] = 30;
    return tmp;
    // tmp 不在此处回收——返回值的 owner 转移给调用者
}

fn main() {
    let arr: u32[3] = create_arr();  // arr 拥有返回值的内存
}  // 回收 arr
```

**返回值不需要 `move` 关键字。编译器自动将返回值的 owner 绑定到调用者的 block。**
被调函数的 block 结束时，跳过返回值的回收。

---

## 3. Block 作用域模型

每个 `{ }` block 是一个作用域边界：

```
fn main() {
    let x: i64 = 10;          // main block 拥有 x
    println(x);               // 输出 10
    
    {
        let x: i64 = 99;      // 内层 block 拥有自己的 x（遮蔽外层）
        let y: i64 = x + 1;   // 内层 block 拥有 y
        println(x);           // 输出 99
    }  // ← 回收内层 x 和 y
    
    println(x);               // 输出 10（外层 x 未受影响）
}  // ← 回收外层 x
```

**内层 block 可以遮蔽外层同名变量，退出后自动恢复。**

---

## 4. 堆分配回收机制

### 4.1 栈变量（默认）

基本类型（`i64`, `f64`, `bool`, `char`）直接分配在栈上。
block 结束时由栈帧弹出自动回收，零开销。

### 4.2 堆变量（数组、张量、字符串）

数组 `u32[N]`、张量 `tensor<f32>`、字符串 `string` 等需要堆分配。

**分配时机**：block 入口统一 `new`
**回收时机**：block 出口统一 `delete`

```
fn process() {
    let data: f64[1024];       // block 入口：new f64[1024]
    let weights: tensor<f32>;  // block 入口：new tensor
    
    // ... 使用 data 和 weights ...
    
}  // block 出口：delete data, delete weights
```

**所有堆分配在同一个 block 边界成对出现，不存在内存泄漏。**

### 4.3 为什么不需要 shared_ptr

| 场景 | shared_ptr (引用计数) | Claw 模型 (独占所有权) |
|------|----------------------|----------------------|
| 赋值 `a = b` | 引用计数 +1 | 值拷贝（新 owner） |
| 函数传参 | 引用计数 +1 | 借用，不改变 ownership |
| 函数返回 | 引用计数不变 | ownership 转移给调用者 |
| block 结束 | 引用计数 -1，可能不回收 | 必定回收，编译期确定 |

Claw 中不存在两个变量同时拥有同一块内存的情况。
因此不需要引用计数，不需要 `shared_ptr`，不需要垃圾收集器。

---

## 5. 全局变量与常量

### 5.1 全局变量

```
// 顶层声明，不在任何函数内
let VERSION: i64 = 1;
```

全局变量写入全局 map，生命周期 = 程序运行期间。
程序退出时统一回收。

### 5.2 常量

```
const PI: f64 = 3.14159265;
```

常量在编译期求值，不分配运行时内存（内联到使用处）。
如果常量类型需要堆分配（如字符串），程序启动时分配，退出时回收。

---

## 6. 与 Rust 所有权模型的对比

| 特性 | Rust | Claw |
|------|------|------|
| 所有权规则 | 值有唯一 owner | 值有唯一 owner |
| move 语义 | 显式 move，变量变空 | 不需要——返回值隐式转移 |
| borrow checker | 编译期检查 `&T` / `&mut T` | 参数即借用，无需标注 |
| 生命周期标注 | `<'a>` 显式标注 | 由 block 嵌套关系隐式决定 |
| 堆分配 | `Box<T>` 显式 | `let` 声明自动，block 边界统一 |
| 引用计数 | `Rc<T>` / `Arc<T>` 可选 | 不支持，也不需要 |
| 垃圾收集 | 无 | 无 |

**Claw 比 Rust 更简单：去掉 move、去掉生命周期标注、去掉 borrow checker。**
用 block 边界统一管理，编译器自动推导所有权转移。

---

## 7. 编译器实现要点

### 7.1 Interpreter 层

```
execute_block(node):
    push_scope()                    // 创建新栈帧
    for stmt in block.stmts:
        execute_statement(stmt)
        if (return/break/continue) break
    pop_scope()                     // 统一回收本帧所有变量
```

### 7.2 Codegen 层（未来）

```
codegen_block(node):
    emit("PUSH_FRAME")
    for stmt in block.stmts:
        codegen_statement(stmt)
    emit("POP_FRAME")              // 生成 delete 指令
```

### 7.3 变量查找

```
scoped_get(name):
    for frame in variable_stack (从内到外):
        if frame.has(name): return frame[name]
    return global_map[name]        // fallback 到全局
```

---

## 8. 禁止清单

以下模式在 Claw 中**不存在**：

- ❌ `shared_ptr` / 引用计数
- ❌ 垃圾收集器（mark-sweep / generational / 任何形式）
- ❌ `move` 关键字（所有权转移是隐式的）
- ❌ `copy` 关键字（值语义是默认的）
- ❌ 循环引用（不存在共享所有权，不可能循环）
- ❌ `weak_ptr`（不存在共享所有权，不需要打破循环）
- ❌ 手动 `free` / `delete`（block 边界自动处理）

---

*本文档定义了 Claw 语言的内存所有权模型。所有编译器实现必须严格遵守上述规则。*
