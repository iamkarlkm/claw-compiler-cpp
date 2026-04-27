// lsp_server.h - Claw Language Server
#ifndef CLAW_LSP_SERVER_H
#define CLAW_LSP_SERVER_H

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <optional>
#include <iostream>
#include <sstream>
#include "lsp_protocol.h"
#include "../lexer/lexer.h"
#include "../parser/parser.h"
#include "../common/common.h"

namespace claw {
namespace lsp {

// ============================================================================
// 文档状态
// ============================================================================

struct DocumentState {
    std::string uri;
    std::string text;
    int version = 0;
    
    std::vector<Token> tokens;
    std::shared_ptr<ast::Program> ast;
    std::vector<Diagnostic> diagnostics;
    bool parsed = false;
    bool has_errors = false;
};

// ============================================================================
// 符号定义
// ============================================================================

struct SymbolDefinition {
    std::string name;
    std::string kind;
    std::string type;
    Location location;
    std::optional<Location> definition_location;
    std::optional<std::string> documentation;
};

// ============================================================================
// LSP 服务器
// ============================================================================

class LSPServer {
public:
    LSPServer();
    ~LSPServer();
    
    void run();
    void shutdown();
    
private:
    bool running_ = false;
    bool initialized_ = false;
    bool shutdown_requested_ = false;
    int sequence_number_ = 0;
    
    std::unordered_map<std::string, std::shared_ptr<DocumentState>> documents_;
    std::mutex documents_mutex_;
    
    std::unordered_map<std::string, std::vector<SymbolDefinition>> symbols_cache_;
    std::mutex symbols_mutex_;
    
    std::unordered_set<std::string> pending_diagnostics_;
    std::mutex diagnostics_mutex_;
    
    // ============================================================================
    // 消息处理
    // ============================================================================
    
    std::optional<JsonRpcRequest> parseRequest(const std::string& line);
    std::optional<JsonRpcNotification> parseNotification(const std::string& line);
    
    JsonRpcResponse handleRequest(const JsonRpcRequest& request);
    void handleNotification(const JsonRpcNotification& notification);
    
    void sendResponse(const JsonRpcResponse& response);
    void sendNotification(const std::string& method, const std::shared_ptr<JsonValue>& params);
    
    // ============================================================================
    // LSP 请求处理
    // ============================================================================
    
    std::shared_ptr<JsonValue> handleInitialize(const std::shared_ptr<JsonValue>& params);
    std::shared_ptr<JsonValue> handleShutdown(const std::shared_ptr<JsonValue>& params);
    
    void handleTextDocumentDidOpen(const std::shared_ptr<JsonValue>& params);
    void handleTextDocumentDidChange(const std::shared_ptr<JsonValue>& params);
    void handleTextDocumentDidSave(const std::shared_ptr<JsonValue>& params);
    void handleTextDocumentDidClose(const std::shared_ptr<JsonValue>& params);
    
    std::shared_ptr<JsonValue> handleHover(const std::shared_ptr<JsonValue>& params);
    std::shared_ptr<JsonValue> handleDefinition(const std::shared_ptr<JsonValue>& params);
    std::shared_ptr<JsonValue> handleCompletion(const std::shared_ptr<JsonValue>& params);
    std::shared_ptr<JsonValue> handleReferences(const std::shared_ptr<JsonValue>& params);
    std::shared_ptr<JsonValue> handleRename(const std::shared_ptr<JsonValue>& params);
    std::shared_ptr<JsonValue> handleDocumentSymbol(const std::shared_ptr<JsonValue>& params);
    std::shared_ptr<JsonValue> handleWorkspaceSymbol(const std::shared_ptr<JsonValue>& params);
    std::shared_ptr<JsonValue> handleCodeAction(const std::shared_ptr<JsonValue>& params);
    std::shared_ptr<JsonValue> handleSemanticTokens(const std::shared_ptr<JsonValue>& params);
    std::shared_ptr<JsonValue> handleInlayHint(const std::shared_ptr<JsonValue>& params);
    std::shared_ptr<JsonValue> handleDocumentFormatting(const std::shared_ptr<JsonValue>& params);
    std::shared_ptr<JsonValue> handleDocumentRangeFormatting(const std::shared_ptr<JsonValue>& params);
    
    // ============================================================================
    // 文档解析
    // ============================================================================
    
    std::shared_ptr<DocumentState> parseDocument(const std::string& uri, const std::string& text);
    
    std::vector<Token> lexSource(const std::string& source, DiagnosticReporter& reporter);
    std::shared_ptr<ast::Program> parseTokens(const std::vector<Token>& tokens, DiagnosticReporter& reporter);
    std::vector<Diagnostic> generateDiagnostics(const std::string& uri, const std::vector<Token>& tokens, const std::shared_ptr<ast::Program>& ast);
    void sendDiagnostics(const std::string& uri, const std::vector<Diagnostic>& diagnostics);
    
    // ============================================================================
    // 符号解析
    // ============================================================================
    
    std::vector<SymbolDefinition> extractSymbols(const std::shared_ptr<ast::Program>& ast, const std::string& uri);
    void extractSymbolsFromStatement(ast::Statement* stmt, const std::string& uri, std::vector<SymbolDefinition>& symbols);
    void extractSymbolsFromBlock(const ast::BlockStmt* block, const std::string& uri, std::vector<SymbolDefinition>& symbols);
    std::optional<Location> findDefinition(const std::string& uri, const Position& position);
    
    // ============================================================================
    // 工具函数
    // ============================================================================
    
    std::string uriToPath(const std::string& uri);
    std::string pathToUri(const std::string& path);
    
    Position offsetToPosition(const std::string& text, size_t offset);
    size_t positionToOffset(const std::string& text, const Position& position);
    
    std::optional<std::string> getWordAt(const std::string& text, const Position& position);
    
    static const std::unordered_set<std::string>& getKeywords();
    static const std::unordered_set<std::string>& getBuiltins();
};

} // namespace lsp
} // namespace claw

#endif // CLAW_LSP_SERVER_H
