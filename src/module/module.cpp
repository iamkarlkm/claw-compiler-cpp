// Claw Compiler - Module System Implementation
// Supports import/export statements and module resolution

#include "module/module.h"
#include <fstream>
#include <sstream>

namespace claw {
namespace module {

// ============ Module Implementation ============

Module::Module(const std::string& id, const std::filesystem::path& path)
    : id_(id), path_(path), visibility_(Visibility::Private), loaded_(false) {
    // Extract simple name from ID
    size_t pos = id.rfind("::");
    if (pos != std::string::npos) {
        name_ = id.substr(pos + 2);
    } else {
        name_ = id;
    }
}

void Module::add_symbol(const std::string& name, const ModuleSymbol& symbol) {
    symbols_[name] = symbol;
}

ModuleSymbol* Module::find_symbol(const std::string& name) {
    auto it = symbols_.find(name);
    if (it != symbols_.end()) {
        return const_cast<ModuleSymbol*>(&it->second);
    }
    return nullptr;
}

void Module::add_submodule(const std::string& name, std::shared_ptr<Module> sub) {
    submodules_[name] = std::move(sub);
}

std::shared_ptr<Module> Module::find_submodule(const std::string& name) {
    auto it = submodules_.find(name);
    if (it != submodules_.end()) {
        return it->second;
    }
    return nullptr;
}

void Module::add_dependency(const std::string& module_id) {
    dependencies_.push_back(module_id);
}

void Module::add_error(const std::string& error) {
    errors_.push_back(error);
}

// ============ ModuleLoader Implementation ============

ModuleLoader::ModuleLoader() {
    // Add current directory as default search path
    search_paths_.push_back(std::filesystem::current_path());
    
    // Add standard library path if exists
    std::filesystem::path std_path = std::filesystem::current_path() / "stdlib";
    if (std::filesystem::exists(std_path)) {
        search_paths_.push_back(std_path);
    }
}

void ModuleLoader::add_search_path(const std::filesystem::path& path) {
    if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
        search_paths_.push_back(path);
    }
}

std::filesystem::path ModuleLoader::find_module_file(const std::string& module_id) {
    std::string relative_path = module_id_to_path(module_id);
    
    for (const auto& search_path : search_paths_) {
        // Try .claw extension
        std::filesystem::path p = search_path / relative_path;
        p = p.replace_extension(".claw");
        if (std::filesystem::exists(p)) {
            return p;
        }
        
        // Try as directory with mod.claw
        p = search_path / relative_path / "mod.claw";
        if (std::filesystem::exists(p)) {
            return p;
        }
        
        // Try .cl extension
        p = search_path / relative_path;
        p = p.replace_extension(".cl");
        if (std::filesystem::exists(p)) {
            return p;
        }
    }
    
    return {};
}

std::string ModuleLoader::module_id_to_path(const std::string& module_id) const {
    // Convert "std::io::fs" to "std/io/fs"
    std::string result = module_id;
    std::string output;
    for (size_t i = 0; i < result.size(); i++) {
        if (result[i] == ':' && i + 1 < result.size() && result[i + 1] == ':') {
            output += '/';
            i++; // skip second ':'
        } else {
            output += result[i];
        }
    }
    return output;
}

std::filesystem::path ModuleLoader::resolve_module_path(const std::string& module_id) {
    return find_module_file(module_id);
}

std::shared_ptr<Module> ModuleLoader::get_cached(const std::string& module_id) const {
    auto it = module_cache_.find(module_id);
    if (it != module_cache_.end()) {
        return it->second;
    }
    return nullptr;
}

void ModuleLoader::cache_module(const std::string& module_id, std::shared_ptr<Module> module) {
    module_cache_[module_id] = module;
}

bool ModuleLoader::is_cached(const std::string& module_id) const {
    return module_cache_.find(module_id) != module_cache_.end();
}

std::shared_ptr<Module> ModuleLoader::load_module(const std::string& module_id) {
    // Check cache first
    if (auto cached = get_cached(module_id)) {
        return cached;
    }
    
    // Find the module file
    auto path = find_module_file(module_id);
    if (path.empty()) {
        add_error("Module not found: " + module_id);
        return nullptr;
    }
    
    // Create module placeholder (actual compilation will be done by compiler pipeline)
    auto module = std::make_shared<Module>(module_id, path);
    module->set_loaded(true);
    
    // Cache it
    cache_module(module_id, module);
    
    return module;
}

void ModuleLoader::add_error(const std::string& error) {
    errors_.push_back(error);
}

// ============ ModuleResolver Implementation ============

ModuleResolver::ModuleResolver(std::shared_ptr<ModuleLoader> loader)
    : loader_(std::move(loader)) {}

bool ModuleResolver::resolve_import(const ImportSpec& spec, ModuleRef& out_module, std::string& out_symbol) {
    // Build full module ID from path
    std::string module_id;
    if (!spec.path.empty()) {
        // All but last component form the module path
        std::vector<std::string> module_path(spec.path.begin(), spec.path.end() - 1);
        module_id = join_path(module_path);
    }
    
    // Last component is the symbol to import
    out_symbol = spec.path.empty() ? "" : spec.path.back();
    
    // Load the module
    auto mod = loader_->load_module(module_id);
    if (!mod) {
        return false;
    }
    
    out_module.module_id = module_id;
    out_module.module = mod;
    
    return true;
}

std::shared_ptr<Module> ModuleResolver::resolve_module(const std::string& module_id) {
    // Check loaded modules
    auto it = loaded_modules_.find(module_id);
    if (it != loaded_modules_.end()) {
        return it->second;
    }
    
    // Check builtin modules
    auto bit = builtin_modules_.find(module_id);
    if (bit != builtin_modules_.end()) {
        return bit->second;
    }
    
    // Load from loader
    auto mod = loader_->load_module(module_id);
    if (mod) {
        loaded_modules_[module_id] = mod;
    }
    return mod;
}

void ModuleResolver::register_module(std::shared_ptr<Module> module) {
    loaded_modules_[module->get_id()] = module;
}

void ModuleResolver::set_current_module(std::shared_ptr<Module> mod) {
    current_module_ = std::move(mod);
}

void ModuleResolver::register_builtin(const std::string& name, std::shared_ptr<Module> module) {
    builtin_modules_[name] = std::move(module);
}

// ============ Utility Functions ============

std::string join_path(const std::vector<std::string>& parts, const std::string& sep) {
    if (parts.empty()) return "";
    
    std::string result = parts[0];
    for (size_t i = 1; i < parts.size(); i++) {
        result += sep + parts[i];
    }
    return result;
}

std::vector<std::string> split_path(const std::string& path, const std::string& sep) {
    std::vector<std::string> result;
    std::string current;
    
    for (size_t i = 0; i < path.size(); i++) {
        // Check for :: separator
        if (i + 1 < path.size() && path[i] == ':' && path[i + 1] == ':') {
            if (!current.empty()) {
                result.push_back(current);
                current.clear();
            }
            i++; // skip second ':'
            continue;
        }
        
        // Check for single character separator
        if (sep.size() == 1 && path[i] == sep[0]) {
            if (!current.empty()) {
                result.push_back(current);
                current.clear();
            }
            continue;
        }
        
        current += path[i];
    }
    
    if (!current.empty()) {
        result.push_back(current);
    }
    
    return result;
}

std::string normalize_module_path(const std::string& path) {
    // Remove leading/trailing whitespace
    std::string result = path;
    
    // Remove quotes if present
    if ((result.front() == '"' && result.back() == '"') ||
        (result.front() == '\'' && result.back() == '\'')) {
        result = result.substr(1, result.size() - 2);
    }
    
    return result;
}

// ============ Global Module System ============

static std::shared_ptr<ModuleLoader> g_global_module_loader;

std::shared_ptr<ModuleLoader> get_global_module_loader() {
    if (!g_global_module_loader) {
        g_global_module_loader = std::make_shared<ModuleLoader>();
    }
    return g_global_module_loader;
}

void set_global_module_loader(std::shared_ptr<ModuleLoader> loader) {
    g_global_module_loader = std::move(loader);
}

} // namespace module
} // namespace claw
