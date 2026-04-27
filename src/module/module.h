// Claw Compiler - Module System Header
// Supports import/export statements and module resolution

#ifndef CLAW_MODULE_H
#define CLAW_MODULE_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <filesystem>
#include "common/common.h"
#include "ast/ast.h"

namespace claw {
namespace module {

// Forward declarations
class Module;
class ModuleLoader;
class ModuleResolver;

// Module visibility for exports
enum class Visibility {
    Private,
    Public,
    Pub,
};

// Import specifier - represents "use path::to::symbol"
struct ImportSpec {
    std::vector<std::string> path;      // path components
    std::string alias;                  // optional "as NewName"
    std::string wildcard_prefix;        // for "use path::*" style
    
    ImportSpec() = default;
    ImportSpec(const std::vector<std::string>& p) : path(p) {}
};

// Module declaration - "mod name { ... }"
struct ModuleDecl {
    std::string name;
    std::vector<std::unique_ptr<ast::Statement>> body;
    Visibility visibility;
    SourceSpan span;
    
    ModuleDecl(const std::string& n, const SourceSpan& s) 
        : name(n), visibility(Visibility::Private), span(s) {}
};

// Import statement - "use path::to::symbol [as alias]"
struct ImportStmt {
    std::vector<ImportSpec> specs;
    Visibility visibility;
    SourceSpan span;
    bool is_reexport;  // "pub use" re-exports symbols
    
    ImportStmt(const SourceSpan& s) : visibility(Visibility::Private), span(s), is_reexport(false) {}
};

// Export item - explicitly mark symbols for export
struct ExportStmt {
    std::vector<std::string> names;
    Visibility visibility;
    SourceSpan span;
    
    ExportStmt(const SourceSpan& s) : visibility(Visibility::Private), span(s) {}
};

// Module symbol - represents an exported symbol
struct ModuleSymbol {
    std::string name;
    std::string type_name;  // Use type name string instead of TypePtr
    Visibility visibility;
    void* data;  // pointer to function/constant/type
    
    ModuleSymbol() : visibility(Visibility::Private), data(nullptr) {}
    ModuleSymbol(const std::string& n, const std::string& t, Visibility v, void* d = nullptr)
        : name(n), type_name(t), visibility(v), data(d) {}
};

// Resolved module reference
struct ModuleRef {
    std::string module_id;        // "std::io" style path
    std::filesystem::path file_path;
    std::shared_ptr<Module> module; // resolved module
    
    ModuleRef() = default;
    ModuleRef(const std::string& id, const std::filesystem::path& path)
        : module_id(id), file_path(path) {}
};

// Module representation
class Module {
public:
    Module(const std::string& id, const std::filesystem::path& path);
    
    // Module identity
    const std::string& get_id() const { return id_; }
    const std::filesystem::path& get_path() const { return path_; }
    const std::string& get_name() const { return name_; }
    
    // AST access
    void set_ast(std::unique_ptr<ast::Program> ast) { ast_ = std::move(ast); }
    ast::Program* get_ast() const { return ast_.get(); }
    
    // Symbol table management
    void add_symbol(const std::string& name, const ModuleSymbol& symbol);
    ModuleSymbol* find_symbol(const std::string& name);
    const std::unordered_map<std::string, ModuleSymbol>& get_symbols() const { return symbols_; }
    
    // Sub-modules
    void add_submodule(const std::string& name, std::shared_ptr<Module> sub);
    std::shared_ptr<Module> find_submodule(const std::string& name);
    const std::unordered_map<std::string, std::shared_ptr<Module>>& get_submodules() const { return submodules_; }
    
    // Dependencies
    void add_dependency(const std::string& module_id);
    const std::vector<std::string>& get_dependencies() const { return dependencies_; }
    
    // Visibility
    void set_visibility(Visibility v) { visibility_ = v; }
    Visibility get_visibility() const { return visibility_; }
    
    // Load state
    bool is_loaded() const { return loaded_; }
    void set_loaded(bool v) { loaded_ = v; }
    
    // Error handling
    void add_error(const std::string& error);
    const std::vector<std::string>& get_errors() const { return errors_; }
    bool has_errors() const { return !errors_.empty(); }
    
private:
    std::string id_;                  // full module ID "std::io::fs"
    std::string name_;                // simple name "fs"
    std::filesystem::path path_;      // file path
    std::unique_ptr<ast::Program> ast_;
    
    std::unordered_map<std::string, ModuleSymbol> symbols_;       // exported symbols
    std::unordered_map<std::string, std::shared_ptr<Module>> submodules_; // sub-modules
    std::vector<std::string> dependencies_;  // imported module IDs
    
    Visibility visibility_;
    bool loaded_;
    std::vector<std::string> errors_;
};

// Module loader - handles file system module resolution
class ModuleLoader {
public:
    ModuleLoader();
    
    // Configure search paths
    void add_search_path(const std::filesystem::path& path);
    const std::vector<std::filesystem::path>& get_search_paths() const { return search_paths_; }
    
    // Module resolution
    std::shared_ptr<Module> load_module(const std::string& module_id);
    std::filesystem::path resolve_module_path(const std::string& module_id);
    
    // Cached modules
    std::shared_ptr<Module> get_cached(const std::string& module_id) const;
    void cache_module(const std::string& module_id, std::shared_ptr<Module> module);
    bool is_cached(const std::string& module_id) const;
    
    // Module compilation
    std::shared_ptr<Module> compile_and_load(const std::string& module_id, 
                                              const std::filesystem::path& source_path);
    
    // Error handling
    void add_error(const std::string& error);
    const std::vector<std::string>& get_errors() const { return errors_; }
    
private:
    std::vector<std::filesystem::path> search_paths_;
    std::unordered_map<std::string, std::shared_ptr<Module>> module_cache_;
    std::vector<std::string> errors_;
    
    std::filesystem::path find_module_file(const std::string& module_id);
    std::string module_id_to_path(const std::string& module_id) const;
};

// Module resolver - resolves import paths to modules
class ModuleResolver {
public:
    ModuleResolver(std::shared_ptr<ModuleLoader> loader);
    
    // Resolve import spec to actual module/symbol
    bool resolve_import(const ImportSpec& spec, ModuleRef& out_module, std::string& out_symbol);
    
    // Resolve module ID to module
    std::shared_ptr<Module> resolve_module(const std::string& module_id);
    
    // Add loaded module
    void register_module(std::shared_ptr<Module> module);
    
    // Current module context
    void set_current_module(std::shared_ptr<Module> mod);
    std::shared_ptr<Module> get_current_module() const { return current_module_; }
    
    // Built-in modules
    void register_builtin(const std::string& name, std::shared_ptr<Module> module);
    
private:
    std::shared_ptr<ModuleLoader> loader_;
    std::shared_ptr<Module> current_module_;
    std::unordered_map<std::string, std::shared_ptr<Module>> loaded_modules_;
    std::unordered_map<std::string, std::shared_ptr<Module>> builtin_modules_;
};

// Utility functions
std::string join_path(const std::vector<std::string>& parts, const std::string& sep = "::");
std::vector<std::string> split_path(const std::string& path, const std::string& sep = "::");
std::string normalize_module_path(const std::string& path);

// Global module system instance
std::shared_ptr<ModuleLoader> get_global_module_loader();
void set_global_module_loader(std::shared_ptr<ModuleLoader> loader);

} // namespace module
} // namespace claw

#endif // CLAW_MODULE_H
