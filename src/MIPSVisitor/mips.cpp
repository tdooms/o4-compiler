//============================================================================
// @author      : Thomas Dooms
// @date        : 5/10/20
// @copyright   : BA2 Informatica - Thomas Dooms - University of Antwerp
//============================================================================

#include "mips.h"
#include "../errors.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Type.h>

namespace
{
std::string reg(uint num)
{
    return (num >= 32 ? "$f" : "$") + std::to_string(num);
}

template <typename Ptr>
std::string label(Ptr* ptr)
{
    return "$" + std::to_string(reinterpret_cast<size_t>(ptr)); // nobody can see this line
}

std::string label(llvm::Function* ptr)
{
    return ptr->getName();
}

std::string operation(std::string&& operation, std::string&& t1 = "", std::string&& t2 = "", std::string&& t3 = "")
{
    std::string res = (operation + ' ');
    if(not t1.empty()) res += t1 + ",";
    if(not t2.empty()) res += t2 + ",";
    if(not t3.empty()) res += t3 + ",";
    res.back() = '\n';
    return res;
}

bool isFloat(llvm::Value* value)
{
    return value->getType()->isPointerTy();
}


void assertSame(llvm::Value* val1, llvm::Value* val2)
{
    if(isFloat(val1) != isFloat(val2))
    {
        throw InternalError("types do not have same type class");
    }
}

void assertSame(llvm::Value* val1, llvm::Value* val2, llvm::Value* t3)
{
    if(isFloat(val1) != isFloat(val2) or isFloat(val1) != isFloat(t3))
    {
        throw InternalError("types do not have same type class");
    }
}

void assertInt(llvm::Value* value)
{
    if(isFloat(value))
    {
        throw InternalError("type must be integer");
    }
}

void assertFloat(llvm::Value* value)
{
    if(not isFloat(value))
    {
        throw InternalError("type must be float");
    }
}

} // namespace

namespace mips
{

RegisterMapper::RegisterMapper(Module* module, llvm::Function* function)
: module(module), function(function)
{
    emptyRegisters[0].resize(end[0] - start[0]);
    std::iota(emptyRegisters[0].begin(), emptyRegisters[0].end(), start[0]);

    emptyRegisters[1].resize(end[1] - start[1]);
    std::iota(emptyRegisters[1].begin(), emptyRegisters[1].end(), start[1]);

    savedRegisters[0] = std::vector<uint>(32, std::numeric_limits<uint>::max());
    savedRegisters[1] = std::vector<uint>(32, std::numeric_limits<uint>::max());

    registerValues[0] = std::vector<llvm::Value*>(32, nullptr);
    registerValues[1] = std::vector<llvm::Value*>(32, nullptr);

    for(auto& arg : function->args())
    {
        const auto fl = isFloat(&arg);
        addressDescriptors[fl].emplace(&arg, stackSize);
        stackSize += 4;
    }
}

uint RegisterMapper::loadValue(std::string& output, llvm::Value* id)
{
    const auto fl = isFloat(id);
    const auto result = [&](auto r) { return r + 32 * fl; };

    // try to place constant value into temp register and be done with it
    const auto tmp = getTempRegister(fl);
    if(placeConstant(output, tmp, id))
    {
        return tmp;
    }

    // we try to find if it is stored in a register already
    if(const auto iter = registerDescriptors[fl].find(id); iter == registerDescriptors[fl].end())
    {
        // we find a suitable register, either by finding an empty one or spilling another one
        auto index = -1;
        if(emptyRegisters[fl].empty())
        {
            index = getNextSpill(fl);
        }
        else
        {
            index = emptyRegisters[fl].back();
            emptyRegisters[fl].pop_back();

            savedRegisters[fl][index] = stackSize;
            output += operation(fl ? "swc1" : "sw", reg(result(index)), std::to_string(stackSize) + "($sp)");

            stackSize += 4;
        }

        // spill if nescessary
        storeRegister(output, index, fl);

        // place it in the desired register
        const auto found = placeValue(output, index, id);

        // look in the alloca table
        if(not fl and not found)
        {
            const auto address = pointerDescriptors[fl].find(id);
            output += operation("la", reg(index), std::to_string(address->second) + "($sp)");
        }

        return result(index);
    }
    else
    {
        return result(iter->second);
    }
}

void RegisterMapper::loadSaved(std::string& output)
{
    for(size_t i = 0; i < savedRegisters[0].size(); i++)
    {
        if(savedRegisters[0][i] == std::numeric_limits<uint>::max()) continue;
        output += operation("lw", reg(i), std::to_string(savedRegisters[0][i]) + "($sp)");
    }

    for(size_t i = 0; i < savedRegisters[1].size(); i++)
    {
        if(savedRegisters[1][i] == std::numeric_limits<uint>::max()) continue;
        output += operation("lwc1", reg(i + 32), std::to_string(savedRegisters[1][i]) + "($sp)");
    }
}

bool RegisterMapper::placeConstant(std::string& output, uint index, llvm::Value* id)
{
    if(const auto& constant = llvm::dyn_cast<llvm::ConstantInt>(id))
    {
        const auto immediate = int(constant->getSExtValue());

        output += operation("lui", reg(index), std::to_string(immediate & 0xffff0000u));
        output += operation("ori", reg(index), std::to_string(immediate & 0x0000ffffu));
        return true;
    }
    else if(const auto& constant = llvm::dyn_cast<llvm::ConstantFP>(id))
    {
        module->addFloat(constant);

        output += operation("l.s", reg(index + 32), label(id));
        return true;
    }
    else if(const auto& constant = llvm::dyn_cast<llvm::GlobalVariable>(id))
    {
        if(constant->getValueType()->isFloatTy())
        {
            output += operation("l.s", reg(index+32), label(id));
        }
        else
        {
            output += operation("lw", reg(index), label(id));
        }
    }
    return false;
}

bool RegisterMapper::placeValue(std::string& output, uint index, llvm::Value* id)
{
    const auto fl = isFloat(id);
    const auto addrIter = addressDescriptors[fl].find(id);

    // if it is already stored on the stack, we put that value in the register
    if(addrIter != addressDescriptors[fl].end())
    {
        if(fl)
        {
            const auto tempReg = getTempRegister(false);
            output += operation("lw", reg(tempReg), std::to_string(addrIter->second) + "($sp)");
            output += operation("mtc1", reg(tempReg), reg(index + 32));
        }
        else
        {
            output += operation("lw", reg(index), std::to_string(addrIter->second) + "($sp)");
        }

        addressDescriptors[fl].erase(addrIter);
        registerDescriptors[fl].emplace(id, index);
        return true;
    }
    return false;
}

uint RegisterMapper::getTempRegister(bool fl)
{
    if(not(temp[fl] == 0 or temp[fl] == 1))
    {
        throw InternalError("integer temp register has wrong value for some reason");
    }
    const auto tmp = temp[fl];
    temp[fl] = !temp[fl];
    return (!fl * 2) + tmp;
}

uint RegisterMapper::getNextSpill(bool fl)
{
    spill[fl]++;
    if(spill[fl] > end[fl])
    {
        spill[fl] = start[fl];
    }
    return spill[fl];
}

void RegisterMapper::storeValue(std::string& output, llvm::Value* id)
{
    const auto fl = isFloat(id);
    const auto iter = registerDescriptors[fl].find(id);

    if(iter != registerDescriptors[fl].end())
    {
        registerDescriptors[fl].erase(iter);
        output += operation("sw", reg(iter->second), std::to_string(stackSize) + "($sp)");
        stackSize += 4;

        emptyRegisters[fl].push_back(iter->second);
        registerDescriptors[fl].emplace(id, iter->second);
    }
}

void RegisterMapper::storeRegister(std::string& output, uint index, bool fl)
{
    if(registerValues[fl][index] != nullptr)
    {
        storeValue(output, registerValues[fl][index]);
    }
}

void RegisterMapper::storeParameters(std::string& output, const std::vector<llvm::Value*>& ids)
{
    auto offset = 0;
    for(auto id : ids)
    {
        const auto index = loadValue(output, id);
        output += operation("sw", reg(index), std::to_string(stackSize + offset) + "($sp)");
        offset += 4;
    }
}

void RegisterMapper::storeReturnValue(std::string& output, llvm::Value* value)
{
    const auto fl = isFloat(value);
    const auto index1 = loadValue(output, value);
    output += operation(fl ? "mov.s" : "move", reg(fl ? 32 : 2), reg(index1));
}

void RegisterMapper::allocateValue(std::string& output, llvm::Value* id, llvm::Type* type)
{
    const auto fl = isFloat(id);
    pointerDescriptors[fl].emplace(id, stackSize);
    stackSize += module->layout.getTypeAllocSize(type);
}

uint RegisterMapper::getSize() const noexcept
{
    return stackSize;
}

void Instruction::print(std::ostream& os)
{
    os << output;
}

RegisterMapper* Instruction::mapper()
{
    return block->function->getMapper();
}

Module * Instruction::module()
{
    return block->function->module;
}

Move::Move(Block* block, llvm::Value* t1, llvm::Value* t2) : Instruction(block)
{
    assertSame(t1, t2);

    const auto index1 = mapper()->loadValue(output, t1);
    const auto index2 = mapper()->loadValue(output, t2);

    output += operation(isFloat(t1) ? "mov.s" : "move", reg(index1), reg(index2));
}

Convert::Convert(Block* block, llvm::Value* t1, llvm::Value* t2) : Instruction(block)
{
    // converts t2 into t1
    const auto index1 = mapper()->loadValue(output, t1);
    const auto index2 = mapper()->loadValue(output, t2);

    if(isFloat(t2))
    {
        // float to int
        assertInt(t1);

        output += operation("cvt.s.w", reg(index2), reg(index2));
        output += operation("mfc1", reg(index1), reg(index2));
    }
    else
    {
        // int to float
        assertFloat(t1);

        output += operation("mtc1", reg(index2), reg(index1));
        output += operation("cvt.w.s", reg(index1), reg(index1));
    }
}

Load::Load(Block* block, llvm::Value* t1, llvm::Value* t2) : Instruction(block)
{
    const auto index1 = mapper()->loadValue(output, t1);

    if(llvm::isa<llvm::Constant>(t2))
    {
    }
    else if(isFloat(t1))
    {
        const auto index2 = mapper()->loadValue(output, t2);

        output += operation("lw", reg(mapper()->getTempRegister(false)), reg(index2));
        output += operation("mtc1", reg(mapper()->getTempRegister(true) + 32), reg(index1));
    }
    else
    {
        const auto index2 = mapper()->loadValue(output, t2);

        const bool isWord = module()->layout.getTypeAllocSize(t1->getType()) == 32;
        output += operation(isWord ? "lw" : "lb", reg(index1), reg(index2));
    }
}

Arithmetic::Arithmetic(Block* block, std::string type, llvm::Value* t1, llvm::Value* t2, llvm::Value* t3)
: Instruction(block)
{
    const auto index1 = mapper()->loadValue(output, t1);
    const auto index2 = mapper()->loadValue(output, t2);
    const auto index3 = mapper()->loadValue(output, t3);

    output += operation(std::move(type), reg(index1), reg(index2), reg(index3));
}

Modulo::Modulo(Block* block, llvm::Value* t1, llvm::Value* t2, llvm::Value* t3) : Instruction(block)
{
    const auto index1 = mapper()->loadValue(output, t1);
    const auto index2 = mapper()->loadValue(output, t2);
    const auto index3 = mapper()->loadValue(output, t3);

    output += operation("divu", reg(index2), reg(index3));
    output += operation("mfhi", reg(index1));
}

NotEquals::NotEquals(Block* block, llvm::Value* t1, llvm::Value* t2, llvm::Value* t3)
: Instruction(block)
{
    const auto index1 = mapper()->loadValue(output, t1);
    const auto index2 = mapper()->loadValue(output, t2);
    const auto index3 = mapper()->loadValue(output, t3);

    output += operation("c.eq.s", reg(index1), reg(index2), reg(index3));
    output += operation("cmp", reg(index1), reg(index1), reg(0));
}

Branch::Branch(Block* block, llvm::Value* t1, llvm::BasicBlock* target, bool eqZero)
: Instruction(block)
{
    const auto index1 = mapper()->loadValue(output, t1);

    output += operation(eqZero ? "beqz" : "bnez", reg(index1), label(target));
}

Call::Call(Block* block, llvm::Function* function, const std::vector<llvm::Value*>& arguments, llvm::Value* ret)
: Instruction(block)
{
    mapper()->storeParameters(output, arguments);
    if(mapper()->getSize() > 0)
    {
        output += operation("addi", "$sp", "$sp", std::to_string(-mapper()->getSize()));
    }
    output += operation("jal", label(function));
    if(mapper()->getSize() > 0)
    {
        output += operation("addi", "$sp", "$sp", std::to_string(mapper()->getSize()));
    }

    if(ret != nullptr)
    {
        mapper()->storeReturnValue(output, ret);
    }
}

Return::Return(Block* block, llvm::Value* value) : Instruction(block)
{
    if(block->function->getFunction()->getName() == "main")
    {
        const auto index = mapper()->loadValue(output, value);
        output += operation("move", reg(4), reg(index));
        output += operation("li", reg(2), std::to_string(17));
        output += operation("syscall");
    }
    else
    {
        if(value != nullptr)
        {
            mapper()->storeReturnValue(output, value);
            mapper()->loadSaved(output);
        }

        output += operation("jr", "$ra");
    }
}

Jump::Jump(Block* block, llvm::BasicBlock* target) : Instruction(block)
{
    output += operation("j", label(target));
}

Allocate::Allocate(Block* block, llvm::Value* t1, llvm::Type* type) : Instruction(block)
{
    mapper()->allocateValue(output, t1, type);
}

Store::Store(Block* block, llvm::Value* t1, llvm::Value* t2) : Instruction(block)
{
    const auto index1 = mapper()->loadValue(output, t1);
    const auto index2 = mapper()->loadValue(output, t2);

    if(isFloat(t1))
    {
        output += operation("s.s", reg(index1), reg(index2));
    }
    else
    {
        const auto isWord = module()->layout.getTypeAllocSize(t1->getType()) == 32;
        output += operation(isWord ? "sw" : "sb", reg(index1), reg(index2));
    }
}

void Block::append(Instruction* instruction)
{
    instructions.emplace_back(instruction);
}

void Block::appendBeforeLast(Instruction* instruction)
{
    instructions.emplace(instructions.end() - 2, instruction);
}

void Block::print(std::ostream& os) const
{
    os << label(block) << ":\n";
    for(const auto& instruction : instructions)
    {
        instruction->print(os);
    }
}

llvm::BasicBlock* Block::getBlock()
{
    return block;
}

void Function::append(Block* block)
{
    blocks.emplace_back(block);
}

void Function::print(std::ostream& os) const
{
    os << label(function) << ":\n";
    for(const auto& block : blocks)
    {
        block->print(os);
    }
}


RegisterMapper* Function::getMapper()
{
    return &mapper;
}

llvm::Function * Function::getFunction()
{
    return function;
}

Block* Function::getBlockByBasicBlock(llvm::BasicBlock* block)
{
    const auto pred = [&](const auto& ptr) { return ptr->getBlock() == block; };
    const auto iter = std::find_if(blocks.begin(), blocks.end(), pred);
    return (iter == blocks.end()) ? nullptr : iter->get();
}

void Module::append(Function* function)
{
    functions.emplace_back(function);
}

void Module::print(std::ostream& os) const
{
    os << ".data\n";
    for(auto variable : floats)
    {
        os << label(variable) << ": .float " << variable->getValueAPF().convertToFloat() << '\n';
    }
    for(auto variable : globals)
    {
        if(variable->getValueType()->isIntegerTy() or variable->getValueType()->isPointerTy())
        {
            os << label(variable) << ": .word ";
            if(const auto* tmp = llvm::dyn_cast<llvm::ConstantInt>(variable->getInitializer()))
            {
                os << tmp->getSExtValue() << '\n';
            }
        }
        else if(variable->getValueType()->isArrayTy()
                and variable->getValueType()->getContainedType(0)->isIntegerTy(8)
                and variable->hasInitializer())
        {
            os << label(variable) << ": .asciiz ";
            if(const auto* tmp = llvm::dyn_cast<llvm::ConstantDataArray>(variable->getInitializer()))
            {
                os << tmp->getRawDataValues().data() << '\n';
            }
            else
                throw InternalError("problem with llvm strings");
        }
        else
        {
            os << label(variable) << ": .space ";
            os << layout.getTypeAllocSize(variable->getValueType());
            os << "\n";
        }
    }

    os << ".text\n";
    os << "j main\n";
    for(const auto& function : functions)
    {
        function->print(os);
    }
}

void Module::addGlobal(llvm::GlobalVariable* variable)
{
    globals.emplace(variable);
}

void Module::addFloat(llvm::ConstantFP* variable)
{
    floats.emplace(variable);
}


} // namespace mips
