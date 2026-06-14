// test/test_deoptimization.cpp - Deoptimization Tests
// Phase 18.1: Deoptimization + OSR Support

#include "../../tests/claw_test.h"
#include "jit/deoptimization.h"
#include "bytecode/bytecode.h"

using namespace claw;
using namespace claw::jit;

// ============================================================================
// DeoptimizationManager Tests
// ============================================================================

TEST(Deoptimization, Manager_Create) {
    auto manager = create_deoptimization_manager();
    ASSERT_TRUE(manager != nullptr);
    ASSERT_EQ(manager->total_deoptimizations(), 0);
}

TEST(Deoptimization, Manager_RegisterPoint) {
    auto manager = create_deoptimization_manager();

    DeoptimizationPoint point;
    point.bytecode_offset = 100;
    point.reason = DeoptimizationReason::kTypeMismatch;
    point.interpreter_entry_offset = 50;
    point.stack_frame_size = 64;
    point.local_count = 5;

    manager->register_deoptimization_point("test_func", point);

    const auto* found = manager->find_deoptimization_point("test_func", 100);
    ASSERT_TRUE(found != nullptr);
    ASSERT_EQ(found->bytecode_offset, 100);
    ASSERT_EQ(found->reason, DeoptimizationReason::kTypeMismatch);
}

TEST(Deoptimization, Manager_ExecuteDeopt) {
    auto manager = create_deoptimization_manager();

    DeoptimizationPoint point;
    point.bytecode_offset = 100;
    point.reason = DeoptimizationReason::kTypeMismatch;
    point.interpreter_entry_offset = 50;
    point.stack_frame_size = 64;
    manager->register_deoptimization_point("test_func", point);

    std::vector<bytecode::Value> guard_values;
    auto target = manager->execute_deoptimization(
        "test_func", 100, DeoptimizationReason::kTypeMismatch, guard_values);

    ASSERT_EQ(target.bytecode_offset, 50);
    ASSERT_EQ(manager->total_deoptimizations(), 1);
}

TEST(Deoptimization, Manager_StatsByReason) {
    auto manager = create_deoptimization_manager();

    std::vector<bytecode::Value> empty;
    manager->execute_deoptimization("f1", 1, DeoptimizationReason::kTypeMismatch, empty);
    manager->execute_deoptimization("f2", 2, DeoptimizationReason::kTypeMismatch, empty);
    manager->execute_deoptimization("f3", 3, DeoptimizationReason::kDivisionByZero, empty);

    ASSERT_EQ(manager->deoptimizations_by_reason(DeoptimizationReason::kTypeMismatch), 2);
    ASSERT_EQ(manager->deoptimizations_by_reason(DeoptimizationReason::kDivisionByZero), 1);
}

TEST(Deoptimization, Manager_Clear) {
    auto manager = create_deoptimization_manager();

    DeoptimizationPoint point;
    point.bytecode_offset = 100;
    manager->register_deoptimization_point("test_func", point);

    std::vector<bytecode::Value> empty;
    manager->execute_deoptimization("test_func", 100, DeoptimizationReason::kTypeMismatch, empty);

    ASSERT_EQ(manager->total_deoptimizations(), 1);

    manager->clear();

    ASSERT_EQ(manager->total_deoptimizations(), 0);
    ASSERT_TRUE(manager->find_deoptimization_point("test_func", 100) == nullptr);
}

// ============================================================================
// OSRCompiler Tests
// ============================================================================

TEST(Deoptimization, OSRCompiler_Create) {
    auto compiler = create_osr_compiler();
    ASSERT_TRUE(compiler != nullptr);
}

TEST(Deoptimization, OSRCompiler_CanOSR) {
    auto compiler = create_osr_compiler();

    bool can_osr = compiler->can_osr("test_func", 100, {});
    ASSERT_TRUE(can_osr);

    can_osr = compiler->can_osr("", 100, {});
    ASSERT_FALSE(can_osr);

    can_osr = compiler->can_osr("test_func", 0, {});
    ASSERT_FALSE(can_osr);
}

TEST(Deoptimization, OSRCompiler_GetEntry) {
    auto compiler = create_osr_compiler();

    void* entry = compiler->get_osr_entry("test_func", 100);
    ASSERT_TRUE(entry == nullptr);
}

TEST(Deoptimization, OSRCompiler_TriggerOSR) {
    auto compiler = create_osr_compiler();

    std::vector<bytecode::Value> state;
    void* osr_entry = nullptr;
    DeoptimizationTarget target;

    bool triggered = compiler->trigger_osr("test_func", 100, state, osr_entry, target);
    ASSERT_FALSE(triggered);
}

TEST(Deoptimization, OSRCompiler_Clear) {
    auto compiler = create_osr_compiler();

    compiler->clear();

    void* entry = compiler->get_osr_entry("test_func", 100);
    ASSERT_TRUE(entry == nullptr);
}

// ============================================================================
// JITCompilerWithDeopt Tests
// ============================================================================

TEST(Deoptimization, JITWithDeopt_Create) {
    auto jit = create_jit_compiler_with_deoptimization();
    ASSERT_TRUE(jit != nullptr);
    ASSERT_EQ(jit->total_deoptimizations(), 0);
    ASSERT_EQ(jit->total_osr(), 0);
}

TEST(Deoptimization, JITWithDeopt_Deoptimize) {
    auto jit = create_jit_compiler_with_deoptimization();

    DeoptimizationPoint point;
    point.bytecode_offset = 100;
    point.reason = DeoptimizationReason::kTypeMismatch;
    point.interpreter_entry_offset = 50;
    jit->add_deoptimization_point("test_func", point);

    std::vector<bytecode::Value> guard_values;
    auto target = jit->deoptimize("test_func", 100, DeoptimizationReason::kTypeMismatch, guard_values);

    ASSERT_EQ(target.bytecode_offset, 50);
    ASSERT_EQ(jit->total_deoptimizations(), 1);
}

TEST(Deoptimization, JITWithDeopt_TypeGuard) {
    auto jit = create_jit_compiler_with_deoptimization();

    auto val = bytecode::Value::integer(42);

    bool pass = jit->check_type_guard(val, bytecode::ValueType::I64);
    ASSERT_TRUE(pass);

    auto fval = bytecode::Value::floating(3.14);
    pass = jit->check_type_guard(fval, bytecode::ValueType::I64);
    ASSERT_FALSE(pass);
}

// ============================================================================
// Translation Cache Tests
// ============================================================================

TEST(Deoptimization, TranslationCache) {
    auto manager = create_deoptimization_manager();

    TranslatedCode translation;
    translation.machine_code = reinterpret_cast<void*>(0x1000);
    translation.code_size = 1024;

    manager->cache_translation("func1", 50, translation);

    const auto* found = manager->find_cached_translation("func1", 50);
    ASSERT_TRUE(found != nullptr);
    ASSERT_EQ(found->machine_code, reinterpret_cast<void*>(0x1000));

    found = manager->find_cached_translation("func1", 99);
    ASSERT_TRUE(found == nullptr);
}

