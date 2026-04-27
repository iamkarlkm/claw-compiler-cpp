// ClawVM Implementation - Stack-based Virtual Machine
// Phase 8: Bytecode Execution Engine

#include "vm/claw_vm.h"
#include <cctype>
#include <csignal>

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
        return Value::int_v(rt.pop().as_int());
    };
    
    // Float function
    builtins["float"] = [](VMRuntime& rt) {
        return Value::float_v(rt.pop().as_float());
    };
    
    // String function
    builtins["string"] = [](VMRuntime& rt) {
        return Value::string_v(rt.pop().to_string());
    };
    
    // Bool function
    builtins["bool"] = [](VMRuntime& rt) {
        return Value::bool_v(rt.pop().as_bool());
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
        rt.pop();
        return Value{ValueTag::ARRAY, arr};
    };
    
    // Range function (generator)
    builtins["range"] = [](VMRuntime& rt) {
        int64_t end = rt.pop().as_int();
        int64_t start = 0;
        if (!rt.peek().is_nil()) {
            start = rt.pop().as_int();
            end = rt.pop().as_int();
        }
        auto arr = std::make_shared<ArrayValue>();
        for (int64_t i = start; i < end; i++) {
            arr->elements.push_back(Value::int_v(i));
        }
        return Value{ValueTag::ARRAY, arr};
    };
    
    // Panic function
    builtins["panic"] = [](VMRuntime& rt) -> Value {
        throw std::runtime_error(rt.pop().to_string());
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
    runtime.gc_cycles++;
}

// ============================================================================
// ClawVM Implementation
// ============================================================================

bool ClawVM::load_module(const bytecode::Module& module) {
    current_module = module;
    
    // Setup globals from module
    for (size_t i = 0; i < module.global_names.size(); i++) {
        // 定义全局变量并获取索引
        int32_t global_idx = runtime.define_global(module.global_names[i]);
        
        // 如果模块有函数，尝试找到对应的函数并存储
        // 在我们的编译中，全局变量名和函数名是相同的
        for (size_t j = 0; j < module.functions.size(); ++j) {
            if (module.functions[j].name == module.global_names[i]) {
                // 使用索引设置全局变量的值
                runtime.set_global(global_idx, Value::int_v(static_cast<int64_t>(j)));
                break;
            }
        }
    }
    
    return true;
}

bool ClawVM::load_module_from_file(const std::string& path) {
    bytecode::BytecodeReader reader;
    
    auto result = reader.read_from_file(path);
    if (!result) {
        last_error = "Failed to load bytecode file: " + path + " - " + reader.get_error();
        had_error = true;
        return false;
    }
    current_module = *result;
    
    return load_module(current_module);
}

Value ClawVM::execute() {
    running = true;
    instructions_executed = 0;
    ip = 0;
    
    try {
        // Find main function
        int32_t main_idx = runtime.get_global_idx("main");
        if (main_idx < 0) {
            error("No main function found");
            return Value::nil();
        }
        
        // Get main function value - could be an index or a closure
        Value main_val = runtime.get_global(main_idx);
        
        // If it's an integer, treat it as a function index
        if (main_val.is_int()) {
            int32_t func_idx = static_cast<int32_t>(main_val.as_int());
            if (func_idx >= 0 && func_idx < (int32_t)current_module.functions.size()) {
                // 设置执行上下文，直接从函数开始执行
                const auto& func = current_module.functions[func_idx];
                
                // 创建主调用帧
                ClosureValue* closure = new ClosureValue();
                
                CallFrame frame;
                frame.closure = closure;
                frame.ip = 0;
                frame.base_stack = 0;
                frame.slot_count = func.local_count;
                runtime.call_frames.push_back(frame);
                
                ip = 0;
                
                // 运行 dispatch 循环
                while (running && dispatch()) {
                    instructions_executed++;
                    
                    // GC trigger
                    if (runtime.gc_enabled && runtime.bytes_allocated > runtime.gc_threshold) {
                        GarbageCollector::collect(runtime);
                    }
                }
            } else {
                error("Invalid main function index");
            }
        } else {
            // Call main (原始方式)
            runtime.push(main_val);
            runtime.push(Value::nil());  // self argument
            
            op_call();
            
            // Run dispatch loop
            while (running && dispatch()) {
                instructions_executed++;
                
                // GC trigger
                if (runtime.gc_enabled && runtime.bytes_allocated > runtime.gc_threshold) {
                    GarbageCollector::collect(runtime);
                }
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
