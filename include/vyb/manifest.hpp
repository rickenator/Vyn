// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vyb {

// A dependency declaration from [dependencies]. Supports path and version/git
// sources; only the path (local) source is resolved by the current build.
struct ManifestDependency {
    std::string name;
    std::string source = "version";   // "path", "git", or "version"
    std::string path;                 // source == "path": directory (repo-relative)
    std::string url;                  // source == "git"
    std::string version;
};

struct ManifestBin {
    std::string name;
    std::string path = "src/main.vyb";
};

// A parsed vyb.toml project manifest.
struct Manifest {
    std::string name = "unnamed";
    std::string version = "0.0.0";
    std::vector<ManifestBin> bins;
    std::vector<ManifestDependency> dependencies;
    std::filesystem::path rootDir;           // directory containing vyb.toml
    bool hasExplicitBins = false;            // false => use the src/main.vyb default
};

// Parse <rootDir>/vyb.toml. Returns std::nullopt (with a message in *error)
// when the file is missing or malformed.
std::optional<Manifest> load_manifest(const std::filesystem::path& rootDir,
                                      std::string* error = nullptr);

// Serialize a manifest back to TOML (used by `vyb new`).
std::string manifest_to_toml(const Manifest& manifest);

// Write a fresh manifest file for `vyb new`.
std::string default_manifest_toml(const std::string& name, const std::string& version);

} // namespace vyb
