/* -*- mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * uARM
 *
 * Copyright (C) 2013 Marco Melletti
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef UARM_PROCESSOR_CC
#define UARM_PROCESSOR_CC

#include "processor.h"
#include "services/util.h"

processor::processor() : pu() {
	status = PS_IDLE;
	BIGEND_sig = false;
	cpint = new coprocessor_interface();
	cpu_registers[REG_CPSR] = MODE_USER;
	/*for(int i = 0; i < CPU_REGISTERS_NUM; i++)
		cpu_registers[i] = 0;*/
}

/* ******************** *
 * 						*
 * Processor facilities *
 * 						*
 * ******************** */

Word *processor::getVisibleRegister(Byte num){
	if(num > 17)	//invalid register
		return NULL;
	if(num == 16)	//status register
		return &cpu_registers[REG_CPSR];
	if(num == 15)	//pc
		return &cpu_registers[REG_PC];
	if(num <= 7)	//common to all modes
		return &cpu_registers[num];
	
	ProcessorMode mode = getMode();
	if(mode != MODE_FAST_INTERRUPT && num < 13) //only fiq has special registers r7-r12
		return &cpu_registers[num];
	switch(mode){
		case MODE_USER:
		case MODE_SYSTEM:
			if(num > 16)	//system & user don't have any saved status register
				return NULL;
			return &cpu_registers[num];
			break;
		case MODE_FAST_INTERRUPT:
			if(num == 17)
				return &cpu_registers[REG_FIQ_BASE+7];
			return &cpu_registers[REG_FIQ_BASE+(num-8)];
			break;
		case MODE_INTERRUPT:
			if(num == 17)
				return &cpu_registers[REG_IRQ_BASE+2];
			return &cpu_registers[REG_IRQ_BASE+(num-13)];
			break;
		case MODE_SUPERVISOR:
			if(num == 17)
				return &cpu_registers[REG_SVC_BASE+2];
			return &cpu_registers[REG_SVC_BASE+(num-13)];
			break;
		case MODE_ABORT:
			if(num == 17)
				return &cpu_registers[REG_ABT_BASE+2];
			return &cpu_registers[REG_ABT_BASE+(num-13)];
			break;
		case MODE_UNDEFINED:
			if(num == 17)
				return &cpu_registers[REG_UNDEF_BASE+2];
			return &cpu_registers[REG_UNDEF_BASE+(num-13)];
			break;
		default:
			return NULL;
			break;
	}
	return NULL;
}

void processor::barrelShifter(bool immediate, Byte byte, Byte half){
	Byte amount;
	Word val;
	if(immediate){	//immediate shift: byte rotated right by 2*half
		amount = half*2;
		val = byte;
		if(amount != 0){
			shifter_operand = (val >> amount) | (val << (sizeof(Word)*8 - amount));
			shifter_carry_out = ((shifter_operand >> 31) == 1 ? true : false);
		} else {
			shifter_operand = val;
			shifter_carry_out = ((cpu_registers[REG_CPSR] & C_MASK) == 1 ? true : false);
		}
	} else {	//register shift, half is regnum byte is shift operand
		if((byte & 1) == 0){	//immediate amount
			amount = byte >> 3;
		} else {				//register amount
			if((byte >> 4) != 15){	//valid register
				amount = (Byte) ((*getVisibleRegister(byte >> 4)) & 0xFF);
			} else {				//r15 cannot be used
				amount = 0;
			}
		}
		val = *getVisibleRegister(half);
		switch((byte >> 1)&3){
			case 0:	//logical shift left
				if(amount == 0){
					shifter_operand = val;
					shifter_carry_out = ((cpu_registers[REG_CPSR] & C_MASK) == 1 ? true : false);
				} else {
					shifter_operand = val << amount;
					shifter_carry_out = ((val & (1<<(32-amount))) == 1 ? true : false);
				}
				break;
			case 1:	//logical shift right
				if(amount == 0){	//special case, represents LSR #32
					shifter_operand = 0;
					shifter_carry_out = ((val >> 31) == 1 ? true : false);
				} else {
					shifter_operand = val >> amount;
					shifter_carry_out = ((val & 1<<(amount-1)) == 1 ? true : false);
				}
				break;
			case 2:	//arithmetic shift right
				if(amount == 0){	//special case, represents ASR #32
					shifter_operand = ((val >> 31) == 1 ? 0xFFFFFFFF : 0);
					shifter_carry_out = ((val >> 31) == 1 ? true : false);
				} else {
					Word ret = val >> amount;
					if((val >> 31) == 1)
						ret |= 0xFFFFFFFF << (32 - amount);
					else
						ret &= 0xFFFFFFFF >> (31 - amount);
					shifter_operand = ret;
					shifter_carry_out = ((val & 1<<(amount-1)) == 1 ? true : false);
				}
				break;
			case 3:	//rotate right
				if(amount == 0){ //special case: rotate right extended
					shifter_operand = val >> 1 | ((cpu_registers[REG_CPSR] & C_MASK) << 31);
					shifter_carry_out = ((val & 1) == 1 ? true : false);
				} else {
					shifter_operand = (val >> amount) | (val << (sizeof(Word)*8 - amount));
					shifter_carry_out = ((shifter_operand >> 31) == 1 ? true : false);
				}
				break;
		}
	}
}

bool processor::condCheck(){
	Byte cond = pipeline[PIPELINE_EXECUTE] >> 28;
	switch(cond){
		case 0:	//EQ
			if(util::getInstance()->checkBit(&cpu_registers[REG_CPSR], Z_POS))
				return true;
			else
				return false;
			break;
		case 1:	//NE
			if(util::getInstance()->checkBit(&cpu_registers[REG_CPSR], Z_POS))
				return false;
			else
				return true;
			break;
		case 2:	//CS/HS
			if(util::getInstance()->checkBit(&cpu_registers[REG_CPSR], C_POS))
				return true;
			else
				return false;
			break;
		case 3:	//CC/LO
			if(util::getInstance()->checkBit(&cpu_registers[REG_CPSR], C_POS))
				return false;
			else
				return true;
			break;
		case 4:	//MI
			if(util::getInstance()->checkBit(&cpu_registers[REG_CPSR], N_POS))
				return true;
			else
				return false;
			break;
		case 5:	//PL
			if(util::getInstance()->checkBit(&cpu_registers[REG_CPSR], N_POS))
				return false;
			else
				return true;
			break;
		case 6:	//VS
			if(util::getInstance()->checkBit(&cpu_registers[REG_CPSR], V_POS))
				return true;
			else
				return false;
			break;
		case 7:	//VC
			if(util::getInstance()->checkBit(&cpu_registers[REG_CPSR], V_POS))
				return false;
			else
				return true;
			break;
		case 8:	//HI
			if(	util::getInstance()->checkBit(&cpu_registers[REG_CPSR], C_POS) &&
				!util::getInstance()->checkBit(&cpu_registers[REG_CPSR], Z_POS))
				return true;
			else
				return false;
			break;
		case 9:	//LS
			if(	!util::getInstance()->checkBit(&cpu_registers[REG_CPSR], C_POS) ||
				util::getInstance()->checkBit(&cpu_registers[REG_CPSR], Z_POS))
				return true;
			else
				return false;
			break;
		case 10:	//GE
			if (util::getInstance()->checkBit(&cpu_registers[REG_CPSR], N_POS) ==
				util::getInstance()->checkBit(&cpu_registers[REG_CPSR], V_POS))
				return true;
			else
				return false;
			break;
		case 11:	//LT
			if (util::getInstance()->checkBit(&cpu_registers[REG_CPSR], N_POS) !=
				util::getInstance()->checkBit(&cpu_registers[REG_CPSR], V_POS))
				return true;
			else
				return false;
			break;
		case 12:	//GT
			if (!util::getInstance()->checkBit(&cpu_registers[REG_CPSR], Z_POS) &&
					(	util::getInstance()->checkBit(&cpu_registers[REG_CPSR], N_POS) ==
						util::getInstance()->checkBit(&cpu_registers[REG_CPSR], V_POS)
					)
				)
				return true;
			else
				return false;
			break;
		case 13:	//LE
			if (util::getInstance()->checkBit(&cpu_registers[REG_CPSR], Z_POS) ||
					(	util::getInstance()->checkBit(&cpu_registers[REG_CPSR], N_POS) !=
						util::getInstance()->checkBit(&cpu_registers[REG_CPSR], V_POS)
					)
				)
				return true;
			else
				return false;
			break;
		case 14:	//AL
			return true;
			break;
		case 15:	//Reserved (should not happen..)
		default:
			return false;
			break;
	}
}

void processor::nextCycle() {
	status = PS_RUNNING;
	bus->fetch(getPC());
	*getPC() += 4;
};

void processor::prefetch() {
	*getPC() -= 8;
	for(int i = 0; i < 3; i++)
		nextCycle();
}

void processor::debugARM(string mnemonic){
	cout << "Executing... "<<mnemonic << "\n";
	setOP(mnemonic);
}

/* ******************** *
 * 						*
 * Instruction decoding *
 * 						*
 * ******************** */

void processor::execute(){
	if(pipeline[PIPELINE_EXECUTE] == OP_HALT){
		status = PS_HALTED;
		return;
	}
	if(condCheck()){
		Byte codeHi = (Byte) (pipeline[PIPELINE_EXECUTE] >> 20) & 0xFF;
		Byte codeLow = (Byte) (pipeline[PIPELINE_EXECUTE] >> 4) & 0xF;
		(*this.*ARM_table[codeHi][codeLow])();	//execute funcion from ARM_table in cell instr[27:20][7:4]
	} else
		NOP();
}



void processor::multiply(){
	
}

void processor::singleDataSwap(){
	
}

void processor::coprocessorInstr(){
	
}

void processor::undefined(){
	
}

void processor::unpredictable(){
	
}

Word processor::get_unpredictable(){
	Word ret;
	for(int i = 0; i < sizeof(Word); i++)
		ret += (rand() % 0xFF) << (i * 8);
	return ret;
}

/* *************************** *
 * 							   *
 * Istructions implementations *
 * 							   *
 * *************************** */

void processor::ADC(){
	debugARM("ADC");
	
		dataProcessing(5);
}

void processor::ADD(){
	debugARM("ADD");
	
		dataProcessing(4);
}

void processor::AND(){
	debugARM("AND");
	
		dataProcessing(0);
}

void processor::B(){
	debugARM("B");
	
		branch(false, false);
}

void processor::BIC(){
	debugARM("BIC");
	
		dataProcessing(14);
}

void processor::BL(){
	debugARM("BL");
	
		branch(true, false);
}

void processor::BX(){
	debugARM("BX");
	
		branch(false, true);
}

void processor::CDP(){
	debugARM("CDP");
}

void processor::CMN(){
	debugARM("CMN");
	
		dataProcessing(11);
}

void processor::CMP(){
	debugARM("CMP");
	
		dataProcessing(10);
}

void processor::EOR(){
	debugARM("EOR");
	
		dataProcessing(1);
}

void processor::LDC(){
	debugARM("LDC");
}

void processor::LDM(){
	debugARM("LDM");
}

void processor::LDR(){
	debugARM("LDR");
	
		singleMemoryAccess(true);
}

void processor::LDRH(){
	debugARM("LDRH");
	
		halfwordDataTransfer(false, true);
}

void processor::LDRSB(){
	debugARM("LDRSB");
		
		halfwordDataTransfer(true, false);
}

void processor::LDRSH(){
	debugARM("LDRSH");
	
		halfwordDataTransfer(true, true);
}
void processor::MCR(){
	debugARM("MCR");
}

void processor::MLA(){
	debugARM("MLA");
}

void processor::MLAL(){
	debugARM("MLAL");
}

void processor::MOV(){
	debugARM("MOV");
	
		dataProcessing(13);
}

void processor::MRC(){
	debugARM("MRC");
}

void processor::MRS(){
	debugARM("MRS");
	
		accessPSR(true);
}

void processor::MSR(){
	debugARM("MSR");
	
		accessPSR(false);
}

void processor::MUL(){
	debugARM("MUL");
}

void processor::MULL(){
	debugARM("MULL");
}

void processor::MVN(){
	debugARM("MVN");
	
		dataProcessing(15);
}

void processor::ORR(){
	debugARM("ORR");
	
		dataProcessing(12);
}

void processor::RSB(){
	debugARM("RSB");
	
		dataProcessing(3);
}

void processor::RSC(){
	debugARM("RSC");
	
		dataProcessing(7);
}

void processor::SBC(){
	debugARM("SBC");
	
		dataProcessing(6);
}

void processor::STC(){
	debugARM("STC");
}

void processor::STM(){
	debugARM("STM");
}

void processor::STR(){
	debugARM("STR");
	
		singleMemoryAccess(false);
}

void processor::STRH(){
	debugARM("STRH");
	
		halfwordDataTransfer(false, false);
}

void processor::SUB(){
	debugARM("SUB");
	
		dataProcessing(2);
}

void processor::SWI(){
	debugARM("SWI");
	
		softwareInterruptTrap();
}

void processor::SWP(){
	debugARM("SWP");
}

void processor::TEQ(){
	debugARM("TEQ");
	
		dataProcessing(9);
}

void processor::TST(){
	debugARM("TST");
	
		dataProcessing(8);
}

void processor::blockDataTransfer(bool load){
	
}

void processor::softwareInterruptTrap(){
	cpu_registers[REG_LR_SVC] = *getPC() - 8;	//lr contains next instruction
	Word *cpsr = getVisibleRegister(REG_CPSR);
	cpu_registers[REG_SPSR_SVC] = *cpsr;	//spsr contains old cpsr
	*cpsr &= 0xFFFFFFE0;
	*cpsr |= MODE_SUPERVISOR;	//enter supervisor mode
	util::getInstance()->resetBitReg(cpsr, 5);	//execute in arm state
	util::getInstance()->setBitReg(cpsr, 7);	//disable normal interrupts
	*getPC() = EXC_SWI;
	
	/*should do some prefetching to execute the supervisor code next cycle..*/
	
	nextCycle();
	nextCycle();
}

void processor::accessPSR(bool load){
	bool spsr = util::getInstance()->checkBit(pipeline[PIPELINE_EXECUTE], 22);
	if(getMode() == MODE_USER && spsr){	//user mode doesn't have SPSR register, unpredictable behavior
		unpredictable();
		return;
	}
	Word *psr = (spsr ? getVisibleRegister(REG_SPSR) : getVisibleRegister(REG_CPSR));
	if(load){	//MRS
		Byte destNum = (pipeline[PIPELINE_EXECUTE] >> 12) & 0xF;
		if(destNum >= 15){	//pc must not be destination register
			unpredictable();
			return;
		}
		else{
			Word *dest = getVisibleRegister(destNum);
			*dest = *psr;
		}
	}
	else{		//MSR
		if(util::getInstance()->checkBit(pipeline[PIPELINE_EXECUTE], 16) && util::getInstance()->checkBit(pipeline[PIPELINE_EXECUTE], 25)){	//if immediate and field bits are set wrong unpredictable behavior
			unpredictable();
			return;
		}
		Word operand;
		if(util::getInstance()->checkBit(pipeline[PIPELINE_EXECUTE], 25)){	//immediate value
			Word val = pipeline[PIPELINE_EXECUTE] & 0xFF;
			Word amount = (pipeline[PIPELINE_EXECUTE] >> 8) & 0xF;
			operand = (val >> amount) | (val << (sizeof(Word)*8 - amount));
		}
		else{	//register value
			if((pipeline[PIPELINE_EXECUTE] & 0xF) < 15)
				operand = *(getVisibleRegister(pipeline[PIPELINE_EXECUTE] & 0xF));
			else{
				unpredictable();
				return;
			}
		}
		if((operand & PSR_UNALLOC_MASK) != 0){	//attempt to set reserved bits
			unpredictable();
			return;
		}
		Word mask = 0xFF000000 | (util::getInstance()->checkBit(pipeline[PIPELINE_EXECUTE], 16) ? 0xFF : 0);
		if(spsr)
			mask &= (PSR_USER_MASK | PSR_PRIV_MASK | PSR_STATE_MASK);
		else{
			if((operand & PSR_STATE_MASK) != 0){	//attempt to set non ARM execution state
				unpredictable();
				return;
			}
			if(getMode() == MODE_USER)
				mask &= PSR_USER_MASK;
			else
				mask &= (PSR_USER_MASK | PSR_PRIV_MASK);
		}
		*psr = (*psr & ! mask) | (operand & mask);
	}
}

void processor::branch(bool link, bool exchange){
	Word *pc = getVisibleRegister(REG_PC);
	if(exchange){
		Word *dest = getVisibleRegister(pipeline[PIPELINE_EXECUTE] & 0xF);
		util::getInstance()->copyBitReg(getVisibleRegister(REG_CPSR), T_POS, (*dest & 1));
		*pc = *dest & 0xFFFFFFFE;
	} else {
		if(link){
			Word *lr = getVisibleRegister(REG_LR);
			*lr = *pc+4;
			*lr &= 0xFFFFFFFC;
		}
		Word offset = (pipeline[PIPELINE_EXECUTE] & 0xFFFFFF) << 2;
		if(offset >> 24 != 0)
			for(int i = 25; i < 32; i++)	//magic numbers, this operation is strictly based on 32bit words..
				offset |= 1<<i;
		*pc += (SWord) offset;
	}
	bus->branchHappened = true;
}

void processor::halfwordDataTransfer(bool sign, bool load_halfwd){
	bool P, U, I, W;
	P = util::getInstance()->checkBit(pipeline[PIPELINE_EXECUTE], 24);
	U = util::getInstance()->checkBit(pipeline[PIPELINE_EXECUTE], 23);
	I = util::getInstance()->checkBit(pipeline[PIPELINE_EXECUTE], 22);
	W = util::getInstance()->checkBit(pipeline[PIPELINE_EXECUTE], 21);
	
	Word *src = getVisibleRegister((pipeline[PIPELINE_EXECUTE] >> 16) & 0xF);
	Word *dest = getVisibleRegister((pipeline[PIPELINE_EXECUTE] >> 12) & 0xF);
	Word offset, address = *src;
	if(src == getVisibleRegister(REG_PC))	//if source is r15 the address must be instruction addr+12
		address += 12;
	
	if(I)	//immediate offset
		offset = (pipeline[PIPELINE_EXECUTE] & 0xF) + ((pipeline[PIPELINE_EXECUTE] >> 8) & 0xF);
	else
		offset = *(getVisibleRegister(pipeline[PIPELINE_EXECUTE] & 0xF));
	
	if(P){	//preindexing
		if(U)
			address += offset;
		else
			address -= offset;
	}
	
	if(!(sign && !load_halfwd) && ((address & 1) > 0)) {	//if address is not halfword aligned return unpredictable value
		*dest = get_unpredictable();
	} else {	//address is ok
		if(sign){ //it's a signed load
			Word ret = 0;
			if(load_halfwd){ //load halfword and sign extend
				HalfWord readwd = bus->getRam()->readH(&address, BIGEND_sig);
				ret = readwd;
				if(readwd >> ((sizeof(HalfWord)*8)-1) != 0)
					for(int i = sizeof(HalfWord)*8; i < sizeof(Word)*8; i ++)
						ret |= 1<<i;
			} else {	//load byte and sign extend
				Byte readwd = bus->getRam()->read(&address, BIGEND_sig);
				ret = readwd;
				if(readwd >> ((sizeof(Byte)*8)-1) != 0)
					for(int i = sizeof(Byte)*8; i < sizeof(Word)*8; i ++)
						ret |= 1<<i;
			}
			*dest = ret;
		} else {	//it's a halfword transfer
			if(load_halfwd) {	//load val
				*dest = bus->getRam()->readH(&address, BIGEND_sig);
			} else {	//store val
				bus->getRam()->writeH(&address, (HalfWord) (*dest & 0xFFFF), BIGEND_sig);
			}
		}
	}
	
	if(P) {	//preindexing
		if(W)	//Writeback
			*src = address;
	} else { //postindexing always writes back
		if(U)
			*src = address + offset;
		else
			*src = address - offset;
	}
}

void processor::singleMemoryAccess(bool L){
	Word *src = getVisibleRegister((pipeline[PIPELINE_EXECUTE] >> 16) & 0xF);
	Word *dest = getVisibleRegister((pipeline[PIPELINE_EXECUTE] >> 12) & 0xF);
	Word offset, address = *src;
	if((pipeline[PIPELINE_EXECUTE] & (1 << 25)) > 0){	//I flag
		barrelShifter(false, ((pipeline[PIPELINE_EXECUTE] >> 4) & 0xFF), (pipeline[PIPELINE_EXECUTE] & 0xF));
		offset = shifter_operand;
	} else
		offset = pipeline[PIPELINE_EXECUTE] & 0xFFF;
	bool P, U, B, W;
	P = util::getInstance()->checkBit(pipeline[PIPELINE_EXECUTE], 24);
	U = util::getInstance()->checkBit(pipeline[PIPELINE_EXECUTE], 23);
	B = util::getInstance()->checkBit(pipeline[PIPELINE_EXECUTE], 22);
	W = util::getInstance()->checkBit(pipeline[PIPELINE_EXECUTE], 21);
	
	if(P){	//pre-indexing
		if(U)
			address += offset;
		else
			address -= offset;
			
		if(B){
			if(L)
				*dest = bus->getRam()->read(&address, BIGEND_sig);
			else
				bus->getRam()->write(&address, ((Byte) *dest & 0xFF), BIGEND_sig);
		} else
			if(L)
				*dest = bus->getRam()->readW(&address, BIGEND_sig);
			else
				bus->getRam()->writeW(&address, *dest, BIGEND_sig);
		if(W)
			*src = address;
	}
	else{
		if(W)	//user-mode forced transfer (only available in privileged mode): use user mode registers
			dest = getRegister((pipeline[PIPELINE_EXECUTE] >> 12) & 0xF);
		if(B){
			if(L)
				*dest = bus->getRam()->read(src, BIGEND_sig);
			else
				bus->getRam()->write(src, ((Byte) *dest & 0xFF), BIGEND_sig);
		} else {
			if(L)
				*dest = bus->getRam()->readW(&address, BIGEND_sig);
			else
				bus->getRam()->writeW(&address, *dest, BIGEND_sig);
		}
		if(U)
			*src = address + offset;
		else
			*src = address - offset;
	}
}

void processor::dataProcessing(Byte opcode){
	if((pipeline[PIPELINE_EXECUTE] & (1 << 25)) > 0)
		barrelShifter(true, (pipeline[PIPELINE_EXECUTE] & 0xFF), ((pipeline[PIPELINE_EXECUTE] >> 8) & 0xF));
	else
		barrelShifter(false, ((pipeline[PIPELINE_EXECUTE] >> 4) & 0xFF), (pipeline[PIPELINE_EXECUTE] & 0xF));
	Word op1 = *(getVisibleRegister((pipeline[PIPELINE_EXECUTE] >> 16) & 0xF));
	Word *dest = getVisibleRegister((pipeline[PIPELINE_EXECUTE] >> 12) & 0xF);
	Word op2 = shifter_operand;
	switch(opcode){
		case 8:		//TST
			pipeline[PIPELINE_EXECUTE] |= 1<<20;	//this instruction always updates CPSR
			dest = &alu_tmp;
		case 0:		//AND
			*dest = op1 & op2;
			bitwiseReturn(dest);
			break;
		case 9:		//TEQ
			pipeline[PIPELINE_EXECUTE] |= 1<<20;	//this instruction always updates CPSR
			dest = &alu_tmp;
		case 1:		//EOR
			*dest = op1 ^ op2;
			bitwiseReturn(dest);
			break;
		case 10:	//CMP
			pipeline[PIPELINE_EXECUTE] |= 1<<20;	//this instruction always updates CPSR
			dest = &alu_tmp;
		case 2:		//SUB
			dataPsum(op1, op2, false, false, dest);
			break;
		case 3:		//RSB
			dataPsum(op2, op1, false, false, dest);
			break;
		case 11:	//CMN
			pipeline[PIPELINE_EXECUTE] |= 1<<20;	//this instruction always updates CPSR
			dest = &alu_tmp;
		case 4:		//ADD
			dataPsum(op1, op2, false, true, dest);
			break;
		case 5:		//ADC
			dataPsum(op1, op2, true, true, dest);
			break;
		case 6:		//SBC
			dataPsum(op1, op2, true, false, dest);
			break;
		case 7:		//RSC
			dataPsum(op2, op1, true, false, dest);
			break;
		case 12:	//ORR
			*dest = op1 | op2;
			bitwiseReturn(dest);
			break;
		case 15:	//MVN
			op2 = INVERT_W(op2);
		case 13:	//MOV
			*dest = op2;
			bitwiseReturn(dest);
			break;
		case 14:	//BIC
			*dest = op1 & (INVERT_W(op2));
			bitwiseReturn(dest);
			break;
	}
}

void processor::dataPsum(Word op1, Word op2, bool carry, bool sum, Word *dest){
	int64_t sres;
	bool overflow = false, borrow = false;
	uint64_t c = (carry && util::getInstance()->checkBit(getVisibleRegister(REG_CPSR), C_POS) ? 1 : 0);
	if(sum){
		uint64_t ures;
		sres = (SDoubleWord)((SWord)op1)+(SDoubleWord)((SWord)op2)+c;
		ures = (DoubleWord)op1+(DoubleWord)op2+c;
		if(ures > 0xFFFFFFFF)
			borrow = true;
	} else {
		sres = (SDoubleWord)((SWord)op1)-(SDoubleWord)((SWord)op2)-c;
		if(op1<(op2+c))
			borrow = true;
	}
	if(sres > (SDoubleWord)0x7FFFFFFF || sres < (SDoubleWord)0xFFFFFFFF80000000)
		overflow = true;
	*dest = (Word) sres & 0xFFFFFFFF;
	if(pipeline[PIPELINE_EXECUTE] & (1<<20)){	// S == 1
		if(((pipeline[PIPELINE_EXECUTE] >> 12) & 0xF) == 15){	//Rd == 15
			Word *savedPSR = getVisibleRegister(REG_SPSR);
			if(savedPSR != NULL)
				cpu_registers[REG_CPSR] = *savedPSR;
			else
				unpredictable();
		} else {
			util::getInstance()->copyBitFromReg(&cpu_registers[REG_CPSR], N_POS, dest, 31);
			util::getInstance()->copyBitReg(&cpu_registers[REG_CPSR], Z_POS, (*dest == 0 ? 1 : 0));
			util::getInstance()->copyBitReg(&cpu_registers[REG_CPSR], C_POS, (borrow ? 1 : 0));
			util::getInstance()->copyBitReg(&cpu_registers[REG_CPSR], V_POS, (overflow ? 1 : 0));
		}
	}
}

void processor::bitwiseReturn(Word *dest){
	if(pipeline[PIPELINE_EXECUTE] & (1<<20)){	// S == 1
		if(((pipeline[PIPELINE_EXECUTE] >> 16) & 0xF) == 15){	//Rd == 15
			Word *savedPSR = getVisibleRegister(REG_SPSR);
			if(savedPSR != NULL)
				cpu_registers[REG_CPSR] = *savedPSR;
			else
				unpredictable();
		} else {
			util::getInstance()->copyBitFromReg(&cpu_registers[REG_CPSR], N_POS, dest, 31);
			util::getInstance()->copyBitReg(&cpu_registers[REG_CPSR], Z_POS, (*dest == 0 ? 1 : 0));
			util::getInstance()->copyBitReg(&cpu_registers[REG_CPSR], C_POS, (shifter_carry_out ? 1 : 0));
		}
	}
}

#endif //UARM_PROCESSOR_CC
