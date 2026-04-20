// Claw Interpreter - Implementation Stubs
// All logic is inlined in interpreter.h using unified ClawValue types
// This file exists for backward compatibility with the build system

#include "interpreter/interpreter.h"

namespace claw {
namespace interpreter {

// All Interpreter methods are defined inline in interpreter.h
// using the unified runtime::ClawValue type from runtime/claw_value.h
//
// Removed old types:
//   - Value (old variant<monostate, int64_t, double, string, bool, char>)
//   - RuntimeValue (old wrapper struct)
//   - Runtime (old environment class)
//   - TensorValue (old tensor struct)
//
// Replaced by:
//   - runtime::ClawValue (tagged union, single type for everything)
//   - Interpreter::globals / scope_stack (variable management)
//   - runtime::TensorData (heap-allocated tensor)

} // namespace interpreter
} // namespace claw
