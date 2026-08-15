// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace vyb {

// Forward declaration
class SemanticAnalyzer;

class Driver {
public:
    Driver() = default;

    // SemanticAnalyzer access for codegen
    void setSemanticAnalyzer(SemanticAnalyzer* analyzer) { semanticAnalyzer_ = analyzer; }
    SemanticAnalyzer* getSemanticAnalyzer() const { return semanticAnalyzer_; }
    bool hasSemanticAnalyzer() const { return semanticAnalyzer_ != nullptr; }

private:
    SemanticAnalyzer* semanticAnalyzer_ = nullptr;
};

} // namespace vyb
