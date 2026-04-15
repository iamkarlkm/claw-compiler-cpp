// Type Checker Header - Forward declarations and additional interfaces
#ifndef CLAW_TYPE_CHECKER_H
#define CLAW_TYPE_CHECKER_H

#include "type_system.h"

namespace claw {
namespace type {

// TypeChecker is fully defined in type_system.h
// This header provides additional type checking utilities

// Type checking result
struct TypeCheckResult {
    bool success;
    std::vector<CompilerError> errors;
    
    explicit TypeCheckResult(bool ok = true) : success(ok) {}
};

// Helper function to run type checking on a program
inline TypeCheckResult check_program_types(const ast::Program& program) {
    TypeChecker checker;
    checker.check(program);
    
    TypeCheckResult result;
    result.success = !checker.has_errors();
    result.errors = checker.errors();
    return result;
}

} // namespace type
} // namespace claw

#endif // CLAW_TYPE_CHECKER_H
