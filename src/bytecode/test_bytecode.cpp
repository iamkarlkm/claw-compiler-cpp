// Test program for Claw Bytecode
#include "bytecode.h"
#include <iostream>

using namespace claw::bytecode;

int main() {
    std::cout << "=== Claw Bytecode Module Test ===\n\n";

    // Create a test module
    Module mod("test_module");

    // Add constants
    mod.constants.add_integer(42);
    mod.constants.add_integer(100);
    mod.constants.add_float(3.14);
    mod.constants.add_float(2.718);
    mod.constants.add_string("Hello");
    mod.constants.add_string("World");

    // Create a test function
    Function func(0, "main", 0);
    func.local_count = 2;
    func.local_names = {"a", "result"};

    // Build bytecode: let result = 42 + 100
    // PUSH 42 (const index 0)
    // PUSH 100 (const index 1)
    // IADD
    // STORE_LOCAL 0 (result)
    // LOAD_LOCAL 0
    // PRINTLN
    // RET_NULL

    func.code.push_back(Instruction::PUSH(0));      // PUSH 42
    func.code.push_back(Instruction::PUSH(1));      // PUSH 100
    func.code.push_back(Instruction(OpCode::IADD, 0));
    func.code.push_back(Instruction::STORE_LOCAL(0)); // STORE_LOCAL result
    func.code.push_back(Instruction::LOAD_LOCAL(0));  // LOAD_LOCAL result
    func.code.push_back(Instruction(OpCode::PRINTLN, 0));
    func.code.push_back(Instruction::RET_NULL());

    mod.add_function(func);

    // Add global
    mod.add_global("global_var", Value::integer(999));

    std::cout << "Module created with:\n";
    std::cout << "  - Constants: " << mod.constants.integers.size() << " integers, "
              << mod.constants.floats.size() << " floats, "
              << mod.constants.strings.size() << " strings\n";
    std::cout << "  - Functions: " << mod.functions.size() << "\n";
    std::cout << "  - Globals: " << mod.global_names.size() << "\n\n";

    // Write to file
    BytecodeWriter writer;
    writer.write_module(mod);
    writer.write_to_file("/tmp/test.claw");

    std::cout << "Bytecode written to /tmp/test.claw\n";
    std::cout << "File size: " << writer.get_buffer().size() << " bytes\n\n";

    // Read back
    BytecodeReader reader;
    auto loaded = reader.read_from_file("/tmp/test.claw");

    if (loaded) {
        std::cout << "Bytecode read successfully!\n";
        std::cout << "Loaded module: " << loaded->name << "\n\n";

        // Disassemble
        Disassembler dis;
        std::cout << dis.disassemble(*loaded);
    } else {
        std::cout << "Error reading bytecode: " << reader.get_error() << "\n";
    }

    std::cout << "\n=== Test Complete ===\n";
    return 0;
}
