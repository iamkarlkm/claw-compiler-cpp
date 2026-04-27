// stdlib/stdlib.h - Claw 标准库核心定义
// 提供基础 I/O、字符串、数学、数组、文件操作等运行时支持

#ifndef CLAW_STDLIB_H
#define CLAW_STDLIB_H

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cmath>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <random>
#include <chrono>
#include <iomanip>

namespace claw {
namespace stdlib {

// ============================================================================
// 值类型封装 (与 VM 值系统兼容)
// ============================================================================

enum class ValueType {
    Nil,
    Bool,
    Int,
    Float,
    String,
    Array,
    Function,
    FileHandle
};

struct Value {
    ValueType type = ValueType::Nil;
    
    union {
        bool bool_val;
        int64_t int_val;
        double float_val;
    };
    
    std::string string_val;
    std::vector<Value> array_val;
    std::shared_ptr<std::fstream> file_handle;
    std::function<Value(const std::vector<Value>&)> func_val;
    
    Value() = default;
    explicit Value(bool v) : type(ValueType::Bool), bool_val(v) {}
    explicit Value(int v) : type(ValueType::Int), int_val(v) {}
    explicit Value(int64_t v) : type(ValueType::Int), int_val(v) {}
    explicit Value(double v) : type(ValueType::Float), float_val(v) {}
    explicit Value(const char* v) : type(ValueType::String), string_val(v) {}
    explicit Value(std::string v) : type(ValueType::String), string_val(std::move(v)) {}
    explicit Value(std::vector<Value> v) : type(ValueType::Array), array_val(std::move(v)) {}
    
    static Value from_int(int64_t v) { return Value(v); }
    static Value from_float(double v) { return Value(v); }
    static Value from_bool(bool v) { return Value(v); }
    static Value from_string(const std::string& v) { return Value(v); }
    static Value from_string(std::string&& v) { return Value(std::move(v)); }
    
    bool is_nil() const { return type == ValueType::Nil; }
    bool is_bool() const { return type == ValueType::Bool; }
    bool is_int() const { return type == ValueType::Int; }
    bool is_float() const { return type == ValueType::Float; }
    bool is_number() const { return is_int() || is_float(); }
    bool is_string() const { return type == ValueType::String; }
    bool is_array() const { return type == ValueType::Array; }
    
    double to_number() const {
        if (is_int()) return static_cast<double>(int_val);
        if (is_float()) return float_val;
        if (is_string()) {
            try { return std::stod(string_val); } catch (...) { return 0.0; }
        }
        return 0.0;
    }
    
    std::string to_string() const;
    int64_t to_int() const;
    bool to_bool() const;
    
    bool equals(const Value& other) const;
};

// ============================================================================
// I/O 模块
// ============================================================================

namespace io {

// 打印值 (不带换行)
Value print(const std::vector<Value>& args);

// 打印值 (带换行)
Value println(const std::vector<Value>& args);

// 格式化输出 (类似 printf)
Value fmt_print(const std::vector<Value>& args);

// 读取用户输入
Value input(const std::vector<Value>& args);

// 读取文件内容
Value read_file(const std::vector<Value>& args);

// 写入文件
Value write_file(const std::vector<Value>& args);

// 追加到文件
Value append_file(const std::vector<Value>& args);

} // namespace io

// ============================================================================
// 字符串模块
// ============================================================================

namespace string {

// 获取字符串长度
Value len(const std::vector<Value>& args);

// 字符串分割
Value split(const std::vector<Value>& args);

// 去除首尾空白
Value trim(const std::vector<Value>& args);

// 字符串替换
Value replace(const std::vector<Value>& args);

// 检查包含
Value contains(const std::vector<Value>& args);

// 子串
Value substring(const std::vector<Value>& args);

// 查找位置
Value find(const std::vector<Value>& args);

// 转换为大写
Value upper(const std::vector<Value>& args);

// 转换为小写
Value lower(const std::vector<Value>& args);

// 检查前缀
Value starts_with(const std::vector<Value>& args);

// 检查后缀
Value ends_with(const std::vector<Value>& args);

// 格式化字符串 (类似 Python str.format)
Value format(const std::vector<Value>& args);

// 重复字符串
Value repeat(const std::vector<Value>& args);

// 反转字符串
Value reverse(const std::vector<Value>& args);

// 连接字符串数组
Value join(const std::vector<Value>& args);

} // namespace string

// ============================================================================
// 数学模块
// ============================================================================

namespace math {

// 绝对值
Value abs(const std::vector<Value>& args);

// 正弦
Value sin(const std::vector<Value>& args);

// 余弦
Value cos(const std::vector<Value>& args);

// 正切
Value tan(const std::vector<Value>& args);

// 反正弦
Value asin(const std::vector<Value>& args);

// 反余弦
Value acos(const std::vector<Value>& args);

// 反正切
Value atan(const std::vector<Value>& args);

// 反正切2
Value atan2(const std::vector<Value>& args);

// 平方根
Value sqrt(const std::vector<Value>& args);

// 幂函数
Value pow(const std::vector<Value>& args);

// 指数
Value exp(const std::vector<Value>& args);

// 自然对数
Value log(const std::vector<Value>& args);

// 以10为底对数
Value log10(const std::vector<Value>& args);

// 向下取整
Value floor(const std::vector<Value>& args);

// 向上取整
Value ceil(const std::vector<Value>& args);

// 四舍五入
Value round(const std::vector<Value>& args);

// 截断
Value trunc(const std::vector<Value>& args);

// 最小值
Value min(const std::vector<Value>& args);

// 最大值
Value max(const std::vector<Value>& args);

// 取模
Value mod(const std::vector<Value>& args);

// 符号函数
Value sign(const std::vector<Value>& args);

// 常量
Value pi(const std::vector<Value>& args);
Value e(const std::vector<Value>& args);

// 随机数
Value random(const std::vector<Value>& args);
Value random_int(const std::vector<Value>& args);
Value random_seed(const std::vector<Value>& args);

} // namespace math

// ============================================================================
// 数组模块
// ============================================================================

namespace array {

// 获取数组长度
Value len(const std::vector<Value>& args);

// 添加元素到末尾
Value push(const std::vector<Value>& args);

// 移除末尾元素
Value pop(const std::vector<Value>& args);

// 插入元素
Value insert(const std::vector<Value>& args);

// 移除元素
Value remove(const std::vector<Value>& args);

// 排序 (升序)
Value sort(const std::vector<Value>& args);

// 反转数组
Value reverse(const std::vector<Value>& args);

// 查找元素
Value find(const std::vector<Value>& args);

// 过滤数组
Value filter(const std::vector<Value>& args);

// 映射数组
Value map(const std::vector<Value>& args);

// 归约数组
Value reduce(const std::vector<Value>& args);

// 切片
Value slice(const std::vector<Value>& args);

// 去重
Value unique(const std::vector<Value>& args);

// 包含检查
Value contains(const std::vector<Value>& args);

// 数组连接
Value concat(const std::vector<Value>& args);

// 范围生成
Value range(const std::vector<Value>& args);

// 填充
Value fill(const std::vector<Value>& args);

} // namespace array

// ============================================================================
// 文件模块
// ============================================================================

namespace file {

// 打开文件
Value open(const std::vector<Value>& args);

// 关闭文件
Value close(const std::vector<Value>& args);

// 读取一行
Value read_line(const std::vector<Value>& args);

// 读取全部
Value read_all(const std::vector<Value>& args);

// 写入内容
Value write(const std::vector<Value>& args);

// 检查文件存在
Value exists(const std::vector<Value>& args);

// 删除文件
Value remove(const std::vector<Value>& args);

// 重命名文件
Value rename(const std::vector<Value>& args);

// 获取文件大小
Value size(const std::vector<Value>& args);

// 创建目录
Value mkdir(const std::vector<Value>& args);

} // namespace file

// ============================================================================
// 类型转换模块
// ============================================================================

namespace convert {

// 转整数
Value to_int(const std::vector<Value>& args);

// 转浮点数
Value to_float(const std::vector<Value>& args);

// 转字符串
Value to_string(const std::vector<Value>& args);

// 转布尔值
Value to_bool(const std::vector<Value>& args);

// 获取类型名
Value type_of(const std::vector<Value>& args);

} // namespace convert

// ============================================================================
// 标准库注册表
// ============================================================================

class StandardLibrary {
public:
    using FunctionMap = std::unordered_map<std::string, std::function<Value(const std::vector<Value>&)>>;
    
    static StandardLibrary& instance();
    
    // 注册所有标准库函数
    void register_all();
    
    // 注册单个模块
    void register_io();
    void register_string();
    void register_math();
    void register_array();
    void register_file();
    void register_convert();
    
    // 获取函数
    std::function<Value(const std::vector<Value>&)> get_function(const std::string& name) const;
    
    // 检查函数是否存在
    bool has_function(const std::string& name) const;
    
    // 获取所有函数名
    std::vector<std::string> get_function_names() const;
    
    const FunctionMap& functions() const { return functions_; }

private:
    StandardLibrary() = default;
    FunctionMap functions_;
};

} // namespace stdlib
} // namespace claw

#endif // CLAW_STDLIB_H
