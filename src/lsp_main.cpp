// lsp_main.cpp - Claw Language Server Main Entry
#include <iostream>
#include "lsp/lsp_server.h"

int main() {
    claw::lsp::LSPServer server;
    server.run();
    return 0;
}
