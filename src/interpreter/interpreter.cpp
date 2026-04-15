// Claw Interpreter Implementation - Direct AST Execution
// Supports: variables, arrays, tensors, functions, expressions, control flow

#include "interpreter/interpreter.h"
#include <stdexcept>
#include <sstream>

namespace claw {
namespace interpreter {

// ============================================================================
// Runtime Value Operations
// ============================================================================

std::string value_to_string(const Value& val) {
    if (std::holds_alternative<std::monostate>(val)) return "null";
    if (std::holds_alternative<int64_t>(val)) return std::to_string(std::get<int64_t>(val));
    if (std::holds_alternative<double>(val)) {
        double d = std::get<double>(val);
        if (std::isnan(d)) return "nan";
        if (std::isinf(d)) return d > 0 ? "inf" : "-inf";
        // Remove trailing zeros
        std::string s = std::to_string(d);
        if (s.find('.') != std::string::npos) {
            s = s.erase(s.find_last_not_of('0') + 1);
            if (s.back() == '.') s.pop_back();
        }
        return s;
    }
    if (std::holds_alternative<std::string>(val)) return std::get<std::string>(val);
    if (std::holds_alternative<bool>(val)) return std::get<bool>(val) ? "true" : "false";
    if (std::holds_alternative<Tensor>(val)) {
        auto t = std::get<Tensor>(val);
        if (!t) return "tensor(null)";
        std::ostringstream oss;
        oss << "tensor<" << t->element_type << ", [";
        for (size_t i = 0; i < t->shape.dims.size(); i++) {
            if (i > 0) oss << ", ";
            oss << t->shape.dims[i];
        }
        oss << "]>";
        return oss.str();
    }
    return "unknown";
}

Value parse_number(const std::string& s) {
    try {
        size_t pos;
        int64_t i = std::stoll(s, &pos);
        if (pos == s.length()) return i;
    } catch (...) {}
    try {
        double d = std::stod(s);
        return d;
    } catch (...) {}
    return Value();
}

Value binary_op(const std::string& op, const Value& a, const Value& b) {
    // Handle tensor operations
    if (std::holds_alternative<Tensor>(a) && std::holds_alternative<Tensor>(b)) {
        auto ta = std::get<Tensor>(a);
        auto tb = std::get<Tensor>(b);
        if (!ta || !tb) return Value();
        
        // Tensor element-wise operations
        if (op == "+") return tensor_element_wise(ta, tb, [](double x, double y) { return x + y; });
        if (op == "-") return tensor_element_wise(ta, tb, [](double x, double y) { return x - y; });
        if (op == "*") return tensor_element_wise(ta, tb, [](double x, double y) { return x * y; });
        if (op == "/") return tensor_element_wise(ta, tb, [](double x, double y) { return x / y; });
        if (op == "%") return tensor_element_wise(ta, tb, [](double x, double y) { return std::fmod(x, y); });
    }
    
    // Numeric operations
    if (std::holds_alternative<int64_t>(a) && std::holds_alternative<int64_t>(b)) {
        int64_t ia = std::get<int64_t>(a);
        int64_t ib = std::get<int64_t>(b);
        if (op == "+") return ia + ib;
        if (op == "-") return ia - ib;
        if (op == "*") return ia * ib;
        if (op == "/") return ib != 0 ? ia / ib : int64_t(0);
        if (op == "%") return ib != 0 ? ia % ib : int64_t(0);
        if (op == "==") return ia == ib;
        if (op == "!=") return ia != ib;
        if (op == "<") return ia < ib;
        if (op == ">") return ia > ib;
        if (op == "<=") return ia <= ib;
        if (op == ">=") return ia >= ib;
    }
    
    if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
        double da = std::get<double>(a);
        double db = std::get<double>(b);
        if (op == "+") return da + db;
        if (op == "-") return da - db;
        if (op == "*") return da * db;
        if (op == "/") return db != 0.0 ? da / db : 0.0;
        if (op == "%") return std::fmod(da, db);
        if (op == "==") return da == db;
        if (op == "!=") return da != db;
        if (op == "<") return da < db;
        if (op == ">") return da > db;
        if (op == "<=") return da <= db;
        if (op == ">=") return da >= db;
    }
    
    // Mixed int/double
    if (std::holds_alternative<int64_t>(a) && std::holds_alternative<double>(b)) {
        double da = static_cast<double>(std::get<int64_t>(a));
        double db = std::get<double>(b);
        if (op == "+") return da + db;
        if (op == "-") return da - db;
        if (op == "*") return da * db;
        if (op == "/") return db != 0.0 ? da / db : 0.0;
        if (op == "%") return std::fmod(da, db);
    }
    
    if (std::holds_alternative<double>(a) && std::holds_alternative<int64_t>(b)) {
        double da = std::get<double>(a);
        double db = static_cast<double>(std::get<int64_t>(b));
        if (op == "+") return da + db;
        if (op == "-") return da - db;
        if (op == "*") return da * db;
        if (op == "/") return db != 0.0 ? da / db : 0.0;
        if (op == "%") return std::fmod(da, db);
    }
    
    // String concatenation
    if (std::holds_alternative<std::string>(a) || std::holds_alternative<std::string>(b)) {
        if (op == "+") return value_to_string(a) + value_to_string(b);
    }
    
    // Comparison
    if (op == "==") return value_to_string(a) == value_to_string(b);
    if (op == "!=") return value_to_string(a) != value_to_string(b);
    
    return Value();
}

Value unary_op(const std::string& op, const Value& a) {
    if (std::holds_alternative<int64_t>(a)) {
        int64_t ia = std::get<int64_t>(a);
        if (op == "-") return -ia;
        if (op == "+") return ia;
        if (op == "!") return ia == 0;
    }
    if (std::holds_alternative<double>(a)) {
        double da = std::get<double>(a);
        if (op == "-") return -da;
        if (op == "+") return da;
        if (op == "!") return da == 0.0;
    }
    if (std::holds_alternative<bool>(a)) {
        bool ba = std::get<bool>(a);
        if (op == "!") return !ba;
    }
    return Value();
}

Tensor tensor_element_wise(Tensor a, Tensor b, std::function<double(double, double)> op) {
    if (!a || !b) return nullptr;
    
    // Broadcast shapes
    auto shape = broadcast_shapes(a->shape.dims, b->shape.dims);
    if (shape.empty()) return nullptr;
    
    auto result = std::make_shared<TensorValue>();
    result->element_type = a->element_type;
    result->shape.dims = shape;
    result->data.resize(result->shape.total_size());
    
    // Apply operation
    for (size_t i = 0; i < result->shape.total_size(); i++) {
        std::vector<int64_t> idx = linear_to_indices(i, shape);
        double av = tensor_get(a, idx);
        double bv = tensor_get(b, idx);
        result->data[i] = op(av, bv);
    }
    
    return result;
}

std::vector<int64_t> broadcast_shapes(const std::vector<int64_t>& a, const std::vector<int64_t>& b) {
    size_t na = a.size(), nb = b.size();
    size_t n = std::max(na, nb);
    
    std::vector<int64_t> result(n, 1);
    
    for (size_t i = 0; i < n; i++) {
        int64_t da = (i < n - na) ? 1 : a[na - n + i];
        int64_t db = (i < n - nb) ? 1 : b[nb - n + i];
        if (da == db) {
            result[i] = da;
        } else if (da == 1) {
            result[i] = db;
        } else if (db == 1) {
            result[i] = da;
        } else {
            return {}; // Incompatible
        }
    }
    return result;
}

std::vector<int64_t> linear_to_indices(size_t idx, const std::vector<int64_t>& dims) {
    std::vector<int64_t> indices(dims.size());
    for (size_t i = dims.size(); i > 0; i--) {
        size_t dim_idx = i - 1;
        indices[dim_idx] = idx % dims[dim_idx];
        idx /= dims[dim_idx];
    }
    return indices;
}

double tensor_get(Tensor t, const std::vector<int64_t>& indices) {
    if (!t) return 0.0;
    size_t linear_idx = 0;
    size_t stride = 1;
    for (size_t i = indices.size(); i > 0; i--) {
        size_t idx = i - 1;
        if (idx < t->shape.dims.size()) {
            linear_idx += indices[idx] * stride;
            stride *= t->shape.dims[idx];
        }
    }
    if (linear_idx < t->data.size()) {
        return t->data[linear_idx];
    }
    return 0.0;
}

void tensor_set(Tensor t, const std::vector<int64_t>& indices, double val) {
    if (!t) return;
    size_t linear_idx = 0;
    size_t stride = 1;
    for (size_t i = indices.size(); i > 0; i--) {
        size_t idx = i - 1;
        if (idx < t->shape.dims.size()) {
            linear_idx += indices[idx] * stride;
            stride *= t->shape.dims[idx];
        }
    }
    if (linear_idx < t->data.size()) {
        t->data[linear_idx] = val;
    }
}

// ============================================================================
// Interpreter Implementation
// ============================================================================

Interpreter::Interpreter() {
    setup_builtins();
}

void Interpreter::setup_builtins() {
    // Print functions
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
        const Value& arg = args[0];
        if (std::holds_alternative<std::string>(arg)) {
            return {int64_t(std::get<std::string>(arg).length())};
        }
        if (std::holds_alternative<Tensor>(arg)) {
            auto t = std::get<Tensor>(arg);
            if (t) return {int64_t(t->shape.dims.size())};
        }
        return {int64_t(1)};
    };
    
    // Math functions
    builtins["abs"] = [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {int64_t(0)};
        const Value& val = args[0];
        if (std::holds_alternative<int64_t>(val)) {
            return {std::llabs(std::get<int64_t>(val))};
        } else if (std::holds_alternative<double>(val)) {
            return {std::fabs(std::get<double>(val))};
        }
        return {int64_t(0)};
    };
    
    builtins["min"] = [](std::vector<Value> args) -> std::vector<Value> {
        if (args.size() < 2) return {int64_t(0)};
        const Value& a = args[0];
        const Value& b = args[1];
        if (std::holds_alternative<int64_t>(a) && std::holds_alternative<int64_t>(b)) {
            return {std::min(std::get<int64_t>(a), std::get<int64_t>(b))};
        }
        if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
            return {std::min(std::get<double>(a), std::get<double>(b))};
        }
        return {int64_t(0)};
    };
    
    builtins["max"] = [](std::vector<Value> args) -> std::vector<Value> {
        if (args.size() < 2) return {int64_t(0)};
        const Value& a = args[0];
        const Value& b = args[1];
        if (std::holds_alternative<int64_t>(a) && std::holds_alternative<int64_t>(b)) {
            return {std::max(std::get<int64_t>(a), std::get<int64_t>(b))};
        }
        if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
            return {std::max(std::get<double>(a), std::get<double>(b))};
        }
        return {int64_t(0)};
    };
    
    builtins["sqrt"] = [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {int64_t(0)};
        const Value& val = args[0];
        if (std::holds_alternative<int64_t>(val)) {
            return {std::sqrt(static_cast<double>(std::get<int64_t>(val)))};
        } else if (std::holds_alternative<double>(val)) {
            return {std::sqrt(std::get<double>(val))};
        }
        return {int64_t(0)};
    };
    
    builtins["pow"] = [](std::vector<Value> args) -> std::vector<Value> {
        if (args.size() < 2) return {int64_t(0)};
        const Value& base = args[0];
        const Value& exp = args[1];
        double b = std::holds_alternative<int64_t>(base) ? static_cast<double>(std::get<int64_t>(base)) 
                   : std::holds_alternative<double>(base) ? std::get<double>(base) : 0.0;
        double e = std::holds_alternative<int64_t>(exp) ? static_cast<double>(std::get<int64_t>(exp))
                   : std::holds_alternative<double>(exp) ? std::get<double>(exp) : 0.0;
        return {std::pow(b, e)};
    };
    
    builtins["sin"] = [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {double(0.0)};
        const Value& val = args[0];
        double v = std::holds_alternative<int64_t>(val) ? static_cast<double>(std::get<int64_t>(val))
                   : std::holds_alternative<double>(val) ? std::get<double>(val) : 0.0;
        return {std::sin(v)};
    };
    
    builtins["cos"] = [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {double(0.0)};
        const Value& val = args[0];
        double v = std::holds_alternative<int64_t>(val) ? static_cast<double>(std::get<int64_t>(val))
                   : std::holds_alternative<double>(val) ? std::get<double>(val) : 0.0;
        return {std::cos(v)};
    };
    
    builtins["floor"] = [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {int64_t(0)};
        const Value& val = args[0];
        if (std::holds_alternative<double>(val)) {
            return {int64_t(std::floor(std::get<double>(val)))};
        } else if (std::holds_alternative<int64_t>(val)) {
            return val;
        }
        return {int64_t(0)};
    };
    
    builtins["ceil"] = [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {int64_t(0)};
        const Value& val = args[0];
        if (std::holds_alternative<double>(val)) {
            return {int64_t(std::ceil(std::get<double>(val)))};
        } else if (std::holds_alternative<int64_t>(val)) {
            return val;
        }
        return {int64_t(0)};
    };
    
    builtins["round"] = [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {int64_t(0)};
        const Value& val = args[0];
        if (std::holds_alternative<double>(val)) {
            return {int64_t(std::round(std::get<double>(val)))};
        } else if (std::holds_alternative<int64_t>(val)) {
            return val;
        }
        return {int64_t(0)};
    };
    
    // Type conversion
    builtins["int"] = [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {int64_t(0)};
        const Value& val = args[0];
        if (std::holds_alternative<int64_t>(val)) return val;
        if (std::holds_alternative<double>(val)) return {int64_t(std::get<double>(val))};
        if (std::holds_alternative<std::string>(val)) {
            return parse_number(std::get<std::string>(val));
        }
        if (std::holds_alternative<bool>(val)) return {int64_t(std::get<bool>(val) ? 1 : 0)};
        return {int64_t(0)};
    };
    
    builtins["float"] = [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {double(0.0)};
        const Value& val = args[0];
        if (std::holds_alternative<double>(val)) return val;
        if (std::holds_alternative<int64_t>(val)) return {double(std::get<int64_t>(val))};
        if (std::holds_alternative<std::string>(val)) return parse_number(std::get<std::string>(val));
        if (std::holds_alternative<bool>(val)) return {double(std::get<bool>(val) ? 1.0 : 0.0)};
        return {double(0.0)};
    };
    
    builtins["string"] = [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {std::string("")};
        return {value_to_string(args[0])};
    };
    
    // Tensor creation
    builtins["zeros"] = [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {Value()};
        auto t = std::make_shared<TensorValue>();
        t->element_type = "f64";
        for (const auto& arg : args) {
            if (std::holds_alternative<int64_t>(arg)) {
                t->shape.dims.push_back(std::get<int64_t>(arg));
            }
        }
        t->data.resize(t->shape.total_size(), 0.0);
        return {Tensor(t)};
    };
    
    builtins["ones"] = [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {Value()};
        auto t = std::make_shared<TensorValue>();
        t->element_type = "f64";
        for (const auto& arg : args) {
            if (std::holds_alternative<int64_t>(arg)) {
                t->shape.dims.push_back(std::get<int64_t>(arg));
            }
        }
        t->data.resize(t->shape.total_size(), 1.0);
        return {Tensor(t)};
    };
    
    builtins["range"] = [](std::vector<Value> args) -> std::vector<Value> {
        int64_t start = 1, end = 0, step = 1;
        if (args.size() >= 1) {
            if (std::holds_alternative<int64_t>(args[0])) end = std::get<int64_t>(args[0]);
        }
        if (args.size() >= 2) {
            if (std::holds_alternative<int64_t>(args[1])) {
                start = end;
                end = std::get<int64_t>(args[1]);
            }
        }
        if (args.size() >= 3) {
            if (std::holds_alternative<int64_t>(args[2])) step = std::get<int64_t>(args[2]);
        }
        
        RuntimeValue result;
        result.type_name = "array";
        for (int64_t i = start; i <= end; i += step) {
            result.array.push_back(int64_t(i));
        }
        return {result};
    };
    
    builtins["tensor"] = [](std::vector<Value> args) -> std::vector<Value> {
        if (args.empty()) return {Value()};
        
        // Check if it's a simple 1D array
        if (args.size() == 1 && std::holds_alternative<RuntimeValue>(args[0])) {
            const RuntimeValue& arr = std::get<RuntimeValue>(args[0]);
            if (arr.is_array()) {
                auto t = std::make_shared<TensorValue>();
                t->element_type = "f64";
                t->shape.dims = {static_cast<int64_t>(arr.array.size())};
                t->data.resize(arr.array.size());
                for (size_t i = 0; i < arr.array.size(); i++) {
                    const Value& v = arr.array[i];
                    if (std::holds_alternative<int64_t>(v)) {
                        t->data[i] = static_cast<double>(std::get<int64_t>(v));
                    } else if (std::holds_alternative<double>(v)) {
                        t->data[i] = std::get<double>(v);
                    }
                }
                return {Tensor(t)};
            }
        }
        
        return {Value()};
    };
    
    // Random functions
    builtins["random"] = [](std::vector<Value> args) -> std::vector<Value> {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<double> dis(0.0, 1.0);
        return {dis(gen)};
    };
    
    builtins["randint"] = [](std::vector<Value> args) -> std::vector<Value> {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        int64_t min_val = 0, max_val = 100;
        if (args.size() >= 1 && std::holds_alternative<int64_t>(args[0])) {
            min_val = std::get<int64_t>(args[0]);
        }
        if (args.size() >= 2 && std::holds_alternative<int64_t>(args[1])) {
            max_val = std::get<int64_t>(args[1]);
        }
        std::uniform_int_distribution<int64_t> dis(min_val, max_val);
        return {dis(gen)};
    };
}

Value Interpreter::execute(const ast::Module& module) {
    Value result;
    for (const auto& stmt : module.statements) {
        result = execute_statement(*stmt);
        if (returned) {
            returned = false;
            break;
        }
    }
    return result;
}

Value Interpreter::execute_statement(const ast::Statement& stmt) {
    switch (stmt.kind) {
        case ast::StatementKind::Let:
            return execute_let(stmt.as_let);
        case ast::StatementKind::Assign:
            return execute_assign(stmt.as_assign);
        case ast::StatementKind::If:
            return execute_if(stmt.as_if);
        case ast::StatementKind::Match:
            return execute_match(stmt.as_match);
        case ast::StatementKind::For:
            return execute_for(stmt.as_for);
        case ast::StatementKind::While:
            return execute_while(stmt.as_while);
        case ast::StatementKind::Loop:
            return execute_loop(stmt.as_loop);
        case ast::StatementKind::Return:
            return execute_return(stmt.as_return);
        case ast::StatementKind::Break:
            return execute_break();
        case ast::StatementKind::Continue:
            return execute_continue();
        case ast::StatementKind::Block:
            return execute_block(stmt.as_block);
        case ast::StatementKind::Expression:
            return execute_expression(*stmt.as_expression);
        case ast::StatementKind::Function:
            return execute_function_decl(stmt.as_function);
        case ast::StatementKind::Publish:
            return execute_publish(stmt.as_publish);
        default:
            return Value();
    }
}

Value Interpreter::execute_let(const ast::LetStatement& let_stmt) {
    if (!let_stmt.initializer) return Value();
    Value val = evaluate_expression(*let_stmt.initializer);
    variables[let_stmt.name] = RuntimeValue::from_value(val);
    return val;
}

Value Interpreter::execute_assign(const ast::AssignStatement& assign_stmt) {
    if (!assign_stmt.value) return Value();
    Value val = evaluate_expression(*assign_stmt.value);
    
    if (assign_stmt.target.kind == ast::ExpressionKind::Identifier) {
        std::string name = assign_stmt.target.as_identifier.name;
        variables[name] = RuntimeValue::from_value(val);
    } else if (assign_stmt.target.kind == ast::ExpressionKind::Index) {
        // Handle array/tensor index assignment
        const auto& idx_expr = assign_stmt.target.as_index;
        Value base_val = evaluate_expression(*idx_expr.base);
        if (std::holds_alternative<RuntimeValue>(base_val)) {
            auto& rt = std::get<RuntimeValue>(base_val);
            if (idx_expr.index) {
                Value idx_val = evaluate_expression(*idx_expr.index);
                if (std::holds_alternative<int64_t>(idx_val)) {
                    int64_t i = std::get<int64_t>(idx_val);
                    if (i >= 1 && i <= static_cast<int64_t>(rt.array.size())) {
                        rt.array[i - 1] = val;
                    }
                }
            }
        }
    }
    return val;
}

Value Interpreter::execute_if(const ast::IfStatement& if_stmt) {
    Value cond = evaluate_expression(*if_stmt.condition);
    bool is_true = value_to_bool(cond);
    
    if (is_true) {
        return execute_block(if_stmt.then_branch);
    } else if (!if_stmt.else_branch.empty()) {
        return execute_block(if_stmt.else_branch);
    }
    return Value();
}

Value Interpreter::execute_match(const ast::MatchStatement& match_stmt) {
    Value match_val = evaluate_expression(*match_stmt.expression);
    
    for (const auto& case_stmt : match_stmt.cases) {
        Value case_val = evaluate_expression(*case_stmt.pattern);
        if (values_equal(match_val, case_val)) {
            return execute_block(case_stmt.body);
        }
    }
    return Value();
}

Value Interpreter::execute_for(const ast::ForStatement& for_stmt) {
    Value iter_val = evaluate_expression(*for_stmt.iterable);
    std::vector<Value> values;
    
    // Extract values from iterable
    if (std::holds_alternative<RuntimeValue>(iter_val)) {
        const RuntimeValue& rt = std::get<RuntimeValue>(iter_val);
        if (rt.is_array()) {
            for (const auto& v : rt.array) {
                values.push_back(v);
            }
        }
    }
    
    if (values.empty() && std::holds_alternative<int64_t>(iter_val)) {
        int64_t n = std::get<int64_t>(iter_val);
        for (int64_t i = 1; i <= n; i++) {
            values.push_back(i);
        }
    }
    
    for (const auto& val : values) {
        variables[for_stmt.variable] = RuntimeValue::from_value(val);
        Value result = execute_block(for_stmt.body);
        if (broken || returned) break;
    }
    
    broken = false;
    return Value();
}

Value Interpreter::execute_while(const ast::WhileStatement& while_stmt) {
    while (true) {
        Value cond = evaluate_expression(*while_stmt.condition);
        if (!value_to_bool(cond)) break;
        
        Value result = execute_block(while_stmt.body);
        if (broken || returned) break;
    }
    broken = false;
    return Value();
}

Value Interpreter::execute_loop(const ast::LoopStatement& loop_stmt) {
    while (true) {
        Value result = execute_block(loop_stmt.body);
        if (returned) break;
        if (broken) {
            broken = false;
            break;
        }
    }
    return Value();
}

Value Interpreter::execute_return(const ast::ReturnStatement& ret_stmt) {
    returned = true;
    if (ret_stmt.value) {
        return evaluate_expression(*ret_stmt.value);
    }
    return Value();
}

Value Interpreter::execute_break() {
    broken = true;
    return Value();
}

Value Interpreter::execute_continue() {
    // In a real implementation, we'd need proper loop nesting tracking
    broken = true;
    return Value();
}

Value Interpreter::execute_block(const ast::Block& block) {
    Value result;
    for (const auto& stmt : block.statements) {
        result = execute_statement(*stmt);
        if (broken || returned) break;
    }
    return result;
}

Value Interpreter::execute_function_decl(const ast::FunctionStatement& func_stmt) {
    // Function declarations would be stored for later use
    // This is a simplified implementation
    return Value();
}

Value Interpreter::execute_publish(const ast::PublishStatement& pub_stmt) {
    Value event_val = evaluate_expression(*pub_stmt.event);
    Value data_val = evaluate_expression(*pub_stmt.data);
    
    std::cout << "[EVENT] " << value_to_string(event_val) << ": " << value_to_string(data_val) << "\n";
    return Value();
}

// ============================================================================
// Expression Evaluation
// ============================================================================

Value Interpreter::evaluate_expression(const ast::Expression& expr) {
    switch (expr.kind) {
        case ast::ExpressionKind::Literal:
            return evaluate_literal(expr.as_literal);
        case ast::ExpressionKind::Identifier:
            return evaluate_identifier(expr.as_identifier);
        case ast::ExpressionKind::Binary:
            return evaluate_binary(expr.as_binary);
        case ast::ExpressionKind::Unary:
            return evaluate_unary(expr.as_unary);
        case ast::ExpressionKind::Call:
            return evaluate_call(expr.as_call);
        case ast::ExpressionKind::Index:
            return evaluate_index(expr.as_index);
        case ast::ExpressionKind::Slice:
            return evaluate_slice(expr.as_slice);
        case ast::ExpressionKind::Lambda:
            return Value(); // Lambda evaluation not implemented
        default:
            return Value();
    }
}

Value Interpreter::evaluate_literal(const ast::LiteralExpression& lit) {
    switch (lit.literal_type) {
        case ast::LiteralType::Integer:
            return int64_t(lit.int_value);
        case ast::LiteralType::Float:
            return lit.float_value;
        case ast::LiteralType::String:
            return lit.string_value;
        case ast::LiteralType::Boolean:
            return lit.bool_value;
        case ast::LiteralType::Array:
            return evaluate_array_literal(lit);
        default:
            return Value();
    }
}

Value Interpreter::evaluate_array_literal(const ast::LiteralExpression& lit) {
    RuntimeValue result;
    result.type_name = "array";
    result.size = lit.elements.size();
    
    for (const auto& elem : lit.elements) {
        result.array.push_back(evaluate_expression(*elem));
    }
    return result;
}

Value Interpreter::evaluate_identifier(const ast::IdentifierExpression& ident) {
    auto it = variables.find(ident.name);
    if (it != variables.end()) {
        return it->second.to_value();
    }
    
    // Check builtins
    if (builtins.find(ident.name) != builtins.end()) {
        return Value(); // Return a marker for builtin function
    }
    
    return Value();
}

Value Interpreter::evaluate_binary(const ast::BinaryExpression& bin) {
    Value left = evaluate_expression(*bin.left);
    Value right = evaluate_expression(*bin.right);
    return binary_op(bin.operator_, left, right);
}

Value Interpreter::evaluate_unary(const ast::UnaryExpression& un) {
    Value operand = evaluate_expression(*un.operand);
    return unary_op(un.operator_, operand);
}

Value Interpreter::evaluate_call(const ast::CallExpression& call) {
    std::string func_name = call.function.as_identifier.name;
    
    // Check builtins
    auto it = builtins.find(func_name);
    if (it != builtins.end()) {
        std::vector<Value> args;
        for (const auto& arg : call.arguments) {
            args.push_back(evaluate_expression(*arg));
        }
        std::vector<Value> results = it->second(args);
        if (!results.empty()) return results[0];
        return Value();
    }
    
    // Check user-defined functions (simplified)
    auto func_it = variables.find(func_name);
    if (func_it != variables.end()) {
        // Function call handling - simplified
    }
    
    return Value();
}

Value Interpreter::evaluate_index(const ast::IndexExpression& idx_expr) {
    Value base_val = evaluate_expression(*idx_expr.base);
    
    if (std::holds_alternative<RuntimeValue>(base_val)) {
        RuntimeValue& rt = std::get<RuntimeValue>(base_val);
        
        if (idx_expr.index) {
            Value idx_val = evaluate_expression(*idx_expr.index);
            
            // Handle array indexing
            if (rt.is_array() && std::holds_alternative<int64_t>(idx_val)) {
                int64_t i = std::get<int64_t>(idx_val);
                if (i >= 1 && i <= static_cast<int64_t>(rt.array.size())) {
                    return rt.array[i - 1];
                }
            }
            
            // Handle tensor indexing
            if (rt.is_tensor() && std::holds_alternative<int64_t>(idx_val)) {
                auto t = rt.tensor;
                if (t) {
                    int64_t i = std::get<int64_t>(idx_val);
                    if (i >= 1 && i <= static_cast<int64_t>(t->data.size())) {
                        return t->data[i - 1];
                    }
                }
            }
        }
    }
    
    return Value();
}

Value Interpreter::evaluate_slice(const ast::SliceExpression& slice_expr) {
    // Simplified slice implementation
    return Value();
}

// ============================================================================
// Helper Functions
// ============================================================================

bool Interpreter::value_to_bool(const Value& val) {
    if (std::holds_alternative<bool>(val)) return std::get<bool>(val);
    if (std::holds_alternative<int64_t>(val)) return std::get<int64_t>(val) != 0;
    if (std::holds_alternative<double>(val)) return std::get<double>(val) != 0.0;
    if (std::holds_alternative<std::string>(val)) return !std::get<std::string>(val).empty();
    if (std::holds_alternative<Tensor>(val)) {
        auto t = std::get<Tensor>(val);
        return t && t->shape.total_size() > 0;
    }
    return false;
}

bool Interpreter::values_equal(const Value& a, const Value& b) {
    if (a.index() != b.index()) return false;
    if (std::holds_alternative<int64_t>(a) && std::holds_alternative<int64_t>(b)) {
        return std::get<int64_t>(a) == std::get<int64_t>(b);
    }
    if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
        return std::get<double>(a) == std::get<double>(b);
    }
    if (std::holds_alternative<std::string>(a) && std::holds_alternative<std::string>(b)) {
        return std::get<std::string>(a) == std::get<std::string>(b);
    }
    if (std::holds_alternative<bool>(a) && std::holds_alternative<bool>(b)) {
        return std::get<bool>(a) == std::get<bool>(b);
    }
    return false;
}

// ============================================================================
// RuntimeValue Methods
// ============================================================================

Value RuntimeValue::to_value() const {
    if (tensor) return tensor;
    if (is_array() && !array.empty()) return *this;
    if (!array.empty()) return array[0];
    return Value();
}

RuntimeValue RuntimeValue::from_value(const Value& val) {
    RuntimeValue rt;
    if (std::holds_alternative<int64_t>(val)) {
        rt.type_name = "i64";
        rt.array = {val};
    } else if (std::holds_alternative<double>(val)) {
        rt.type_name = "f64";
        rt.array = {val};
    } else if (std::holds_alternative<std::string>(val)) {
        rt.type_name = "string";
        rt.array = {val};
    } else if (std::holds_alternative<bool>(val)) {
        rt.type_name = "bool";
        rt.array = {val};
    } else if (std::holds_alternative<Tensor>(val)) {
        rt.tensor = std::get<Tensor>(val);
    } else if (std::holds_alternative<RuntimeValue>(val)) {
        return std::get<RuntimeValue>(val);
    }
    return rt;
}

} // namespace interpreter
} // namespace claw
