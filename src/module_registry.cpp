// SPDX-License-Identifier: Apache-2.0

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

// Kernel mode (issue #198) flag — defined in src/main.cpp. When true, module
// resolution treats the root as a pure device module and skips the stdlib
// core::aspects auto-import (it would drag host __vyb_* symbols into NVPTX code).
extern bool g_kernel_mode;

namespace {

// Collects every identifier name reachable from an AST subtree. Used to build the
// dependency closure for subset imports (`import m::{foo}`): a requested
// declaration may transitively call/name sibling module-level declarations that
// must be spliced into the importer alongside it. The collector over-collects
// (it records bound names and locals too), which is safe: the resolver only
// promotes a collected name to a dependency if it matches a module-level
// declaration in the origin module, and an extra module-level name is at worst
// carried (never exported).
class FreeIdentifierCollector : public ast::Visitor {
public:
    std::unordered_set<std::string> names;

private:
    void exprs(const std::vector<ast::ExprPtr>& v) { for (const auto& e : v) if (e) e->accept(*this); }
    void stmts(const std::vector<ast::StmtPtr>& v) { for (const auto& s : v) if (s) s->accept(*this); }
    void expr(const ast::ExprPtr& e) { if (e) e->accept(*this); }
    void stmt(const ast::StmtPtr& s) { if (s) s->accept(*this); }
    void ty(const ast::TypeNodePtr& t) { if (t) t->accept(*this); }
    void tys(const std::vector<ast::TypeNodePtr>& v) { for (const auto& t : v) if (t) t->accept(*this); }

public:
    // Literals
    void visit(ast::Identifier* node) override { if (node) names.insert(node->name); }
    void visit(ast::IntegerLiteral*) override {}
    void visit(ast::FloatLiteral*) override {}
    void visit(ast::StringLiteral*) override {}
    void visit(ast::BooleanLiteral*) override {}
    void visit(ast::NilLiteral*) override {}
    void visit(ast::ObjectLiteral* node) override {
        if (!node) return;
        if (node->typePath) node->typePath->accept(*this);
        for (const auto& prop : node->properties) {
            if (prop.key) prop.key->accept(*this);
            expr(prop.value);
        }
    }

    // Expressions
    void visit(ast::UnaryExpression* node) override { if (node) expr(node->operand); }
    void visit(ast::BinaryExpression* node) override {
        if (!node) return;
        expr(node->left); expr(node->right);
    }
    void visit(ast::CallExpression* node) override {
        if (!node) return;
        expr(node->callee);
        exprs(node->arguments);
        tys(node->explicitTypeArgs);
    }
    void visit(ast::MemberExpression* node) override {
        if (!node) return;
        expr(node->object); expr(node->property);
    }
    void visit(ast::AssignmentExpression* node) override {
        if (!node) return;
        expr(node->left); expr(node->right);
    }
    void visit(ast::ArrayLiteral* node) override { if (node) exprs(node->elements); }
    void visit(ast::BorrowExpression* node) override { if (node) expr(node->expression); }
    void visit(ast::PointerDerefExpression* node) override { if (node) expr(node->pointer); }
    void visit(ast::AddrOfExpression* node) override { if (node) expr(node->getLocation()); }
    void visit(ast::FromIntToLocExpression* node) override {
        if (!node) return;
        expr(node->getAddressExpression());
        ty(node->getTargetType());
    }
    void visit(ast::ArrayElementExpression* node) override {
        if (!node) return;
        expr(node->array); expr(node->index);
    }
    void visit(ast::LocationExpression* node) override { if (node) expr(node->expression); }
    void visit(ast::ListComprehension* node) override {
        if (!node) return;
        expr(node->elementExpr);
        if (node->loopVariable) node->loopVariable->accept(*this);
        expr(node->iterableExpr);
        expr(node->conditionExpr);
    }
    void visit(ast::IfExpression* node) override {
        if (!node) return;
        expr(node->condition); expr(node->thenBranch); expr(node->elseBranch);
    }
    void visit(ast::ConstructionExpression* node) override {
        if (!node) return;
        ty(node->constructedType);
        exprs(node->arguments);
    }
    void visit(ast::ArrayInitializationExpression* node) override {
        if (!node) return;
        ty(node->elementType);
        expr(node->sizeExpression);
    }
    void visit(ast::GenericInstantiationExpression* node) override {
        if (!node) return;
        expr(node->baseExpression);
        tys(node->genericArguments);
    }
    void visit(ast::LogicalExpression* node) override {
        if (!node) return;
        expr(node->left); expr(node->right);
    }
    void visit(ast::ConditionalExpression* node) override {
        if (!node) return;
        expr(node->condition); expr(node->thenExpr); expr(node->elseExpr);
    }
    void visit(ast::SequenceExpression* node) override { if (node) exprs(node->expressions); }
    void visit(ast::FunctionExpression* node) override {
        if (!node) return;
        for (const auto& param : node->params) ty(param.typeNode);
        expr(node->body);
    }
    void visit(ast::ThisExpression*) override {}
    void visit(ast::SuperExpression*) override {}
    void visit(ast::AwaitExpression* node) override { if (node) expr(node->expr); }
    void visit(ast::RangeExpression* node) override {
        if (!node) return;
        expr(node->start); expr(node->end); expr(node->step);
    }
    void visit(ast::BlockExpression* node) override {
        if (!node) return;
        if (node->block) node->block->accept(*this);
        for (const auto& tc : node->trapClauses) if (tc) tc->accept(*this);
        if (node->ensureClause) node->ensureClause->accept(*this);
    }
    void visit(ast::SelectExpression* node) override {
        if (!node) return;
        expr(node->expr);
        for (const auto& c : node->cases) { expr(c.first); expr(c.second); }
    }
    void visit(ast::ComparisonPattern* node) override { if (node) expr(node->value); }
    void visit(ast::StructPattern* node) override {
        if (!node) return;
        ty(node->typeName);
        for (const auto& b : node->bindings) if (b) b->accept(*this);
    }
    void visit(ast::SetPattern* node) override {
        if (!node) return;
        for (const auto& e : node->elements) expr(e);
    }
    void visit(ast::TypeofExpression* node) override {
        if (!node) return;
        expr(node->operand);
        ty(node->typeArg);
    }
    void visit(ast::TypenameExpression* node) override { if (node) expr(node->operand); }
    void visit(ast::AsExpression* node) override {
        if (!node) return;
        expr(node->operand);
        ty(node->targetType);
    }

    // Statements
    void visit(ast::BlockStatement* node) override { if (node) stmts(node->body); }
    void visit(ast::ExpressionStatement* node) override { if (node) expr(node->expression); }
    void visit(ast::IfStatement* node) override {
        if (!node) return;
        expr(node->test);
        stmt(node->consequent);
        stmt(node->alternate);
    }
    void visit(ast::ForStatement* node) override {
        if (!node) return;
        if (node->init) node->init->accept(*this);
        expr(node->test);
        expr(node->update);
        stmt(node->body);
    }
    void visit(ast::WhileStatement* node) override {
        if (!node) return;
        expr(node->test);
        stmt(node->body);
    }
    void visit(ast::ReturnStatement* node) override { if (node) expr(node->argument); }
    void visit(ast::PassStatement* node) override { if (node) expr(node->argument); }
    void visit(ast::BreakStatement*) override {}
    void visit(ast::ContinueStatement*) override {}
    void visit(ast::TryStatement* node) override {
        if (!node) return;
        if (node->tryBlock) node->tryBlock->accept(*this);
        if (node->catchBlock) node->catchBlock->accept(*this);
        if (node->finallyBlock) node->finallyBlock->accept(*this);
    }
    void visit(ast::UnsafeStatement* node) override { if (node && node->block) node->block->accept(*this); }
    void visit(ast::EmptyStatement*) override {}
    void visit(ast::ExternStatement* node) override {
        if (!node) return;
        ty(node->returnType);
        for (const auto& param : node->parameters) ty(param.typeNode);
    }
    void visit(ast::ThrowStatement* node) override { if (node) expr(node->expr); }
    void visit(ast::MatchStatement* node) override {
        if (!node) return;
        expr(node->expr);
        for (const auto& c : node->cases) { expr(c.first); expr(c.second); }
        for (const auto& g : node->guards) expr(g);
    }
    void visit(ast::MatchExpression* node) override { if (node && node->match) node->match->accept(*this); }
    void visit(ast::YieldStatement* node) override { if (node) expr(node->expression); }
    void visit(ast::YieldReturnStatement* node) override { if (node) expr(node->expression); }
    void visit(ast::AssertStatement* node) override {
        if (!node) return;
        expr(node->condition);
        expr(node->message);
    }
    void visit(ast::FailStatement* node) override {
        if (!node) return;
        expr(node->error);
        ty(node->errorType);
    }
    void visit(ast::TrapClause* node) override {
        if (!node) return;
        ty(node->errorType);
        tys(node->errorTypes);
        stmt(node->handler);
    }
    void visit(ast::EnsureClause* node) override { if (node) stmt(node->cleanupBlock); }
    void visit(ast::RefailStatement* node) override { if (node) expr(node->wrappedError); }
    void visit(ast::PanicStatement* node) override { if (node) expr(node->message); }
    void visit(ast::ExitStatement* node) override { if (node) expr(node->code); }
    void visit(ast::DeferStatement* node) override { if (node) stmt(node->statement); }
    void visit(ast::TupleDestructureAssignment* node) override {
        if (!node) return;
        for (const auto& id : node->identifiers) if (id) id->accept(*this);
        expr(node->expression);
    }

    // Declarations
    void visit(ast::VariableDeclaration* node) override {
        if (!node) return;
        ty(node->typeNode);
        if (node->init) node->init->accept(*this);
    }
    void visit(ast::FunctionDeclaration* node) override {
        if (!node) return;
        for (const auto& param : node->params) ty(param.typeNode);
        for (const auto& gp : node->genericParams) if (gp) gp->accept(*this);
        ty(node->returnTypeNode);
        if (node->body) node->body->accept(*this);
    }
    void visit(ast::TypeAliasDeclaration* node) override { if (node) ty(node->typeNode); }
    void visit(ast::ImportDeclaration*) override {}
    void visit(ast::StructDeclaration* node) override {
        if (!node) return;
        for (const auto& gp : node->genericParams) if (gp) gp->accept(*this);
        for (const auto& f : node->fields) if (f) f->accept(*this);
        for (const auto& ctor : node->constructors) if (ctor) ctor->accept(*this);
    }
    void visit(ast::ClassDeclaration* node) override {
        if (!node) return;
        for (const auto& gp : node->genericParams) if (gp) gp->accept(*this);
        for (const auto& m : node->members) if (m) m->accept(*this);
    }
    void visit(ast::FieldDeclaration* node) override {
        if (!node) return;
        ty(node->typeNode);
        expr(node->initializer);
    }
    void visit(ast::BindDeclaration* node) override {
        if (!node) return;
        for (const auto& gp : node->genericParams) if (gp) gp->accept(*this);
        ty(node->traitType);
        ty(node->selfType);
        for (const auto& ab : node->associatedTypeBindings) ty(ab.valueType);
        for (const auto& m : node->methods) if (m) m->accept(*this);
    }
    void visit(ast::EnumDeclaration* node) override {
        if (!node) return;
        for (const auto& gp : node->genericParams) if (gp) gp->accept(*this);
        for (const auto& v : node->variants) if (v) v->accept(*this);
    }
    void visit(ast::EnumVariant* node) override { if (node) tys(node->associatedTypes); }
    void visit(ast::GenericParameter* node) override { if (node) tys(node->bounds); }
    void visit(ast::TemplateDeclaration* node) override {
        if (!node) return;
        for (const auto& gp : node->genericParams) if (gp) gp->accept(*this);
        if (node->body) node->body->accept(*this);
    }
    void visit(ast::AspectDeclaration* node) override {
        if (!node) return;
        for (const auto& gp : node->genericParams) if (gp) gp->accept(*this);
        for (const auto& st : node->superTypes) if (st) st->accept(*this);
        for (const auto& at : node->associatedTypes) if (at) at->accept(*this);
        tys(node->associatedTypeDefaults);
        for (const auto& constraints : node->associatedTypeConstraints) tys(constraints);
        for (const auto& m : node->methods) if (m) m->accept(*this);
    }
    void visit(ast::NamespaceDeclaration* node) override {
        if (!node) return;
        for (const auto& m : node->members) if (m) m->accept(*this);
    }
    void visit(ast::Module* node) override { if (node) stmts(node->body); }

    // Types
    void visit(ast::TypeNode*) override {}
    void visit(ast::TypeName* node) override {
        if (!node) return;
        if (node->identifier) node->identifier->accept(*this);
        tys(node->genericArgs);
    }
    void visit(ast::PointerType* node) override { if (node) ty(node->pointeeType); }
    void visit(ast::ArrayType* node) override {
        if (!node) return;
        ty(node->elementType);
        expr(node->sizeExpression);
    }
    void visit(ast::VecType* node) override { if (node) ty(node->elementType); }
    void visit(ast::FutureType* node) override { if (node) ty(node->resultType); }
    void visit(ast::FunctionType* node) override {
        if (!node) return;
        tys(node->parameterTypes);
        ty(node->returnType);
    }
    void visit(ast::OptionalType* node) override { if (node) ty(node->containedType); }
    void visit(ast::TupleTypeNode* node) override { if (node) tys(node->memberTypes); }
};

// --- AST deep-clone support ---------------------------------------------------
// Deep-copies a module's AST statements so a dependency body stays pristine and
// can be spliced into multiple consumers independently. The module resolver used
// to `std::move` a dependency's statements into the first importer (single-
// consumer): a second subset import of the same module, or a shared dependency
// imported by several modules, found an already-emptied origin body. This visitor
// reconstructs an equivalent subtree, leaving the source untouched.
class AstCloner : public ast::Visitor {
public:
    using BlockStatementPtr = std::unique_ptr<ast::BlockStatement>;
    using GenericParameterPtr = std::unique_ptr<ast::GenericParameter>;
    using FunctionDeclarationPtr = std::unique_ptr<ast::FunctionDeclaration>;
    using FieldDeclarationPtr = std::unique_ptr<ast::FieldDeclaration>;
    using EnumVariantPtr = std::unique_ptr<ast::EnumVariant>;
    using TrapClausePtr = std::unique_ptr<ast::TrapClause>;
    using EnsureClausePtr = std::unique_ptr<ast::EnsureClause>;
    using MatchStatementPtr = std::unique_ptr<ast::MatchStatement>;

    std::unique_ptr<ast::Node> result_;

    template <typename T>
    std::unique_ptr<T> take() {
        return std::unique_ptr<T>(static_cast<T*>(result_.release()));
    }

    // Entry point: deep-copy a top-level statement (a dependency declaration).
    ast::StmtPtr cloneStatement(ast::Statement* s) { return cloneStmt(s); }

private:
    ast::IdentifierPtr cloneId(ast::Identifier* id) {
        return id ? (id->accept(*this), take<ast::Identifier>()) : nullptr;
    }
    ast::TypeNodePtr cloneTy(ast::TypeNode* t) {
        if (!t) return nullptr;
        t->accept(*this);
        return take<ast::TypeNode>();
    }
    ast::ExprPtr cloneExpr(ast::Expression* e) {
        if (!e) return nullptr;
        e->accept(*this);
        return take<ast::Expression>();
    }
    ast::StmtPtr cloneStmt(ast::Statement* s) {
        if (!s) return nullptr;
        s->accept(*this);
        return take<ast::Statement>();
    }
    ast::DeclPtr cloneDecl(ast::Declaration* d) {
        if (!d) return nullptr;
        d->accept(*this);
        return take<ast::Declaration>();
    }
    BlockStatementPtr cloneBlock(ast::BlockStatement* b) {
        if (!b) return nullptr;
        b->accept(*this);
        return take<ast::BlockStatement>();
    }
    ast::NodePtr cloneNode(ast::Node* n) {
        if (!n) return nullptr;
        n->accept(*this);
        return take<ast::Node>();
    }
    GenericParameterPtr cloneGenericParam(ast::GenericParameter* g) {
        if (!g) return nullptr;
        g->accept(*this);
        return take<ast::GenericParameter>();
    }
    FunctionDeclarationPtr cloneFuncDecl(ast::FunctionDeclaration* f) {
        if (!f) return nullptr;
        f->accept(*this);
        return take<ast::FunctionDeclaration>();
    }
    FieldDeclarationPtr cloneField(ast::FieldDeclaration* f) {
        if (!f) return nullptr;
        f->accept(*this);
        return take<ast::FieldDeclaration>();
    }
    EnumVariantPtr cloneEnumVariant(ast::EnumVariant* v) {
        if (!v) return nullptr;
        v->accept(*this);
        return take<ast::EnumVariant>();
    }
    TrapClausePtr cloneTrap(ast::TrapClause* t) {
        if (!t) return nullptr;
        t->accept(*this);
        return take<ast::TrapClause>();
    }
    EnsureClausePtr cloneEnsure(ast::EnsureClause* e) {
        if (!e) return nullptr;
        e->accept(*this);
        return take<ast::EnsureClause>();
    }
    MatchStatementPtr cloneMatchStmt(ast::MatchStatement* m) {
        if (!m) return nullptr;
        m->accept(*this);
        return take<ast::MatchStatement>();
    }

    std::vector<ast::ExprPtr> cloneExprs(std::vector<ast::ExprPtr>& v) {
        std::vector<ast::ExprPtr> out;
        out.reserve(v.size());
        for (auto& e : v) out.push_back(cloneExpr(e.get()));
        return out;
    }
    std::vector<ast::StmtPtr> cloneStmts(std::vector<ast::StmtPtr>& v) {
        std::vector<ast::StmtPtr> out;
        out.reserve(v.size());
        for (auto& s : v) out.push_back(cloneStmt(s.get()));
        return out;
    }
    std::vector<ast::TypeNodePtr> cloneTypes(std::vector<ast::TypeNodePtr>& v) {
        std::vector<ast::TypeNodePtr> out;
        out.reserve(v.size());
        for (auto& t : v) out.push_back(cloneTy(t.get()));
        return out;
    }
    std::vector<ast::IdentifierPtr> cloneIds(std::vector<ast::IdentifierPtr>& v) {
        std::vector<ast::IdentifierPtr> out;
        out.reserve(v.size());
        for (auto& id : v) out.push_back(cloneId(id.get()));
        return out;
    }
    std::vector<GenericParameterPtr> cloneGenericParams(std::vector<GenericParameterPtr>& v) {
        std::vector<GenericParameterPtr> out;
        out.reserve(v.size());
        for (auto& g : v) out.push_back(cloneGenericParam(g.get()));
        return out;
    }
    std::vector<FieldDeclarationPtr> cloneFields(std::vector<FieldDeclarationPtr>& v) {
        std::vector<FieldDeclarationPtr> out;
        out.reserve(v.size());
        for (auto& f : v) out.push_back(cloneField(f.get()));
        return out;
    }
    std::vector<FunctionDeclarationPtr> cloneFuncs(std::vector<FunctionDeclarationPtr>& v) {
        std::vector<FunctionDeclarationPtr> out;
        out.reserve(v.size());
        for (auto& f : v) out.push_back(cloneFuncDecl(f.get()));
        return out;
    }
    std::vector<EnumVariantPtr> cloneVariants(std::vector<EnumVariantPtr>& v) {
        std::vector<EnumVariantPtr> out;
        out.reserve(v.size());
        for (auto& var : v) out.push_back(cloneEnumVariant(var.get()));
        return out;
    }
    std::vector<TrapClausePtr> cloneTraps(std::vector<TrapClausePtr>& v) {
        std::vector<TrapClausePtr> out;
        out.reserve(v.size());
        for (auto& t : v) out.push_back(cloneTrap(t.get()));
        return out;
    }
    std::vector<ast::DeclPtr> cloneDecls(std::vector<ast::DeclPtr>& v) {
        std::vector<ast::DeclPtr> out;
        out.reserve(v.size());
        for (auto& d : v) out.push_back(cloneDecl(d.get()));
        return out;
    }
    ast::FunctionParameter cloneParam(ast::FunctionParameter& p) {
        return ast::FunctionParameter(cloneId(p.name.get()), cloneTy(p.typeNode.get()), p.isMutable);
    }
    std::vector<ast::FunctionParameter> cloneParams(std::vector<ast::FunctionParameter>& v) {
        std::vector<ast::FunctionParameter> out;
        out.reserve(v.size());
        for (auto& p : v) out.push_back(cloneParam(p));
        return out;
    }
    std::vector<std::pair<ast::ExprPtr, ast::ExprPtr>> cloneCasePairs(std::vector<std::pair<ast::ExprPtr, ast::ExprPtr>>& v) {
        std::vector<std::pair<ast::ExprPtr, ast::ExprPtr>> out;
        out.reserve(v.size());
        for (auto& c : v) out.push_back({cloneExpr(c.first.get()), cloneExpr(c.second.get())});
        return out;
    }

public:
    // Literals
    void visit(ast::Identifier* node) override {
        result_ = std::make_unique<ast::Identifier>(node->loc, node->name);
    }
    void visit(ast::IntegerLiteral* node) override {
        result_ = std::make_unique<ast::IntegerLiteral>(node->loc, node->value, node->isUnsigned, node->uvalue);
    }
    void visit(ast::FloatLiteral* node) override {
        result_ = std::make_unique<ast::FloatLiteral>(node->loc, node->value);
    }
    void visit(ast::StringLiteral* node) override {
        result_ = std::make_unique<ast::StringLiteral>(node->loc, node->value);
    }
    void visit(ast::BooleanLiteral* node) override {
        result_ = std::make_unique<ast::BooleanLiteral>(node->loc, node->value);
    }
    void visit(ast::NilLiteral* node) override {
        result_ = std::make_unique<ast::NilLiteral>(node->loc);
    }
    void visit(ast::ArrayLiteral* node) override {
        result_ = std::make_unique<ast::ArrayLiteral>(node->loc, cloneExprs(node->elements));
    }
    void visit(ast::ObjectLiteral* node) override {
        std::vector<ast::ObjectProperty> props;
        props.reserve(node->properties.size());
        for (auto& prop : node->properties) {
            props.push_back(ast::ObjectProperty(prop.loc, cloneId(prop.key.get()), cloneExpr(prop.value.get())));
        }
        result_ = std::make_unique<ast::ObjectLiteral>(node->loc, cloneTy(node->typePath.get()), std::move(props));
    }

    // Expressions
    void visit(ast::UnaryExpression* node) override {
        result_ = std::make_unique<ast::UnaryExpression>(node->loc, node->op, cloneExpr(node->operand.get()));
    }
    void visit(ast::BinaryExpression* node) override {
        result_ = std::make_unique<ast::BinaryExpression>(node->loc, cloneExpr(node->left.get()), node->op, cloneExpr(node->right.get()));
    }
    void visit(ast::CallExpression* node) override {
        auto call = std::make_unique<ast::CallExpression>(node->loc, cloneExpr(node->callee.get()), cloneExprs(node->arguments));
        for (auto& a : node->explicitTypeArgs) call->explicitTypeArgs.push_back(cloneTy(a.get()));
        result_ = std::move(call);
    }
    void visit(ast::MemberExpression* node) override {
        result_ = std::make_unique<ast::MemberExpression>(node->loc, cloneExpr(node->object.get()), cloneExpr(node->property.get()), node->computed);
    }
    void visit(ast::AssignmentExpression* node) override {
        result_ = std::make_unique<ast::AssignmentExpression>(node->loc, cloneExpr(node->left.get()), node->op, cloneExpr(node->right.get()));
    }
    void visit(ast::BorrowExpression* node) override {
        result_ = std::make_unique<ast::BorrowExpression>(node->loc, cloneExpr(node->expression.get()), node->kind);
    }
    void visit(ast::PointerDerefExpression* node) override {
        result_ = std::make_unique<ast::PointerDerefExpression>(node->loc, cloneExpr(node->pointer.get()));
    }
    void visit(ast::AddrOfExpression* node) override {
        result_ = std::make_unique<ast::AddrOfExpression>(node->loc, cloneExpr(const_cast<ast::ExprPtr&>(node->getLocation()).get()));
    }
    void visit(ast::FromIntToLocExpression* node) override {
        result_ = std::make_unique<ast::FromIntToLocExpression>(node->loc, cloneExpr(const_cast<ast::ExprPtr&>(node->getAddressExpression()).get()), cloneTy(const_cast<ast::TypeNodePtr&>(node->getTargetType()).get()));
    }
    void visit(ast::ArrayElementExpression* node) override {
        result_ = std::make_unique<ast::ArrayElementExpression>(node->loc, cloneExpr(node->array.get()), cloneExpr(node->index.get()));
    }
    void visit(ast::LocationExpression* node) override {
        result_ = std::make_unique<ast::LocationExpression>(node->loc, cloneExpr(node->expression.get()));
    }
    void visit(ast::ListComprehension* node) override {
        result_ = std::make_unique<ast::ListComprehension>(node->loc, cloneExpr(node->elementExpr.get()), cloneId(node->loopVariable.get()), cloneExpr(node->iterableExpr.get()), cloneExpr(node->conditionExpr.get()));
    }
    void visit(ast::IfExpression* node) override {
        result_ = std::make_unique<ast::IfExpression>(node->loc, cloneExpr(node->condition.get()), cloneExpr(node->thenBranch.get()), cloneExpr(node->elseBranch.get()));
    }
    void visit(ast::ConstructionExpression* node) override {
        result_ = std::make_unique<ast::ConstructionExpression>(node->loc, cloneTy(node->constructedType.get()), cloneExprs(node->arguments));
    }
    void visit(ast::ArrayInitializationExpression* node) override {
        result_ = std::make_unique<ast::ArrayInitializationExpression>(node->loc, cloneTy(node->elementType.get()), cloneExpr(node->sizeExpression.get()));
    }
    void visit(ast::GenericInstantiationExpression* node) override {
        result_ = std::make_unique<ast::GenericInstantiationExpression>(node->loc, cloneExpr(node->baseExpression.get()), cloneTypes(node->genericArguments), node->lt_loc, node->gt_loc);
    }
    void visit(ast::LogicalExpression* node) override {
        result_ = std::make_unique<ast::LogicalExpression>(node->loc, cloneExpr(node->left.get()), node->op, cloneExpr(node->right.get()));
    }
    void visit(ast::ConditionalExpression* node) override {
        result_ = std::make_unique<ast::ConditionalExpression>(node->loc, cloneExpr(node->condition.get()), cloneExpr(node->thenExpr.get()), cloneExpr(node->elseExpr.get()));
    }
    void visit(ast::SequenceExpression* node) override {
        result_ = std::make_unique<ast::SequenceExpression>(node->loc, cloneExprs(node->expressions));
    }
    void visit(ast::FunctionExpression* node) override {
        result_ = std::make_unique<ast::FunctionExpression>(node->loc, cloneParams(node->params), cloneExpr(node->body.get()), node->isAsync);
    }
    void visit(ast::ThisExpression* node) override {
        result_ = std::make_unique<ast::ThisExpression>(node->loc);
    }
    void visit(ast::SuperExpression* node) override {
        result_ = std::make_unique<ast::SuperExpression>(node->loc);
    }
    void visit(ast::AwaitExpression* node) override {
        result_ = std::make_unique<ast::AwaitExpression>(node->loc, cloneExpr(node->expr.get()));
    }
    void visit(ast::RangeExpression* node) override {
        result_ = std::make_unique<ast::RangeExpression>(node->loc, cloneExpr(node->start.get()), cloneExpr(node->end.get()), cloneExpr(node->step.get()));
    }
    void visit(ast::BlockExpression* node) override {
        result_ = std::make_unique<ast::BlockExpression>(node->loc, cloneBlock(node->block.get()), cloneTraps(node->trapClauses), cloneEnsure(node->ensureClause.get()));
    }
    void visit(ast::SelectExpression* node) override {
        result_ = std::make_unique<ast::SelectExpression>(node->loc, cloneExpr(node->expr.get()), cloneCasePairs(node->cases));
    }
    void visit(ast::ComparisonPattern* node) override {
        result_ = std::make_unique<ast::ComparisonPattern>(node->loc, node->op, cloneExpr(node->value.get()));
    }
    void visit(ast::StructPattern* node) override {
        result_ = std::make_unique<ast::StructPattern>(node->loc, cloneTy(node->typeName.get()), cloneIds(node->bindings));
    }
    void visit(ast::SetPattern* node) override {
        result_ = std::make_unique<ast::SetPattern>(node->loc, cloneExprs(node->elements));
    }
    void visit(ast::TypeofExpression* node) override {
        if (node->operand) {
            result_ = std::make_unique<ast::TypeofExpression>(node->loc, cloneExpr(node->operand.get()));
        } else {
            result_ = std::make_unique<ast::TypeofExpression>(node->loc, cloneTy(node->typeArg.get()));
        }
    }
    void visit(ast::TypenameExpression* node) override {
        result_ = std::make_unique<ast::TypenameExpression>(node->loc, cloneExpr(node->operand.get()));
    }
    void visit(ast::AsExpression* node) override {
        result_ = std::make_unique<ast::AsExpression>(node->loc, cloneExpr(node->operand.get()), cloneTy(node->targetType.get()));
    }
    void visit(ast::MatchExpression* node) override {
        result_ = std::make_unique<ast::MatchExpression>(node->loc, cloneMatchStmt(node->match.get()));
    }

    // Statements
    void visit(ast::BlockStatement* node) override {
        result_ = std::make_unique<ast::BlockStatement>(node->loc, cloneStmts(node->body));
    }
    void visit(ast::ExpressionStatement* node) override {
        result_ = std::make_unique<ast::ExpressionStatement>(node->loc, cloneExpr(node->expression.get()));
    }
    void visit(ast::IfStatement* node) override {
        result_ = std::make_unique<ast::IfStatement>(node->loc, cloneExpr(node->test.get()), cloneStmt(node->consequent.get()), cloneStmt(node->alternate.get()));
    }
    void visit(ast::ForStatement* node) override {
        auto f = std::make_unique<ast::ForStatement>(node->loc, cloneNode(node->init.get()), cloneExpr(node->test.get()), cloneExpr(node->update.get()), cloneStmt(node->body.get()));
        f->label = node->label;
        result_ = std::move(f);
    }
    void visit(ast::WhileStatement* node) override {
        auto w = std::make_unique<ast::WhileStatement>(node->loc, cloneExpr(node->test.get()), cloneStmt(node->body.get()));
        w->label = node->label;
        result_ = std::move(w);
    }
    void visit(ast::ReturnStatement* node) override {
        result_ = std::make_unique<ast::ReturnStatement>(node->loc, cloneExpr(node->argument.get()));
    }
    void visit(ast::PassStatement* node) override {
        result_ = std::make_unique<ast::PassStatement>(node->loc, cloneExpr(node->argument.get()));
    }
    void visit(ast::BreakStatement* node) override {
        auto b = std::make_unique<ast::BreakStatement>(node->loc);
        b->label = node->label;
        result_ = std::move(b);
    }
    void visit(ast::ContinueStatement* node) override {
        auto c = std::make_unique<ast::ContinueStatement>(node->loc);
        c->label = node->label;
        result_ = std::move(c);
    }
    void visit(ast::TryStatement* node) override {
        result_ = std::make_unique<ast::TryStatement>(node->loc, cloneBlock(node->tryBlock.get()), node->catchIdent, cloneBlock(node->catchBlock.get()), cloneBlock(node->finallyBlock.get()));
    }
    void visit(ast::UnsafeStatement* node) override {
        result_ = std::make_unique<ast::UnsafeStatement>(node->loc, cloneBlock(node->block.get()));
    }
    void visit(ast::EmptyStatement* node) override {
        result_ = std::make_unique<ast::EmptyStatement>(node->loc);
    }
    void visit(ast::ExternStatement* node) override {
        result_ = std::make_unique<ast::ExternStatement>(node->loc, cloneId(node->name.get()), cloneTy(node->returnType.get()), cloneParams(node->parameters));
    }
    void visit(ast::ThrowStatement* node) override {
        result_ = std::make_unique<ast::ThrowStatement>(node->loc, cloneExpr(node->expr.get()));
    }
    void visit(ast::MatchStatement* node) override {
        result_ = std::make_unique<ast::MatchStatement>(node->loc, cloneExpr(node->expr.get()), cloneCasePairs(node->cases), cloneExprs(node->guards));
    }
    void visit(ast::YieldStatement* node) override {
        result_ = std::make_unique<ast::YieldStatement>(node->loc, cloneExpr(node->expression.get()));
    }
    void visit(ast::YieldReturnStatement* node) override {
        result_ = std::make_unique<ast::YieldReturnStatement>(node->loc, cloneExpr(node->expression.get()));
    }
    void visit(ast::AssertStatement* node) override {
        result_ = std::make_unique<ast::AssertStatement>(node->loc, cloneExpr(node->condition.get()), cloneExpr(node->message.get()));
    }
    void visit(ast::FailStatement* node) override {
        result_ = std::make_unique<ast::FailStatement>(node->loc, cloneExpr(node->error.get()), cloneTy(node->errorType.get()));
    }
    void visit(ast::TrapClause* node) override {
        auto t = std::make_unique<ast::TrapClause>(node->loc, cloneId(node->errorName.get()), cloneTy(node->errorType.get()), cloneStmt(node->handler.get()), node->isWildcard, node->isMultiType);
        for (auto& et : node->errorTypes) t->errorTypes.push_back(cloneTy(et.get()));
        result_ = std::move(t);
    }
    void visit(ast::EnsureClause* node) override {
        result_ = std::make_unique<ast::EnsureClause>(node->loc, cloneStmt(node->cleanupBlock.get()));
    }
    void visit(ast::RefailStatement* node) override {
        result_ = std::make_unique<ast::RefailStatement>(node->loc, cloneExpr(node->wrappedError.get()));
    }
    void visit(ast::PanicStatement* node) override {
        result_ = std::make_unique<ast::PanicStatement>(node->loc, cloneExpr(node->message.get()));
    }
    void visit(ast::ExitStatement* node) override {
        result_ = std::make_unique<ast::ExitStatement>(node->loc, cloneExpr(node->code.get()));
    }
    void visit(ast::DeferStatement* node) override {
        result_ = std::make_unique<ast::DeferStatement>(node->loc, cloneStmt(node->statement.get()));
    }
    void visit(ast::TupleDestructureAssignment* node) override {
        result_ = std::make_unique<ast::TupleDestructureAssignment>(node->loc, cloneIds(node->identifiers), cloneExpr(node->expression.get()));
    }

    // Declarations
    void visit(ast::VariableDeclaration* node) override {
        auto v = std::make_unique<ast::VariableDeclaration>(node->loc, cloneId(node->id.get()), node->isConst, cloneTy(node->typeNode.get()));
        if (node->init) v->init = std::shared_ptr<ast::Expression>(cloneExpr(node->init.get()));
        result_ = std::move(v);
    }
    void visit(ast::FunctionDeclaration* node) override {
        result_ = std::make_unique<ast::FunctionDeclaration>(node->loc, cloneId(node->id.get()), cloneParams(node->params), cloneBlock(node->body.get()), node->isAsync, cloneTy(node->returnTypeNode.get()), node->hasDefaultImpl, cloneGenericParams(node->genericParams), node->variadic);
    }
    void visit(ast::TypeAliasDeclaration* node) override {
        result_ = std::make_unique<ast::TypeAliasDeclaration>(node->loc, cloneId(node->name.get()), cloneTy(node->typeNode.get()));
    }
    void visit(ast::ImportDeclaration* node) override {
        std::vector<ast::ImportSpecifier> specs;
        specs.reserve(node->specifiers.size());
        for (auto& s : node->specifiers) {
            specs.push_back(ast::ImportSpecifier(cloneId(s.importedName.get()), cloneId(s.localName.get())));
        }
        std::unique_ptr<ast::StringLiteral> src = (node->source
            ? (node->source->accept(*this), take<ast::StringLiteral>()) : nullptr);
        std::unique_ptr<ast::StringLiteral> locator = (node->locator
            ? (node->locator->accept(*this), take<ast::StringLiteral>()) : nullptr);
        result_ = std::make_unique<ast::ImportDeclaration>(node->loc, node->kind, std::move(src), std::move(locator), std::move(specs), cloneId(node->defaultImport.get()), cloneId(node->namespaceImport.get()));
    }
    void visit(ast::StructDeclaration* node) override {
        auto sd = std::make_unique<ast::StructDeclaration>(node->loc, cloneId(node->name.get()), cloneGenericParams(node->genericParams), cloneFields(node->fields), node->reprC);
        sd->constructors = cloneFuncs(node->constructors);
        result_ = std::move(sd);
    }
    void visit(ast::ClassDeclaration* node) override {
        result_ = std::make_unique<ast::ClassDeclaration>(node->loc, cloneId(node->name.get()), cloneGenericParams(node->genericParams), cloneDecls(node->members));
    }
    void visit(ast::FieldDeclaration* node) override {
        result_ = std::make_unique<ast::FieldDeclaration>(node->loc, cloneId(node->name.get()), cloneTy(node->typeNode.get()), cloneExpr(node->initializer.get()), node->isMutable);
    }
    void visit(ast::BindDeclaration* node) override {
        std::vector<ast::BindDeclaration::AssociatedTypeBinding> binds;
        binds.reserve(node->associatedTypeBindings.size());
        for (auto& b : node->associatedTypeBindings) {
            binds.push_back({cloneId(b.name.get()), cloneTy(b.valueType.get())});
        }
        result_ = std::make_unique<ast::BindDeclaration>(node->loc, cloneTy(node->selfType.get()), std::move(binds), cloneFuncs(node->methods), cloneId(node->name.get()), cloneGenericParams(node->genericParams), cloneTy(node->traitType.get()));
    }
    void visit(ast::EnumDeclaration* node) override {
        result_ = std::make_unique<ast::EnumDeclaration>(node->loc, cloneId(node->name.get()), cloneGenericParams(node->genericParams), cloneVariants(node->variants));
    }
    void visit(ast::EnumVariant* node) override {
        auto v = std::make_unique<ast::EnumVariant>(node->loc, cloneId(node->name.get()), cloneTypes(node->associatedTypes));
        v->value = node->value;
        v->hasValue = node->hasValue;
        result_ = std::move(v);
    }
    void visit(ast::GenericParameter* node) override {
        result_ = std::make_unique<ast::GenericParameter>(node->loc, cloneId(node->name.get()), cloneTypes(node->bounds));
    }
    void visit(ast::TemplateDeclaration* node) override {
        result_ = std::make_unique<ast::TemplateDeclaration>(node->loc, cloneId(node->name.get()), cloneGenericParams(node->genericParams), cloneDecl(node->body.get()));
    }
    void visit(ast::AspectDeclaration* node) override {
        std::vector<std::vector<ast::TypeNodePtr>> constraints;
        constraints.reserve(node->associatedTypeConstraints.size());
        for (auto& c : node->associatedTypeConstraints) constraints.push_back(cloneTypes(c));
        result_ = std::make_unique<ast::AspectDeclaration>(node->loc, cloneId(node->name.get()), cloneGenericParams(node->genericParams), cloneIds(node->superTypes), cloneIds(node->associatedTypes), cloneTypes(node->associatedTypeDefaults), std::move(constraints), cloneFuncs(node->methods));
    }
    void visit(ast::NamespaceDeclaration* node) override {
        result_ = std::make_unique<ast::NamespaceDeclaration>(node->loc, cloneId(node->name.get()), cloneDecls(node->members));
    }

    // Other
    void visit(ast::TypeNode*) override { result_.reset(); }
    void visit(ast::Module* node) override {
        result_ = std::make_unique<ast::Module>(node->loc, cloneStmts(node->body));
    }
    void visit(ast::TypeName* node) override {
        result_ = std::make_unique<ast::TypeName>(node->loc, cloneId(node->identifier.get()), cloneTypes(node->genericArgs));
    }
    void visit(ast::PointerType* node) override {
        result_ = std::make_unique<ast::PointerType>(node->loc, cloneTy(node->pointeeType.get()));
    }
    void visit(ast::ArrayType* node) override {
        result_ = std::make_unique<ast::ArrayType>(node->loc, cloneTy(node->elementType.get()), cloneExpr(node->sizeExpression.get()));
    }
    void visit(ast::VecType* node) override {
        result_ = std::make_unique<ast::VecType>(node->loc, cloneTy(node->elementType.get()));
    }
    void visit(ast::FutureType* node) override {
        result_ = std::make_unique<ast::FutureType>(node->loc, cloneTy(node->resultType.get()));
    }
    void visit(ast::FunctionType* node) override {
        result_ = std::make_unique<ast::FunctionType>(node->loc, cloneTypes(node->parameterTypes), cloneTy(node->returnType.get()));
    }
    void visit(ast::OptionalType* node) override {
        result_ = std::make_unique<ast::OptionalType>(node->loc, cloneTy(node->containedType.get()));
    }
    void visit(ast::TupleTypeNode* node) override {
        result_ = std::make_unique<ast::TupleTypeNode>(node->loc, cloneTypes(node->memberTypes));
    }
};

// Deep-copy a single origin statement so the module's own body is left intact
// and can be re-spliced into later consumers unchanged.
static ast::StmtPtr cloneSplicedStmt(ast::StmtPtr& src) {
    AstCloner cloner;
    return cloner.cloneStatement(src.get());
}



// A module's namespace bindings: namespace identifier (`GV`) -> the set of
// origin symbol names it exposes. `import module as GV` (or `import * as GV`)
// binds the whole module's visible exports under `GV`; qualified access
// `GV.sym` is rewritten to the plain symbol `sym` during module resolution so
// the rest of the compiler (semantic analysis and codegen) needs no awareness
// of namespaces. Plainly imported symbols are untouched.
using ModuleNSMap = std::unordered_map<std::string, std::unordered_map<std::string, std::string>>;

static std::string mangledNamespaceName(const std::string& ns, const std::string& symbol) {
    return "__ns_" + ns + "_" + symbol;
}

static void rewriteNamespaceRawStmt(ast::Statement* stmt, const ModuleNSMap& localNS);
static void rewriteNamespaceStmt(ast::StmtPtr& stmt, const ModuleNSMap& localNS) {
    rewriteNamespaceRawStmt(stmt.get(), localNS);
}
static void rewriteNamespaceExpr(ast::ExprPtr& expr, const ModuleNSMap& localNS);

// Replace `NS.sym` (a non-computed member expression whose object is a
// namespace bound locally and whose property is one of that namespace's
// symbols) with a bare identifier for `sym`. Returns true if replaced.
static bool foldNamespaceMember(ast::ExprPtr& e, const ModuleNSMap& localNS) {
    auto* member = dynamic_cast<ast::MemberExpression*>(e.get());
    if (!member || member->computed) {
        return false;
    }
    auto* objId = dynamic_cast<ast::Identifier*>(member->object.get());
    auto* propId = dynamic_cast<ast::Identifier*>(member->property.get());
    if (!objId || !propId) {
        return false;
    }
    auto nsIt = localNS.find(objId->name);
    if (nsIt == localNS.end()) {
        return false;
    }
    auto symIt = nsIt->second.find(propId->name);
    if (symIt == nsIt->second.end()) {
        return false;
    }
    ast::ExprPtr replacement = std::make_unique<ast::Identifier>(member->loc, symIt->second);
    e = std::move(replacement);
    return true;
}

static void rewriteBlockBody(std::vector<ast::StmtPtr>& body, const ModuleNSMap& localNS) {
    for (auto& st : body) {
        if (st) rewriteNamespaceStmt(st, localNS);
    }
}

// Root-only fold for a shared_ptr<Expression> initializer (VariableDeclaration).
static void foldVariableInit(std::shared_ptr<ast::Expression>& init, const ModuleNSMap& localNS) {
    if (!init) return;
    if (auto* mem = dynamic_cast<ast::MemberExpression*>(init.get())) {
        if (mem->computed) return;
        auto* objId = dynamic_cast<ast::Identifier*>(mem->object.get());
        auto* propId = dynamic_cast<ast::Identifier*>(mem->property.get());
        if (!objId || !propId) return;
        auto nsIt = localNS.find(objId->name);
        if (nsIt != localNS.end()) {
            auto symIt = nsIt->second.find(propId->name);
            if (symIt != nsIt->second.end()) {
                init = std::make_shared<ast::Identifier>(mem->loc, symIt->second);
            }
        }
    }
}

// --- Statement traversal (raw pointer; recurses into owned children by ref) ---
static void rewriteNamespaceRawStmt(ast::Statement* stmt, const ModuleNSMap& localNS) {
    if (!stmt) return;
    if (auto* n = dynamic_cast<ast::BlockStatement*>(stmt)) {
        rewriteBlockBody(n->body, localNS);
    } else if (auto* n = dynamic_cast<ast::ExpressionStatement*>(stmt)) {
        rewriteNamespaceExpr(n->expression, localNS);
    } else if (auto* n = dynamic_cast<ast::IfStatement*>(stmt)) {
        rewriteNamespaceExpr(n->test, localNS);
        rewriteNamespaceStmt(n->consequent, localNS);
        rewriteNamespaceStmt(n->alternate, localNS);
    } else if (auto* n = dynamic_cast<ast::WhileStatement*>(stmt)) {
        rewriteNamespaceExpr(n->test, localNS);
        rewriteNamespaceStmt(n->body, localNS);
    } else if (auto* n = dynamic_cast<ast::ForStatement*>(stmt)) {
        if (n->init) {
            if (auto* es = dynamic_cast<ast::ExpressionStatement*>(n->init.get())) {
                rewriteNamespaceExpr(es->expression, localNS);
            } else if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(n->init.get())) {
                foldVariableInit(vd->init, localNS);
            }
        }
        rewriteNamespaceExpr(n->test, localNS);
        rewriteNamespaceExpr(n->update, localNS);
        rewriteNamespaceStmt(n->body, localNS);
    } else if (auto* n = dynamic_cast<ast::ReturnStatement*>(stmt)) {
        rewriteNamespaceExpr(n->argument, localNS);
    } else if (auto* n = dynamic_cast<ast::PassStatement*>(stmt)) {
        rewriteNamespaceExpr(n->argument, localNS);
    } else if (auto* n = dynamic_cast<ast::TryStatement*>(stmt)) {
        if (n->tryBlock) rewriteBlockBody(n->tryBlock->body, localNS);
        if (n->catchBlock) rewriteBlockBody(n->catchBlock->body, localNS);
        if (n->finallyBlock) rewriteBlockBody(n->finallyBlock->body, localNS);
    } else if (auto* n = dynamic_cast<ast::UnsafeStatement*>(stmt)) {
        if (n->block) rewriteBlockBody(n->block->body, localNS);
    } else if (auto* n = dynamic_cast<ast::ThrowStatement*>(stmt)) {
        rewriteNamespaceExpr(n->expr, localNS);
    } else if (auto* n = dynamic_cast<ast::MatchStatement*>(stmt)) {
        rewriteNamespaceExpr(n->expr, localNS);
        for (auto& c : n->cases) {
            rewriteNamespaceExpr(c.first, localNS);
            rewriteNamespaceExpr(c.second, localNS);
        }
        for (auto& g : n->guards) rewriteNamespaceExpr(g, localNS);
    } else if (auto* n = dynamic_cast<ast::YieldStatement*>(stmt)) {
        rewriteNamespaceExpr(n->expression, localNS);
    } else if (auto* n = dynamic_cast<ast::YieldReturnStatement*>(stmt)) {
        rewriteNamespaceExpr(n->expression, localNS);
    } else if (auto* n = dynamic_cast<ast::AssertStatement*>(stmt)) {
        rewriteNamespaceExpr(n->condition, localNS);
        rewriteNamespaceExpr(n->message, localNS);
    } else if (auto* n = dynamic_cast<ast::FailStatement*>(stmt)) {
        rewriteNamespaceExpr(n->error, localNS);
    } else if (auto* n = dynamic_cast<ast::RefailStatement*>(stmt)) {
        rewriteNamespaceExpr(n->wrappedError, localNS);
    } else if (auto* n = dynamic_cast<ast::PanicStatement*>(stmt)) {
        rewriteNamespaceExpr(n->message, localNS);
    } else if (auto* n = dynamic_cast<ast::ExitStatement*>(stmt)) {
        rewriteNamespaceExpr(n->code, localNS);
    } else if (auto* n = dynamic_cast<ast::DeferStatement*>(stmt)) {
        rewriteNamespaceStmt(n->statement, localNS);
    } else if (auto* n = dynamic_cast<ast::TupleDestructureAssignment*>(stmt)) {
        rewriteNamespaceExpr(n->expression, localNS);
    } else if (auto* n = dynamic_cast<ast::VariableDeclaration*>(stmt)) {
        foldVariableInit(n->init, localNS);
    } else if (auto* n = dynamic_cast<ast::FunctionDeclaration*>(stmt)) {
        if (n->body) rewriteBlockBody(n->body->body, localNS);
    } else if (auto* n = dynamic_cast<ast::StructDeclaration*>(stmt)) {
        for (const auto& f : n->fields) {
            if (f) rewriteNamespaceExpr(f->initializer, localNS);
        }
        for (const auto& ctor : n->constructors) {
            if (ctor && ctor->body) rewriteBlockBody(ctor->body->body, localNS);
        }
    } else if (auto* n = dynamic_cast<ast::AspectDeclaration*>(stmt)) {
        for (const auto& method : n->methods) {
            if (method && method->body) rewriteBlockBody(method->body->body, localNS);
        }
    } else if (auto* n = dynamic_cast<ast::BindDeclaration*>(stmt)) {
        for (const auto& method : n->methods) {
            if (method && method->body) rewriteBlockBody(method->body->body, localNS);
        }
    } else if (auto* n = dynamic_cast<ast::ClassDeclaration*>(stmt)) {
        for (const auto& m : n->members) {
            if (!m) continue;
            if (auto* f = dynamic_cast<ast::FieldDeclaration*>(m.get())) {
                rewriteNamespaceExpr(f->initializer, localNS);
            } else if (auto* fn = dynamic_cast<ast::FunctionDeclaration*>(m.get())) {
                if (fn->body) rewriteBlockBody(fn->body->body, localNS);
            }
        }
    }
}

// --- Expression traversal (fold a namespace root, else recurse) ---
static void rewriteNamespaceExpr(ast::ExprPtr& expr, const ModuleNSMap& localNS) {
    if (!expr) return;
    if (foldNamespaceMember(expr, localNS)) {
        return;
    }
    if (auto* n = dynamic_cast<ast::CallExpression*>(expr.get())) {
        rewriteNamespaceExpr(n->callee, localNS);
        for (auto& a : n->arguments) rewriteNamespaceExpr(a, localNS);
    } else if (auto* n = dynamic_cast<ast::MemberExpression*>(expr.get())) {
        rewriteNamespaceExpr(n->object, localNS);
        rewriteNamespaceExpr(n->property, localNS);
    } else if (auto* n = dynamic_cast<ast::AssignmentExpression*>(expr.get())) {
        rewriteNamespaceExpr(n->left, localNS);
        rewriteNamespaceExpr(n->right, localNS);
    } else if (auto* n = dynamic_cast<ast::ObjectLiteral*>(expr.get())) {
        for (auto& prop : n->properties) rewriteNamespaceExpr(prop.value, localNS);
    } else if (auto* n = dynamic_cast<ast::UnaryExpression*>(expr.get())) {
        rewriteNamespaceExpr(n->operand, localNS);
    } else if (auto* n = dynamic_cast<ast::BinaryExpression*>(expr.get())) {
        rewriteNamespaceExpr(n->left, localNS);
        rewriteNamespaceExpr(n->right, localNS);
    } else if (auto* n = dynamic_cast<ast::LogicalExpression*>(expr.get())) {
        rewriteNamespaceExpr(n->left, localNS);
        rewriteNamespaceExpr(n->right, localNS);
    } else if (auto* n = dynamic_cast<ast::ConditionalExpression*>(expr.get())) {
        rewriteNamespaceExpr(n->condition, localNS);
        rewriteNamespaceExpr(n->thenExpr, localNS);
        rewriteNamespaceExpr(n->elseExpr, localNS);
    } else if (auto* n = dynamic_cast<ast::SequenceExpression*>(expr.get())) {
        for (auto& x : n->expressions) rewriteNamespaceExpr(x, localNS);
    } else if (auto* n = dynamic_cast<ast::ArrayLiteral*>(expr.get())) {
        for (auto& x : n->elements) rewriteNamespaceExpr(x, localNS);
    } else if (auto* n = dynamic_cast<ast::BorrowExpression*>(expr.get())) {
        rewriteNamespaceExpr(n->expression, localNS);
    } else if (auto* n = dynamic_cast<ast::PointerDerefExpression*>(expr.get())) {
        rewriteNamespaceExpr(n->pointer, localNS);
    } else if (auto* n = dynamic_cast<ast::AddrOfExpression*>(expr.get())) {
        rewriteNamespaceExpr(n->getLocation(), localNS);
    } else if (auto* n = dynamic_cast<ast::ArrayElementExpression*>(expr.get())) {
        rewriteNamespaceExpr(n->array, localNS);
        rewriteNamespaceExpr(n->index, localNS);
    } else if (auto* n = dynamic_cast<ast::LocationExpression*>(expr.get())) {
        rewriteNamespaceExpr(n->expression, localNS);
    } else if (auto* n = dynamic_cast<ast::ListComprehension*>(expr.get())) {
        rewriteNamespaceExpr(n->elementExpr, localNS);
        rewriteNamespaceExpr(n->iterableExpr, localNS);
        rewriteNamespaceExpr(n->conditionExpr, localNS);
    } else if (auto* n = dynamic_cast<ast::IfExpression*>(expr.get())) {
        rewriteNamespaceExpr(n->condition, localNS);
        rewriteNamespaceExpr(n->thenBranch, localNS);
        rewriteNamespaceExpr(n->elseBranch, localNS);
    } else if (auto* n = dynamic_cast<ast::ConstructionExpression*>(expr.get())) {
        for (auto& a : n->arguments) rewriteNamespaceExpr(a, localNS);
    } else if (auto* n = dynamic_cast<ast::ArrayInitializationExpression*>(expr.get())) {
        rewriteNamespaceExpr(n->sizeExpression, localNS);
    } else if (auto* n = dynamic_cast<ast::GenericInstantiationExpression*>(expr.get())) {
        rewriteNamespaceExpr(n->baseExpression, localNS);
    } else if (auto* n = dynamic_cast<ast::FunctionExpression*>(expr.get())) {
        rewriteNamespaceExpr(n->body, localNS);
    } else if (auto* n = dynamic_cast<ast::AwaitExpression*>(expr.get())) {
        rewriteNamespaceExpr(n->expr, localNS);
    } else if (auto* n = dynamic_cast<ast::RangeExpression*>(expr.get())) {
        rewriteNamespaceExpr(n->start, localNS);
        rewriteNamespaceExpr(n->end, localNS);
        rewriteNamespaceExpr(n->step, localNS);
    } else if (auto* n = dynamic_cast<ast::BlockExpression*>(expr.get())) {
        if (n->block) rewriteBlockBody(n->block->body, localNS);
        for (const auto& tc : n->trapClauses) {
            if (tc) rewriteNamespaceRawStmt(tc->handler.get(), localNS);
        }
        if (n->ensureClause) rewriteNamespaceRawStmt(n->ensureClause->cleanupBlock.get(), localNS);
    } else if (auto* n = dynamic_cast<ast::SelectExpression*>(expr.get())) {
        rewriteNamespaceExpr(n->expr, localNS);
        for (auto& c : n->cases) {
            rewriteNamespaceExpr(c.first, localNS);
            rewriteNamespaceExpr(c.second, localNS);
        }
    } else if (auto* n = dynamic_cast<ast::ComparisonPattern*>(expr.get())) {
        rewriteNamespaceExpr(n->value, localNS);
    } else if (auto* n = dynamic_cast<ast::SetPattern*>(expr.get())) {
        for (auto& e : n->elements) rewriteNamespaceExpr(e, localNS);
    } else if (auto* n = dynamic_cast<ast::TypeofExpression*>(expr.get())) {
        rewriteNamespaceExpr(n->operand, localNS);
    } else if (auto* n = dynamic_cast<ast::TypenameExpression*>(expr.get())) {
        rewriteNamespaceExpr(n->operand, localNS);
    } else if (auto* n = dynamic_cast<ast::AsExpression*>(expr.get())) {
        rewriteNamespaceExpr(n->operand, localNS);
    } else if (auto* n = dynamic_cast<ast::MatchExpression*>(expr.get())) {
        rewriteNamespaceRawStmt(n->match.get(), localNS);
    }
}

// Maps a namespace's carried origin symbol name to its mangled name.
using NSSymbolMap = std::unordered_map<std::string, std::string>;

// Rewrites, within a carried namespace declaration's subtree, every bare
// identifier that names another symbol of the same namespace to its mangled form
// (and, transitively, inside the statements/expressions of that declaration). This
// keeps a module's own cross-references consistent once its top-level names are
// mangled. A function-local variable that happens to shadow a namespace symbol is
// treated as a reference (an accepted corner case for unusual naming).
static void mangleBareExpr(ast::ExprPtr& expr, const NSSymbolMap& symbolToMangled);
static void mangleBareRawStmt(ast::Statement* stmt, const NSSymbolMap& symbolToMangled);
static void mangleBareStmt(ast::StmtPtr& stmt, const NSSymbolMap& symbolToMangled) {
    if (!stmt) return;
    mangleBareRawStmt(stmt.get(), symbolToMangled);
}
static void mangleBareBlock(std::vector<ast::StmtPtr>& body, const NSSymbolMap& m) {
    for (auto& st : body) if (st) mangleBareStmt(st, m);
}
static void foldBareInit(std::shared_ptr<ast::Expression>& init, const NSSymbolMap& symbolToMangled) {
    if (!init) return;
    if (auto* id = dynamic_cast<ast::Identifier*>(init.get())) {
        auto it = symbolToMangled.find(id->name);
        if (it != symbolToMangled.end()) {
            init = std::make_shared<ast::Identifier>(id->loc, it->second);
        }
    }
}

#define MANGLE_MEMBER(NAME) if (!foldIdentifierMember(NAME, symbolToMangled)) mangleBareExpr(NAME, symbolToMangled)

static bool foldIdentifierMember(ast::ExprPtr& e, const NSSymbolMap& symbolToMangled) {
    auto* id = dynamic_cast<ast::Identifier*>(e.get());
    if (!id) return false;
    auto it = symbolToMangled.find(id->name);
    if (it == symbolToMangled.end()) return false;
    ast::ExprPtr replacement = std::make_unique<ast::Identifier>(id->loc, it->second);
    e = std::move(replacement);
    return true;
}

static void mangleBareExpr(ast::ExprPtr& expr, const NSSymbolMap& symbolToMangled) {
    if (!expr) return;
    if (foldIdentifierMember(expr, symbolToMangled)) return;
    if (auto* n = dynamic_cast<ast::CallExpression*>(expr.get())) {
        MANGLE_MEMBER(n->callee);
        for (auto& a : n->arguments) MANGLE_MEMBER(a);
    } else if (auto* n = dynamic_cast<ast::MemberExpression*>(expr.get())) {
        MANGLE_MEMBER(n->object);
        MANGLE_MEMBER(n->property);
    } else if (auto* n = dynamic_cast<ast::AssignmentExpression*>(expr.get())) {
        MANGLE_MEMBER(n->left);
        MANGLE_MEMBER(n->right);
    } else if (auto* n = dynamic_cast<ast::ObjectLiteral*>(expr.get())) {
        for (auto& prop : n->properties) MANGLE_MEMBER(prop.value);
    } else if (auto* n = dynamic_cast<ast::UnaryExpression*>(expr.get())) {
        MANGLE_MEMBER(n->operand);
    } else if (auto* n = dynamic_cast<ast::BinaryExpression*>(expr.get())) {
        MANGLE_MEMBER(n->left); MANGLE_MEMBER(n->right);
    } else if (auto* n = dynamic_cast<ast::LogicalExpression*>(expr.get())) {
        MANGLE_MEMBER(n->left); MANGLE_MEMBER(n->right);
    } else if (auto* n = dynamic_cast<ast::ConditionalExpression*>(expr.get())) {
        MANGLE_MEMBER(n->condition); MANGLE_MEMBER(n->thenExpr); MANGLE_MEMBER(n->elseExpr);
    } else if (auto* n = dynamic_cast<ast::SequenceExpression*>(expr.get())) {
        for (auto& x : n->expressions) MANGLE_MEMBER(x);
    } else if (auto* n = dynamic_cast<ast::ArrayLiteral*>(expr.get())) {
        for (auto& x : n->elements) MANGLE_MEMBER(x);
    } else if (auto* n = dynamic_cast<ast::BorrowExpression*>(expr.get())) {
        MANGLE_MEMBER(n->expression);
    } else if (auto* n = dynamic_cast<ast::PointerDerefExpression*>(expr.get())) {
        MANGLE_MEMBER(n->pointer);
    } else if (auto* n = dynamic_cast<ast::AddrOfExpression*>(expr.get())) {
        MANGLE_MEMBER(n->getLocation());
    } else if (auto* n = dynamic_cast<ast::ArrayElementExpression*>(expr.get())) {
        MANGLE_MEMBER(n->array); MANGLE_MEMBER(n->index);
    } else if (auto* n = dynamic_cast<ast::LocationExpression*>(expr.get())) {
        MANGLE_MEMBER(n->expression);
    } else if (auto* n = dynamic_cast<ast::ListComprehension*>(expr.get())) {
        MANGLE_MEMBER(n->elementExpr); MANGLE_MEMBER(n->iterableExpr); MANGLE_MEMBER(n->conditionExpr);
    } else if (auto* n = dynamic_cast<ast::IfExpression*>(expr.get())) {
        MANGLE_MEMBER(n->condition); MANGLE_MEMBER(n->thenBranch); MANGLE_MEMBER(n->elseBranch);
    } else if (auto* n = dynamic_cast<ast::ConstructionExpression*>(expr.get())) {
        for (auto& a : n->arguments) MANGLE_MEMBER(a);
    } else if (auto* n = dynamic_cast<ast::ArrayInitializationExpression*>(expr.get())) {
        MANGLE_MEMBER(n->sizeExpression);
    } else if (auto* n = dynamic_cast<ast::GenericInstantiationExpression*>(expr.get())) {
        MANGLE_MEMBER(n->baseExpression);
    } else if (auto* n = dynamic_cast<ast::FunctionExpression*>(expr.get())) {
        MANGLE_MEMBER(n->body);
    } else if (auto* n = dynamic_cast<ast::AwaitExpression*>(expr.get())) {
        MANGLE_MEMBER(n->expr);
    } else if (auto* n = dynamic_cast<ast::RangeExpression*>(expr.get())) {
        MANGLE_MEMBER(n->start); MANGLE_MEMBER(n->end); MANGLE_MEMBER(n->step);
    } else if (auto* n = dynamic_cast<ast::BlockExpression*>(expr.get())) {
        if (n->block) mangleBareBlock(n->block->body, symbolToMangled);
    } else if (auto* n = dynamic_cast<ast::SelectExpression*>(expr.get())) {
        MANGLE_MEMBER(n->expr);
        for (auto& c : n->cases) { MANGLE_MEMBER(c.first); MANGLE_MEMBER(c.second); }
    } else if (auto* n = dynamic_cast<ast::ComparisonPattern*>(expr.get())) {
        MANGLE_MEMBER(n->value);
    } else if (auto* n = dynamic_cast<ast::TypeofExpression*>(expr.get())) {
        MANGLE_MEMBER(n->operand);
    } else if (auto* n = dynamic_cast<ast::TypenameExpression*>(expr.get())) {
        MANGLE_MEMBER(n->operand);
    } else if (auto* n = dynamic_cast<ast::AsExpression*>(expr.get())) {
        MANGLE_MEMBER(n->operand);
    } else if (auto* n = dynamic_cast<ast::MatchExpression*>(expr.get())) {
        if (n->match) mangleBareRawStmt(n->match.get(), symbolToMangled);
    }
}

static void mangleBareRawStmt(ast::Statement* stmt, const NSSymbolMap& symbolToMangled) {
    if (!stmt) return;
    if (auto* n = dynamic_cast<ast::BlockStatement*>(stmt)) {
        mangleBareBlock(n->body, symbolToMangled);
    } else if (auto* n = dynamic_cast<ast::ExpressionStatement*>(stmt)) {
        MANGLE_MEMBER(n->expression);
    } else if (auto* n = dynamic_cast<ast::IfStatement*>(stmt)) {
        MANGLE_MEMBER(n->test); mangleBareRawStmt(n->consequent.get(), symbolToMangled); mangleBareRawStmt(n->alternate.get(), symbolToMangled);
    } else if (auto* n = dynamic_cast<ast::WhileStatement*>(stmt)) {
        MANGLE_MEMBER(n->test); mangleBareRawStmt(n->body.get(), symbolToMangled);
    } else if (auto* n = dynamic_cast<ast::ForStatement*>(stmt)) {
        if (n->init) {
            if (auto* es = dynamic_cast<ast::ExpressionStatement*>(n->init.get())) MANGLE_MEMBER(es->expression);
            else if (auto* vd = dynamic_cast<ast::VariableDeclaration*>(n->init.get())) foldBareInit(vd->init, symbolToMangled);
        }
        MANGLE_MEMBER(n->test); MANGLE_MEMBER(n->update); mangleBareRawStmt(n->body.get(), symbolToMangled);
    } else if (auto* n = dynamic_cast<ast::ReturnStatement*>(stmt)) {
        MANGLE_MEMBER(n->argument);
    } else if (auto* n = dynamic_cast<ast::PassStatement*>(stmt)) {
        MANGLE_MEMBER(n->argument);
    } else if (auto* n = dynamic_cast<ast::TryStatement*>(stmt)) {
        if (n->tryBlock) mangleBareBlock(n->tryBlock->body, symbolToMangled);
        if (n->catchBlock) mangleBareBlock(n->catchBlock->body, symbolToMangled);
        if (n->finallyBlock) mangleBareBlock(n->finallyBlock->body, symbolToMangled);
    } else if (auto* n = dynamic_cast<ast::UnsafeStatement*>(stmt)) {
        if (n->block) mangleBareBlock(n->block->body, symbolToMangled);
    } else if (auto* n = dynamic_cast<ast::ThrowStatement*>(stmt)) {
        MANGLE_MEMBER(n->expr);
    } else if (auto* n = dynamic_cast<ast::MatchStatement*>(stmt)) {
        MANGLE_MEMBER(n->expr);
        for (auto& c : n->cases) { MANGLE_MEMBER(c.first); MANGLE_MEMBER(c.second); }
        for (auto& g : n->guards) MANGLE_MEMBER(g);
    } else if (auto* n = dynamic_cast<ast::YieldStatement*>(stmt)) {
        MANGLE_MEMBER(n->expression);
    } else if (auto* n = dynamic_cast<ast::YieldReturnStatement*>(stmt)) {
        MANGLE_MEMBER(n->expression);
    } else if (auto* n = dynamic_cast<ast::AssertStatement*>(stmt)) {
        MANGLE_MEMBER(n->condition); MANGLE_MEMBER(n->message);
    } else if (auto* n = dynamic_cast<ast::FailStatement*>(stmt)) {
        MANGLE_MEMBER(n->error);
    } else if (auto* n = dynamic_cast<ast::RefailStatement*>(stmt)) {
        MANGLE_MEMBER(n->wrappedError);
    } else if (auto* n = dynamic_cast<ast::PanicStatement*>(stmt)) {
        MANGLE_MEMBER(n->message);
    } else if (auto* n = dynamic_cast<ast::ExitStatement*>(stmt)) {
        MANGLE_MEMBER(n->code);
    } else if (auto* n = dynamic_cast<ast::DeferStatement*>(stmt)) {
        mangleBareRawStmt(n->statement.get(), symbolToMangled);
    } else if (auto* n = dynamic_cast<ast::TupleDestructureAssignment*>(stmt)) {
        MANGLE_MEMBER(n->expression);
    } else if (auto* n = dynamic_cast<ast::VariableDeclaration*>(stmt)) {
        foldBareInit(n->init, symbolToMangled);
    } else if (auto* n = dynamic_cast<ast::FunctionDeclaration*>(stmt)) {
        if (n->body) mangleBareBlock(n->body->body, symbolToMangled);
    } else if (auto* n = dynamic_cast<ast::StructDeclaration*>(stmt)) {
        for (const auto& ctor : n->constructors) if (ctor && ctor->body) mangleBareBlock(ctor->body->body, symbolToMangled);
    } else if (auto* n = dynamic_cast<ast::AspectDeclaration*>(stmt)) {
        for (const auto& method : n->methods) if (method && method->body) mangleBareBlock(method->body->body, symbolToMangled);
    } else if (auto* n = dynamic_cast<ast::BindDeclaration*>(stmt)) {
        for (const auto& method : n->methods) if (method && method->body) mangleBareBlock(method->body->body, symbolToMangled);
    } else if (auto* n = dynamic_cast<ast::ClassDeclaration*>(stmt)) {
        for (const auto& m : n->members) {
            if (!m) continue;
            if (auto* fn = dynamic_cast<ast::FunctionDeclaration*>(m.get())) { if (fn->body) mangleBareBlock(fn->body->body, symbolToMangled); }
        }
    }
}

#undef MANGLE_MEMBER

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
    std::string rootKey = resolveModule(source, fileName, "<root>");
    auto it = records_.find(rootKey);
    if (it == records_.end() || !it->second.module) {
        throw std::runtime_error("Module resolution failed for root module: " + rootKey);
    }
    return std::move(it->second.module);
}

std::string ModuleRegistry::resolveModule(const std::string& source,
                                          const fs::path& sourcePath,
                                          const std::string& importSpelling) {
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
        // Declaring-module key of each name already spliced into this module. With
        // clone-on-import a shared dependency (e.g. core::aspects) is deep-cloned
        // into several consumer modules, so the SAME origin declaration can surface
        // here twice via different feature paths; tracking its origin lets the
        // re-splice be skipped instead of colliding. A genuinely distinct
        // declaration with the same name still errors.
        std::unordered_map<std::string, std::string> seenOrigins;
        auto declaringOrigin = [&](const std::string& n) -> std::string {
            auto it = moduleKeyByName_.find(n);
            return it != moduleKeyByName_.end() ? it->second : currentKey;
        };
        size_t importIndex = 0;

        currentRecord.bundles = metadata.bundles;
        currentRecord.sharesByName = metadata.sharesByName;
        currentRecord.importedModuleKeys.clear();

        // Namespace-scope bookkeeping: this module's own declarations seed its
        // resolvable scope and belong to it; its `share(...)` declarations are
        // the names it exports to importers. Plain-imported symbols are granted
        // into the scope below but never become part of the export set, so a
        // consumer of this module does not inherit this module's dependencies.
        std::unordered_set<std::string>& ownScope = effectiveScope_[currentKey];
        for (auto& stmt : module->body) {
            std::string name = declarationName(stmt);
            if (name.empty()) {
                continue;
            }
            ownScope.insert(name);
            moduleKeyByName_[name] = currentKey;
        }
        for (const auto& kv : metadata.sharesByName) {
            exports_[currentKey].insert(kv.first);
        }

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

        // Per-module namespace bindings (ns name -> carried symbol origin names)
        // and the indices of this module's own (non-import, non-spliced) top-level
        // statements, which are the only ones rewritten for qualified `NS.sym`.
        ModuleNSMap localNamespaces;
        std::vector<size_t> ownStmtIndices;

        for (auto& stmt : module->body) {
            if (auto* importDecl = dynamic_cast<ast::ImportDeclaration*>(stmt.get())) {
                std::vector<std::string> importShare;
                if (importIndex < metadata.importShares.size()) {
                    importShare = metadata.importShares[importIndex];
                }
                ++importIndex;

                // Whole-module namespace import: `import module as NS`,
                // `import * as NS from "..."`, or the legacy `as`-alias encoding
                // (a specifier carrying a null imported name). The namespace binds
                // the origin module's visible exports under `NS` for qualified
                // `NS.sym` access instead of importing them into the module's
                // unqualified scope.
                std::string nsName = importDecl->namespaceImport ? importDecl->namespaceImport->name : "";
                for (const auto& specifier : importDecl->specifiers) {
                    if (!specifier.importedName) {
                        if (specifier.localName && nsName.empty()) {
                            nsName = specifier.localName->name;
                        }
                        continue;
                    }
                    if (!nsName.empty()) {
                        throw std::runtime_error(
                            "Module resolution error in " + currentKey +
                            ": cannot combine a namespace alias with an import list");
                    }
                }
                const bool isNamespaceImport = !nsName.empty();

                if (!options_.skipImportResolution) {
                ResolvedImportPath importPath = resolveImportPath(importDecl, currentPath);
                std::string importedSource = readSourceFile(importPath.resolvedPath);
                std::string importedKey;
                try {
                    importedKey = resolveModule(importedSource, importPath.resolvedPath, importPath.importSpelling);
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

                std::unordered_map<std::string, std::string> requestedRenames;
                std::unordered_set<std::string> requestedNames;
                for (const auto& specifier : importDecl->specifiers) {
                    const std::string importedName = specifier.importedName->name;
                    requestedNames.insert(importedName);
                    requestedRenames[importedName] = specifier.localName ? specifier.localName->name : importedName;
                }

                // Subset-import dependency closure: when a specific name (e.g.
                // `foo`) is requested, any module-level sibling it transitively
                // references (e.g. `bar`) must also be carried so the carried
                // declaration can be compiled inside this module. Only names
                // that are *visible* where the origin sees them are promoted
                // (mirrors whole-module import), so a private helper cannot be
                // dragged in accidentally. The closure is computed over the
                // origin's already-resolved body, which includes anything the
                // origin itself imported, so imports-of-imports are carried too.
                bool closureComputed = false;
                std::unordered_set<std::string> carryNames = requestedNames;
                // Binds (keyed `bind:SelfType:Aspect`) that a carried declaration
                // refers to through its method calls. Tracked separately because a
                // bind has no user-facing symbol to alias; the `bind:`-prefixed keys
                // never collide with ordinary declaration names.
                std::unordered_set<std::string> carryBindKeys;
                // Recompute the dependency closure on every subset import of this
                // module: the origin body is now deep-cloned per importer (rather
                // than moved into the first importer), so a repeat subset import of
                // the same module must be spliced independently again.
                if (!requestedNames.empty() && importedRecord.module) {
                    // Module-level declarations this import may depend on, plus a
                    // bind side-table. Binds are keyed by `bind:SelfType:Aspect` and
                    // also indexed by the method names they provide, so the closure
                    // can follow aspect dispatch (`v.iter()`, `it.next()`) that a
                    // carried declaration uses. Without this a leaf module's
                    // `for (x in v)` would fail to resolve `iter`/`next` unless the
                    // consumer also imported collections.
                    std::unordered_map<std::string, ast::Node*> declByName;
                    std::unordered_set<std::string> moduleDeclNames;
                    std::unordered_map<std::string, ast::Node*> bindByKey;
                    std::unordered_map<std::string, std::vector<std::string>> bindMethodToKeys;
                    // Bind reachability footprint. A bind is admitted to the
                    // closure only when its own self type is already in play, so
                    // a `Vec` `for..in` does not drag in the iterator binds of
                    // every other collection. `bindSelfBase` is the unwrapped
                    // self-type name (e.g. `Vec`); `bindTypeBases` is the union
                    // of the self type, trait type, and every method return type,
                    // which is what makes a downstream bind (e.g. `next` on the
                    // `VecIter` returned by `iter`) reachable once `iter` is
                    // admitted.
                    std::unordered_map<std::string, std::string> bindSelfBase;
                    std::unordered_map<std::string, std::unordered_set<std::string>> bindTypeBases;
                    auto baseTypeName = [](const ast::TypeNode* ty) -> std::string {
                        if (!ty) return "";
                        if (auto* tn = dynamic_cast<const ast::TypeName*>(ty)) {
                            if (tn->identifier) return tn->identifier->name;
                        }
                        return "";
                    };
                    for (const auto& decl : importedRecord.module->body) {
                        if (!decl) continue;
                        if (dynamic_cast<ast::ImportDeclaration*>(decl.get())) continue;
                        if (auto* bind = dynamic_cast<ast::BindDeclaration*>(decl.get())) {
                            std::string bk = declarationName(decl);
                            if (bk.empty()) continue;
                            bindByKey.emplace(bk, decl.get());
                            for (const auto& m : bind->methods) {
                                if (m && m->id) bindMethodToKeys[m->id->name].push_back(bk);
                            }
                            std::string selfBase = baseTypeName(bind->selfType.get());
                            if (!selfBase.empty()) bindSelfBase[bk] = selfBase;
                            auto& bases = bindTypeBases[bk];
                            if (!selfBase.empty()) bases.insert(selfBase);
                            std::string traitBase = baseTypeName(bind->traitType.get());
                            if (!traitBase.empty()) bases.insert(traitBase);
                            for (const auto& m : bind->methods) {
                                if (m) {
                                    std::string rb = baseTypeName(m->returnTypeNode.get());
                                    if (!rb.empty()) bases.insert(rb);
                                }
                            }
                            continue;
                        }
                        std::string n = declarationName(decl);
                        if (n.empty()) continue;
                        moduleDeclNames.insert(n);
                        declByName.emplace(n, decl.get());
                    }
                    FreeIdentifierCollector collector;
                    // Order-independent fixpoint. A single pass can see `next`
                    // referenced before `iter` has made `VecIter` reachable, so
                    // keep rescanning until nothing new is admitted.
                    std::unordered_set<std::string> reachableTypes(carryNames.begin(), carryNames.end());
                    std::unordered_set<std::string> referencedMethods;
                    std::unordered_set<std::string> declWalked;
                    std::unordered_set<std::string> bindBodyWalked;
                    bool changed = true;
                    while (changed) {
                        changed = false;
                        // Walk carried module declarations and admitted binds,
                        // absorbing free identifiers into reachability. Walking a
                        // declaration's signature is enough to seed its receiver
                        // types (e.g. `Vec`), so only that type's binds are
                        // reachable.
                        std::vector<std::string> passDecls(carryNames.begin(), carryNames.end());
                        for (const auto& key : passDecls) {
                            if (!declWalked.insert(key).second) continue;
                            auto it = declByName.find(key);
                            if (it == declByName.end()) continue;
                            collector.names.clear();
                            it->second->accept(collector);
                            for (const auto& ref : collector.names) {
                                reachableTypes.insert(ref);
                                if (moduleDeclNames.count(ref) && carryNames.insert(ref).second) {
                                    changed = true;
                                }
                                if (bindMethodToKeys.count(ref)) referencedMethods.insert(ref);
                            }
                        }
                        std::vector<std::string> passBinds(carryBindKeys.begin(), carryBindKeys.end());
                        for (const auto& key : passBinds) {
                            if (!bindBodyWalked.insert(key).second) continue;
                            auto it = bindByKey.find(key);
                            if (it == bindByKey.end()) continue;
                            collector.names.clear();
                            it->second->accept(collector);
                            for (const auto& ref : collector.names) {
                                reachableTypes.insert(ref);
                                if (moduleDeclNames.count(ref) && carryNames.insert(ref).second) {
                                    changed = true;
                                }
                                if (bindMethodToKeys.count(ref)) referencedMethods.insert(ref);
                            }
                        }
                        // Admit binds for referenced methods whose self type is
                        // reachable; their footprints extend reachability, so this
                        // must run to a fixpoint in its own right too.
                        bool admitted = true;
                        while (admitted) {
                            admitted = false;
                            for (const auto& m : referencedMethods) {
                                auto mIt = bindMethodToKeys.find(m);
                                if (mIt == bindMethodToKeys.end()) continue;
                                for (const auto& bk : mIt->second) {
                                    if (carryBindKeys.count(bk)) continue;
                                    auto sbIt = bindSelfBase.find(bk);
                                    if (sbIt == bindSelfBase.end() ||
                                        !reachableTypes.count(sbIt->second)) {
                                        continue;
                                    }
                                    carryBindKeys.insert(bk);
                                    auto tbIt = bindTypeBases.find(bk);
                                    if (tbIt != bindTypeBases.end()) {
                                        for (const auto& base : tbIt->second) {
                                            reachableTypes.insert(base);
                                        }
                                    }
                                    changed = true;
                                    admitted = true;
                                }
                            }
                        }
                    }
                    closureComputed = true;
                }

                // Grants: a plain `import` exposes only the origin module's
                // *genuine* exports to this module's resolvable scope (a leaked
                // transitive dependency of the origin is not available here);
                // `smuggle` grants whatever is smuggled; `share(...)` also adds
                // the granted names back to this module's exports. Computed for
                // every importer regardless of how many times the origin module is
                // consumed, so each importer's scope is correct regardless of
                // splice order. A namespace import adds nothing to the unqualified
                // scope (access is via `NS.sym` only).
                if (!isNamespaceImport) {
                    const bool isSmuggle = importDecl->kind == ast::ImportKind::Smuggle;
                    const std::unordered_set<std::string>& importedExports = exports_[importedKey];
                    const std::unordered_set<std::string>& importedScope =
                        isSmuggle ? allNames_[importedKey] : importedExports;
                    std::unordered_set<std::string>& scope = effectiveScope_[currentKey];
                    if (requestedNames.empty()) {
                        scope.insert(importedScope.begin(), importedScope.end());
                        if (!importShare.empty() && !isSmuggle) {
                            exports_[currentKey].insert(importedScope.begin(), importedScope.end());
                        }
                    } else {
                        for (const auto& pair : requestedRenames) {
                            if (isSmuggle || importedExports.count(pair.first)) {
                                scope.insert(pair.second);
                                if (!importShare.empty() && !isSmuggle) {
                                    exports_[currentKey].insert(pair.second);
                                }
                            }
                        }
                        // Dependency siblings spliced in alongside a requested
                        // name are resolvable in this module's code but are never
                        // added to its export set (they only leak to a *consumer*
                        // of this module when an export actually references them).
                        if (closureComputed) {
                            for (const auto& depName : carryNames) {
                                if (!requestedNames.count(depName) &&
                                    importedExports.count(depName)) {
                                    scope.insert(depName);
                                }
                            }
                        }
                    }
                }

                if (!importedRecord.module) {
                    continue;
                }

                // Immutable snapshot for filtering carried binds: requestedNames shrinks
                // as aspects are spliced, but binds must not leak in just because the
                // requested set emptied mid-splice.
                const std::unordered_set<std::string> requestedForBinds = requestedNames;

                // Namespace symbol map collected from splicing (origin name -> mangled
                // name) exposed under `nsName`. Only genuine non-bind declarations
                // participate. Precomputed so bare cross-references inside any carried
                // declaration can be re-mapped to their mangled names consistently.
                std::unordered_map<std::string, std::string> namespaceMangled;
                if (isNamespaceImport) {
                    for (const auto& decl : importedRecord.module->body) {
                        if (!decl || isMainFunction(decl)) continue;
                        if (dynamic_cast<ast::BindDeclaration*>(decl.get())) continue;
                        std::string n = declarationName(decl);
                        if (n.empty()) continue;
                        if (declarationVisible(n, importedRecord, metadata.bundles, importDecl)) {
                            namespaceMangled[n] = mangledNamespaceName(nsName, n);
                        }
                    }
                }

                // A carried declaration keeps the visibility it had in its origin
                // module (e.g. a `share(all)` dependency symbol stays share(all))
                // so the next import hop still carries it; an explicit `share(...)`
                // on this import overrides that. Without this inheritance a plain
                // import would record no entry in the importing module's
                // sharesByName, and `declarationVisible` would silently drop the
                // transitively-imported symbol on the following hop (a module
                // couldn't even use its own imports).
                auto carryShare = [&](const std::string& originName,
                                      const std::string& localName) {
                    auto originShare = importedRecord.sharesByName.find(originName);
                    if (originShare != importedRecord.sharesByName.end()) {
                        currentRecord.sharesByName[localName] = originShare->second;
                    } else if (!importShare.empty()) {
                        currentRecord.sharesByName[localName] = importShare;
                    }
                };

                // A subset import (`import m::{a, b}`) must splice only the requested
                // names plus the dependency closure. `requestedNames` is erased as each
                // requested name is satisfied, so capture whether this was a subset
                // import once here rather than testing the now-mutated set mid-loop.
                const bool isSubsetImport = !requestedNames.empty();
                for (auto& importedStmt : importedRecord.module->body) {
                    if (isMainFunction(importedStmt)) {
                        throw std::runtime_error("Imported module must not define main(): " + importedRecord.sourcePath.string());
                    }

                    // Binds carry impl methods for (target, aspect) pairs. They have no
                    // symbol name to alias, so they are handled distinctly: carried when the
                    // module is imported whole, the requested aspect is in the specifier
                    // list, or the dependency closure resolved a method call to this bind,
                    // without consuming a requested-name slot (the aspect declaration
                    // itself must still be imported).
                    if (auto* bind = dynamic_cast<ast::BindDeclaration*>(importedStmt.get())) {
                        std::string bindKey = declarationName(importedStmt);
                        if (bindKey.empty()) {
                            continue;
                        }
                        std::string bindTrait = bind->traitType ? bind->traitType->toString() : "";

                        const bool bindRequested = requestedForBinds.empty() ||
                            (!bindTrait.empty() && requestedForBinds.find(bindTrait) != requestedForBinds.end());
                        const bool bindClosureCarried = isSubsetImport && carryBindKeys.count(bindKey);
                        if (!bindRequested && !bindClosureCarried) {
                            continue;
                        }
                        if (!declarationVisible(bindKey, importedRecord, metadata.bundles, importDecl)) {
                            continue;
                        }
                        if (seenNames.find(bindKey) != seenNames.end()) {
                            if (bindClosureCarried ||
                                seenOrigins[bindKey] == declaringOrigin(bindKey)) {
                                // The same bind already reached this module through
                                // another import path (e.g. a core auto-import), or the
                                // same origin bind was deep-cloned via a shared dependency;
                                // either way re-splicing it is harmless.
                                continue;
                            }
                            throw std::runtime_error("Duplicate bind after splice: '" + bindKey +
                                                     "' while importing '" + importPath.importSpelling + "' from " +
                                                     importPath.importerFile + ":" + std::to_string(importPath.line));
                        }
                        seenNames.insert(bindKey);
                        seenOrigins[bindKey] = declaringOrigin(bindKey);
                        carryShare(bindKey, bindKey);
                        resolvedBody.push_back(cloneSplicedStmt(importedStmt));
                        continue;
                    }

                    std::string name = declarationName(importedStmt);
                    if (name.empty()) {
                        continue;
                    }
                    const std::string originName = name;

                    if (isSubsetImport && carryNames.find(name) == carryNames.end()) {
                        continue;
                    }

                    if (!declarationVisible(name, importedRecord, metadata.bundles, importDecl)) {
                        continue;
                    }

                    // Deep-copy the statement first so the origin module's body stays
                    // intact. Renames and namespace mangling below apply only to the
                    // copy, letting the same origin declaration satisfy each consumer
                    // unchanged instead of being moved into the very first importer.
                    ast::StmtPtr copyStmt = cloneSplicedStmt(importedStmt);

                    if (!requestedNames.empty()) {
                        requestedNames.erase(name);
                        auto renameIt = requestedRenames.find(name);
                        if (renameIt != requestedRenames.end() && renameIt->second != name) {
                            if (!renameDeclaration(copyStmt, renameIt->second)) {
                                throw std::runtime_error("Cannot alias imported declaration '" + name +
                                                         "' from " + importedRecord.sourcePath.string());
                            }
                            name = renameIt->second;
                        }
                    }

                    if (seenNames.find(name) != seenNames.end()) {
                        // Same origin declaration deep-cloned through a shared dependency
                        // (e.g. core::aspects surfaced via this module's own core
                        // auto-import and again via a whole-imported consumer that also
                        // carried it), or an explicitly-requested symbol already made
                        // available by a whole import of a module that re-exports it:
                        // skip rather than treat as a real conflict. The requested name
                        // was already erased above, so the missing-import check is happy.
                        if (seenOrigins.count(name) &&
                            seenOrigins[name] == declaringOrigin(originName)) {
                            continue;
                        }
                        const bool explicitlyRequested = requestedRenames.count(originName) > 0;
                        if (isSubsetImport && !explicitlyRequested) {
                            // A dependency-closure carry that is already present in
                            // this module (via a core auto-import or a sibling import
                            // path) is a duplicate, but skipping it is safe: the
                            // machinery the carried declaration depended on is already
                            // here, so re-splicing it would only collide.
                            continue;
                        }
                        throw std::runtime_error("Duplicate symbol after splice: '" + name +
                                                 "' while importing '" + importPath.importSpelling + "' from " +
                                                 importPath.importerFile + ":" + std::to_string(importPath.line));
                    }

                    if (isNamespaceImport) {
                        std::string mangled = mangledNamespaceName(nsName, originName);
                        if (!renameDeclaration(copyStmt, mangled)) {
                            throw std::runtime_error("Cannot mangle namespace import '" + originName +
                                                     "' from " + importedRecord.sourcePath.string());
                        }
                        // Re-map the carried declaration's own cross-references to the
                        // mangled names so its internal calls/reads stay consistent.
                        mangleBareStmt(copyStmt, namespaceMangled);
                        if (seenNames.find(mangled) != seenNames.end()) {
                            throw std::runtime_error("Duplicate symbol after splice: '" + mangled +
                                                     "' while importing '" + importPath.importSpelling + "' from " +
                                                     importPath.importerFile + ":" + std::to_string(importPath.line));
                        }
                        seenNames.insert(mangled);
                        seenOrigins[mangled] = declaringOrigin(originName);
                        // The mangled symbol keeps share visibility from its origin so
                        // it can follow an exported function onto the next import hop
                        // (the mangled name is not user-writeable, so this never
                        // exposes the plain symbol unqualified).
                        carryShare(originName, mangled);
                        namespaceMangled[originName] = mangled;
                        resolvedBody.push_back(std::move(copyStmt));
                        continue;
                    }

                    seenNames.insert(name);
                    seenOrigins[name] = declaringOrigin(originName);
                    carryShare(originName, name);
                    resolvedBody.push_back(std::move(copyStmt));
                }

                if (!requestedNames.empty()) {
                    auto missing = *requestedNames.begin();
                    throw std::runtime_error("Imported symbol '" + missing + "' is not exported by " +
                                             importedRecord.sourcePath.string() + " (from '" +
                                             importPath.importSpelling + "' at " + importPath.importerFile + ":" +
                                             std::to_string(importPath.line) + ")");
                }

                if (isNamespaceImport) {
                    localNamespaces[nsName] = std::move(namespaceMangled);
                }

                continue;
                }
            }

            std::string name = declarationName(stmt);
            if (!name.empty()) {
                seenNames.insert(name);
                seenOrigins[name] = currentKey;
            }
            ownStmtIndices.push_back(resolvedBody.size());
            resolvedBody.push_back(std::move(stmt));
        }

        // Register this module's namespace bindings and rewrite its own qualified
        // `NS.sym` references to the plain symbols they name. Declarations spliced
        // in from other modules are not rewritten here - each module rewrote its
        // own references during its own resolution.
        for (const auto& nsPair : localNamespaces) {
            namespaces_[currentKey][nsPair.first] = nsPair.second;
        }
        if (!localNamespaces.empty()) {
            for (size_t idx : ownStmtIndices) {
                if (idx < resolvedBody.size() && resolvedBody[idx]) {
                    rewriteNamespaceRawStmt(resolvedBody[idx].get(), localNamespaces);
                }
            }
        }

        // Every name present in the module's (now fully spliced) scope - its own
        // declarations and everything it pulled in - so `smuggle <module>` can
        // grant the whole scope, bypassing export checks.
        auto& moduleAll = allNames_[currentKey];
        moduleAll.clear();
        for (auto& stmt : resolvedBody) {
            std::string name = declarationName(stmt);
            if (!name.empty()) {
                moduleAll.insert(name);
            }
        }

        module->body = std::move(resolvedBody);
        currentRecord.module = std::move(module);
        currentRecord.state = ModuleState::Resolved;
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

    if (importDecl->defaultImport) {
        throw std::runtime_error("Unsupported import form: default imports are not yet supported (" +
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

        // Kernel mode (issue #198): device modules must be self-contained pure
        // value/data-parallel code. The stdlib core::aspects auto-import (which
        // pre-wires host-sided Display/hash binds for the scalar types) would pull
        // host-runtime __vyb_* symbols into the NVPTX module, so `--kernel` implies
        // `no_core()` unless the module explicitly imports what it needs.
        if (vyb::g_kernel_mode) {
            metadata.coreAutoImport = false;
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
