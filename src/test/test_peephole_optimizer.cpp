// test/test_peephole_optimizer.cpp - Unit tests for bytecode peephole optimization

#include "../optimizer/peephole_optimizer.h"
#include "../bytecode/bytecode.h"
#include "test.h"

using namespace claw;

CLAW_TEST_SUITE(PeepholeOptimizer);

CLAW_TEST(push_pop_removal) {
    bytecode::Module mod;
    bytecode::Function func;
    func.code = {
        bytecode::Instruction::PUSH(0),
        bytecode::Instruction(bytecode::OpCode::POP, 0),
        bytecode::Instruction::RET_NULL()
    };
    mod.functions.push_back(func);

    optimizer::PeepholeStats stats;
    bool changed = optimizer::optimize_peephole(mod, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.instructions_removed, 2);
    CLAW_ASSERT_EQ(mod.functions[0].code.size(), 1);
    CLAW_ASSERT(mod.functions[0].code[0].op == bytecode::OpCode::RET_NULL);

    return test::TestStatus::Pass;
}

CLAW_TEST(dup_pop_removal) {
    bytecode::Module mod;
    bytecode::Function func;
    func.code = {
        bytecode::Instruction(bytecode::OpCode::DUP, 0),
        bytecode::Instruction(bytecode::OpCode::POP, 0),
        bytecode::Instruction::RET_NULL()
    };
    mod.functions.push_back(func);

    optimizer::PeepholeStats stats;
    bool changed = optimizer::optimize_peephole(mod, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.instructions_removed, 2);
    CLAW_ASSERT_EQ(mod.functions[0].code.size(), 1);

    return test::TestStatus::Pass;
}

CLAW_TEST(load_store_same_local) {
    bytecode::Module mod;
    bytecode::Function func;
    func.code = {
        bytecode::Instruction::LOAD_LOCAL(3),
        bytecode::Instruction::STORE_LOCAL(3),
        bytecode::Instruction::RET_NULL()
    };
    mod.functions.push_back(func);

    optimizer::PeepholeStats stats;
    bool changed = optimizer::optimize_peephole(mod, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.instructions_removed, 2);
    CLAW_ASSERT_EQ(mod.functions[0].code.size(), 1);

    return test::TestStatus::Pass;
}

CLAW_TEST(double_not_removal) {
    bytecode::Module mod;
    bytecode::Function func;
    func.code = {
        bytecode::Instruction(bytecode::OpCode::NOT, 0),
        bytecode::Instruction(bytecode::OpCode::NOT, 0),
        bytecode::Instruction::RET_NULL()
    };
    mod.functions.push_back(func);

    optimizer::PeepholeStats stats;
    bool changed = optimizer::optimize_peephole(mod, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.instructions_removed, 2);
    CLAW_ASSERT_EQ(mod.functions[0].code.size(), 1);

    return test::TestStatus::Pass;
}

CLAW_TEST(nop_removal) {
    bytecode::Module mod;
    bytecode::Function func;
    func.code = {
        bytecode::Instruction::NOP(),
        bytecode::Instruction::RET_NULL()
    };
    mod.functions.push_back(func);

    optimizer::PeepholeStats stats;
    bool changed = optimizer::optimize_peephole(mod, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.instructions_removed, 1);
    CLAW_ASSERT_EQ(mod.functions[0].code.size(), 1);

    return test::TestStatus::Pass;
}

CLAW_TEST(jump_target_update) {
    bytecode::Module mod;
    bytecode::Function func;
    func.code = {
        bytecode::Instruction(bytecode::OpCode::JMP, 3),  // jump to instruction 3
        bytecode::Instruction::NOP(),
        bytecode::Instruction::NOP(),
        bytecode::Instruction::RET_NULL()
    };
    mod.functions.push_back(func);

    optimizer::PeepholeStats stats;
    bool changed = optimizer::optimize_peephole(mod, &stats);

    CLAW_ASSERT(changed);
    CLAW_ASSERT_EQ(stats.instructions_removed, 2);  // two NOPs
    CLAW_ASSERT_EQ(mod.functions[0].code.size(), 2);
    // Jump target should be remapped from 3 to 1
    CLAW_ASSERT_EQ(mod.functions[0].code[0].operand, 1);
    CLAW_ASSERT(mod.functions[0].code[1].op == bytecode::OpCode::RET_NULL);

    return test::TestStatus::Pass;
}

CLAW_TEST(no_change) {
    bytecode::Module mod;
    bytecode::Function func;
    func.code = {
        bytecode::Instruction::PUSH(0),
        bytecode::Instruction::RET_NULL()
    };
    mod.functions.push_back(func);

    optimizer::PeepholeStats stats;
    bool changed = optimizer::optimize_peephole(mod, &stats);

    CLAW_ASSERT_FALSE(changed);
    CLAW_ASSERT_EQ(stats.instructions_removed, 0);
    CLAW_ASSERT_EQ(mod.functions[0].code.size(), 2);

    return test::TestStatus::Pass;
}

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "Claw Peephole Optimizer Tests\n";
    std::cout << "========================================\n\n";
    return claw::test::run_tests(argc, argv);
}
