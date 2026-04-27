// lsp_main.cpp - Claw Language Server Entry Point
#include "lsp_server.h"
#include <iostream>

int main(int argc, char* argv[]) {
    claw::lsp::LSPServer server;
    
    std::cerr << "Claw Language Server started..." << std::endl;
    server.run();
    
    return 0;
}
