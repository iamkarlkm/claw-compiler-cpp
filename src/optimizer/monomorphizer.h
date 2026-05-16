// optimizer/monomorphizer.h - Generic monomorphization pass

#ifndef CLAW_OPTIMIZER_MONOMORPHIZER_H
#define CLAW_OPTIMIZER_MONOMORPHIZER_H

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace claw {
namespace ast {
    class Program;
    class Statement;
    class Expression;
    class FunctionStmt;
    class CallExpr;
    class Identifier;
}

namespace optimizer {

// ============================================================================
// Monomorphizer - Compile-time generic instantiation
//
// Transforms generic functions into concrete type-specialized versions.
// Example: fn id<T>(x: T) -> T { x }  +  id<Int>(42)
//        → fn id__Int(x: Int) -> Int { x }  +  id__Int(42)
// ============================================================================

class Monomorphizer {
public:
    Monomorphizer() = default;

    // Main entry: monomorphize all generic calls in the program
    bool monomorphize(ast::Program& program);

    // Statistics
    int get_instantiated_count() const { return instantiated_count_; }
    int get_replaced_count() const { return replaced_count_; }

private:
    // Generic function registry: name -> FunctionStmt*
    std::unordered_map<std::string, ast::FunctionStmt*> generic_functions_;

    // Already instantiated: (generic_name, type_args) -> mangled_name
    std::unordered_map<std::string, std::string> instances_;

    // Statistics
    int instantiated_count_ = 0;
    int replaced_count_ = 0;

    // Phase 1: collect all generic function definitions
    void collect_generic_functions(ast::Program& program);

    // Phase 2: collect instantiation sites and generate instances
    void process_program(ast::Program& program);

    // Phase 3: replace generic calls with concrete calls
    void replace_calls(ast::Program& program);

    // Helper: process a statement tree looking for calls and nested functions
    void process_statement(ast::Statement* stmt, ast::Program& program);

    // Helper: process an expression tree looking for calls
    void process_expression(ast::Expression* expr, ast::Program& program);

    // Generate a concrete instance from a generic function
    std::unique_ptr<ast::FunctionStmt> instantiate_function(
        ast::FunctionStmt* generic_fn,
        const std::vector<std::string>& type_args);

    // Replace type variable names in a type annotation string
    std::string substitute_type(
        const std::string& type_str,
        const std::unordered_map<std::string, std::string>& subst);

    // Name mangling: id + [Int] → id__Int
    std::string mangle_name(
        const std::string& base,
        const std::vector<std::string>& type_args) const;

    // Build substitution map from type params to concrete types
    std::unordered_map<std::string, std::string> build_substitution(
        ast::FunctionStmt* generic_fn,
        const std::vector<std::string>& type_args) const;
};

} // namespace optimizer
} // namespace claw

#endif // CLAW_OPTIMIZER_MONOMORPHIZER_H
