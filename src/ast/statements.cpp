//============================================================================
// @author      : Thomas Dooms
// @date        : 3/21/20
// @copyright   : BA2 Informatica - Thomas Dooms - University of Antwerp
//============================================================================

#include "statements.h"
#include "IRVisitor/irVisitor.h"
#include "helper.h"
#include <numeric>

namespace Ast
{
std::string Scope::name() const
{
    return "block";
}

std::vector<Node*> Scope::children() const
{
    return std::vector<Node*>(statements.begin(), statements.end());
}

std::string Scope::color() const
{
    return "#ceebe3"; // light green
}
Node* Scope::fold()
{
    // this removes the statements after a return fucntion
    if(table->getType() == ScopeType::function)
    {
        for(size_t i = 0; i < statements.size(); i++)
        {
            if(dynamic_cast<ReturnStatement*>(statements[i]))
            {
                statements.resize(i + 1);
                break;
            }
        }
    }

    Helper::fold_children(statements);
    return this;
}

void Scope::visit(IRVisitor& visitor)
{
    visitor.visitScope(*this);
}

std::string Statement::color() const
{
    return " #ebcee5"; // light orange/pink
}

std::string VariableDeclaration::name() const
{
    return "variable declaration";
}

std::string VariableDeclaration::value() const
{
    return table->lookup(identifier)->type->string(identifier);
}

std::vector<Node*> VariableDeclaration::children() const
{
    if(expr) return { expr };
    else
        return {};
}

Node* VariableDeclaration::fold()
{
    const auto& entry = table->lookup(identifier);
    if(not entry->isUsed) return nullptr;

    Helper::folder(expr);
    if(auto* res = dynamic_cast<Ast::Literal*>(expr))
    {
        if(entry->type->isConst())
        {
            entry->literal = res->literal;
            if(not entry->isDerefed) return nullptr;
        }
    }

    // precast if it is constant
    if(expr and expr->constant())
    {
        const auto lambda
        = [&](const auto& val) { return Helper::fold_cast(val, type, table, line, column); };

        if(auto* res = dynamic_cast<Literal*>(expr))
        {
            expr = std::visit(lambda, res->literal);
        }
    }

    return this;
}

bool VariableDeclaration::fill() const
{
    if(not table->insert(identifier, type, expr or type->isArrayType()))
    {
        if(table->getType() == ScopeType::global)
        {
            const auto& entry = table->lookup(identifier);
            if(entry->isInitialized)
            {
                std::cout
                << SemanticError("redefinition of already defined variable in global scope", line, column);
                return false;
            }
            else
            {
                entry->isInitialized |= static_cast<bool>(expr);
            }

            if(*entry->type != *type)
            {
                std::cout
                << SemanticError("redefinition of variable with different type in global scope", line, column);
                return false;
            }
        }
        else
        {
            std::cout << RedefinitionError(identifier, line, column);
            return false;
        }
    }
    return true;
}

bool VariableDeclaration::check() const
{
    if(type->isVoidType())
    {
        std::cout << SemanticError("type declaration cannot have void type");
        return false;
    }

    if(expr)
    {
        if(table->getType() == ScopeType::global and not expr->constant())
        {
            std::cout << NonConstantGlobal(identifier, line, column);
            return false;
        }
        return Type::convert(expr->type(), type, false, line, column);
    }
    else
    {
        return true;
    }
}

std::string FunctionDefinition::name() const
{
    return "function declaration";
}

std::string FunctionDefinition::value() const
{
    return table->lookup(identifier)->type->string();
}

std::vector<Node*> FunctionDefinition::children() const
{
    return { body };
}

Node* FunctionDefinition::fold()
{
    const auto pred = [](auto& elem) { return dynamic_cast<ReturnStatement*>(elem); };
    Helper::remove_dead(body->statements, pred);

    Helper::folder(body);
    return this;
}

bool FunctionDefinition::fill() const
{
    auto res = Helper::fill_table_with_function(parameters, returnType, identifier, table,
                                                body->table, line, column);
    if(res)
    {
        const auto& entry = table->lookup(identifier);
        if(entry->isInitialized)
        {
            std::cout << SemanticError("function already defined before", line, column);
            return false;
        }
        entry->isInitialized = true;
    }
    return res;
}

bool FunctionDefinition::check() const
{
    bool                       found  = false;
    bool                       worked = true;
    std::function<void(Node*)> func   = [&](auto* root) {
        if(auto* res = dynamic_cast<ReturnStatement*>(root))
        {
            found     = true;
            auto type = (res->expr) ? res->expr->type() : new Type;
            worked &= Type::convert(type, returnType, false, res->line, res->column, true);
        }
        for(auto* child : root->children())
        {
            func(child);
        }
    };
    func(body);
    if(not worked)
    {
        return false;
    }

    if(returnType->isVoidType() or identifier == "main")
    {
    }
    else if(not found)
    {
        std::cout << SemanticError("no return statement in nonvoid function", line, column, true);
    }
    return true;
}

void FunctionDefinition::visit(IRVisitor& visitor)
{
    visitor.visitFunctionDefinition(*this);
}

std::string FunctionDeclaration::name() const
{
    return "function declaration";
}

std::string FunctionDeclaration::value() const
{
    return table->lookup(identifier)->type->string();
}

Node* FunctionDeclaration::fold()
{
    return this;
}

bool FunctionDeclaration::fill() const
{
    return Helper::fill_table_with_function(parameters, returnType, identifier, table,
                                            std::make_shared<SymbolTable>(ScopeType::plain, table),
                                            line, column);
}

void FunctionDeclaration::visit(IRVisitor& visitor)
{
    visitor.visitFunctionDeclaration(*this);
}

void VariableDeclaration::visit(IRVisitor& visitor)
{
    visitor.visitDeclaration(*this);
}

std::string LoopStatement::name() const
{
    if(doWhile) return "do while";
    else
        return "loop";
}

std::vector<Node*> LoopStatement::children() const
{
    auto res = std::vector<Node*>(init.begin(), init.end());
    if(condition) res.emplace_back(condition);
    if(iteration) res.emplace_back(iteration);
    res.emplace_back(body);
    return res;
}

Node* LoopStatement::fold()
{
    // removes the dead code after continue or breaks
    if(auto* res = dynamic_cast<Scope*>(body))
    {
        const auto pred = [](auto& elem) { return dynamic_cast<ControlStatement*>(elem); };
        Helper::remove_dead(res->statements, pred);
    }

    Helper::fold_children(init);
    Helper::folder(condition);
    Helper::folder(iteration);

    if(condition->constant())
    {
        if(auto* res = dynamic_cast<Ast::Literal*>(condition))
        {
            if(not Helper::evaluate(res))
            {
                return nullptr;
            }
        }
    }

    return this;
}

void LoopStatement::visit(IRVisitor& visitor)
{
    visitor.visitLoopStatement(*this);
}

std::string IfStatement::name() const
{
    return "if";
}

std::vector<Node*> IfStatement::children() const
{
    std::vector<Node*> res;
    if(condition) res.emplace_back(condition);
    if(ifBody) res.emplace_back(ifBody);
    if(elseBody) res.emplace_back(elseBody);
    return res;
}

Node* IfStatement::fold()
{
    Helper::folder(condition);
    Helper::folder(ifBody);
    Helper::folder(elseBody);

    if(condition->constant())
    {
        if(auto* res = dynamic_cast<Ast::Literal*>(condition))
        {
            if(Helper::evaluate(res))
            {
                return ifBody;
            }
            else if(elseBody)
            {
                return elseBody;
            }
            else
            {
                return nullptr;
            }
        }
    }
    return this;
}

void IfStatement::visit(IRVisitor& visitor)
{
    visitor.visitIfStatement(*this);
}

std::string ControlStatement::name() const
{
    return type;
}

bool ControlStatement::check() const
{
    if(table->lookupType(ScopeType::loop))
    {
        return true;
    }
    else
    {
        std::cout << SemanticError(type + " statement is not in a loop", line, column);
        return false;
    }
}

Node* ControlStatement::fold()
{
    return this;
}

void ControlStatement::visit(IRVisitor& visitor)
{
    visitor.visitControlStatement(*this);
}

std::string ReturnStatement::name() const
{
    return "return";
}

std::vector<Node*> ReturnStatement::children() const
{
    if(expr) return { expr };
    else
        return {};
}

bool ReturnStatement::check() const
{
    if(table->lookupType(ScopeType::function))
    {
        return true;
    }
    else
    {
        std::cout << SemanticError("return statement is not in a loop", line, column);
        return false;
    }
}

Node* ReturnStatement::fold()
{
    Helper::folder(expr);
    return this;
}

void ReturnStatement::visit(IRVisitor& visitor)
{
    visitor.visitReturnStatement(*this);
}

std::string IncludeStdioStatement::name() const
{
    return "#include <stdio.h>";
}

std::vector<Node*> IncludeStdioStatement::children() const
{
    return {};
}

bool IncludeStdioStatement::check() const
{
    table->lookup("printf")->isInitialized = true;
    table->lookup("scanf")->isInitialized  = true;

    return true;
}

bool IncludeStdioStatement::fill() const
{
    auto returnType = new Type(false, BaseType::Int);
    auto strType    = new Type(true, new Type(false, BaseType::Char));
    auto funcType   = new Type(returnType, { strType }, true);

    if(not table->insert("printf", funcType, false))
    {
        std::cout << SemanticError(
        "cannot include stdio.h: printf already declared with a different signature", line, column);
        return false;
    }
    if(not table->insert("scanf", funcType, false))
    {
        std::cout << SemanticError(
        "cannot include stdio.h: scanf already declared with a different signature", line, column);
        return false;
    }

    return true;
}

Node* IncludeStdioStatement::fold()
{
    return this;
}

void IncludeStdioStatement::visit(IRVisitor& visitor)
{
    visitor.visitIncludeStdioStatement(*this);
}

} // namespace Ast