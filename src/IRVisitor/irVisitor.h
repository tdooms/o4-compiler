//
// Created by ward on 3/28/20.
//

#ifndef COMPILER_IRVISITOR_H
#define COMPILER_IRVISITOR_H

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#include <filesystem>
#include <ast/statements.h>
#include <llvm/IR/NoFolder.h>
#include <llvm/Pass.h>

#include "ast/expressions.h"
#include "ast/node.h"

class IRVisitor {
public:
	explicit IRVisitor(const std::filesystem::path& input);

	void convertAST(const std::unique_ptr<Ast::Node>& root);

	void LLVMOptimize(int level);

	void print(const std::filesystem::path& output);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void visitLiteral(const Ast::Literal& literal);

	void visitStringLiteral(const Ast::StringLiteral& stringLiteral);

	void visitComment(const Ast::Comment& comment);

	void visitVariable(const Ast::Variable& variable);

	void visitScope(const Ast::Scope& scope);

	void visitBinaryExpr(const Ast::BinaryExpr& binaryExpr);

	void visitPostfixExpr(const Ast::PostfixExpr& postfixExpr);

	void visitPrefixExpr(const Ast::PrefixExpr& prefixExpr);

	void visitCastExpr(const Ast::CastExpr& castExpr);

	void visitAssignment(const Ast::Assignment& assignment);

	void visitDeclaration(const Ast::VariableDeclaration& declaration);

	void visitIfStatement(const Ast::IfStatement& ifStatement);

	void visitLoopStatement(const Ast::LoopStatement& loopStatement);

	void visitControlStatement(const Ast::ControlStatement& controlStatement);

	void visitReturnStatement(const Ast::ReturnStatement& returnStatement);

	void visitFunctionDefinition(const Ast::FunctionDefinition& functionDefinition);

	void visitFunctionCall(const Ast::FunctionCall& functionCall);

	void visitSubscriptExpr(const Ast::SubscriptExpr& subscriptExpr);

	void visitIncludeStdioStatement(const Ast::IncludeStdioStatement& includeStdioStatement);

	void visitFunctionDeclaration(const Ast::FunctionDeclaration& functionDeclaration);

	llvm::Module& getModule();

private:
	llvm::LLVMContext context;
	llvm::Module module;
	llvm::IRBuilder<> builder;

	llvm::Value* ret{};
	llvm::BasicBlock* breakBlock{};
	llvm::BasicBlock* continueBlock{};
	bool isRvalue = true;

	llvm::Value* cast(llvm::Value* value, llvm::Type* to);

	llvm::Value* increaseOrDecrease(bool inc, llvm::Value* input);

	llvm::Type* convertToIR(Type* type, bool function = false, bool first = true);

	llvm::AllocaInst* createAlloca(llvm::Type* type, const std::string& name);

	llvm::Value* LRValue(Ast::Node* ASTValue, bool requiresRvalue, llvm::Value* inc = nullptr);

	llvm::Function* getOrCreateFunction(const std::string& identifier, std::shared_ptr<SymbolTable> table);
};

#endif //COMPILER_IRVISITOR_H
