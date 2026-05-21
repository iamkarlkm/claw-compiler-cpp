// type/error_effect.cpp - Error effect implementation

#include "error_effect.h"

namespace claw {
namespace type {

std::string ErrorEffectInfo::to_string() const {
    switch (kind) {
        case ErrorEffect::NoError:
            return "noraise";
        case ErrorEffect::ConcreteError:
            if (error_type) return "raise " + error_type->to_string();
            return "raise";
        case ErrorEffect::GenericError:
            return "raise?" + (polymorphic_var.empty() ? "" : "(" + polymorphic_var + ")");
        case ErrorEffect::UnknownError:
            return "unknown";
    }
    return "unknown";
}

ErrorEffectInfo union_effects(const ErrorEffectInfo& a, const ErrorEffectInfo& b) {
    // If either is unknown, result is unknown
    if (a.is_unknown() || b.is_unknown()) {
        return ErrorEffectInfo();
    }

    // If both are NoError, result is NoError
    if (a.is_no_error() && b.is_no_error()) {
        return ErrorEffectInfo::no_error();
    }

    // If one is NoError and the other is something else, result is the something else
    if (a.is_no_error()) return b;
    if (b.is_no_error()) return a;

    // If both are ConcreteError with the same type, keep it
    if (a.is_concrete_error() && b.is_concrete_error()) {
        if (a.error_type && b.error_type && a.error_type->equals(b.error_type)) {
            return ErrorEffectInfo::concrete_error(a.error_type);
        }
        // Different concrete errors — for now return unknown
        // (a more advanced system could track a set of possible errors)
        return ErrorEffectInfo();
    }

    // If either is GenericError, result is GenericError
    if (a.is_generic_error()) return a;
    if (b.is_generic_error()) return b;

    return ErrorEffectInfo();
}

} // namespace type
} // namespace claw
