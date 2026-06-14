// test/test_vm_evaluator.cpp - VM 真实评估器单元测试

#include "../auto_scheduler/vm_evaluator.h"
#include "../auto_scheduler/auto_scheduler.h"
#include "test.h"
#include <cmath>

using namespace claw;
using namespace claw::scheduler;
using namespace claw::tensorir;

// ============================================================================
// 测试辅助函数
// ============================================================================

static MatmulOp* create_test_matmul(int64_t m, int64_t n, int64_t k) {
    auto* op = new MatmulOp();

    auto* a = new Buffer("A", "f32", DimList{m, k});
    auto* b = new Buffer("B", "f32", DimList{k, n});
    auto* c = new Buffer("C", "f32", DimList{m, n});

    op->inputs.push_back(a);
    op->inputs.push_back(b);
    op->outputs.push_back(c);

    return op;
}

static ReduceOp* create_test_reduce(const std::vector<int64_t>& shape) {
    auto* op = new ReduceOp();

    auto* input = new Buffer("input", "f32", DimList{shape.begin(), shape.end()});
    auto* output = new Buffer("output", "f32", DimList{1});

    op->inputs.push_back(input);
    op->outputs.push_back(output);
    op->reduce_type = ReduceOp::ReduceType::Sum;

    return op;
}

static ComputeOp* create_test_compute(const std::vector<int64_t>& shape) {
    auto* op = new ComputeOp("element_wise");

    auto* output = new Buffer("out", "f32", DimList{shape.begin(), shape.end()});
    op->outputs.push_back(output);
    op->body_expr = "x * 2.0 + 1.0";

    return op;
}

// ============================================================================
// VMEvaluator 测试
// ============================================================================

CLAW_TEST_SUITE(VMEvaluator)

CLAW_TEST(VMEvaluator_Create) {
    VMEvaluatorConfig config;
    config.warmup_iterations = 1;
    config.measurement_iterations = 2;
    config.timeout_ms = 5000.0;

    VMEvaluator eval(config);

    CLAW_ASSERT_EQ(eval.get_config().warmup_iterations, 1);
    CLAW_ASSERT_EQ(eval.get_config().measurement_iterations, 2);

    auto stats = eval.get_stats();
    CLAW_ASSERT_EQ(stats.total_evaluations, 0);
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(VMEvaluator_MatMulEvaluate) {
    auto* op = create_test_matmul(32, 32, 32);
    ScheduleSpace space(op, nullptr);

    VMEvaluatorConfig config;
    config.warmup_iterations = 1;
    config.measurement_iterations = 3;
    config.timeout_ms = 10000.0;
    config.verbose = false;

    VMEvaluator eval(config);

    auto default_config = space.get_default_config();
    auto result = eval.evaluate(default_config, op, &space);

    CLAW_ASSERT(result.is_valid);
    CLAW_ASSERT(result.measured_time_ms > 0.0);
    CLAW_ASSERT(result.measured_time_ms < 5000.0);

    delete op->inputs[0];
    delete op->inputs[1];
    delete op->outputs[0];
    delete op;
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(VMEvaluator_ReduceEvaluate) {
    auto* op = create_test_reduce({256, 256});
    ScheduleSpace space(op, nullptr);

    VMEvaluatorConfig config;
    config.warmup_iterations = 1;
    config.measurement_iterations = 3;
    config.timeout_ms = 10000.0;

    VMEvaluator eval(config);

    auto default_config = space.get_default_config();
    auto result = eval.evaluate(default_config, op, &space);

    CLAW_ASSERT(result.is_valid);
    CLAW_ASSERT(result.measured_time_ms > 0.0);

    delete op->inputs[0];
    delete op->outputs[0];
    delete op;
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(VMEvaluator_ComputeEvaluate) {
    auto* op = create_test_compute({128, 128});
    ScheduleSpace space(op, nullptr);

    VMEvaluatorConfig config;
    config.warmup_iterations = 1;
    config.measurement_iterations = 3;
    config.timeout_ms = 10000.0;

    VMEvaluator eval(config);

    auto default_config = space.get_default_config();
    auto result = eval.evaluate(default_config, op, &space);

    CLAW_ASSERT(result.is_valid);
    CLAW_ASSERT(result.measured_time_ms > 0.0);

    delete op->outputs[0];
    delete op;
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(VMEvaluator_ConfigComparison) {
    auto* op = create_test_matmul(16, 16, 16);
    ScheduleSpace space(op, nullptr);

    VMEvaluatorConfig config;
    config.warmup_iterations = 1;
    config.measurement_iterations = 3;
    config.timeout_ms = 10000.0;

    VMEvaluator eval(config);

    auto config1 = space.get_default_config();
    auto result1 = eval.evaluate(config1, op, &space);

    std::mt19937 rng(42);
    auto config2 = space.random_sample(rng);
    auto result2 = eval.evaluate(config2, op, &space);

    CLAW_ASSERT(result1.is_valid);
    CLAW_ASSERT(result2.is_valid);
    CLAW_ASSERT(result1.measured_time_ms > 0);
    CLAW_ASSERT(result2.measured_time_ms > 0);

    delete op->inputs[0];
    delete op->inputs[1];
    delete op->outputs[0];
    delete op;
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(VMEvaluator_Warmup) {
    auto* op = create_test_matmul(16, 16, 16);

    VMEvaluatorConfig config;
    config.warmup_iterations = 3;
    config.measurement_iterations = 2;
    config.timeout_ms = 5000.0;

    VMEvaluator eval(config);

    eval.warmup(op, 3);

    auto stats = eval.get_stats();
    CLAW_ASSERT_EQ(stats.total_evaluations, 0);

    delete op->inputs[0];
    delete op->inputs[1];
    delete op->outputs[0];
    delete op;
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(VMEvaluator_StatsTracking) {
    auto* op = create_test_matmul(16, 16, 16);
    ScheduleSpace space(op, nullptr);

    VMEvaluatorConfig config;
    config.warmup_iterations = 1;
    config.measurement_iterations = 2;
    config.timeout_ms = 10000.0;

    VMEvaluator eval(config);

    auto cfg = space.get_default_config();
    auto result = eval.evaluate(cfg, op, &space);

    auto stats = eval.get_stats();
    CLAW_ASSERT_EQ(stats.total_evaluations, 1);
    CLAW_ASSERT_EQ(stats.total_errors, 0);
    CLAW_ASSERT_EQ(stats.total_timeouts, 0);
    CLAW_ASSERT(stats.avg_execution_time_ms > 0);

    delete op->inputs[0];
    delete op->inputs[1];
    delete op->outputs[0];
    delete op;
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(VMEvaluator_ResetStats) {
    auto* op = create_test_matmul(16, 16, 16);
    ScheduleSpace space(op, nullptr);

    VMEvaluatorConfig config;
    config.warmup_iterations = 1;
    config.measurement_iterations = 2;
    config.timeout_ms = 10000.0;

    VMEvaluator eval(config);

    auto cfg = space.get_default_config();
    eval.evaluate(cfg, op, &space);

    eval.reset_stats();

    auto stats = eval.get_stats();
    CLAW_ASSERT_EQ(stats.total_evaluations, 0);

    delete op->inputs[0];
    delete op->inputs[1];
    delete op->outputs[0];
    delete op;
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(VMEvaluator_NullOp) {
    VMEvaluatorConfig config;
    config.warmup_iterations = 0;
    config.measurement_iterations = 1;

    VMEvaluator eval(config);

    ScheduleConfig empty_config;
    auto result = eval.evaluate(empty_config, nullptr, nullptr);

    CLAW_ASSERT_FALSE(result.is_valid);
    CLAW_ASSERT_FALSE(result.error_msg.empty());
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(VMEvaluator_FastEvaluatorFactory) {
    auto eval = create_fast_vm_evaluator();
    CLAW_ASSERT(eval != nullptr);

    auto* op = create_test_matmul(16, 16, 16);
    ScheduleSpace space(op, nullptr);

    auto cfg = space.get_default_config();
    auto result = eval->evaluate(cfg, op, &space);

    CLAW_ASSERT(result.is_valid);

    delete op->inputs[0];
    delete op->inputs[1];
    delete op->outputs[0];
    delete op;
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(VMEvaluator_PreciseEvaluatorFactory) {
    auto eval = create_precise_vm_evaluator();
    CLAW_ASSERT(eval != nullptr);

    auto* op = create_test_matmul(16, 16, 16);
    ScheduleSpace space(op, nullptr);

    auto cfg = space.get_default_config();
    auto result = eval->evaluate(cfg, op, &space);

    CLAW_ASSERT(result.is_valid);

    delete op->inputs[0];
    delete op->inputs[1];
    delete op->outputs[0];
    delete op;
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(VMEvaluator_AutoSchedulerIntegration) {
    auto* op = create_test_matmul(16, 16, 16);

    AutoSchedulerConfig sched_config;
    sched_config.use_mock_evaluator = false;
    sched_config.search_options.max_trials = 5;
    sched_config.search_options.top_k = 3;
    sched_config.search_options.timeout_sec = 30.0;
    sched_config.verbose = false;

    AutoScheduler scheduler(sched_config);
    auto result = scheduler.schedule_op(op, nullptr);

    CLAW_ASSERT(result.best_result.is_valid);
    CLAW_ASSERT(result.trials_conducted > 0);
    CLAW_ASSERT(result.best_result.measured_time_ms > 0);

    delete op->inputs[0];
    delete op->inputs[1];
    delete op->outputs[0];
    delete op;
    return claw::test::TestStatus::Pass;
}

CLAW_TEST(VMEvaluator_TensorOpCompiler_Error) {
    TensorOpCompiler compiler;

    auto mod = compiler.compile(nullptr, ScheduleConfig{}, nullptr);
    CLAW_ASSERT_FALSE(compiler.get_last_error().empty());

    TensorOp unsupported_op(TensorOp::OpKind::Cast, "cast");
    mod = compiler.compile(&unsupported_op, ScheduleConfig{}, nullptr);
    CLAW_ASSERT_FALSE(compiler.get_last_error().empty());
    return claw::test::TestStatus::Pass;
}

// ============================================================================
// 测试入口
// ============================================================================

int main() {
    std::cout << "========================================\n";
    std::cout << "Claw VM Evaluator Unit Tests\n";
    std::cout << "========================================\n\n";

    claw::test::TestRunner runner;
    bool success = runner.run_all();

    auto& registry = claw::test::TestRegistry::instance();
    auto& stats = registry.get_stats();

    std::cout << "\n========================================\n";
    std::cout << "Results: " << stats.passed << " passed, "
              << stats.failed << " failed\n";
    std::cout << "========================================\n";

    return success ? 0 : 1;
}
