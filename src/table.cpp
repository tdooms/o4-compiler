//============================================================================
// @author      : Thomas Dooms
// @date        : 3/15/20
// @copyright   : BA2 Informatica - Thomas Dooms - University of Antwerp
//============================================================================

#include "table.h"

TableElement* SymbolTable::lookup(const std::string& id)
{
    const auto iter = table.find(id);
    if(iter == table.end())
    {
        if(parent) return parent->lookup(id);
        else
            return nullptr;
    }
    else
        return &iter->second;
}

llvm::Value** SymbolTable::lookupAllocaInst(const std::string& id)
{
    const auto iter = table.find(id);
    if(iter == table.end() or iter->second.allocaInst == nullptr)
    {
        if(parent) return parent->lookupAllocaInst(id);
        else
            return nullptr;
    }
    else
        return &iter->second.allocaInst;
}

void SymbolTable::setUsed(const std::string& id)
{
    const auto& iter = table.find(id);
    if(iter != table.end())
    {
        iter->second.isUsed = true;
    }
    if(parent)
    {
        parent->setUsed(id);
    }
}

bool SymbolTable::insert(const std::string& id, Type* type, bool initialized)
{
    return table.emplace(id, TableElement{ type, std::nullopt, initialized, false, false }).second;
}

std::shared_ptr<SymbolTable>& SymbolTable::getParent()
{
    return parent;
}

ScopeType SymbolTable::getType()
{
    return type;
}

bool SymbolTable::lookupType(ScopeType type)
{
    auto iter = this;
    while(iter != nullptr)
    {
        if(type == iter->type) return true;
        iter = iter->parent.get();
    }
    return false;
}