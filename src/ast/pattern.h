// ast/pattern.h - Dedicated Pattern AST hierarchy for pattern matching

#ifndef CLAW_AST_PATTERN_H
#define CLAW_AST_PATTERN_H

#include <memory>
#include <string>
#include <vector>
#include "ast.h"

namespace claw {
namespace ast {

// ============================================================================
// Pattern base class
// ============================================================================

class Pattern : public ASTNode {
public:
    enum class Kind {
        Wildcard,       // _
        Variable,       // x
        Literal,        // 42, true, "hello"
        Constructor,    // Some(x), None, Cons(head, tail)
        Tuple,          // (a, b, c)
        Array,          // [a, b, c]
        Rest,           // ..rest (spread in array/struct pattern)
        Or,             // A | B
        Range,          // 1..10
        Binding,        // x @ Some(_)
    };

    Pattern(Kind kind, const SourceSpan& span) : kind_(kind) { span_ = span; }
    Kind get_kind() const { return kind_; }

private:
    Kind kind_;
};

// ============================================================================
// Concrete pattern types
// ============================================================================

// Wildcard pattern: _
class WildcardPattern : public Pattern {
public:
    WildcardPattern(const SourceSpan& span) : Pattern(Kind::Wildcard, span) {}

    std::string to_string() const override { return "_"; }
};

// Variable pattern: x (binds the matched value to a name)
class VariablePattern : public Pattern {
public:
    VariablePattern(const std::string& name, const SourceSpan& span)
        : Pattern(Kind::Variable, span), name_(name) {}

    const std::string& get_name() const { return name_; }

    std::string to_string() const override { return name_; }

private:
    std::string name_;
};

// Literal pattern: 42, true, "hello"
class LiteralPattern : public Pattern {
public:
    using Value = LiteralExpr::Value;

    LiteralPattern(const Value& value, const SourceSpan& span)
        : Pattern(Kind::Literal, span), value_(value) {}

    const Value& get_value() const { return value_; }

    std::string to_string() const override {
        return std::visit([](auto&& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, int64_t>) return std::to_string(v);
            else if constexpr (std::is_same_v<T, double>) return std::to_string(v);
            else if constexpr (std::is_same_v<T, std::string>) return "\"" + v + "\"";
            else if constexpr (std::is_same_v<T, bool>) return v ? "true" : "false";
            else if constexpr (std::is_same_v<T, char>) return std::string(1, v);
            return "null";
        }, value_);
    }

private:
    Value value_;
};

// Constructor pattern: Some(x), None, Cons(head, tail)
class ConstructorPattern : public Pattern {
public:
    ConstructorPattern(const std::string& name, const SourceSpan& span)
        : Pattern(Kind::Constructor, span), name_(name) {}

    void add_field(std::unique_ptr<Pattern> field) {
        fields_.push_back(std::move(field));
    }

    const std::string& get_name() const { return name_; }
    const auto& get_fields() const { return fields_; }
    auto& mutable_fields() { return fields_; }

    std::string to_string() const override {
        std::string result = name_;
        if (!fields_.empty()) {
            result += "(";
            for (size_t i = 0; i < fields_.size(); i++) {
                result += fields_[i]->to_string();
                if (i + 1 < fields_.size()) result += ", ";
            }
            result += ")";
        }
        return result;
    }

private:
    std::string name_;
    std::vector<std::unique_ptr<Pattern>> fields_;
};

// Tuple pattern: (a, b, c)
class TuplePattern : public Pattern {
public:
    TuplePattern(const SourceSpan& span) : Pattern(Kind::Tuple, span) {}

    void add_element(std::unique_ptr<Pattern> elem) {
        elements_.push_back(std::move(elem));
    }

    const auto& get_elements() const { return elements_; }
    auto& mutable_elements() { return elements_; }
    size_t size() const { return elements_.size(); }

    std::string to_string() const override {
        std::string result = "(";
        for (size_t i = 0; i < elements_.size(); i++) {
            result += elements_[i]->to_string();
            if (i + 1 < elements_.size()) result += ", ";
        }
        result += ")";
        return result;
    }

private:
    std::vector<std::unique_ptr<Pattern>> elements_;
};

// Array pattern: [a, b, ..rest]
class ArrayPattern : public Pattern {
public:
    ArrayPattern(const SourceSpan& span) : Pattern(Kind::Array, span) {}

    void add_element(std::unique_ptr<Pattern> elem) {
        elements_.push_back(std::move(elem));
    }

    const auto& get_elements() const { return elements_; }
    auto& mutable_elements() { return elements_; }

    std::string to_string() const override {
        std::string result = "[";
        for (size_t i = 0; i < elements_.size(); i++) {
            result += elements_[i]->to_string();
            if (i + 1 < elements_.size()) result += ", ";
        }
        result += "]";
        return result;
    }

private:
    std::vector<std::unique_ptr<Pattern>> elements_;
};

// Rest pattern: ..rest (used inside array/struct patterns)
class RestPattern : public Pattern {
public:
    RestPattern(const std::string& bind_name, const SourceSpan& span)
        : Pattern(Kind::Rest, span), bind_name_(bind_name) {}

    const std::string& get_bind_name() const { return bind_name_; }

    std::string to_string() const override {
        return ".." + bind_name_;
    }

private:
    std::string bind_name_;
};

// Or pattern: A | B (matches if either pattern matches)
class OrPattern : public Pattern {
public:
    OrPattern(std::unique_ptr<Pattern> left, std::unique_ptr<Pattern> right,
              const SourceSpan& span)
        : Pattern(Kind::Or, span), left_(std::move(left)), right_(std::move(right)) {}

    Pattern* get_left() const { return left_.get(); }
    Pattern* get_right() const { return right_.get(); }
    std::unique_ptr<Pattern>& mutable_left() { return left_; }
    std::unique_ptr<Pattern>& mutable_right() { return right_; }

    std::string to_string() const override {
        return left_->to_string() + " | " + right_->to_string();
    }

private:
    std::unique_ptr<Pattern> left_;
    std::unique_ptr<Pattern> right_;
};

// Range pattern: 1..10 (matches values in range)
class RangePattern : public Pattern {
public:
    RangePattern(std::unique_ptr<Pattern> start, std::unique_ptr<Pattern> end,
                 bool inclusive, const SourceSpan& span)
        : Pattern(Kind::Range, span), start_(std::move(start)), end_(std::move(end)),
          inclusive_(inclusive) {}

    Pattern* get_start() const { return start_.get(); }
    Pattern* get_end() const { return end_.get(); }
    bool is_inclusive() const { return inclusive_; }

    std::string to_string() const override {
        return start_->to_string() + (inclusive_ ? "..=" : "..") + end_->to_string();
    }

private:
    std::unique_ptr<Pattern> start_;
    std::unique_ptr<Pattern> end_;
    bool inclusive_;
};

// Binding pattern: x @ Some(_) (binds name AND destructures)
class BindingPattern : public Pattern {
public:
    BindingPattern(const std::string& name, std::unique_ptr<Pattern> sub_pattern,
                   const SourceSpan& span)
        : Pattern(Kind::Binding, span), name_(name), sub_pattern_(std::move(sub_pattern)) {}

    const std::string& get_name() const { return name_; }
    Pattern* get_sub_pattern() const { return sub_pattern_.get(); }
    std::unique_ptr<Pattern>& mutable_sub_pattern() { return sub_pattern_; }

    std::string to_string() const override {
        return name_ + " @ " + sub_pattern_->to_string();
    }

private:
    std::string name_;
    std::unique_ptr<Pattern> sub_pattern_;
};

// ============================================================================
// Clone helper
// ============================================================================

std::unique_ptr<Pattern> clone_pattern(const Pattern& pat);

} // namespace ast
} // namespace claw

#endif // CLAW_AST_PATTERN_H
