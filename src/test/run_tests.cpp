// Claw Compiler - Test Runner Main Entry

#include <iostream>
#include "test/test.h"
#include "test/test_lexer.cpp"
#include "test/test_parser.cpp"

int main(int argc, char* argv[]) {
    std::cout << "Claw Compiler Test Suite\n";
    std::cout << "=========================\n";
    
    return claw::test::run_tests(argc, argv);
}
