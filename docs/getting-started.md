# Claw 语言快速入门

> 本文面向首次使用 Claw 的开发者，15 分钟内写出第一个程序。

---

## 安装

### 依赖

- clang++ (支持 C++17)
- LLVM (通过 `llvm-config` 自动探测)
- libreadline
- libmsquic (可选，启用 WebTransport)

### 编译

```bash
git clone <repo-url>
cd claw-compiler
make all          # 编译 claw + claw-lsp + claw-repl + claw-debugger
```

如果 LLVM 探测失败，手动指定：

```bash
make all LLVM_PREFIX=/usr/local/opt/llvm
```

---

## Hello World

创建 `hello.claw`：

```claw
fn main() {
    println("Hello, World!");
}
```

运行：

```bash
./claw hello.claw          # 默认：AST 解释器
./claw -b hello.claw       # Bytecode VM 模式
./claw --aot hello.claw    # AOT 编译为原生可执行文件
```

---

## 基础语法

### 变量与类型

```claw
let name = "Claw"           // 字符串
let age = 3                 // 整数
let pi = 3.14159            // 浮点数
let active = true           // 布尔值
```

Claw 是**静态类型**语言，但支持局部类型推断。编译器会自动推导 `name` 为 `string`，`age` 为 `int`。

### 函数

```claw
fn add(a, b) {
    return a + b;
}

fn greet(name) {
    println("Hello, " + name + "!");
}

fn main() {
    let sum = add(10, 20);
    println(sum);           // 30
    greet("Claw");
}
```

### 控制流

```claw
fn main() {
    let score = 85;

    if score >= 90 {
        println("A");
    } else if score >= 80 {
        println("B");
    } else {
        println("C");
    }

    let i = 0;
    while i < 5 {
        println(i);
        i = i + 1;
    }
}
```

### 数组

```claw
fn main() {
    let arr = arr_range(0, 5, 1);   // [0, 1, 2, 3, 4]
    println(arr_len(arr));           // 5

    let arr2 = arr_push(arr, 10);    // [0, 1, 2, 3, 4, 10]
    println(arr_len(arr2));          // 6

    let arr3 = arr_slice(arr2, 1, 4); // [1, 2, 3]
}
```

---

## 执行模式对比

| 模式 | 命令 | 特点 | 适用场景 |
|------|------|------|---------|
| 解释器 | `./claw file.claw` | 启动最快，逐行执行 | 开发调试 |
| Bytecode VM | `./claw -b file.claw` | 编译为字节码后执行 | 平衡速度 |
| JIT | `./claw -j file.claw` | 热点代码编译为机器码 | 计算密集型 |
| AOT | `./claw --aot file.claw` | 完全编译为原生可执行文件 | 生产部署 |
| C Codegen | `./claw -C file.claw` | 生成 C 源码 | 跨平台移植 |

---

## 标准库速查

### I/O

```claw
print(value)                // 打印（不换行）
println(value)              // 打印（换行）
input()                     // 读取一行输入
read_file(path)             // 读取文件内容为字符串
write_file(path, content)   // 写入文件，返回是否成功
append_file(path, content)  // 追加到文件
```

### 字符串

```claw
str_len(s)                  // 长度
str_upper(s)                // 转大写
str_lower(s)                // 转小写
str_trim(s)                 // 去首尾空白
str_contains(s, sub)        // 是否包含子串
str_find(s, sub)            // 查找位置（-1 表示未找到）
str_replace(s, from, to)    // 替换所有匹配
str_split(s, delim)         // 分割为数组
str_substring(s, start, len) // 子串
str_starts_with(s, prefix)  // 是否以 prefix 开头
str_ends_with(s, suffix)    // 是否以 suffix 结尾
str_reverse(s)              // 反转
str_repeat(s, n)            // 重复 n 次
str_join(arr, delim)        // 用 delim 连接数组元素
format(fmt, arg)            // 简单格式化（替换第一个 {}）
```

### 数学

```claw
abs(x)                      // 绝对值
sin(x), cos(x), tan(x)      // 三角函数
asin(x), acos(x), atan(x)   // 反三角函数
atan2(y, x)                 // 反正切（考虑象限）
sqrt(x)                     // 平方根
pow(base, exp)              // 幂
exp(x)                      // e^x
log(x)                      // 自然对数
log10(x)                    // 常用对数
floor(x), ceil(x), round(x), trunc(x)  // 取整
min(a, b), max(a, b)        // 最值
sign(x)                     // 符号（-1, 0, 1）
pi()                        // π
e()                         // e
random()                    // [0, 1) 随机浮点数
random_int(min, max)        // [min, max] 随机整数
```

### 数组

```claw
arr_len(arr)                // 长度
arr_push(arr, val)          // 末尾添加
arr_pop(arr)                // 移除并返回末尾元素
arr_insert(arr, idx, val)   // 在 idx 处插入
arr_remove(arr, idx)        // 移除 idx 处元素
arr_sort(arr)               // 升序排序
arr_reverse(arr)            // 反转
arr_find(arr, val)          // 查找索引（-1 表示未找到）
arr_contains(arr, val)      // 是否包含
arr_unique(arr)             // 去重
arr_concat(a, b)            // 连接两个数组
arr_slice(arr, start, end)  // 切片 [start, end)
arr_range(start, end, step) // 生成等差数列
arr_fill(n, val)            // 生成 n 个 val
```

### 文件系统

```claw
file_exists(path)           // 文件是否存在
file_remove(path)           // 删除文件
file_rename(old, new)       // 重命名
file_size(path)             // 文件大小（字节）
mkdir(path)                 // 创建目录
```

### 类型转换

```claw
to_int(value)               // 转整数
to_float(value)             // 转浮点数
to_string(value)            // 转字符串
to_bool(value)              // 转布尔值
type_of(value)              // 返回类型名字符串
```

---

## 常见问题

**Q: `mod` 是关键字，怎么取模？**  
A: 目前 `mod` 被保留为模块关键字。取模可用 `x - (x / y) * y` 或等待后续版本提供 `fmod` 函数。

**Q: 编译缓存导致修改未生效？**  
A: 删除 `.claw_cache` 目录强制重新编译。

**Q: 如何查看编译后的字节码？**  
A: 使用 `./claw -b --verbose file.claw`。

---

## 下一步

- 阅读 [`claw-language-spec.md`](claw-language-spec.md) 了解完整语法
- 阅读 [`claw-memory-model.md`](claw-memory-model.md) 理解内存管理
- 查看 `docs/*.claw` 示例代码
