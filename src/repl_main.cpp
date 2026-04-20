// repl_main.cpp - Standalone REPL entry point
// Usage: claw-repl [options]

#include "repl/repl.h"

int main(int argc, char** argv) {
    return claw::repl::start_repl(argc, argv);
}
