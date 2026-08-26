// SPDX-License-Identifier: Apache-2.0
//
// Minimal vyb.toml manifest parser + project-model helpers backing the `vyb
// build` and `vyb new` subcommands. Only the TOML subset the manifest needs is
// supported: [table] and [[array-of-tables]] headers, `key = value` pairs, bare
// or double-quoted string values, integers, and inline tables `{ k = v, ... }`.

#include "vyb/manifest.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

namespace vyb {

namespace {

struct Table {
    std::string section;                       // package / bin / dependencies
    bool isArray = false;
    std::vector<std::pair<std::string, std::string>> keys;   // key -> raw value
};

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// Strip a line to before its first comment byte ('#'), honoring double quotes.
std::string strip_comment(const std::string& line) {
    bool inQ = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') inQ = !inQ;
        else if (line[i] == '#' && !inQ) return line.substr(0, i);
    }
    return line;
}

// Split "key = value" at the first '=' outside quotes.
bool split_kv(const std::string& line, std::string* key, std::string* value) {
    bool inQ = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') inQ = !inQ;
        else if (line[i] == '=' && !inQ) {
            *key = trim(line.substr(0, i));
            *value = trim(line.substr(i + 1));
            return !key->empty() && !value->empty();
        }
    }
    return false;
}

// Strip surrounding double quotes from a quoted string value.
std::string unquote(const std::string& raw) {
    std::string s = trim(raw);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.size() - 2);
    }
    return s;   // bare string / number: return as-is
}

// Parse an inline table `{ k = v, k2 = v2 }` into (key -> string value) pairs.
// Nested tables are not needed for the manifest subset.
std::vector<std::pair<std::string, std::string>> parse_inline_table(const std::string& raw) {
    std::vector<std::pair<std::string, std::string>> out;
    std::string body = trim(raw);
    if (body.size() >= 2 && body.front() == '{' && body.back() == '}') {
        body = body.substr(1, body.size() - 2);
    }
    // Split on commas that are not inside quotes.
    std::vector<std::string> parts;
    std::string cur;
    bool inQ = false;
    for (char c : body) {
        if (c == '"') inQ = !inQ;
        if (c == ',' && !inQ) { parts.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    parts.push_back(cur);
    for (const std::string& part : parts) {
        std::string k, v;
        if (split_kv(part, &k, &v)) out.emplace_back(k, unquote(v));
    }
    return out;
}

// The minimal top-level parse: one pass over vyb.toml into section tables.
std::vector<Table> parse_toml(const std::string& text, std::string* error) {
    std::vector<Table> tables;
    int arrayIndex = -1;          // index into tables[] for the open [[bin]]
    std::istringstream in(text);
    std::string line;
    int lineno = 0;
    auto currentSection = [&]() -> Table* {
        if (arrayIndex >= 0 && arrayIndex < static_cast<int>(tables.size()))
            return &tables[arrayIndex];
        if (!tables.empty()) return &tables.back();
        return nullptr;
    };

    while (std::getline(in, line)) {
        ++lineno;
        std::string t = trim(strip_comment(line));
        if (t.empty()) continue;

        if (t[0] == '[') {
            // Header: [[bin]] or [table].
            bool isArray = t.compare(0, 2, "[[") == 0;
            size_t close = t.rfind(']');
            if (close == std::string::npos) {
                if (error) *error = "malformed table header at line " + std::to_string(lineno);
                return {};
            }
            std::string name = trim(t.substr(isArray ? 2 : 1,
                                             close - (isArray ? 2 : 1)));
            tables.push_back(Table{name, isArray, {}});
            arrayIndex = isArray ? static_cast<int>(tables.size()) - 1 : -1;
            continue;
        }

        std::string key, value;
        if (!split_kv(t, &key, &value)) {
            if (error) *error = "could not parse key = value at line " + std::to_string(lineno);
            return {};
        }
        // #164: reject unsupported TOML value forms with a precise location instead
        // of silently mis-parsing them. The vyb manifest format deliberately does
        // NOT adopt full TOML: only bare/quoted strings, integers, and inline
        // tables { k = v, ... } are value forms.
        if (!value.empty() && value[0] == '[') {
            if (error)
                *error = "unsupported TOML array value for key '" + key + "' at line "
                         + std::to_string(lineno)
                         + ": the vyb manifest format supports only bare/quoted "
                           "strings, integers, and inline tables { k = v, ... }; full "
                           "TOML arrays are not supported (#164); see "
                           "doc/MANIFEST.md";
            return {};
        }
        Table* cur = currentSection();
        if (!cur) {
            if (error) *error = "key '" + key + "' appears before any [section] at line "
                              + std::to_string(lineno);
            return {};
        }
        cur->keys.emplace_back(key, value);
    }
    return tables;
}

const std::string* value_in(const std::vector<std::pair<std::string, std::string>>& keys,
                            const std::string& key) {
    for (const auto& kv : keys)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

} // namespace

std::optional<Manifest> load_manifest(const std::filesystem::path& rootDir,
                                      std::string* error) {
    std::filesystem::path file = rootDir / "vyb.toml";
    std::ifstream in(file);
    if (!in) {
        if (error) *error = "no manifest found: " + file.string();
        return std::nullopt;
    }
    std::stringstream ss;
    ss << in.rdbuf();

    auto tables = parse_toml(ss.str(), error);
    if (tables.empty() && !ss.str().empty()) return std::nullopt;

    Manifest m;
    m.rootDir = rootDir;

    for (const auto& t : tables) {
        if (t.section == "package") {
            if (const std::string* v = value_in(t.keys, "name")) m.name = unquote(*v);
            if (const std::string* v = value_in(t.keys, "version")) m.version = unquote(*v);
        } else if (t.section == "dependencies") {
            for (const auto& kv : t.keys) {
                ManifestDependency dep;
                dep.name = kv.first;
                std::string raw = trim(kv.second);
                if (!raw.empty() && raw.front() == '{') {
                    for (const auto& pk : parse_inline_table(raw)) {
                        if (pk.first == "path") { dep.source = "path"; dep.path = pk.second; }
                        else if (pk.first == "git") { dep.source = "git"; dep.url = pk.second; }
                        else if (pk.first == "version") { dep.source = "version"; dep.version = pk.second; }
                        else if (pk.first == "tag") { dep.version = pk.second; }
                    }
                } else {
                    dep.source = "version";
                    dep.version = unquote(kv.second);
                }
                if (dep.source == "path" && dep.path.empty()) dep.source = "version";
                if (dep.source == "version" && dep.version.empty()) dep.source = "path";
                m.dependencies.push_back(std::move(dep));
            }
        } else if (t.section == "bin") {
            ManifestBin b;
            if (const std::string* v = value_in(t.keys, "name")) b.name = unquote(*v);
            if (const std::string* v = value_in(t.keys, "path")) b.path = unquote(*v);
            m.bins.push_back(std::move(b));
        }
        // Unknown tables are ignored (forward compatibility).
    }

    if (!m.bins.empty()) m.hasExplicitBins = true;
    if (!m.hasExplicitBins) m.bins.push_back(ManifestBin{});
    if (m.name == "unnamed") {
        std::string nm = rootDir.filename().string();
        if (!nm.empty()) m.name = nm;
    }
    if (m.bins[0].name.empty()) m.bins[0].name = m.name;
    return m;
}

std::string default_manifest_toml(const std::string& name, const std::string& version) {
    std::ostringstream out;
    out << "[package]\n"
           "name = \"" << name << "\"\n"
           "version = \"" << version << "\"\n"
           "\n"
           "[[bin]]\n"
           "name = \"" << name << "\"\n"
           "path = \"src/main.vyb\"\n"
           "\n"
           "# Local path dependencies (resolved by `vyb build`):\n"
           "# [dependencies]\n"
           "# mylib = { path = \"../mylib\" }\n";
    return out.str();
}

std::string manifest_to_toml(const Manifest& manifest) {
    std::ostringstream out;
    out << "[package]\n"
           "name = \"" << manifest.name << "\"\n"
           "version = \"" << manifest.version << "\"\n";
    if (manifest.hasExplicitBins) {
        for (const auto& b : manifest.bins) {
            out << "\n[[bin]]\nname = \"" << b.name << "\"\npath = \"" << b.path << "\"\n";
        }
    }
    if (!manifest.dependencies.empty()) {
        out << "\n[dependencies]\n";
        for (const auto& d : manifest.dependencies) {
            out << d.name << " = { " << d.source << " = \"" << d.path << "\", version = \""
                << d.version << "\" }\n";
        }
    }
    return out.str();
}

} // namespace vyb
