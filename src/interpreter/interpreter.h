// Claw Interpreter - Direct AST Execution
// Supports: variables, arrays, tensors, functions, expressions

#ifndef CLAW_INTERPRETER_H
#define CLAW_INTERPRETER_H

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <functional>
#include <sstream>
#include <cmath>
#include <random>
#include <algorithm>
#include <numeric>
#include "ast/ast.h"
#include "lexer/token.h"

namespace claw {
namespace interpreter {

// ============================================================================
// Tensor Support - Multi-dimensional array runtime
// ============================================================================

// Tensor shape representation
struct TensorShape {
    std::vector<int64_t> dims;  // e.g., [1024, 1024] for 2D
    
    int64_t total_size() const {
        int64_t total = 1;
        for (auto d : dims) total *= d;
        return total;
    }
    
    int64_t index(const std::vector<int64_t>& indices) const {
        if (indices.size() != dims.size()) return -1;
        int64_t idx = 0;
        int64_t stride = 1;
        for (int64_t i = dims.size() - 1; i >= 0; i--) {
            if (indices[i] < 0 || indices[i] >= dims[i]) return -1;
            idx += indices[i] * stride;
            stride *= dims[i];
        }
        return idx;
    }
};

// Tensor value with multi-dimensional support
struct TensorValue {
    std::string element_type;  // f32, f64, i32, i64, etc.
    TensorShape shape;
    std::vector<double> data;  // Flat storage (currently use double for all numeric)
    std::vector<int64_t> int_data;  // For integer tensors
    
    bool is_integer() const {
        return element_type == "i32" || element_type == "i64" || 
               element_type == "u32" || element_type == "u64";
    }
    
    // Get value at indices
    double get_f64(const std::vector<int64_t>& indices) const {
        int64_t idx = shape.index(indices);
        if (idx < 0 || idx >= static_cast<int64_t>(data.size())) return 0.0;
        return data[idx];
    }
    
    int64_t get_int(const std::vector<int64_t>& indices) const {
        int64_t idx = shape.index(indices);
        if (idx < 0 || idx >= static_cast<int64_t>(int_data.size())) return 0;
        return int_data[idx];
    }
    
    // Set value at indices
    void set_f64(const std::vector<int64_t>& indices, double val) {
        int64_t idx = shape.index(indices);
        if (idx >= 0 && idx < static_cast<int64_t>(data.size())) {
            data[idx] = val;
        }
    }
    
    void set_int(const std::vector<int64_t>& indices, int64_t val) {
        int64_t idx = shape.index(indices);
        if (idx >= 0 && idx < static_cast<int64_t>(int_data.size())) {
            int_data[idx] = val;
        }
    }
    
    // Element-wise access by linear index (1-based for Claw)
    double& at_f64(int64_t idx) {
        if (idx < 1) idx = 1;
        if (idx > static_cast<int64_t>(data.size())) idx = data.size();
        return data[idx - 1];
    }
    
    int64_t& at_int(int64_t idx) {
        if (idx < 1) idx = 1;
        if (idx > static_cast<int64_t>(int_data.size())) idx = int_data.size();
        return int_data[idx - 1];
    }
};

// Extended Value type to support tensors
using Tensor = std::shared_ptr<TensorValue>;

// Runtime value types - match AST literal types exactly
using Value = std::variant<
    std::monostate,
    int64_t,
    double,
    std::string,
    bool,
    char
>;

// Runtime value wrapper that can hold scalar, array, or tensor
struct RuntimeValue {
    std::string type_name;  // u32, f64, tensor<f32>, etc.
    int64_t size;           // array size, 1 for scalar
    Value scalar;

    // For arrays
    std::vector<RuntimeValue> array;
    
    // For tensors (multi-dimensional)
    Tensor tensor;

    bool is_array() const { return size > 1 || !array.empty(); }
    bool is_tensor() const { return tensor != nullptr; }

    // Get value at index (1-based in Claw)
    Value& at(int64_t idx) {
        if (idx == 1 || array.empty()) {
            return scalar;
        }
        if (idx >= 1 && idx <= static_cast<int64_t>(array.size())) {
            return array[idx - 1].scalar;
        }
        return scalar;
    }

    const Value& at(int64_t idx) const {
        if (idx == 1 || array.empty()) {
            return scalar;
        }
        if (idx >= 1 && idx <= static_cast<int64_t>(array.size())) {
            return array[idx - 1].scalar;
        }
        return scalar;
    }
};

// Runtime environment
class Runtime {
public:
    std::map<std::string, RuntimeValue> variables;

    // Built-in functions
    std::map<std::string, std::function<std::vector<Value>(std::vector<Value>)>> builtins;

    Runtime() {
        // Register built-in functions
        builtins["println"] = [this](std::vector<Value> args) -> std::vector<Value> {
            for (size_t i = 0; i < args.size(); i++) {
                std::cout << value_to_string(args[i]);
                if (i < args.size() - 1) std::cout << " ";
            }
            std::cout << "\n";
            return {};
        };

        builtins["print"] = [this](std::vector<Value> args) -> std::vector<Value> {
            for (size_t i = 0; i < args.size(); i++) {
                std::cout << value_to_string(args[i]);
                if (i < args.size() - 1) std::cout << " ";
            }
            return {};
        };

        builtins["len"] = [this](std::vector<Value> args) -> std::vector<Value> {
            if (args.empty()) return {int64_t(0)};
            return {int64_t(1)};
        };

        // Math functions
        builtins["abs"] = [](std::vector<Value> args) -> std::vector<Value> {
            if (args.empty()) return {int64_t(0)};
            const auto& val = args[0];
            if (std::holds_alternative<int64_t>(val)) {
                return {std::llabs(std::get<int64_t>(val))};
            } else if (std::holds_alternative<double>(val)) {
                return {std::fabs(std::get<double>(val))};
            }
            return {int64_t(0)};
        };

        builtins["min"] = [](std::vector<Value> args) -> std::vector<Value> {
            if (args.size() < 2) return {int64_t(0)};
            int64_t a = std::get<int64_t>(args[0]);
            int64_t b = std::get<int64_t>(args[1]);
            return {std::min(a, b)};
        };

        builtins["max"] = [](std::vector<Value> args) -> std::vector<Value> {
            if (args.size() < 2) return {int64_t(0)};
            int64_t a = std::get<int64_t>(args[0]);
            int64_t b = std::get<int64_t>(args[1]);
            return {std::max(a, b)};
        };

        builtins["sqrt"] = [](std::vector<Value> args) -> std::vector<Value> {
            if (args.empty()) return {int64_t(0)};
            const auto& val = args[0];
            if (std::holds_alternative<int64_t>(val)) {
                return {std::sqrt(static_cast<double>(std::get<int64_t>(val)))};
            } else if (std::holds_alternative<double>(val)) {
                return {std::sqrt(std::get<double>(val))};
            }
            return {int64_t(0)};
        };

        builtins["pow"] = [](std::vector<Value> args) -> std::vector<Value> {
            if (args.size() < 2) return {int64_t(0)};
            double base = std::get<int64_t>(args[0]);
            double exp = std::get<int64_t>(args[1]);
            return {std::pow(base, exp)};
        };
        
        // ========================================
        // Tensor built-in functions
        // ========================================
        
        // tensor::zeros<f32>([m, n]) - create zero tensor
        builtins["tensor_zeros"] = [this](std::vector<Value> args) -> std::vector<Value> {
            // Args: [dim1, dim2, ...] or [element_type, dim1, dim2, ...]
            std::vector<int64_t> dims;
            size_t start_idx = 0;
            std::string elem_type = "f32";
            
            if (!args.empty() && std::holds_alternative<std::string>(args[0])) {
                elem_type = std::get<std::string>(args[0]);
                start_idx = 1;
            }
            
            for (size_t i = start_idx; i < args.size(); i++) {
                if (std::holds_alternative<int64_t>(args[i])) {
                    dims.push_back(std::get<int64_t>(args[i]));
                }
            }
            
            if (dims.empty()) dims = {1, 1};
            Tensor t = zeros(elem_type, dims);
            
            // Return shape as array for now
            std::vector<Value> result;
            for (int64_t d : t->shape.dims) {
                result.push_back(d);
            }
            return result;
        };
        
        // tensor::ones<f32>([m, n]) - create ones tensor
        builtins["tensor_ones"] = [this](std::vector<Value> args) -> std::vector<Value> {
            std::vector<int64_t> dims;
            size_t start_idx = 0;
            std::string elem_type = "f32";
            
            if (!args.empty() && std::holds_alternative<std::string>(args[0])) {
                elem_type = std::get<std::string>(args[0]);
                start_idx = 1;
            }
            
            for (size_t i = start_idx; i < args.size(); i++) {
                if (std::holds_alternative<int64_t>(args[i])) {
                    dims.push_back(std::get<int64_t>(args[i]));
                }
            }
            
            if (dims.empty()) dims = {1, 1};
            Tensor t = ones(elem_type, dims);
            
            std::vector<Value> result;
            for (int64_t d : t->shape.dims) {
                result.push_back(d);
            }
            return result;
        };
        
        // tensor::random<f32>([m, n]) - create random tensor
        builtins["tensor_random"] = [this](std::vector<Value> args) -> std::vector<Value> {
            std::vector<int64_t> dims;
            size_t start_idx = 0;
            std::string elem_type = "f32";
            
            if (!args.empty() && std::holds_alternative<std::string>(args[0])) {
                elem_type = std::get<std::string>(args[0]);
                start_idx = 1;
            }
            
            for (size_t i = start_idx; i < args.size(); i++) {
                if (std::holds_alternative<int64_t>(args[i])) {
                    dims.push_back(std::get<int64_t>(args[i]));
                }
            }
            
            if (dims.empty()) dims = {1, 1};
            Tensor t = random_tensor(elem_type, dims);
            
            std::vector<Value> result;
            for (int64_t d : t->shape.dims) {
                result.push_back(d);
            }
            return result;
        };
        
        // tensor::matmul(A, B) - matrix multiplication
        builtins["tensor_matmul"] = [this](std::vector<Value> args) -> std::vector<Value> {
            // For now, compute simple matmul if dims provided as args
            // Args: [M, K, K, N] or simulate
            if (args.size() >= 4) {
                int64_t M = std::get<int64_t>(args[0]);
                int64_t K = std::get<int64_t>(args[1]);
                int64_t N = std::get<int64_t>(args[3]);
                
                Tensor A = random_tensor("f32", {M, K});
                Tensor B = random_tensor("f32", {K, N});
                Tensor C = matmul(A, B);
                
                std::vector<Value> result;
                result.push_back(int64_t(C->shape.dims[0]));
                result.push_back(int64_t(C->shape.dims[1]));
                return result;
            }
            return {int64_t(0)};
        };
        
        // tensor::relu(x) - ReLU activation
        builtins["tensor_relu"] = [](std::vector<Value> args) -> std::vector<Value> {
            if (args.empty()) return {int64_t(0)};
            if (std::holds_alternative<double>(args[0])) {
                double val = std::get<double>(args[0]);
                return {std::max(0.0, val)};
            } else if (std::holds_alternative<int64_t>(args[0])) {
                int64_t val = std::get<int64_t>(args[0]);
                return {std::max<int64_t>(0, val)};
            }
            return {int64_t(0)};
        };
        
        // tensor::sigmoid(x) - sigmoid activation
        builtins["tensor_sigmoid"] = [](std::vector<Value> args) -> std::vector<Value> {
            if (args.empty()) return {double(0.0)};
            double val = 0.0;
            if (std::holds_alternative<double>(args[0])) {
                val = std::get<double>(args[0]);
            } else if (std::holds_alternative<int64_t>(args[0])) {
                val = static_cast<double>(std::get<int64_t>(args[0]));
            }
            return {1.0 / (1.0 + std::exp(-val))};
        };
        
        // tensor::softmax(x, dim) - softmax activation
        builtins["tensor_softmax"] = [](std::vector<Value> args) -> std::vector<Value> {
            if (args.empty()) return {double(0.0)};
            double val = 0.0;
            if (std::holds_alternative<double>(args[0])) {
                val = std::get<double>(args[0]);
            } else if (std::holds_alternative<int64_t>(args[0])) {
                val = static_cast<double>(std::get<int64_t>(args[0]));
            }
            return {double(1.0)};
        };
        
        // tensor::shape(tensor) - get tensor shape
        builtins["tensor_shape"] = [](std::vector<Value> args) -> std::vector<Value> {
            if (args.empty()) return {int64_t(0)};
            return {int64_t(1), int64_t(1)};
        };
        
        // tensor::reshape(tensor, new_dims) - reshape tensor
        builtins["tensor_reshape"] = [](std::vector<Value> args) -> std::vector<Value> {
            return {int64_t(1), int64_t(1)};
        };
        
        // tensor::transpose(tensor) - transpose tensor
        builtins["tensor_transpose"] = [](std::vector<Value> args) -> std::vector<Value> {
            return {int64_t(1), int64_t(1)};
        };
        
        // tensor::abs(tensor) - element-wise absolute value
        builtins["tensor_abs"] = [](std::vector<Value> args) -> std::vector<Value> {
            if (args.empty()) return {int64_t(0)};
            if (std::holds_alternative<double>(args[0])) {
                return {std::fabs(std::get<double>(args[0]))};
            } else if (std::holds_alternative<int64_t>(args[0])) {
                return {std::llabs(std::get<int64_t>(args[0]))};
            }
            return {int64_t(0)};
        };
        
        // tensor::sum(tensor) - sum all elements
        builtins["tensor_sum"] = [](std::vector<Value> args) -> std::vector<Value> {
            if (args.empty()) return {int64_t(0)};
            if (std::holds_alternative<double>(args[0])) {
                return {std::get<double>(args[0])};
            } else if (std::holds_alternative<int64_t>(args[0])) {
                return {std::get<int64_t>(args[0])};
            }
            return {int64_t(0)};
        };
    }

    // Convert value to string
    std::string value_to_string(const Value& v) {
        return std::visit([](auto&& val) -> std::string {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return "null";
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return std::to_string(val);
            } else if constexpr (std::is_same_v<T, double>) {
                return std::to_string(val);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return val;
            } else if constexpr (std::is_same_v<T, bool>) {
                return val ? "true" : "false";
            } else if constexpr (std::is_same_v<T, char>) {
                return std::string(1, val);
            }
            return "<?>";
        }, v);
    }

    // Set variable
    void set_var(const std::string& name, const RuntimeValue& value) {
        variables[name] = value;
    }

    // Get variable
    RuntimeValue* get_var(const std::string& name) {
        auto it = variables.find(name);
        if (it != variables.end()) {
            return &it->second;
        }
        return nullptr;
    }
    
    // ========================================
    // Tensor operations
    // ========================================
    
    // Create a tensor with given shape and type
    Tensor create_tensor(const std::string& elem_type, const std::vector<int64_t>& dims) {
        auto tensor = std::make_shared<TensorValue>();
        tensor->element_type = elem_type;
        tensor->shape.dims = dims;
        int64_t total = tensor->shape.total_size();
        
        if (elem_type == "f32" || elem_type == "f64") {
            tensor->data.resize(total, 0.0);
        } else {
            tensor->int_data.resize(total, 0);
        }
        return tensor;
    }
    
    // Create zero-initialized tensor
    Tensor zeros(const std::string& elem_type, const std::vector<int64_t>& dims) {
        return create_tensor(elem_type, dims);
    }
    
    // Create ones tensor
    Tensor ones(const std::string& elem_type, const std::vector<int64_t>& dims) {
        auto t = create_tensor(elem_type, dims);
        int64_t total = t->shape.total_size();
        if (t->is_integer()) {
            for (int64_t i = 0; i < total; i++) t->int_data[i] = 1;
        } else {
            for (int64_t i = 0; i < total; i++) t->data[i] = 1.0;
        }
        return t;
    }
    
    // Create random tensor
    Tensor random_tensor(const std::string& elem_type, const std::vector<int64_t>& dims) {
        auto t = create_tensor(elem_type, dims);
        int64_t total = t->shape.total_size();
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(0.0, 1.0);
        
        if (t->is_integer()) {
            std::uniform_int_distribution<> dis_int(0, 100);
            for (int64_t i = 0; i < total; i++) t->int_data[i] = dis_int(gen);
        } else {
            for (int64_t i = 0; i < total; i++) t->data[i] = dis(gen);
        }
        return t;
    }
    
    // Matrix multiplication: A [M, K] * B [K, N] = C [M, N]
    Tensor matmul(const Tensor& A, const Tensor& B) {
        if (A->shape.dims.size() != 2 || B->shape.dims.size() != 2) return nullptr;
        int64_t M = A->shape.dims[0];
        int64_t K = A->shape.dims[1];
        int64_t N = B->shape.dims[1];
        
        if (B->shape.dims[0] != K) return nullptr;  // Dimension mismatch
        
        auto C = create_tensor(A->element_type, {M, N});
        
        for (int64_t i = 0; i < M; i++) {
            for (int64_t j = 0; j < N; j++) {
                double sum = 0.0;
                for (int64_t k = 0; k < K; k++) {
                    double a = A->get_f64({i, k});
                    double b = B->get_f64({k, j});
                    sum += a * b;
                }
                C->set_f64({i, j}, sum);
            }
        }
        return C;
    }
    
    // Element-wise operations
    Tensor element_wise_op(const Tensor& A, const Tensor& B, const std::string& op) {
        if (A->shape.dims != B->shape.dims) return nullptr;
        
        auto C = create_tensor(A->element_type, A->shape.dims);
        int64_t total = A->shape.total_size();
        
        for (int64_t i = 0; i < total; i++) {
            std::vector<int64_t> idx = linear_to_indices(i, A->shape.dims);
            double a = A->get_f64(idx);
            double b = B->get_f64(idx);
            double c = 0.0;
            
            if (op == "add") c = a + b;
            else if (op == "sub") c = a - b;
            else if (op == "mul") c = a * b;
            else if (op == "div") c = (b != 0) ? a / b : 0;
            
            C->set_f64(idx, c);
        }
        return C;
    }
    
    // ReLU activation
    Tensor relu(const Tensor& A) {
        auto C = create_tensor(A->element_type, A->shape.dims);
        int64_t total = A->shape.total_size();
        
        for (int64_t i = 0; i < total; i++) {
            std::vector<int64_t> idx = linear_to_indices(i, A->shape.dims);
            double val = A->get_f64(idx);
            C->set_f64(idx, std::max(0.0, val));
        }
        return C;
    }
    
    // Helper: linear index to multi-dimensional indices
    std::vector<int64_t> linear_to_indices(int64_t linear_idx, const std::vector<int64_t>& dims) {
        std::vector<int64_t> idx(dims.size());
        int64_t remaining = linear_idx;
        for (size_t d = 0; d < dims.size(); d++) {
            int64_t stride = 1;
            for (size_t k = d + 1; k < dims.size(); k++) stride *= dims[k];
            idx[d] = remaining / stride;
            remaining %= stride;
        }
        return idx;
    }
};

// Helper: safe get Value from const reference
inline Value copy_value(const claw::ast::LiteralExpr::Value& v) {
    return std::visit([](auto&& val) -> Value {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return Value{};
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return Value{val};
        } else if constexpr (std::is_same_v<T, double>) {
            return Value{val};
        } else if constexpr (std::is_same_v<T, std::string>) {
            return Value{val};
        } else if constexpr (std::is_same_v<T, bool>) {
            return Value{val};
        } else if constexpr (std::is_same_v<T, char>) {
            return Value{val};
        }
        return Value{};
    }, v);
}

// ============================================================================
// Event System Runtime - For Publish/Subscribe/SerialProcess
// ============================================================================

using EventHandler = std::function<void(std::vector<Value>)>;

struct EventSubscription {
    std::string event_name;
    EventHandler handler;
    claw::ast::ASTNode* handler_body = nullptr;
};

class EventSystem {
public:
    // Event handlers mapped by event name
    std::map<std::string, std::vector<EventSubscription>> events;
    
    // Serial processes (named event handlers)
    std::map<std::string, claw::ast::ASTNode*> serial_processes;
    
    // Subscribe to an event
    void subscribe(const std::string& event_name, EventHandler handler, 
                   claw::ast::ASTNode* body = nullptr) {
        EventSubscription sub;
        sub.event_name = event_name;
        sub.handler = handler;
        sub.handler_body = body;
        events[event_name].push_back(sub);
    }
    
    // Publish an event with arguments
    void publish(const std::string& event_name, std::vector<Value> args) {
        auto it = events.find(event_name);
        if (it != events.end()) {
            for (auto& sub : it->second) {
                if (sub.handler) {
                    sub.handler(args);
                }
            }
        }
    }
    
    // Register a serial process
    void register_process(const std::string& name, claw::ast::ASTNode* body) {
        serial_processes[name] = body;
    }
    
    // Get process by name
    claw::ast::ASTNode* get_process(const std::string& name) {
        auto it = serial_processes.find(name);
        if (it != serial_processes.end()) {
            return it->second;
        }
        return nullptr;
    }
    
    // Print event system status
    void print_status() {
        std::cout << "=== Event System Status ===\n";
        std::cout << "Registered events: " << events.size() << "\n";
        for (auto& [name, subs] : events) {
            std::cout << "  - " << name << ": " << subs.size() << " handlers\n";
        }
        std::cout << "Serial processes: " << serial_processes.size() << "\n";
        for (auto& [name, fn] : serial_processes) {
            std::cout << "  - " << name << "\n";
        }
        std::cout << "============================\n";
    }
};

// Interpreter class
class Interpreter {
public:
    Runtime runtime;
    EventSystem event_system;
    bool break_flag = false;
    bool continue_flag = false;
    Value return_value;
    claw::ast::Program* current_program = nullptr;

    // Variable stack for function calls
    std::vector<std::map<std::string, RuntimeValue>> variable_stack;

    Interpreter() {}

    // Execute a program
    void execute(claw::ast::Program* program) {
        current_program = program;  // Store program reference

        // First pass: find serial processes and register them
        for (const auto& decl : program->get_declarations()) {
            if (decl->get_kind() == claw::ast::Statement::Kind::SerialProcess) {
                auto* proc = static_cast<claw::ast::SerialProcessStmt*>(decl.get());
                std::string proc_name = proc->get_name();
                claw::ast::ASTNode* proc_body = proc->get_body();
                if (proc_body) {
                    event_system.register_process(proc_name, proc_body);
                }
            }
        }
        
        // First pass: find main function
        claw::ast::FunctionStmt* main_fn = nullptr;

        for (const auto& decl : program->get_declarations()) {
            if (decl->get_kind() == claw::ast::Statement::Kind::Function) {
                auto* fn = static_cast<claw::ast::FunctionStmt*>(decl.get());
                if (fn->get_name() == "main") {
                    main_fn = fn;
                    break;
                }
            }
        }

        // Execute main if found
        if (main_fn) {
            execute_function(main_fn);
        } else {
            std::cerr << "Error: No main function found\n";
        }
    }
    // Execute a function with arguments
    Value execute_function(claw::ast::FunctionStmt* fn,
                           const std::vector<Value>& args = {}) {
        // Push new variable scope
        variable_stack.push_back(std::map<std::string, RuntimeValue>());

        // Bind parameters
        const auto& params = fn->get_params();
        for (size_t i = 0; i < params.size() && i < args.size(); i++) {
            std::string param_name = params[i].first;
            RuntimeValue param_val;
            param_val.type_name = params[i].second;
            param_val.size = 1;
            param_val.scalar = args[i];
            runtime.set_var(param_name, param_val);
        }

        // Reset return value
        return_value = Value();

        // Execute function body
        if (fn->get_body()) {
            execute_block(fn->get_body());
        }

        // Pop variable scope
        variable_stack.pop_back();

        // Return the return value
        return return_value;
    }

    // Execute a block
    void execute_block(claw::ast::ASTNode* node) {
        if (!node) return;

        auto kind = static_cast<const claw::ast::Statement*>(node)->get_kind();
        if (kind == claw::ast::Statement::Kind::Block) {
            auto* block = static_cast<claw::ast::BlockStmt*>(node);
            for (const auto& stmt : block->get_statements()) {
                execute_statement(stmt.get());
                if (break_flag || continue_flag) break;
            }
        }
    }

    // Execute a statement
    void execute_statement(claw::ast::Statement* stmt) {
        if (!stmt) return;

        auto kind = stmt->get_kind();

        switch (kind) {
            case claw::ast::Statement::Kind::Let: {
                auto* let_stmt = static_cast<claw::ast::LetStmt*>(stmt);
                execute_let(let_stmt);
                break;
            }
            case claw::ast::Statement::Kind::Assign: {
                auto* assign = static_cast<claw::ast::AssignStmt*>(stmt);
                execute_assign(assign);
                break;
            }
            case claw::ast::Statement::Kind::Expression: {
                auto* expr_stmt = static_cast<claw::ast::ExprStmt*>(stmt);
                auto* expr = expr_stmt->get_expr();

                // Check if this is an assignment expression (a[1] = 42)
                if (expr->get_kind() == claw::ast::Expression::Kind::Binary) {
                    auto* bin = static_cast<claw::ast::BinaryExpr*>(expr);
                    if (bin->get_operator() == claw::TokenType::Op_eq_assign) {
                        execute_binary_assignment(bin);
                        break;
                    }
                }

                // Otherwise just evaluate
                evaluate(expr);
                break;
            }
            case claw::ast::Statement::Kind::Block: {
                // Execute block statements
                execute_block(stmt);
                break;
            }
            case claw::ast::Statement::Kind::Break: {
                break_flag = true;
                break;
            }
            case claw::ast::Statement::Kind::Continue: {
                continue_flag = true;
                break;
            }
            case claw::ast::Statement::Kind::If: {
                execute_if(static_cast<claw::ast::IfStmt*>(stmt));
                break;
            }
            case claw::ast::Statement::Kind::For: {
                execute_for(static_cast<claw::ast::ForStmt*>(stmt));
                break;
            }
            case claw::ast::Statement::Kind::While: {
                execute_while(static_cast<claw::ast::WhileStmt*>(stmt));
                break;
            }
            case claw::ast::Statement::Kind::Return: {
                auto* ret = static_cast<claw::ast::ReturnStmt*>(stmt);
                if (ret->get_value()) {
                    return_value = evaluate(ret->get_value());
                } else {
                    return_value = Value();
                }
                break;
            }
            case claw::ast::Statement::Kind::Publish: {
                // Event system - publish event with arguments
                auto* pub = static_cast<claw::ast::PublishStmt*>(stmt);
                std::string event_name = pub->get_event_name();
                
                // Evaluate arguments
                std::vector<Value> args;
                for (auto& arg : pub->get_arguments()) {
                    args.push_back(evaluate(arg.get()));
                }
                
                // Publish the event
                event_system.publish(event_name, args);
                break;
            }
            case claw::ast::Statement::Kind::Subscribe: {
                // Event system - subscribe to event
                auto* sub = static_cast<claw::ast::SubscribeStmt*>(stmt);
                std::string event_name = sub->get_event_name();
                claw::ast::FunctionStmt* handler_fn = sub->get_handler();
                
                if (handler_fn) {
                    // Create handler that executes the function
                    EventHandler handler = [this, handler_fn](std::vector<Value> args) {
                        execute_function(handler_fn, args);
                    };
                    event_system.subscribe(event_name, handler, handler_fn);
                }
                break;
            }
            case claw::ast::Statement::Kind::SerialProcess: {
                // Event system - register serial process
                auto* proc = static_cast<claw::ast::SerialProcessStmt*>(stmt);
                std::string proc_name = proc->get_name();
                claw::ast::ASTNode* proc_body = proc->get_body();
                
                if (proc_body) {
                    event_system.register_process(proc_name, proc_body);
                }
                break;
            }
            default:
                break;
        }
    }

    // Execute let statement
    void execute_let(claw::ast::LetStmt* let) {
        std::string name = let->get_name();
        std::string type_str = let->get_type();

        RuntimeValue val;
        val.type_name = type_str;

        // Parse array size from type like "u32[1]"
        size_t bracket_pos = type_str.find('[');
        if (bracket_pos != std::string::npos) {
            size_t end_bracket = type_str.find(']', bracket_pos);
            if (end_bracket != std::string::npos) {
                std::string size_str = type_str.substr(bracket_pos + 1,
                                                       end_bracket - bracket_pos - 1);
                val.size = std::stoll(size_str);
            }
            val.type_name = type_str.substr(0, bracket_pos);
        } else {
            val.size = 1;
        }

        // Initialize array
        if (val.size > 1) {
            val.array.resize(val.size);
        }

        // Evaluate initializer if present
        if (let->get_initializer()) {
            Value init_val = evaluate(let->get_initializer());
            if (val.size == 1) {
                val.scalar = init_val;
            } else if (!val.array.empty()) {
                val.array[0].scalar = init_val;
            }
        }

        runtime.set_var(name, val);
    }

    // Execute binary assignment (a[1] = 42)
    void execute_binary_assignment(claw::ast::BinaryExpr* bin) {
        auto* target = bin->get_left();
        Value value = evaluate(bin->get_right());

        if (target->get_kind() == claw::ast::Expression::Kind::Index) {
            auto* idx_expr = static_cast<claw::ast::IndexExpr*>(target);
            auto* obj = idx_expr->get_object();
            auto* idx_expr_p = idx_expr->get_index();

            if (obj->get_kind() == claw::ast::Expression::Kind::Identifier) {
                auto* ident = static_cast<claw::ast::IdentifierExpr*>(obj);
                RuntimeValue* var = runtime.get_var(ident->get_name());

                if (var) {
                    Value idx_val = evaluate(idx_expr_p);
                    int64_t idx = std::get<int64_t>(idx_val);

                    if (var->size == 1) {
                        var->scalar = value;
                    } else if (idx >= 1 && idx <= static_cast<int64_t>(var->array.size())) {
                        var->array[idx - 1].scalar = value;
                    }
                }
            }
        }
    }

    // Execute assignment
    void execute_assign(claw::ast::AssignStmt* assign) {
        // Get target (should be index expression like a[1])
        claw::ast::Expression* target = assign->get_target();
        Value value = evaluate(assign->get_value());

        if (target->get_kind() == claw::ast::Expression::Kind::Index) {
            auto* index_expr = static_cast<claw::ast::IndexExpr*>(target);
            claw::ast::Expression* obj = index_expr->get_object();
            claw::ast::Expression* idx_expr = index_expr->get_index();

            if (obj->get_kind() == claw::ast::Expression::Kind::Identifier) {
                auto* ident = static_cast<claw::ast::IdentifierExpr*>(obj);
                RuntimeValue* var = runtime.get_var(ident->get_name());

                if (var) {
                    // Get index value
                    Value idx_val = evaluate(idx_expr);
                    int64_t idx = std::get<int64_t>(idx_val);

                    // Set value at index
                    if (var->size == 1) {
                        var->scalar = value;
                    } else if (idx >= 1 && idx <= static_cast<int64_t>(var->array.size())) {
                        var->array[idx - 1].scalar = value;
                    }
                }
            }
        }
    }

    // Execute if statement
    void execute_if(claw::ast::IfStmt* if_stmt) {
        const auto& conditions = if_stmt->get_conditions();
        const auto& bodies = if_stmt->get_bodies();

        if (conditions.empty()) return;

        // Check each condition in order
        for (size_t i = 0; i < conditions.size(); i++) {
            Value cond = evaluate(conditions[i].get());
            bool cond_bool = value_to_bool(cond);

            if (cond_bool) {
                execute_block(bodies[i].get());
                return;  // Exit after matching branch
            }
        }

        // If no condition matched, execute else body if present
        if (if_stmt->get_else_body()) {
            execute_block(if_stmt->get_else_body());
        }
    }

    // Execute for statement
    void execute_for(claw::ast::ForStmt* for_stmt) {
        std::string var_name = for_stmt->get_variable();
        claw::ast::Expression* iterable = for_stmt->get_iterable();

        // Check if iterable is a range expression (1..10)
        if (iterable->get_kind() == claw::ast::Expression::Kind::Binary) {
            auto* bin = static_cast<claw::ast::BinaryExpr*>(iterable);
            if (bin->get_operator() == TokenType::Dot) {
                // It's a range: start..end
                Value start_val = evaluate(bin->get_left());
                Value end_val = evaluate(bin->get_right());

                if (!std::holds_alternative<int64_t>(start_val) ||
                    !std::holds_alternative<int64_t>(end_val)) {
                    std::cerr << "Error: Range bounds must be integers\n";
                    return;
                }

                int64_t start = std::get<int64_t>(start_val);
                int64_t end = std::get<int64_t>(end_val);

                // 1-based range in Claw: 1..10 includes 1 through 10
                for (int64_t i = start; i <= end; i++) {
                    // Create loop variable
                    RuntimeValue loop_var;
                    loop_var.type_name = "u32";
                    loop_var.size = 1;
                    loop_var.scalar = static_cast<int64_t>(i);
                    runtime.set_var(var_name, loop_var);

                    // Execute loop body
                    execute_block(for_stmt->get_body());

                    if (break_flag) {
                        break_flag = false;
                        break;
                    }
                    if (continue_flag) {
                        continue_flag = false;
                        continue;
                    }
                }
                return;
            }
        }

        // Support array/tensor iteration
        Value arr_val = evaluate(iterable);
        
        // RuntimeValue is not in variant, handle iteration differently
        // Check if it's a scalar value that can be iterated
            
            if (arr.size > 1 && !arr.array.empty()) {
                // It's an array: iterate over elements
                for (size_t idx = 0; idx < arr.array.size(); idx++) {
                    RuntimeValue loop_var = arr.array[idx];
                    // Convert 0-based to 1-based index for Claw semantics
                    loop_var.scalar = static_cast<int64_t>(idx + 1);
                    runtime.set_var(var_name, loop_var);
                    
                    execute_block(for_stmt->get_body());
                    
                    if (break_flag) {
                        break_flag = false;
                        break;
                    }
                    if (continue_flag) {
                        continue_flag = false;
                        continue;
                    }
                }
                return;
            } else if (arr.size == 1) {
                // Single element: iterate once
                RuntimeValue loop_var = arr;
                loop_var.scalar = 1;
                runtime.set_var(var_name, loop_var);
                execute_block(for_stmt->get_body());
                return;
            }
        }
        
        // Handle vector iteration (std::vector<Value>)
        if (std::holds_alternative<std::vector<Value>>(arr_val)) {
            auto vec = std::get<std::vector<Value>>(arr_val);
            for (size_t idx = 0; idx < vec.size(); idx++) {
                const auto& elem = vec[idx];
                RuntimeValue loop_var;
                loop_var.type_name = "unknown";
                loop_var.size = 1;
                
                // Extract scalar from variant
                if (std::holds_alternative<int64_t>(elem)) {
                    loop_var.scalar = std::get<int64_t>(elem);
                } else if (std::holds_alternative<double>(elem)) {
                    loop_var.scalar = static_cast<int64_t>(std::get<double>(elem));
                } else if (std::holds_alternative<bool>(elem)) {
                    loop_var.scalar = std::get<bool>(elem) ? 1 : 0;
                } else {
                    loop_var.scalar = 0;
                }
                
                runtime.set_var(var_name, loop_var);
                execute_block(for_stmt->get_body());
                
                if (break_flag) {
                    break_flag = false;
                    break;
                }
                if (continue_flag) {
                    continue_flag = false;
                    continue;
                }
            }
            return;
        }
        
        // Handle string iteration (character by character)
        if (std::holds_alternative<std::string>(arr_val)) {
            auto str = std::get<std::string>(arr_val);
            for (size_t idx = 0; idx < str.size(); idx++) {
                RuntimeValue loop_var;
                loop_var.type_name = "char";
                loop_var.size = 1;
                loop_var.scalar = static_cast<int64_t>(str[idx]);
                runtime.set_var(var_name, loop_var);
                
                execute_block(for_stmt->get_body());
                
                if (break_flag) {
                    break_flag = false;
                    break;
                }
                if (continue_flag) {
                    continue_flag = false;
                    continue;
                }
            }
            return;
        }
        
        std::cerr << "Error: Unsupported for loop iterable type\n";
    }

    // Execute while statement
    void execute_while(claw::ast::WhileStmt* while_stmt) {
        auto* condition = while_stmt->get_condition();

        while (true) {
            Value cond = evaluate(condition);
            bool cond_bool = value_to_bool(cond);

            if (!cond_bool) {
                break;
            }

            // Execute loop body
            execute_block(while_stmt->get_body());

            if (break_flag) {
                break_flag = false;
                break;
            }
            if (continue_flag) {
                continue_flag = false;
                continue;
            }
        }
    }

    // Convert value to bool
    bool value_to_bool(const Value& v) {
        return std::visit([](auto&& val) -> bool {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, bool>) {
                return val;
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return val != 0;
            } else if constexpr (std::is_same_v<T, double>) {
                return val != 0.0;
            } else if constexpr (std::is_same_v<T, std::string>) {
                return !val.empty();
            }
            return false;
        }, v);
    }

    // Evaluate expression and return value
    Value evaluate(claw::ast::Expression* expr) {
        if (!expr) return Value();

        auto kind = expr->get_kind();

        switch (kind) {
            case claw::ast::Expression::Kind::Literal: {
                auto* lit = static_cast<claw::ast::LiteralExpr*>(expr);
                return copy_value(lit->get_value());
            }
            case claw::ast::Expression::Kind::Identifier: {
                auto* ident = static_cast<claw::ast::IdentifierExpr*>(expr);
                RuntimeValue* var = runtime.get_var(ident->get_name());
                if (var) {
                    return var->at(1);  // Return scalar (index 1)
                }
                return Value();
            }
            case claw::ast::Expression::Kind::Binary: {
                return evaluate_binary(static_cast<claw::ast::BinaryExpr*>(expr));
            }
            case claw::ast::Expression::Kind::Unary: {
                return evaluate_unary(static_cast<claw::ast::UnaryExpr*>(expr));
            }
            case claw::ast::Expression::Kind::Index: {
                return evaluate_index(static_cast<claw::ast::IndexExpr*>(expr));
            }
            case claw::ast::Expression::Kind::Call: {
                return evaluate_call(static_cast<claw::ast::CallExpr*>(expr));
            }
            default:
                return Value();
        }
    }

    // Evaluate binary expression
    Value evaluate_binary(claw::ast::BinaryExpr* bin) {
        Value left_val = evaluate(bin->get_left());
        Value right_val = evaluate(bin->get_right());

        TokenType op = bin->get_operator();

        // Handle string concatenation
        if (op == TokenType::Op_plus &&
            std::holds_alternative<std::string>(left_val)) {
            return runtime.value_to_string(left_val) + runtime.value_to_string(right_val);
        }

        // Try int64_t first
        if (std::holds_alternative<int64_t>(left_val) &&
            std::holds_alternative<int64_t>(right_val)) {
            int64_t l = std::get<int64_t>(left_val);
            int64_t r = std::get<int64_t>(right_val);

            switch (op) {
                case TokenType::Op_plus: return l + r;
                case TokenType::Op_minus: return l - r;
                case TokenType::Op_star: return l * r;
                case TokenType::Op_slash: return r != 0 ? l / r : int64_t(0);
                case TokenType::Op_percent: return r != 0 ? l % r : int64_t(0);
                case TokenType::Op_eq: return l == r;
                case TokenType::Op_neq: return l != r;
                case TokenType::Op_lt: return l < r;
                case TokenType::Op_gt: return l > r;
                case TokenType::Op_lte: return l <= r;
                case TokenType::Op_gte: return l >= r;
                default: return Value();
            }
        }

        // Handle mixed int/double
        double l = value_to_double(left_val);
        double r = value_to_double(right_val);

        switch (op) {
            case TokenType::Op_plus: return int64_t(l + r);
            case TokenType::Op_minus: return int64_t(l - r);
            case TokenType::Op_star: return int64_t(l * r);
            case TokenType::Op_slash: return r != 0 ? int64_t(l / r) : int64_t(0);
            case TokenType::Op_percent: return int64_t(fmod(l, r));
            case TokenType::Op_eq: return l == r;
            case TokenType::Op_neq: return l != r;
            case TokenType::Op_lt: return l < r;
            case TokenType::Op_gt: return l > r;
            case TokenType::Op_lte: return l <= r;
            case TokenType::Op_gte: return l >= r;
            default: return Value();
        }
    }

    // Evaluate unary expression
    Value evaluate_unary(claw::ast::UnaryExpr* un) {
        Value operand = evaluate(un->get_operand());
        TokenType op = un->get_operator();

        switch (op) {
            case TokenType::Op_minus:
                if (std::holds_alternative<int64_t>(operand)) {
                    return -std::get<int64_t>(operand);
                } else if (std::holds_alternative<double>(operand)) {
                    return -std::get<double>(operand);
                }
                break;
            case TokenType::Op_bang:
                if (std::holds_alternative<bool>(operand)) {
                    return !std::get<bool>(operand);
                }
                break;
            default:
                break;
        }

        return Value();
    }

    // Evaluate index expression (array access)
    Value evaluate_index(claw::ast::IndexExpr* index) {
        claw::ast::Expression* obj = index->get_object();
        claw::ast::Expression* idx_expr = index->get_index();

        if (obj->get_kind() == claw::ast::Expression::Kind::Identifier) {
            auto* ident = static_cast<claw::ast::IdentifierExpr*>(obj);
            RuntimeValue* var = runtime.get_var(ident->get_name());

            if (var) {
                // Get index
                Value idx_val = evaluate(idx_expr);
                if (std::holds_alternative<int64_t>(idx_val)) {
                    int64_t idx = std::get<int64_t>(idx_val);
                    return var->at(idx);
                }
            }
        }

        return Value();
    }

    // Evaluate function call
    Value evaluate_call(claw::ast::CallExpr* call) {
        claw::ast::Expression* callee = call->get_callee();

        if (callee->get_kind() == claw::ast::Expression::Kind::Identifier) {
            auto* ident = static_cast<claw::ast::IdentifierExpr*>(callee);
            std::string func_name = ident->get_name();

            // Check built-in functions first
            auto it = runtime.builtins.find(func_name);
            if (it != runtime.builtins.end()) {
                // Evaluate arguments
                std::vector<Value> args;
                for (const auto& arg : call->get_arguments()) {
                    args.push_back(evaluate(arg.get()));
                }

                // Call builtin
                auto result = it->second(args);
                return result.empty() ? Value() : result[0];
            }

            // Check user-defined functions
            if (current_program) {
                for (const auto& decl : current_program->get_declarations()) {
                    if (decl->get_kind() == claw::ast::Statement::Kind::Function) {
                        auto* fn = static_cast<claw::ast::FunctionStmt*>(decl.get());
                        if (fn->get_name() == func_name) {
                            // Evaluate arguments
                            std::vector<Value> args;
                            for (const auto& arg : call->get_arguments()) {
                                args.push_back(evaluate(arg.get()));
                            }

                            // Call function
                            return execute_function(fn, args);
                        }
                    }
                }
            }
        }

        return Value();
    }

    // Helper: convert value to double
    double value_to_double(const Value& v) {
        return std::visit([](auto&& val) -> double {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, int64_t>) return static_cast<double>(val);
            else if constexpr (std::is_same_v<T, double>) return val;
            else if constexpr (std::is_same_v<T, bool>) return val ? 1.0 : 0.0;
            return 0.0;
        }, v);
    }
};

} // namespace interpreter
} // namespace claw

#endif // CLAW_INTERPRETER_H
