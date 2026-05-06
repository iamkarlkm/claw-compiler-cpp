// ClawVM Implementation - Stack-based Virtual Machine
// Phase 8: Bytecode Execution Engine

#include "vm/claw_vm.h"
#include <cctype>
#include <csignal>
#include <random>
#include <fstream>
#include <filesystem>

namespace claw {
namespace vm {

// ============================================================================
// Value Implementation
// ============================================================================

std::string Value::to_string() const {
    switch (tag) {
        case ValueTag::NIL: return "nil";
        case ValueTag::BOOL: return std::get<bool>(data) ? "true" : "false";
        case ValueTag::INT: return std::to_string(std::get<int64_t>(data));
        case ValueTag::FLOAT: {
            double f = std::get<double>(data);
            if (std::isnan(f)) return "nan";
            if (std::isinf(f)) return f > 0 ? "inf" : "-inf";
            std::ostringstream ss;
            ss << std::showpoint << f;
            return ss.str();
        }
        case ValueTag::STRING: return "\"" + std::get<std::string>(data) + "\"";
        case ValueTag::ARRAY: {
            auto arr = std::get<std::shared_ptr<ArrayValue>>(data);
            std::ostringstream ss;
            ss << "[";
            for (size_t i = 0; i < arr->elements.size(); i++) {
                if (i > 0) ss << ", ";
                ss << arr->elements[i].to_string();
            }
            ss << "]";
            return ss.str();
        }
        case ValueTag::TUPLE: {
            auto tup = std::get<std::shared_ptr<TupleValue>>(data);
            std::ostringstream ss;
            ss << "(";
            for (size_t i = 0; i < tup->elements.size(); i++) {
                if (i > 0) ss << ", ";
                ss << tup->elements[i].to_string();
            }
            ss << ")";
            return ss.str();
        }
        case ValueTag::TENSOR: {
            auto ten = std::get<std::shared_ptr<TensorValue>>(data);
            std::ostringstream ss;
            ss << "tensor<" << ten->element_type << "[";
            for (size_t i = 0; i < ten->shape.size(); i++) {
                if (i > 0) ss << "x";
                ss << ten->shape[i];
            }
            ss << "]>";
            return ss.str();
        }
        case ValueTag::FUNCTION: {
            auto fn = std::get<std::shared_ptr<FunctionValue>>(data);
            return "fn " + fn->name;
        }
        case ValueTag::CLOSURE: {
            auto cl = std::get<std::shared_ptr<ClosureValue>>(data);
            return "closure " + cl->function->name;
        }
        case ValueTag::USERDATA: return "userdata";
        default: return "<unknown>";
    }
}

std::string Value::type_name() const {
    switch (tag) {
        case ValueTag::NIL: return "nil";
        case ValueTag::BOOL: return "bool";
        case ValueTag::INT: return "int";
        case ValueTag::FLOAT: return "float";
        case ValueTag::STRING: return "string";
        case ValueTag::ARRAY: return "array";
        case ValueTag::TUPLE: return "tuple";
        case ValueTag::TENSOR: return "tensor";
        case ValueTag::FUNCTION: return "function";
        case ValueTag::CLOSURE: return "closure";
        case ValueTag::USERDATA: return "userdata";
        default: return "unknown";
    }
}

// Convert bytecode::Value to vm::Value
static Value convert_bytecode_value(const bytecode::Value& bv) {
    switch (bv.type) {
        case bytecode::ValueType::NIL: return Value::nil();
        case bytecode::ValueType::BOOL: return Value::bool_v(bv.data.b);
        case bytecode::ValueType::I64: return Value::int_v(bv.data.i64);
        case bytecode::ValueType::F64: return Value::float_v(bv.data.f64);
        case bytecode::ValueType::STRING: return Value::string_v(bv.str);
        default: return Value::nil();
    }
}

bool Value::equals(const Value& other) const {
    if (tag != other.tag) {
        // Allow int/float comparison
        if (is_number() && other.is_number()) {
            return as_float() == other.as_float();
        }
        return false;
    }
    
    switch (tag) {
        case ValueTag::NIL: return true;
        case ValueTag::BOOL: return as_bool() == other.as_bool();
        case ValueTag::INT: return as_int() == other.as_int();
        case ValueTag::FLOAT: return as_float() == other.as_float();
        case ValueTag::STRING: return as_string() == other.as_string();
        case ValueTag::ARRAY: {
            auto a1 = std::get<std::shared_ptr<ArrayValue>>(data);
            auto a2 = std::get<std::shared_ptr<ArrayValue>>(other.data);
            if (a1->elements.size() != a2->elements.size()) return false;
            for (size_t i = 0; i < a1->elements.size(); i++) {
                if (!a1->elements[i].equals(a2->elements[i])) return false;
            }
            return true;
        }
        default: return false;
    }
}

// ============================================================================
// VMRuntime Implementation
// ============================================================================

int32_t VMRuntime::define_global(const std::string& name) {
    auto it = global_map.find(name);
    if (it != global_map.end()) {
        return it->second;
    }
    int32_t idx = static_cast<int32_t>(global_map.size());
    global_map[name] = idx;
    return idx;
}

int32_t VMRuntime::get_global_idx(const std::string& name) {
    auto it = global_map.find(name);
    if (it != global_map.end()) {
        return it->second;
    }
    return -1;
}

void VMRuntime::set_global(int32_t idx, const Value& val) {
    if (idx >= 0 && idx < static_cast<int32_t>(globals.size())) {
        globals[idx] = val;
    }
}

Value VMRuntime::get_global(int32_t idx) const {
    if (idx >= 0 && idx < static_cast<int32_t>(globals.size())) {
        return globals[idx];
    }
    return Value::nil();
}

std::shared_ptr<UpvalueValue> VMRuntime::capture_upvalue(Value* slot) {
    // Check if there's an existing open upvalue at this location
    for (auto& uv : open_upvalues) {
        if (uv->location == slot) {
            return uv;
        }
    }
    
    // Create new upvalue
    auto upvalue = std::make_shared<UpvalueValue>(slot);
    open_upvalues.push_back(upvalue);
    return upvalue;
}

void VMRuntime::close_upvalues(int32_t slot_idx) {
    Value* slot = &stack[slot_idx];
    
    for (auto it = open_upvalues.begin(); it != open_upvalues.end(); ) {
        if ((*it)->location >= slot) {
            (*it)->close();
            it = open_upvalues.erase(it);
        } else {
            ++it;
        }
    }
}

void VMRuntime::setup_builtins() {
    // Print function
    builtins["print"] = [](VMRuntime& rt) {
        std::cout << rt.peek().to_string();
        return Value::nil();
    };

    // Println function
    builtins["println"] = [](VMRuntime& rt) {
        std::cout << rt.peek().to_string() << std::endl;
        return Value::nil();
    };

    // Len function
    builtins["len"] = [](VMRuntime& rt) {
        Value& v = rt.peek();
        if (v.is_array()) {
            auto arr = std::get<std::shared_ptr<ArrayValue>>(v.data);
            return Value::int_v(static_cast<int64_t>(arr->elements.size()));
        }
        if (v.is_string()) {
            return Value::int_v(static_cast<int64_t>(v.as_string().size()));
        }
        return Value::nil();
    };

    // Type function
    builtins["type"] = [](VMRuntime& rt) {
        return Value::string_v(rt.peek().type_name());
    };

    // Int function
    builtins["int"] = [](VMRuntime& rt) {
        return Value::int_v(rt.peek().as_int());
    };

    // Float function
    builtins["float"] = [](VMRuntime& rt) {
        return Value::float_v(rt.peek().as_float());
    };

    // String function
    builtins["string"] = [](VMRuntime& rt) {
        return Value::string_v(rt.peek().to_string());
    };

    // Bool function
    builtins["bool"] = [](VMRuntime& rt) {
        return Value::bool_v(rt.peek().as_bool());
    };

    // Input function
    builtins["input"] = [](VMRuntime& rt) {
        std::string line;
        std::getline(std::cin, line);
        return Value::string_v(line);
    };

    // Array function
    builtins["array"] = [](VMRuntime& rt) {
        auto arr = std::make_shared<ArrayValue>();
        return Value{ValueTag::ARRAY, arr};
    };

    // Range function (generator)
    builtins["range"] = [](VMRuntime& rt) {
        int64_t end = rt.peek().as_int();
        int64_t start = 0;
        auto arr = std::make_shared<ArrayValue>();
        for (int64_t i = start; i < end; i++) {
            arr->elements.push_back(Value::int_v(i));
        }
        return Value{ValueTag::ARRAY, arr};
    };

    // Panic function
    builtins["panic"] = [](VMRuntime& rt) -> Value {
        throw std::runtime_error(rt.peek().to_string());
        return Value::nil();
    };
}

// ============================================================================
// Garbage Collector Implementation
// ============================================================================

void GarbageCollector::mark_value(Value& val) {
    switch (val.tag) {
        case ValueTag::ARRAY:
            mark_array(std::get<std::shared_ptr<ArrayValue>>(val.data).get());
            break;
        case ValueTag::TUPLE:
            mark_tuple(std::get<std::shared_ptr<TupleValue>>(val.data).get());
            break;
        case ValueTag::TENSOR:
            mark_tensor(std::get<std::shared_ptr<TensorValue>>(val.data).get());
            break;
        case ValueTag::FUNCTION:
            mark_function(std::get<std::shared_ptr<FunctionValue>>(val.data).get());
            break;
        case ValueTag::CLOSURE:
            mark_closure(std::get<std::shared_ptr<ClosureValue>>(val.data).get());
            break;
        case ValueTag::USERDATA:
            mark_userdata(std::get<std::shared_ptr<UserDataValue>>(val.data).get());
            break;
        default:
            break;
    }
}

void GarbageCollector::mark_array(ArrayValue* arr) {
    if (!arr || arr->marked) return;
    arr->marked = true;
    for (auto& val : arr->elements) {
        mark_value(val);
    }
}

void GarbageCollector::mark_tuple(TupleValue* tup) {
    if (!tup || tup->marked) return;
    tup->marked = true;
    for (auto& val : tup->elements) {
        mark_value(val);
    }
}

void GarbageCollector::mark_tensor(TensorValue* ten) {
    if (!ten || ten->marked) return;
    ten->marked = true;
    // Tensors don't contain heap-allocated values
}

void GarbageCollector::mark_function(FunctionValue* fn) {
    if (!fn || fn->marked) return;
    fn->marked = true;
    // Mark constants used by the function
    for (auto& c : fn->constants) {
        mark_value(const_cast<Value&>(c));
    }
}

void GarbageCollector::mark_closure(ClosureValue* cl) {
    if (!cl || cl->marked) return;
    cl->marked = true;
    mark_function(cl->function.get());
    for (auto& uv : cl->upvalues) {
        // Mark the upvalue's location if it's closed
        if (!uv->is_open) {
            mark_value(uv->closed);
        }
    }
}

void GarbageCollector::mark_userdata(UserDataValue* ud) {
    if (!ud || ud->marked) return;
    ud->marked = true;
}

void GarbageCollector::collect(VMRuntime& runtime) {
    // Mark phase
    // Mark all values in call frames
    for (auto& frame : runtime.call_frames) {
        if (frame.closure) {
            mark_closure(frame.closure);
        }
    }
    
    // Mark values on stack
    for (int32_t i = 0; i < runtime.stack_top; i++) {
        mark_value(runtime.stack[i]);
    }
    
    // Mark globals
    for (auto& g : runtime.globals) {
        mark_value(g);
    }
    
    // Sweep phase
    sweep(runtime);
}

void GarbageCollector::sweep(VMRuntime& runtime) {
    // For simplicity, just reset marked flags
    // Full implementation would free unmarked objects
    // Note: gc_cycles is now in ClawVM, not VMRuntime
    // This is handled externally if needed
}

// ============================================================================
// ClawVM Implementation
// ============================================================================

bool ClawVM::load_module(const bytecode::Module& module) {
    current_module = module;
    
    // Setup globals from module
    for (size_t i = 0; i < module.global_names.size(); i++) {
        runtime.define_global(module.global_names[i]);
    }
    
    return true;
}

bool ClawVM::load_module_from_file(const std::string& path) {
    bytecode::BytecodeReader reader;
    
    auto module_opt = reader.read_from_file(path);
    if (!module_opt) {
        last_error = "Failed to load bytecode file: " + path + " - " + reader.get_error();
        had_error = true;
        return false;
    }
    
    return load_module(*module_opt);
}

Value ClawVM::execute() {
    running = true;
    instructions_executed = 0;
    ip = 0;
    current_function = nullptr;
    current_function_idx = 0;
    
    try {
        // Find main function in module
        int32_t main_idx = -1;
        for (size_t i = 0; i < current_module.functions.size(); i++) {
            if (current_module.functions[i].name == "main") {
                main_idx = static_cast<int32_t>(i);
                break;
            }
        }
        
        if (main_idx < 0) {
            error("No main function found in bytecode module");
            return Value::nil();
        }
        
        // Setup current function context
        current_function_idx = static_cast<uint32_t>(main_idx);
        current_function = &current_module.functions[current_function_idx];
        ip = 0;

        // Create a closure for main so op_ret() can restore current_function
        auto main_fn = std::make_shared<FunctionValue>();
        main_fn->func_id = main_idx;
        main_fn->name = current_module.functions[main_idx].name;
        main_fn->arity = static_cast<int32_t>(current_module.functions[main_idx].arity);
        main_fn->local_count = static_cast<int32_t>(current_module.functions[main_idx].local_count);
        main_fn->max_stack = static_cast<int32_t>(current_module.functions[main_idx].local_count + current_module.functions[main_idx].arity + 16);
        auto main_closure = std::make_shared<ClosureValue>();
        main_closure->function = main_fn;

        // Push initial frame for main
        CallFrame frame;
        frame.closure = main_closure.get();
        frame.ip = 0;
        frame.base_stack = 0;
        frame.slot_count = 256;
        frame.local_count = main_fn->local_count;
        runtime.call_frames.push_back(frame);
        runtime.frame_count++;

        // Reserve stack space for locals so expression stack doesn't overlap
        if (current_function) {
            runtime.stack_top = frame.base_stack + current_function->local_count;
        }

        // Run dispatch loop
        while (running && dispatch()) {
            instructions_executed++;
            
            // GC trigger
            if (runtime.gc_enabled && runtime.bytes_allocated > runtime.gc_threshold) {
                GarbageCollector::collect(runtime);
            }
        }
        
    } catch (const std::exception& e) {
        error(e.what());
        had_error = true;
    }
    
    if (runtime.stack_top > 0) {
        return runtime.pop();
    }
    return Value::nil();
}

bool ClawVM::step() {
    if (!running) return false;
    
    try {
        return dispatch();
    } catch (const std::exception& e) {
        error(e.what());
        had_error = true;
        return false;
    }
}

void ClawVM::reset() {
    runtime.stack_top = 0;
    runtime.call_frames.clear();
    runtime.frame_count = 0;
    runtime.open_upvalues.clear();
    ip = 0;
    running = false;
    had_error = false;
    last_error.clear();
}

std::string ClawVM::dump_stack() const {
    std::ostringstream ss;
    ss << "Stack [" << runtime.stack_top << "]: ";
    for (int32_t i = 0; i < runtime.stack_top; i++) {
        if (i > 0) ss << ", ";
        ss << runtime.stack[i].to_string();
    }
    return ss.str();
}

std::string ClawVM::dump_callframes() const {
    std::ostringstream ss;
    ss << "Call Frames [" << runtime.frame_count << "]:\n";
    for (int32_t i = 0; i < runtime.frame_count; i++) {
        auto& frame = runtime.call_frames[i];
        ss << "  [" << i << "] " << frame.closure->function->name 
           << " ip=" << frame.ip << " base=" << frame.base_stack << "\n";
    }
    return ss.str();
}

// Helper methods
Value& ClawVM::current_closure() {
    return runtime.stack[runtime.call_frames[runtime.frame_count - 1].base_stack - 1];
}

int32_t ClawVM::read_byte() {
    if (!current_function || ip >= static_cast<int32_t>(current_function->code.size())) {
        error("Unexpected end of bytecode");
        return 0;
    }
    return static_cast<int32_t>(current_function->code[ip++].op);
}

int32_t ClawVM::read_short() {
    int32_t b1 = read_byte();
    int32_t b2 = read_byte();
    return (b1 << 8) | b2;
}

int32_t ClawVM::read_int() {
    int32_t b1 = read_byte();
    int32_t b2 = read_byte();
    int32_t b3 = read_byte();
    int32_t b4 = read_byte();
    return (b1 << 24) | (b2 << 16) | (b3 << 8) | b4;
}

double ClawVM::read_double() {
    // Read from constants pool
    int32_t idx = read_int();
    if (idx >= 0 && idx < static_cast<int32_t>(current_module.constants.floats.size())) {
        return current_module.constants.get_double(static_cast<uint32_t>(idx));
    }
    return 0.0;
}

std::string ClawVM::read_string() {
    int32_t idx = read_int();
    if (idx >= 0 && idx < static_cast<int32_t>(current_module.constants.strings.size())) {
        return current_module.constants.get_string(static_cast<uint32_t>(idx));
    }
    return "";
}

void ClawVM::error(const std::string& msg) {
    last_error = msg;
    had_error = true;
    running = false;
    
    std::cerr << "VM Error: " << msg << std::endl;
    std::cerr << dump_callframes() << std::endl;
    std::cerr << dump_stack() << std::endl;
}

// ============================================================================
// Instruction Dispatch
// ============================================================================

bool ClawVM::dispatch() {
    if (!running || !current_function || ip >= static_cast<int32_t>(current_function->code.size())) {
        running = false;
        return false;
    }

    int32_t op = read_byte();
    
    // Use if-else chain instead of switch for OpCode enum class
    // Stack operations
    if (op == static_cast<int32_t>(bytecode::OpCode::NOP)) return op_nop();
    if (op == static_cast<int32_t>(bytecode::OpCode::PUSH)) return op_push();
    if (op == static_cast<int32_t>(bytecode::OpCode::POP)) return op_pop();
    if (op == static_cast<int32_t>(bytecode::OpCode::DUP)) return op_dup();
    if (op == static_cast<int32_t>(bytecode::OpCode::SWAP)) return op_swap();
    
    // Integer ops
    if (op == static_cast<int32_t>(bytecode::OpCode::IADD)) return op_iadd();
    if (op == static_cast<int32_t>(bytecode::OpCode::ISUB)) return op_isub();
    if (op == static_cast<int32_t>(bytecode::OpCode::IMUL)) return op_imul();
    if (op == static_cast<int32_t>(bytecode::OpCode::IDIV)) return op_idiv();
    if (op == static_cast<int32_t>(bytecode::OpCode::IMOD)) return op_imod();
    if (op == static_cast<int32_t>(bytecode::OpCode::INEG)) return op_ineg();
    if (op == static_cast<int32_t>(bytecode::OpCode::IINC)) return op_iinc();
    
    // Float ops
    if (op == static_cast<int32_t>(bytecode::OpCode::FADD)) return op_fadd();
    if (op == static_cast<int32_t>(bytecode::OpCode::FSUB)) return op_fsub();
    if (op == static_cast<int32_t>(bytecode::OpCode::FMUL)) return op_fmul();
    if (op == static_cast<int32_t>(bytecode::OpCode::FDIV)) return op_fdiv();
    if (op == static_cast<int32_t>(bytecode::OpCode::FMOD)) return op_fmod();
    if (op == static_cast<int32_t>(bytecode::OpCode::FNEG)) return op_fneg();
    if (op == static_cast<int32_t>(bytecode::OpCode::FINC)) return op_finc();
    
    // Comparison ops
    if (op == static_cast<int32_t>(bytecode::OpCode::IEQ)) return op_ieq();
    if (op == static_cast<int32_t>(bytecode::OpCode::INE)) return op_ine();
    if (op == static_cast<int32_t>(bytecode::OpCode::ILT)) return op_ilt();
    if (op == static_cast<int32_t>(bytecode::OpCode::ILE)) return op_ile();
    if (op == static_cast<int32_t>(bytecode::OpCode::IGT)) return op_igt();
    if (op == static_cast<int32_t>(bytecode::OpCode::IGE)) return op_ige();
    
    if (op == static_cast<int32_t>(bytecode::OpCode::FEQ)) return op_feq();
    if (op == static_cast<int32_t>(bytecode::OpCode::FNE)) return op_fne();
    if (op == static_cast<int32_t>(bytecode::OpCode::FLT)) return op_flt();
    if (op == static_cast<int32_t>(bytecode::OpCode::FLE)) return op_fle();
    if (op == static_cast<int32_t>(bytecode::OpCode::FGT)) return op_fgt();
    if (op == static_cast<int32_t>(bytecode::OpCode::FGE)) return op_fge();
    
    // Logical/bit ops
    if (op == static_cast<int32_t>(bytecode::OpCode::AND)) return op_and();
    if (op == static_cast<int32_t>(bytecode::OpCode::OR)) return op_or();
    if (op == static_cast<int32_t>(bytecode::OpCode::NOT)) return op_not();
    if (op == static_cast<int32_t>(bytecode::OpCode::BAND)) return op_band();
    if (op == static_cast<int32_t>(bytecode::OpCode::BOR)) return op_bor();
    if (op == static_cast<int32_t>(bytecode::OpCode::BXOR)) return op_bxor();
    if (op == static_cast<int32_t>(bytecode::OpCode::BNOT)) return op_bnot();
    if (op == static_cast<int32_t>(bytecode::OpCode::SHL)) return op_shl();
    if (op == static_cast<int32_t>(bytecode::OpCode::SHR)) return op_shr();
    if (op == static_cast<int32_t>(bytecode::OpCode::USHR)) return op_ushr();
    
    // Type conversions
    if (op == static_cast<int32_t>(bytecode::OpCode::I2F)) return op_i2f();
    if (op == static_cast<int32_t>(bytecode::OpCode::F2I)) return op_f2i();
    if (op == static_cast<int32_t>(bytecode::OpCode::I2B)) return op_i2b();
    if (op == static_cast<int32_t>(bytecode::OpCode::B2I)) return op_b2i();
    if (op == static_cast<int32_t>(bytecode::OpCode::I2S)) return op_i2s();
    if (op == static_cast<int32_t>(bytecode::OpCode::F2S)) return op_f2s();
    if (op == static_cast<int32_t>(bytecode::OpCode::S2I)) return op_s2i();
    if (op == static_cast<int32_t>(bytecode::OpCode::S2F)) return op_s2f();
    
    // Local variables
    if (op == static_cast<int32_t>(bytecode::OpCode::LOAD_LOCAL)) return op_load_local();
    if (op == static_cast<int32_t>(bytecode::OpCode::STORE_LOCAL)) return op_store_local();
    if (op == static_cast<int32_t>(bytecode::OpCode::LOAD_LOCAL_0)) return op_load_local_0();
    if (op == static_cast<int32_t>(bytecode::OpCode::LOAD_LOCAL_1)) return op_load_local_1();
    
    // Global variables
    if (op == static_cast<int32_t>(bytecode::OpCode::LOAD_GLOBAL)) return op_load_global();
    if (op == static_cast<int32_t>(bytecode::OpCode::STORE_GLOBAL)) return op_store_global();
    if (op == static_cast<int32_t>(bytecode::OpCode::DEFINE_GLOBAL)) return op_define_global();
    
    // Control flow
    if (op == static_cast<int32_t>(bytecode::OpCode::JMP)) return op_jmp();
    if (op == static_cast<int32_t>(bytecode::OpCode::JMP_IF)) return op_jmp_if();
    if (op == static_cast<int32_t>(bytecode::OpCode::JMP_IF_NOT)) return op_jmp_if_not();
    if (op == static_cast<int32_t>(bytecode::OpCode::LOOP)) return op_loop();
    if (op == static_cast<int32_t>(bytecode::OpCode::CALL)) return op_call();
    if (op == static_cast<int32_t>(bytecode::OpCode::RET)) return op_ret();
    if (op == static_cast<int32_t>(bytecode::OpCode::RET_NULL)) return op_ret_null();
    if (op == static_cast<int32_t>(bytecode::OpCode::CALL_EXT)) return op_call_ext();
    
    // Functions
    if (op == static_cast<int32_t>(bytecode::OpCode::DEFINE_FUNC)) return op_define_func();
    if (op == static_cast<int32_t>(bytecode::OpCode::CLOSURE)) return op_closure();
    if (op == static_cast<int32_t>(bytecode::OpCode::CLOSE_UPVALUE)) return op_close_upvalue();
    if (op == static_cast<int32_t>(bytecode::OpCode::GET_UPVALUE)) return op_get_upvalue();
    if (op == static_cast<int32_t>(bytecode::OpCode::SET_UPVALUE)) return op_set_upvalue();
    
    // Arrays
    if (op == static_cast<int32_t>(bytecode::OpCode::ALLOC_ARRAY)) return op_alloc_array();
    if (op == static_cast<int32_t>(bytecode::OpCode::LOAD_INDEX)) return op_load_index();
    if (op == static_cast<int32_t>(bytecode::OpCode::STORE_INDEX)) return op_store_index();
    if (op == static_cast<int32_t>(bytecode::OpCode::ARRAY_LEN)) return op_array_len();
    if (op == static_cast<int32_t>(bytecode::OpCode::ARRAY_PUSH)) return op_array_push();
    
    // Objects
    if (op == static_cast<int32_t>(bytecode::OpCode::ALLOC_OBJ)) return op_alloc_obj();
    if (op == static_cast<int32_t>(bytecode::OpCode::LOAD_FIELD)) return op_load_field();
    if (op == static_cast<int32_t>(bytecode::OpCode::STORE_FIELD)) return op_store_field();
    if (op == static_cast<int32_t>(bytecode::OpCode::OBJ_TYPE)) return op_obj_type();
    
    // Tuples
    if (op == static_cast<int32_t>(bytecode::OpCode::CREATE_TUPLE)) return op_create_tuple();
    if (op == static_cast<int32_t>(bytecode::OpCode::LOAD_ELEM)) return op_load_elem();
    if (op == static_cast<int32_t>(bytecode::OpCode::STORE_ELEM)) return op_store_elem();
    
    // Tensors
    if (op == static_cast<int32_t>(bytecode::OpCode::TENSOR_CREATE)) return op_tensor_create();
    if (op == static_cast<int32_t>(bytecode::OpCode::TENSOR_LOAD)) return op_tensor_load();
    if (op == static_cast<int32_t>(bytecode::OpCode::TENSOR_STORE)) return op_tensor_store();
    if (op == static_cast<int32_t>(bytecode::OpCode::TENSOR_MATMUL)) return op_tensor_matmul();
    if (op == static_cast<int32_t>(bytecode::OpCode::TENSOR_RESHAPE)) return op_tensor_reshape();
    
    // System
    if (op == static_cast<int32_t>(bytecode::OpCode::PRINT)) return op_print();
    if (op == static_cast<int32_t>(bytecode::OpCode::PRINTLN)) return op_println();
    if (op == static_cast<int32_t>(bytecode::OpCode::PANIC)) return op_panic();
    if (op == static_cast<int32_t>(bytecode::OpCode::HALT)) { running = false; return false; }
    if (op == static_cast<int32_t>(bytecode::OpCode::INPUT)) return op_input();
    if (op == static_cast<int32_t>(bytecode::OpCode::TYPE_OF)) return op_type_of();
    if (op == static_cast<int32_t>(bytecode::OpCode::EXT)) return op_ext();
    
    error("Unknown opcode: " + std::to_string(op));
    return false;
}

bool ClawVM::op_halt() {
    running = false;
    return true;
}

// ============================================================================
// Stack Operations
// ============================================================================

bool ClawVM::op_nop() { return true; }

bool ClawVM::op_push() {
    int32_t idx = static_cast<int32_t>(current_function->code[ip - 1].operand);
    if (idx >= 0 && idx < static_cast<int32_t>(current_module.constants.values.size())) {
        runtime.push(convert_bytecode_value(current_module.constants.values[idx]));
        return true;
    }
    error("Invalid constant index");
    return false;
}

bool ClawVM::op_pop() {
    runtime.pop();
    return true;
}

bool ClawVM::op_dup() {
    Value v = runtime.peek();
    runtime.push(v);
    return true;
}

bool ClawVM::op_swap() {
    Value a = runtime.pop();
    Value b = runtime.pop();
    runtime.push(a);
    runtime.push(b);
    return true;
}

// ============================================================================
// Integer Operations
// ============================================================================

bool ClawVM::op_iadd() {
    int64_t b = runtime.pop().as_int();
    int64_t a = runtime.pop().as_int();
    runtime.push(Value::int_v(a + b));
    return true;
}

bool ClawVM::op_isub() {
    int64_t b = runtime.pop().as_int();
    int64_t a = runtime.pop().as_int();
    runtime.push(Value::int_v(a - b));
    return true;
}

bool ClawVM::op_imul() {
    int64_t b = runtime.pop().as_int();
    int64_t a = runtime.pop().as_int();
    runtime.push(Value::int_v(a * b));
    return true;
}

bool ClawVM::op_idiv() {
    int64_t b = runtime.pop().as_int();
    if (b == 0) { error("Division by zero"); return false; }
    int64_t a = runtime.pop().as_int();
    runtime.push(Value::int_v(a / b));
    return true;
}

bool ClawVM::op_imod() {
    int64_t b = runtime.pop().as_int();
    if (b == 0) { error("Modulo by zero"); return false; }
    int64_t a = runtime.pop().as_int();
    runtime.push(Value::int_v(a % b));
    return true;
}

bool ClawVM::op_ineg() {
    int64_t a = runtime.pop().as_int();
    runtime.push(Value::int_v(-a));
    return true;
}

bool ClawVM::op_iinc() {
    int32_t slot = static_cast<int32_t>(current_function->code[ip - 1].operand);
    Value& v = runtime.slot(runtime.call_frames.back().base_stack + slot);
    v = Value::int_v(v.as_int() + 1);
    return true;
}

// ============================================================================
// Float Operations
// ============================================================================

bool ClawVM::op_fadd() {
    double b = runtime.pop().as_float();
    double a = runtime.pop().as_float();
    runtime.push(Value::float_v(a + b));
    return true;
}

bool ClawVM::op_fsub() {
    double b = runtime.pop().as_float();
    double a = runtime.pop().as_float();
    runtime.push(Value::float_v(a - b));
    return true;
}

bool ClawVM::op_fmul() {
    double b = runtime.pop().as_float();
    double a = runtime.pop().as_float();
    runtime.push(Value::float_v(a * b));
    return true;
}

bool ClawVM::op_fdiv() {
    double b = runtime.pop().as_float();
    if (b == 0.0) { error("Division by zero"); return false; }
    double a = runtime.pop().as_float();
    runtime.push(Value::float_v(a / b));
    return true;
}

bool ClawVM::op_fmod() {
    double b = runtime.pop().as_float();
    if (b == 0.0) { error("Modulo by zero"); return false; }
    double a = runtime.pop().as_float();
    runtime.push(Value::float_v(std::fmod(a, b)));
    return true;
}

bool ClawVM::op_fneg() {
    double a = runtime.pop().as_float();
    runtime.push(Value::float_v(-a));
    return true;
}

bool ClawVM::op_finc() {
    int32_t slot = static_cast<int32_t>(current_function->code[ip - 1].operand);
    Value& v = runtime.slot(runtime.call_frames.back().base_stack + slot);
    v = Value::float_v(v.as_float() + 1.0);
    return true;
}

// ============================================================================
// Comparison Operations
// ============================================================================

bool ClawVM::op_ieq() {
    int64_t b = runtime.pop().as_int();
    int64_t a = runtime.pop().as_int();
    runtime.push(Value::bool_v(a == b));
    return true;
}

bool ClawVM::op_ine() {
    int64_t b = runtime.pop().as_int();
    int64_t a = runtime.pop().as_int();
    runtime.push(Value::bool_v(a != b));
    return true;
}

bool ClawVM::op_ilt() {
    int64_t b = runtime.pop().as_int();
    int64_t a = runtime.pop().as_int();
    runtime.push(Value::bool_v(a < b));
    return true;
}

bool ClawVM::op_ile() {
    int64_t b = runtime.pop().as_int();
    int64_t a = runtime.pop().as_int();
    runtime.push(Value::bool_v(a <= b));
    return true;
}

bool ClawVM::op_igt() {
    int64_t b = runtime.pop().as_int();
    int64_t a = runtime.pop().as_int();
    runtime.push(Value::bool_v(a > b));
    return true;
}

bool ClawVM::op_ige() {
    int64_t b = runtime.pop().as_int();
    int64_t a = runtime.pop().as_int();
    runtime.push(Value::bool_v(a >= b));
    return true;
}

bool ClawVM::op_feq() {
    double b = runtime.pop().as_float();
    double a = runtime.pop().as_float();
    runtime.push(Value::bool_v(a == b));
    return true;
}

bool ClawVM::op_fne() {
    double b = runtime.pop().as_float();
    double a = runtime.pop().as_float();
    runtime.push(Value::bool_v(a != b));
    return true;
}

bool ClawVM::op_flt() {
    double b = runtime.pop().as_float();
    double a = runtime.pop().as_float();
    runtime.push(Value::bool_v(a < b));
    return true;
}

bool ClawVM::op_fle() {
    double b = runtime.pop().as_float();
    double a = runtime.pop().as_float();
    runtime.push(Value::bool_v(a <= b));
    return true;
}

bool ClawVM::op_fgt() {
    double b = runtime.pop().as_float();
    double a = runtime.pop().as_float();
    runtime.push(Value::bool_v(a > b));
    return true;
}

bool ClawVM::op_fge() {
    double b = runtime.pop().as_float();
    double a = runtime.pop().as_float();
    runtime.push(Value::bool_v(a >= b));
    return true;
}

// ============================================================================
// Logical/Bit Operations
// ============================================================================

bool ClawVM::op_and() {
    bool b = runtime.pop().as_bool();
    bool a = runtime.pop().as_bool();
    runtime.push(Value::bool_v(a && b));
    return true;
}

bool ClawVM::op_or() {
    bool b = runtime.pop().as_bool();
    bool a = runtime.pop().as_bool();
    runtime.push(Value::bool_v(a || b));
    return true;
}

bool ClawVM::op_not() {
    bool a = runtime.pop().as_bool();
    runtime.push(Value::bool_v(!a));
    return true;
}

bool ClawVM::op_band() {
    int64_t b = runtime.pop().as_int();
    int64_t a = runtime.pop().as_int();
    runtime.push(Value::int_v(a & b));
    return true;
}

bool ClawVM::op_bor() {
    int64_t b = runtime.pop().as_int();
    int64_t a = runtime.pop().as_int();
    runtime.push(Value::int_v(a | b));
    return true;
}

bool ClawVM::op_bxor() {
    int64_t b = runtime.pop().as_int();
    int64_t a = runtime.pop().as_int();
    runtime.push(Value::int_v(a ^ b));
    return true;
}

bool ClawVM::op_bnot() {
    int64_t a = runtime.pop().as_int();
    runtime.push(Value::int_v(~a));
    return true;
}

bool ClawVM::op_shl() {
    int64_t b = runtime.pop().as_int();
    int64_t a = runtime.pop().as_int();
    runtime.push(Value::int_v(a << b));
    return true;
}

bool ClawVM::op_shr() {
    int64_t b = runtime.pop().as_int();
    int64_t a = runtime.pop().as_int();
    runtime.push(Value::int_v(a >> b));
    return true;
}

bool ClawVM::op_ushr() {
    int64_t b = runtime.pop().as_int();
    uint64_t a = static_cast<uint64_t>(runtime.pop().as_int());
    runtime.push(Value::int_v(static_cast<int64_t>(a >> b)));
    return true;
}

// ============================================================================
// Type Conversions
// ============================================================================

bool ClawVM::op_i2f() {
    int64_t a = runtime.pop().as_int();
    runtime.push(Value::float_v(static_cast<double>(a)));
    return true;
}

bool ClawVM::op_f2i() {
    double a = runtime.pop().as_float();
    runtime.push(Value::int_v(static_cast<int64_t>(a)));
    return true;
}

bool ClawVM::op_i2b() {
    int64_t a = runtime.pop().as_int();
    runtime.push(Value::bool_v(a != 0));
    return true;
}

bool ClawVM::op_b2i() {
    bool a = runtime.pop().as_bool();
    runtime.push(Value::int_v(a ? 1 : 0));
    return true;
}

bool ClawVM::op_i2s() {
    int64_t a = runtime.pop().as_int();
    runtime.push(Value::string_v(std::to_string(a)));
    return true;
}

bool ClawVM::op_f2s() {
    double a = runtime.pop().as_float();
    runtime.push(Value::string_v(std::to_string(a)));
    return true;
}

bool ClawVM::op_s2i() {
    const std::string& s = runtime.pop().as_string();
    runtime.push(Value::int_v(std::stoll(s)));
    return true;
}

bool ClawVM::op_s2f() {
    const std::string& s = runtime.pop().as_string();
    runtime.push(Value::float_v(std::stod(s)));
    return true;
}

// ============================================================================
// Local Variable Operations
// ============================================================================

bool ClawVM::op_load_local() {
    int32_t slot = static_cast<int32_t>(current_function->code[ip - 1].operand);
    int32_t base = runtime.call_frames.back().base_stack;
    Value v = runtime.stack[base + slot];
    runtime.push(v);
    return true;
}

bool ClawVM::op_store_local() {
    int32_t slot = static_cast<int32_t>(current_function->code[ip - 1].operand);
    int32_t base = runtime.call_frames.back().base_stack;
    Value v = runtime.pop();
    runtime.stack[base + slot] = v;
    return true;
}

bool ClawVM::op_load_local_0() {
    int32_t base = runtime.call_frames.back().base_stack;
    runtime.push(runtime.stack[base]);
    return true;
}

bool ClawVM::op_load_local_1() {
    int32_t base = runtime.call_frames.back().base_stack;
    runtime.push(runtime.stack[base + 1]);
    return true;
}

// ============================================================================
// Global Variable Operations
// ============================================================================

bool ClawVM::op_load_global() {
    int32_t idx = static_cast<int32_t>(current_function->code[ip - 1].operand);
    std::string name;
    if (idx >= 0 && idx < static_cast<int32_t>(current_module.constants.values.size())) {
        name = current_module.constants.values[idx].str;
    }

    int32_t gidx = runtime.get_global_idx(name);
    if (gidx >= 0) {
        runtime.push(runtime.get_global(gidx));
        return true;
    }

    // Auto-resolve function names from module.functions
    for (size_t i = 0; i < current_module.functions.size(); i++) {
        if (current_module.functions[i].name == name) {
            auto fn = std::make_shared<FunctionValue>();
            fn->func_id = static_cast<int32_t>(i);
            fn->name = name;
            fn->arity = static_cast<int32_t>(current_module.functions[i].arity);
            fn->local_count = static_cast<int32_t>(current_module.functions[i].local_count);
            fn->max_stack = static_cast<int32_t>(current_module.functions[i].local_count + current_module.functions[i].arity + 16);

            auto closure = std::make_shared<ClosureValue>();
            closure->function = fn;
            runtime.push(Value{ValueTag::CLOSURE, closure});
            return true;
        }
    }

    runtime.push(Value::nil());
    return true;
}

bool ClawVM::op_store_global() {
    int32_t str_idx = static_cast<int32_t>(current_function->code[ip - 1].operand);
    std::string name;
    if (str_idx >= 0 && str_idx < static_cast<int32_t>(current_module.constants.values.size())) {
        name = current_module.constants.values[str_idx].str;
    }
    int32_t gidx = runtime.define_global(name);
    Value v = runtime.pop();
    runtime.set_global(gidx, v);
    return true;
}

bool ClawVM::op_define_global() {
    int32_t str_idx = static_cast<int32_t>(current_function->code[ip - 1].operand);
    std::string name;
    if (str_idx >= 0 && str_idx < static_cast<int32_t>(current_module.constants.values.size())) {
        name = current_module.constants.values[str_idx].str;
    }
    int32_t idx = runtime.define_global(name);
    Value v = runtime.pop();
    runtime.set_global(idx, v);
    return true;
}

// ============================================================================
// Control Flow Operations
// ============================================================================

bool ClawVM::op_jmp() {
    int32_t offset = static_cast<int32_t>(current_function->code[ip - 1].operand);
    ip += offset;
    return true;
}

bool ClawVM::op_jmp_if() {
    int32_t offset = static_cast<int32_t>(current_function->code[ip - 1].operand);
    if (runtime.pop().as_bool()) {
        ip += offset;
    }
    return true;
}

bool ClawVM::op_jmp_if_not() {
    int32_t offset = static_cast<int32_t>(current_function->code[ip - 1].operand);
    if (!runtime.pop().as_bool()) {
        ip += offset;
    }
    return true;
}

bool ClawVM::op_loop() {
    int32_t offset = static_cast<int32_t>(current_function->code[ip - 1].operand);
    ip -= offset;
    return true;
}

bool ClawVM::op_call() {
    int32_t arg_count = static_cast<int32_t>(current_function->code[ip - 1].operand);

    // Get the closure (on top of stack, args are below it)
    Value callee = runtime.pop();

    if (!callee.is_closure()) {
        error("Can only call functions");
        return false;
    }

    auto closure = std::get<std::shared_ptr<ClosureValue>>(callee.data);
    auto& func = closure->function;

    // Save current IP in current frame
    if (!runtime.call_frames.empty()) {
        runtime.call_frames.back().ip = ip;
    }

    // Create new call frame
    if (static_cast<size_t>(runtime.frame_count) >= MAX_CALL_FRAMES) {
        error("Call stack overflow");
        return false;
    }

    CallFrame frame;
    frame.closure = closure.get();
    frame.ip = 0;
    frame.base_stack = runtime.stack_top - arg_count;
    frame.slot_count = func->max_stack > 0 ? func->max_stack : 256;
    frame.local_count = func->local_count;

    runtime.call_frames.push_back(frame);
    runtime.frame_count++;

    // Reserve stack space for locals so expression stack doesn't overlap
    runtime.stack_top = frame.base_stack + func->local_count;

    // Update current function
    if (func->func_id >= 0 && func->func_id < static_cast<int32_t>(current_module.functions.size())) {
        current_function_idx = func->func_id;
        current_function = &current_module.functions[current_function_idx];
    } else {
        error("Invalid function id in closure");
        return false;
    }

    ip = 0;

    return true;
}

bool ClawVM::op_ret() {
    Value result = runtime.pop();

    // Pop call frame
    if (runtime.frame_count > 0) {
        runtime.call_frames.pop_back();
        runtime.frame_count--;
    }

    // Remove local slots, but keep caller's locals reserved
    int32_t caller_base = runtime.call_frames.empty() ? 0 : runtime.call_frames.back().base_stack;
    int32_t caller_locals = runtime.call_frames.empty() ? 0 : runtime.call_frames.back().local_count;
    runtime.stack_top = caller_base + caller_locals;

    // Push result
    runtime.push(result);

    if (runtime.frame_count == 0) {
        running = false;
    } else {
        // Restore IP and current function from caller frame
        ip = runtime.call_frames.back().ip;
        auto caller_closure = runtime.call_frames.back().closure;
        if (caller_closure && caller_closure->function) {
            current_function_idx = caller_closure->function->func_id;
            if (current_function_idx >= 0 && current_function_idx < static_cast<int32_t>(current_module.functions.size())) {
                current_function = &current_module.functions[current_function_idx];
            }
        }
    }

    return true;
}

bool ClawVM::op_ret_null() {
    if (runtime.frame_count > 0) {
        runtime.call_frames.pop_back();
        runtime.frame_count--;
    }

    int32_t caller_base = runtime.call_frames.empty() ? 0 : runtime.call_frames.back().base_stack;
    int32_t caller_locals = runtime.call_frames.empty() ? 0 : runtime.call_frames.back().local_count;
    runtime.stack_top = caller_base + caller_locals;
    runtime.push(Value::nil());

    if (runtime.frame_count == 0) {
        running = false;
    } else {
        ip = runtime.call_frames.back().ip;
        auto caller_closure = runtime.call_frames.back().closure;
        if (caller_closure && caller_closure->function) {
            current_function_idx = caller_closure->function->func_id;
            if (current_function_idx >= 0 && current_function_idx < static_cast<int32_t>(current_module.functions.size())) {
                current_function = &current_module.functions[current_function_idx];
            }
        }
    }

    return true;
}

bool ClawVM::op_call_ext() {
    int32_t packed = static_cast<int32_t>(current_function->code[ip - 1].operand);
    int32_t str_idx = packed & 0xFFFF;
    int32_t arg_count = (packed >> 16) & 0xFFFF;

    std::string name;
    if (str_idx >= 0 && str_idx < static_cast<int32_t>(current_module.constants.values.size())) {
        name = current_module.constants.values[str_idx].str;
    }

    auto it = runtime.builtins.find(name);
    if (it == runtime.builtins.end()) {
        error("Unknown builtin: " + name);
        return false;
    }

    Value result = it->second(runtime);

    // Pop arguments, then push result
    if (arg_count > 0) {
        for (int i = 0; i < arg_count; i++) {
            runtime.pop();
        }
    }
    runtime.push(result);
    return true;
}

// ============================================================================
// Function Operations
// ============================================================================

bool ClawVM::op_define_func() {
    int32_t arity = read_byte();
    int32_t upvalue_count = read_byte();
    int32_t local_count = read_byte();
    int32_t max_stack = read_byte();
    
    auto fn = std::make_shared<FunctionValue>();
    fn->arity = arity;
    fn->upvalue_count = upvalue_count;
    fn->local_count = local_count;
    fn->max_stack = max_stack;
    
    runtime.push(Value{ValueTag::FUNCTION, fn});
    return true;
}

bool ClawVM::op_closure() {
    int32_t func_idx = static_cast<int32_t>(current_function->code[ip - 1].operand);

    // Get function from module (by index into functions vector)
    if (func_idx < 0 || func_idx >= static_cast<int32_t>(current_module.functions.size())) {
        error("Invalid function index");
        return false;
    }

    auto& func = current_module.functions[func_idx];
    std::shared_ptr<FunctionValue> fn = std::make_shared<FunctionValue>();
    fn->func_id = func_idx;
    fn->name = func.name;
    fn->arity = static_cast<int32_t>(func.arity);
    fn->local_count = static_cast<int32_t>(func.local_count);
    fn->max_stack = static_cast<int32_t>(func.local_count + func.arity + 16);

    // Create closure with upvalues
    auto closure = std::make_shared<ClosureValue>();
    closure->function = fn;

    runtime.push(Value{ValueTag::CLOSURE, closure});
    return true;
}

bool ClawVM::op_close_upvalue() {
    Value v = runtime.pop();
    int32_t slot = runtime.stack_top - 1;
    runtime.close_upvalues(slot);
    runtime.push(v);
    return true;
}

bool ClawVM::op_get_upvalue() {
    int32_t index = static_cast<int32_t>(current_function->code[ip - 1].operand);
    auto& closure = *runtime.call_frames.back().closure;
    Value& uv = closure.upvalues[index]->get();
    runtime.push(uv);
    return true;
}

bool ClawVM::op_set_upvalue() {
    int32_t index = static_cast<int32_t>(current_function->code[ip - 1].operand);
    Value v = runtime.pop();
    auto& closure = *runtime.call_frames.back().closure;
    closure.upvalues[index]->get() = v;
    return true;
}

// ============================================================================
// Array Operations
// ============================================================================

bool ClawVM::op_alloc_array() {
    auto arr = std::make_shared<ArrayValue>();
    runtime.push(Value{ValueTag::ARRAY, arr});
    return true;
}

bool ClawVM::op_load_index() {
    Value idx_val = runtime.pop();
    Value obj = runtime.pop();
    
    int64_t idx = idx_val.as_int();
    
    if (obj.is_array()) {
        auto arr = std::get<std::shared_ptr<ArrayValue>>(obj.data);
        if (idx < 0) idx += arr->elements.size();
        if (idx < 0 || idx >= static_cast<int64_t>(arr->elements.size())) {
            error("Array index out of bounds");
            return false;
        }
        runtime.push(arr->elements[idx]);
    } else if (obj.is_string()) {
        const std::string& s = obj.as_string();
        if (idx < 0) idx += s.size();
        if (idx < 0 || idx >= static_cast<int64_t>(s.size())) {
            error("String index out of bounds");
            return false;
        }
        runtime.push(Value::string_v(std::string(1, s[idx])));
    } else if (obj.is_tuple()) {
        auto tup = std::get<std::shared_ptr<TupleValue>>(obj.data);
        if (idx < 0) idx += tup->elements.size();
        if (idx < 0 || idx >= static_cast<int64_t>(tup->elements.size())) {
            error("Tuple index out of bounds");
            return false;
        }
        runtime.push(tup->elements[idx]);
    } else {
        error("Cannot index this type");
        return false;
    }
    
    return true;
}

bool ClawVM::op_store_index() {
    Value val = runtime.pop();
    Value idx_val = runtime.pop();
    Value obj = runtime.pop();
    
    int64_t idx = idx_val.as_int();
    
    if (obj.is_array()) {
        auto arr = std::get<std::shared_ptr<ArrayValue>>(obj.data);
        if (idx < 0) idx += arr->elements.size();
        if (idx < 0 || idx >= static_cast<int64_t>(arr->elements.size())) {
            error("Array index out of bounds");
            return false;
        }
        arr->elements[idx] = val;
    } else {
        error("Cannot store to this type");
        return false;
    }
    
    runtime.push(val);
    return true;
}

bool ClawVM::op_array_len() {
    Value obj = runtime.pop();
    
    if (obj.is_array()) {
        auto arr = std::get<std::shared_ptr<ArrayValue>>(obj.data);
        runtime.push(Value::int_v(static_cast<int64_t>(arr->elements.size())));
    } else if (obj.is_string()) {
        runtime.push(Value::int_v(static_cast<int64_t>(obj.as_string().size())));
    } else if (obj.is_tuple()) {
        auto tup = std::get<std::shared_ptr<TupleValue>>(obj.data);
        runtime.push(Value::int_v(static_cast<int64_t>(tup->elements.size())));
    } else {
        error("Cannot get length of this type");
        return false;
    }
    
    return true;
}

bool ClawVM::op_array_push() {
    Value val = runtime.pop();
    Value arr_val = runtime.pop();
    
    if (!arr_val.is_array()) {
        error("Can only push to arrays");
        return false;
    }
    
    auto arr = std::get<std::shared_ptr<ArrayValue>>(arr_val.data);
    arr->elements.push_back(val);
    
    runtime.push(arr_val);
    return true;
}

// ============================================================================
// Iterator Operations (NEW - 2026-04-26)
// ============================================================================

bool ClawVM::op_iter_create() {
    // Create iterator from iterable (array)
    // Stack: [array] -> [iterator]
    Value iterable = runtime.pop();
    
    auto iter = std::make_shared<IteratorValue>();
    
    if (iterable.is_array()) {
        auto arr = std::get<std::shared_ptr<ArrayValue>>(iterable.data);
        iter->kind = "array";
        iter->size = static_cast<int64_t>(arr->elements.size());
        iter->index = 0;
    } else if (iterable.is_string()) {
        const std::string& s = iterable.as_string();
        iter->kind = "array";
        iter->size = static_cast<int64_t>(s.size());
        iter->index = 0;
    } else if (iterable.is_iterator()) {
        runtime.push(iterable);
        return true;
    } else {
        error("Cannot create iterator from this type");
        return false;
    }
    
    runtime.push(Value::iterator_v(iter));
    return true;
}

bool ClawVM::op_iter_next() {
    // Get next element from iterator
    // Stack: [iterator] -> [value, done]
    Value iter_val = runtime.pop();
    
    if (!iter_val.is_iterator()) {
        error("Expected iterator");
        return false;
    }
    
    auto iter = std::get<std::shared_ptr<IteratorValue>>(iter_val.data);
    Value result;
    Value done;
    
    if (iter->kind == "array") {
        if (iter->index < iter->size) {
            // Get array element at current index - simplified: return index
            result = Value::int_v(iter->index);
            iter->index++;
            done = Value::bool_v(false);
        } else {
            result = Value::nil();
            done = Value::bool_v(true);
        }
    } else if (iter->kind == "range") {
        if ((iter->step > 0 && iter->index < iter->end) || 
            (iter->step < 0 && iter->index > iter->end)) {
            result = Value::int_v(iter->index);
            iter->index += iter->step;
            done = Value::bool_v(false);
        } else {
            result = Value::nil();
            done = Value::bool_v(true);
        }
    } else if (iter->kind == "enumerate") {
        if (iter->index < iter->size) {
            // Return tuple (index, value)
            result = Value::int_v(iter->outer_index);
            iter->index++;
            iter->outer_index++;
            done = Value::bool_v(false);
        } else {
            result = Value::nil();
            done = Value::bool_v(true);
        }
    } else if (iter->kind == "zip") {
        if (iter->index < iter->size) {
            result = Value::int_v(iter->index);
            iter->index++;
            done = Value::bool_v(false);
        } else {
            result = Value::nil();
            done = Value::bool_v(true);
        }
    } else {
        error("Unknown iterator kind");
        return false;
    }
    
    runtime.push(result);
    runtime.push(done);
    return true;
}

bool ClawVM::op_iter_has_next() {
    // Check if iterator has more elements
    // Stack: [iterator] -> [bool]
    Value iter_val = runtime.pop();
    
    if (!iter_val.is_iterator()) {
        error("Expected iterator");
        return false;
    }
    
    auto iter = std::get<std::shared_ptr<IteratorValue>>(iter_val.data);
    bool has_next = false;
    
    if (iter->kind == "array") {
        has_next = iter->index < iter->size;
    } else if (iter->kind == "range") {
        has_next = (iter->step > 0 && iter->index < iter->end) || 
                   (iter->step < 0 && iter->index > iter->end);
    } else if (iter->kind == "enumerate") {
        has_next = iter->index < iter->size;
    } else if (iter->kind == "zip") {
        has_next = iter->index < iter->size;
    }
    
    runtime.push(Value::bool_v(has_next));
    return true;
}

bool ClawVM::op_iter_reset() {
    // Reset iterator to beginning
    // Stack: [iterator] -> [iterator]
    Value iter_val = runtime.pop();
    
    if (!iter_val.is_iterator()) {
        error("Expected iterator");
        return false;
    }
    
    auto iter = std::get<std::shared_ptr<IteratorValue>>(iter_val.data);
    iter->index = 0;
    
    if (iter->kind == "range") {
        iter->index = iter->start;
    } else if (iter->kind == "enumerate") {
        iter->outer_index = 0;
    }
    
    runtime.push(iter_val);
    return true;
}

bool ClawVM::op_iter_get_index() {
    // Get current index
    // Stack: [iterator] -> [index]
    Value iter_val = runtime.pop();
    
    if (!iter_val.is_iterator()) {
        error("Expected iterator");
        return false;
    }
    
    auto iter = std::get<std::shared_ptr<IteratorValue>>(iter_val.data);
    runtime.push(Value::int_v(iter->index));
    return true;
}

bool ClawVM::op_range_create() {
    // Create range iterator: start, end, step
    // Stack: [start, end, step] -> [iterator]
    int64_t step = 1;
    int64_t end = 0;
    int64_t start = 0;
    
    // Pop in reverse order
    Value step_val = runtime.pop();
    Value end_val = runtime.pop();
    Value start_val = runtime.pop();
    
    if (step_val.is_int()) {
        step = std::get<int64_t>(step_val.data);
    }
    if (end_val.is_int()) {
        end = std::get<int64_t>(end_val.data);
    }
    if (start_val.is_int()) {
        start = std::get<int64_t>(start_val.data);
    }
    
    auto iter = IteratorValue::create_range_iterator(start, end, step);
    runtime.push(Value::iterator_v(iter));
    return true;
}

bool ClawVM::op_enumerate_create() {
    // Create enumerate iterator
    // Stack: [array] -> [iterator]
    Value arr_val = runtime.pop();
    
    if (!arr_val.is_array()) {
        error("enumerate requires an array");
        return false;
    }
    
    auto arr = std::get<std::shared_ptr<ArrayValue>>(arr_val.data);
    auto iter = IteratorValue::create_enumerate_iterator(arr->elements);
    runtime.push(Value::iterator_v(iter));
    return true;
}

bool ClawVM::op_zip_create() {
    // Create zip iterator from multiple arrays
    // Stack: [count, array1, array2, ...] -> [iterator]
    Value count_val = runtime.pop();
    int32_t count = 1;
    
    if (count_val.is_int()) {
        count = static_cast<int32_t>(std::get<int64_t>(count_val.data));
    }
    
    std::vector<std::vector<Value>> arrays;
    arrays.reserve(count);
    
    for (int32_t i = 0; i < count; i++) {
        Value arr_val = runtime.pop();
        if (arr_val.is_array()) {
            auto arr = std::get<std::shared_ptr<ArrayValue>>(arr_val.data);
            arrays.push_back(arr->elements);
        } else {
            error("zip requires arrays");
            return false;
        }
    }
    
    auto iter = IteratorValue::create_zip_iterator(arrays);
    runtime.push(Value::iterator_v(iter));
    return true;
}

// ============================================================================
// Object Operations (simplified)
// ============================================================================

bool ClawVM::op_alloc_obj() {
    auto ud = std::make_shared<UserDataValue>();
    runtime.push(Value{ValueTag::USERDATA, ud});
    return true;
}

bool ClawVM::op_load_field() {
    Value field_val = runtime.pop();
    Value obj = runtime.pop();
    std::string field = field_val.as_string();
    // Simplified: just return nil for now
    runtime.push(Value::nil());
    return true;
}

bool ClawVM::op_store_field() {
    Value field_val = runtime.pop();
    std::string field = field_val.as_string();
    Value val = runtime.pop();
    Value obj = runtime.pop();
    // Simplified: ignore for now
    runtime.push(val);
    return true;
}

bool ClawVM::op_obj_type() {
    Value obj = runtime.pop();
    runtime.push(Value::string_v(obj.type_name()));
    return true;
}

// ============================================================================
// Tuple Operations
// ============================================================================

bool ClawVM::op_create_tuple() {
    int32_t count = static_cast<int32_t>(current_function->code[ip - 1].operand);
    auto tup = std::make_shared<TupleValue>();
    tup->elements.reserve(count);
    for (int32_t i = 0; i < count; i++) {
        tup->elements.push_back(runtime.pop());
    }
    std::reverse(tup->elements.begin(), tup->elements.end());
    runtime.push(Value{ValueTag::TUPLE, tup});
    return true;
}

bool ClawVM::op_load_elem() {
    return op_load_index();
}

bool ClawVM::op_store_elem() {
    return op_store_index();
}

// ============================================================================
// Tensor Operations (simplified)
// ============================================================================

bool ClawVM::op_tensor_create() {
    std::string dtype = read_string();
    int32_t rank = read_byte();
    
    auto tensor = std::make_shared<TensorValue>();
    tensor->element_type = dtype;
    tensor->shape.resize(rank);
    
    // Read shape in reverse order
    for (int32_t i = rank - 1; i >= 0; i--) {
        tensor->shape[i] = runtime.pop().as_int();
    }
    
    // Allocate data
    tensor->data.resize(tensor->total_size(), 0.0);
    tensor->int_data.resize(tensor->total_size(), 0);
    
    runtime.push(Value{ValueTag::TENSOR, tensor});
    return true;
}

bool ClawVM::op_tensor_load() {
    Value idx_val = runtime.pop();
    Value tensor_val = runtime.pop();
    
    if (!tensor_val.is_tensor()) {
        error("Not a tensor");
        return false;
    }
    
    auto tensor = std::get<std::shared_ptr<TensorValue>>(tensor_val.data);
    int64_t idx = idx_val.as_int();
    
    if (tensor->is_integer()) {
        runtime.push(Value::int_v(tensor->int_data[idx]));
    } else {
        runtime.push(Value::float_v(tensor->data[idx]));
    }
    return true;
}

bool ClawVM::op_tensor_store() {
    Value val = runtime.pop();
    Value idx_val = runtime.pop();
    Value tensor_val = runtime.pop();
    
    if (!tensor_val.is_tensor()) {
        error("Not a tensor");
        return false;
    }
    
    auto tensor = std::get<std::shared_ptr<TensorValue>>(tensor_val.data);
    int64_t idx = idx_val.as_int();
    
    if (tensor->is_integer()) {
        tensor->int_data[idx] = val.as_int();
    } else {
        tensor->data[idx] = val.as_float();
    }
    
    runtime.push(tensor_val);
    return true;
}

bool ClawVM::op_tensor_matmul() {
    Value b = runtime.pop();
    Value a = runtime.pop();
    
    if (!a.is_tensor() || !b.is_tensor()) {
        error("Matmul requires tensors");
        return false;
    }
    
    auto ta = std::get<std::shared_ptr<TensorValue>>(a.data);
    auto tb = std::get<std::shared_ptr<TensorValue>>(b.data);
    
    // Simple matrix multiplication
    if (ta->shape.size() != 2 || tb->shape.size() != 2 ||
        ta->shape[1] != tb->shape[0]) {
        error("Incompatible shapes for matmul");
        return false;
    }
    
    auto result = std::make_shared<TensorValue>();
    result->element_type = ta->element_type;
    result->shape = {ta->shape[0], tb->shape[1]};
    result->data.resize(result->total_size(), 0.0);
    result->int_data.resize(result->total_size(), 0);
    
    // Naive matrix multiply
    for (int64_t i = 0; i < ta->shape[0]; i++) {
        for (int64_t j = 0; j < tb->shape[1]; j++) {
            for (int64_t k = 0; k < ta->shape[1]; k++) {
                int64_t a_idx = i * ta->shape[1] + k;
                int64_t b_idx = k * tb->shape[1] + j;
                int64_t r_idx = i * result->shape[1] + j;
                result->data[r_idx] += ta->data[a_idx] * tb->data[b_idx];
            }
        }
    }
    
    runtime.push(Value{ValueTag::TENSOR, result});
    return true;
}

bool ClawVM::op_tensor_reshape() {
    Value tensor_val = runtime.pop();
    int32_t new_rank = read_byte();
    
    if (!tensor_val.is_tensor()) {
        error("Not a tensor");
        return false;
    }
    
    auto tensor = std::get<std::shared_ptr<TensorValue>>(tensor_val.data);
    auto result = std::make_shared<TensorValue>(*tensor);
    
    result->shape.resize(new_rank);
    for (int32_t i = new_rank - 1; i >= 0; i--) {
        result->shape[i] = runtime.pop().as_int();
    }
    
    // Verify size matches
    if (result->total_size() != tensor->total_size()) {
        error("Cannot reshape: size mismatch");
        return false;
    }
    
    runtime.push(Value{ValueTag::TENSOR, result});
    return true;
}

// ============================================================================
// System Operations
// ============================================================================

bool ClawVM::op_print() {
    Value v = runtime.pop();
    std::cout << v.to_string();
    return true;
}

bool ClawVM::op_println() {
    Value v = runtime.pop();
    std::cout << v.to_string() << std::endl;
    return true;
}

bool ClawVM::op_panic() {
    Value v = runtime.pop();
    error("Panic: " + v.to_string());
    return false;
}

bool ClawVM::op_input() {
    std::string line;
    std::getline(std::cin, line);
    runtime.push(Value::string_v(line));
    return true;
}

bool ClawVM::op_type_of() {
    Value v = runtime.pop();
    runtime.push(Value::string_v(v.type_name()));
    return true;
}

bool ClawVM::op_ext() {
    // Extension opcode - for stdlib function calls
    // Format: EXT <opcode>
    int opcode = static_cast<int>(current_function->code[ip - 1].operand);

    auto& stack = runtime.stack;
    
    switch (opcode) {
        // ========== I/O 函数 (0-9) ==========
        case 0: { // print
            if (stack.empty()) return true;
            Value v = stack.back();
            std::cout << v.to_string();
            stack.pop_back();
            return true;
        }
        case 1: { // println
            if (stack.empty()) return true;
            Value v = stack.back();
            std::cout << v.to_string() << "\n";
            stack.pop_back();
            return true;
        }
        case 2: { // input
            std::string line;
            std::getline(std::cin, line);
            runtime.push(Value::string_v(line));
            return true;
        }
        case 3: { // input_str
            if (stack.empty()) return true;
            std::string prompt = stack.back().to_string();
            stack.pop_back();
            std::cout << prompt;
            std::string line;
            std::getline(std::cin, line);
            runtime.push(Value::string_v(line));
            return true;
        }
        
        // ========== 字符串函数 (10-29) ==========
        case 10: { // str_len
            if (stack.empty()) return true;
            Value v = stack.back();
            int64_t len = 0;
            if (v.tag == ValueTag::STRING) {
                len = std::get<std::string>(v.data).size();
            } else if (v.tag == ValueTag::ARRAY) {
                auto arr = std::get<std::shared_ptr<ArrayValue>>(v.data);
                len = arr ? arr->elements.size() : 0;
            }
            stack.back() = Value::int_v(len);
            return true;
        }
        case 15: { // str_upper
            if (stack.empty()) return true;
            Value v = stack.back();
            if (v.tag == ValueTag::STRING) {
                std::string s = std::get<std::string>(v.data);
                for (char& c : s) c = toupper(c);
                stack.back() = Value::string_v(s);
            }
            return true;
        }
        case 16: { // str_lower
            if (stack.empty()) return true;
            Value v = stack.back();
            if (v.tag == ValueTag::STRING) {
                std::string s = std::get<std::string>(v.data);
                for (char& c : s) c = tolower(c);
                stack.back() = Value::string_v(s);
            }
            return true;
        }
        case 17: { // str_trim
            if (stack.empty()) return true;
            Value v = stack.back();
            if (v.tag == ValueTag::STRING) {
                std::string s = std::get<std::string>(v.data);
                size_t start = s.find_first_not_of(" \t\n\r");
                size_t end = s.find_last_not_of(" \t\n\r");
                if (start == std::string::npos) {
                    stack.back() = Value::string_v("");
                } else {
                    stack.back() = Value::string_v(s.substr(start, end - start + 1));
                }
            }
            return true;
        }
        
        // ========== 数学函数 (30-59) ==========
        case 30: { // abs
            if (stack.empty()) return true;
            Value v = stack.back();
            if (v.tag == ValueTag::INT) {
                int64_t x = v.as_int();
                stack.back() = Value::int_v(x < 0 ? -x : x);
            } else if (v.tag == ValueTag::FLOAT) {
                double x = v.as_float();
                stack.back() = Value::float_v(x < 0 ? -x : x);
            }
            return true;
        }
        case 31: { // sin
            if (stack.empty()) return true;
            Value v = stack.back();
            double x = v.as_float();
            stack.back() = Value::float_v(std::sin(x));
            return true;
        }
        case 32: { // cos
            if (stack.empty()) return true;
            Value v = stack.back();
            double x = v.as_float();
            stack.back() = Value::float_v(std::cos(x));
            return true;
        }
        case 33: { // tan
            if (stack.empty()) return true;
            Value v = stack.back();
            double x = v.as_float();
            stack.back() = Value::float_v(std::tan(x));
            return true;
        }
        case 38: { // sqrt
            if (stack.empty()) return true;
            Value v = stack.back();
            double x = v.as_float();
            stack.back() = Value::float_v(std::sqrt(x));
            return true;
        }
        case 39: { // pow
            if (stack.size() < 2) return true;
            double base = stack[stack.size() - 2].as_float();
            double exp = stack[stack.size() - 1].as_float();
            stack.pop_back();
            stack.back() = Value::float_v(std::pow(base, exp));
            return true;
        }
        case 43: { // floor
            if (stack.empty()) return true;
            Value v = stack.back();
            double x = v.as_float();
            stack.back() = Value::float_v(std::floor(x));
            return true;
        }
        case 44: { // ceil
            if (stack.empty()) return true;
            Value v = stack.back();
            double x = v.as_float();
            stack.back() = Value::float_v(std::ceil(x));
            return true;
        }
        case 45: { // round
            if (stack.empty()) return true;
            Value v = stack.back();
            double x = v.as_float();
            stack.back() = Value::float_v(std::round(x));
            return true;
        }
        case 51: { // pi
            runtime.push(Value::float_v(3.14159265358979323846));
            return true;
        }
        case 52: { // e
            runtime.push(Value::float_v(2.71828182845904523536));
            return true;
        }
        case 53: { // random
            static std::random_device rd;
            static std::mt19937 gen(rd());
            static std::uniform_real_distribution<> dis(0.0, 1.0);
            runtime.push(Value::float_v(dis(gen)));
            return true;
        }
        
        // ========== 数组函数 (60-79) ==========
        case 60: { // arr_len
            if (stack.empty()) return true;
            Value v = stack.back();
            size_t len = 0;
            if (v.tag == ValueTag::ARRAY) {
                auto arr = std::get<std::shared_ptr<ArrayValue>>(v.data);
                len = arr ? arr->elements.size() : 0;
            }
            stack.back() = Value::int_v(static_cast<int64_t>(len));
            return true;
        }
        case 61: { // arr_push
            if (stack.size() < 2) return true;
            Value arr_val = stack[stack.size() - 2];
            Value elem = stack.back();
            stack.pop_back();
            if (arr_val.tag == ValueTag::ARRAY) {
                auto arr = std::get<std::shared_ptr<ArrayValue>>(arr_val.data);
                if (arr) {
                    arr->elements.push_back(elem);
                }
            }
            stack.back() = arr_val;
            return true;
        }
        case 62: { // arr_pop
            if (stack.empty()) return true;
            Value v = stack.back();
            if (v.tag == ValueTag::ARRAY) {
                auto arr = std::get<std::shared_ptr<ArrayValue>>(v.data);
                if (arr && !arr->elements.empty()) {
                    stack.back() = arr->elements.back();
                    arr->elements.pop_back();
                }
            }
            return true;
        }
        case 65: { // arr_sort
            if (stack.empty()) return true;
            Value v = stack.back();
            if (v.tag == ValueTag::ARRAY) {
                auto arr = std::get<std::shared_ptr<ArrayValue>>(v.data);
                if (arr) {
                    std::sort(arr->elements.begin(), arr->elements.end(), 
                        [](const Value& a, const Value& b) {
                            return a.as_float() < b.as_float();
                        });
                }
            }
            return true;
        }
        
        // ========== 类型转换函数 (90-99) ==========
        case 90: { // to_int
            if (stack.empty()) return true;
            Value v = stack.back();
            stack.back() = Value::int_v(static_cast<int64_t>(v.as_float()));
            return true;
        }
        case 91: { // to_float
            if (stack.empty()) return true;
            Value v = stack.back();
            stack.back() = Value::float_v(v.as_float());
            return true;
        }
        case 92: { // to_string
            if (stack.empty()) return true;
            Value v = stack.back();
            stack.back() = Value::string_v(v.to_string());
            return true;
        }
        case 93: { // to_bool
            if (stack.empty()) return true;
            Value v = stack.back();
            stack.back() = Value::bool_v(v.as_bool());
            return true;
        }
        case 94: { // type_of
            if (stack.empty()) return true;
            Value v = stack.back();
            std::string type_name;
            switch (v.tag) {
                case ValueTag::NIL: type_name = "nil"; break;
                case ValueTag::BOOL: type_name = "bool"; break;
                case ValueTag::INT: type_name = "int"; break;
                case ValueTag::FLOAT: type_name = "float"; break;
                case ValueTag::STRING: type_name = "string"; break;
                case ValueTag::ARRAY: type_name = "array"; break;
                case ValueTag::TUPLE: type_name = "tuple"; break;
                case ValueTag::TENSOR: type_name = "tensor"; break;
                case ValueTag::FUNCTION: type_name = "function"; break;
                case ValueTag::CLOSURE: type_name = "closure"; break;
                default: type_name = "unknown"; break;
            }
            stack.back() = Value::string_v(type_name);
            return true;
        }
        
        // ========== 更多字符串函数 (10-29) ==========
        case 11: { // str_contains
            if (stack.size() < 2) return true;
            std::string sub = stack.back().as_string();
            stack.pop_back();
            std::string s = stack.back().as_string();
            stack.back() = Value::bool_v(s.find(sub) != std::string::npos);
            return true;
        }
        case 12: { // str_find
            if (stack.size() < 2) return true;
            std::string sub = stack.back().as_string();
            stack.pop_back();
            std::string s = stack.back().as_string();
            size_t pos = s.find(sub);
            stack.back() = Value::int_v(pos == std::string::npos ? -1 : static_cast<int64_t>(pos));
            return true;
        }
        case 13: { // str_replace
            if (stack.size() < 3) return true;
            std::string to = stack.back().as_string();
            stack.pop_back();
            std::string from = stack.back().as_string();
            stack.pop_back();
            std::string s = stack.back().as_string();
            size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos) {
                s.replace(pos, from.length(), to);
                pos += to.length();
            }
            stack.back() = Value::string_v(s);
            return true;
        }
        case 14: { // str_split
            if (stack.size() < 2) return true;
            std::string delim = stack.back().as_string();
            stack.pop_back();
            std::string s = stack.back().as_string();
            std::vector<Value> result;
            size_t start = 0, end = 0;
            while ((end = s.find(delim, start)) != std::string::npos) {
                result.push_back(Value::string_v(s.substr(start, end - start)));
                start = end + delim.length();
            }
            result.push_back(Value::string_v(s.substr(start)));
            auto arr = std::make_shared<ArrayValue>();
            arr->elements = result;
            stack.back() = Value::array_v(arr);
            return true;
        }
        case 18: { // str_substring
            if (stack.size() < 3) return true;
            int64_t len = stack.back().as_int();
            stack.pop_back();
            int64_t start = stack.back().as_int();
            stack.pop_back();
            std::string s = stack.back().as_string();
            if (start < 0) start = 0;
            if (start >= static_cast<int64_t>(s.length())) {
                stack.back() = Value::string_v("");
            } else {
                auto max_len = static_cast<size_t>(s.length() - start);
                auto actual_len = len > 0 ? std::min(static_cast<size_t>(len), max_len) : max_len;
                stack.back() = Value::string_v(s.substr(start, actual_len));
            }
            return true;
        }
        case 19: { // str_starts_with
            if (stack.size() < 2) return true;
            std::string prefix = stack.back().as_string();
            stack.pop_back();
            std::string s = stack.back().as_string();
            stack.back() = Value::bool_v(s.rfind(prefix, 0) == 0);
            return true;
        }
        case 20: { // str_ends_with
            if (stack.size() < 2) return true;
            std::string suffix = stack.back().as_string();
            stack.pop_back();
            std::string s = stack.back().as_string();
            if (suffix.length() > s.length()) {
                stack.back() = Value::bool_v(false);
            } else {
                stack.back() = Value::bool_v(s.compare(s.length() - suffix.length(), suffix.length(), suffix) == 0);
            }
            return true;
        }
        case 21: { // str_reverse
            if (stack.empty()) return true;
            std::string s = stack.back().as_string();
            std::reverse(s.begin(), s.end());
            stack.back() = Value::string_v(s);
            return true;
        }
        case 22: { // str_repeat
            if (stack.size() < 2) return true;
            int64_t n = stack.back().as_int();
            stack.pop_back();
            std::string s = stack.back().as_string();
            if (n <= 0) {
                stack.back() = Value::string_v("");
            } else {
                std::string result;
                result.reserve(s.length() * n);
                for (int64_t i = 0; i < n; ++i) {
                    result += s;
                }
                stack.back() = Value::string_v(result);
            }
            return true;
        }
        case 23: { // str_join
            if (stack.size() < 2) return true;
            std::string delim = stack.back().as_string();
            stack.pop_back();
            Value arr_val = stack.back();
            std::string result;
            if (arr_val.tag == ValueTag::ARRAY) {
                auto arr = std::get<std::shared_ptr<ArrayValue>>(arr_val.data);
                if (arr) {
                    for (size_t i = 0; i < arr->elements.size(); ++i) {
                        if (i > 0) result += delim;
                        result += arr->elements[i].to_string();
                    }
                }
            }
            stack.back() = Value::string_v(result);
            return true;
        }
        case 24: { // format - simplified version
            // Just concat all args for now
            // Would need proper format string parsing for full support
            return true;
        }
        
        // ========== 更多数学函数 (30-59) ==========
        case 34: { // asin
            if (stack.empty()) return true;
            double x = stack.back().as_float();
            stack.back() = Value::float_v(std::asin(x));
            return true;
        }
        case 35: { // acos
            if (stack.empty()) return true;
            double x = stack.back().as_float();
            stack.back() = Value::float_v(std::acos(x));
            return true;
        }
        case 36: { // atan
            if (stack.empty()) return true;
            double x = stack.back().as_float();
            stack.back() = Value::float_v(std::atan(x));
            return true;
        }
        case 37: { // atan2
            if (stack.size() < 2) return true;
            double y = stack.back().as_float();
            stack.pop_back();
            double x = stack.back().as_float();
            stack.back() = Value::float_v(std::atan2(y, x));
            return true;
        }
        case 40: { // exp
            if (stack.empty()) return true;
            double x = stack.back().as_float();
            stack.back() = Value::float_v(std::exp(x));
            return true;
        }
        case 41: { // log
            if (stack.empty()) return true;
            double x = stack.back().as_float();
            stack.back() = Value::float_v(std::log(x));
            return true;
        }
        case 42: { // log10
            if (stack.empty()) return true;
            double x = stack.back().as_float();
            stack.back() = Value::float_v(std::log10(x));
            return true;
        }
        case 46: { // trunc
            if (stack.empty()) return true;
            double x = stack.back().as_float();
            stack.back() = Value::float_v(std::trunc(x));
            return true;
        }
        case 47: { // min
            if (stack.size() < 2) return true;
            Value b = stack.back();
            stack.pop_back();
            Value a = stack.back();
            if (a.tag == ValueTag::INT && b.tag == ValueTag::INT) {
                stack.back() = Value::int_v(std::min(a.as_int(), b.as_int()));
            } else {
                stack.back() = Value::float_v(std::min(a.as_float(), b.as_float()));
            }
            return true;
        }
        case 48: { // max
            if (stack.size() < 2) return true;
            Value b = stack.back();
            stack.pop_back();
            Value a = stack.back();
            if (a.tag == ValueTag::INT && b.tag == ValueTag::INT) {
                stack.back() = Value::int_v(std::max(a.as_int(), b.as_int()));
            } else {
                stack.back() = Value::float_v(std::max(a.as_float(), b.as_float()));
            }
            return true;
        }
        case 49: { // mod
            if (stack.size() < 2) return true;
            Value b = stack.back();
            stack.pop_back();
            Value a = stack.back();
            if (a.tag == ValueTag::INT && b.tag == ValueTag::INT) {
                stack.back() = Value::int_v(a.as_int() % b.as_int());
            } else {
                stack.back() = Value::float_v(std::fmod(a.as_float(), b.as_float()));
            }
            return true;
        }
        case 50: { // sign
            if (stack.empty()) return true;
            Value v = stack.back();
            int64_t result = 0;
            if (v.tag == ValueTag::INT) {
                int64_t iv = v.as_int();
                result = (iv > 0) - (iv < 0);
            } else {
                double fv = v.as_float();
                result = (fv > 0) - (fv < 0);
            }
            stack.back() = Value::int_v(result);
            return true;
        }
        case 54: { // random_int
            if (stack.size() < 2) return true;
            int64_t max = stack.back().as_int();
            stack.pop_back();
            int64_t min = stack.back().as_int();
            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::uniform_int_distribution<int64_t> dist(min, max);
            stack.back() = Value::int_v(dist(gen));
            return true;
        }
        case 55: { // random_seed
            if (!stack.empty()) stack.pop_back();
            return true;
        }
        
        // ========== 更多数组函数 (60-79) ==========
        case 63: { // arr_insert
            if (stack.size() < 3) return true;
            Value val = stack.back();
            stack.pop_back();
            int64_t idx = stack.back().as_int();
            stack.pop_back();
            Value arr_val = stack.back();
            if (arr_val.tag == ValueTag::ARRAY) {
                auto arr = std::get<std::shared_ptr<ArrayValue>>(arr_val.data);
                if (arr && idx >= 0 && idx <= static_cast<int64_t>(arr->elements.size())) {
                    arr->elements.insert(arr->elements.begin() + idx, val);
                }
            }
            stack.back() = arr_val;
            return true;
        }
        case 64: { // arr_remove
            if (stack.size() < 2) return true;
            int64_t idx = stack.back().as_int();
            stack.pop_back();
            Value arr_val = stack.back();
            if (arr_val.tag == ValueTag::ARRAY) {
                auto arr = std::get<std::shared_ptr<ArrayValue>>(arr_val.data);
                if (arr && idx >= 0 && idx < static_cast<int64_t>(arr->elements.size())) {
                    arr->elements.erase(arr->elements.begin() + idx);
                }
            }
            stack.back() = arr_val;
            return true;
        }
        case 66: { // arr_reverse
            if (stack.empty()) return true;
            Value arr_val = stack.back();
            if (arr_val.tag == ValueTag::ARRAY) {
                auto arr = std::get<std::shared_ptr<ArrayValue>>(arr_val.data);
                if (arr) {
                    std::reverse(arr->elements.begin(), arr->elements.end());
                }
            }
            return true;
        }
        case 67: { // arr_find
            if (stack.size() < 2) return true;
            Value val = stack.back();
            stack.pop_back();
            Value arr_val = stack.back();
            int64_t result = -1;
            if (arr_val.tag == ValueTag::ARRAY) {
                auto arr = std::get<std::shared_ptr<ArrayValue>>(arr_val.data);
                if (arr) {
                    for (size_t i = 0; i < arr->elements.size(); ++i) {
                        if (arr->elements[i].to_string() == val.to_string()) {
                            result = static_cast<int64_t>(i);
                            break;
                        }
                    }
                }
            }
            stack.back() = Value::int_v(result);
            return true;
        }
        case 68: { // arr_contains
            if (stack.size() < 2) return true;
            Value val = stack.back();
            stack.pop_back();
            Value arr_val = stack.back();
            bool found = false;
            if (arr_val.tag == ValueTag::ARRAY) {
                auto arr = std::get<std::shared_ptr<ArrayValue>>(arr_val.data);
                if (arr) {
                    for (const auto& elem : arr->elements) {
                        if (elem.to_string() == val.to_string()) {
                            found = true;
                            break;
                        }
                    }
                }
            }
            stack.back() = Value::bool_v(found);
            return true;
        }
        case 69: { // arr_unique
            if (stack.empty()) return true;
            Value arr_val = stack.back();
            if (arr_val.tag == ValueTag::ARRAY) {
                auto arr = std::get<std::shared_ptr<ArrayValue>>(arr_val.data);
                if (arr) {
                    std::vector<Value> unique_vals;
                    for (const auto& elem : arr->elements) {
                        bool found = false;
                        for (const auto& u : unique_vals) {
                            if (u.to_string() == elem.to_string()) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) unique_vals.push_back(elem);
                    }
                    auto new_arr = std::make_shared<ArrayValue>();
                    new_arr->elements = unique_vals;
                    stack.back() = Value::array_v(new_arr);
                }
            }
            return true;
        }
        case 70: { // arr_concat
            if (stack.size() < 2) return true;
            Value arr2_val = stack.back();
            stack.pop_back();
            Value arr1_val = stack.back();
            std::vector<Value> result;
            if (arr1_val.tag == ValueTag::ARRAY) {
                auto arr1 = std::get<std::shared_ptr<ArrayValue>>(arr1_val.data);
                if (arr1) {
                    for (const auto& v : arr1->elements) result.push_back(v);
                }
            }
            if (arr2_val.tag == ValueTag::ARRAY) {
                auto arr2 = std::get<std::shared_ptr<ArrayValue>>(arr2_val.data);
                if (arr2) {
                    for (const auto& v : arr2->elements) result.push_back(v);
                }
            }
            auto new_arr = std::make_shared<ArrayValue>();
            new_arr->elements = result;
            stack.back() = Value::array_v(new_arr);
            return true;
        }
        case 71: { // arr_slice
            if (stack.size() < 3) return true;
            int64_t end = stack.back().as_int();
            stack.pop_back();
            int64_t start = stack.back().as_int();
            stack.pop_back();
            Value arr_val = stack.back();
            std::vector<Value> result;
            if (arr_val.tag == ValueTag::ARRAY) {
                auto arr = std::get<std::shared_ptr<ArrayValue>>(arr_val.data);
                if (arr) {
                    auto sz = arr->elements.size();
                    if (start < 0) start = 0;
                    if (end > static_cast<int64_t>(sz)) end = sz;
                    if (start < end) {
                        for (auto i = start; i < end; ++i) {
                            result.push_back(arr->elements[i]);
                        }
                    }
                }
            }
            auto new_arr = std::make_shared<ArrayValue>();
            new_arr->elements = result;
            stack.back() = Value::array_v(new_arr);
            return true;
        }
        case 72: { // arr_range
            if (stack.size() < 3) return true;
            int64_t step = stack.back().as_int();
            stack.pop_back();
            int64_t end = stack.back().as_int();
            stack.pop_back();
            int64_t start = stack.back().as_int();
            std::vector<Value> result;
            if (step != 0) {
                if (step > 0) {
                    for (int64_t i = start; i < end; i += step) {
                        result.push_back(Value::int_v(i));
                    }
                } else {
                    for (int64_t i = start; i > end; i += step) {
                        result.push_back(Value::int_v(i));
                    }
                }
            }
            auto new_arr = std::make_shared<ArrayValue>();
            new_arr->elements = result;
            stack.back() = Value::array_v(new_arr);
            return true;
        }
        case 73: { // arr_fill
            if (stack.size() < 2) return true;
            Value val = stack.back();
            stack.pop_back();
            int64_t n = stack.back().as_int();
            std::vector<Value> result;
            for (int64_t i = 0; i < n; ++i) {
                result.push_back(val);
            }
            auto new_arr = std::make_shared<ArrayValue>();
            new_arr->elements = result;
            stack.back() = Value::array_v(new_arr);
            return true;
        }
        
        // ========== 文件函数 (80-89) ==========
        case 80: { // file_open - simplified
            if (stack.size() < 2) return true;
            stack.pop_back();
            std::string path = stack.back().as_string();
            stack.back() = Value::string_v("file:" + path);
            return true;
        }
        case 81: { // file_close
            stack.push_back(Value::bool_v(true));
            return true;
        }
        case 82: { // file_read_line
            stack.push_back(Value::string_v(""));
            return true;
        }
        case 83: { // file_read_all
            if (stack.empty()) return true;
            std::string path = stack.back().as_string();
            stack.pop_back();
            std::ifstream file(path);
            std::string content;
            if (file.is_open()) {
                content = std::string((std::istreambuf_iterator<char>(file)),
                                      std::istreambuf_iterator<char>());
                file.close();
            }
            stack.push_back(Value::string_v(content));
            return true;
        }
        case 84: { // file_write
            if (stack.size() < 2) return true;
            std::string content = stack.back().as_string();
            stack.pop_back();
            std::string path = stack.back().as_string();
            stack.pop_back();
            std::ofstream file(path);
            bool success = file.is_open();
            if (success) {
                file << content;
                file.close();
            }
            stack.push_back(Value::bool_v(success));
            return true;
        }
        case 85: { // file_exists
            if (stack.empty()) return true;
            std::string path = stack.back().as_string();
            std::ifstream file(path);
            stack.back() = Value::bool_v(file.is_open());
            return true;
        }
        case 86: { // file_remove
            if (stack.empty()) return true;
            std::string path = stack.back().as_string();
            stack.pop_back();
            bool success = std::remove(path.c_str()) == 0;
            stack.push_back(Value::bool_v(success));
            return true;
        }
        case 87: { // file_rename
            if (stack.size() < 2) return true;
            std::string new_path = stack.back().as_string();
            stack.pop_back();
            std::string old_path = stack.back().as_string();
            stack.pop_back();
            bool success = std::rename(old_path.c_str(), new_path.c_str()) == 0;
            stack.push_back(Value::bool_v(success));
            return true;
        }
        case 88: { // file_size
            if (stack.empty()) return true;
            std::string path = stack.back().as_string();
            std::ifstream file(path, std::ios::ate | std::ios::binary);
            int64_t size = 0;
            if (file.is_open()) {
                size = file.tellg();
                file.close();
            }
            stack.back() = Value::int_v(size);
            return true;
        }
        case 89: { // mkdir
            if (stack.empty()) return true;
            std::string path = stack.back().as_string();
            bool success = std::filesystem::create_directory(path);
            stack.back() = Value::bool_v(success);
            return true;
        }
        
        // ========== 张量函数 (100-108) ==========
        case 100: { // tensor_create
            if (stack.size() < 2) return true;
            stack.pop_back(); // dtype (ignored for now)
            Value shape_val = stack.back();
            int64_t total = 1;
            if (shape_val.tag == ValueTag::ARRAY) {
                auto shape_arr = std::get<std::shared_ptr<ArrayValue>>(shape_val.data);
                if (shape_arr) {
                    for (const auto& d : shape_arr->elements) {
                        total *= d.as_int();
                    }
                }
            }
            auto new_arr = std::make_shared<ArrayValue>();
            new_arr->elements.resize(total, Value::float_v(0.0));
            stack.back() = Value::array_v(new_arr);
            return true;
        }
        case 101: { // tensor_zeros
            if (stack.empty()) return true;
            Value shape_val = stack.back();
            int64_t total = 1;
            if (shape_val.tag == ValueTag::ARRAY) {
                auto shape_arr = std::get<std::shared_ptr<ArrayValue>>(shape_val.data);
                if (shape_arr) {
                    for (const auto& d : shape_arr->elements) {
                        total *= d.as_int();
                    }
                }
            }
            auto new_arr = std::make_shared<ArrayValue>();
            new_arr->elements.resize(total, Value::float_v(0.0));
            stack.back() = Value::array_v(new_arr);
            return true;
        }
        case 102: { // tensor_ones
            if (stack.empty()) return true;
            Value shape_val = stack.back();
            int64_t total = 1;
            if (shape_val.tag == ValueTag::ARRAY) {
                auto shape_arr = std::get<std::shared_ptr<ArrayValue>>(shape_val.data);
                if (shape_arr) {
                    for (const auto& d : shape_arr->elements) {
                        total *= d.as_int();
                    }
                }
            }
            auto new_arr = std::make_shared<ArrayValue>();
            new_arr->elements.resize(total, Value::float_v(1.0));
            stack.back() = Value::array_v(new_arr);
            return true;
        }
        case 103: { // tensor_randn
            if (stack.empty()) return true;
            Value shape_val = stack.back();
            int64_t total = 1;
            if (shape_val.tag == ValueTag::ARRAY) {
                auto shape_arr = std::get<std::shared_ptr<ArrayValue>>(shape_val.data);
                if (shape_arr) {
                    for (const auto& d : shape_arr->elements) {
                        total *= d.as_int();
                    }
                }
            }
            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::normal_distribution<double> dist(0.0, 1.0);
            auto new_arr = std::make_shared<ArrayValue>();
            for (int64_t i = 0; i < total; ++i) {
                new_arr->elements.push_back(Value::float_v(dist(gen)));
            }
            stack.back() = Value::array_v(new_arr);
            return true;
        }
        case 104: { // tensor_matmul - simplified as dot product
            if (stack.size() < 2) return true;
            Value b_val = stack.back();
            stack.pop_back();
            Value a_val = stack.back();
            double sum = 0;
            if (a_val.tag == ValueTag::ARRAY && b_val.tag == ValueTag::ARRAY) {
                auto arr_a = std::get<std::shared_ptr<ArrayValue>>(a_val.data);
                auto arr_b = std::get<std::shared_ptr<ArrayValue>>(b_val.data);
                if (arr_a && arr_b) {
                    size_t min_len = std::min(arr_a->elements.size(), arr_b->elements.size());
                    for (size_t i = 0; i < min_len; ++i) {
                        sum += arr_a->elements[i].as_float() * arr_b->elements[i].as_float();
                    }
                }
            }
            stack.back() = Value::float_v(sum);
            return true;
        }
        case 105: { // tensor_reshape - just return as-is for now
            return true;
        }
        case 106: { // tensor_transpose - just return as-is for now
            return true;
        }
        case 107: { // tensor_sum
            if (stack.size() < 2) return true;
            stack.pop_back(); // axis (ignored)
            Value tensor_val = stack.back();
            double sum = 0;
            if (tensor_val.tag == ValueTag::ARRAY) {
                auto arr = std::get<std::shared_ptr<ArrayValue>>(tensor_val.data);
                if (arr) {
                    for (const auto& v : arr->elements) {
                        sum += v.as_float();
                    }
                }
            }
            stack.back() = Value::float_v(sum);
            return true;
        }
        case 108: { // tensor_mean
            if (stack.size() < 2) return true;
            stack.pop_back(); // axis (ignored)
            Value tensor_val = stack.back();
            double sum = 0;
            size_t count = 0;
            if (tensor_val.tag == ValueTag::ARRAY) {
                auto arr = std::get<std::shared_ptr<ArrayValue>>(tensor_val.data);
                if (arr) {
                    for (const auto& v : arr->elements) {
                        sum += v.as_float();
                        count++;
                    }
                }
            }
            stack.back() = Value::float_v(count > 0 ? sum / count : 0.0);
            return true;
        }
        
        default:
            std::cerr << "Unknown EXT opcode: " << opcode << "\n";
            return false;
    }
}

} // namespace vm
} // namespace claw
