// ClawVM Implementation - Stack-based Virtual Machine
// Phase 8: Bytecode Execution Engine

#include "vm/claw_vm.h"
#include "vm/webtransport_backend.h"
#include <cctype>
#include <csignal>
#include <random>
#include <fstream>
#include <filesystem>

namespace claw {
namespace vm {

// ============================================================================
// VMRuntime Constructor / Destructor
// ============================================================================

VMRuntime::VMRuntime(size_t stack_size)
    : stack(stack_size), globals(MAX_GLOBALS),
      array_pool(4), tuple_pool(2), iterator_pool(2), object_pool(4), tensor_pool(2),
      coroutine_pool(2), future_pool(2), channel_pool(2) {
    wt_backend_mock = std::make_unique<MockWebTransportBackend>();
#ifdef CLAW_ENABLE_WEBTRANSPORT
    wt_backend_msquic = std::make_unique<MsquicWebTransportBackend>();
#endif
    setup_builtins();
}

VMRuntime::~VMRuntime() = default;

WebTransportBackend* VMRuntime::select_wt_backend(const std::string& url) {
#ifdef CLAW_ENABLE_WEBTRANSPORT
    if (url.find("mock://") == 0) {
        return wt_backend_mock.get();
    }
    return wt_backend_msquic.get();
#else
    (void)url;
    return wt_backend_mock.get();
#endif
}

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
        case ValueTag::OBJECT: {
            auto obj = std::get<std::shared_ptr<ObjectValue>>(data);
            return obj->type_name;
        }
        case ValueTag::ITERATOR: return "iterator";
        case ValueTag::COROUTINE: return "coroutine";
        case ValueTag::FUTURE: {
            auto fut = std::get<std::shared_ptr<FutureValue>>(data);
            return fut->is_resolved ? "future(resolved)" : "future(pending)";
        }
        case ValueTag::WEBTRANSPORT: {
            auto wt = std::get<std::shared_ptr<WebTransportValue>>(data);
            return "webtransport(" + wt->url + ")";
        }
        case ValueTag::CHANNEL: {
            auto ch = std::get<std::shared_ptr<ChannelValue>>(data);
            return "channel(" + std::to_string(ch->queue.size()) + "/" +
                   (ch->capacity == 0 ? "unbounded" : std::to_string(ch->capacity)) +
                   ")";
        }
        default: return "<unknown>";
    }
}

std::string Value::to_print_string() const {
    switch (tag) {
        case ValueTag::STRING: return std::get<std::string>(data);
        default: return to_string();
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
        case ValueTag::OBJECT: return "object";
        case ValueTag::ITERATOR: return "iterator";
        case ValueTag::COROUTINE: return "coroutine";
        case ValueTag::FUTURE: return "future";
        case ValueTag::WEBTRANSPORT: return "webtransport";
        case ValueTag::CHANNEL: return "channel";
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
        case ValueTag::OBJECT: {
            auto o1 = std::get<std::shared_ptr<ObjectValue>>(data);
            auto o2 = std::get<std::shared_ptr<ObjectValue>>(other.data);
            if (o1->fields.size() != o2->fields.size()) return false;
            for (const auto& [k, v] : o1->fields) {
                auto it = o2->fields.find(k);
                if (it == o2->fields.end() || !v.equals(it->second)) return false;
            }
            return true;
        }
        case ValueTag::WEBTRANSPORT: {
            auto w1 = std::get<std::shared_ptr<WebTransportValue>>(data);
            auto w2 = std::get<std::shared_ptr<WebTransportValue>>(other.data);
            return w1->url == w2->url;
        }
        case ValueTag::CHANNEL: {
            auto c1 = std::get<std::shared_ptr<ChannelValue>>(data);
            auto c2 = std::get<std::shared_ptr<ChannelValue>>(other.data);
            return c1.get() == c2.get();
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
        std::cout << rt.peek().to_print_string();
        return Value::nil();
    };

    // Println function
    builtins["println"] = [](VMRuntime& rt) {
        std::cout << rt.peek().to_print_string() << std::endl;
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

    // String concat function (takes 2 args from stack)
    builtins["str_concat"] = [](VMRuntime& rt) {
        if (rt.stack_top < 2) return Value::string_v("");
        std::string b = rt.peek().to_string();
        std::string a = rt.stack[rt.stack_top - 2].to_string();
        return Value::string_v(a + b);
    };

    // Bool function
    builtins["bool"] = [](VMRuntime& rt) {
        return Value::bool_v(rt.peek().as_bool());
    };

    // Input function
    builtins["input"] = [](VMRuntime& /*rt*/) {
        std::string line;
        std::getline(std::cin, line);
        return Value::string_v(line);
    };

    // Array function
    builtins["array"] = [](VMRuntime& rt) {
        auto arr = rt.array_pool.acquire();
        return Value{ValueTag::ARRAY, arr};
    };

    // Range function (generator)
    builtins["range"] = [](VMRuntime& rt) {
        int64_t end = rt.peek().as_int();
        int64_t start = 0;
        auto arr = rt.array_pool.acquire();
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

    // ============================================================================
    // Channel builtins (P0 - MPMC queue foundation)
    // ============================================================================
    builtins["channel"] = [](VMRuntime& rt) -> Value {
        int64_t cap = 0;
        if (rt.stack_top > 0) {
            cap = rt.peek().as_int();
        }
        auto ch = rt.channel_pool.acquire();
        ch->queue.clear();
        ch->capacity = cap > 0 ? static_cast<size_t>(cap) : 0;
        ch->closed = false;
        return Value::channel_v(ch);
    };

    builtins["ch_send"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) {
            auto fut = rt.future_pool.acquire();
            fut->is_resolved = true;
            fut->resolved_value = Value::bool_v(false);
            return Value::future_v(fut);
        }
        Value val = rt.peek();
        Value ch_val = rt.stack[rt.stack_top - 2];
        auto ch = ch_val.as_channel();
        auto future = rt.future_pool.acquire();
        if (!ch) {
            future->is_resolved = true;
            future->resolved_value = Value::bool_v(false);
            return Value::future_v(future);
        }
        std::lock_guard<std::mutex> lock(ch->mtx);
        if (ch->closed) {
            future->is_resolved = true;
            future->resolved_value = Value::bool_v(false);
        } else if (ch->capacity == 0 || ch->queue.size() < ch->capacity) {
            ch->queue.push_back(val);
            future->is_resolved = true;
            future->resolved_value = Value::bool_v(true);
            ch->cv.notify_one();
        } else {
            future->is_resolved = true;
            future->resolved_value = Value::bool_v(false);
        }
        return Value::future_v(future);
    };

    builtins["ch_recv"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) {
            auto fut = rt.future_pool.acquire();
            fut->is_resolved = true;
            return Value::future_v(fut);
        }
        Value ch_val = rt.peek();
        auto ch = ch_val.as_channel();
        auto future = rt.future_pool.acquire();
        if (!ch) {
            future->is_resolved = true;
            return Value::future_v(future);
        }
        std::lock_guard<std::mutex> lock(ch->mtx);
        if (!ch->queue.empty()) {
            Value val = ch->queue.front();
            ch->queue.pop_front();
            future->is_resolved = true;
            future->resolved_value = val;
            ch->cv.notify_one();
        } else if (ch->closed) {
            future->is_resolved = true;
        } else {
            future->is_resolved = true;
        }
        return Value::future_v(future);
    };

    builtins["stream_next"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) {
            auto fut = rt.future_pool.acquire();
            fut->is_resolved = true;
            return Value::future_v(fut);
        }
        Value ch_val = rt.peek();
        auto ch = ch_val.as_channel();
        auto future = rt.future_pool.acquire();
        if (!ch) {
            future->is_resolved = true;
            return Value::future_v(future);
        }
        auto tup = rt.tuple_pool.acquire();
        std::lock_guard<std::mutex> lock(ch->mtx);
        if (!ch->queue.empty()) {
            Value val = ch->queue.front();
            ch->queue.pop_front();
            tup->elements.push_back(val);
            tup->elements.push_back(Value::bool_v(true));
            future->is_resolved = true;
            future->resolved_value = Value::tuple_v(tup);
            ch->cv.notify_one();
        } else if (ch->closed) {
            tup->elements.push_back(Value::nil());
            tup->elements.push_back(Value::bool_v(false));
            future->is_resolved = true;
            future->resolved_value = Value::tuple_v(tup);
        } else {
            tup->elements.push_back(Value::nil());
            tup->elements.push_back(Value::bool_v(false));
            future->is_resolved = true;
            future->resolved_value = Value::tuple_v(tup);
        }
        return Value::future_v(future);
    };

    builtins["ch_try_send"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::bool_v(false);
        Value val = rt.peek();
        Value ch_val = rt.stack[rt.stack_top - 2];
        auto ch = ch_val.as_channel();
        if (!ch) return Value::bool_v(false);
        std::lock_guard<std::mutex> lock(ch->mtx);
        if (ch->closed) return Value::bool_v(false);
        if (ch->capacity > 0 && ch->queue.size() >= ch->capacity) return Value::bool_v(false);
        ch->queue.push_back(val);
        ch->cv.notify_one();
        return Value::bool_v(true);
    };

    builtins["ch_try_recv"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::nil();
        Value ch_val = rt.peek();
        auto ch = ch_val.as_channel();
        if (!ch) return Value::nil();
        std::lock_guard<std::mutex> lock(ch->mtx);
        if (ch->queue.empty()) return Value::nil();
        Value val = ch->queue.front();
        ch->queue.pop_front();
        ch->cv.notify_one();
        return val;
    };

    builtins["ch_close"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::bool_v(false);
        Value ch_val = rt.peek();
        auto ch = ch_val.as_channel();
        if (!ch) return Value::bool_v(false);
        std::lock_guard<std::mutex> lock(ch->mtx);
        ch->closed = true;
        ch->cv.notify_all();
        return Value::bool_v(true);
    };

    // Event system builtins (P1)
    builtins["event_publish"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::bool_v(false);
        Value val = rt.peek();
        Value name_val = rt.stack[rt.stack_top - 2];
        if (!name_val.is_string()) return Value::bool_v(false);
        std::string name = name_val.as_string();
        auto& ch = rt.event_channels[name];
        if (!ch) {
            ch = rt.channel_pool.acquire();
            ch->capacity = 0;
            ch->closed = false;
        }
        {
            std::lock_guard<std::mutex> lock(ch->mtx);
            ch->queue.push_back(val);
            ch->cv.notify_one();
        }
        return Value::bool_v(true);
    };

    builtins["event_subscribe"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::bool_v(false);
        Value handler_val = rt.peek();
        Value name_val = rt.stack[rt.stack_top - 2];
        if (!name_val.is_string()) return Value::bool_v(false);
        if (!handler_val.is_closure()) return Value::bool_v(false);
        std::string name = name_val.as_string();
        auto& ch = rt.event_channels[name];
        if (!ch) {
            ch = rt.channel_pool.acquire();
            ch->capacity = 0;
            ch->closed = false;
        }
        rt.event_handlers[name].push_back(handler_val);
        return Value::bool_v(true);
    };

    // Command system builtins (P3)
    builtins["command_send"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) {
            auto fut = rt.future_pool.acquire();
            fut->is_resolved = true;
            return Value::future_v(fut);
        }
        Value args_val = rt.peek();
        Value name_val = rt.stack[rt.stack_top - 2];
        if (!name_val.is_string()) {
            auto fut = rt.future_pool.acquire();
            fut->is_resolved = true;
            return Value::future_v(fut);
        }
        std::string name = name_val.as_string();
        int64_t req_id = rt.next_command_id++;

        // Create pending future
        auto future = rt.future_pool.acquire();
        future->is_resolved = false;
        rt.pending_commands[req_id] = future;

        // Pack request into tuple (id, name, args)
        auto req_tup = rt.tuple_pool.acquire();
        req_tup->elements.push_back(Value::int_v(req_id));
        req_tup->elements.push_back(name_val);
        req_tup->elements.push_back(args_val);

        // Send to command channel
        if (!rt.command_channel) {
            rt.command_channel = rt.channel_pool.acquire();
            rt.command_channel->capacity = 0;
            rt.command_channel->closed = false;
        }
        {
            std::lock_guard<std::mutex> lock(rt.command_channel->mtx);
            rt.command_channel->queue.push_back(Value::tuple_v(req_tup));
            rt.command_channel->cv.notify_one();
        }

        return Value::future_v(future);
    };

    builtins["command_register"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::bool_v(false);
        Value handler_val = rt.peek();
        Value name_val = rt.stack[rt.stack_top - 2];
        if (!name_val.is_string()) return Value::bool_v(false);
        if (!handler_val.is_closure()) return Value::bool_v(false);
        std::string name = name_val.as_string();
        rt.command_handlers[name].push_back(handler_val);
        return Value::bool_v(true);
    };

    // ============================================================================
    // Stream transformation operators (P5)
    // ============================================================================
    builtins["stream_map"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::channel_v(rt.channel_pool.acquire());
        Value closure_val = rt.peek();
        Value ch_val = rt.stack[rt.stack_top - 2];
        auto in_ch = ch_val.as_channel();
        auto out_ch = rt.channel_pool.acquire();
        out_ch->queue.clear();
        out_ch->capacity = 0;
        out_ch->closed = false;
        if (!in_ch || !closure_val.is_closure()) {
            return Value::channel_v(out_ch);
        }
        std::vector<Value> elems;
        {
            std::lock_guard<std::mutex> lock(in_ch->mtx);
            elems.assign(in_ch->queue.begin(), in_ch->queue.end());
            in_ch->queue.clear();
        }
        for (auto& elem : elems) {
            if (rt.vm) {
                Value mapped = rt.vm->execute_closure(closure_val, {elem});
                std::lock_guard<std::mutex> lock(out_ch->mtx);
                out_ch->queue.push_back(mapped);
            }
        }
        return Value::channel_v(out_ch);
    };

    builtins["stream_filter"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::channel_v(rt.channel_pool.acquire());
        Value closure_val = rt.peek();
        Value ch_val = rt.stack[rt.stack_top - 2];
        auto in_ch = ch_val.as_channel();
        auto out_ch = rt.channel_pool.acquire();
        out_ch->queue.clear();
        out_ch->capacity = 0;
        out_ch->closed = false;
        if (!in_ch || !closure_val.is_closure()) {
            return Value::channel_v(out_ch);
        }
        std::vector<Value> elems;
        {
            std::lock_guard<std::mutex> lock(in_ch->mtx);
            elems.assign(in_ch->queue.begin(), in_ch->queue.end());
            in_ch->queue.clear();
        }
        for (auto& elem : elems) {
            if (rt.vm) {
                Value pred = rt.vm->execute_closure(closure_val, {elem});
                if (pred.as_bool()) {
                    std::lock_guard<std::mutex> lock(out_ch->mtx);
                    out_ch->queue.push_back(elem);
                }
            }
        }
        return Value::channel_v(out_ch);
    };

    builtins["stream_buffer"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::channel_v(rt.channel_pool.acquire());
        int64_t batch_size = rt.peek().as_int();
        Value ch_val = rt.stack[rt.stack_top - 2];
        auto in_ch = ch_val.as_channel();
        auto out_ch = rt.channel_pool.acquire();
        out_ch->queue.clear();
        out_ch->capacity = 0;
        out_ch->closed = false;
        if (!in_ch || batch_size <= 0) {
            return Value::channel_v(out_ch);
        }
        std::vector<Value> elems;
        {
            std::lock_guard<std::mutex> lock(in_ch->mtx);
            elems.assign(in_ch->queue.begin(), in_ch->queue.end());
            in_ch->queue.clear();
        }
        std::vector<Value> batch;
        for (auto& elem : elems) {
            batch.push_back(elem);
            if (static_cast<int64_t>(batch.size()) >= batch_size) {
                auto arr = rt.array_pool.acquire();
                arr->elements = std::move(batch);
                std::lock_guard<std::mutex> lock(out_ch->mtx);
                out_ch->queue.push_back(Value::array_v(arr));
                batch.clear();
            }
        }
        if (!batch.empty()) {
            auto arr = rt.array_pool.acquire();
            arr->elements = std::move(batch);
            std::lock_guard<std::mutex> lock(out_ch->mtx);
            out_ch->queue.push_back(Value::array_v(arr));
        }
        return Value::channel_v(out_ch);
    };

    builtins["stream_merge"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::channel_v(rt.channel_pool.acquire());
        Value arr_val = rt.peek();
        auto out_ch = rt.channel_pool.acquire();
        out_ch->queue.clear();
        out_ch->capacity = 0;
        out_ch->closed = false;
        if (!arr_val.is_array()) {
            return Value::channel_v(out_ch);
        }
        auto arr = std::get<std::shared_ptr<ArrayValue>>(arr_val.data);
        for (auto& ch_val : arr->elements) {
            auto in_ch = ch_val.as_channel();
            if (!in_ch) continue;
            std::vector<Value> elems;
            {
                std::lock_guard<std::mutex> lock(in_ch->mtx);
                elems.assign(in_ch->queue.begin(), in_ch->queue.end());
                in_ch->queue.clear();
            }
            std::lock_guard<std::mutex> lock(out_ch->mtx);
            for (auto& elem : elems) {
                out_ch->queue.push_back(elem);
            }
        }
        return Value::channel_v(out_ch);
    };

#ifdef CLAW_ENABLE_WEBTRANSPORT
    // WebTransport mock builtins (async, return Future with WebTransport handle)
    builtins["wt_connect"] = [](VMRuntime& rt) -> Value {
        std::string url = "";
        if (rt.stack_top > 0) {
            url = rt.stack[rt.stack_top - 1].as_string();
        }
        auto wt = std::make_shared<WebTransportValue>();
        wt->url = url;
        wt->connected = true;
        auto future = rt.future_pool.acquire();
        future->is_resolved = true;
        future->resolved_value = Value::webtransport_v(wt);
        return Value::future_v(future);
    };
    builtins["wt_send"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) {
            auto future = rt.future_pool.acquire();
            future->is_resolved = true;
            future->resolved_value = Value::bool_v(false);
            return Value::future_v(future);
        }
        std::string data = rt.stack[rt.stack_top - 1].as_string();
        Value handle = rt.stack[rt.stack_top - 2];
        bool ok = false;
        if (handle.is_webtransport()) {
            auto wt = handle.as_webtransport();
            {
                std::lock_guard<std::mutex> lock(wt->queue_mutex);
                wt->incoming_queue.push_back(data);
            }
            ok = true;
        }
        auto future = rt.future_pool.acquire();
        future->is_resolved = true;
        future->resolved_value = Value::bool_v(ok);
        return Value::future_v(future);
    };
    builtins["wt_recv"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) {
            auto future = rt.future_pool.acquire();
            future->is_resolved = true;
            future->resolved_value = Value::string_v("");
            return Value::future_v(future);
        }
        Value handle = rt.stack[rt.stack_top - 1];
        std::string msg = "hello";
        if (handle.is_webtransport()) {
            auto wt = handle.as_webtransport();
            std::lock_guard<std::mutex> lock(wt->queue_mutex);
            if (!wt->incoming_queue.empty()) {
                msg = wt->incoming_queue.front();
                wt->incoming_queue.pop_front();
            }
        }
        auto future = rt.future_pool.acquire();
        future->is_resolved = true;
        future->resolved_value = Value::string_v(msg);
        return Value::future_v(future);
    };
    builtins["wt_recv_timeout"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) {
            auto future = rt.future_pool.acquire();
            future->is_resolved = true;
            future->resolved_value = Value::string_v("");
            return Value::future_v(future);
        }
        Value handle = rt.stack[rt.stack_top - 2];
        std::string msg = "hello";
        if (handle.is_webtransport()) {
            auto wt = handle.as_webtransport();
            std::lock_guard<std::mutex> lock(wt->queue_mutex);
            if (!wt->incoming_queue.empty()) {
                msg = wt->incoming_queue.front();
                wt->incoming_queue.pop_front();
            }
        }
        auto future = rt.future_pool.acquire();
        future->is_resolved = true;
        future->resolved_value = Value::string_v(msg);
        return Value::future_v(future);
    };
    builtins["wt_close"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) {
            auto future = rt.future_pool.acquire();
            future->is_resolved = true;
            future->resolved_value = Value::bool_v(false);
            return Value::future_v(future);
        }
        Value handle = rt.stack[rt.stack_top - 1];
        if (handle.is_webtransport()) {
            handle.as_webtransport()->closed = true;
        }
        auto future = rt.future_pool.acquire();
        future->is_resolved = true;
        future->resolved_value = Value::bool_v(true);
        return Value::future_v(future);
    };
    builtins["wt_ready"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::bool_v(false);
        Value handle = rt.stack[rt.stack_top - 1];
        if (handle.is_webtransport()) {
            return Value::bool_v(handle.as_webtransport()->connected);
        }
        return Value::bool_v(false);
    };

    // WebTransport bridge builtins (P4)
    builtins["wt_bridge_event"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::bool_v(false);
        Value conn_val = rt.stack[rt.stack_top - 1];
        Value name_val = rt.stack[rt.stack_top - 2];
        if (!conn_val.is_webtransport() || !name_val.is_string()) return Value::bool_v(false);
        VMRuntime::BridgeEntry entry;
        entry.target_name = name_val.as_string();
        entry.bridge_kind = "event";
        entry.connection = conn_val.as_webtransport();
        rt.bridge_registry.push_back(std::move(entry));
        return Value::bool_v(true);
    };

    builtins["wt_bridge_command"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::bool_v(false);
        Value conn_val = rt.stack[rt.stack_top - 1];
        Value name_val = rt.stack[rt.stack_top - 2];
        if (!conn_val.is_webtransport() || !name_val.is_string()) return Value::bool_v(false);
        VMRuntime::BridgeEntry entry;
        entry.target_name = name_val.as_string();
        entry.bridge_kind = "command";
        entry.connection = conn_val.as_webtransport();
        rt.bridge_registry.push_back(std::move(entry));
        return Value::bool_v(true);
    };

    builtins["wt_bridge_stream"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::bool_v(false);
        Value conn_val = rt.stack[rt.stack_top - 1];
        Value name_val = rt.stack[rt.stack_top - 2];
        if (!conn_val.is_webtransport() || !name_val.is_string()) return Value::bool_v(false);
        VMRuntime::BridgeEntry entry;
        entry.target_name = name_val.as_string();
        entry.bridge_kind = "stream";
        entry.connection = conn_val.as_webtransport();
        rt.bridge_registry.push_back(std::move(entry));
        return Value::bool_v(true);
    };
#endif

    // ============================================================================
    // I/O builtins
    // ============================================================================
    builtins["read_file"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::string_v("");
        std::string path = rt.peek().as_string();
        std::ifstream file(path);
        std::string content;
        if (file.is_open()) {
            content = std::string((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
            file.close();
        }
        return Value::string_v(content);
    };

    builtins["write_file"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::bool_v(false);
        std::string content = rt.peek().as_string();
        std::string path = rt.stack[rt.stack_top - 2].as_string();
        std::ofstream file(path);
        bool ok = file.is_open();
        if (ok) {
            file << content;
            file.close();
        }
        return Value::bool_v(ok);
    };

    builtins["append_file"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::bool_v(false);
        std::string content = rt.peek().as_string();
        std::string path = rt.stack[rt.stack_top - 2].as_string();
        std::ofstream file(path, std::ios::app);
        bool ok = file.is_open();
        if (ok) {
            file << content;
            file.close();
        }
        return Value::bool_v(ok);
    };

    // ============================================================================
    // String builtins
    // ============================================================================
    builtins["str_len"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::int_v(0);
        Value& v = rt.peek();
        if (v.is_string()) {
            return Value::int_v(static_cast<int64_t>(v.as_string().size()));
        }
        if (v.is_array()) {
            auto arr = std::get<std::shared_ptr<ArrayValue>>(v.data);
            return Value::int_v(static_cast<int64_t>(arr ? arr->elements.size() : 0));
        }
        return Value::int_v(0);
    };

    builtins["str_contains"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::bool_v(false);
        std::string sub = rt.peek().as_string();
        std::string s = rt.stack[rt.stack_top - 2].as_string();
        return Value::bool_v(s.find(sub) != std::string::npos);
    };

    builtins["str_find"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::int_v(-1);
        std::string sub = rt.peek().as_string();
        std::string s = rt.stack[rt.stack_top - 2].as_string();
        size_t pos = s.find(sub);
        return Value::int_v(pos == std::string::npos ? -1 : static_cast<int64_t>(pos));
    };

    builtins["str_replace"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 3) return Value::string_v("");
        std::string to = rt.peek().as_string();
        std::string from = rt.stack[rt.stack_top - 2].as_string();
        std::string s = rt.stack[rt.stack_top - 3].as_string();
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.length(), to);
            pos += to.length();
        }
        return Value::string_v(s);
    };

    builtins["str_split"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::array_v(rt.array_pool.acquire());
        std::string delim = rt.peek().as_string();
        std::string s = rt.stack[rt.stack_top - 2].as_string();
        auto arr = rt.array_pool.acquire();
        size_t start = 0, end = 0;
        while ((end = s.find(delim, start)) != std::string::npos) {
            arr->elements.push_back(Value::string_v(s.substr(start, end - start)));
            start = end + delim.length();
        }
        arr->elements.push_back(Value::string_v(s.substr(start)));
        return Value::array_v(arr);
    };

    builtins["str_upper"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::string_v("");
        std::string s = rt.peek().as_string();
        for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return Value::string_v(s);
    };

    builtins["str_lower"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::string_v("");
        std::string s = rt.peek().as_string();
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return Value::string_v(s);
    };

    builtins["str_trim"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::string_v("");
        std::string s = rt.peek().as_string();
        size_t a = s.find_first_not_of(" \t\n\r");
        if (a == std::string::npos) return Value::string_v("");
        size_t b = s.find_last_not_of(" \t\n\r");
        return Value::string_v(s.substr(a, b - a + 1));
    };

    builtins["str_substring"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 3) return Value::string_v("");
        int64_t len = rt.peek().as_int();
        int64_t start = rt.stack[rt.stack_top - 2].as_int();
        std::string s = rt.stack[rt.stack_top - 3].as_string();
        if (start < 0) start = 0;
        if (start >= static_cast<int64_t>(s.length())) return Value::string_v("");
        auto max_len = static_cast<size_t>(s.length() - start);
        auto actual_len = len > 0 ? std::min(static_cast<size_t>(len), max_len) : max_len;
        return Value::string_v(s.substr(start, actual_len));
    };

    builtins["str_starts_with"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::bool_v(false);
        std::string prefix = rt.peek().as_string();
        std::string s = rt.stack[rt.stack_top - 2].as_string();
        return Value::bool_v(s.rfind(prefix, 0) == 0);
    };

    builtins["str_ends_with"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::bool_v(false);
        std::string suffix = rt.peek().as_string();
        std::string s = rt.stack[rt.stack_top - 2].as_string();
        if (suffix.length() > s.length()) return Value::bool_v(false);
        return Value::bool_v(s.compare(s.length() - suffix.length(), suffix.length(), suffix) == 0);
    };

    builtins["str_reverse"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::string_v("");
        std::string s = rt.peek().as_string();
        std::reverse(s.begin(), s.end());
        return Value::string_v(s);
    };

    builtins["str_repeat"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::string_v("");
        int64_t n = rt.peek().as_int();
        std::string s = rt.stack[rt.stack_top - 2].as_string();
        if (n <= 0) return Value::string_v("");
        std::string result;
        result.reserve(s.length() * n);
        for (int64_t i = 0; i < n; ++i) result += s;
        return Value::string_v(result);
    };

    builtins["str_join"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::string_v("");
        std::string delim = rt.peek().as_string();
        Value arr_val = rt.stack[rt.stack_top - 2];
        std::string result;
        if (arr_val.is_array()) {
            auto arr = std::get<std::shared_ptr<ArrayValue>>(arr_val.data);
            if (arr) {
                for (size_t i = 0; i < arr->elements.size(); ++i) {
                    if (i > 0) result += delim;
                    result += arr->elements[i].to_string();
                }
            }
        }
        return Value::string_v(result);
    };

    builtins["format"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::string_v("");
        std::string arg = rt.peek().to_string();
        std::string fmt = rt.stack[rt.stack_top - 2].as_string();
        size_t pos = fmt.find("{}");
        if (pos != std::string::npos) {
            fmt.replace(pos, 2, arg);
        }
        return Value::string_v(fmt);
    };

    // ============================================================================
    // Math builtins
    // ============================================================================
    builtins["abs"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::int_v(0);
        Value v = rt.peek();
        if (v.is_int()) {
            int64_t x = v.as_int();
            return Value::int_v(x < 0 ? -x : x);
        }
        return Value::float_v(std::fabs(v.as_float()));
    };

    builtins["sin"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::float_v(0.0);
        return Value::float_v(std::sin(rt.peek().as_float()));
    };

    builtins["cos"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::float_v(0.0);
        return Value::float_v(std::cos(rt.peek().as_float()));
    };

    builtins["tan"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::float_v(0.0);
        return Value::float_v(std::tan(rt.peek().as_float()));
    };

    builtins["asin"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::float_v(0.0);
        return Value::float_v(std::asin(rt.peek().as_float()));
    };

    builtins["acos"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::float_v(0.0);
        return Value::float_v(std::acos(rt.peek().as_float()));
    };

    builtins["atan"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::float_v(0.0);
        return Value::float_v(std::atan(rt.peek().as_float()));
    };

    builtins["atan2"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::float_v(0.0);
        double y = rt.peek().as_float();
        double x = rt.stack[rt.stack_top - 2].as_float();
        return Value::float_v(std::atan2(y, x));
    };

    builtins["sqrt"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::float_v(0.0);
        return Value::float_v(std::sqrt(rt.peek().as_float()));
    };

    builtins["pow"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::float_v(0.0);
        double exp = rt.peek().as_float();
        double base = rt.stack[rt.stack_top - 2].as_float();
        return Value::float_v(std::pow(base, exp));
    };

    builtins["exp"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::float_v(0.0);
        return Value::float_v(std::exp(rt.peek().as_float()));
    };

    builtins["log"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::float_v(0.0);
        return Value::float_v(std::log(rt.peek().as_float()));
    };

    builtins["log10"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::float_v(0.0);
        return Value::float_v(std::log10(rt.peek().as_float()));
    };

    builtins["floor"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::float_v(0.0);
        return Value::float_v(std::floor(rt.peek().as_float()));
    };

    builtins["ceil"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::float_v(0.0);
        return Value::float_v(std::ceil(rt.peek().as_float()));
    };

    builtins["round"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::float_v(0.0);
        return Value::float_v(std::round(rt.peek().as_float()));
    };

    builtins["trunc"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::float_v(0.0);
        return Value::float_v(std::trunc(rt.peek().as_float()));
    };

    builtins["min"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::int_v(0);
        Value b = rt.peek();
        Value a = rt.stack[rt.stack_top - 2];
        if (a.is_int() && b.is_int()) {
            return Value::int_v(std::min(a.as_int(), b.as_int()));
        }
        return Value::float_v(std::min(a.as_float(), b.as_float()));
    };

    builtins["max"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::int_v(0);
        Value b = rt.peek();
        Value a = rt.stack[rt.stack_top - 2];
        if (a.is_int() && b.is_int()) {
            return Value::int_v(std::max(a.as_int(), b.as_int()));
        }
        return Value::float_v(std::max(a.as_float(), b.as_float()));
    };

    builtins["mod"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::int_v(0);
        Value b = rt.peek();
        Value a = rt.stack[rt.stack_top - 2];
        if (a.is_int() && b.is_int()) {
            return Value::int_v(a.as_int() % b.as_int());
        }
        return Value::float_v(std::fmod(a.as_float(), b.as_float()));
    };

    builtins["sign"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::int_v(0);
        Value v = rt.peek();
        if (v.is_int()) {
            int64_t iv = v.as_int();
            return Value::int_v((iv > 0) - (iv < 0));
        }
        double fv = v.as_float();
        return Value::int_v((fv > 0) - (fv < 0));
    };

    builtins["pi"] = [](VMRuntime&) -> Value {
        return Value::float_v(3.14159265358979323846);
    };

    builtins["e"] = [](VMRuntime&) -> Value {
        return Value::float_v(2.71828182845904523536);
    };

    builtins["random"] = [](VMRuntime&) -> Value {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_real_distribution<> dis(0.0, 1.0);
        return Value::float_v(dis(gen));
    };

    builtins["random_int"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::int_v(0);
        int64_t max = rt.peek().as_int();
        int64_t min = rt.stack[rt.stack_top - 2].as_int();
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<int64_t> dist(min, max);
        return Value::int_v(dist(gen));
    };

    builtins["random_seed"] = [](VMRuntime&) -> Value {
        return Value::nil();
    };

    // ============================================================================
    // Array builtins
    // ============================================================================
    builtins["arr_len"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::int_v(0);
        Value v = rt.peek();
        if (v.is_array()) {
            auto arr = std::get<std::shared_ptr<ArrayValue>>(v.data);
            return Value::int_v(static_cast<int64_t>(arr ? arr->elements.size() : 0));
        }
        return Value::int_v(0);
    };

    builtins["arr_push"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::nil();
        Value elem = rt.peek();
        Value arr_val = rt.stack[rt.stack_top - 2];
        if (arr_val.is_array()) {
            auto arr = std::get<std::shared_ptr<ArrayValue>>(arr_val.data);
            if (arr) arr->elements.push_back(elem);
        }
        return arr_val;
    };

    builtins["arr_pop"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::nil();
        Value v = rt.peek();
        if (v.is_array()) {
            auto arr = std::get<std::shared_ptr<ArrayValue>>(v.data);
            if (arr && !arr->elements.empty()) {
                Value result = arr->elements.back();
                arr->elements.pop_back();
                return result;
            }
        }
        return Value::nil();
    };

    builtins["arr_insert"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 3) return Value::nil();
        Value val = rt.peek();
        int64_t idx = rt.stack[rt.stack_top - 2].as_int();
        Value arr_val = rt.stack[rt.stack_top - 3];
        if (arr_val.is_array()) {
            auto arr = std::get<std::shared_ptr<ArrayValue>>(arr_val.data);
            if (arr && idx >= 0 && idx <= static_cast<int64_t>(arr->elements.size())) {
                arr->elements.insert(arr->elements.begin() + idx, val);
            }
        }
        return arr_val;
    };

    builtins["arr_remove"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::nil();
        int64_t idx = rt.peek().as_int();
        Value arr_val = rt.stack[rt.stack_top - 2];
        if (arr_val.is_array()) {
            auto arr = std::get<std::shared_ptr<ArrayValue>>(arr_val.data);
            if (arr && idx >= 0 && idx < static_cast<int64_t>(arr->elements.size())) {
                arr->elements.erase(arr->elements.begin() + idx);
            }
        }
        return arr_val;
    };

    builtins["arr_sort"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::nil();
        Value v = rt.peek();
        if (v.is_array()) {
            auto arr = std::get<std::shared_ptr<ArrayValue>>(v.data);
            if (arr) {
                std::sort(arr->elements.begin(), arr->elements.end(),
                    [](const Value& a, const Value& b) {
                        return a.as_float() < b.as_float();
                    });
            }
        }
        return v;
    };

    builtins["arr_reverse"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::nil();
        Value v = rt.peek();
        if (v.is_array()) {
            auto arr = std::get<std::shared_ptr<ArrayValue>>(v.data);
            if (arr) std::reverse(arr->elements.begin(), arr->elements.end());
        }
        return v;
    };

    builtins["arr_find"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::int_v(-1);
        Value val = rt.peek();
        Value arr_val = rt.stack[rt.stack_top - 2];
        if (arr_val.is_array()) {
            auto arr = std::get<std::shared_ptr<ArrayValue>>(arr_val.data);
            if (arr) {
                for (size_t i = 0; i < arr->elements.size(); ++i) {
                    if (arr->elements[i].to_string() == val.to_string()) {
                        return Value::int_v(static_cast<int64_t>(i));
                    }
                }
            }
        }
        return Value::int_v(-1);
    };

    builtins["arr_contains"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::bool_v(false);
        Value val = rt.peek();
        Value arr_val = rt.stack[rt.stack_top - 2];
        if (arr_val.is_array()) {
            auto arr = std::get<std::shared_ptr<ArrayValue>>(arr_val.data);
            if (arr) {
                for (const auto& elem : arr->elements) {
                    if (elem.to_string() == val.to_string()) {
                        return Value::bool_v(true);
                    }
                }
            }
        }
        return Value::bool_v(false);
    };

    builtins["arr_unique"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::array_v(rt.array_pool.acquire());
        Value v = rt.peek();
        if (v.is_array()) {
            auto arr = std::get<std::shared_ptr<ArrayValue>>(v.data);
            if (arr) {
                auto result = rt.array_pool.acquire();
                for (const auto& elem : arr->elements) {
                    bool found = false;
                    for (const auto& u : result->elements) {
                        if (u.to_string() == elem.to_string()) { found = true; break; }
                    }
                    if (!found) result->elements.push_back(elem);
                }
                return Value::array_v(result);
            }
        }
        return v;
    };

    builtins["arr_concat"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::array_v(rt.array_pool.acquire());
        Value b_val = rt.peek();
        Value a_val = rt.stack[rt.stack_top - 2];
        auto result = rt.array_pool.acquire();
        if (a_val.is_array()) {
            auto a = std::get<std::shared_ptr<ArrayValue>>(a_val.data);
            if (a) for (const auto& v : a->elements) result->elements.push_back(v);
        }
        if (b_val.is_array()) {
            auto b = std::get<std::shared_ptr<ArrayValue>>(b_val.data);
            if (b) for (const auto& v : b->elements) result->elements.push_back(v);
        }
        return Value::array_v(result);
    };

    builtins["arr_slice"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 3) return Value::array_v(rt.array_pool.acquire());
        int64_t end = rt.peek().as_int();
        int64_t start = rt.stack[rt.stack_top - 2].as_int();
        Value arr_val = rt.stack[rt.stack_top - 3];
        auto result = rt.array_pool.acquire();
        if (arr_val.is_array()) {
            auto arr = std::get<std::shared_ptr<ArrayValue>>(arr_val.data);
            if (arr) {
                auto sz = arr->elements.size();
                if (start < 0) start = 0;
                if (end > static_cast<int64_t>(sz)) end = sz;
                for (auto i = start; i < end; ++i) result->elements.push_back(arr->elements[i]);
            }
        }
        return Value::array_v(result);
    };

    builtins["arr_range"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 3) return Value::array_v(rt.array_pool.acquire());
        int64_t step = rt.peek().as_int();
        int64_t end = rt.stack[rt.stack_top - 2].as_int();
        int64_t start = rt.stack[rt.stack_top - 3].as_int();
        auto result = rt.array_pool.acquire();
        if (step != 0) {
            if (step > 0) {
                for (int64_t i = start; i < end; i += step) result->elements.push_back(Value::int_v(i));
            } else {
                for (int64_t i = start; i > end; i += step) result->elements.push_back(Value::int_v(i));
            }
        }
        return Value::array_v(result);
    };

    builtins["arr_fill"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::array_v(rt.array_pool.acquire());
        Value val = rt.peek();
        int64_t n = rt.stack[rt.stack_top - 2].as_int();
        auto result = rt.array_pool.acquire();
        for (int64_t i = 0; i < n; ++i) result->elements.push_back(val);
        return Value::array_v(result);
    };

    // ============================================================================
    // File builtins
    // ============================================================================
    builtins["file_exists"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::bool_v(false);
        std::string path = rt.peek().as_string();
        std::ifstream file(path);
        return Value::bool_v(file.is_open());
    };

    builtins["file_remove"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::bool_v(false);
        std::string path = rt.peek().as_string();
        return Value::bool_v(std::remove(path.c_str()) == 0);
    };

    builtins["file_rename"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 2) return Value::bool_v(false);
        std::string new_path = rt.peek().as_string();
        std::string old_path = rt.stack[rt.stack_top - 2].as_string();
        return Value::bool_v(std::rename(old_path.c_str(), new_path.c_str()) == 0);
    };

    builtins["file_size"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::int_v(0);
        std::string path = rt.peek().as_string();
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        int64_t size = 0;
        if (file.is_open()) {
            size = file.tellg();
            file.close();
        }
        return Value::int_v(size);
    };

    builtins["mkdir"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::bool_v(false);
        std::string path = rt.peek().as_string();
        return Value::bool_v(std::filesystem::create_directory(path));
    };

    // ============================================================================
    // Type conversion builtins
    // ============================================================================
    builtins["to_int"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::int_v(0);
        return Value::int_v(static_cast<int64_t>(rt.peek().as_float()));
    };

    builtins["to_float"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::float_v(0.0);
        return Value::float_v(rt.peek().as_float());
    };

    builtins["to_string"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::string_v("");
        return Value::string_v(rt.peek().to_string());
    };

    builtins["to_bool"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::bool_v(false);
        return Value::bool_v(rt.peek().as_bool());
    };

    builtins["type_of"] = [](VMRuntime& rt) -> Value {
        if (rt.stack_top < 1) return Value::string_v("nil");
        return Value::string_v(rt.peek().type_name());
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
        case ValueTag::OBJECT:
            mark_object(std::get<std::shared_ptr<ObjectValue>>(val.data).get());
            break;
        case ValueTag::ITERATOR:
            mark_iterator(std::get<std::shared_ptr<IteratorValue>>(val.data).get());
            break;
        case ValueTag::COROUTINE:
            mark_coroutine(std::get<std::shared_ptr<CoroutineValue>>(val.data).get());
            break;
        case ValueTag::FUTURE:
            mark_future(std::get<std::shared_ptr<FutureValue>>(val.data).get());
            break;
        case ValueTag::WEBTRANSPORT:
            mark_webtransport(std::get<std::shared_ptr<WebTransportValue>>(val.data).get());
            break;
        case ValueTag::CHANNEL: {
            auto ch = std::get<std::shared_ptr<ChannelValue>>(val.data);
            if (!ch->marked) {
                ch->marked = true;
                for (auto& qv : ch->queue) {
                    mark_value(qv);
                }
            }
            break;
        }
        default:
            break;
    }
}

void GarbageCollector::mark_object(ObjectValue* obj) {
    if (!obj || obj->marked) return;
    obj->marked = true;
    for (auto& [name, val] : obj->fields) {
        mark_value(val);
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

void GarbageCollector::mark_iterator(IteratorValue* iter) {
    if (!iter || iter->marked) return;
    iter->marked = true;
    for (auto& arr : iter->arrays) {
        for (auto& v : arr) {
            mark_value(const_cast<Value&>(v));
        }
    }
}

void GarbageCollector::mark_coroutine(CoroutineValue* coro) {
    if (!coro || coro->marked) return;
    coro->marked = true;
    for (auto& v : coro->saved_locals) mark_value(const_cast<Value&>(v));
    for (auto& v : coro->saved_expr_stack) mark_value(const_cast<Value&>(v));
    if (coro->parent_future) mark_future(coro->parent_future.get());
    if (coro->waiting_on) mark_future(coro->waiting_on.get());
}

void GarbageCollector::mark_future(FutureValue* fut) {
    if (!fut || fut->marked) return;
    fut->marked = true;
    mark_value(const_cast<Value&>(fut->resolved_value));
    for (auto& coro : fut->waiting_coroutines) {
        if (coro) mark_coroutine(coro.get());
    }
}

void GarbageCollector::mark_webtransport(WebTransportValue* wt) {
    if (!wt || wt->marked) return;
    wt->marked = true;
}

void GarbageCollector::collect(VMRuntime& runtime) {
    // Mark phase
    // Mark all values in call frames
    for (auto& frame : runtime.call_frames) {
        if (frame.closure) {
            mark_closure(frame.closure.get());
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

    // Mark event handlers
    for (auto& [name, handlers] : runtime.event_handlers) {
        for (auto& handler : handlers) {
            mark_value(handler);
        }
    }
    // Mark event channels
    for (auto& [name, ch] : runtime.event_channels) {
        if (ch) {
            std::lock_guard<std::mutex> lock(ch->mtx);
            for (auto& val : ch->queue) {
                mark_value(val);
            }
        }
    }

    // Sweep phase
    sweep(runtime);
}

void GarbageCollector::sweep(VMRuntime& /*runtime*/) {
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
    method_table.clear();

    // Setup globals from module
    for (size_t i = 0; i < module.global_names.size(); i++) {
        runtime.define_global(module.global_names[i]);
    }

    // Build method dispatch table from mangled function names: Type__method
    for (size_t i = 0; i < module.functions.size(); i++) {
        const std::string& name = module.functions[i].name;
        size_t pos = name.find("__");
        if (pos != std::string::npos && pos > 0) {
            std::string type_name = name.substr(0, pos);
            std::string method_name = name.substr(pos + 2);
            method_table[type_name][method_name] = static_cast<int32_t>(i);
        }
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

bool ClawVM::execute_begin() {
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
            last_error = "No main function found in bytecode module";
            had_error = true;
            running = false;
            return false;
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
        frame.closure = main_closure;
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

        return true;

    } catch (const std::exception& e) {
        error(e.what());
        had_error = true;
        running = false;
        return false;
    }
}

Value ClawVM::execute_finish() {
    if (runtime.stack_top > 0) {
        return runtime.pop();
    }
    return Value::nil();
}

int32_t ClawVM::peek_opcode() const {
    if (!current_function || ip < 0 || ip >= static_cast<int32_t>(current_function->code.size())) {
        return -1;
    }
    return static_cast<int32_t>(current_function->code[ip].op);
}

Value ClawVM::execute() {
    if (!execute_begin()) {
        return Value::nil();
    }

    try {
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

    return execute_finish();
}

Value ClawVM::execute_closure(Value closure_val, const std::vector<Value>& args) {
    if (!closure_val.is_closure()) {
        return Value::nil();
    }

    int32_t saved_frame_count = runtime.frame_count;
    int32_t saved_stack_top = runtime.stack_top;

    auto closure = std::get<std::shared_ptr<ClosureValue>>(closure_val.data);
    auto& func = closure->function;

    // Push args
    for (auto& arg : args) {
        runtime.push(arg);
    }
    // Push closure
    runtime.push(closure_val);

    // Replicate op_call logic
    if (static_cast<size_t>(runtime.frame_count) >= MAX_CALL_FRAMES) {
        error("Call stack overflow in execute_closure");
        runtime.stack_top = saved_stack_top;
        return Value::nil();
    }

    if (!runtime.call_frames.empty()) {
        runtime.call_frames.back().ip = ip;
    }

    CallFrame frame;
    frame.closure = closure;
    frame.ip = 0;
    frame.base_stack = runtime.stack_top - static_cast<int32_t>(args.size()) - 1;
    frame.slot_count = func->max_stack > 0 ? func->max_stack : 256;
    frame.local_count = func->local_count;

    runtime.call_frames.push_back(frame);
    runtime.frame_count++;

    // Update current function
    if (func->func_id >= 0 && func->func_id < static_cast<int32_t>(current_module.functions.size())) {
        current_function_idx = func->func_id;
        current_function = &current_module.functions[current_function_idx];
    } else {
        error("Invalid function id in execute_closure");
        runtime.call_frames.pop_back();
        runtime.frame_count--;
        runtime.stack_top = saved_stack_top;
        return Value::nil();
    }

    ip = 0;
    if (current_function) {
        runtime.stack_top = frame.base_stack + current_function->local_count;
    }

    // Run until the closure frame returns
    bool prev_running = running;
    running = true;
    while (running && runtime.frame_count > saved_frame_count) {
        if (!dispatch()) break;
        instructions_executed++;
    }
    running = prev_running;

    Value result = Value::nil();
    if (runtime.stack_top > frame.base_stack) {
        result = runtime.pop();
    }
    runtime.stack_top = saved_stack_top;
    return result;
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

[[gnu::hot]] bool ClawVM::dispatch() {
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
    if (op == static_cast<int32_t>(bytecode::OpCode::EQ)) return op_eq();
    if (op == static_cast<int32_t>(bytecode::OpCode::NE)) return op_ne();

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
    if (op == static_cast<int32_t>(bytecode::OpCode::CALL_METHOD)) return op_call_method();
    
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
    if (op == static_cast<int32_t>(bytecode::OpCode::ALLOC_OBJ_TYPE)) return op_alloc_obj_type();
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
    if (op == static_cast<int32_t>(bytecode::OpCode::THROW)) return op_throw();

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

bool ClawVM::op_eq() {
    Value b = runtime.pop();
    Value a = runtime.pop();
    bool result = false;
    if (a.is_int() && b.is_int()) {
        result = std::get<int64_t>(a.data) == std::get<int64_t>(b.data);
    } else if (a.is_float() && b.is_float()) {
        result = std::get<double>(a.data) == std::get<double>(b.data);
    } else if (a.is_bool() && b.is_bool()) {
        result = std::get<bool>(a.data) == std::get<bool>(b.data);
    } else if (a.is_string() && b.is_string()) {
        result = std::get<std::string>(a.data) == std::get<std::string>(b.data);
    } else if (a.is_nil() && b.is_nil()) {
        result = true;
    }
    runtime.push(Value::bool_v(result));
    return true;
}

bool ClawVM::op_ne() {
    Value b = runtime.pop();
    Value a = runtime.pop();
    bool result = false;
    if (a.is_int() && b.is_int()) {
        result = std::get<int64_t>(a.data) != std::get<int64_t>(b.data);
    } else if (a.is_float() && b.is_float()) {
        result = std::get<double>(a.data) != std::get<double>(b.data);
    } else if (a.is_bool() && b.is_bool()) {
        result = std::get<bool>(a.data) != std::get<bool>(b.data);
    } else if (a.is_string() && b.is_string()) {
        result = std::get<std::string>(a.data) != std::get<std::string>(b.data);
    } else if (a.is_nil() && b.is_nil()) {
        result = false;
    } else {
        result = true; // Different types are not equal
    }
    runtime.push(Value::bool_v(result));
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
    frame.closure = closure;
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
            if (current_function_idx < current_module.functions.size()) {
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
            if (current_function_idx < current_module.functions.size()) {
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

    // Save IP so execute_closure (or nested dispatch) can restore caller state
    if (!runtime.call_frames.empty()) {
        runtime.call_frames.back().ip = ip;
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

bool ClawVM::op_call_method() {
    int32_t packed = static_cast<int32_t>(current_function->code[ip - 1].operand);
    int32_t str_idx = packed & 0xFFFF;
    int32_t arg_count = (packed >> 16) & 0xFFFF;

    std::string method_name;
    if (str_idx >= 0 && str_idx < static_cast<int32_t>(current_module.constants.values.size())) {
        method_name = current_module.constants.values[str_idx].str;
    }

    // Pop arguments (self is the last one popped)
    std::vector<Value> args;
    args.reserve(arg_count);
    for (int i = 0; i < arg_count; i++) {
        args.push_back(runtime.pop());
    }
    std::reverse(args.begin(), args.end());

    if (args.empty()) {
        error("Method call requires at least self argument");
        return false;
    }

    Value& self = args[0];
    std::string type_name;
    if (self.tag == ValueTag::OBJECT) {
        type_name = std::get<std::shared_ptr<ObjectValue>>(self.data)->type_name;
    }

    if (type_name.empty()) {
        error("Method call on non-object type");
        return false;
    }

    auto type_it = method_table.find(type_name);
    if (type_it == method_table.end()) {
        error("No methods found for type: " + type_name);
        return false;
    }

    auto method_it = type_it->second.find(method_name);
    if (method_it == type_it->second.end()) {
        error("Method not found: " + type_name + "." + method_name);
        return false;
    }

    int32_t func_idx = method_it->second;
    if (func_idx < 0 || func_idx >= static_cast<int32_t>(current_module.functions.size())) {
        error("Invalid method function index");
        return false;
    }

    // Create closure for the method
    auto closure = std::make_shared<ClosureValue>();
    closure->function = std::make_shared<FunctionValue>();
    closure->function->func_id = func_idx;
    closure->function->arity = current_module.functions[func_idx].arity;
    closure->function->local_count = current_module.functions[func_idx].local_count;
    closure->function->max_stack = 256; // default max stack
    closure->function->name = current_module.functions[func_idx].name;

    // Save current IP in current frame
    if (!runtime.call_frames.empty()) {
        runtime.call_frames.back().ip = ip;
    }

    // Create new call frame
    if (static_cast<size_t>(runtime.frame_count) >= MAX_CALL_FRAMES) {
        error("Call stack overflow");
        return false;
    }

    // Push closure and args back onto stack for the call
    runtime.push(Value{ValueTag::CLOSURE, closure});
    for (const auto& arg : args) {
        runtime.push(arg);
    }

    CallFrame frame;
    frame.closure = closure;
    frame.ip = 0;
    frame.base_stack = runtime.stack_top - arg_count;
    frame.slot_count = closure->function->max_stack > 0 ? closure->function->max_stack : 256;
    frame.local_count = closure->function->local_count;

    runtime.call_frames.push_back(frame);
    runtime.frame_count++;

    // Reserve stack space for locals
    runtime.stack_top = frame.base_stack + frame.local_count;

    // Update current function
    current_function_idx = func_idx;
    current_function = &current_module.functions[func_idx];
    ip = 0;

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
    auto arr = runtime.array_pool.acquire();
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
    
    auto iter = runtime.iterator_pool.acquire();
    
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
    
    auto iter = runtime.iterator_pool.acquire();
    iter->kind = "range";
    iter->start = start;
    iter->end = end;
    iter->step = step;
    iter->index = start;
    iter->size = (end - start + (step > 0 ? step - 1 : step + 1)) / (step > 0 ? step : -step);
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
    auto iter = runtime.iterator_pool.acquire();
    iter->kind = "enumerate";
    iter->size = static_cast<int64_t>(arr->elements.size());
    iter->index = 0;
    iter->outer_index = 0;
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

    auto iter = runtime.iterator_pool.acquire();
    iter->kind = "zip";
    iter->arrays = arrays;
    iter->size = arrays.empty() ? 0 : static_cast<int64_t>(arrays[0].size());
    for (const auto& arr : arrays) {
        if (static_cast<int64_t>(arr.size()) < iter->size) {
            iter->size = static_cast<int64_t>(arr.size());
        }
    }
    iter->index = 0;
    runtime.push(Value::iterator_v(iter));
    return true;
}

// ============================================================================
// Coroutine Operations
// ============================================================================

void ClawVM::save_coroutine_frame(std::shared_ptr<CoroutineValue> coro) {
    if (runtime.call_frames.empty()) return;

    auto& frame = runtime.call_frames.back();
    coro->func_id = current_function_idx;
    coro->saved_ip = ip;
    coro->saved_base_stack = frame.base_stack;
    coro->saved_stack_top = runtime.stack_top;

    // Save locals (base_stack to base_stack + local_count)
    coro->saved_locals.clear();
    for (int32_t i = frame.base_stack; i < frame.base_stack + frame.local_count; i++) {
        coro->saved_locals.push_back(runtime.stack[i]);
    }

    // Save expression stack (above locals up to stack_top)
    coro->saved_expr_stack.clear();
    for (int32_t i = frame.base_stack + frame.local_count; i < runtime.stack_top; i++) {
        coro->saved_expr_stack.push_back(runtime.stack[i]);
    }
}

void ClawVM::restore_coroutine_frame(std::shared_ptr<CoroutineValue> coro) {
    if (coro->func_id < 0) return;

    // Set current function context
    current_function_idx = coro->func_id;
    if (current_function_idx < current_module.functions.size()) {
        current_function = &current_module.functions[current_function_idx];
    }

    ip = coro->saved_ip;

    // Create a synthetic closure for the coroutine
    auto func = std::make_shared<FunctionValue>();
    func->func_id = coro->func_id;
    func->name = current_function ? current_function->name : "coro";
    func->arity = current_function ? current_function->arity : 0;
    func->local_count = current_function ? current_function->local_count : 0;
    func->max_stack = func->local_count + func->arity + 16;

    auto closure = std::make_shared<ClosureValue>();
    closure->function = func;

    // Setup call frame at current stack top
    CallFrame frame;
    frame.closure = closure;
    frame.ip = coro->saved_ip;
    frame.base_stack = runtime.stack_top;
    frame.slot_count = func->max_stack;
    frame.local_count = func->local_count;

    // Ensure stack capacity
    int32_t required_top = frame.base_stack + func->local_count + static_cast<int32_t>(coro->saved_expr_stack.size()) + 16;
    if (required_top > static_cast<int32_t>(runtime.stack.size())) {
        runtime.stack.resize(runtime.stack.size() * 2);
    }

    // Restore locals
    for (size_t i = 0; i < coro->saved_locals.size() && i < static_cast<size_t>(func->local_count); i++) {
        runtime.stack[frame.base_stack + i] = coro->saved_locals[i];
    }
    for (int32_t i = static_cast<int32_t>(coro->saved_locals.size()); i < func->local_count; i++) {
        runtime.stack[frame.base_stack + i] = Value::nil();
    }

    // Restore expression stack
    for (size_t i = 0; i < coro->saved_expr_stack.size(); i++) {
        runtime.stack[frame.base_stack + func->local_count + i] = coro->saved_expr_stack[i];
    }

    runtime.stack_top = frame.base_stack + func->local_count + static_cast<int32_t>(coro->saved_expr_stack.size());

    runtime.call_frames.push_back(frame);
    runtime.frame_count++;
}

Value ClawVM::resume_coroutine(std::shared_ptr<CoroutineValue> coro) {
    if (coro->is_complete) {
        if (coro->parent_future && coro->parent_future->is_resolved) {
            return coro->parent_future->resolved_value;
        }
        return Value::nil();
    }

    restore_coroutine_frame(coro);

    // If this coroutine was waiting on a resolved future, push the value
    // as if CO_AWAIT had resolved it immediately
    if (coro->waiting_on && coro->waiting_on->is_resolved) {
        runtime.push(coro->waiting_on->resolved_value);
        coro->waiting_on = nullptr;
    }

    running = true;

    // Run until completion or next suspension
    while (running && dispatch()) {
        instructions_executed++;
    }

    // If the coroutine finished (frame_count == 0), mark it complete
    if (runtime.frame_count == 0 || !running) {
        coro->is_complete = true;
    }

    if (coro->parent_future && coro->parent_future->is_resolved) {
        return coro->parent_future->resolved_value;
    }
    return Value::nil();
}

bool ClawVM::op_co_create() {
    // Stack: [closure] -> [coroutine]
    Value callee = runtime.pop();
    if (!callee.is_closure()) {
        error("CO_CREATE requires a closure");
        return false;
    }

    auto closure = std::get<std::shared_ptr<ClosureValue>>(callee.data);
    auto& func = closure->function;

    auto coro = runtime.coroutine_pool.acquire();
    coro->func_id = func->func_id;
    coro->saved_ip = 0;
    coro->saved_base_stack = 0;
    coro->saved_stack_top = 0;
    coro->saved_locals.clear();
    coro->saved_expr_stack.clear();
    coro->is_complete = false;

    runtime.push(Value::coroutine_v(coro));
    return true;
}

bool ClawVM::op_co_yield() {
    // General coroutine yield - save current frame and suspend
    // For async/await, CO_AWAIT is used instead
    if (!runtime.call_frames.empty()) {
        // Create a temporary coroutine to save state
        auto coro = runtime.coroutine_pool.acquire();
        save_coroutine_frame(coro);
        // Note: the caller of CO_RESUME should hold onto this coroutine
    }
    running = false;
    return true;
}

bool ClawVM::op_co_resume() {
    // Stack: [coroutine] -> [result]
    Value coro_val = runtime.pop();
    if (!coro_val.is_coroutine()) {
        error("CO_RESUME requires a coroutine");
        return false;
    }

    auto coro = std::get<std::shared_ptr<CoroutineValue>>(coro_val.data);
    resume_coroutine(coro);

    if (coro->parent_future && coro->parent_future->is_resolved) {
        runtime.push(coro->parent_future->resolved_value);
    } else {
        runtime.push(Value::nil());
    }
    return true;
}

bool ClawVM::op_co_await() {
    // Stack: [future] -> [value]  (if resolved)
    //        [future] -> []       (if pending, suspends)
    if (runtime.stack_top <= 0) {
        error("Stack underflow in CO_AWAIT");
        return false;
    }

    Value future_val = runtime.pop();
    if (!future_val.is_future()) {
        error("CO_AWAIT requires a Future");
        return false;
    }

    auto future = std::get<std::shared_ptr<FutureValue>>(future_val.data);

    if (future->is_resolved) {
        // Future already resolved - push value and continue
        runtime.push(future->resolved_value);
        return true;
    }

    // Future pending - save state and suspend
    auto coro = runtime.coroutine_pool.acquire();
    coro->waiting_on = future;
    save_coroutine_frame(coro);

    // Register as waiting on this future
    future->waiting_coroutines.push_back(coro);

    // Pop current call frame without returning a value (suspend)
    if (runtime.frame_count > 0) {
        runtime.call_frames.pop_back();
        runtime.frame_count--;
    }

    // Reset stack to caller's reserved state
    int32_t caller_base = runtime.call_frames.empty() ? 0 : runtime.call_frames.back().base_stack;
    int32_t caller_locals = runtime.call_frames.empty() ? 0 : runtime.call_frames.back().local_count;
    runtime.stack_top = caller_base + caller_locals;

    if (runtime.frame_count == 0) {
        running = false;
    } else {
        // Restore caller context
        ip = runtime.call_frames.back().ip;
        auto caller_closure = runtime.call_frames.back().closure;
        if (caller_closure && caller_closure->function) {
            current_function_idx = caller_closure->function->func_id;
            if (current_function_idx < current_module.functions.size()) {
                current_function = &current_module.functions[current_function_idx];
            }
        }
    }

    return true;
}

bool ClawVM::op_async_call(int32_t arg_count) {
    // Like op_call but schedules the function asynchronously
    // Stack: [closure, arg1, ..., argN] -> [future]
    if (arg_count < 0) arg_count = 0;

    Value callee = runtime.pop();
    if (!callee.is_closure()) {
        error("ASYNC_CALL requires a closure");
        return false;
    }

    auto closure = std::get<std::shared_ptr<ClosureValue>>(callee.data);
    auto& func = closure->function;

    // Collect arguments from stack
    std::vector<Value> args;
    int32_t args_base = runtime.stack_top - arg_count;
    for (int32_t i = 0; i < arg_count; i++) {
        args.push_back(runtime.stack[args_base + i]);
    }

    // Create future for the async function's result
    auto future = runtime.future_pool.acquire();
    future->is_resolved = false;

    // Create coroutine with initial locals (future in slot 0, then args, then nil padding)
    auto coro = runtime.coroutine_pool.acquire();
    coro->func_id = func->func_id;
    coro->parent_future = future;
    coro->saved_ip = 0;
    coro->saved_base_stack = 0;
    coro->saved_stack_top = func->local_count;
    coro->is_complete = false;

    // Slot 0 = future (injected so FUTURE_RESOLVE resolves the caller's future)
    coro->saved_locals.push_back(Value::future_v(future));
    for (int32_t i = 0; i < arg_count; i++) {
        coro->saved_locals.push_back(args[i]);
    }
    for (int32_t i = arg_count + 1; i < func->local_count; i++) {
        coro->saved_locals.push_back(Value::nil());
    }
    coro->saved_expr_stack.clear();

    // Remove args from stack and push future
    runtime.stack_top = args_base;
    runtime.push(Value::future_v(future));

    // Schedule coroutine for execution by the event loop
    runtime.ready_coroutines.push_back(coro);

    return true;
}

bool ClawVM::op_future_create() {
    // Create a future and store it in local slot 0 (hidden __future for async fn)
    // If slot 0 already has a Future (injected by ASYNC_CALL), keep it
    if (runtime.call_frames.empty()) {
        error("FUTURE_CREATE outside of function");
        return false;
    }

    auto& frame = runtime.call_frames.back();
    if (frame.local_count > 0 && runtime.stack[frame.base_stack].is_future()) {
        return true; // Future already injected by ASYNC_CALL
    }

    auto future = runtime.future_pool.acquire();
    future->is_resolved = false;
    runtime.stack[frame.base_stack] = Value::future_v(future);
    return true;
}

bool ClawVM::op_future_resolve() {
    // Stack: [value] -> []
    // Resolve the current async function's future (in local slot 0) and return
    if (runtime.stack_top <= 0) {
        error("Stack underflow in FUTURE_RESOLVE");
        return false;
    }

    Value result = runtime.pop();

    if (runtime.call_frames.empty()) {
        error("FUTURE_RESOLVE outside of function");
        return false;
    }

    auto& frame = runtime.call_frames.back();
    Value future_val = runtime.stack[frame.base_stack];

    if (!future_val.is_future()) {
        error("FUTURE_RESOLVE: slot 0 is not a Future");
        return false;
    }

    auto future = std::get<std::shared_ptr<FutureValue>>(future_val.data);
    future->is_resolved = true;
    future->resolved_value = result;

    // Wake up waiting coroutines via callback
    if (runtime.on_future_resolved) {
        runtime.on_future_resolved(future);
    }

    // Pop current frame (return from async function without pushing result)
    runtime.call_frames.pop_back();
    runtime.frame_count--;

    int32_t caller_base = runtime.call_frames.empty() ? 0 : runtime.call_frames.back().base_stack;
    int32_t caller_locals = runtime.call_frames.empty() ? 0 : runtime.call_frames.back().local_count;
    runtime.stack_top = caller_base + caller_locals;

    if (runtime.frame_count == 0) {
        running = false;
    } else {
        ip = runtime.call_frames.back().ip;
        auto caller_closure = runtime.call_frames.back().closure;
        if (caller_closure && caller_closure->function) {
            current_function_idx = caller_closure->function->func_id;
            if (current_function_idx < current_module.functions.size()) {
                current_function = &current_module.functions[current_function_idx];
            }
        }
    }

    return true;
}

bool ClawVM::op_future_is_ready() {
    // Stack: [future] -> [bool]
    if (runtime.stack_top <= 0) {
        error("Stack underflow in FUTURE_IS_READY");
        return false;
    }

    Value future_val = runtime.pop();
    if (!future_val.is_future()) {
        error("FUTURE_IS_READY requires a Future");
        return false;
    }

    auto future = std::get<std::shared_ptr<FutureValue>>(future_val.data);
    runtime.push(Value::bool_v(future->is_resolved));
    return true;
}

// ============================================================================
// Object Operations (simplified)
// ============================================================================

bool ClawVM::op_alloc_obj() {
    auto obj = runtime.object_pool.acquire();
    obj->type_name.clear();
    obj->fields.clear();
    obj->marked = false;
    runtime.push(Value::object_v(obj));
    return true;
}

bool ClawVM::op_alloc_obj_type() {
    int32_t str_idx = static_cast<int32_t>(current_function->code[ip - 1].operand);
    auto obj = runtime.object_pool.acquire();
    obj->fields.clear();
    obj->marked = false;
    if (str_idx >= 0 && str_idx < static_cast<int32_t>(current_module.constants.values.size())) {
        obj->type_name = current_module.constants.values[str_idx].str;
    } else {
        obj->type_name.clear();
    }
    runtime.push(Value::object_v(obj));
    return true;
}

bool ClawVM::op_load_field() {
    Value field_val = runtime.pop();
    Value obj = runtime.pop();
    std::string field = field_val.as_string();
    if (obj.is_object()) {
        auto o = std::get<std::shared_ptr<ObjectValue>>(obj.data);
        auto it = o->fields.find(field);
        if (it != o->fields.end()) {
            runtime.push(it->second);
            return true;
        }
    }
    runtime.push(Value::nil());
    return true;
}

bool ClawVM::op_store_field() {
    Value field_val = runtime.pop();
    std::string field = field_val.as_string();
    Value val = runtime.pop();
    Value obj = runtime.pop();
    if (obj.is_object()) {
        auto o = std::get<std::shared_ptr<ObjectValue>>(obj.data);
        o->fields[field] = val;
    }
    runtime.push(obj);
    return true;
}

bool ClawVM::op_obj_type() {
    Value obj = runtime.pop();
    if (obj.is_object()) {
        auto o = std::get<std::shared_ptr<ObjectValue>>(obj.data);
        runtime.push(Value::string_v(o->type_name));
    } else {
        runtime.push(Value::string_v(obj.type_name()));
    }
    return true;
}

// ============================================================================
// Tuple Operations
// ============================================================================

bool ClawVM::op_create_tuple() {
    int32_t count = static_cast<int32_t>(current_function->code[ip - 1].operand);
    auto tup = runtime.tuple_pool.acquire();
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
    
    auto tensor = runtime.tensor_pool.acquire();
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
    
    auto result = runtime.tensor_pool.acquire();
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
    auto result = runtime.tensor_pool.acquire();
    *result = *tensor;

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

bool ClawVM::op_throw() {
    Value exception = runtime.pop();

    // Search for exception handler in current function
    while (runtime.frame_count > 0) {
        auto* func = current_function;
        if (func) {
            const auto* handler = func->find_handler(static_cast<uint32_t>(ip));
            if (handler) {
                // Jump to catch block
                ip = static_cast<int32_t>(handler->catch_ip);
                // Store exception in catch variable if specified
                if (handler->catch_var >= 0) {
                    uint32_t slot = runtime.call_frames[runtime.frame_count - 1].base_stack + handler->catch_var;
                    if (slot < runtime.stack.size()) {
                        runtime.stack[slot] = exception;
                    } else if (slot == runtime.stack.size()) {
                        runtime.stack.push_back(exception);
                    } else {
                        runtime.stack.resize(slot + 1, Value::nil());
                        runtime.stack[slot] = exception;
                    }
                }
                return true;
            }
        }

        // No handler in current function - unwind one frame
        if (runtime.frame_count <= 1) break;
        runtime.call_frames.pop_back();
        runtime.frame_count--;
        if (runtime.frame_count > 0) {
            auto& frame = runtime.call_frames[runtime.frame_count - 1];
            current_function_idx = frame.closure->function->func_id;
            current_function = &current_module.functions[current_function_idx];
            ip = frame.ip;
        }
    }

    // No handler found - treat as panic
    error("Uncaught exception: " + exception.to_string());
    had_error = true;
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
    // Extension opcode - for stdlib function calls and coroutine ops
    // Format: EXT <packed>
    //   packed & 0xFF      = sub-opcode
    //   (packed >> 8)      = extended argument (e.g. arg_count for ASYNC_CALL)
    int packed = static_cast<int>(current_function->code[ip - 1].operand);
    int opcode = packed & 0xFF;
    int ext_arg = (packed >> 8) & 0xFFFFFF;

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
        case 4: { // read_file
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
            runtime.push(Value::string_v(content));
            return true;
        }
        case 5: { // write_file
            if (stack.size() < 2) return true;
            std::string content = stack.back().as_string();
            stack.pop_back();
            std::string path = stack.back().as_string();
            stack.pop_back();
            std::ofstream file(path);
            bool ok = file.is_open();
            if (ok) {
                file << content;
                file.close();
            }
            runtime.push(Value::bool_v(ok));
            return true;
        }
        case 6: { // append_file
            if (stack.size() < 2) return true;
            std::string content = stack.back().as_string();
            stack.pop_back();
            std::string path = stack.back().as_string();
            stack.pop_back();
            std::ofstream file(path, std::ios::app);
            bool ok = file.is_open();
            if (ok) {
                file << content;
                file.close();
            }
            runtime.push(Value::bool_v(ok));
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
            auto arr = runtime.array_pool.acquire();
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
                    auto new_arr = runtime.array_pool.acquire();
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
            auto new_arr = runtime.array_pool.acquire();
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
            auto new_arr = runtime.array_pool.acquire();
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
            auto new_arr = runtime.array_pool.acquire();
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
            auto new_arr = runtime.array_pool.acquire();
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
            auto new_arr = runtime.array_pool.acquire();
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
            auto new_arr = runtime.array_pool.acquire();
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
            auto new_arr = runtime.array_pool.acquire();
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
            auto new_arr = runtime.array_pool.acquire();
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
        
        // ========== 协程操作 (150-157) ==========
        case 150: return op_co_create();
        case 151: return op_co_yield();
        case 152: return op_co_resume();
        case 153: return op_co_await();
        case 154: return op_async_call(ext_arg);
        case 155: return op_future_create();
        case 156: return op_future_resolve();
        case 157: return op_future_is_ready();

#ifdef CLAW_ENABLE_WEBTRANSPORT
        // ========== WebTransport 函数 (200-205) ==========
        case static_cast<int>(bytecode::ExtOpCode::WT_CONNECT): {
            std::string url = "";
            if (runtime.stack_top > 0) {
                url = runtime.stack[runtime.stack_top - 1].as_string();
                runtime.stack_top--;
            }
            auto* backend = runtime.select_wt_backend(url);
            auto wt = backend->connect(url);
            if (wt) {
                wt->backend = backend;
                wt->runtime = &runtime;
            }
            auto future = runtime.future_pool.acquire();
            future->resolved_value = Value::webtransport_v(wt);

            bool is_mock = (url.find("mock://") == 0);
            if (is_mock || !wt || wt->closed) {
                future->is_resolved = true;
            } else {
                wt->connect_future = future;
                future->is_resolved = false;
                runtime.pending_futures++;
            }
            runtime.push(Value::future_v(future));
            return true;
        }
        case static_cast<int>(bytecode::ExtOpCode::WT_SEND): {
            if (runtime.stack_top < 2) {
                auto future = runtime.future_pool.acquire();
                future->is_resolved = true;
                future->resolved_value = Value::bool_v(false);
                runtime.push(Value::future_v(future));
                return true;
            }
            std::string data = runtime.stack[runtime.stack_top - 1].as_string();
            runtime.stack_top--;
            Value handle = runtime.stack[runtime.stack_top - 1];
            runtime.stack_top--;
            bool ok = false;
            if (handle.is_webtransport()) {
                auto wt = handle.as_webtransport();
                if (wt && wt->backend) ok = wt->backend->send(wt, data);
            }
            auto future = runtime.future_pool.acquire();
            future->is_resolved = true;
            future->resolved_value = Value::bool_v(ok);
            runtime.push(Value::future_v(future));
            return true;
        }
        case static_cast<int>(bytecode::ExtOpCode::WT_RECV): {
            if (runtime.stack_top <= 0) {
                auto future = runtime.future_pool.acquire();
                future->is_resolved = true;
                future->resolved_value = Value::string_v("");
                runtime.push(Value::future_v(future));
                return true;
            }
            Value handle = runtime.stack[runtime.stack_top - 1];
            runtime.stack_top--;
            std::string msg = "";
            if (handle.is_webtransport()) {
                auto wt = handle.as_webtransport();
                if (wt && wt->backend) msg = wt->backend->recv(wt, -1);
            }
            auto future = runtime.future_pool.acquire();
            future->is_resolved = true;
            future->resolved_value = Value::string_v(msg);
            runtime.push(Value::future_v(future));
            return true;
        }
        case static_cast<int>(bytecode::ExtOpCode::WT_RECV_TIMEOUT): {
            if (runtime.stack_top < 2) {
                auto future = runtime.future_pool.acquire();
                future->is_resolved = true;
                future->resolved_value = Value::string_v("");
                runtime.push(Value::future_v(future));
                return true;
            }
            int timeout_ms = static_cast<int>(runtime.stack[runtime.stack_top - 1].as_int());
            runtime.stack_top--;
            Value handle = runtime.stack[runtime.stack_top - 1];
            runtime.stack_top--;
            std::string msg = "";
            if (handle.is_webtransport()) {
                auto wt = handle.as_webtransport();
                if (wt && wt->backend) msg = wt->backend->recv(wt, timeout_ms);
            }
            auto future = runtime.future_pool.acquire();
            future->is_resolved = true;
            future->resolved_value = Value::string_v(msg);
            runtime.push(Value::future_v(future));
            return true;
        }
        case static_cast<int>(bytecode::ExtOpCode::WT_CLOSE): {
            if (runtime.stack_top <= 0) {
                auto future = runtime.future_pool.acquire();
                future->is_resolved = true;
                future->resolved_value = Value::bool_v(false);
                runtime.push(Value::future_v(future));
                return true;
            }
            Value handle = runtime.stack[runtime.stack_top - 1];
            runtime.stack_top--;
            bool ok = false;
            if (handle.is_webtransport()) {
                auto wt = handle.as_webtransport();
                if (wt && wt->backend) ok = wt->backend->close(wt);
            }
            auto future = runtime.future_pool.acquire();
            future->is_resolved = true;
            future->resolved_value = Value::bool_v(ok);
            runtime.push(Value::future_v(future));
            return true;
        }
        case static_cast<int>(bytecode::ExtOpCode::WT_READY): {
            if (runtime.stack_top <= 0) {
                runtime.push(Value::bool_v(false));
                return true;
            }
            Value handle = runtime.stack[runtime.stack_top - 1];
            runtime.stack_top--;
            bool ready = false;
            if (handle.is_webtransport()) {
                auto wt = handle.as_webtransport();
                if (wt && wt->backend) ready = wt->backend->ready(wt);
            }
            runtime.push(Value::bool_v(ready));
            return true;
        }
        case static_cast<int>(bytecode::ExtOpCode::WT_OPEN_STREAM): {
            if (runtime.stack_top < 2) {
                auto future = runtime.future_pool.acquire();
                future->is_resolved = true;
                future->resolved_value = Value::nil();
                runtime.push(Value::future_v(future));
                return true;
            }
            bool bidirectional = runtime.stack[runtime.stack_top - 1].as_bool();
            runtime.stack_top--;
            Value handle = runtime.stack[runtime.stack_top - 1];
            runtime.stack_top--;
            auto future = runtime.future_pool.acquire();
            if (handle.is_webtransport()) {
                auto wt = handle.as_webtransport();
                if (wt && wt->backend) {
                    auto stream = wt->backend->open_stream(wt, bidirectional);
                    if (stream) {
                        stream->backend = wt->backend;
                        future->resolved_value = Value::webtransport_v(stream);
                    }
                }
            }
            future->is_resolved = true;
            runtime.push(Value::future_v(future));
            return true;
        }
        case static_cast<int>(bytecode::ExtOpCode::WT_STREAM_SEND): {
            if (runtime.stack_top < 2) {
                auto future = runtime.future_pool.acquire();
                future->is_resolved = true;
                future->resolved_value = Value::bool_v(false);
                runtime.push(Value::future_v(future));
                return true;
            }
            std::string data = runtime.stack[runtime.stack_top - 1].as_string();
            runtime.stack_top--;
            Value handle = runtime.stack[runtime.stack_top - 1];
            runtime.stack_top--;
            bool ok = false;
            if (handle.is_webtransport()) {
                auto stream = handle.as_webtransport();
                if (stream && stream->backend) ok = stream->backend->stream_send(stream, data);
            }
            auto future = runtime.future_pool.acquire();
            future->is_resolved = true;
            future->resolved_value = Value::bool_v(ok);
            runtime.push(Value::future_v(future));
            return true;
        }
        case static_cast<int>(bytecode::ExtOpCode::WT_STREAM_RECV): {
            if (runtime.stack_top <= 0) {
                auto future = runtime.future_pool.acquire();
                future->is_resolved = true;
                future->resolved_value = Value::string_v("");
                runtime.push(Value::future_v(future));
                return true;
            }
            Value handle = runtime.stack[runtime.stack_top - 1];
            runtime.stack_top--;
            std::string msg = "";
            if (handle.is_webtransport()) {
                auto stream = handle.as_webtransport();
                if (stream && stream->backend) msg = stream->backend->stream_recv(stream, -1);
            }
            auto future = runtime.future_pool.acquire();
            future->is_resolved = true;
            future->resolved_value = Value::string_v(msg);
            runtime.push(Value::future_v(future));
            return true;
        }
        case static_cast<int>(bytecode::ExtOpCode::WT_STREAM_CLOSE): {
            if (runtime.stack_top <= 0) {
                auto future = runtime.future_pool.acquire();
                future->is_resolved = true;
                future->resolved_value = Value::bool_v(false);
                runtime.push(Value::future_v(future));
                return true;
            }
            Value handle = runtime.stack[runtime.stack_top - 1];
            runtime.stack_top--;
            bool ok = false;
            if (handle.is_webtransport()) {
                auto stream = handle.as_webtransport();
                if (stream && stream->backend) ok = stream->backend->stream_close(stream);
            }
            auto future = runtime.future_pool.acquire();
            future->is_resolved = true;
            future->resolved_value = Value::bool_v(ok);
            runtime.push(Value::future_v(future));
            return true;
        }
        case static_cast<int>(bytecode::ExtOpCode::WT_LISTEN): {
            std::string address = "";
            if (runtime.stack_top > 0) {
                address = runtime.stack[runtime.stack_top - 1].as_string();
                runtime.stack_top--;
            }
            auto* backend = runtime.select_wt_backend(address);
            auto server = backend->listen(address);
            if (server) {
                server->backend = backend;
            }
            auto future = runtime.future_pool.acquire();
            future->is_resolved = true;
            future->resolved_value = server ? Value::webtransport_v(server) : Value::nil();
            runtime.push(Value::future_v(future));
            return true;
        }
        case static_cast<int>(bytecode::ExtOpCode::WT_ACCEPT): {
            if (runtime.stack_top <= 0) {
                auto future = runtime.future_pool.acquire();
                future->is_resolved = true;
                future->resolved_value = Value::nil();
                runtime.push(Value::future_v(future));
                return true;
            }
            Value handle = runtime.stack[runtime.stack_top - 1];
            runtime.stack_top--;
            auto future = runtime.future_pool.acquire();
            if (handle.is_webtransport()) {
                auto server = handle.as_webtransport();
                if (server && server->backend) {
                    auto conn = server->backend->accept(server, -1);
                    if (conn) {
                        conn->backend = server->backend;
                        future->resolved_value = Value::webtransport_v(conn);
                    }
                }
            }
            future->is_resolved = true;
            runtime.push(Value::future_v(future));
            return true;
        }
        case static_cast<int>(bytecode::ExtOpCode::WT_CLOSE_SERVER): {
            if (runtime.stack_top <= 0) {
                auto future = runtime.future_pool.acquire();
                future->is_resolved = true;
                future->resolved_value = Value::bool_v(false);
                runtime.push(Value::future_v(future));
                return true;
            }
            Value handle = runtime.stack[runtime.stack_top - 1];
            runtime.stack_top--;
            bool ok = false;
            if (handle.is_webtransport()) {
                auto server = handle.as_webtransport();
                if (server && server->backend) ok = server->backend->close_server(server);
            }
            auto future = runtime.future_pool.acquire();
            future->is_resolved = true;
            future->resolved_value = Value::bool_v(ok);
            runtime.push(Value::future_v(future));
            return true;
        }
#endif

        default:
            std::cerr << "Unknown EXT opcode: " << opcode << "\n";
            return false;
    }
}

} // namespace vm
} // namespace claw
