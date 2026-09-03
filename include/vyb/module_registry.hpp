// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vyb/parser/ast.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace vyb {

enum class ModuleState {
    Unresolved,
    Parsing,
    Resolved,
    Failed
};

struct ModuleRecord {
    std::string key;
    std::filesystem::path sourcePath;
    std::unique_ptr<ast::Module> module;
    std::vector<std::string> importedModuleKeys;
    ModuleState state = ModuleState::Unresolved;
    std::string importSpelling;
    std::vector<std::string> bundles;
    std::unordered_map<std::string, std::vector<std::string>> sharesByName;
};

struct ModuleRegistryOptions {
    std::vector<std::filesystem::path> cliModulePaths;
    std::filesystem::path executablePath;
    bool skipImportResolution = false;
    // #204: dependency/import names that require a package-level `freedom`
    // boundary (declared `[mod] boundary=["freedom"]` in the dep's vyb.toml and
    // surfaced by `vyb build`). Such a module may only be consumed through
    // `smuggle`, never ordinary `import`. Empty = nothing is privileged.
    std::vector<std::string> privilegedModules;
};

class ModuleRegistry {
public:
    explicit ModuleRegistry(ModuleRegistryOptions options = {});

    std::unique_ptr<ast::Module> resolveRoot(const std::string& source, const std::string& fileName);

    const std::vector<std::string>& topologicalOrder() const { return topologicalOrder_; }
    const std::unordered_map<std::string, ModuleRecord>& records() const { return records_; }
    const std::vector<std::filesystem::path>& configuredSearchPaths() const { return configuredSearchPaths_; }

    // Namespace-scope resolution data, computed during module resolution so a
    // consumer never sees another module's private dependencies.
    //   moduleKeyByName_ : every top-level symbol -> the module that declares it
    //   effectiveScope_  : module key -> the set of names that module's own code
    //                      may resolve (its declarations plus what it imported)
    //   exports_         : module key -> the names the module shares to importers
    const std::unordered_map<std::string, std::string>& moduleKeyByName() const { return moduleKeyByName_; }
    const std::unordered_map<std::string, std::unordered_set<std::string>>& effectiveScope() const { return effectiveScope_; }
    const std::unordered_map<std::string, std::unordered_set<std::string>>& exportsPerModule() const { return exports_; }
    const std::unordered_map<std::string, std::unordered_set<std::string>>& allNamesPerModule() const { return allNames_; }
    // Namespace imports: module key -> (namespace identifier -> (symbol name -> mangled name)).
    const std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<std::string, std::string>>>& namespacesPerModule() const { return namespaces_; }

private:
    struct SourceMetadata {
        std::string source;
        std::vector<std::string> bundles;
        std::unordered_map<std::string, std::vector<std::string>> sharesByName;
        std::vector<std::vector<std::string>> importShares;
        bool coreAutoImport = true;
    };

    struct ResolvedImportPath {
        std::filesystem::path resolvedPath;
        std::vector<std::filesystem::path> triedPaths;
        std::string importSpelling;
        unsigned int line = 1;
        std::string importerFile;
    };

    ModuleRegistryOptions options_;
    std::vector<std::filesystem::path> configuredSearchPaths_;
    std::unordered_map<std::string, ModuleRecord> records_;
    std::vector<std::string> activeStack_;
    std::vector<std::string> topologicalOrder_;
    std::unordered_map<std::string, std::string> moduleKeyByName_;
    std::unordered_map<std::string, std::unordered_set<std::string>> effectiveScope_;
    std::unordered_map<std::string, std::unordered_set<std::string>> exports_;
    std::unordered_map<std::string, std::unordered_set<std::string>> allNames_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<std::string, std::string>>> namespaces_;

    std::string resolveModule(const std::string& source,
                              const std::filesystem::path& sourcePath,
                              const std::string& importSpelling);
    ResolvedImportPath resolveImportPath(const ast::ImportDeclaration* importDecl, const std::filesystem::path& importingFile) const;
    std::vector<std::filesystem::path> buildSearchRoots(const std::filesystem::path& importingFile) const;
    std::vector<std::filesystem::path> buildConfiguredSearchPaths() const;
    std::filesystem::path normalizePath(const std::filesystem::path& path) const;
    std::optional<std::filesystem::path> discoverStdlibRoot() const;
    static std::string readSourceFile(const std::filesystem::path& path);
    static std::filesystem::path modulePathRelativeFile(const std::string& modulePath);
    static SourceMetadata preprocessModuleSource(const std::string& source);
    static std::unique_ptr<ast::Module> parseModuleOnly(const std::string& source, const std::string& fileName);
    static std::string declarationName(const ast::StmtPtr& stmt);
    static bool renameDeclaration(ast::StmtPtr& stmt, const std::string& newName);
    static bool isMainFunction(const ast::StmtPtr& stmt);
    static bool sharesAllow(const std::vector<std::string>& shares, const std::vector<std::string>& importerBundles);
    static bool declarationVisible(const std::string& name,
                                   const ModuleRecord& importedModule,
                                   const std::vector<std::string>& importerBundles,
                                   const ast::ImportDeclaration* importDecl);
};

} // namespace vyb
