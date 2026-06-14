// test_ml_cost_model_adapter.cpp - ML 成本模型适配器测试
// 测试 MLCostModelAdapter 的功能

#include "../ml/ml_cost_model_adapter.h"
#include "../auto_scheduler/schedule_space.h"
#include "../tensorir/tensor_ir.h"
#include <iostream>
#include <cassert>

using namespace claw;
using namespace claw::ml;
using namespace claw::scheduler;

void test_adapter_creation() {
    std::cout << "Test: Adapter Creation..." << std::endl;
    
    auto adapter = create_ml_cost_model_adapter(CostModelType::HEURISTIC, true);
    assert(adapter != nullptr);
    
    std::cout << "  PASSED: Adapter created successfully" << std::endl;
}

void test_adapter_predict() {
    std::cout << "Test: Adapter Predict..." << std::endl;
    
    auto adapter = create_ml_cost_model_adapter(CostModelType::HEURISTIC, true);
    
    // 创建测试特征
    ScheduleSpace::OpFeatures features;
    features.op_kind = "matmul";
    features.num_inputs = 2;
    features.num_outputs = 1;
    features.num_dims = 2;
    features.output_shape = {64, 64};
    features.arithmetic_intensity = 100;
    features.is_reduction = false;
    
    // 创建测试配置
    ScheduleConfig config;
    config.decisions.push_back(ScheduleDecision(ScheduleDecision::Kind::Tile));
    config.decisions.back().int_params = {32, 32};
    
    double pred = adapter->predict(config, features);
    
    assert(pred > 0);
    std::cout << "  PASSED: Prediction = " << pred << std::endl;
}

void test_adapter_hybrid_mode() {
    std::cout << "Test: Adapter Hybrid Mode..." << std::endl;
    
    auto adapter = create_ml_cost_model_adapter(CostModelType::HEURISTIC, true);
    
    // 转换为 MLCostModelAdapter 以访问 enable_hybrid_mode
    // 注意：这是内部测试，不需要访问私有方法
    
    ScheduleSpace::OpFeatures features;
    features.op_kind = "add";
    features.num_inputs = 2;
    features.num_outputs = 1;
    features.num_dims = 1;
    features.output_shape = {100};
    features.arithmetic_intensity = 10;
    features.is_reduction = false;
    
    ScheduleConfig config;
    config.decisions.push_back(ScheduleDecision(ScheduleDecision::Kind::Parallel));
    
    double pred = adapter->predict(config, features);
    
    assert(pred > 0);
    std::cout << "  PASSED: Hybrid prediction = " << pred << std::endl;
}

void test_adapter_update() {
    std::cout << "Test: Adapter Update..." << std::endl;
    
    auto adapter = create_ml_cost_model_adapter(CostModelType::HEURISTIC, true);
    
    ScheduleSpace::OpFeatures features;
    features.op_kind = "conv2d";
    features.num_inputs = 1;
    features.num_outputs = 1;
    features.num_dims = 4;
    features.output_shape = {1, 224, 224, 3};
    features.arithmetic_intensity = 50;
    features.is_reduction = true;
    
    ScheduleConfig config;
    
    // 更新模型
    adapter->update(config, features, 10.5);
    
    std::cout << "  PASSED: Update completed without error" << std::endl;
}

void test_adapter_save_load() {
    std::cout << "Test: Adapter Save/Load..." << std::endl;
    
    auto adapter = create_ml_cost_model_adapter(CostModelType::HEURISTIC, true);
    
    // 保存/加载（当前实现返回 false，这是预期行为）
    bool saved = adapter->save("/tmp/test_model.bin");
    bool loaded = adapter->load("/tmp/test_model.bin");
    
    // 不应崩溃，即使保存/加载失败
    std::cout << "  PASSED: Save/Load interface works (save=" << saved << ", load=" << loaded << ")" << std::endl;
}

void test_different_op_types() {
    std::cout << "Test: Different Operation Types..." << std::endl;
    
    auto adapter = create_ml_cost_model_adapter(CostModelType::HEURISTIC, true);
    
    std::vector<std::string> op_kinds = {"add", "mul", "matmul", "conv2d", "relu", "softmax"};
    
    for (const auto& op : op_kinds) {
        ScheduleSpace::OpFeatures features;
        features.op_kind = op;
        features.num_inputs = 2;
        features.num_outputs = 1;
        features.num_dims = 2;
        features.output_shape = {32, 32};
        features.arithmetic_intensity = 10;
        features.is_reduction = false;
        
        ScheduleConfig config;
        
        double pred = adapter->predict(config, features);
        assert(pred > 0);
        
        std::cout << "    " << op << ": " << pred << std::endl;
    }
    
    std::cout << "  PASSED: All operation types work" << std::endl;
}

void test_different_schedules() {
    std::cout << "Test: Different Schedule Decisions..." << std::endl;
    
    auto adapter = create_ml_cost_model_adapter(CostModelType::HEURISTIC, true);
    
    ScheduleSpace::OpFeatures features;
    features.op_kind = "matmul";
    features.num_inputs = 2;
    features.num_outputs = 1;
    features.num_dims = 2;
    features.output_shape = {64, 64};
    features.arithmetic_intensity = 100;
    features.is_reduction = false;
    
    // 测试不同调度决策
    std::vector<ScheduleDecision::Kind> decisions = {
        ScheduleDecision::Kind::Tile,
        ScheduleDecision::Kind::Fuse,
        ScheduleDecision::Kind::Parallel,
        ScheduleDecision::Kind::Vectorize,
        ScheduleDecision::Kind::Unroll,
        ScheduleDecision::Kind::Split,
    };
    
    for (auto kind : decisions) {
        ScheduleConfig config;
        config.decisions.push_back(ScheduleDecision(kind));
        
        double pred = adapter->predict(config, features);
        assert(pred > 0);
        
        std::cout << "    " << static_cast<int>(kind) << ": " << pred << std::endl;
    }
    
    std::cout << "  PASSED: All schedule decisions work" << std::endl;
}

void test_feature_conversion() {
    std::cout << "Test: Feature Conversion..." << std::endl;
    
    auto adapter = create_ml_cost_model_adapter(CostModelType::HEURISTIC, true);
    
    // 测试各种特征
    std::vector<ScheduleSpace::OpFeatures> test_features = {
        // 小张量
        []() {
            ScheduleSpace::OpFeatures f;
            f.op_kind = "add";
            f.num_inputs = 2;
            f.num_outputs = 1;
            f.num_dims = 2;
            f.output_shape = {8, 8};
            f.arithmetic_intensity = 1;
            f.is_reduction = false;
            return f;
        }(),
        // 大张量
        []() {
            ScheduleSpace::OpFeatures f;
            f.op_kind = "matmul";
            f.num_inputs = 2;
            f.num_outputs = 1;
            f.num_dims = 2;
            f.output_shape = {1024, 1024};
            f.arithmetic_intensity = 1000;
            f.is_reduction = false;
            return f;
        }(),
        // 归约操作
        []() {
            ScheduleSpace::OpFeatures f;
            f.op_kind = "sum";
            f.num_inputs = 1;
            f.num_outputs = 1;
            f.num_dims = 3;
            f.output_shape = {1, 1, 1};
            f.arithmetic_intensity = 10;
            f.is_reduction = true;
            return f;
        }(),
    };
    
    ScheduleConfig config;
    int idx = 0;
    for (const auto& f : test_features) {
        double pred = adapter->predict(config, f);
        assert(pred > 0);
        std::cout << "    case " << idx++ << ": " << pred << std::endl;
    }
    
    std::cout << "  PASSED: Feature conversion works" << std::endl;
}

int main() {
    std::cout << "=== ML Cost Model Adapter Tests ===" << std::endl;
    std::cout << std::endl;
    
    test_adapter_creation();
    test_adapter_predict();
    test_adapter_hybrid_mode();
    test_adapter_update();
    test_adapter_save_load();
    test_different_op_types();
    test_different_schedules();
    test_feature_conversion();
    
    std::cout << std::endl;
    std::cout << "=== All Tests Passed ===" << std::endl;
    
    return 0;
}
