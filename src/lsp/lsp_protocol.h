// lsp_protocol.h - Language Server Protocol 定义
#ifndef CLAW_LSP_PROTOCOL_H
#define CLAW_LSP_PROTOCOL_H

#include <string>
#include <vector>
#include <optional>
#include <map>
#include <memory>

namespace claw {
namespace lsp {

// ============================================================================
// JSON-RPC 2.0 类型 (使用指针避免递归)
// ============================================================================

struct JsonValue {
    enum class Kind { Null, Bool, Int, Double, String, Array, Object };
    Kind kind = Kind::Null;
    
    // 值存储
    bool bool_val = false;
    int int_val = 0;
    double double_val = 0.0;
    std::string string_val;
    std::vector<std::shared_ptr<JsonValue>> array_val;
    std::map<std::string, std::shared_ptr<JsonValue>> object_val;
    
    // 构造函数
    JsonValue() : kind(Kind::Null) {}
    JsonValue(std::nullptr_t) : kind(Kind::Null) {}
    JsonValue(bool v) : kind(Kind::Bool), bool_val(v) {}
    JsonValue(int v) : kind(Kind::Int), int_val(v) {}
    JsonValue(double v) : kind(Kind::Double), double_val(v) {}
    JsonValue(const std::string& v) : kind(Kind::String), string_val(v) {}
    JsonValue(const char* v) : kind(Kind::String), string_val(v) {}
    
    static JsonValue Array(const std::vector<std::shared_ptr<JsonValue>>& v) {
        JsonValue j;
        j.kind = Kind::Array;
        j.array_val = v;
        return j;
    }
    
    static JsonValue Object(const std::map<std::string, std::shared_ptr<JsonValue>>& v) {
        JsonValue j;
        j.kind = Kind::Object;
        j.object_val = v;
        return j;
    }
    
    bool is_null() const { return kind == Kind::Null; }
    bool is_bool() const { return kind == Kind::Bool; }
    bool is_int() const { return kind == Kind::Int; }
    bool is_double() const { return kind == Kind::Double; }
    bool is_string() const { return kind == Kind::String; }
    bool is_array() const { return kind == Kind::Array; }
    bool is_object() const { return kind == Kind::Object; }
    
    bool as_bool() const { return bool_val; }
    int as_int() const { return int_val; }
    double as_double() const { return double_val; }
    const std::string& as_string() const { return string_val; }
    const std::vector<std::shared_ptr<JsonValue>>& as_array() const { return array_val; }
    const std::map<std::string, std::shared_ptr<JsonValue>>& as_object() const { return object_val; }
};

// 便捷函数
inline std::shared_ptr<JsonValue> json_null() { return std::make_shared<JsonValue>(nullptr); }
inline std::shared_ptr<JsonValue> json_bool(bool v) { return std::make_shared<JsonValue>(v); }
inline std::shared_ptr<JsonValue> json_int(int v) { return std::make_shared<JsonValue>(v); }
inline std::shared_ptr<JsonValue> json_double(double v) { return std::make_shared<JsonValue>(v); }
inline std::shared_ptr<JsonValue> json_string(const std::string& v) { return std::make_shared<JsonValue>(v); }
inline std::shared_ptr<JsonValue> json_string(const char* v) { return std::make_shared<JsonValue>(v); }
inline std::shared_ptr<JsonValue> json_array(const std::vector<std::shared_ptr<JsonValue>>& v) { return std::make_shared<JsonValue>(JsonValue::Array(v)); }
inline std::shared_ptr<JsonValue> json_object(const std::map<std::string, std::shared_ptr<JsonValue>>& v) { return std::make_shared<JsonValue>(JsonValue::Object(v)); }

// ============================================================================
// JSON-RPC 消息
// ============================================================================

struct JsonRpcRequest {
    std::string jsonrpc = "2.0";
    std::optional<std::string> id;
    std::string method;
    std::shared_ptr<JsonValue> params;
};

struct JsonRpcResponse {
    std::string jsonrpc = "2.0";
    std::optional<std::string> id;
    std::shared_ptr<JsonValue> result;
    std::shared_ptr<JsonValue> error;
};

struct JsonRpcNotification {
    std::string jsonrpc = "2.0";
    std::string method;
    std::shared_ptr<JsonValue> params;
};

// ============================================================================
// LSP 基础类型
// ============================================================================

struct Position {
    int line = 0;
    int character = 0;
};

struct Range {
    Position start;
    Position end;
};

struct Location {
    std::string uri;
    Range range;
};

struct Diagnostic {
    Range range;
    int severity = 1;
    std::string message;
    std::optional<std::string> source;
};

struct TextDocumentIdentifier {
    std::string uri;
};

struct TextDocumentItem {
    std::string uri;
    std::string languageId;
    int version = 0;
    std::string text;
};

struct VersionedTextDocumentIdentifier {
    std::string uri;
    int version = 0;
};

struct TextDocumentPositionParams {
    TextDocumentIdentifier textDocument;
    Position position;
};

struct TextDocumentChangeEvent {
    VersionedTextDocumentIdentifier textDocument;
    std::string text;
};

// ============================================================================
// LSP 请求参数
// ============================================================================

struct InitializeParams {
    std::optional<int> processId;
    std::optional<std::string> rootUri;
    std::optional<std::string> rootPath;
    std::shared_ptr<JsonValue> capabilities;
    std::shared_ptr<JsonValue> workspaceFolders;
};

struct HoverParams {
    TextDocumentIdentifier textDocument;
    Position position;
};

struct DefinitionParams {
    TextDocumentIdentifier textDocument;
    Position position;
};

struct CompletionParams {
    TextDocumentIdentifier textDocument;
    Position position;
    std::shared_ptr<JsonValue> context;
};

struct DidOpenTextDocumentParams {
    TextDocumentItem textDocument;
};

struct DidChangeTextDocumentParams {
    VersionedTextDocumentIdentifier textDocument;
    std::vector<TextDocumentChangeEvent> contentChanges;
};

struct DidSaveTextDocumentParams {
    TextDocumentIdentifier textDocument;
    std::optional<std::string> text;
};

struct DidCloseTextDocumentParams {
    TextDocumentIdentifier textDocument;
};

// ============================================================================
// 新增 LSP 请求参数 (References/Rename/DocumentSymbols)
// ============================================================================

struct ReferenceParams {
    TextDocumentIdentifier textDocument;
    Position position;
    std::shared_ptr<JsonValue> context;
};

struct RenameParams {
    TextDocumentIdentifier textDocument;
    Position position;
    std::string newName;
};

struct DocumentSymbolParams {
    TextDocumentIdentifier textDocument;
};

struct WorkspaceSymbolParams {
    std::string query;
};

struct CodeActionParams {
    TextDocumentIdentifier textDocument;
    Range range;
    std::shared_ptr<JsonValue> context;
};

struct SemanticTokensParams {
    TextDocumentIdentifier textDocument;
    std::optional<Range> range;
};

struct SemanticTokensFullParams {
    TextDocumentIdentifier textDocument;
    std::optional<Range> range;
    std::optional<std::string> workDoneToken;
};

struct InlayHintParams {
    TextDocumentIdentifier textDocument;
    Range range;
};

struct FormattingOptions {
    int tabSize = 4;
    bool insertSpaces = true;
    std::optional<std::string> indentStyle;
};

struct DocumentFormattingParams {
    TextDocumentIdentifier textDocument;
    FormattingOptions options;
};

struct DocumentRangeFormattingParams {
    TextDocumentIdentifier textDocument;
    Range range;
    FormattingOptions options;
};

// ============================================================================
// LSP 响应类型
// ============================================================================

struct Hover {
    std::optional<Range> range;
    std::shared_ptr<JsonValue> contents;
};

struct CompletionItem {
    std::string label;
    std::optional<int> kind;
    std::optional<std::string> detail;
    std::optional<std::string> documentation;
    std::optional<std::string> insertText;
    std::optional<int> insertTextFormat;
};

struct CompletionList {
    bool isIncomplete = false;
    std::vector<CompletionItem> items;
};

struct InitializeResult {
    std::shared_ptr<JsonValue> capabilities;
};

struct ServerCapabilities {
    std::optional<int> textDocumentSync;
    std::optional<bool> hoverProvider;
    std::optional<bool> definitionProvider;
    std::optional<bool> completionProvider;
    std::optional<bool> referencesProvider;
    std::optional<bool> renameProvider;
    std::optional<bool> documentSymbolProvider;
    std::optional<bool> workspaceSymbolProvider;
    std::optional<bool> codeActionProvider;
    std::optional<bool> semanticTokensProvider;
    std::optional<bool> inlayHintProvider;
    std::optional<bool> documentFormattingProvider;
    std::optional<bool> documentRangeFormattingProvider;
    std::shared_ptr<JsonValue> diagnosticsProvider;
};

// ============================================================================
// 新增响应类型
// ============================================================================

struct ReferenceContext {
    bool includeDeclaration = false;
};

struct LocationLink {
    std::optional<Location> originSelectionRange;
    Location targetUri;
    Range targetRange;
    Range targetSelectionRange;
};

struct SymbolInformation {
    std::string name;
    int kind = 0;
    std::optional<Location> location;
    std::optional<std::string> containerName;
};

// ============================================================================
// 响应类型定义 (补充新增类型)
// ============================================================================

struct TextEdit {
    Range range;
    std::string newText;
};

struct TextDocumentEdit {
    VersionedTextDocumentIdentifier textDocument;
    std::vector<TextEdit> edits;
};

struct Command {
    std::string title;
    std::string command;
    std::optional<std::vector<std::shared_ptr<JsonValue>>> arguments;
};

struct CodeAction {
    std::string title;
    std::optional<std::string> kind;
    std::optional<std::vector<TextDocumentEdit>> edit;
    std::optional<Command> command;
};

struct DocumentEdit {
    TextDocumentItem textDocument;
    std::vector<TextEdit> edits;
};

struct WorkspaceEdit {
    std::optional<std::map<std::string, std::vector<TextEdit>>> changes;
    std::optional<std::vector<DocumentEdit>> documentChanges;
};

struct DocumentSymbol {
    std::string name;
    std::optional<std::string> detail;
    int kind = 0;
    Range range;
    Range selectionRange;
    std::optional<std::vector<DocumentSymbol>> children;
};

struct SemanticToken {
    int line = 0;
    int startChar = 0;
    int length = 0;
    int tokenType = 0;
    int tokenModifiers = 0;
};

struct SemanticTokens {
    std::optional<std::string> resultId;
    std::vector<SemanticToken> data;
};

struct InlayHint {
    Position position;
    std::string label;
    std::optional<int> kind;
    std::optional<TextEdit> textEdit;
    std::optional<std::string> tooltip;
};

// ============================================================================
// JSON 辅助函数
// ============================================================================

std::string jsonEncode(const std::shared_ptr<JsonValue>& value);
std::shared_ptr<JsonValue> jsonDecode(const std::string& str);
std::shared_ptr<JsonValue> getObjectField(const std::shared_ptr<JsonValue>& obj, const std::string& field);
std::optional<std::string> getStringField(const std::shared_ptr<JsonValue>& obj, const std::string& field);
std::optional<int> getIntField(const std::shared_ptr<JsonValue>& obj, const std::string& field);

} // namespace lsp
} // namespace claw

#endif // CLAW_LSP_PROTOCOL_H
