//
// Created by ward on 3/6/20.
//

#include "ast.h"
#include <iostream>
#include <functional>
#include <fstream>

namespace Ast
{
std::ofstream& operator<<(std::ofstream& stream, const std::unique_ptr<Node>& root)
{
    std::function<void(Node*)> recursion = [&](Node* node)
        {
        stream << '"' << node << "\"[label=\"" << node->name() << "\\n"
               << node->value() << "\"];\n";
        for (const auto child : node->children())
        {
            stream << '"' << node << "\" -> \"" << child << "\";\n";
            recursion(child);
        }
    };
    recursion(root.get());
    return stream;
}

std::string Expr::color() const
{
    return "#98fb98";
}

std::string Statement::color() const
{
    return " #5dade2";
}

std::string Block::name() const
{
    return "file";
}
std::string Block::value() const
{
    return "";
}
std::vector<Node*> Block::children() const
{
    std::vector<Node*> result(expressions.size());
    std::copy(expressions.begin(), expressions.end(), result.begin());
    return result;
}
std::string Block::color() const
{
    return "#f08080";
}

std::string BinaryExpr::name() const
{
    return "binary expression";
}
std::string BinaryExpr::value() const
{
    return operation;
}
std::vector<Node*> BinaryExpr::children() const
{
    return {lhs, rhs};
}

std::string UnaryExpr::name() const
{
    return "unary expression";
}
std::string UnaryExpr::value() const
{
    return operation;
}
std::vector<Node*> UnaryExpr::children() const
{
    return {operand};
}

std::string Literal::name() const
{
    return "literal";
}
std::string Literal::value() const
{
    return std::visit([&](const auto& val){ return std::to_string(val); }, literal);
}
std::vector<Node*> Literal::children() const
{
    return {};
}

std::string Variable::name() const
{
    return "variable";
}
std::string Variable::value() const
{
    return identifier;
}
std::vector<Node*> Variable::children() const
{
    return {};
}

std::string Pointer::name() const
{
    return "pointer";
}
std::string Pointer::value() const
{
    return "";
}
std::vector<Node*> Pointer::children() const
{
    return {};
}

std::string Assignment::name() const
{
    return "assignment";
}
std::string Assignment::value() const
{
    return variable;
}
std::vector<Node*> Assignment::children() const
{
    return {expr};
}

std::string Declaration::name() const
{
    return "declaration";
}
std::string Declaration::value() const
{
    return "";
}
std::vector<Node*> Declaration::children() const
{
    if(expr) return {assignee, expr};
    else return {assignee};
}


}