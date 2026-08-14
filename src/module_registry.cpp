#include "vyb/module_registry.hpp"
#include "vyb/parser/lexer.hpp"
#include "vyb/parser/parser.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace vyb {
namespace fs = std::filesystem;

namespace {

std::string trimCopy(const std::string& text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(start, end - start);
}

bool startsWithWord(const std::string& text, const std::string& word) {
    if (text.rfind(word, 0) != 0) {
        return false;
    }
    return text.size() == word.size() ||
           (!std::isalnum(static_cast<unsigned char>(text[word.size()])) &&
            text[word.size()] != '_');
}

std::vector<std::string> parseDirectiveArgs(const std::string& inside) {
    std::vector<std::string> args;
    size_t start = 0;
    while (start < inside.size()) {
        size_t comma = inside.find(',', start);
        std::string arg = trimCopy(inside.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (!arg.empty()) {
            args.push_back(arg);
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return args;
}

std::optional<std::vector<std::string>> consumeDirective(std::string& line, const std::string& name) {
    std::string trimmed = trimCopy(line);
    std::string prefix = name + "(";
    if (trimmed.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }

    size_t close = trimmed.find(')', prefix.size());
    if (close == std::string::npos) {
        throw std::runtime_error("Malformed " + name + "(...) directive");
    }

    auto args = parseDirectiveArgs(trimmed.substr(prefix.size(), close - prefix.size()));
    std::string rest = trimCopy(trimmed.substr(close + 1));
    line = rest;
    return args;
}

std::string takeIdentifierAfterKeyword(const std::string& line, const std::string& keyword) {
    if (!startsWithWord(line, keyword)) {
        return "";
    }
    size_t pos = keyword.size();
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
        ++pos;
    }
    size_t start = pos;
    while (pos < line.size() &&
           (std::isalnum(static_cast<unsigned char>(line[pos])) || line[pos] == '_')) {
        ++pos;
    }
    return pos > start ? line.substr(start, pos - start) : "";
}

std::string bindKeyFromLine(const std::string& line);

std::string declarationNameFromLine(const std::string& line) {
    std::string name = takeIdentifierAfterKeyword(line, "struct");
    if (!name.empty()) return name;
    name = takeIdentifierAfterKeyword(line, "enum");
    if (!name.empty()) return name;
    name = takeIdentifierAfterKeyword(line, "aspect");
    if (!name.empty()) return name;
    name = takeIdentifierAfterKeyword(line, "class");
    if (!name.empty()) return name;
    name = takeIdentifierAfterKeyword(line, "type");
    if (!name.empty()) return name;
    name = takeIdentifierAfterKeyword(line, "fn");
    if (!name.empty()) return name;
    name = takeIdentifierAfterKeyword(line, "extern");
    if (!name.empty() && line.find('"') == std::string::npos) return name;
    name = takeIdentifierAfterKeyword(line, "async");
    if (!name.empty() && line.find('(') != std::string::npos) return name;
    name = bindKeyFromLine(line);
    if (!name.empty()) return name;

    if (!line.empty() && (std::isalpha(static_cast<unsigned char>(line[0])) || line[0] == '_')) {
        size_t pos = 1;
        while (pos < line.size() &&
               (std::isalnum(static_cast<unsigned char>(line[pos])) || line[pos] == '_')) {
            ++pos;
        }
        std::string candidate = line.substr(0, pos);
        if (pos < line.size() && (line[pos] == '(' || line[pos] == '<' || line[pos] == '=')) {
            return candidate;
        }
    }

    return "";
}

bool isImportLine(const std::string& line) {
    return startsWithWord(line, "import") || startsWithWord(line, "smuggle");
}

// Produces a stable per-(target,trait) key for a `bind` declaration so binds can
// be carried across module imports and visibility/dedup tracked like other shared
// declarations. Matches SemanticAnalyzer-style bind identity (target -> trait).
std::string bindKeyFromLine(const std::string& line) {
    if (!startsWithWord(line, "bind")) {
        return "";
    }
    std::string rest = trimCopy(line.substr(4));
    // Skip generic parameters, e.g. `bind<T> Iterator -> Boxer<T>` or
    // `bind<K<Hashable, Equatable>, V> MapOps -> HashMap<K, V>`. Balanced-angle
    // scanning handles type-parameter bounds that themselves wrap a generic
    // (nested `<...>`), e.g. the inner `<Hashable, Equatable>` in the bound.
    if (!rest.empty() && rest[0] == '<') {
        size_t depth = 0;
        size_t i = 0;
        for (; i < rest.size(); ++i) {
            if (rest[i] == '<') ++depth;
            else if (rest[i] == '>') {
                --depth;
                if (depth == 0) { ++i; break; }
            }
        }
        if (depth != 0) {
            return "";
        }
        rest = trimCopy(rest.substr(i));
    }
    // Trait name.
    std::string trait;
    while (rest.size() > 0 &&
           (std::isalnum(static_cast<unsigned char>(rest[0])) || rest[0] == '_')) {
        trait += rest[0];
        rest = rest.substr(1);
    }
    rest = trimCopy(rest);
    if (!startsWithWord(rest, "->")) {
        return "";
    }
    rest = trimCopy(rest.substr(2));
    // Target type runs up to the opening brace.
    size_t brace = rest.find('{');
    std::string target = trimCopy(brace == std::string::npos ? rest : rest.substr(0, brace));
    if (trait.empty() || target.empty()) {
        return "";
    }
    return "bind:" + target + ":" + trait;
}

// Returns true when auto-importing `core::aspects` would collide with the module
// (it already imports it, or locally redefines one of the core aspects / a
// primitive bind that core::aspects pre-wires). Auto-import is skipped then.
bool moduleHasCoreAutoImportConflict(const ast::Module* module) {
    if (!module) {
        return true;
    }
    static const std::unordered_set<std::string> coreAspects = {
        "Display", "Debug", "Clone", "Equatable", "Hashable", "Comparable"};
    static const std::unordered_set<std::string> scalarTargets = {
        "Int", "Float", "Bool", "String"};
    for (const auto& stmt : module->body) {
        if (auto* imp = dynamic_cast<ast::ImportDeclaration*>(stmt.get())) {
            if (imp->source) {
                const std::string& src = imp->source->value;
                if (src == "core::aspects" || src == "core::prelude" || src == "prelude") {
                    return true; // already imports the core contracts (directly or via prelude)
                }
            }
            continue;
        }
        if (auto* aspect = dynamic_cast<ast::AspectDeclaration*>(stmt.get())) {
            if (aspect->name && coreAspects.count(aspect->name->name)) {
                return true; // local redefinition of a core aspect
            }
            continue;
        }
        if (auto* bind = dynamic_cast<ast::BindDeclaration*>(stmt.get())) {
            if (bind->selfType && bind->traitType) {
                std::string target = bind->selfType->toString();
                std::string trait = bind->traitType->toString();
                if (scalarTargets.count(target) && coreAspects.count(trait)) {
                    return true; // collides with a pre-wired primitive bind
                }
            }
            continue;
        }
    }
    return false;
}

bool pathIsUnder(const fs::path& path, const fs::path& root) {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::absolute(path, ec), ec);
    fs::path r = fs::weakly_canonical(fs::absolute(root, ec), ec);
    auto pit = p.begin(), rit = r.begin();
    for (; pit != p.end() && rit != r.end(); ++pit, ++rit) {
        if (*pit != *rit) {
            return false;
        }
    }
    return rit == r.end();
}

} // namespace

ModuleRegistry::ModuleRegistry(ModuleRegistryOptions options)
    : options_(std::move(options)) {
    configuredSearchPaths_ = buildConfiguredSearchPaths();
}

std::unique_ptr<ast::Module> ModuleRegistry::resolveRoot(const std::string& source, const std::string& fileName) {
    std::string rootKey = resolveModule(source, fileName, "<root>", true);
    auto it = records_.find(rootKey);
    if (it == records_.end() || !it->second.module) {
        throw std::runtime_error("Module resolution failed for root module: " + rootKey);
    }
    return std::move(it->second.module);
}

std::string ModuleRegistry::resolveModule(const std::string& source,
                                          const fs::path& sourcePath,
                                          const std::string& importSpelling,
                                          bool isRoot) {
    fs::path currentPath = normalizePath(sourcePath);
    std::string currentKey = currentPath.string();
    ModuleRecord& currentRecord = records_[currentKey];
    currentRecord.key = currentKey;
    currentRecord.sourcePath = currentPath;
    currentRecord.importSpelling = importSpelling;

    if (currentRecord.state == ModuleState::Resolved) {
        return currentKey;
    }

    if (currentRecord.state == ModuleState::Parsing) {
        std::ostringstream chain;
        bool inCycle = false;
        for (const auto& activeKey : activeStack_) {
            if (activeKey == currentKey) {
                inCycle = true;
            }
            if (inCycle) {
                chain << "\n - " << activeKey;
            }
        }
        chain << "\n - " << currentKey;
        throw std::runtime_error(
            "Circular import detected while resolving module '" + currentKey +
            "' (from '" + importSpelling + "'). Dependency chain:" + chain.str());
    }

    if (currentRecord.state == ModuleState::Failed) {
        throw std::runtime_error("Module previously failed to resolve: " + currentKey);
    }

    currentRecord.state = ModuleState::Parsing;
    activeStack_.push_back(currentKey);

    try {
        SourceMetadata metadata = preprocessModuleSource(source);
        std::unique_ptr<ast::Module> module;
        try {
            module = parseModuleOnly(metadata.source, currentKey);
        } catch (const std::exception& e) {
            throw std::runtime_error("Parse error inside module '" + currentKey + "': " + e.what());
        }
        std::vector<ast::StmtPtr> resolvedBody;
        std::unordered_set<std::string> seenNames;
        size_t importIndex = 0;

        currentRecord.bundles = metadata.bundles;
        currentRecord.sharesByName = metadata.sharesByName;
        currentRecord.importedModuleKeys.clear();

        // Auto-import core::* contracts (core::aspects) unless opted out via the
        // `no_core()` directive, the module is part of the stdlib itself (the stdlib
        // wires its own imports precisely), or the module already imports/defines the
        // contracts. The synthetic import is inserted at the front (like a top-of-file
        // import) so the core aspects and their binds register before any program body
        // runs; auto-import is skipped entirely on any potential name conflict, so the
        // front-insertion cannot shadow the module's own declarations.
        bool moduleIsUnderStdlib = false;
        if (auto stdlibRoot = discoverStdlibRoot()) {
            moduleIsUnderStdlib = pathIsUnder(currentPath, *stdlibRoot);
        }
        if (metadata.coreAutoImport && !moduleIsUnderStdlib &&
            !moduleHasCoreAutoImportConflict(module.get())) {
            // Reserve an empty importShare slot for the synthetic import so the
            // positional importShares array stays aligned with the module's real imports.
            metadata.importShares.insert(metadata.importShares.begin(), std::vector<std::string>{});
            module->body.insert(module->body.begin(),
                std::make_unique<ast::ImportDeclaration>(
                    SourceLocation{}, ast::ImportKind::TrustedImport,
                    std::make_unique<ast::StringLiteral>(SourceLocation{}, "core::aspects")));
        }

        for (auto& stmt : module->body) {
            if (auto* importDecl = dynamic_cast<ast::ImportDeclaration*>(stmt.get())) {
                std::vector<std::string> importShare;
                if (importIndex < metadata.importShares.size()) {
                    importShare = metadata.importShares[importIndex];
                }
                ++importIndex;

                for (const auto& specifier : importDecl->specifiers) {
                    if (!specifier.importedName) {
                        throw std::runtime_error(
                            "Module resolution error in " + currentKey +
                            ": whole-module aliases are not supported; use import module::{symbol as alias}");
                    }
                }

                if (!options_.skipImportResolution) {
                ResolvedImportPath importPath = resolveImportPath(importDecl, currentPath);
                std::string importedSource = readSourceFile(importPath.resolvedPath);
                std::string importedKey;
                try {
                    importedKey = resolveModule(importedSource, importPath.resolvedPath, importPath.importSpelling, false);
                } catch (const std::exception& e) {
                    std::string message = e.what();
                    if (message.find("Parse error inside module") != std::string::npos) {
                        throw std::runtime_error(message + " (import '" + importPath.importSpelling + "' from " +
                                                 importPath.importerFile + ":" + std::to_string(importPath.line) + ")");
                    }
                    throw;
                }

                currentRecord.importedModuleKeys.push_back(importedKey);
                ModuleRecord& importedRecord = records_.at(importedKey);
                if (importedRecord.emitted || !importedRecord.module) {
                    continue;
                }

                std::unordered_map<std::string, std::string> requestedRenames;
                std::unordered_set<std::string> requestedNames;
                for (const auto& specifier : importDecl->specifiers) {
                    const std::string importedName = specifier.importedName->name;
                    requestedNames.insert(importedName);
                    requestedRenames[importedName] = specifier.localName ? specifier.localName->name : importedName;
                }
                // Immutable snapshot for filtering carried binds: requestedNames shrinks
                // as aspects are spliced, but binds must not leak in just because the
                // requested set emptied mid-splice.
                const std::unordered_set<std::string> requestedForBinds = requestedNames;

                for (auto& importedStmt : importedRecord.module->body) {
                    if (isMainFunction(importedStmt)) {
                        throw std::runtime_error("Imported module must not define main(): " + importedRecord.sourcePath.string());
                    }

                    // Binds carry impl methods for (target, aspect) pairs. They have no
                    // symbol name to alias, so they are handled distinctly: carried when the
                    // module is imported whole or the requested aspect is in the specifier
                    // list, without consuming a requested-name slot (the aspect declaration
                    // itself must still be imported).
                    if (auto* bind = dynamic_cast<ast::BindDeclaration*>(importedStmt.get())) {
                        std::string bindKey = declarationName(importedStmt);
                        if (bindKey.empty()) {
                            continue;
                        }
                        std::string bindTrait = bind->traitType ? bind->traitType->toString() : "";

                        if (!requestedForBinds.empty() &&
                            (bindTrait.empty() || requestedForBinds.find(bindTrait) == requestedForBinds.end())) {
                            continue;
                        }
                        if (!declarationVisible(bindKey, importedRecord, metadata.bundles, importDecl)) {
                            continue;
                        }
                        if (seenNames.find(bindKey) != seenNames.end()) {
                            throw std::runtime_error("Duplicate bind after splice: '" + bindKey +
                                                     "' while importing '" + importPath.importSpelling + "' from " +
                                                     importPath.importerFile + ":" + std::to_string(importPath.line));
                        }
                        seenNames.insert(bindKey);
                        if (!importShare.empty()) {
                            currentRecord.sharesByName[bindKey] = importShare;
                        }
                        resolvedBody.push_back(std::move(importedStmt));
                        continue;
                    }

                    std::string name = declarationName(importedStmt);
                    if (name.empty()) {
                        continue;
                    }

                    if (!requestedNames.empty() && requestedNames.find(name) == requestedNames.end()) {
                        continue;
                    }

                    if (!declarationVisible(name, importedRecord, metadata.bundles, importDecl)) {
                        continue;
                    }

                    if (!requestedNames.empty()) {
                        requestedNames.erase(name);
                        auto renameIt = requestedRenames.find(name);
                        if (renameIt != requestedRenames.end() && renameIt->second != name) {
                            if (!renameDeclaration(importedStmt, renameIt->second)) {
                                throw std::runtime_error("Cannot alias imported declaration '" + name +
                                                         "' from " + importedRecord.sourcePath.string());
                            }
                            name = renameIt->second;
                        }
                    }

                    if (seenNames.find(name) != seenNames.end()) {
                        throw std::runtime_error("Duplicate symbol after splice: '" + name +
                                                 "' while importing '" + importPath.importSpelling + "' from " +
                                                 importPath.importerFile + ":" + std::to_string(importPath.line));
                    }

                    seenNames.insert(name);
                    if (!importShare.empty()) {
                        currentRecord.sharesByName[name] = importShare;
                    }
                    resolvedBody.push_back(std::move(importedStmt));
                }

                if (!requestedNames.empty()) {
                    auto missing = *requestedNames.begin();
                    throw std::runtime_error("Imported symbol '" + missing + "' is not exported by " +
                                             importedRecord.sourcePath.string() + " (from '" +
                                             importPath.importSpelling + "' at " + importPath.importerFile + ":" +
                                             std::to_string(importPath.line) + ")");
                }

                importedRecord.emitted = true;
                continue;
                }
            }

            std::string name = declarationName(stmt);
            if (!name.empty()) {
                seenNames.insert(name);
            }
            resolvedBody.push_back(std::move(stmt));
        }

        module->body = std::move(resolvedBody);
        currentRecord.module = std::move(module);
        currentRecord.state = ModuleState::Resolved;
        if (!isRoot) {
            currentRecord.emitted = false;
        }
        topologicalOrder_.push_back(currentKey);
    } catch (...) {
        currentRecord.state = ModuleState::Failed;
        activeStack_.pop_back();
        throw;
    }

    activeStack_.pop_back();
    return currentKey;
}

ModuleRegistry::ResolvedImportPath ModuleRegistry::resolveImportPath(const ast::ImportDeclaration* importDecl,
                                                                     const fs::path& importingFile) const {
    if (!importDecl || !importDecl->source) {
        throw std::runtime_error("Malformed import declaration");
    }

    if (importDecl->defaultImport || importDecl->namespaceImport) {
        throw std::runtime_error("Unsupported import form: default and namespace imports are not yet supported (" +
                                 importDecl->toString() + ")");
    }

    ResolvedImportPath result;
    result.importSpelling = importDecl->toString();
    result.line = importDecl->loc.line;
    result.importerFile = importingFile.string();

    fs::path importerDir = importingFile.parent_path();
    if (importDecl->locator) {
        const std::string& locator = importDecl->locator->value;
        if (locator.find("://") != std::string::npos) {
            throw std::runtime_error("Unsupported import locator protocol in '" + result.importSpelling + "'");
        }

        fs::path located(locator);
        fs::path candidate = normalizePath(located.is_absolute() ? located : importerDir / located);
        result.triedPaths.push_back(candidate);
        if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
            result.resolvedPath = candidate;
            return result;
        }

        std::ostringstream message;
        message << "Module file not found for import '" << result.importSpelling << "' from "
                << importingFile.string() << ":" << result.line << "\nTried paths:";
        for (const auto& tried : result.triedPaths) {
            message << "\n - " << tried.string();
        }
        throw std::runtime_error(message.str());
    }

    fs::path relativeModule = modulePathRelativeFile(importDecl->source->value);
    std::vector<fs::path> searchRoots = buildSearchRoots(importingFile);
    for (const auto& root : searchRoots) {
        fs::path fileCandidate = normalizePath(root / relativeModule);
        result.triedPaths.push_back(fileCandidate);
        if (fs::exists(fileCandidate) && fs::is_regular_file(fileCandidate)) {
            result.resolvedPath = fileCandidate;
            return result;
        }

        fs::path dirCandidate = normalizePath(root / relativeModule.parent_path() /
                                              relativeModule.stem() / "mod.vyb");
        result.triedPaths.push_back(dirCandidate);
        if (fs::exists(dirCandidate) && fs::is_regular_file(dirCandidate)) {
            result.resolvedPath = dirCandidate;
            return result;
        }
    }

    std::ostringstream message;
    message << "Module file not found for import '" << result.importSpelling << "' from "
            << importingFile.string() << ":" << result.line << "\nTried paths:";
    for (const auto& tried : result.triedPaths) {
        message << "\n - " << tried.string();
    }
    throw std::runtime_error(message.str());
}

std::vector<fs::path> ModuleRegistry::buildSearchRoots(const fs::path& importingFile) const {
    std::vector<fs::path> roots;
    std::unordered_set<std::string> seen;

    auto addRoot = [&](const fs::path& root) {
        fs::path normalized = normalizePath(root);
        std::string key = normalized.string();
        if (seen.insert(key).second) {
            roots.push_back(normalized);
        }
    };

    addRoot(importingFile.parent_path());
    for (const auto& configured : configuredSearchPaths_) {
        addRoot(configured);
    }
    return roots;
}

std::vector<fs::path> ModuleRegistry::buildConfiguredSearchPaths() const {
    std::vector<fs::path> paths;
    std::unordered_set<std::string> seen;

    auto addPath = [&](const fs::path& path) {
        fs::path normalized = normalizePath(path);
        std::string key = normalized.string();
        if (seen.insert(key).second) {
            paths.push_back(normalized);
        }
    };

    for (const auto& path : options_.cliModulePaths) {
        addPath(path);
    }

    if (const char* envPaths = std::getenv("VYB_MODULE_PATH")) {
        std::stringstream stream(envPaths);
        std::string segment;
        while (std::getline(stream, segment, ':')) {
            segment = trimCopy(segment);
            if (!segment.empty()) {
                addPath(segment);
            }
        }
    }

    if (auto stdlibRoot = discoverStdlibRoot()) {
        addPath(*stdlibRoot);
    }

    return paths;
}

fs::path ModuleRegistry::normalizePath(const fs::path& path) const {
    std::error_code ec;
    fs::path absolute = fs::absolute(path, ec);
    if (ec) {
        absolute = path;
    }

    fs::path normalized = fs::weakly_canonical(absolute, ec);
    if (ec) {
        normalized = absolute.lexically_normal();
    }
    return normalized;
}

std::optional<fs::path> ModuleRegistry::discoverStdlibRoot() const {
    if (const char* stdlibEnv = std::getenv("VYB_STDLIB")) {
        std::string value = trimCopy(stdlibEnv);
        if (!value.empty()) {
            return normalizePath(value);
        }
    }

    if (!options_.executablePath.empty()) {
        fs::path exeDir = normalizePath(options_.executablePath).parent_path();
        std::vector<fs::path> probes = {
            exeDir / ".." / "stdlib",
            exeDir / "stdlib",
        };
        for (const auto& probe : probes) {
            fs::path normalized = normalizePath(probe);
            if (fs::exists(normalized) && fs::is_directory(normalized)) {
                return normalized;
            }
        }
    }

    return std::nullopt;
}

std::string ModuleRegistry::readSourceFile(const fs::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Could not read imported module: " + path.string());
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

fs::path ModuleRegistry::modulePathRelativeFile(const std::string& modulePath) {
    fs::path relative;
    size_t start = 0;
    while (start < modulePath.size()) {
        size_t sep = modulePath.find("::", start);
        std::string segment = modulePath.substr(start, sep == std::string::npos ? std::string::npos : sep - start);
        if (!segment.empty()) {
            relative /= segment;
        }
        if (sep == std::string::npos) {
            break;
        }
        start = sep + 2;
    }
    if (!relative.has_extension()) {
        relative += ".vyb";
    }
    return relative;
}

ModuleRegistry::SourceMetadata ModuleRegistry::preprocessModuleSource(const std::string& source) {
    SourceMetadata metadata;
    std::stringstream input(source);
    std::stringstream cleaned;
    std::string line;
    std::optional<std::vector<std::string>> pendingShare;

    while (std::getline(input, line)) {
        std::string working = trimCopy(line);

        if (auto bundleArgs = consumeDirective(working, "bundle")) {
            metadata.bundles.insert(metadata.bundles.end(), bundleArgs->begin(), bundleArgs->end());
            if (working.empty()) {
                cleaned << '\n';
                continue;
            }
        }

        if (auto shareArgs = consumeDirective(working, "share")) {
            pendingShare = *shareArgs;
            if (working.empty()) {
                cleaned << '\n';
                continue;
            }
        }

        if (auto noCore = consumeDirective(working, "no_core")) {
            (void)noCore;
            metadata.coreAutoImport = false;
            if (working.empty()) {
                cleaned << '\n';
                continue;
            }
        }

        if (working.empty()) {
            cleaned << line << '\n';
            continue;
        }

        if (isImportLine(working)) {
            metadata.importShares.push_back(pendingShare.value_or(std::vector<std::string>{}));
            pendingShare.reset();
            cleaned << working << '\n';
            continue;
        }

        if (pendingShare) {
            std::string name = declarationNameFromLine(working);
            if (!name.empty()) {
                metadata.sharesByName[name] = *pendingShare;
                pendingShare.reset();
            }
            cleaned << working << '\n';
            continue;
        }

        cleaned << line << '\n';
    }

    metadata.source = cleaned.str();
    return metadata;
}

std::unique_ptr<ast::Module> ModuleRegistry::parseModuleOnly(const std::string& source, const std::string& fileName) {
    Lexer lexer(source, fileName);
    std::vector<vyb::token::Token> tokens = lexer.tokenize();
    vyb::Parser parser(tokens, fileName);
    auto ast = parser.parse_module();
    if (!ast) {
        throw std::runtime_error("Failed to parse source code: " + fileName);
    }
    return ast;
}

std::string ModuleRegistry::declarationName(const ast::StmtPtr& stmt) {
    if (auto* fn = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
        return fn->id ? fn->id->name : "";
    }
    if (auto* var = dynamic_cast<ast::VariableDeclaration*>(stmt.get())) {
        return var->id ? var->id->name : "";
    }
    if (auto* typeAlias = dynamic_cast<ast::TypeAliasDeclaration*>(stmt.get())) {
        return typeAlias->name ? typeAlias->name->name : "";
    }
    if (auto* st = dynamic_cast<ast::StructDeclaration*>(stmt.get())) {
        return st->name ? st->name->name : "";
    }
    if (auto* en = dynamic_cast<ast::EnumDeclaration*>(stmt.get())) {
        return en->name ? en->name->name : "";
    }
    if (auto* aspect = dynamic_cast<ast::AspectDeclaration*>(stmt.get())) {
        return aspect->name ? aspect->name->name : "";
    }
    if (auto* cls = dynamic_cast<ast::ClassDeclaration*>(stmt.get())) {
        return cls->name ? cls->name->name : "";
    }
    if (auto* bind = dynamic_cast<ast::BindDeclaration*>(stmt.get())) {
        if (bind->selfType && bind->traitType) {
            return "bind:" + bind->selfType->toString() + ":" + bind->traitType->toString();
        }
        return "bind";
    }
    return "";
}

bool ModuleRegistry::renameDeclaration(ast::StmtPtr& stmt, const std::string& newName) {
    if (auto* fn = dynamic_cast<ast::FunctionDeclaration*>(stmt.get())) {
        if (!fn->id) return false;
        fn->id->name = newName;
        return true;
    }
    if (auto* var = dynamic_cast<ast::VariableDeclaration*>(stmt.get())) {
        if (!var->id) return false;
        var->id->name = newName;
        return true;
    }
    if (auto* typeAlias = dynamic_cast<ast::TypeAliasDeclaration*>(stmt.get())) {
        if (!typeAlias->name) return false;
        typeAlias->name->name = newName;
        return true;
    }
    if (auto* st = dynamic_cast<ast::StructDeclaration*>(stmt.get())) {
        if (!st->name) return false;
        st->name->name = newName;
        return true;
    }
    if (auto* en = dynamic_cast<ast::EnumDeclaration*>(stmt.get())) {
        if (!en->name) return false;
        en->name->name = newName;
        return true;
    }
    if (auto* aspect = dynamic_cast<ast::AspectDeclaration*>(stmt.get())) {
        if (!aspect->name) return false;
        aspect->name->name = newName;
        return true;
    }
    if (auto* cls = dynamic_cast<ast::ClassDeclaration*>(stmt.get())) {
        if (!cls->name) return false;
        cls->name->name = newName;
        return true;
    }
    return false;
}

bool ModuleRegistry::isMainFunction(const ast::StmtPtr& stmt) {
    auto* fn = dynamic_cast<ast::FunctionDeclaration*>(stmt.get());
    return fn && fn->id && fn->id->name == "main";
}

bool ModuleRegistry::sharesAllow(const std::vector<std::string>& shares, const std::vector<std::string>& importerBundles) {
    for (const auto& share : shares) {
        if (share == "all") {
            return true;
        }
        if (std::find(importerBundles.begin(), importerBundles.end(), share) != importerBundles.end()) {
            return true;
        }
    }
    return false;
}

bool ModuleRegistry::declarationVisible(const std::string& name,
                                        const ModuleRecord& importedModule,
                                        const std::vector<std::string>& importerBundles,
                                        const ast::ImportDeclaration* importDecl) {
    if (importDecl && importDecl->kind == ast::ImportKind::Smuggle) {
        return true;
    }
    auto shareIt = importedModule.sharesByName.find(name);
    if (shareIt == importedModule.sharesByName.end()) {
        return false;
    }
    return sharesAllow(shareIt->second, importerBundles);
}

} // namespace vyb
