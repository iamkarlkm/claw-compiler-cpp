// optimizer/tree_shaker.h - Module-level Tree Shaking for Claw
// Removes unused functions and globals based on reachability analysis.
// Inspired by MoonBit's aggressive tree shaking for minimal output size.

#ifndef CLAW_TREE_SHAKER_H
#define CLAW_TREE_SHAKER_H

#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "../ast/ast.h"

namespace claw {
namespace optimizer {

// ============================================================================
// Tree Shaking Statistics
// ============================================================================
struct TreeShakeStats {
    int functions_total = 0;
    int functions_removed = 0;
    int globals_total = 0;
    int globals_removed = 0;
    int structs_total = 0;
    int structs_removed = 0;

    std::string summary() const {
        return "tree-shake: removed " + std::to_string(functions_removed) +
               "/" + std::to_string(functions_total) + " functions, " +
               std::to_string(globals_removed) + "/" + std::to_string(globals_total) +
               " globals, " + std::to_string(structs_removed) + "/" +
               std::to_string(structs_total) + " structs";
    }
};

// ============================================================================
// Tree Shaker
// ============================================================================
class TreeShaker {
public:
    TreeShaker();

    // Run tree shaking on a program. Returns true if any nodes were removed.
    bool shake(ast::Program& program, TreeShakeStats* stats = nullptr);

    // Add additional entry-point function names (e.g., #[test] functions).
    void add_entry_point(const std::string& name);

    // Clear custom entry points.
    void clear_entry_points();

private:
    // Build call graph: function name -> set of callee names
    std::unordered_map<std::string, std::unordered_set<std::string>> call_graph_;
    // All function names -> their declaration index in program
    std::unordered_map<std::string, size_t> function_index_;
    // Struct names -> their declaration index
    std::unordered_map<std::string, size_t> struct_index_;
    // Global (let) names -> their declaration index
    std::unordered_map<std::string, size_t> global_index_;
    // Globals referenced by reachable code
    std::unordered_set<std::string> used_globals_;
    // Structs referenced by reachable code
    std::unordered_set<std::string> used_structs_;
    // Custom entry points
    std::unordered_set<std::string> custom_entries_;

    // Scan all declarations to build indices and call graph
    void scan_program(const ast::Program& program);

    // Scan a function body for calls, global uses, struct uses
    void scan_function_body(const ast::FunctionStmt& func);

    // Scan top-level (non-function) statements for entry-point discovery
    void scan_top_level(const ast::Statement& stmt);

    // Recursive expression scanner
    void scan_expr(const ast::Expression& expr,
                   std::unordered_set<std::string>* local_calls,
                   std::unordered_set<std::string>* local_globals,
                   std::unordered_set<std::string>* local_structs);

    // Recursive statement scanner
    void scan_stmt(const ast::Statement& stmt,
                   std::unordered_set<std::string>* local_calls,
                   std::unordered_set<std::string>* local_globals,
                   std::unordered_set<std::string>* local_structs);

    // Mark all reachable functions, globals, and structs from entry points
    void mark_reachable();

    // Collect entry-point function names
    std::unordered_set<std::string> collect_entry_points();
};

// ============================================================================
// Convenience function
// ============================================================================
bool tree_shake(ast::Program& program, TreeShakeStats* stats = nullptr);

} // namespace optimizer
} // namespace claw

#endif // CLAW_TREE_SHAKER_H
