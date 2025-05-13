// Implementations for AST nodes
// Note: Most of these are just placeholders and need to be implemented correctly.

#include <sstream>
#include <stdexcept> // Required for std::runtime_error
#include "vyn/ast.hpp"
#include "vyn/token.hpp" // Required for token_type_to_string

namespace Vyn::AST {

    // SourceLocation
    // Constructor is defined inline in ast.hpp
    // toString() was not declared in ast.hpp for SourceLocation struct

    // Node
    // Constructor is defined inline in ast.hpp

    // Literal Nodes
    IntLiteralNode::IntLiteralNode(SourceLocation loc, long long val, Node* p) : LiteralNode(NodeType::INT_LITERAL, std::move(loc), p), value(val) {}
    void IntLiteralNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string IntLiteralNode::toString(int indent) const {
        std::stringstream ss;
        ss << "IntLiteralNode(" << value << ")";
        return ss.str();
    }

    FloatLiteralNode::FloatLiteralNode(SourceLocation loc, double val, Node* p) : LiteralNode(NodeType::FLOAT_LITERAL, std::move(loc), p), value(val) {}
    void FloatLiteralNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string FloatLiteralNode::toString(int indent) const {
        std::stringstream ss;
        ss << "FloatLiteralNode(" << value << ")";
        return ss.str();
    }

    StringLiteralNode::StringLiteralNode(SourceLocation loc, std::string val, Node* p) : LiteralNode(NodeType::STRING_LITERAL, std::move(loc), p), value(std::move(val)) {}
    void StringLiteralNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string StringLiteralNode::toString(int indent) const {
        std::stringstream ss;
        // Correctly escape special characters in the string if necessary, for now, simple representation
        ss << "StringLiteralNode(\"" << value << "\")";
        return ss.str();
    }

    CharLiteralNode::CharLiteralNode(SourceLocation loc, char val, Node* p) : LiteralNode(NodeType::CHAR_LITERAL, std::move(loc), p), value(val) {}
    void CharLiteralNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string CharLiteralNode::toString(int indent) const {
        std::stringstream ss;
        ss << "CharLiteralNode(\'" << value << "\')";
        return ss.str();
    }

    BoolLiteralNode::BoolLiteralNode(SourceLocation loc, bool val, Node* p) : LiteralNode(NodeType::BOOL_LITERAL, std::move(loc), p), value(val) {}
    void BoolLiteralNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string BoolLiteralNode::toString(int indent) const {
        std::stringstream ss;
        ss << "BoolLiteralNode(" << (value ? "true" : "false") << ")";
        return ss.str();
    }

    NullLiteralNode::NullLiteralNode(SourceLocation loc, Node* p) : LiteralNode(NodeType::NULL_LITERAL, std::move(loc), p) {}
    void NullLiteralNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string NullLiteralNode::toString(int indent) const {
        return "NullLiteralNode(null)";
    }

    IdentifierNode::IdentifierNode(SourceLocation loc, std::string n, Node* p) : ExprNode(NodeType::IDENTIFIER, std::move(loc), p), name(std::move(n)) {}
    void IdentifierNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string IdentifierNode::toString(int indent) const {
        std::stringstream ss;
        ss << "IdentifierNode(" << name << ")";
        return ss.str();
    }

    PathNode::PathNode(SourceLocation loc, std::vector<std::unique_ptr<IdentifierNode>> segs, Node* p) : Node(NodeType::PATH, std::move(loc), p), segments(std::move(segs)) {}
    void PathNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string PathNode::toString(int indent) const {
        std::stringstream ss;
        ss << "PathNode(";
        for (size_t i = 0; i < segments.size(); ++i) {
            if (segments[i]) {
                ss << segments[i]->toString(indent);
            } else {
                ss << "<null_segment>";
            }
            if (i < segments.size() - 1) {
                ss << "::";
            }
        }
        ss << ")";
        return ss.str();
    }
    // getPathString implementation would be here if needed, matching ast.hpp

    TupleLiteralNode::TupleLiteralNode(SourceLocation loc, std::vector<std::unique_ptr<ExprNode>> elems, Node* p) : ExprNode(NodeType::TUPLE_LITERAL, std::move(loc), p), elements(std::move(elems)) {}
    void TupleLiteralNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string TupleLiteralNode::toString(int indent) const {
        std::stringstream ss;
        ss << "TupleLiteralNode(";
        for (size_t i = 0; i < elements.size(); ++i) {
            if (elements[i]) {
                ss << elements[i]->toString(indent);
            } else {
                ss << "<null_element>";
            }
            if (i < elements.size() - 1) {
                ss << ", ";
            }
        }
        ss << ")";
        return ss.str();
    }

    // ArrayLiteralNode - Assuming definition in ast.hpp like:
    // class ArrayLiteralNode : public ExprNode {
    // public:
    //     std::vector<std::unique_ptr<ExprNode>> elements;
    //     ArrayLiteralNode(SourceLocation loc, std::vector<std::unique_ptr<ExprNode>> elems, Node* p = nullptr);
    //     void accept(Visitor& visitor) override;
    //     std::string toString(int indent = 0) const override;
    // };
    // And NodeType::ARRAY_LITERAL exists
    ArrayLiteralNode::ArrayLiteralNode(SourceLocation loc, std::vector<std::unique_ptr<ExprNode>> elems, Node* p) : ExprNode(NodeType::ARRAY_LITERAL, std::move(loc), p), elements(std::move(elems)) {}
    void ArrayLiteralNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string ArrayLiteralNode::toString(int indent) const {
        std::stringstream ss;
        ss << "ArrayLiteralNode([";
        for (size_t i = 0; i < elements.size(); ++i) {
            if (elements[i]) ss << elements[i]->toString(indent); else ss << "<null>";
            if (i < elements.size() - 1) {
                ss << ", ";
            }
        }
        ss << "])";
        return ss.str();
    }
    
    // StructLiteralField is a struct in ast.hpp, not a Node. Its constructor and toString are likely inline or part of StructLiteralNode.
    // The StructLiteralFieldNode from ast.cpp seems to correspond to ast.hpp's StructLiteralField helper struct.
    // We'll assume StructLiteralFieldNode is not a standalone AST node to be visited.
    // If StructLiteralFieldNode was intended to be a Node:
    // StructLiteralFieldNode::StructLiteralFieldNode(SourceLocation loc, std::unique_ptr<IdentifierNode> n, std::unique_ptr<ExprNode> val, Node* p) : Node(NodeType::STRUCT_LITERAL_FIELD, std::move(loc), p), name(std::move(n)), value(std::move(val)) {}
    // void StructLiteralFieldNode::accept(Visitor& visitor) { visitor.visit(this); }
    // std::string StructLiteralFieldNode::toString(int indent) const { /* ... */ }

    // StructLiteralNode uses StructLiteralField helper struct from ast.hpp
    StructLiteralNode::StructLiteralNode(SourceLocation loc, std::unique_ptr<TypeNode> struct_type, std::vector<StructLiteralField> flds, Node* p) : ExprNode(NodeType::STRUCT_LITERAL, std::move(loc), p), type(std::move(struct_type)), fields(std::move(flds)) {}
    void StructLiteralNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string StructLiteralNode::toString(int indent) const {
        std::stringstream ss;
        ss << "StructLiteralNode(";
        if (type) ss << type->toString(indent); else ss << "<null_type>";
        ss << " {";
        for (size_t i = 0; i < fields.size(); ++i) {
            // Assuming StructLiteralField has a toString or is handled here
            if(fields[i].name) ss << fields[i].name->toString(indent); else ss << "<null_name>";
            ss << ": ";
            if(fields[i].value) ss << fields[i].value->toString(indent); else ss << "<null_value>";
            if (i < fields.size() - 1) {
                ss << ", ";
            }
        }
        ss << "})";
        return ss.str();
    }

    UnaryOpNode::UnaryOpNode(SourceLocation loc, Vyn::TokenType o, std::unique_ptr<ExprNode> oper, Node* p)
        : ExprNode(NodeType::UNARY_OP, std::move(loc), p), op(o), operand(std::move(oper)) {}

    void UnaryOpNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string UnaryOpNode::toString(int indent) const {
        std::stringstream ss;
        ss << "UnaryOpNode(" << token_type_to_string(op) << ", ";
        if (operand) ss << operand->toString(indent); else ss << "<null_operand>";
        ss << ")";
        return ss.str();
    }

    BinaryOpNode::BinaryOpNode(SourceLocation loc, Vyn::TokenType o, std::unique_ptr<ExprNode> l, std::unique_ptr<ExprNode> r, Node* p)
        : ExprNode(NodeType::BINARY_OP, std::move(loc), p), op(o), left(std::move(l)), right(std::move(r)) {}

    void BinaryOpNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string BinaryOpNode::toString(int indent) const {
        std::stringstream ss;
        ss << "BinaryOpNode(" << token_type_to_string(op) << ", ";
        if (left) ss << left->toString(indent); else ss << "<null_left>";
        ss << ", ";
        if (right) ss << right->toString(indent); else ss << "<null_right>";
        ss << ")";
        return ss.str();
    }

    // CallNode in ast.cpp should be CallExprNode from ast.hpp
    CallExprNode::CallExprNode(SourceLocation loc, std::unique_ptr<ExprNode> cal, std::vector<std::unique_ptr<ExprNode>> args, Node* p) : ExprNode(NodeType::CALL_EXPR, std::move(loc), p), callee(std::move(cal)), arguments(std::move(args)) {}
    void CallExprNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string CallExprNode::toString(int indent) const {
        std::stringstream ss;
        ss << "CallExprNode(" ;
        if (callee) ss << callee->toString(indent); else ss << "<null_callee>";
        ss << "(";
        for (size_t i = 0; i < arguments.size(); ++i) {
            if (arguments[i]) ss << arguments[i]->toString(indent); else ss << "<null_arg>";
            if (i < arguments.size() - 1) {
                ss << ", ";
            }
        }
        ss << "))";
        return ss.str();
    }

    // IndexNode in ast.cpp should be ArrayAccessNode from ast.hpp
    // Assuming NodeType::ARRAY_ACCESS exists
    ArrayAccessNode::ArrayAccessNode(SourceLocation loc, std::unique_ptr<ExprNode> arr, std::unique_ptr<ExprNode> idx, Node* p) : ExprNode(NodeType::ARRAY_ACCESS, std::move(loc), p), array(std::move(arr)), index(std::move(idx)) {}
    void ArrayAccessNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string ArrayAccessNode::toString(int indent) const {
        std::stringstream ss;
        ss << "ArrayAccessNode(";
        if (array) ss << array->toString(indent); else ss << "<null_array>";
        ss << "[";
        if (index) ss << index->toString(indent); else ss << "<null_index>";
        ss << "])";
        return ss.str();
    }

    MemberAccessNode::MemberAccessNode(SourceLocation loc, std::unique_ptr<ExprNode> obj, std::unique_ptr<IdentifierNode> mem, Node* p) : ExprNode(NodeType::MEMBER_ACCESS, std::move(loc), p), object(std::move(obj)), member(std::move(mem)) {}
    void MemberAccessNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string MemberAccessNode::toString(int indent) const {
        std::stringstream ss;
        ss << "MemberAccessNode(";
        if (object) ss << object->toString(indent); else ss << "<null_object>";
        ss << ".";
        if (member) ss << member->toString(indent); else ss << "<null_member>";
        ss << ")";
        return ss.str();
    }

    // IfNode in ast.cpp should be IfStmtNode from ast.hpp (or IfExprNode if it's an expression)
    // Assuming IfStmtNode and NodeType::IF_STMT
    IfStmtNode::IfStmtNode(SourceLocation loc, std::unique_ptr<ExprNode> cond, std::unique_ptr<BlockStmtNode> thenB, std::unique_ptr<StmtNode> elseB, Node* p) : StmtNode(NodeType::IF_STMT, std::move(loc), p), condition(std::move(cond)), thenBranch(std::move(thenB)), elseBranch(std::move(elseB)) {}
    void IfStmtNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string IfStmtNode::toString(int indent) const {
        std::stringstream ss;
        ss << "IfStmtNode(if ";
        if (condition) ss << condition->toString(indent); else ss << "<null_condition>";
        ss << " then ";
        if (thenBranch) ss << thenBranch->toString(indent); else ss << "<null_thenBranch>";
        if (elseBranch) {
            ss << " else " << elseBranch->toString(indent);
        }
        ss << ")";
        return ss.str();
    }

    // BlockNode in ast.cpp should be BlockStmtNode from ast.hpp
    // Assuming NodeType::BLOCK_STMT
    BlockStmtNode::BlockStmtNode(SourceLocation loc, std::vector<std::unique_ptr<StmtNode>> stmts, Node* p) : StmtNode(NodeType::BLOCK_STMT, std::move(loc), p), statements(std::move(stmts)) {}
    void BlockStmtNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string BlockStmtNode::toString(int indent) const {
        std::stringstream ss;
        ss << "BlockStmtNode({";
        for (const auto& stmt : statements) {
            if (stmt) ss << stmt->toString(indent) << "; "; else ss << "<null_stmt>; ";
        }
        ss << "})";
        return ss.str();
    }

    // VarDeclNode in ast.cpp should be VarDeclStmtNode from ast.hpp
    // Assuming NodeType::VAR_DECL_STMT
    VarDeclStmtNode::VarDeclStmtNode(SourceLocation loc, std::unique_ptr<PatternNode> pat, std::unique_ptr<TypeNode> ta, std::unique_ptr<ExprNode> init, bool mut, Node* p) : StmtNode(NodeType::VAR_DECL_STMT, std::move(loc), p), pattern(std::move(pat)), typeAnnotation(std::move(ta)), initializer(std::move(init)), isMut(mut) {}
    void VarDeclStmtNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string VarDeclStmtNode::toString(int indent) const {
        std::stringstream ss;
        ss << "VarDeclStmtNode(" << (isMut ? "mut " : "let ");
        if (pattern) ss << pattern->toString(indent); else ss << "<null_pattern>";
        if (typeAnnotation) {
            ss << ": " << typeAnnotation->toString(indent);
        }
        if (initializer) {
            ss << " = " << initializer->toString(indent);
        }
        ss << ")";
        return ss.str();
    }

    ParamNode::ParamNode(SourceLocation loc, bool ref, bool mut, std::unique_ptr<IdentifierNode> n, std::unique_ptr<TypeNode> ta, Node* p) : DeclNode(NodeType::PARAM, std::move(loc), p), isRef(ref), isMut(mut), name(std::move(n)), typeAnnotation(std::move(ta)) {}
    void ParamNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string ParamNode::toString(int indent) const {
        std::stringstream ss;
        ss << "ParamNode(";
        if (isRef) ss << "ref ";
        if (isMut) ss << "mut ";
        if (name) ss << name->toString(indent); else ss << "<null_name>";
        if (typeAnnotation) {
            ss << ": " << typeAnnotation->toString(indent);
        }
        ss << ")";
        return ss.str();
    }

    GenericParamNode::GenericParamNode(SourceLocation loc, std::unique_ptr<IdentifierNode> param_name, std::vector<std::unique_ptr<TypeNode>> bounds, Node* p) : Node(NodeType::GENERIC_PARAM, std::move(loc), p), name(std::move(param_name)), trait_bounds(std::move(bounds)) {}
    void GenericParamNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string GenericParamNode::toString(int indent) const {
        std::stringstream ss;
        ss << "GenericParamNode(";
        if (name) ss << name->toString(indent); else ss << "<null_name>";
        if (!trait_bounds.empty()) {
            ss << ": ";
            for (size_t i = 0; i < trait_bounds.size(); ++i) {
                if (trait_bounds[i]) ss << trait_bounds[i]->toString(indent); else ss << "<null_bound>";
                if (i < trait_bounds.size() - 1) ss << " + ";
            }
        }
        ss << ")";
        return ss.str();
    }

    FuncDeclNode::FuncDeclNode(SourceLocation loc, std::unique_ptr<IdentifierNode> func_name, std::vector<std::unique_ptr<GenericParamNode>> gen_params, std::vector<std::unique_ptr<ParamNode>> func_params, std::unique_ptr<TypeNode> ret_type, std::unique_ptr<BlockStmtNode> func_body, bool async, bool ext, Node* p)
        : DeclNode(NodeType::FUNC_DECL, std::move(loc), p), name(std::move(func_name)), generic_params(std::move(gen_params)), params(std::move(func_params)), return_type(std::move(ret_type)), body(std::move(func_body)), is_async(async), is_extern(ext) {}
    void FuncDeclNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string FuncDeclNode::toString(int indent) const {
        std::stringstream ss;
        ss << "FuncDeclNode(";
        if (name) ss << name->toString(indent); else ss << "<null_name>";
        if (!generic_params.empty()) {
            ss << "<";
            for (size_t i = 0; i < generic_params.size(); ++i) {
                if (generic_params[i]) ss << generic_params[i]->toString(indent); else ss << "<null_gen_param>";
                if (i < generic_params.size() - 1) ss << ", ";
            }
            ss << ">";
        }
        ss << "(";
        for (size_t i = 0; i < params.size(); ++i) {
            if (params[i]) ss << params[i]->toString(indent); else ss << "<null_param>";
            if (i < params.size() - 1) {
                ss << ", ";
            }
        }
        ss << ")";
        if (return_type) {
            ss << " -> " << return_type->toString(indent);
        }
        if (body) {
            ss << " " << body->toString(indent);
        } else {
            ss << " <extern_or_interface>";
        }
        ss << ")";
        return ss.str();
    }

    // ReturnNode in ast.cpp should be ReturnStmtNode from ast.hpp
    // Assuming NodeType::RETURN_STMT
    ReturnStmtNode::ReturnStmtNode(SourceLocation loc, std::unique_ptr<ExprNode> val, Node* p) : StmtNode(NodeType::RETURN_STMT, std::move(loc), p), value(std::move(val)) {}
    void ReturnStmtNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string ReturnStmtNode::toString(int indent) const {
        std::stringstream ss;
        ss << "ReturnStmtNode(return";
        if (value) {
            ss << " " << value->toString(indent);
        }
        ss << ")";
        return ss.str();
    }

    // StructFieldNode in ast.cpp should be FieldDeclNode from ast.hpp
    // Assuming NodeType::FIELD_DECL
    FieldDeclNode::FieldDeclNode(SourceLocation loc, std::unique_ptr<IdentifierNode> field_name, std::unique_ptr<TypeNode> type_ann, std::unique_ptr<ExprNode> init, bool is_mut, Node* p) : DeclNode(NodeType::FIELD_DECL, std::move(loc), p), name(std::move(field_name)), type_annotation(std::move(type_ann)), initializer(std::move(init)), is_mutable(is_mut) {}
    void FieldDeclNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string FieldDeclNode::toString(int indent) const {
        std::stringstream ss;
        ss << "FieldDeclNode(";
        if (name) ss << name->toString(indent); else ss << "<null_name>";
        ss << ": ";
        if (type_annotation) ss << type_annotation->toString(indent); else ss << "<null_type>";
        if (initializer) ss << " = " << initializer->toString(indent);
        ss << (is_mutable ? " (mutable)" : "") << ")";
        return ss.str();
    }

    StructDeclNode::StructDeclNode(SourceLocation loc, std::unique_ptr<IdentifierNode> struct_name, std::vector<std::unique_ptr<GenericParamNode>> gen_params, std::vector<std::unique_ptr<FieldDeclNode>> struct_fields, Node* p)
        : DeclNode(NodeType::STRUCT_DECL, std::move(loc), p), name(std::move(struct_name)), generic_params(std::move(gen_params)), fields(std::move(struct_fields)) {}
    void StructDeclNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string StructDeclNode::toString(int indent) const {
        std::stringstream ss;
        ss << "StructDeclNode(struct ";
        if (name) ss << name->toString(indent); else ss << "<null_name>";
        if (!generic_params.empty()) {
            ss << "<";
            for (size_t i = 0; i < generic_params.size(); ++i) {
                if (generic_params[i]) ss << generic_params[i]->toString(indent); else ss << "<null_gen_param>";
                if (i < generic_params.size() - 1) ss << ", ";
            }
            ss << ">";
        }
        ss << " {";
        for (const auto& field : fields) {
            if (field) ss << field->toString(indent) << "; "; else ss << "<null_field>; ";
        }
        ss << "})";
        return ss.str();
    }

    ImplDeclNode::ImplDeclNode(SourceLocation loc, std::unique_ptr<TypeNode> tr_type, std::unique_ptr<TypeNode> slf_type, std::vector<std::unique_ptr<GenericParamNode>> gen_params, std::vector<std::unique_ptr<FuncDeclNode>> impl_methods, Node* p)
        : DeclNode(NodeType::IMPL_DECL, std::move(loc), p), trait_type(std::move(tr_type)), self_type(std::move(slf_type)), generic_params(std::move(gen_params)), methods(std::move(impl_methods)) {}
    void ImplDeclNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string ImplDeclNode::toString(int indent) const {
        std::stringstream ss;
        ss << "ImplDeclNode(impl ";
        // Note: original ast.cpp ImplDeclNode had an 'identifier' field, ast.hpp does not. Following ast.hpp.
        if (!generic_params.empty()) {
            ss << "<";
            for (size_t i = 0; i < generic_params.size(); ++i) {
                if (generic_params[i]) ss << generic_params[i]->toString(indent); else ss << "<null_gen_param>";
                if (i < generic_params.size() - 1) ss << ", ";
            }
            ss << "> ";
        }
        if (trait_type) {
            ss << trait_type->toString(indent) << " for ";
        }
        if (self_type) ss << self_type->toString(indent); else ss << "<null_self_type>";
        ss << " {";
        for (const auto& member : methods) {
            if (member) ss << member->toString(indent) << "; "; else ss << "<null_method>; ";
        }
        ss << "})";
        return ss.str();
    }

    // TraitDeclNode - Assuming definition in ast.hpp
    // class TraitDeclNode : public DeclNode {
    // public:
    //     std::unique_ptr<IdentifierNode> name;
    //     std::vector<std::unique_ptr<GenericParamNode>> generic_params;
    //     std::vector<std::unique_ptr<Node>> members; // Associated types, methods signatures
    //     TraitDeclNode(SourceLocation loc, std::unique_ptr<IdentifierNode> n, std::vector<std::unique_ptr<GenericParamNode>> gp, std::vector<std::unique_ptr<Node>> mem, Node* p = nullptr);
    //     void accept(Visitor& visitor) override;
    //     std::string toString(int indent = 0) const override;
    // };
    // And NodeType::TRAIT_DECL exists
    TraitDeclNode::TraitDeclNode(SourceLocation loc, std::unique_ptr<IdentifierNode> n, std::vector<std::unique_ptr<GenericParamNode>> gp, std::vector<std::unique_ptr<Node>> mem, Node* p) : DeclNode(NodeType::TRAIT_DECL, std::move(loc), p), name(std::move(n)), generic_params(std::move(gp)), members(std::move(mem)) {}
    void TraitDeclNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string TraitDeclNode::toString(int indent) const {
        std::stringstream ss;
        ss << "TraitDeclNode(trait ";
        if (name) ss << name->toString(indent); else ss << "<null_name>";
     if (!generic_params.empty()) {
        ss << "<";
        for (size_t i = 0; i < generic_params.size(); ++i) {
            if (generic_params[i]) ss << generic_params[i]->toString(indent); else ss << "<null_gen_param>";
            if (i < generic_params.size() - 1) ss << ", ";
        }
        ss << ">";
    }
    ss << " {";
    // ast.cpp original had separate associatedTypes and members. ast.hpp has one 'members' vector.
    for (const auto& member : members) {
        if (member) ss << member->toString(indent) << "; "; else ss << "<null_member>; ";
    }
    ss << "})";
    return ss.str();
}

    EnumVariantNode::EnumVariantNode(SourceLocation loc, std::unique_ptr<IdentifierNode> n, std::vector<std::unique_ptr<TypeNode>> t_params, Node* p) : Node(NodeType::ENUM_VARIANT, std::move(loc), p), name(std::move(n)), types(std::move(t_params)) {}
    void EnumVariantNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string EnumVariantNode::toString(int indent) const {
        std::stringstream ss;
        ss << "EnumVariantNode(";
        if (name) ss << name->toString(indent); else ss << "<null_name>";
        if (!types.empty()) {
            ss << "(";
            for (size_t i = 0; i < types.size(); ++i) {
                if (types[i]) ss << types[i]->toString(indent); else ss << "<null_type>";
                if (i < types.size() - 1) ss << ", ";
            }
            ss << ")";
        }
        ss << ")";
        return ss.str();
    }

    EnumDeclNode::EnumDeclNode(SourceLocation loc, std::unique_ptr<IdentifierNode> n, std::vector<std::unique_ptr<GenericParamNode>> gp, std::vector<std::unique_ptr<EnumVariantNode>> v, Node* p)
        : DeclNode(NodeType::ENUM_DECL, std::move(loc), p), name(std::move(n)), generic_params(std::move(gp)), variants(std::move(v)) {}
    void EnumDeclNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string EnumDeclNode::toString(int indent) const {
        std::stringstream ss;
        ss << "EnumDeclNode(enum ";
        if (name) ss << name->toString(indent); else ss << "<null_name>";
        if (!generic_params.empty()) {
            ss << "<";
            for (size_t i = 0; i < generic_params.size(); ++i) {
                if (generic_params[i]) ss << generic_params[i]->toString(indent); else ss << "<null_gen_param>";
                if (i < generic_params.size() - 1) ss << ", ";
            }
            ss << ">";
        }
        ss << " {";
        for (size_t i = 0; i < variants.size(); ++i) {
            if (variants[i]) ss << variants[i]->toString(indent); else ss << "<null_variant>";
            if (i < variants.size() - 1) ss << ", ";
        }
        ss << "})";
        return ss.str();
    }

    // TypePathNode in ast.cpp seems to be IdentifierTypeNode in ast.hpp
    // Assuming NodeType::IDENTIFIER_TYPE
    IdentifierTypeNode::IdentifierTypeNode(SourceLocation loc, std::unique_ptr<PathNode> type_path, Node* p) : TypeNode(NodeType::IDENTIFIER_TYPE, std::move(loc), p), path(std::move(type_path)) {}
    void IdentifierTypeNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string IdentifierTypeNode::toString(int indent) const {
        std::stringstream ss;
        ss << "IdentifierTypeNode(";
        if (path) ss << path->toString(indent); else ss << "<null_path>";
        // Original ast.cpp TypePathNode had genericArgs. ast.hpp IdentifierTypeNode does not. PathNode itself might handle generics if part of its structure.
        ss << ")";
        return ss.str();
    }
    
    // TupleTypeNode - Assuming definition in ast.hpp
    // class TupleTypeNode : public TypeNode {
    // public:
    //     std::vector<std::unique_ptr<TypeNode>> elementTypes;
    //     TupleTypeNode(SourceLocation loc, std::vector<std::unique_ptr<TypeNode>> types, Node* p = nullptr);
    //     void accept(Visitor& visitor) override;
    //     std::string toString(int indent = 0) const override;
    // };
    // And NodeType::TUPLE_TYPE exists
    TupleTypeNode::TupleTypeNode(SourceLocation loc, std::vector<std::unique_ptr<TypeNode>> types, Node* p) : TypeNode(NodeType::TUPLE_TYPE, std::move(loc), p), elementTypes(std::move(types)) {}
    void TupleTypeNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string TupleTypeNode::toString(int indent) const {
        std::stringstream ss;
        ss << "TupleTypeNode((";
        for (size_t i = 0; i < elementTypes.size(); ++i) {
            if (elementTypes[i]) ss << elementTypes[i]->toString(indent); else ss << "<null_type>";
            if (i < elementTypes.size() - 1) {
                ss << ", ";
            }
        }
        ss << "))";
        return ss.str();
    }

    ArrayTypeNode::ArrayTypeNode(SourceLocation loc, std::unique_ptr<TypeNode> et, std::unique_ptr<ExprNode> sz, Node* p) : TypeNode(NodeType::ARRAY_TYPE, std::move(loc), p), elementType(std::move(et)), size(std::move(sz)) {}
    void ArrayTypeNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string ArrayTypeNode::toString(int indent) const {
        std::stringstream ss;
        ss << "ArrayTypeNode([";
        if (elementType) ss << elementType->toString(indent); else ss << "<null_elementType>";
        if (size) {
            ss << "; " << size->toString(indent);
        }
        ss << "])";
        return ss.str();
    }

    PointerTypeNode::PointerTypeNode(SourceLocation loc, std::unique_ptr<TypeNode> ptt, bool mut, Node* p) : TypeNode(NodeType::POINTER_TYPE, std::move(loc), p), pointedToType(std::move(ptt)), isMutable(mut) {}
    void PointerTypeNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string PointerTypeNode::toString(int indent) const {
        std::stringstream ss;
        ss << "PointerTypeNode(*";
        // Original ast.cpp had isConst and isMutable. ast.hpp has only isMutable.
        if (isMutable) ss << "mut ";
        if (pointedToType) ss << pointedToType->toString(indent); else ss << "<null_pointedToType>";
        ss << ")";
        return ss.str();
    }

    OptionalTypeNode::OptionalTypeNode(SourceLocation loc, std::unique_ptr<TypeNode> wt, Node* p) : TypeNode(NodeType::OPTIONAL_TYPE, std::move(loc), p), wrappedType(std::move(wt)) {}
    void OptionalTypeNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string OptionalTypeNode::toString(int indent) const {
        std::stringstream ss;
        ss << "OptionalTypeNode(";
        if (wrappedType) ss << wrappedType->toString(indent); else ss << "<null_wrappedType>";
        ss << "?)";
        return ss.str();
    }

    TypeAliasDeclNode::TypeAliasDeclNode(SourceLocation loc, std::unique_ptr<IdentifierNode> n, std::vector<std::unique_ptr<GenericParamNode>> gp, std::unique_ptr<TypeNode> at, Node* p)
        : DeclNode(NodeType::TYPE_ALIAS_DECL, std::move(loc), p), name(std::move(n)), generic_params(std::move(gp)), aliased_type(std::move(at)) {}
    void TypeAliasDeclNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string TypeAliasDeclNode::toString(int indent) const {
        std::stringstream ss;
        ss << "TypeAliasDeclNode(type ";
        if (name) ss << name->toString(indent); else ss << "<null_name>";
        if (!generic_params.empty()) {
            ss << "<";
            for (size_t i = 0; i < generic_params.size(); ++i) {
                if (generic_params[i]) ss << generic_params[i]->toString(indent); else ss << "<null_gen_param>";
                if (i < generic_params.size() - 1) ss << ", ";
            }
            ss << ">";
        }
        ss << " = ";
        if (aliased_type) ss << aliased_type->toString(indent); else ss << "<null_aliased_type>";
        ss << ")";
        return ss.str();
    }

    GlobalVarDeclNode::GlobalVarDeclNode(SourceLocation loc, std::unique_ptr<PatternNode> pat, std::unique_ptr<TypeNode> ta, std::unique_ptr<ExprNode> init, bool is_mut, bool is_cst, Node* p)
        : DeclNode(NodeType::GLOBAL_VAR_DECL, std::move(loc), p), pattern(std::move(pat)), type_annotation(std::move(ta)), initializer(std::move(init)), is_mutable(is_mut), is_const(is_cst) {}
    void GlobalVarDeclNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string GlobalVarDeclNode::toString(int indent) const {
        std::stringstream ss;
        ss << "GlobalVarDeclNode(" << (is_const ? "const " : (is_mutable ? "mut " : "let "));
        if (pattern) ss << pattern->toString(indent); else ss << "<null_pattern>";
        if (type_annotation) {
            ss << ": " << type_annotation->toString(indent);
        }
        if (initializer) {
            ss << " = " << initializer->toString(indent);
        }
        ss << ")";
        return ss.str();
    }

    ModuleNode::ModuleNode(SourceLocation loc, std::vector<std::unique_ptr<Node>> b, Node* p) : Node(NodeType::MODULE, std::move(loc), p), body(std::move(b)) {}
    void ModuleNode::accept(Visitor& visitor) {
        visitor.visit(this);
    }
    std::string ModuleNode::toString(int indent) const {
        std::stringstream ss;
        ss << "ModuleNode({";
        for (const auto& decl : body) {
            if (decl) ss << decl->toString(indent) << "; "; else ss << "<null_decl>; ";
        }
        ss << "})";
        return ss.str();
    }

    // Visitor default implementations
    // These should match the declarations in ast.hpp (e.g., visit(IntLiteralNode* node))
    // And dispatch to the visit method of their AST base class.

    void Visitor::visit(IntLiteralNode* node) { Visitor::visit(static_cast<LiteralNode*>(node)); }
    void Visitor::visit(FloatLiteralNode* node) { Visitor::visit(static_cast<LiteralNode*>(node)); }
    void Visitor::visit(StringLiteralNode* node) { Visitor::visit(static_cast<LiteralNode*>(node)); }
    void Visitor::visit(CharLiteralNode* node) { Visitor::visit(static_cast<LiteralNode*>(node)); }
    void Visitor::visit(BoolLiteralNode* node) { Visitor::visit(static_cast<LiteralNode*>(node)); }
    void Visitor::visit(NullLiteralNode* node) { Visitor::visit(static_cast<LiteralNode*>(node)); }
    void Visitor::visit(ArrayLiteralNode* node) { Visitor::visit(static_cast<ExprNode*>(node)); }
    void Visitor::visit(IdentifierNode* node) { Visitor::visit(static_cast<ExprNode*>(node)); }
    void Visitor::visit(PathNode* node) { Visitor::visit(static_cast<Node*>(node)); }
    void Visitor::visit(TupleLiteralNode* node) { Visitor::visit(static_cast<ExprNode*>(node)); }
    void Visitor::visit(StructLiteralNode* node) { Visitor::visit(static_cast<ExprNode*>(node)); }
    void Visitor::visit(UnaryOpNode* node) { Visitor::visit(static_cast<ExprNode*>(node)); }
    void Visitor::visit(BinaryOpNode* node) { Visitor::visit(static_cast<ExprNode*>(node)); }
    void Visitor::visit(CallExprNode* node) { Visitor::visit(static_cast<ExprNode*>(node)); }
    void Visitor::visit(ArrayAccessNode* node) { Visitor::visit(static_cast<ExprNode*>(node)); }
    void Visitor::visit(MemberAccessNode* node) { Visitor::visit(static_cast<ExprNode*>(node)); }
    void Visitor::visit(AssignmentNode* node) { Visitor::visit(static_cast<ExprNode*>(node)); }
    // void Visitor::visit(IfExprNode* node) { Visitor::visit(static_cast<ExprNode*>(node)); } // Not in ast.hpp

    void Visitor::visit(BlockStmtNode* node) { Visitor::visit(static_cast<StmtNode*>(node)); }
    void Visitor::visit(ExpressionStmtNode* node) { Visitor::visit(static_cast<StmtNode*>(node));}
    void Visitor::visit(VarDeclStmtNode* node) { Visitor::visit(static_cast<StmtNode*>(node)); }
    void Visitor::visit(ReturnStmtNode* node) { Visitor::visit(static_cast<StmtNode*>(node)); }
    void Visitor::visit(IfStmtNode* node) { Visitor::visit(static_cast<StmtNode*>(node)); }
    void Visitor::visit(WhileStmtNode* node) { Visitor::visit(static_cast<StmtNode*>(node)); }
    void Visitor::visit(ForStmtNode* node) { Visitor::visit(static_cast<StmtNode*>(node)); }
    void Visitor::visit(BreakStmtNode* node) { Visitor::visit(static_cast<StmtNode*>(node)); }
    void Visitor::visit(ContinueStmtNode* node) { Visitor::visit(static_cast<StmtNode*>(node)); }
    // void Visitor::visit(ImportStmtNode* node) { Visitor::visit(static_cast<StmtNode*>(node)); } // Not in ast.hpp

    void Visitor::visit(FuncDeclNode* node) { Visitor::visit(static_cast<DeclNode*>(node)); }
    void Visitor::visit(ParamNode* node) { Visitor::visit(static_cast<DeclNode*>(node)); }
    void Visitor::visit(GenericParamNode* node) { Visitor::visit(static_cast<Node*>(node)); }
    void Visitor::visit(StructDeclNode* node) { Visitor::visit(static_cast<DeclNode*>(node)); }
    void Visitor::visit(EnumDeclNode* node) { Visitor::visit(static_cast<DeclNode*>(node)); }
    void Visitor::visit(EnumVariantNode* node) { Visitor::visit(static_cast<Node*>(node)); }
    void Visitor::visit(FieldDeclNode* node) { Visitor::visit(static_cast<DeclNode*>(node)); }
    void Visitor::visit(TraitDeclNode* node) { Visitor::visit(static_cast<DeclNode*>(node)); }
    // void Visitor::visit(ClassDeclNode* node) { Visitor::visit(static_cast<DeclNode*>(node)); } // Not in ast.hpp
    // void Visitor::visit(MethodDeclNode* node) { Visitor::visit(static_cast<DeclNode*>(node)); } // Not in ast.hpp
    void Visitor::visit(ImplDeclNode* node) { Visitor::visit(static_cast<DeclNode*>(node)); }
    void Visitor::visit(TypeAliasDeclNode* node) { Visitor::visit(static_cast<DeclNode*>(node)); }
    void Visitor::visit(GlobalVarDeclNode* node) { Visitor::visit(static_cast<DeclNode*>(node)); }

    void Visitor::visit(TypeNameNode* node) { Visitor::visit(static_cast<Node*>(node)); } // TypeNameNode inherits Node
    void Visitor::visit(IdentifierTypeNode* node) { Visitor::visit(static_cast<TypeNode*>(node)); }
    void Visitor::visit(ArrayTypeNode* node) { Visitor::visit(static_cast<TypeNode*>(node)); }
    void Visitor::visit(PointerTypeNode* node) { Visitor::visit(static_cast<TypeNode*>(node)); }
    void Visitor::visit(OptionalTypeNode* node) { Visitor::visit(static_cast<TypeNode*>(node)); }
    void Visitor::visit(TupleTypeNode* node) { Visitor::visit(static_cast<TypeNode*>(node)); }

    void Visitor::visit(IdentifierPatternNode* node) { Visitor::visit(static_cast<PatternNode*>(node)); }
    void Visitor::visit(LiteralPatternNode* node) { Visitor::visit(static_cast<PatternNode*>(node)); }
    void Visitor::visit(WildcardPatternNode* node) { Visitor::visit(static_cast<PatternNode*>(node)); }
    void Visitor::visit(TuplePatternNode* node) { Visitor::visit(static_cast<PatternNode*>(node)); }
    void Visitor::visit(StructPatternNode* node) { Visitor::visit(static_cast<PatternNode*>(node)); }
    void Visitor::visit(EnumVariantPatternNode* node) { Visitor::visit(static_cast<PatternNode*>(node)); }
    
    void Visitor::visit(ModuleNode* node) { Visitor::visit(static_cast<Node*>(node)); }

    // Base class visits
    void Visitor::visit(LiteralNode* node) { Visitor::visit(static_cast<ExprNode*>(node)); }
    void Visitor::visit(PatternNode* node) { Visitor::visit(static_cast<Node*>(node)); }
    void Visitor::visit(ExprNode* node) { Visitor::visit(static_cast<Node*>(node)); }
    void Visitor::visit(StmtNode* node) { Visitor::visit(static_cast<Node*>(node)); }
    void Visitor::visit(DeclNode* node) { Visitor::visit(static_cast<Node*>(node)); }
    void Visitor::visit(TypeNode* node) { Visitor::visit(static_cast<Node*>(node)); } 

    void Visitor::visit(Node* node) {
        // Default behavior: can be empty, or log, or throw, depending on design.
    }
}
