// debugger_main.cpp - Standalone debugger entry point
// Usage: claw-debugger <source_file>

#include "debugger/claw_debugger_cli.h"
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <source_file.claw>\n";
        return 1;
    }

    auto cli = claw::debugger::create_debugger_cli();
    return cli->run_interactive(argv[1]);
}
