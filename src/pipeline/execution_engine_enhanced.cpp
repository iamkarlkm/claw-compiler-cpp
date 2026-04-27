// execution_engine_enhanced.cpp - ExecutionEngine 增强版
// 集成语义分析和类型检查到编译器流水线

#include "pipeline/execution_engine.h"
#include "semantic/semantic_analyzer.h"
#include "type/type_system.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "ast/ast.h"
#include "bytecode/bytecode_compiler.h"
#include "vm/claw_vm.h"
#include "jit/jit_compiler.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

namespace claw {

// ============================================================================
// SemanticTypeCheckIntegration - 语义分析和类型检查集成类
// ============================================================================

class SemanticTypeCheckIntegration {
public:
    struct CheckResult {
        bool success = false;
        std::vector<std::string> semantic_errors;
        std::vector<std::string> type_errors;
        std::vector<std::string> warnings;
        std::shared_ptr<semantic::SymbolTable> symbol_table;
        std::unordered_map<std::string, type::TypePtr> inferred_types;
        std::chrono::microseconds analysis_time;
        std::chrono::microseconds typecheck_time;
    };
    
    static CheckResult perform_full_check(ast::Program* program) {
        CheckResult result;
        if (!program) {
            result.semantic_errors.push_back("Program is null");
            return result;
        }
        
        auto sem_start = std::chrono::steady_clock::now();
        semantic::SemanticAnalyzer analyzer;
        auto sem_result = analyzer.analyze(program);
        auto sem_end = std::chrono::steady_clock::now();
        result.analysis_time = std::chrono::duration_cast<std::chrono::microseconds>(
            sem_end - sem_start);
        
        for (const auto& diag : sem_result.diagnostics) {
            std::string msg;
            switch (diag.kind) {
                case semantic::DiagnosticKind::Error:
                    msg = "[Semantic Error] " + diag.message;
                    result.semantic_errors.push_back(msg);
                    break;
                case semantic::DiagnosticKind::Warning:
                    msg = "[Warning] " + diag.message;
                    result.warnings.push_back(msg);
                    break;
                case semantic::DiagnosticKind::Note:
                    msg = "[Note] " + diag.message;
                    result.warnings.push_back(msg);
                    break;
            }
        }
        
        result.symbol_table = sem_result.symbol_table;
        
        if (!sem_result.success) {
            return result;
        }
        
        auto type_start = std::chrono::steady_clock::now();
        type::TypeChecker checker;
        checker.check(*program);
        auto type_end = std::chrono::steady_clock::now();
        result.typecheck_time = std::chrono::duration_cast<std::chrono::microseconds>(
            type_end - type_start);
        
        for (const auto& err : checker.errors()) {
            result.type_errors.push_back("[Type Error] " + std::string(err.what()));
        }
        
        if (result.type_errors.empty()) {
            result.success = true;
        }
        
        return result;
    }
    
    static CheckResult perform_fast_check(ast::Program* program) {
        CheckResult result;
        if (!program) {
            result.semantic_errors.push_back("Program is null");
            return result;
        }
        
        semantic::SemanticAnalyzer analyzer;
        auto sem_result = analyzer.analyze(program);
        
        for (const auto& diag : sem_result.diagnostics) {
            if (diag.kind == semantic::DiagnosticKind::Error) {
                result.semantic_errors.push_back("[Semantic] " + diag.message);
            } else {
                result.warnings.push_back("[" + diagnostic_kind_name(diag.kind) + "] " + diag.message);
            }
        }
        
        result.symbol_table = sem_result.symbol_table;
        
        type::TypeChecker checker;
        checker.check(*program);
        
        for (const auto& err : checker.errors()) {
            result.type_errors.push_back("[Type] " + std::string(err.what()));
        }
        
        if (result.semantic_errors.empty() && result.type_errors.empty()) {
            result.success = true;
        }
        
        return result;
    }

private:
    static std::string diagnostic_kind_name(semantic::DiagnosticKind kind) {
        switch (kind) {
            case semantic::DiagnosticKind::Error: return "Error";
            case semantic::DiagnosticKind::Warning: return "Warning";
            case semantic::DiagnosticKind::Note: return "Note";
            default: return "Unknown";
        }
    }
};

// ============================================================================
// ExecutionEngine 增强方法
// ============================================================================

bool ExecutionEngine::load_source_with_checks(const std::string& source, 
                                               void* check_result) {
    last_error_.clear();
    source_loaded_ = false;
    
    std::vector<Token> tokens;
    try {
        Lexer lexer(source);
        tokens = lexer.scan_all();
    } catch (const std::exception& e) {
        last_error_ = std::string("Lexer error: ") + e.what();
        return false;
    }
    
    std::unique_ptr<ast::Program> program;
    try {
        Parser parser(tokens);
        program = parser.parse();
        if (!program) {
            last_error_ = "Parsed AST is not a Program";
            return false;
        }
    } catch (const std::exception& e) {
        last_error_ = std::string("Parser error: ") + e.what();
        return false;
    }
    
    auto result = SemanticTypeCheckIntegration::perform_full_check(program.get());
    
    if (check_result) {
        *static_cast<SemanticTypeCheckIntegration::CheckResult*>(check_result) = result;
    }
    
    if (!result.success) {
        std::ostringstream oss;
        oss << "Compilation failed:\n";
        if (!result.semantic_errors.empty()) {
            oss << "\n=== Semantic Errors ===\n";
            for (const auto& err : result.semantic_errors) {
                oss << "  " << err << "\n";
            }
        }
        if (!result.type_errors.empty()) {
            oss << "\n=== Type Errors ===\n";
            for (const auto& err : result.type_errors) {
                oss << "  " << err << "\n";
            }
        }
        last_error_ = oss.str();
        return false;
    }
    
    try {
        BytecodeCompiler compiler;
        compiled_module_ = compiler.compile(*program);
        if (!compiled_module_) {
            last_error_ = "Bytecode compilation failed";
            return false;
        }
    } catch (const std::exception& e) {
        last_error_ = std::string("Bytecode compilation error: ") + e.what();
        return false;
    }
    
    if (vm_) {
        vm_->load_module(*compiled_module_);
    }
    
    source_loaded_ = true;
    return true;
}

std::string ExecutionEngine::get_compilation_diagnostics(const std::string& source) {
    std::ostringstream oss;
    
    std::vector<Token> tokens;
    try {
        Lexer lexer(source);
        tokens = lexer.scan_all();
        oss << "[LEXER] Success: " << tokens.size() << " tokens generated\n";
    } catch (const std::exception& e) {
        oss << "[LEXER] Error: " << e.what() << "\n";
        return oss.str();
    }
    
    std::unique_ptr<ast::Program> program;
    try {
        Parser parser(tokens);
        program = parser.parse();
        if (program) {
            oss << "[PARSER] Success: AST generated\n";
        } else {
            oss << "[PARSER] Error: AST is not a Program\n";
            return oss.str();
        }
    } catch (const std::exception& e) {
        oss << "[PARSER] Error: " << e.what() << "\n";
        return oss.str();
    }
    
    auto check_result = SemanticTypeCheckIntegration::perform_full_check(program.get());
    
    oss << "[SEMANTIC] " << (check_result.semantic_errors.empty() ? "Success" : "Failed") 
        << " (" << check_result.semantic_errors.size() << " errors, " 
        << check_result.warnings.size() << " warnings)\n";
    
    if (!check_result.semantic_errors.empty()) {
        for (const auto& err : check_result.semantic_errors) {
            oss << "  " << err << "\n";
        }
    }
    
    oss << "[TYPE CHECK] " << (check_result.type_errors.empty() ? "Success" : "Failed")
        << " (" << check_result.type_errors.size() << " errors)\n";
    
    if (!check_result.type_errors.empty()) {
        for (const auto& err : check_result.type_errors) {
            oss << "  " << err << "\n";
        }
    }
    
    oss << "[TIMING] Semantic: " << check_result.analysis_time.count() / 1000.0 
        << " ms, TypeCheck: " << check_result.typecheck_time.count() / 1000.0 << " ms\n";
    
    return oss.str();
}

} // namespace claw
