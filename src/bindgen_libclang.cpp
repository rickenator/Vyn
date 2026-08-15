// SPDX-License-Identifier: Apache-2.0

#ifdef VYB_BINDGEN_LIBCLANG

#include "vyb/bindgen.hpp"

#include <clang-c/Index.h>

#include <algorithm>
#include <cstdint>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace vyb {
namespace bindgen {

namespace {

// --- small helpers ---------------------------------------------------------

std::string curSpell(CXCursor c) {
    CXString s = clang_getCursorSpelling(c);
    std::string out = clang_getCString(s);
    clang_disposeString(s);
    return out;
}

std::string typeSpell(CXType t) {
    CXString s = clang_getTypeSpelling(t);
    std::string out = clang_getCString(s);
    clang_disposeString(s);
    return out;
}

// Strips a leading C tag ("struct ", "enum ", "union "); returns the bare name.
std::string stripTag(const std::string& spell) {
    for (const char* p : {"struct ", "enum ", "union "}) {
        size_t n = 0;
        for (; p[n]; ++n) {}
        if (spell.compare(0, n, p) == 0) return spell.substr(n);
    }
    return spell;
}

// Parses a C integer literal token (decimal/hex/binary, optional u/l suffix).
bool parseIntText(const std::string& raw, long long& out) {
    std::string s = raw;
    while (!s.empty() && (s.back() == 'u' || s.back() == 'U' ||
                          s.back() == 'l' || s.back() == 'L'))
        s.pop_back();
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    long long v = 0;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B'))
        v = strtoll(s.c_str() + 2, &end, 2);
    else
        v = strtoll(s.c_str(), &end, 0);
    if (end == nullptr || *end != '\0' || errno != 0) return false;
    out = v;
    return true;
}

bool isIdentText(const std::string& t) {
    if (t.empty() || !(std::isalpha(static_cast<unsigned char>(t[0])) || t[0] == '_'))
        return false;
    return std::all_of(t.begin(), t.end(), [](char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
    });
}

bool isIntLiteral(const std::string& t) {
    if (t.empty()) return false;
    char c = t[0];
    if (std::isdigit(static_cast<unsigned char>(c))) return true;
    if ((c == '+' || c == '-') && t.size() > 1 &&
        std::isdigit(static_cast<unsigned char>(t[1])))
        return true;
    return false;
}

bool isFloatLiteral(const std::string& t) {
    if (t.empty() || t == "e" || t == "E") return false;
    bool dot = false, exp = false;
    size_t i = 0;
    if (t[i] == '+' || t[i] == '-') ++i;
    if (i >= t.size()) return false;
    for (; i < t.size(); ++i) {
        char c = t[i];
        if (c == '.') { if (dot || exp) return false; dot = true; }
        else if (c == 'e' || c == 'E') { if (exp || !i) return false; exp = true; }
        else if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return dot || exp;
}

bool isStringLiteral(const std::string& t) {
    return t.size() >= 2 && t.front() == '"' && t.back() == '"';
}

static bool isChar(CXType t) {
    return t.kind == CXType_SChar || t.kind == CXType_Char_S || t.kind == CXType_Char_U;
}

static std::string intType(long long v) {
    return (v >= INT32_MIN && v <= INT32_MAX) ? "CInt" : "Int";
}

// Tiny recursive-descent integer evaluator for `#define` constant expressions
// (+, -, *, /, %, <<, >>, |, ^, &, parens, unary +/-/~). Identifiers resolve
// against previously-bound integer-constant macros.
class IntEval {
public:
    IntEval(const std::vector<std::string>& toks,
            const std::map<std::string, long long>& env)
        : toks_(toks), env_(env) {}

    bool parse(long long& out) {
        p_ = 0;
        if (!bor(out)) return false;
        return p_ == toks_.size();
    }

private:
    const std::vector<std::string>& toks_;
    const std::map<std::string, long long>& env_;
    size_t p_ = 0;

    bool peek(const char* s) const { return p_ < toks_.size() && toks_[p_] == s; }
    bool match(const char* s) {
        if (peek(s)) { ++p_; return true; }
        return false;
    }

    bool primary(long long& v) {
        if (p_ >= toks_.size()) return false;
        const std::string& t = toks_[p_];
        if (t == "(") {
            ++p_;
            if (!bor(v)) return false;
            return match(")");
        }
        if (parseIntText(t, v)) { ++p_; return true; }
        auto it = env_.find(t);
        if (it != env_.end()) { v = it->second; ++p_; return true; }
        return false;
    }

    bool unary(long long& v) {
        if (match("-")) { long long x; if (!unary(x)) return false; v = -x; return true; }
        if (match("+")) { return unary(v); }
        if (match("~")) { long long x; if (!unary(x)) return false; v = ~x; return true; }
        return primary(v);
    }

    bool bor(long long& v) {   // |
        if (!bxor(v)) return false;
        while (match("|")) { long long r; if (!bxor(r)) return false; v |= r; }
        return true;
    }
    bool bxor(long long& v) {  // ^
        if (!band(v)) return false;
        while (match("^")) { long long r; if (!band(r)) return false; v ^= r; }
        return true;
    }
    bool band(long long& v) {  // &
        if (!shift(v)) return false;
        while (match("&")) { long long r; if (!shift(r)) return false; v &= r; }
        return true;
    }
    bool shift(long long& v) { // << >>
        if (!add(v)) return false;
        for (;;) {
            if (match("<<")) { long long r; if (!add(r)) return false; v <<= r; }
            else if (match(">>")) { long long r; if (!add(r)) return false; v >>= r; }
            else return true;
        }
    }
    bool add(long long& v) {   // + -
        if (!mul(v)) return false;
        for (;;) {
            if (match("+")) { long long r; if (!mul(r)) return false; v += r; }
            else if (match("-")) { long long r; if (!mul(r)) return false; v -= r; }
            else return true;
        }
    }
    bool mul(long long& v) {   // * / %
        if (!unary(v)) return false;
        for (;;) {
            if (match("*")) { long long r; if (!unary(r)) return false; v *= r; }
            else if (match("/")) { long long r; if (!unary(r)) return false; if (!r) return false; v /= r; }
            else if (match("%")) { long long r; if (!unary(r)) return false; if (!r) return false; v %= r; }
            else return true;
        }
    }
};

// --- generator ------------------------------------------------------------

struct Field { std::string name; std::string type; };
struct Param { std::string name; std::string type; };

class Generator {
public:
    Generator(CXTranslationUnit tu, CXFile mainFile) : tu_(tu), mainFile_(mainFile) {}

    std::string run(std::vector<std::string>& warnings) {
        warnings_ = &warnings;
        clang_visitChildren(clang_getTranslationUnitCursor(tu_), &topLevelVisitor, this);
        std::ostringstream os;
        os << "# Auto-generated by `vyb bindgen --full` (libclang). Do not edit.\n\n";
        for (const auto& chunk : chunks_) os << chunk;
        return os.str();
    }

    bool inMainFile(CXSourceLocation loc) const {
        CXFile f;
        unsigned line, col, off;
        clang_getSpellingLocation(loc, &f, &line, &col, &off);
        return f && clang_File_isEqual(f, mainFile_);
    }

    void warn(const std::string& msg) { if (warnings_) warnings_->push_back(msg); }

    // --- emission ----------------------------------------------------------

    void emitStructNamed(CXCursor structCur, const std::string& name) {
        if (!emitted_.insert(name).second) { warn("skipping duplicate struct '" + name + "'"); return; }
        std::vector<Field> fields;
        bool sawBitfield = false;
        bool unsupported = false;
        structCtx_ = &fields;
        structBitfield_ = &sawBitfield;
        structUnsupported_ = &unsupported;
        clang_visitChildren(structCur, &fieldVisitor, this);
        structCtx_ = nullptr;
        structBitfield_ = nullptr;
        structUnsupported_ = nullptr;
        if (sawBitfield) {
            warn("skipping struct '" + name + "': contains bitfields that cannot be ABI-represented");
            return;
        }
        if (unsupported) {
            warn("skipping struct '" + name + "': has a field with an unsupported type");
            return;
        }
        std::ostringstream os;
        os << "share(all)\n#[repr(C)]\nstruct " << name << " {\n";
        for (auto& f : fields) os << "    " << f.name << "<" << f.type << ">,\n";
        os << "}\n\n";
        chunks_.push_back(os.str());
    }

    void emitEnumNamed(CXCursor enumCur, const std::string& name) {
        if (!emitted_.insert(name).second) { warn("skipping duplicate enum '" + name + "'"); return; }
        std::vector<std::string> variants;
        bool explicitValues = false;
        enumCtx_ = &variants;
        enumExplicit_ = &explicitValues;
        clang_visitChildren(enumCur, &enumVisitor, this);
        enumCtx_ = nullptr;
        enumExplicit_ = nullptr;
        if (explicitValues) {
            warn("skipping enum '" + name + "': has explicit values; Vyb enums are sequential from 0");
            return;
        }
        std::ostringstream os;
        os << "share(all)\nenum " << name << " { ";
        for (size_t i = 0; i < variants.size(); ++i) {
            if (i) os << ", ";
            os << variants[i];
        }
        os << " }\n\n";
        chunks_.push_back(os.str());
    }

    void emitFunction(CXCursor c) {
        std::string name = curSpell(c);
        CXType ftype = clang_getCursorType(c);
        std::string ret = mapType(clang_getResultType(ftype));
        std::vector<Param> params;
        int n = clang_Cursor_getNumArguments(c);
        for (int i = 0; i < n; ++i) {
            CXCursor pk = clang_Cursor_getArgument(c, i);
            Param pm;
            pm.name = curSpell(pk);
            if (pm.name.empty()) pm.name = "a" + std::to_string(i);
            pm.type = mapType(clang_getCursorType(pk));
            if (pm.type.empty()) {
                warn("skipping function '" + name + "': unsupported parameter type");
                return;
            }
            params.push_back(std::move(pm));
        }
        if (!emitted_.insert(name).second) { warn("skipping duplicate declaration '" + name + "'"); return; }
        bool varargs = clang_isFunctionTypeVariadic(ftype);
        std::ostringstream os;
        os << "share(all)\n" << name << "(";
        for (size_t i = 0; i < params.size(); ++i) {
            if (i) os << ", ";
            os << params[i].name << "<" << params[i].type << ">";
        }
        if (varargs) {
            if (!params.empty()) os << ", ";
            os << "...";
        }
        os << ")<" << ret << ">\n\n";
        chunks_.push_back(os.str());
    }

    // --- macros ------------------------------------------------------------

    struct MacroDef {
        std::string name;
        bool funcLike = false;
        std::vector<std::string> params;
        std::vector<std::string> body;
    };

    void handleMacro(CXCursor c) {
        MacroDef m = extractMacro(c);
        if (m.name.empty() || m.body.empty()) return;  // `#define X` include guards etc.
        if (emitted_.count(m.name)) {
            warn("skipping duplicate macro '" + m.name + "'");
            return;
        }

        if (!m.funcLike) {
            if (m.body.size() == 1) {
                const std::string& tok = m.body[0];
                long long v;
                if (isIntLiteral(tok) && parseIntText(tok, v)) {
                    constVal_[m.name] = v;
                    emitConstMacro(m.name, tok, intType(v));
                    return;
                }
                if (isFloatLiteral(tok)) { emitConstMacro(m.name, tok, "Float"); return; }
                if (isStringLiteral(tok)) { emitConstMacro(m.name, tok, "String"); return; }
            }
            long long v = 0;
            IntEval eval(m.body, constVal_);
            if (eval.parse(v)) {
                constVal_[m.name] = v;
                emitConstMacro(m.name, std::to_string(v), intType(v));
                return;
            }
            warn("skipping object-like macro '" + m.name + "': unsupported expression body");
            return;
        }

        // function-like: bind as an integer-const Vyb function
        std::set<std::string> params(m.params.begin(), m.params.end());
        std::string expr;
        if (!translateBody(m.body, params, expr)) {
            warn("skipping function-like macro '" + m.name +
                 "': body is not an integer-arithmetic expression over its params");
            return;
        }
        emitted_.insert(m.name);
        std::ostringstream os;
        os << "share(all)\n" << m.name << "(";
        for (size_t i = 0; i < m.params.size(); ++i) {
            if (i) os << ", ";
            os << m.params[i] << "<Int>";
        }
        os << ")<Int> -> {\n    return " << expr << "\n}\n\n";
        chunks_.push_back(os.str());
    }

    MacroDef extractMacro(CXCursor c) {
        MacroDef m;
        m.funcLike = clang_Cursor_isMacroFunctionLike(c);
        CXSourceRange r = clang_getCursorExtent(c);
        CXToken* toks = nullptr;
        unsigned n = 0;
        clang_tokenize(tu_, r, &toks, &n);
        std::vector<std::string> texts;
        for (unsigned i = 0; i < n; ++i)
            texts.push_back(clang_getCString(clang_getTokenSpelling(tu_, toks[i])));
        clang_disposeTokens(tu_, toks, n);
        if (texts.empty()) return m;
        m.name = texts[0];
        if (m.funcLike && texts.size() > 1 && texts[1] == "(") {
            size_t depth = 0, close = (size_t)-1;
            for (size_t i = 1; i < texts.size(); ++i) {
                if (texts[i] == "(") ++depth;
                else if (texts[i] == ")") { --depth; if (depth == 0) { close = i; break; } }
            }
            if (close == (size_t)-1) return m;  // malformed
            for (size_t i = 2; i < close; ++i)
                if (texts[i] != "," && isIdentText(texts[i])) m.params.push_back(texts[i]);
            for (size_t i = close + 1; i < texts.size(); ++i) m.body.push_back(texts[i]);
        } else {
            for (size_t i = 1; i < texts.size(); ++i) m.body.push_back(texts[i]);
        }
        return m;
    }

    // Passes through integer-arithmetic tokens/operators/params, inlining
    // previously-bound integer constant macros. Anything else fails.
    bool translateBody(const std::vector<std::string>& toks,
                       const std::set<std::string>& params, std::string& expr) {
        static const std::set<std::string> ok = {
            "(", ")", "+", "-", "*", "/", "%", "<<", ">>", "&", "|", "^", "~"};
        std::string out;
        for (const auto& t : toks) {
            if (isIntLiteral(t) || isFloatLiteral(t)) { out += t; continue; }
            if (ok.count(t)) { out += t; continue; }
            if (isIdentText(t)) {
                if (params.count(t)) { out += t; continue; }
                auto cv = constVal_.find(t);
                if (cv != constVal_.end()) { out += std::to_string(cv->second); continue; }
                return false;
            }
            return false;
        }
        expr = out;
        return !expr.empty();
    }

    void emitConstMacro(const std::string& name, const std::string& text,
                        const std::string& type) {
        emitted_.insert(name);
        std::ostringstream os;
        os << "share(all)\n" << name << "()<" << type << "> -> {\n    return " << text
           << "\n}\n\n";
        chunks_.push_back(os.str());
    }

    // --- type mapping ------------------------------------------------------

    std::string mapType(CXType t) {
        // CXType_Elaborated / CXType_Typedef keep the declared spelling (e.g.
        // `size_t`), which canonicalizes to `unsigned long`. Map the C size
        // aliases specially so the CSize ABI name is preserved.
        if (t.kind == CXType_Elaborated || t.kind == CXType_Typedef) {
            std::string spell = stripTag(typeSpell(t));
            auto it = aliases_.find(spell);
            if (it != aliases_.end()) return it->second;
            if (spell == "size_t") return "CSize";
            if (spell == "ssize_t") return "CSSize";
            if (spell == "ptrdiff_t") return "CSSize";
        }
        switch (t.kind) {
            case CXType_Elaborated: {
                std::string spell = stripTag(typeSpell(t));
                auto it = aliases_.find(spell);
                if (it != aliases_.end()) return it->second;
                // Not a declared alias: if it canonicalizes to a record/enum it
                // is a struct/enum forward reference kept by name; otherwise it
                // is a typedef alias to a primitive (e.g. <stdint.h> int32_t),
                // resolved through its canonical type.
                CXType canon = clang_getCanonicalType(t);
                if (canon.kind == CXType_Record || canon.kind == CXType_Enum)
                    return spell;
                return mapCanonical(canon);
            }
            case CXType_Typedef: {
                return mapCanonical(clang_getCanonicalType(t));
            }
            default:
                return mapCanonical(t);
        }
    }

    std::string mapCanonical(CXType t) {
        switch (t.kind) {
            case CXType_Void: return "CVoid";
            case CXType_Char_U: case CXType_Char_S: case CXType_SChar: return "CChar";
            case CXType_UChar: return "CUChar";
            case CXType_Short: return "CShort";
            case CXType_UShort: return "CUShort";
            case CXType_Int: return "CInt";
            case CXType_UInt: return "CUInt";
            case CXType_Long: return "CLong";
            case CXType_ULong: return "CULong";
            case CXType_LongLong: return "Int";
            case CXType_ULongLong: return "UInt64";
            case CXType_Float: return "CFloat";
            case CXType_Double: case CXType_LongDouble: return "CDouble";
            case CXType_Record: case CXType_Enum: {
                std::string spell = stripTag(typeSpell(t));
                auto it = aliases_.find(spell);
                return it != aliases_.end() ? it->second : spell;
            }
            case CXType_Pointer: {
                CXType pointee = clang_getPointeeType(t);
                if (pointee.kind == CXType_FunctionProto || pointee.kind == CXType_FunctionNoProto)
                    return "loc<" + mapFnPointer(pointee) + ">";
                if (isChar(pointee) && clang_getPointeeType(pointee).kind == CXType_Invalid)
                    return "CString";  // char* -> CString
                return "loc<" + mapType(pointee) + ">";
            }
            case CXType_ConstantArray:
            case CXType_IncompleteArray:
            case CXType_VariableArray:
            case CXType_DependentSizedArray: {
                CXType elt = clang_getArrayElementType(t);
                if (isChar(elt)) return "CString";
                return "loc<" + mapType(elt) + ">";
            }
            case CXType_FunctionProto:
            case CXType_FunctionNoProto:
                return mapFnPointer(t);
            default:
                return "";
        }
    }

    std::string mapFnPointer(CXType f) {
        std::string ret = mapType(clang_getResultType(f));
        std::string params;
        int n = clang_getNumArgTypes(f);
        for (int i = 0; i < n; ++i) {
            if (i) params += ", ";
            params += mapType(clang_getArgType(f, i));
        }
        if (clang_isFunctionTypeVariadic(f)) {
            if (n > 0) params += ", ";
            params += "...";
        }
        return "fn(" + params + ") -> " + ret;
    }

    // --- declarations / visitors ------------------------------------------

    void handleTypedef(CXCursor c) {
        std::string name = curSpell(c);
        if (name == "size_t") { aliases_[name] = "CSize"; return; }
        CXType underlying = clang_getTypedefDeclUnderlyingType(c);
        if (underlying.kind == CXType_Elaborated)
            underlying = clang_getCanonicalType(underlying);
        if (underlying.kind == CXType_Record || underlying.kind == CXType_Enum) {
            CXCursor decl = clang_getTypeDeclaration(underlying);
            std::string tag = curSpell(decl);
            if (tag.empty()) {
                if (!aliases_.insert({name, name}).second) { warn("skipping duplicate typedef '" + name + "'"); return; }
                if (underlying.kind == CXType_Record) emitStructNamed(decl, name);
                else emitEnumNamed(decl, name);
            } else {
                if (!aliases_.insert({name, tag}).second) warn("skipping duplicate typedef '" + name + "'");
            }
            return;
        }
        std::string mapped = mapType(underlying);
        if (mapped.empty()) { warn("skipping typedef '" + name + "': unsupported underlying type"); return; }
        if (!aliases_.insert({name, mapped}).second) warn("skipping duplicate typedef '" + name + "'");
    }

    static enum CXChildVisitResult topLevelVisitor(CXCursor c, CXCursor p, CXClientData d) {
        return static_cast<Generator*>(d)->topLevel(c, p);
    }
    static enum CXChildVisitResult fieldVisitor(CXCursor c, CXCursor p, CXClientData d) {
        return static_cast<Generator*>(d)->field(c, p);
    }
    static enum CXChildVisitResult enumVisitor(CXCursor c, CXCursor p, CXClientData d) {
        return static_cast<Generator*>(d)->enumConstant(c, p);
    }

    enum CXChildVisitResult topLevel(CXCursor c, CXCursor p) {
        (void)p;
        if (!inMainFile(clang_getCursorLocation(c))) return CXChildVisit_Continue;
        switch (c.kind) {
            case CXCursor_LinkageSpec: return CXChildVisit_Recurse;
            case CXCursor_StructDecl:
                if (clang_isCursorDefinition(c)) emitStructNamed(c, curSpell(c));
                return CXChildVisit_Continue;
            case CXCursor_UnionDecl:
                warn("skipping union '" + curSpell(c) + "': unions are not yet ABI-represented");
                return CXChildVisit_Continue;
            case CXCursor_EnumDecl:
                if (clang_isCursorDefinition(c)) emitEnumNamed(c, curSpell(c));
                return CXChildVisit_Continue;
            case CXCursor_TypedefDecl: handleTypedef(c); return CXChildVisit_Continue;
            case CXCursor_FunctionDecl: emitFunction(c); return CXChildVisit_Continue;
            case CXCursor_MacroDefinition: handleMacro(c); return CXChildVisit_Continue;
            default: return CXChildVisit_Continue;
        }
    }

    enum CXChildVisitResult field(CXCursor c, CXCursor p) {
        (void)p;
        if (c.kind != CXCursor_FieldDecl) return CXChildVisit_Continue;
        if (clang_Cursor_isBitField(c)) { if (structBitfield_) *structBitfield_ = true; return CXChildVisit_Continue; }
        std::string ftype = mapType(clang_getCursorType(c));
        if (ftype.empty()) { if (structUnsupported_) *structUnsupported_ = true; return CXChildVisit_Continue; }
        if (structCtx_) structCtx_->push_back({curSpell(c), ftype});
        return CXChildVisit_Continue;
    }

    enum CXChildVisitResult enumConstant(CXCursor c, CXCursor p) {
        (void)p;
        if (c.kind != CXCursor_EnumConstantDecl) return CXChildVisit_Continue;
        if (enumExplicit_ && hasExplicitValue(c)) *enumExplicit_ = true;
        if (enumCtx_) enumCtx_->push_back(curSpell(c));
        return CXChildVisit_Continue;
    }

    bool hasExplicitValue(CXCursor c) {
        CXSourceRange r = clang_getCursorExtent(c);
        CXToken* toks = nullptr;
        unsigned n = 0;
        clang_tokenize(tu_, r, &toks, &n);
        bool explicitVal = false;
        for (unsigned i = 0; i < n && !explicitVal; ++i)
            if (std::string(clang_getCString(clang_getTokenSpelling(tu_, toks[i]))) == "=")
                explicitVal = true;
        clang_disposeTokens(tu_, toks, n);
        return explicitVal;
    }

    CXTranslationUnit tu_;
    CXFile mainFile_;
    std::vector<std::string>* warnings_ = nullptr;
    std::vector<std::string> chunks_;
    std::set<std::string> emitted_;
    std::map<std::string, std::string> aliases_;
    std::map<std::string, long long> constVal_;

    std::vector<Field>* structCtx_ = nullptr;
    bool* structBitfield_ = nullptr;
    bool* structUnsupported_ = nullptr;
    std::vector<std::string>* enumCtx_ = nullptr;
    bool* enumExplicit_ = nullptr;
};

} // namespace

std::string generateBindingsFull(const std::string& headerPath,
                                 const std::vector<std::string>& cmdArgs,
                                 std::vector<std::string>* warnings) {
    CXIndex idx = clang_createIndex(0, 0);
    std::vector<std::string> owned;
    std::vector<const char*> args;
    args.push_back("-x");
    args.push_back("c-header");
    size_t slash = headerPath.find_last_of('/');
    if (slash != std::string::npos && slash > 0) {
        owned.push_back("-I" + headerPath.substr(0, slash));
        args.push_back(owned.back().c_str());
    }
    for (const auto& a : cmdArgs) { owned.push_back(a); args.push_back(owned.back().c_str()); }

    CXTranslationUnit tu =
        clang_parseTranslationUnit(idx, headerPath.c_str(), args.data(),
                                   static_cast<int>(args.size()), nullptr, 0,
                                   CXTranslationUnit_DetailedPreprocessingRecord);
    if (!tu) {
        if (warnings) warnings->push_back("libclang failed to parse the header");
        clang_disposeIndex(idx);
        return "";
    }
    CXFile mainFile = clang_getFile(tu, headerPath.c_str());
    std::vector<std::string> out;
    Generator gen(tu, mainFile);
    std::string result = gen.run(out);
    if (warnings) { for (auto& w : out) warnings->push_back(w); }
    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(idx);
    return result;
}

} // namespace bindgen
} // namespace vyb

#endif // VYB_BINDGEN_LIBCLANG
