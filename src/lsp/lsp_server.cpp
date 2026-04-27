// lsp_server.cpp - Claw Language Server 实现
#include "lsp_server.h"
#include <algorithm>
#include <fstream>
#include <filesystem>

namespace claw {
namespace lsp {

// ============================================================================
// 关键字和内置类型
// ============================================================================

const std::unordered_set<std::string>& LSPServer::getKeywords() {
    static const std::unordered_set<std::string> keywords = {
        "fn", "let", "const", "if", "else", "match", "for", "while", "loop",
        "return", "break", "continue", "pub", "mod", "use", "struct", "enum",
        "trait", "impl", "self", "Self", "true", "false", "nil", "async", "await",
        "serial", "process", "publish", "subscribe", "spawn", "tensor", "in"
    };
    return keywords;
}

const std::unordered_set<std::string>& LSPServer::getBuiltins() {
    static const std::unordered_set<std::string> builtins = {
        "print", "println", "panic", "input", "len", "range", "type", "int",
        "float", "string", "bool", "array", "tensor", "shape", "reshape"
    };
    return builtins;
}

// ============================================================================
// 构造/析构
// ============================================================================

LSPServer::LSPServer() = default;
LSPServer::~LSPServer() = default;

// ============================================================================
// 主循环
// ============================================================================

void LSPServer::run() {
    running_ = true;
    std::string line;
    
    while (running_ && std::getline(std::cin, line)) {
        if (line.empty()) continue;
        
        if (auto request = parseRequest(line)) {
            auto response = handleRequest(*request);
            if (request->id) {
                sendResponse(response);
            }
        } else if (auto notification = parseNotification(line)) {
            handleNotification(*notification);
        }
    }
}

void LSPServer::shutdown() {
    running_ = false;
    shutdown_requested_ = true;
}

// ============================================================================
// 消息解析
// ============================================================================

std::optional<JsonRpcRequest> LSPServer::parseRequest(const std::string& line) {
    try {
        auto json = jsonDecode(line);
        if (!json || !json->is_object()) return std::nullopt;
        
        JsonRpcRequest request;
        auto obj = json->as_object();
        
        if (obj.count("id")) {
            auto& id = obj["id"];
            if (id->is_int()) request.id = std::to_string(id->int_val);
            else if (id->is_string()) request.id = id->string_val;
        }
        
        if (obj.count("method")) {
            request.method = obj["method"]->string_val;
        }
        
        if (obj.count("params")) {
            request.params = obj["params"];
        }
        
        if (request.method.empty()) return std::nullopt;
        
        return request;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<JsonRpcNotification> LSPServer::parseNotification(const std::string& line) {
    try {
        auto json = jsonDecode(line);
        if (!json || !json->is_object()) return std::nullopt;
        
        auto obj = json->as_object();
        
        // notification 没有 id
        if (obj.count("id") && !obj["id"]->is_null()) return std::nullopt;
        
        JsonRpcNotification notification;
        if (obj.count("method")) {
            notification.method = obj["method"]->string_val;
        }
        if (obj.count("params")) {
            notification.params = obj["params"];
        }
        
        if (notification.method.empty()) return std::nullopt;
        
        return notification;
    } catch (...) {
        return std::nullopt;
    }
}

// ============================================================================
// 请求处理
// ============================================================================

JsonRpcResponse LSPServer::handleRequest(const JsonRpcRequest& request) {
    JsonRpcResponse response;
    response.id = request.id;
    
    try {
        if (request.method == "initialize") {
            response.result = handleInitialize(request.params);
        } else if (request.method == "shutdown") {
            response.result = handleShutdown(request.params);
        } else if (request.method == "textDocument/hover") {
            response.result = handleHover(request.params);
        } else if (request.method == "textDocument/definition") {
            response.result = handleDefinition(request.params);
        } else if (request.method == "textDocument/completion") {
            response.result = handleCompletion(request.params);
        } else if (request.method == "textDocument/references") {
            response.result = handleReferences(request.params);
        } else if (request.method == "textDocument/rename") {
            response.result = handleRename(request.params);
        } else if (request.method == "textDocument/documentSymbol") {
            response.result = handleDocumentSymbol(request.params);
        } else if (request.method == "workspace/symbol") {
            response.result = handleWorkspaceSymbol(request.params);
        } else if (request.method == "textDocument/codeAction") {
            response.result = handleCodeAction(request.params);
        } else if (request.method == "textDocument/semanticTokens/full") {
            response.result = handleSemanticTokens(request.params);
        } else if (request.method == "textDocument/inlayHint") {
            response.result = handleInlayHint(request.params);
        } else if (request.method == "textDocument/formatting") {
            response.result = handleDocumentFormatting(request.params);
        } else if (request.method == "textDocument/rangeFormatting") {
            response.result = handleDocumentRangeFormatting(request.params);
        } else if (request.method == "$/cancelRequest") {
            // 取消请求处理 (静默忽略)
        } else {
            response.error = json_object({{"code", json_int(-32601)}, {"message", json_string("Method not found: " + request.method)}});
        }
    } catch (const std::exception& e) {
        response.error = json_object({{"code", json_int(-32603)}, {"message", json_string(std::string("Internal error: ") + e.what())}});
    }
    
    return response;
}

void LSPServer::handleNotification(const JsonRpcNotification& notification) {
    try {
        if (notification.method == "initialized") {
            initialized_ = true;
        } else if (notification.method == "textDocument/didOpen") {
            handleTextDocumentDidOpen(notification.params);
        } else if (notification.method == "textDocument/didChange") {
            handleTextDocumentDidChange(notification.params);
        } else if (notification.method == "textDocument/didSave") {
            handleTextDocumentDidSave(notification.params);
        } else if (notification.method == "textDocument/didClose") {
            handleTextDocumentDidClose(notification.params);
        }
    } catch (const std::exception& e) {
        std::cerr << "Notification error: " << e.what() << std::endl;
    }
}

// ============================================================================
// 发送消息
// ============================================================================

void LSPServer::sendResponse(const JsonRpcResponse& response) {
    auto obj = std::map<std::string, std::shared_ptr<JsonValue>>{
        {"jsonrpc", json_string("2.0")}
    };
    
    if (response.id) obj["id"] = json_string(*response.id);
    if (response.result) obj["result"] = response.result;
    if (response.error) obj["error"] = response.error;
    
    std::cout << jsonEncode(json_object(obj)) << "\n" << std::flush;
}

void LSPServer::sendNotification(const std::string& method, const std::shared_ptr<JsonValue>& params) {
    auto obj = std::map<std::string, std::shared_ptr<JsonValue>>{
        {"jsonrpc", json_string("2.0")},
        {"method", json_string(method)}
    };
    if (params) obj["params"] = params;
    
    std::cout << jsonEncode(json_object(obj)) << "\n" << std::flush;
}

// ============================================================================
// LSP 请求处理
// ============================================================================

std::shared_ptr<JsonValue> LSPServer::handleInitialize(const std::shared_ptr<JsonValue>& params) {
    initialized_ = true;
    
    auto capabilities = json_object({
        {"textDocumentSync", json_int(1)},
        {"hoverProvider", json_bool(true)},
        {"definitionProvider", json_bool(true)},
        {"completionProvider", json_object({{"resolveProvider", json_bool(false)}})},
        {"referencesProvider", json_bool(true)},
        {"renameProvider", json_bool(true)},
        {"documentSymbolProvider", json_bool(true)},
        {"workspaceSymbolProvider", json_bool(true)},
        {"codeActionProvider", json_bool(true)},
        {"semanticTokensProvider", json_object({
            {"legend", json_object({
                {"tokenTypes", json_array({
                    json_string("namespace"), json_string("function"), json_string("variable"),
                    json_string("keyword"), json_string("string"), json_string("number"),
                    json_string("type"), json_string("class"), json_string("interface"),
                    json_string("parameter"), json_string("property"), json_string("enum")
                })},
                {"tokenModifiers", json_array({
                    json_string("declaration"), json_string("definition"), json_string("readonly"),
                    json_string("static"), json_string("deprecated"), json_string("abstract")
                })}
            })},
            {"range", json_bool(true)},
            {"full", json_bool(true)}
        })},
        {"inlayHintProvider", json_bool(true)},
        {"documentFormattingProvider", json_bool(true)},
        {"documentRangeFormattingProvider", json_bool(true)}
    });
    
    auto serverInfo = json_object({
        {"name", json_string("Claw Language Server")},
        {"version", json_string("0.1.0")}
    });
    
    return json_object({
        {"capabilities", capabilities},
        {"serverInfo", serverInfo}
    });
}

std::shared_ptr<JsonValue> LSPServer::handleShutdown(const std::shared_ptr<JsonValue>& params) {
    shutdown_requested_ = true;
    shutdown();
    return json_null();
}

std::shared_ptr<JsonValue> LSPServer::handleHover(const std::shared_ptr<JsonValue>& params) {
    auto textDocument = getObjectField(params, "textDocument");
    auto position = getObjectField(params, "position");
    if (!textDocument || !position) return json_null();
    
    auto uri = getStringField(textDocument, "uri");
    auto line = getIntField(position, "line");
    auto character = getIntField(position, "character");
    
    if (!uri || !line || !character) return json_null();
    
    std::lock_guard<std::mutex> lock(documents_mutex_);
    auto it = documents_.find(*uri);
    if (it == documents_.end()) return json_null();
    
    auto& doc = it->second;
    auto word = getWordAt(doc->text, Position{*line, *character});
    
    if (!word) return json_null();
    
    // 检查关键字
    if (getKeywords().count(*word)) {
        return json_object({
            {"contents", json_object({
                {"kind", json_string("markdown")},
                {"value", json_string("**Keyword**: `" + *word + "`")}
            })}
        });
    }
    
    // 检查内置函数
    if (getBuiltins().count(*word)) {
        return json_object({
            {"contents", json_object({
                {"kind", json_string("markdown")},
                {"value", json_string("**Builtin**: `" + *word + "`\n\nBuilt-in function.")}
            })}
        });
    }
    
    // 查找符号
    std::lock_guard<std::mutex> sym_lock(symbols_mutex_);
    auto sym_it = symbols_cache_.find(*uri);
    if (sym_it != symbols_cache_.end()) {
        for (const auto& sym : sym_it->second) {
            if (sym.name == *word) {
                return json_object({
                    {"contents", json_object({
                        {"kind", json_string("markdown")},
                        {"value", json_string("**" + sym.kind + "**: `" + sym.name + "`\n\nType: " + sym.type)}
                    })}
                });
            }
        }
    }
    
    return json_null();
}

std::shared_ptr<JsonValue> LSPServer::handleDefinition(const std::shared_ptr<JsonValue>& params) {
    auto textDocument = getObjectField(params, "textDocument");
    auto position = getObjectField(params, "position");
    if (!textDocument || !position) return json_null();
    
    auto uri = getStringField(textDocument, "uri");
    auto line = getIntField(position, "line");
    auto character = getIntField(position, "character");
    
    if (!uri || !line || !character) return json_null();
    
    auto location = findDefinition(*uri, Position{*line, *character});
    if (!location) return json_null();
    
    return json_object({
        {"uri", json_string(location->uri)},
        {"range", json_object({
            {"start", json_object({
                {"line", json_int(location->range.start.line)},
                {"character", json_int(location->range.start.character)}
            })},
            {"end", json_object({
                {"line", json_int(location->range.end.line)},
                {"character", json_int(location->range.end.character)}
            })}
        })}
    });
}

std::shared_ptr<JsonValue> LSPServer::handleCompletion(const std::shared_ptr<JsonValue>& params) {
    auto textDocument = getObjectField(params, "textDocument");
    auto position = getObjectField(params, "position");
    if (!textDocument || !position) return json_null();
    
    auto uri = getStringField(textDocument, "uri");
    
    if (!uri) return json_null();
    
    std::vector<std::shared_ptr<JsonValue>> items;
    
    // 添加关键字
    for (const auto& kw : getKeywords()) {
        items.push_back(json_object({
            {"label", json_string(kw)},
            {"kind", json_int(14)}
        }));
    }
    
    // 添加内置函数
    for (const auto& bi : getBuiltins()) {
        items.push_back(json_object({
            {"label", json_string(bi)},
            {"kind", json_int(3)},
            {"detail", json_string("Built-in function")}
        }));
    }
    
    // 添加当前文档的符号
    {
        std::lock_guard<std::mutex> sym_lock(symbols_mutex_);
        auto sym_it = symbols_cache_.find(*uri);
        if (sym_it != symbols_cache_.end()) {
            for (const auto& sym : sym_it->second) {
                int kind = 6;
                if (sym.kind == "function") kind = 3;
                else if (sym.kind == "type") kind = 7;
                
                items.push_back(json_object({
                    {"label", json_string(sym.name)},
                    {"kind", json_int(kind)},
                    {"detail", json_string(sym.type)}
                }));
            }
        }
    }
    
    return json_object({
        {"isIncomplete", json_bool(false)},
        {"items", json_array(items)}
    });
}

// ============================================================================
// 引用查找 (References)
// ============================================================================

std::shared_ptr<JsonValue> LSPServer::handleReferences(const std::shared_ptr<JsonValue>& params) {
    auto textDocument = getObjectField(params, "textDocument");
    auto position = getObjectField(params, "position");
    if (!textDocument || !position) return json_null();
    
    auto uri = getStringField(textDocument, "uri");
    auto line = getIntField(position, "line");
    auto character = getIntField(position, "character");
    
    if (!uri || !line || !character) return json_null();
    
    // 获取光标处的单词
    std::string targetWord;
    {
        std::lock_guard<std::mutex> lock(documents_mutex_);
        auto it = documents_.find(*uri);
        if (it == documents_.end()) return json_null();
        auto word = getWordAt(it->second->text, Position{*line, *character});
        if (!word) return json_null();
        targetWord = *word;
    }
    
    std::vector<std::shared_ptr<JsonValue>> references;
    
    // 在当前文档中查找所有引用
    {
        std::lock_guard<std::mutex> lock(documents_mutex_);
        std::lock_guard<std::mutex> sym_lock(symbols_mutex_);
        
        auto docIt = documents_.find(*uri);
        auto symIt = symbols_cache_.find(*uri);
        
        if (docIt != documents_.end() && symIt != symbols_cache_.end()) {
            const auto& text = docIt->second->text;
            const auto& symbols = symIt->second;
            
            // 找到符号定义位置
            std::optional<Location> defLocation;
            for (const auto& sym : symbols) {
                if (sym.name == targetWord && (sym.kind == "function" || sym.kind == "variable" || sym.kind == "parameter")) {
                    defLocation = sym.location;
                    break;
                }
            }
            
            // 遍历文本查找所有使用位置
            size_t pos = 0;
            while ((pos = text.find(targetWord, pos)) != std::string::npos) {
                // 检查是否是完整的单词
                bool isWord = true;
                if (pos > 0) {
                    char c = text[pos - 1];
                    if (std::isalnum(c) || c == '_') isWord = false;
                }
                if (pos + targetWord.size() < text.size()) {
                    char c = text[pos + targetWord.size()];
                    if (std::isalnum(c) || c == '_') isWord = false;
                }
                
                if (isWord) {
                    auto refPos = offsetToPosition(text, pos);
                    // 排除定义位置本身
                    if (!defLocation || refPos.line != defLocation->range.start.line || 
                        refPos.character != defLocation->range.start.character) {
                        references.push_back(json_object({
                            {"uri", json_string(*uri)},
                            {"range", json_object({
                                {"start", json_object({
                                    {"line", json_int(refPos.line)},
                                    {"character", json_int(refPos.character)}
                                })},
                                {"end", json_object({
                                    {"line", json_int(refPos.line)},
                                    {"character", json_int(refPos.character + (int)targetWord.size())}
                                })}
                            })}
                        }));
                    }
                }
                pos += targetWord.size();
            }
        }
    }
    
    return json_array(references);
}

// ============================================================================
// 重命名 (Rename)
// ============================================================================

std::shared_ptr<JsonValue> LSPServer::handleRename(const std::shared_ptr<JsonValue>& params) {
    auto textDocument = getObjectField(params, "textDocument");
    auto position = getObjectField(params, "position");
    auto newName = getStringField(params, "newName");
    
    if (!textDocument || !position || !newName) return json_null();
    
    auto uri = getStringField(textDocument, "uri");
    auto line = getIntField(position, "line");
    auto character = getIntField(position, "character");
    
    if (!uri || !line || !character) return json_null();
    
    // 获取要重命名的单词
    std::string oldName;
    {
        std::lock_guard<std::mutex> lock(documents_mutex_);
        auto it = documents_.find(*uri);
        if (it == documents_.end()) return json_null();
        auto word = getWordAt(it->second->text, Position{*line, *character});
        if (!word) return json_null();
        oldName = *word;
    }
    
    // 不能重命名关键字
    if (getKeywords().count(oldName) || getBuiltins().count(oldName)) {
        return json_null();
    }
    
    // 构建 workspace edit
    std::map<std::string, std::shared_ptr<JsonValue>> changesJson;
    {
        std::lock_guard<std::mutex> lock(documents_mutex_);
        std::lock_guard<std::mutex> sym_lock(symbols_mutex_);
        
        // 遍历所有打开的文档
        for (const auto& docPair : documents_) {
            const auto& docUri = docPair.first;
            const auto& text = docPair.second->text;
            
            std::vector<std::shared_ptr<JsonValue>> edits;
            size_t pos = 0;
            
            while ((pos = text.find(oldName, pos)) != std::string::npos) {
                // 检查是否是完整的单词
                bool isWord = true;
                if (pos > 0) {
                    char c = text[pos - 1];
                    if (std::isalnum(c) || c == '_') isWord = false;
                }
                if (pos + oldName.size() < text.size()) {
                    char c = text[pos + oldName.size()];
                    if (std::isalnum(c) || c == '_') isWord = false;
                }
                
                if (isWord) {
                    auto refPos = offsetToPosition(text, pos);
                    edits.push_back(json_object({
                        {"range", json_object({
                            {"start", json_object({
                                {"line", json_int(refPos.line)},
                                {"character", json_int(refPos.character)}
                            })},
                            {"end", json_object({
                                {"line", json_int(refPos.line)},
                                {"character", json_int(refPos.character + (int)oldName.size())}
                            })}
                        })},
                        {"newText", json_string(*newName)}
                    }));
                }
                pos += oldName.size();
            }
            
            if (!edits.empty()) {
                changesJson[docUri] = json_array(edits);
            }
        }
    }
    
    return json_object({
        {"changes", json_object(changesJson)}
    });
}

// ============================================================================
// 文档符号 (Document Symbols)
// ============================================================================

std::shared_ptr<JsonValue> LSPServer::handleDocumentSymbol(const std::shared_ptr<JsonValue>& params) {
    auto textDocument = getObjectField(params, "textDocument");
    if (!textDocument) return json_array({});
    
    auto uri = getStringField(textDocument, "uri");
    if (!uri) return json_array({});
    
    std::vector<std::shared_ptr<JsonValue>> symbols;
    
    {
        std::lock_guard<std::mutex> lock(documents_mutex_);
        std::lock_guard<std::mutex> sym_lock(symbols_mutex_);
        
        auto docIt = documents_.find(*uri);
        auto symIt = symbols_cache_.find(*uri);
        
        if (docIt == documents_.end() || symIt == symbols_cache_.end()) {
            return json_array({});
        }
        
        const auto& text = docIt->second->text;
        const auto& syms = symIt->second;
        
        // 按行号排序符号
        std::map<int, std::vector<SymbolDefinition>> symbolsByLine;
        for (const auto& sym : syms) {
            symbolsByLine[sym.location.range.start.line].push_back(sym);
        }
        
        // 构建层级符号树
        for (const auto& [line, symsOnLine] : symbolsByLine) {
            for (const auto& sym : symsOnLine) {
                int kind = 0;
                if (sym.kind == "function") kind = 12;
                else if (sym.kind == "variable") kind = 5;
                else if (sym.kind == "constant") kind = 6;
                else if (sym.kind == "parameter") kind = 0;
                else if (sym.kind == "type") kind = 7;
                else if (sym.kind == "struct") kind = 23;
                else if (sym.kind == "enum") kind = 10;
                else if (sym.kind == "trait") kind = 7;
                
                auto range = sym.location.range;
                
                symbols.push_back(json_object({
                    {"name", json_string(sym.name)},
                    {"kind", json_int(kind)},
                    {"range", json_object({
                        {"start", json_object({
                            {"line", json_int(range.start.line)},
                            {"character", json_int(range.start.character)}
                        })},
                        {"end", json_object({
                            {"line", json_int(range.end.line)},
                            {"character", json_int(range.end.character)}
                        })}
                    })},
                    {"selectionRange", json_object({
                        {"start", json_object({
                            {"line", json_int(range.start.line)},
                            {"character", json_int(range.start.character)}
                        })},
                        {"end", json_object({
                            {"line", json_int(range.start.line)},
                            {"character", json_int(range.start.character + (int)sym.name.size())}
                        })}
                    })},
                    {"detail", json_string(sym.type)}
                }));
            }
        }
    }
    
    return json_array(symbols);
}

// ============================================================================
// 工作区符号 (Workspace Symbols)
// ============================================================================

std::shared_ptr<JsonValue> LSPServer::handleWorkspaceSymbol(const std::shared_ptr<JsonValue>& params) {
    auto query = getStringField(params, "query");
    if (!query || query->empty()) return json_array({});
    
    std::vector<std::shared_ptr<JsonValue>> results;
    
    {
        std::lock_guard<std::mutex> lock(documents_mutex_);
        std::lock_guard<std::mutex> sym_lock(symbols_mutex_);
        
        for (const auto& symPair : symbols_cache_) {
            const auto& uri = symPair.first;
            const auto& syms = symPair.second;
            
            for (const auto& sym : syms) {
                // 模糊匹配
                if (sym.name.find(*query) != std::string::npos) {
                    int kind = 0;
                    if (sym.kind == "function") kind = 12;
                    else if (sym.kind == "variable") kind = 5;
                    else if (sym.kind == "type") kind = 7;
                    
                    results.push_back(json_object({
                        {"name", json_string(sym.name)},
                        {"kind", json_int(kind)},
                        {"location", json_object({
                            {"uri", json_string(uri)},
                            {"range", json_object({
                                {"start", json_object({
                                    {"line", json_int(sym.location.range.start.line)},
                                    {"character", json_int(sym.location.range.start.character)}
                                })},
                                {"end", json_object({
                                    {"line", json_int(sym.location.range.end.line)},
                                    {"character", json_int(sym.location.range.end.character)}
                                })}
                            })}
                        })},
                        {"containerName", json_string("")}
                    }));
                }
            }
        }
    }
    
    return json_array(results);
}

// ============================================================================
// 代码操作 (Code Action)
// ============================================================================

std::shared_ptr<JsonValue> LSPServer::handleCodeAction(const std::shared_ptr<JsonValue>& params) {
    auto textDocument = getObjectField(params, "textDocument");
    auto range = getObjectField(params, "range");
    auto context = getObjectField(params, "context");
    
    if (!textDocument || !range) return json_array({});
    
    auto uri = getStringField(textDocument, "uri");
    if (!uri) return json_array({});
    
    std::vector<std::shared_ptr<JsonValue>> actions;
    
    // 获取诊断信息
    std::vector<Diagnostic> diagnostics;
    {
        std::lock_guard<std::mutex> lock(documents_mutex_);
        auto it = documents_.find(*uri);
        if (it != documents_.end()) {
            diagnostics = it->second->diagnostics;
        }
    }
    
    // 基于诊断生成代码操作
    for (const auto& diag : diagnostics) {
        // 示例：为未使用的变量生成移除操作
        if (diag.message.find("unused") != std::string::npos) {
            actions.push_back(json_object({
                {"title", json_string("Remove unused variable")},
                {"kind", json_string("quickfix")},
                {"edit", json_object({
                    {"changes", json_object({
                        {*uri, json_array({
                            json_object({
                                {"range", json_object({
                                    {"start", json_object({
                                        {"line", json_int(diag.range.start.line)},
                                        {"character", json_int(diag.range.start.character)}
                                    })},
                                    {"end", json_object({
                                        {"line", json_int(diag.range.end.line)},
                                        {"character", json_int(diag.range.end.character)}
                                    })}
                                })},
                                {"newText", json_string("")}
                            })
                        })}
                    })}
                })}
            }));
        }
    }
    
    // 添加一些常见的代码操作
    actions.push_back(json_object({
        {"title", json_string("Add return type annotation")},
        {"kind", json_string("refactor")}
    }));
    
    actions.push_back(json_object({
        {"title", json_string("Extract to function")},
        {"kind", json_string("refactor.extract")}
    }));
    
    return json_array(actions);
}

// ============================================================================
// 语义标记 (Semantic Tokens)
// ============================================================================

std::shared_ptr<JsonValue> LSPServer::handleSemanticTokens(const std::shared_ptr<JsonValue>& params) {
    auto textDocument = getObjectField(params, "textDocument");
    if (!textDocument) return json_object({{"data", json_array({})}});
    
    auto uri = getStringField(textDocument, "uri");
    if (!uri) return json_object({{"data", json_array({})}});
    
    std::vector<int> tokens;
    
    {
        std::lock_guard<std::mutex> lock(documents_mutex_);
        std::lock_guard<std::mutex> sym_lock(symbols_mutex_);
        
        auto docIt = documents_.find(*uri);
        auto symIt = symbols_cache_.find(*uri);
        
        if (docIt == documents_.end()) return json_object({{"data", json_array({})}});
        
        const auto& text = docIt->second->text;
        
        // 简单的词法标记生成
        // 0 = namespace, 1 = function, 2 = variable, 3 = keyword, 4 = string, 5 = number
        int currentLine = 0;
        int lineStart = 0;
        
        for (size_t i = 0; i <= text.size(); ++i) {
            if (i == text.size() || text[i] == '\n') {
                // 处理行末
                if (i > lineStart) {
                    std::string lineText = text.substr(lineStart, i - lineStart);
                    
                    // 查找关键字
                    for (const auto& kw : getKeywords()) {
                        size_t pos = 0;
                        while ((pos = lineText.find(kw, pos)) != std::string::npos) {
                            // 检查完整单词
                            bool isWord = true;
                            if (pos > 0 && (std::isalnum(lineText[pos-1]) || lineText[pos-1] == '_')) isWord = false;
                            if (pos + kw.size() < lineText.size() && (std::isalnum(lineText[pos + kw.size()]) || lineText[pos + kw.size()] == '_')) isWord = false;
                            
                            if (isWord) {
                                tokens.push_back(currentLine);
                                tokens.push_back((int)pos);
                                tokens.push_back((int)kw.size());
                                tokens.push_back(3); // keyword
                                tokens.push_back(0);
                            }
                            pos += kw.size();
                        }
                    }
                }
                currentLine++;
                lineStart = i + 1;
            }
        }
    }
    
    std::vector<std::shared_ptr<JsonValue>> tokenData;
    for (int t : tokens) {
        tokenData.push_back(json_int(t));
    }
    
    return json_object({
        {"resultId", json_string("1")},
        {"data", json_array(tokenData)}
    });
}

// ============================================================================
// Inlay 提示 (Inlay Hint)
// ============================================================================

std::shared_ptr<JsonValue> LSPServer::handleInlayHint(const std::shared_ptr<JsonValue>& params) {
    auto textDocument = getObjectField(params, "textDocument");
    auto range = getObjectField(params, "range");
    
    if (!textDocument) return json_array({});
    
    auto uri = getStringField(textDocument, "uri");
    if (!uri) return json_array({});
    
    std::vector<std::shared_ptr<JsonValue>> hints;
    
    {
        std::lock_guard<std::mutex> lock(documents_mutex_);
        std::lock_guard<std::mutex> sym_lock(symbols_mutex_);
        
        auto symIt = symbols_cache_.find(*uri);
        if (symIt != symbols_cache_.end()) {
            for (const auto& sym : symIt->second) {
                if (sym.kind == "parameter") {
                    auto pos = sym.location.range.start;
                    hints.push_back(json_object({
                        {"position", json_object({
                            {"line", json_int(pos.line)},
                            {"character", json_int(pos.character)}
                        })},
                        {"label", json_string(sym.name + ": " + sym.type)},
                        {"kind", json_int(1)}
                    }));
                }
            }
        }
    }
    
    return json_array(hints);
}

// ============================================================================
// 文档格式化 (Document Formatting)
// ============================================================================

std::shared_ptr<JsonValue> LSPServer::handleDocumentFormatting(const std::shared_ptr<JsonValue>& params) {
    auto textDocument = getObjectField(params, "textDocument");
    auto options = getObjectField(params, "options");
    
    if (!textDocument) return json_array({});
    
    auto uri = getStringField(textDocument, "uri");
    if (!uri) return json_array({});
    
    // 获取格式化选项
    int tabSize = 4;
    bool insertSpaces = true;
    
    if (options) {
        auto ts = getIntField(options, "tabSize");
        auto is = getObjectField(options, "insertSpaces");
        if (ts) tabSize = *ts;
        if (is) {
            insertSpaces = is->is_bool() ? is->bool_val : true;
        }
    }
    
    std::string indentStr = insertSpaces ? std::string(tabSize, ' ') : "\t";
    
    std::vector<std::shared_ptr<JsonValue>> edits;
    
    {
        std::lock_guard<std::mutex> lock(documents_mutex_);
        auto it = documents_.find(*uri);
        if (it == documents_.end()) return json_array({});
        
        std::string text = it->second->text;
        
        // 简单的格式化：统一缩进
        std::string formatted;
        int currentIndent = 0;
        bool inString = false;
        
        for (size_t i = 0; i < text.size(); ++i) {
            char c = text[i];
            
            // 处理字符串
            if (c == '"' && (i == 0 || text[i-1] != '\\')) {
                inString = !inString;
            }
            
            if (!inString) {
                // 增加缩进
                if (c == '{' || c == '(' || c == '[') {
                    formatted += c;
                    currentIndent++;
                    continue;
                }
                // 减少缩进
                if (c == '}' || c == ')' || c == ']') {
                    currentIndent = std::max(0, currentIndent - 1);
                    formatted += c;
                    continue;
                }
                // 换行后添加缩进
                if (c == '\n') {
                    formatted += c;
                    formatted += std::string(currentIndent, '\t');
                    // 跳过后续空白
                    while (i + 1 < text.size() && (text[i + 1] == ' ' || text[i + 1] == '\t')) {
                        i++;
                    }
                    continue;
                }
            }
            
            formatted += c;
        }
        
        // 生成文本编辑
        edits.push_back(json_object({
            {"range", json_object({
                {"start", json_object({
                    {"line", json_int(0)},
                    {"character", json_int(0)}
                })},
                {"end", json_object({
                    {"line", json_int((int)std::count(text.begin(), text.end(), '\n'))},
                    {"character", json_int(0)}
                })}
            })},
            {"newText", json_string(formatted)}
        }));
    }
    
    return json_array(edits);
}

// ============================================================================
// 范围格式化 (Range Formatting)
// ============================================================================

std::shared_ptr<JsonValue> LSPServer::handleDocumentRangeFormatting(const std::shared_ptr<JsonValue>& params) {
    auto textDocument = getObjectField(params, "textDocument");
    auto range = getObjectField(params, "range");
    auto options = getObjectField(params, "options");
    
    if (!textDocument || !range) return json_array({});
    
    auto uri = getStringField(textDocument, "uri");
    if (!uri) return json_array({});
    
    auto startObj = getObjectField(range, "start");
    auto endObj = getObjectField(range, "end");
    if (!startObj || !endObj) return json_array({});
    
    auto startLineObj = getObjectField(startObj, "line");
    auto startCharObj = getObjectField(startObj, "character");
    auto endLineObj = getObjectField(endObj, "line");
    auto endCharObj = getObjectField(endObj, "character");
    
    if (!startLineObj || !startCharObj || !endLineObj || !endCharObj) return json_array({});
    
    int startL = startLineObj->int_val;
    int startC = startCharObj->int_val;
    int endL = endLineObj->int_val;
    int endC = endCharObj->int_val;
    
    // 获取格式化选项
    int tabSize = 4;
    if (options) {
        auto ts = getIntField(options, "tabSize");
        if (ts) tabSize = *ts;
    }
    
    std::vector<std::shared_ptr<JsonValue>> edits;
    
    {
        std::lock_guard<std::mutex> lock(documents_mutex_);
        auto it = documents_.find(*uri);
        if (it == documents_.end()) return json_array({});
        
        // 简单处理：删除所选范围内的多余空白
        std::string text = it->second->text;
        
        size_t startOffset = positionToOffset(text, {startL, startC});
        size_t endOffset = positionToOffset(text, {endL, endC});
        
        std::string selectedText = text.substr(startOffset, endOffset - startOffset);
        
        // 移除多余空白
        std::string formatted;
        bool lastWasSpace = false;
        for (char c : selectedText) {
            if (c == ' ' || c == '\t') {
                if (!lastWasSpace) {
                    formatted += c;
                    lastWasSpace = true;
                }
            } else {
                formatted += c;
                lastWasSpace = false;
            }
        }
        
        if (formatted != selectedText) {
            edits.push_back(json_object({
                {"range", json_object({
                    {"start", json_object({
                        {"line", json_int(startL)},
                        {"character", json_int(startC)}
                    })},
                    {"end", json_object({
                        {"line", json_int(endL)},
                        {"character", json_int(endC)}
                    })}
                })},
                {"newText", json_string(formatted)}
            }));
        }
    }
    
    return json_array(edits);
}

// ============================================================================
// 文档操作
// ============================================================================

void LSPServer::handleTextDocumentDidOpen(const std::shared_ptr<JsonValue>& params) {
    auto textDocument = getObjectField(params, "textDocument");
    if (!textDocument) return;
    
    auto uri = getStringField(textDocument, "uri");
    auto text = getStringField(textDocument, "text");
    auto version = getIntField(textDocument, "version");
    
    if (!uri || !text) return;
    
    {
        std::lock_guard<std::mutex> lock(documents_mutex_);
        auto doc = std::make_shared<DocumentState>();
        doc->uri = *uri;
        doc->text = *text;
        doc->version = version.value_or(1);
        documents_[*uri] = doc;
    }
    
    auto doc = parseDocument(*uri, *text);
    sendDiagnostics(*uri, doc->diagnostics);
}

void LSPServer::handleTextDocumentDidChange(const std::shared_ptr<JsonValue>& params) {
    auto textDocument = getObjectField(params, "textDocument");
    auto contentChanges = getObjectField(params, "contentChanges");
    if (!textDocument || !contentChanges) return;
    
    auto uri = getStringField(textDocument, "uri");
    auto version = getIntField(textDocument, "version");
    if (!uri) return;
    
    std::string newText;
    if (contentChanges->is_array() && !contentChanges->array_val.empty()) {
        auto first = contentChanges->array_val[0];
        auto changeText = getStringField(first, "text");
        if (changeText) newText = *changeText;
    }
    
    if (newText.empty()) return;
    
    {
        std::lock_guard<std::mutex> lock(documents_mutex_);
        auto it = documents_.find(*uri);
        if (it != documents_.end()) {
            it->second->text = newText;
            it->second->version = version.value_or(it->second->version + 1);
            it->second->parsed = false;
        }
    }
    
    auto doc = parseDocument(*uri, newText);
    sendDiagnostics(*uri, doc->diagnostics);
}

void LSPServer::handleTextDocumentDidSave(const std::shared_ptr<JsonValue>& params) {
    auto textDocument = getObjectField(params, "textDocument");
    if (!textDocument) return;
    
    auto uri = getStringField(textDocument, "uri");
    if (!uri) return;
    
    std::lock_guard<std::mutex> lock(documents_mutex_);
    auto it = documents_.find(*uri);
    if (it != documents_.end()) {
        parseDocument(*uri, it->second->text);
    }
}

void LSPServer::handleTextDocumentDidClose(const std::shared_ptr<JsonValue>& params) {
    auto textDocument = getObjectField(params, "textDocument");
    if (!textDocument) return;
    
    auto uri = getStringField(textDocument, "uri");
    if (!uri) return;
    
    std::lock_guard<std::mutex> lock(documents_mutex_);
    documents_.erase(*uri);
    
    std::lock_guard<std::mutex> sym_lock(symbols_mutex_);
    symbols_cache_.erase(*uri);
}

// ============================================================================
// 诊断通知
// ============================================================================

void LSPServer::sendDiagnostics(const std::string& uri, const std::vector<Diagnostic>& diags) {
    std::vector<std::shared_ptr<JsonValue>> diagJson;
    for (const auto& d : diags) {
        diagJson.push_back(json_object({
            {"range", json_object({
                {"start", json_object({
                    {"line", json_int(d.range.start.line)},
                    {"character", json_int(d.range.start.character)}
                })},
                {"end", json_object({
                    {"line", json_int(d.range.end.line)},
                    {"character", json_int(d.range.end.character)}
                })}
            })},
            {"severity", json_int(d.severity)},
            {"message", json_string(d.message)}
        }));
    }
    
    sendNotification("textDocument/publishDiagnostics", json_object({
        {"uri", json_string(uri)},
        {"diagnostics", json_array(diagJson)}
    }));
}

// ============================================================================
// 文档解析
// ============================================================================

std::shared_ptr<DocumentState> LSPServer::parseDocument(const std::string& uri, const std::string& text) {
    std::shared_ptr<DocumentState> doc;
    
    {
        std::lock_guard<std::mutex> lock(documents_mutex_);
        auto it = documents_.find(uri);
        if (it != documents_.end()) {
            doc = it->second;
        } else {
            doc = std::make_shared<DocumentState>();
            doc->uri = uri;
            documents_[uri] = doc;
        }
    }
    
    doc->text = text;
    doc->parsed = false;
    doc->has_errors = false;
    doc->diagnostics.clear();
    
    // 词法分析
    DiagnosticReporter reporter;
    doc->tokens = lexSource(text, reporter);
    
    // 语法分析
    doc->ast = parseTokens(doc->tokens, reporter);
    
    // 生成诊断
    doc->diagnostics = generateDiagnostics(uri, doc->tokens, doc->ast);
    doc->has_errors = !doc->diagnostics.empty();
    doc->parsed = true;
    
    // 提取符号
    if (doc->ast) {
        auto symbols = extractSymbols(doc->ast, uri);
        std::lock_guard<std::mutex> sym_lock(symbols_mutex_);
        symbols_cache_[uri] = symbols;
    }
    
    return doc;
}

std::vector<Token> LSPServer::lexSource(const std::string& source, DiagnosticReporter& reporter) {
    claw::Lexer lexer(source, "<lsp>");
    lexer.set_reporter(&reporter);
    return lexer.scan_all();
}

std::shared_ptr<ast::Program> LSPServer::parseTokens(const std::vector<Token>& tokens, DiagnosticReporter& reporter) {
    claw::Parser parser(tokens);
    parser.set_reporter(&reporter);
    auto program = parser.parse();
    return program;
}

std::vector<Diagnostic> LSPServer::generateDiagnostics(const std::string& uri, const std::vector<Token>& tokens, const std::shared_ptr<ast::Program>& ast) {
    std::vector<Diagnostic> diagnostics;
    
    for (const auto& token : tokens) {
        if (token.type == claw::TokenType::Invalid) {
            Diagnostic diag;
            diag.range = Range{
                Position{(int)token.span.start.line, (int)token.span.start.column},
                Position{(int)token.span.start.line, (int)token.span.start.column + 1}
            };
            diag.severity = 1;
            diag.message = "Lexical error: " + token.text;
            diagnostics.push_back(diag);
        }
    }
    
    return diagnostics;
}

// ============================================================================
// 符号解析
// ============================================================================

// Helper to convert SourceSpan to Location
static Location spanToLocation(const std::string& uri, const claw::SourceSpan& span) {
    return Location{uri, {static_cast<int>(span.start.line - 1), static_cast<int>(span.start.column)}};
}

std::vector<SymbolDefinition> LSPServer::extractSymbols(const std::shared_ptr<ast::Program>& ast, const std::string& uri) {
    std::vector<SymbolDefinition> symbols;
    if (!ast) return symbols;
    
    // 遍历所有声明，提取符号定义
    for (auto& decl : ast->get_declarations()) {
        extractSymbolsFromStatement(decl.get(), uri, symbols);
    }
    
    return symbols;
}

void LSPServer::extractSymbolsFromStatement(ast::Statement* stmt, 
                                              const std::string& uri,
                                              std::vector<SymbolDefinition>& symbols) {
    if (!stmt) return;
    
    auto span = stmt->get_span();
    
    switch (stmt->get_kind()) {
        case ast::Statement::Kind::Function: {
            auto* fn = dynamic_cast<ast::FunctionStmt*>(stmt);
            if (fn) {
                SymbolDefinition sym;
                sym.name = fn->get_name();
                sym.kind = "function";
                sym.type = "fn";
                sym.location = spanToLocation(uri, span);
                symbols.push_back(sym);
                
                // 提取函数参数 (params_ 是 vector<pair<string, string>>)
                for (const auto& param : fn->get_params()) {
                    SymbolDefinition param_sym;
                    param_sym.name = param.first;  // 参数名
                    param_sym.kind = "parameter";
                    param_sym.type = param.second; // 参数类型
                    param_sym.location = spanToLocation(uri, span);
                    symbols.push_back(param_sym);
                }
            }
            break;
        }
        
        case ast::Statement::Kind::Let: {
            auto* let = dynamic_cast<ast::LetStmt*>(stmt);
            if (let) {
                SymbolDefinition sym;
                sym.name = let->get_name();
                sym.kind = "variable";
                sym.type = "let";
                sym.location = spanToLocation(uri, span);
                symbols.push_back(sym);
            }
            break;
        }
        
        case ast::Statement::Kind::Const: {
            auto* con = dynamic_cast<ast::ConstStmt*>(stmt);
            if (con) {
                SymbolDefinition sym;
                sym.name = con->get_name();
                sym.kind = "constant";
                sym.type = "const";
                sym.location = spanToLocation(uri, span);
                symbols.push_back(sym);
            }
            break;
        }
        
        case ast::Statement::Kind::Block: {
            auto* block = dynamic_cast<ast::BlockStmt*>(stmt);
            if (block) {
                extractSymbolsFromBlock(block, uri, symbols);
            }
            break;
        }
        
        default:
            break;
    }
}

void LSPServer::extractSymbolsFromBlock(const ast::BlockStmt* block,
                                          const std::string& uri,
                                          std::vector<SymbolDefinition>& symbols) {
    if (!block) return;
    
    // 使用 get_statements() 方法获取语句
    for (const auto& stmt : block->get_statements()) {
        extractSymbolsFromStatement(stmt.get(), uri, symbols);
    }
}

std::optional<Location> LSPServer::findDefinition(const std::string& uri, const Position& position) {
    std::string text;
    {
        std::lock_guard<std::mutex> lock(documents_mutex_);
        auto it = documents_.find(uri);
        if (it != documents_.end()) text = it->second->text;
    }
    
    auto word = getWordAt(text, position);
    if (!word) return std::nullopt;
    
    std::lock_guard<std::mutex> sym_lock(symbols_mutex_);
    auto it = symbols_cache_.find(uri);
    if (it != symbols_cache_.end()) {
        for (const auto& sym : it->second) {
            if (sym.name == *word) {
                return sym.location;
            }
        }
    }
    
    return std::nullopt;
}

// ============================================================================
// 工具函数
// ============================================================================

std::string LSPServer::uriToPath(const std::string& uri) {
    if (uri.rfind("file://", 0) == 0) {
        return uri.substr(7);
    }
    return uri;
}

std::string LSPServer::pathToUri(const std::string& path) {
    return "file://" + path;
}

Position LSPServer::offsetToPosition(const std::string& text, size_t offset) {
    Position pos;
    for (size_t i = 0; i < offset && i < text.size(); ++i) {
        if (text[i] == '\n') {
            pos.line++;
            pos.character = 0;
        } else {
            pos.character++;
        }
    }
    return pos;
}

size_t LSPServer::positionToOffset(const std::string& text, const Position& position) {
    size_t offset = 0;
    int line = 0;
    while (offset < text.size()) {
        if (line == position.line) break;
        if (text[offset] == '\n') line++;
        offset++;
    }
    return offset + position.character;
}

std::optional<std::string> LSPServer::getWordAt(const std::string& text, const Position& position) {
    if (text.empty()) return std::nullopt;
    
    size_t offset = positionToOffset(text, position);
    if (offset >= text.size()) return std::nullopt;
    
    size_t start = offset;
    while (start > 0 && (std::isalnum(text[start - 1]) || text[start - 1] == '_')) {
        start--;
    }
    
    size_t end = offset;
    while (end < text.size() && (std::isalnum(text[end]) || text[end] == '_')) {
        end++;
    }
    
    if (start >= end) return std::nullopt;
    return text.substr(start, end - start);
}

} // namespace lsp
} // namespace claw
