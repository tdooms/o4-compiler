//============================================================================
// @author      : Thomas Dooms
// @date        : 5/10/20
// @copyright   : BA2 Informatica - Thomas Dooms - University of Antwerp
//============================================================================

#pragma once

#include <iostream>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Value.h>

#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <string>
#include <vector>

namespace mips
{

class Block;
class Function;
class Module;

class RegisterMapper
{
    public:
    explicit RegisterMapper(Module* module, llvm::Function* function);

    uint loadValue(std::string& output, llvm::Value* id);
    void loadSaved(std::string& output);

    bool placeConstant(std::string& output, uint index, llvm::Value* id);
    bool placeValue(std::string& output, uint index, llvm::Value* id);

    uint getTempRegister(bool fl);
    uint getNextSpill(bool fl);

    void storeValue(std::string& output, llvm::Value* id);
    void storeRegister(std::string& output, uint index, bool fl);
    void storeParameters(std::string& output, const std::vector<llvm::Value*>& ids);

    void allocateValue(std::string& output, llvm::Value* id, llvm::Type* type);

    [[nodiscard]] uint getSize() const noexcept;

    private:
    Module* module;
    llvm::Function* function;

    std::array<std::vector<uint>, 2> emptyRegisters;
    std::array<std::vector<uint>, 2> savedRegisters;
    std::array<std::vector<llvm::Value*>, 2> registerValues;

    std::array<std::map<llvm::Value*, uint>, 2> registerDescriptors;
    std::array<std::map<llvm::Value*, uint>, 2> addressDescriptors;
    std::array<std::map<llvm::Value*, uint>, 2> pointerDescriptors;

    std::array<uint, 2> start = {4, 2};
    std::array<uint, 2> end = {26, 32};
    std::array<uint, 2> spill = {start[0], start[1]};
    std::array<uint, 2> temp = {0, 0};

    uint stackSize = 0;
};

class Instruction
{
    public:
    explicit Instruction(Block* block) : block(block)
    {
    }

    void print(std::ostream& os);

    RegisterMapper* mapper();

    protected:
    Block* block;
    std::string output;
};

// move
struct Move : public Instruction
{
    Move(Block* block, llvm::Value* t1, llvm::Value* t2);
};

struct Convert : public Instruction
{
    Convert(Block* block, llvm::Value* t1, llvm::Value* t2);
};

// lw, li, lb, l.s
struct Load : public Instruction
{
    Load(Block* block, llvm::Value* t1, llvm::Value* t2);
    Load(Block* block, llvm::Value* t1, llvm::GlobalVariable* variable);
};

// add, sub, mul
struct Arithmetic : public Instruction
{
    Arithmetic(Block* block, std::string type, llvm::Value* t1, llvm::Value* t2, llvm::Value* t3);
};

// modulo
struct Modulo : public Instruction
{
    Modulo(Block* block, llvm::Value* t1, llvm::Value* t2, llvm::Value* t3);
};

struct NotEquals : public Instruction
{
    NotEquals(Block* block, llvm::Value* t1, llvm::Value* t2, llvm::Value* t3);
};

struct Branch : public Instruction
{
    explicit Branch(Block* block, llvm::Value* t1, llvm::BasicBlock* target, bool eqZero);
};

// jal
struct Call : public Instruction
{
    explicit Call(Block* block, llvm::Function* function, const std::vector<llvm::Value*>& arguments);
};

struct Return : public Instruction
{
    explicit Return(Block* block);
};

// j
struct Jump : public Instruction
{
    explicit Jump(Block* block, llvm::BasicBlock* target);
};

struct Allocate : public Instruction
{
    Allocate(Block* block, llvm::Value* t1, llvm::Type* type);
};

// sw, sb
struct Store : public Instruction
{
    explicit Store(Block* block, llvm::Value* t1, llvm::Value* t2);
    explicit Store(Block* block, llvm::Value* t1, llvm::GlobalVariable* variable);
};

class Block
{
    friend class Instruction;

    public:
    explicit Block(Function* function, llvm::BasicBlock* block) : block(block), function(function)
    {
    }

    void append(Instruction* instruction);

    void appendBeforeLast(Instruction* instruction);

    void print(std::ostream& os) const;

    llvm::BasicBlock* getBlock();

    private:
    llvm::BasicBlock* block;
    std::vector<std::unique_ptr<Instruction>> instructions;
    Function* function;
};

class Function
{
    friend class Block;

    public:
    explicit Function(Module* module, llvm::Function* function) : function(function), mapper(module, function), module(module)
    {
    }

    void append(Block* block);

    void print(std::ostream& os) const;

    RegisterMapper* getMapper();

    Module* getModule();

    Block* getBlockByBasicBlock(llvm::BasicBlock* block);

    private:
    llvm::Function* function;
    std::vector<std::unique_ptr<Block>> blocks;

    RegisterMapper mapper;
    Module* module;
};

class Module
{
    public:
    explicit Module(const llvm::DataLayout& layout) : layout(layout)
    {
    }

    void append(Function* function);

    void print(std::ostream& os) const;

    void addGlobal(llvm::GlobalVariable* variable);

    void addFloat(llvm::ConstantFP* variable);

    llvm::DataLayout layout;

    private:
    std::vector<std::unique_ptr<Function>> functions;
    std::set<llvm::GlobalVariable*> globals;
    std::set<llvm::ConstantFP*> floats;
};


} // namespace mips