//
// Created by ward on 5/7/20.
//

#include "mipsVisitor.h"
#include <fstream>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Target/TargetLoweringObjectFile.h>
#include <llvm/IR/Constant.h>
#include "../errors.h"

using namespace llvm;
using namespace mips;

MIPSVisitor::MIPSVisitor(const llvm::Module& module)
		:module(module.getDataLayout()) { }

void MIPSVisitor::convertIR(llvm::Module& module)
{
	visit(module);
}

void MIPSVisitor::print(const std::filesystem::path& output)
{
	std::ofstream stream(output);
	module.print(stream);
	stream.close();
}

void MIPSVisitor::visitModule(llvm::Module& M)
{
	if (M.getFunction("printf") || M.getFunction("scanf"))
		module.includeStdio(M.getFunction("printf"), M.getFunction("scanf"));
	for (auto& global: M.globals()) {
		try {
			Constant * ini = global.getInitializer();
			while (const auto& c = dyn_cast_or_null<ConstantExpr>(ini)) {
				ini = c->getOperand(0);
			}
			global.setInitializer(ini);
		}
		catch (...) {
			throw InternalError("MIPS doesn't allow complex initializers for global variables");
		}
		module.addGlobal(&global);
	}
}

void MIPSVisitor::visitFunction(llvm::Function& F)
{
	if (F.isDeclaration()) return;
	currentFunction = new mips::Function(&module, &F);
	module.append(currentFunction);
}

void MIPSVisitor::visitBasicBlock(BasicBlock& BB)
{
	currentBlock = new mips::Block(currentFunction, &BB);
	currentFunction->append(currentBlock);
}

void MIPSVisitor::visitCmpInst(CmpInst& I)
{
	const auto& a = &I;
	const auto& b = processOperand(I.getOperand(0));
	const auto& c = processOperand(I.getOperand(1));
	mips::Instruction* instruction;
	switch (I.getPredicate()) {
	case CmpInst::FCMP_OEQ:
	case CmpInst::FCMP_UEQ:
		instruction = new mips::Arithmetic(currentBlock, "c.eq.s", a, b, c);
		break;
	case CmpInst::FCMP_OGT:
	case CmpInst::FCMP_UGT:
		instruction = new mips::Arithmetic(currentBlock, "c.lt.s", a, c, b);
		break;
	case CmpInst::FCMP_OGE:
	case CmpInst::FCMP_UGE:
		instruction = new mips::Arithmetic(currentBlock, "c.le.s", a, c, b);
		break;
	case CmpInst::FCMP_OLT:
	case CmpInst::FCMP_ULT:
		instruction = new mips::Arithmetic(currentBlock, "c.lt.s", a, b, c);
		break;
	case CmpInst::FCMP_OLE:
	case CmpInst::FCMP_ULE:
		instruction = new mips::Arithmetic(currentBlock, "c.le.s", a, b, c);
		break;
	case CmpInst::FCMP_ONE:
	case CmpInst::FCMP_UNE:
		instruction = new mips::NotEquals(currentBlock, a, b, c);
		break;
	case CmpInst::ICMP_EQ:
		instruction = new mips::Arithmetic(currentBlock, "seq", a, b, c);
		break;
	case CmpInst::ICMP_NE:
		instruction = new mips::Arithmetic(currentBlock, "sne", a, b, c);
		break;
	case CmpInst::ICMP_UGT:
		instruction = new mips::Arithmetic(currentBlock, "sgtu", a, b, c);
		break;
	case CmpInst::ICMP_UGE:
		instruction = new mips::Arithmetic(currentBlock, "sgeu", a, b, c);
		break;
	case CmpInst::ICMP_ULT:
		instruction = new mips::Arithmetic(currentBlock, "sleu", a, b, c);
		break;
	case CmpInst::ICMP_ULE:
		instruction = new mips::Arithmetic(currentBlock, "slue", a, b, c);
		break;
	case CmpInst::ICMP_SGT:
		instruction = new mips::Arithmetic(currentBlock, "sgt", a, b, c);
		break;
	case CmpInst::ICMP_SGE:
		instruction = new mips::Arithmetic(currentBlock, "sge", a, b, c);
		break;
	case CmpInst::ICMP_SLT:
		instruction = new mips::Arithmetic(currentBlock, "slt", a, b, c);
		break;
	case CmpInst::ICMP_SLE:
		instruction = new mips::Arithmetic(currentBlock, "sle", a, b, c);
		break;
	default:
		instruction = nullptr;
		InstVisitor::visitCmpInst(I);
		break;
	}
	currentBlock->append(instruction);
}

void MIPSVisitor::visitLoadInst(LoadInst& I)
{
	currentBlock->append(new mips::Load(currentBlock, &I, processOperand(I.getPointerOperand())));
}

void MIPSVisitor::visitAllocaInst(AllocaInst& I)
{
	currentBlock->append(new mips::Allocate(currentBlock, &I, I.getAllocatedType()));
}

void MIPSVisitor::visitStoreInst(StoreInst& I)
{
	currentBlock->append(
			new mips::Store(currentBlock, processOperand(I.getValueOperand()), processOperand(I.getPointerOperand())));
}

void MIPSVisitor::visitGetElementPtrInst(GetElementPtrInst& I)
{
	const auto& base = processOperand(I.getPointerOperand());
	APInt a(32, 0);
	if (I.hasAllZeroIndices()) {
		currentBlock->append(new mips::Move(currentBlock, &I, base));
	}
	else if (I.accumulateConstantOffset(module.layout, a)) {
		currentBlock->append(new mips::Arithmetic(currentBlock, "addu", &I, base,
				Constant::getIntegerValue(IntegerType::getInt32Ty(I.getContext()), a)));
	}
	else {
		llvm::Type* currentType = I.getPointerOperandType();
		for (const auto& index: I.indices()) {
			const auto i = processOperand(index);
			currentType = currentType->getContainedType(0);
			const auto size = module.layout.getTypeAllocSize(currentType);
			if (const auto& constant = dyn_cast<ConstantInt>(i)) {
				if (not constant->getZExtValue()) continue;
				currentBlock->append(new mips::Arithmetic(currentBlock, "addu", &I, base,
						ConstantInt::get(IntegerType::getInt32Ty(I.getContext()), size*constant->getZExtValue())));
				continue;
			}
			const auto& mul = BinaryOperator::Create(llvm::Instruction::Mul, i,
					ConstantInt::get(IntegerType::getInt32Ty(I.getContext()), size));
			currentBlock->append(
					new mips::Arithmetic(currentBlock, "mulu", mul, processOperand(mul->getOperand(0)),
							processOperand(mul->getOperand(1))));
			currentBlock->append(new mips::Arithmetic(currentBlock, "addu", &I, base, mul));
		}
	}
}

void MIPSVisitor::visitPHINode(PHINode& I)
{//
//	const auto backup = currentBlock;
//	for (const auto& block: I.blocks()) {
//		const auto& mipsBlock = currentFunction->getBlockByBasicBlock(block);
//		currentBlock = mipsBlock;
//		const auto& value = processOperand(I.getIncomingValueForBlock(block));
//		mipsBlock->appendBeforeLast(new mips::Move(currentBlock, &I, value));
//	}
//	currentBlock = backup;
	InstVisitor::visitPHINode(I);
}

void MIPSVisitor::visitTruncInst(TruncInst& I)
{
	currentBlock->append(new mips::Move(currentBlock, &I, I.getOperand(0)));
}

void MIPSVisitor::visitZExtInst(ZExtInst& I)
{
	currentBlock->append(new mips::Move(currentBlock, &I, I.getOperand(0)));
}

void MIPSVisitor::visitSExtInst(SExtInst& I)
{
	currentBlock->append(new mips::Move(currentBlock, &I, I.getOperand(0)));
}

void MIPSVisitor::visitFPTruncInst(FPTruncInst& I)
{
	currentBlock->append(new mips::Move(currentBlock, &I, I.getOperand(0)));
}

void MIPSVisitor::visitFPExtInst(FPExtInst& I)
{
	currentBlock->append(new mips::Move(currentBlock, &I, I.getOperand(0)));
}

void MIPSVisitor::visitFPToUIInst(FPToUIInst& I)
{
	//cvt.w.s
	currentBlock->append(new mips::Convert(currentBlock, &I, processOperand(I.getOperand(0))));
}

void MIPSVisitor::visitFPToSIInst(FPToSIInst& I)
{
	//cvt.w.s
	currentBlock->append(new mips::Convert(currentBlock, &I, processOperand(I.getOperand(0))));
}

void MIPSVisitor::visitUIToFPInst(UIToFPInst& I)
{
	//cvt.s.w
	currentBlock->append(new mips::Convert(currentBlock, &I, processOperand(I.getOperand(0))));
}

void MIPSVisitor::visitSIToFPInst(SIToFPInst& I)
{
	//cvt.w.s
	currentBlock->append(new mips::Convert(currentBlock, &I, processOperand(I.getOperand(0))));
}

void MIPSVisitor::visitPtrToIntInst(PtrToIntInst& I)
{
	currentBlock->append(new mips::Move(currentBlock, &I, I.getOperand(0)));
}

void MIPSVisitor::visitIntToPtrInst(IntToPtrInst& I)
{
	currentBlock->append(new mips::Move(currentBlock, &I, I.getOperand(0)));
}

void MIPSVisitor::visitBitCastInst(BitCastInst& I)
{
	currentBlock->append(new mips::Move(currentBlock, &I, I.getOperand(0)));
}

void MIPSVisitor::visitCallInst(CallInst& I)
{
	std::vector<Value*> args;
	for (const auto& arg: I.args()) {
		args.emplace_back(processOperand(arg));
	}
	currentBlock->append(new mips::Call(currentBlock, I.getCalledFunction(), std::move(args), &I));
}

void MIPSVisitor::visitReturnInst(ReturnInst& I)
{
	currentBlock->append(
			new mips::Return(currentBlock,
					(isa_and_nonnull<UndefValue>(I.getReturnValue())) ? nullptr : processOperand(I.getReturnValue())));
}

void MIPSVisitor::visitBranchInst(BranchInst& I)
{
	if (I.isConditional()) {
		bool first = currentBlock->getBlock()->getNextNode()==I.getSuccessor(0);
		bool second = currentBlock->getBlock()->getNextNode()==I.getSuccessor(1);
		if (first && !second)
			currentBlock->append(
					new mips::Branch(currentBlock, processOperand(I.getCondition()), I.getSuccessor(1),
							true));   // bneqz
		else if (!first && second)
			currentBlock->append(
					new mips::Branch(currentBlock, processOperand(I.getCondition()), I.getSuccessor(0),
							false));    //beqz
		else if (!first && !second) {
			currentBlock->append(
					new mips::Branch(currentBlock, processOperand(I.getCondition()), I.getSuccessor(1),
							true));   // bneqz
			currentBlock->append(new mips::Jump(currentBlock, I.getSuccessor(0)));
		}
	}
	else {
		currentBlock->append(new mips::Jump(currentBlock, I.getSuccessor(0)));
	}
}

void MIPSVisitor::visitBinaryOperator(BinaryOperator& I)
{
	const auto& a = &I;
	const auto& b = processOperand(I.getOperand(0));
	const auto& c = processOperand(I.getOperand(1));
	mips::Instruction* instruction;

	switch (I.getOpcode()) {
	case llvm::Instruction::Add:
		instruction = new mips::Arithmetic(currentBlock, "add", a, b, c);
		break;
	case llvm::Instruction::FAdd:
		instruction = new mips::Arithmetic(currentBlock, "add.s", a, b, c);
		break;
	case llvm::Instruction::Sub:
		instruction = new mips::Arithmetic(currentBlock, "sub", a, b, c);
		break;
	case llvm::Instruction::FSub:
		instruction = new mips::Arithmetic(currentBlock, "sub.s", a, b, c);
		break;
	case llvm::Instruction::Mul:
		instruction = new mips::Arithmetic(currentBlock, "mul", a, b, c);
		break;
	case llvm::Instruction::FMul:
		instruction = new mips::Arithmetic(currentBlock, "mul.s", a, b, c);
		break;
	case llvm::Instruction::UDiv:
		instruction = new mips::Arithmetic(currentBlock, "divu", a, b, c);
		break;
	case llvm::Instruction::SDiv:
		instruction = new mips::Arithmetic(currentBlock, "div", a, b, c);
		break;
	case llvm::Instruction::FDiv:
		instruction = new mips::Arithmetic(currentBlock, "div.s", a, b, c);
		break;
	case llvm::Instruction::URem:
		instruction = new Modulo(currentBlock, a, b, c);
		break;
	case llvm::Instruction::SRem:
		instruction = new Modulo(currentBlock, a, b, c);
		break;
	case llvm::Instruction::And:
		instruction = new mips::Arithmetic(currentBlock, "and", a, b, c);
		break;
	case llvm::Instruction::Or:
		instruction = new mips::Arithmetic(currentBlock, "or", a, b, c);
		break;
	case llvm::Instruction::Xor:
		instruction = new mips::Arithmetic(currentBlock, "xor", a, b, c);
		break;
	default:
		InstVisitor::visitBinaryOperator(I);
	}

	currentBlock->append(instruction);
}

void MIPSVisitor::visitInstruction(llvm::Instruction& I)
{
	std::string str;
	llvm::raw_string_ostream rso(str);
	I.print(rso);
	throw InternalError("IR instruction '"+str+"' is not implemented in MIPS (try turning optimizations off)");
}

llvm::Value* MIPSVisitor::processOperand(llvm::Value* value)
{
	ConstantExpr* c;
	if (!(c = dyn_cast_or_null<ConstantExpr>(value))) {
		return value;
	}

	llvm::Instruction* instruction = c->getAsInstruction();
	visit(instruction);
	return instruction;
}
