// type/error_effect.h - Compile-time error effect tracking

#ifndef CLAW_ERROR_EFFECT_H
#define CLAW_ERROR_EFFECT_H

#include <string>
#include <vector>
#include <memory>
#include "type_system.h"

namespace claw {
namespace type {

// ============================================================================
// Error effect classification
// ============================================================================

enum class ErrorEffect {
    UnknownError,   // Unannotated, to be inferred
    NoError,        // noraise — guaranteed not to raise
    ConcreteError,  // raise SomeError — may raise a specific error type
    GenericError,   // raise? — error effect determined by parameters (polymorphic)
};

// ============================================================================
// Error effect info attached to functions and expressions
// ============================================================================

struct ErrorEffectInfo {
    ErrorEffect kind = ErrorEffect::UnknownError;

    // For ConcreteError: the specific error type being raised
    TypePtr error_type;

    // For GenericError: the constraint variable name (e.g., "raise?")
    std::string polymorphic_var;

    ErrorEffectInfo() = default;
    explicit ErrorEffectInfo(ErrorEffect k) : kind(k) {}

    static ErrorEffectInfo no_error() {
        return ErrorEffectInfo(ErrorEffect::NoError);
    }

    static ErrorEffectInfo concrete_error(TypePtr type) {
        ErrorEffectInfo info(ErrorEffect::ConcreteError);
        info.error_type = std::move(type);
        return info;
    }

    static ErrorEffectInfo generic_error(std::string var) {
        ErrorEffectInfo info(ErrorEffect::GenericError);
        info.polymorphic_var = std::move(var);
        return info;
    }

    bool is_no_error() const { return kind == ErrorEffect::NoError; }
    bool is_concrete_error() const { return kind == ErrorEffect::ConcreteError; }
    bool is_generic_error() const { return kind == ErrorEffect::GenericError; }
    bool is_unknown() const { return kind == ErrorEffect::UnknownError; }

    bool can_raise() const {
        return kind == ErrorEffect::ConcreteError ||
               kind == ErrorEffect::GenericError ||
               kind == ErrorEffect::UnknownError;
    }

    std::string to_string() const;

    bool operator==(const ErrorEffectInfo& other) const {
        if (kind != other.kind) return false;
        if (kind == ErrorEffect::ConcreteError) {
            if (!error_type || !other.error_type) return false;
            return error_type->equals(other.error_type);
        }
        if (kind == ErrorEffect::GenericError) {
            return polymorphic_var == other.polymorphic_var;
        }
        return true;
    }

    bool operator!=(const ErrorEffectInfo& other) const {
        return !(*this == other);
    }
};

// ============================================================================
// Union of two error effects (used for if/else branches, etc.)
// ============================================================================

ErrorEffectInfo union_effects(const ErrorEffectInfo& a, const ErrorEffectInfo& b);

} // namespace type
} // namespace claw

#endif // CLAW_ERROR_EFFECT_H
