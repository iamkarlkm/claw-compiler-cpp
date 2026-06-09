# Claw 标准库参考

> 本文列出 Claw v0.2.0 内置标准库函数。所有函数无需导入即可直接使用。

---

## 约定

- `num` 表示 `int` 或 `float`
- `void` 表示副作用函数，返回值通常为 `nil`
- 数组类型写作 `array`，内部元素类型为运行期确定
- 字符串索引从 `0` 开始
- 数组切片范围 `[start, end)`，左闭右开

---

## I/O 模块

| 函数 | 签名 | 说明 |
|------|------|------|
| `print` | `void print(value)` | 打印值，不换行 |
| `println` | `void println(value)` | 打印值并换行 |
| `input` | `string input()` | 从标准输入读取一行 |
| `read_file` | `string read_file(path)` | 读取整个文件为字符串，文件不存在返回空字符串 |
| `write_file` | `bool write_file(path, content)` | 写入文件，覆盖已有内容 |
| `append_file` | `bool append_file(path, content)` | 追加内容到文件末尾 |

**示例：**

```claw
let content = read_file("data.txt");
println(content);

let ok = write_file("out.txt", "result");
if ok {
    println("写入成功");
}
```

---

## 字符串模块

| 函数 | 签名 | 说明 |
|------|------|------|
| `str_len` | `int str_len(s)` | 返回字符串长度（字节数） |
| `str_contains` | `bool str_contains(s, sub)` | 是否包含子串 |
| `str_find` | `int str_find(s, sub)` | 返回子串首次出现索引，未找到返回 `-1` |
| `str_replace` | `string str_replace(s, from, to)` | 替换所有 `from` 为 `to` |
| `str_split` | `array str_split(s, delim)` | 按分隔符分割为字符串数组 |
| `str_upper` | `string str_upper(s)` | 转大写 |
| `str_lower` | `string str_lower(s)` | 转小写 |
| `str_trim` | `string str_trim(s)` | 去除首尾空白字符 |
| `str_substring` | `string str_substring(s, start, len)` | 截取子串，`start` 从 `0` 开始 |
| `str_starts_with` | `bool str_starts_with(s, prefix)` | 是否以 prefix 开头 |
| `str_ends_with` | `bool str_ends_with(s, suffix)` | 是否以 suffix 结尾 |
| `str_reverse` | `string str_reverse(s)` | 反转字符串 |
| `str_repeat` | `string str_repeat(s, n)` | 重复 `n` 次，`n <= 0` 返回空串 |
| `str_join` | `string str_join(arr, delim)` | 用 `delim` 连接数组中的字符串 |
| `format` | `string format(fmt, arg)` | 将 `fmt` 中第一个 `{}` 替换为 `arg` |

**示例：**

```claw
let s = "  Hello World  ";
println(str_trim(s));           // "Hello World"
println(str_lower(s));          // "  hello world  "
println(str_contains(s, "World")); // true

let parts = str_split("a,b,c", ",");
println(arr_len(parts));        // 3

let msg = format("Value: {}", 42);
println(msg);                   // "Value: 42"
```

---

## 数学模块

| 函数 | 签名 | 说明 |
|------|------|------|
| `abs` | `num abs(x)` | 绝对值 |
| `sin` | `float sin(x)` | 正弦（弧度） |
| `cos` | `float cos(x)` | 余弦（弧度） |
| `tan` | `float tan(x)` | 正切（弧度） |
| `asin` | `float asin(x)` | 反正弦 |
| `acos` | `float acos(x)` | 反余弦 |
| `atan` | `float atan(x)` | 反正切 |
| `atan2` | `float atan2(y, x)` | 四象限反正切 |
| `sqrt` | `float sqrt(x)` | 平方根 |
| `pow` | `float pow(base, exp)` | 幂运算 |
| `exp` | `float exp(x)` | e 的 x 次方 |
| `log` | `float log(x)` | 自然对数 |
| `log10` | `float log10(x)` | 以 10 为底对数 |
| `floor` | `float floor(x)` | 向下取整 |
| `ceil` | `float ceil(x)` | 向上取整 |
| `round` | `float round(x)` | 四舍五入 |
| `trunc` | `float trunc(x)` | 向零截断 |
| `min` | `num min(a, b)` | 较小值 |
| `max` | `num max(a, b)` | 较大值 |
| `sign` | `int sign(x)` | 符号函数：正数 `1`，负数 `-1`，零 `0` |
| `pi` | `float pi()` | 圆周率 π |
| `e` | `float e()` | 自然常数 e |
| `random` | `float random()` | `[0, 1)` 均匀分布随机数 |
| `random_int` | `int random_int(min, max)` | `[min, max]` 均匀分布随机整数 |
| `random_seed` | `void random_seed(seed)` | 设置随机种子（当前为占位实现） |

**示例：**

```claw
let r = sqrt(2);
println(r);                     // 1.41421...

let angle = pi() / 4;
println(sin(angle));            // 0.7071...

let dice = random_int(1, 6);
println(dice);
```

---

## 数组模块

| 函数 | 签名 | 说明 |
|------|------|------|
| `arr_len` | `int arr_len(arr)` | 返回元素个数 |
| `arr_push` | `array arr_push(arr, val)` | 在末尾添加元素，返回修改后的数组 |
| `arr_pop` | `value arr_pop(arr)` | 移除并返回末尾元素，空数组返回 `nil` |
| `arr_insert` | `array arr_insert(arr, idx, val)` | 在 `idx` 处插入，返回修改后的数组 |
| `arr_remove` | `array arr_remove(arr, idx)` | 删除 `idx` 处元素，返回修改后的数组 |
| `arr_sort` | `array arr_sort(arr)` | 升序排序（按数值比较），返回原数组 |
| `arr_reverse` | `array arr_reverse(arr)` | 反转顺序，返回原数组 |
| `arr_find` | `int arr_find(arr, val)` | 查找元素索引，未找到返回 `-1` |
| `arr_contains` | `bool arr_contains(arr, val)` | 是否包含指定元素 |
| `arr_unique` | `array arr_unique(arr)` | 去重（基于 `to_string` 比较） |
| `arr_concat` | `array arr_concat(a, b)` | 连接两个数组 |
| `arr_slice` | `array arr_slice(arr, start, end)` | 截取 `[start, end)` |
| `arr_range` | `array arr_range(start, end, step)` | 生成等差数列 |
| `arr_fill` | `array arr_fill(n, val)` | 生成包含 `n` 个 `val` 的数组 |

**示例：**

```claw
let nums = arr_range(1, 10, 2);   // [1, 3, 5, 7, 9]
println(arr_len(nums));            // 5

let nums2 = arr_push(nums, 11);
println(arr_len(nums2));           // 6

let evens = arr_fill(5, 0);       // [0, 0, 0, 0, 0]
```

---

## 文件系统模块

| 函数 | 签名 | 说明 |
|------|------|------|
| `file_exists` | `bool file_exists(path)` | 文件是否存在 |
| `file_remove` | `bool file_remove(path)` | 删除文件 |
| `file_rename` | `bool file_rename(old, new)` | 重命名文件 |
| `file_size` | `int file_size(path)` | 文件大小（字节），不存在返回 `0` |
| `mkdir` | `bool mkdir(path)` | 创建目录，已存在返回 `false` |

---

## 类型转换模块

| 函数 | 签名 | 说明 |
|------|------|------|
| `to_int` | `int to_int(value)` | 截断为整数 |
| `to_float` | `float to_float(value)` | 转为浮点数 |
| `to_string` | `string to_string(value)` | 转为字符串 |
| `to_bool` | `bool to_bool(value)` | 转为布尔值 |
| `type_of` | `string type_of(value)` | 返回类型名：`nil`/`bool`/`int`/`float`/`string`/`array`/`function` |

**示例：**

```claw
println(type_of(42));       // "int"
println(type_of("hello"));  // "string"
println(type_of(arr_fill(3, 0))); // "array"

let n = to_int(3.14);
println(n);                 // 3
```

---

## 注意事项

1. **无 `mod` 函数**：`mod` 是 Claw 关键字（用于模块声明），暂不提供取模内置函数。可用 `x - (x / y) * y` 替代。
2. **数组函数修改语义**：`arr_push`、`arr_insert`、`arr_remove` 等函数会修改原数组并返回它（类似引用语义）。
3. **字符串比较**：`arr_find`、`arr_contains`、`arr_unique` 内部使用 `to_string()` 结果比较。
4. **文件操作路径**：使用相对路径或绝对路径，当前工作目录为运行 `./claw` 的目录。
