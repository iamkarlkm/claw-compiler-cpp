// ir.h - Claw 中间表示 (IR) 定义
// 基于 SSA 形式，支持多后端代码生成

#ifndef CLAW_IR_H
#define CLAW_IR_H

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <optional>
#include "../common/common.h"

namespace claw {
namespace ir {

// ============================================================================
// Type Definitions (与类型系统对齐)
// ============================================================================

enum class PrimitiveTypeKind {
    Void, Bool, Int8, Int16, Int32, Int64,
    UInt8, UInt16, UInt32, UInt64,
    Float32, Float64, String, Bytes
};

struct Type {
    virtual ~Type() = default;
    virtual std::string to_string() const = 0;
    virtual bool equals(const Type& other) const = 0;
};

struct PrimitiveType : public Type {
    PrimitiveTypeKind kind;
    explicit PrimitiveType(PrimitiveTypeKind k) : kind(k) {}
    std::string to_string() const override;
    bool equals(const Type& other) const override;
};

struct PointerType : public Type {
    std::shared_ptr<Type> pointee;
    explicit PointerType(std::shared_ptr<Type> p) : pointee(std::move(p)) {}
    std::string to_string() const override;
    bool equals(const Type& other) const override;
};

struct ArrayType : public Type {
    std::shared_ptr<Type> element_type;
    int64_t size;
    ArrayType(std::shared_ptr<Type> elem, int64_t sz) 
        : element_type(std::move(elem)), size(sz) {}
    std::string to_string() const override;
    bool equals(const Type& other) const override;
};

struct FunctionType : public Type {
    std::shared_ptr<Type> return_type;
    std::vector<std::shared_ptr<Type>> param_types;
    FunctionType(std::shared_ptr<Type> ret, std::vector<std::shared_ptr<Type>> params)
        : return_type(std::move(ret)), param_types(std::move(params)) {}
    std::string to_string() const override;
    bool equals(const Type& other) const override;
};

// ============================================================================
// Value & Instruction Definitions
// ============================================================================

// 前向声明
struct Value;
struct Instruction;
struct BasicBlock;
struct Function;

// Value 表示一个 IR 级别的值（常量、参数、指令结果）
struct Value {
    std::string name;
    std::shared_ptr<Type> type;
    std::weak_ptr<Instruction> defining_inst;
    bool is_constant = false;
    
    // 常量值
    std::variant<
        int64_t, double, std::string, bool, std::vector<int8_t>
    > constant_value;
    
    Value(std::string n, std::shared_ptr<Type> t) 
        : name(std::move(n)), type(std::move(t)) {}
    
    bool is_instruction() const { return !defining_inst.expired(); }
    bool is_argument() const { return defining_inst.expired() && !is_constant; }
};

// 操作码枚举
enum class OpCode {
    // 算术运算
    Add, Sub, Mul, Div, Mod,
    // 比较运算
    Eq, Ne, Lt, Le, Gt, Ge,
    // 逻辑运算
    And, Or, Not,
    // 位运算
    BitAnd, BitOr, BitXor, BitNot, Shl, Shr,
    // 内存操作
    Alloca, Load, Store, GetElementPtr,
    // 类型转换
    Trunc, ZExt, SExt, FPTrunc, FPExt, FPToSI, SIToFP,
    // 控制流
    Br, CondBr, Switch, Call, Ret, Phi,
    // 张量操作
    TensorCreate, TensorLoad, TensorStore, TensorMatmul,
    // 特殊操作
    Print, Panic, Unreachable
};

// 指令基类
struct Instruction : public std::enable_shared_from_this<Instruction> {
    OpCode opcode;
    std::shared_ptr<Type> type;
    std::vector<std::shared_ptr<Value>> operands;
    std::string name;
    SourceLocation loc;
    
    Instruction(OpCode op, std::shared_ptr<Type> ty) 
        : opcode(op), type(std::move(ty)) {}
    
    virtual ~Instruction() = default;
    virtual std::string to_string() const;
    virtual std::string get_op_name() const;
};

// ============================================================================
// Specific Instruction Types
// ============================================================================

struct BinaryInst : public Instruction {
    BinaryInst(OpCode op, std::shared_ptr<Value> lhs, std::shared_ptr<Value> rhs, 
               std::shared_ptr<Type> ty)
        : Instruction(op, std::move(ty)) {
        operands = {std::move(lhs), std::move(rhs)};
    }
};

struct UnaryInst : public Instruction {
    UnaryInst(OpCode op, std::shared_ptr<Value> operand, std::shared_ptr<Type> ty)
        : Instruction(op, std::move(ty)) {
        operands = {std::move(operand)};
    }
};

struct CallInst : public Instruction {
    std::string callee_name;
    CallInst(std::string func, std::vector<std::shared_ptr<Value>> args,
             std::shared_ptr<Type> ret_ty)
        : Instruction(OpCode::Call, std::move(ret_ty)), callee_name(std::move(func)) {
        operands = std::move(args);
    }
};

struct LoadInst : public Instruction {
    std::shared_ptr<Value> address;
    LoadInst(std::shared_ptr<Value> addr, std::shared_ptr<Type> ty)
        : Instruction(OpCode::Load, std::move(ty)), address(std::move(addr)) {
        operands = {address};
    }
};

struct StoreInst : public Instruction {
    std::shared_ptr<Value> address;
    std::shared_ptr<Value> value;
    StoreInst(std::shared_ptr<Value> addr, std::shared_ptr<Value> val)
        : Instruction(OpCode::Store, nullptr), address(std::move(addr)), value(std::move(val)) {
        operands = {address, value};
    }
};

struct AllocaInst : public Instruction {
    int64_t count;  // 数组元素数量，1 表示标量
    AllocaInst(std::shared_ptr<Type> elem_ty, int64_t cnt, std::shared_ptr<Type> ptr_ty)
        : Instruction(OpCode::Alloca, std::move(ptr_ty)), count(cnt) {
        // 元素类型作为元数据存储
    }
    std::shared_ptr<Type> get_allocated_type() const { return type->pointee; }
};

struct ReturnInst : public Instruction {
    ReturnInst(std::shared_ptr<Value> val)
        : Instruction(OpCode::Ret, nullptr) {
        if (val) operands.push_back(std::move(val));
    }
};

struct BranchInst : public Instruction {
    std::shared_ptr<BasicBlock> target;
    BranchInst(std::shared_ptr<BasicBlock> bb)
        : Instruction(OpCode::Br, nullptr), target(std::move(bb)) {
        operands.push_back(nullptr);  // placeholder
    }
};

struct CondBranchInst : public Instruction {
    std::shared_ptr<BasicBlock> true_block;
    std::shared_ptr<BasicBlock> false_block;
    CondBranchInst(std::shared_ptr<Value> cond, 
                   std::shared_ptr<BasicBlock> true_bb,
                   std::shared_ptr<BasicBlock> false_bb)
        : Instruction(OpCode::CondBr, nullptr), 
          true_block(std::move(true_bb)), 
          false_block(std::move(false_bb)) {
        operands = {std::move(cond)};
    }
};

struct PhiInst : public Instruction {
    std::vector<std::pair<std::shared_ptr<BasicBlock>, std::shared_ptr<Value>>> incoming;
    PhiInst(std::shared_ptr<Type> ty) : Instruction(OpCode::Phi, std::move(ty)) {}
    void add_incoming(std::shared_ptr<BasicBlock> bb, std::shared_ptr<Value> val) {
        incoming.emplace_back(std::move(bb), std::move(val));
    }
};

// ============================================================================
// Basic Block
// ============================================================================

struct BasicBlock {
    std::string name;
    std::vector<std::shared_ptr<Instruction>> instructions;
    std::shared_ptr<Instruction> terminator;  // 终止指令 (ret/br)
    std::weak_ptr<Function> parent;
    
    // 前驱和后继块
    std::vector<std::weak_ptr<BasicBlock>> predecessors;
    std::vector<std::weak_ptr<BasicBlock>> successors;
    
    explicit BasicBlock(std::string n) : name(std::move(n)) {}
    
    void add_instruction(std::shared_ptr<Instruction> inst);
    void set_terminator(std::shared_ptr<Instruction> term);
    
    // 获取活跃变量集合（用于 SSA 构造）
    std::vector<std::string> get_live_variables() const;
};

// ============================================================================
// Function
// ============================================================================

struct Function {
    std::string name;
    std::shared_ptr<Type> return_type;
    std::vector<std::shared_ptr<Value>> arguments;  // 参数列表
    std::vector<std::shared_ptr<BasicBlock>> blocks;
    std::unordered_map<std::string, std::shared_ptr<Value>> symbol_table;
    
    // 函数属性
    bool is_extern = false;
    bool is_variadic = false;
    bool is_recursive = false;
    
    explicit Function(std::string n, std::shared_ptr<Type> ret)
        : name(std::move(n)), return_type(std::move(ret)) {}
    
    std::shared_ptr<BasicBlock> create_block(std::string name);
    std::shared_ptr<BasicBlock> get_entry_block() const;
    void add_block(std::shared_ptr<BasicBlock> bb);
};

// ============================================================================
// Module (顶层 IR 容器)
// ============================================================================

struct Module {
    std::string name;
    std::vector<std::shared_ptr<Function>> functions;
    std::unordered_map<std::string, std::shared_ptr<Function>> function_map;
    
    // 全局变量
    std::unordered_map<std::string, std::shared_ptr<Value>> globals;
    
    // 数据段（常量字符串等）
    std::vector<std::pair<std::string, std::string>> string_constants;
    
    explicit Module(std::string n) : name(std::move(n)) {}
    
    void add_function(std::shared_ptr<Function> func);
    std::shared_ptr<Function> get_function(const std::string& name) const;
    void add_global(std::string name, std::shared_ptr<Value> val);
    std::string add_string_constant(const std::string& str);
};

// ============================================================================
// IR Builder (从 AST 构建 IR)
// ============================================================================

class IRBuilder {
public:
    std::shared_ptr<Module> module;
    std::shared_ptr<Function> current_function;
    std::shared_ptr<BasicBlock> current_block;
    
    // 名称生成器
    int64_t name_counter = 0;
    
    // 类型缓存
    std::unordered_map<std::string, std::shared_ptr<Type>> type_cache;
    
    IRBuilder();
    
    // 类型操作
    std::shared_ptr<Type> get_primitive_type(PrimitiveTypeKind kind);
    std::shared_ptr<Type> get_pointer_type(std::shared_ptr<Type> pointee);
    std::shared_ptr<Type> get_array_type(std::shared_ptr<Type> elem, int64_t size);
    std::shared_ptr<Type> get_function_type(std::shared_ptr<Type> ret,
                                             std::vector<std::shared_ptr<Type>> params);
    
    // 名称操作
    std::string create_name(const std::string& prefix);
    std::string create_block_name(const std::string& prefix);
    
    // 函数/块操作
    std::shared_ptr<Function> create_function(std::string name, 
                                               std::shared_ptr<Type> ret_type);
    std::shared_ptr<BasicBlock> create_block(std::string name);
    void set_insert_point(std::shared_ptr<BasicBlock> bb);
    
    // 指令创建
    std::shared_ptr<Value> create_alloca(std::shared_ptr<Type> elem_type,
                                          int64_t count = 1,
                                          std::string name = "");
    
    std::shared_ptr<Value> create_load(std::shared_ptr<Value> addr,
                                        std::string name = "");
    
    void create_store(std::shared_ptr<Value> value, std::shared_ptr<Value> addr);
    
    std::shared_ptr<Value> create_binary_op(OpCode op,
                                              std::shared_ptr<Value> lhs,
                                              std::shared_ptr<Value> rhs,
                                              std::string name = "");
    
    std::shared_ptr<Value> create_unary_op(OpCode op,
                                             std::shared_ptr<Value> operand,
                                             std::string name = "");
    
    std::shared_ptr<Value> create_call(std::string callee,
                                         std::vector<std::shared_ptr<Value>> args,
                                         std::string name = "");
    
    std::shared_ptr<Value> create_cmp(OpCode cmp_op,
                                        std::shared_ptr<Value> lhs,
                                        std::shared_ptr<Value> rhs,
                                        std::string name = "");
    
    void create_ret(std::shared_ptr<Value> value);
    void create_ret_void();
    
    void create_br(std::shared_ptr<BasicBlock> target);
    void create_cond_br(std::shared_ptr<Value> cond,
                         std::shared_ptr<BasicBlock> true_block,
                         std::shared_ptr<BasicBlock> false_block);
    
    std::shared_ptr<Value> create_phi(std::string name = "");
    
    // 常量创建
    std::shared_ptr<Value> create_constant(int64_t val);
    std::shared_ptr<Value> create_constant(double val);
    std::shared_ptr<Value> create_constant(bool val);
    std::shared_ptr<Value> create_constant(const std::string& val);
    
    // 类型转换
    std::shared_ptr<Value> create_cast(OpCode cast_op,
                                         std::shared_ptr<Value> value,
                                         std::shared_ptr<Type> target_type,
                                         std::string name = "");
    
    // 张量操作
    std::shared_ptr<Value> create_tensor_create(std::vector<int64_t> shape,
                                                   std::shared_ptr<Type> elem_type);
    std::shared_ptr<Value> create_tensor_load(std::shared_ptr<Value> tensor,
                                                 std::vector<std::shared_ptr<Value>> indices);
    void create_tensor_store(std::shared_ptr<Value> tensor,
                              std::vector<std::shared_ptr<Value>> indices,
                              std::shared_ptr<Value> value);
    
    // 特殊操作
    void create_panic(std::string message);
    void create_print(std::shared_ptr<Value> value);
    
    // 工具函数
    std::shared_ptr<Type> map_claw_type_to_ir(const std::string& claw_type);
};

} // namespace ir
} // namespace claw

#endif // CLAW_IR_H
