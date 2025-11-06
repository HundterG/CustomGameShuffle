#include "CPUOpCodes.inl"

struct NES_CPU
{
	// registers
	uint8_t A = 0, X = 0, Y = 0;
	uint8_t stackPointer = 0xFD;
	uint16_t programCounter = 0;
	bool carry = false, zero = false, interruptDisable = true, decimal = false, overflow = false, negative = false;

	int tickSkipCount = 0;
	bool waitingNMI = false, waitingIRQ = false, halted = false;

	void Reset(NES_Ram &ram)
	{
		A = X = Y = 0;
		programCounter = ram.Get(0xFFFC) + ((uint16_t)(ram.Get(0xFFFD)) << 8);
		stackPointer = 0xFD;
		carry = false, zero = false, interruptDisable = true, decimal = false, overflow = false, negative = false;
		tickSkipCount = 0;
		waitingNMI = waitingIRQ = halted = false;
	}

#define PUSHSTACK(__value) { ram.Set(0x0100 | stackPointer, __value); --stackPointer; }

	void Tick(NES_Ram &ram)
	{
		if(0 < --tickSkipCount)
			return;

		if(waitingNMI)
		{
			PUSHSTACK((uint8_t)(programCounter >> 8));
			PUSHSTACK((uint8_t)(programCounter & 0x00FF));

			uint8_t flags = (uint8_t)(carry) |
				((uint8_t)(zero) << 1) |
				((uint8_t)(interruptDisable) << 2) |
				((uint8_t)(decimal) << 3) |
				((uint8_t)(overflow) << 6) |
				((uint8_t)(negative) << 7) |
				0x30;
			PUSHSTACK(flags);
			
			interruptDisable = true;
			programCounter = ram.Get(0xFFFA) + ((uint16_t)(ram.Get(0xFFFB)) << 8);
			tickSkipCount = 7;
			waitingNMI = waitingIRQ = false;
			return;
		}
		else if(waitingIRQ)
		{
			if(!interruptDisable)
			{
				PUSHSTACK((uint8_t)(programCounter >> 8));
				PUSHSTACK((uint8_t)(programCounter & 0x00FF));
	
				uint8_t flags = (uint8_t)(carry) |
					((uint8_t)(zero) << 1) |
					((uint8_t)(interruptDisable) << 2) |
					((uint8_t)(decimal) << 3) |
					((uint8_t)(overflow) << 6) |
					((uint8_t)(negative) << 7) |
					0x30;
				PUSHSTACK(flags);
	
				interruptDisable = true;
				programCounter = ram.Get(0xFFFE) + ((uint16_t)(ram.Get(0xFFFF)) << 8);
				tickSkipCount = 7;
			}
			waitingNMI = waitingIRQ = false;
			return;
		}

		uint8_t opCode = ram.Get(programCounter);
		char const *hexTable = "0123456789ABCDEF";
		char addrString[5] = {0};
		addrString[3] = hexTable[programCounter & 0x000F];
		addrString[2] = hexTable[(programCounter & 0x00F0) >> 4];
		addrString[1] = hexTable[(programCounter & 0x0F00) >> 8];
		addrString[0] = hexTable[(programCounter & 0xF000) >> 12];
		char fString[3] = {0};
		fString[1] = hexTable[opCode & 0x0F];
		fString[0] = hexTable[(opCode & 0xF0) >> 4];
		//logcpu << "$" << addrString << ": " << fString << "\n";
		++programCounter;

		//std::cout << int(opCode) << " : " << std::hex << programCounter-1 << std::dec << "\n";

		switch(NES_CPU_OpCode(opCode))
		{
// Helper Macros
#define SETZN(__value) { zero = (__value) == 0; negative = ((__value) & 0x80) != 0; }
#define POPSTACK() [&]() { ++stackPointer; return ram.Get(0x0100 | stackPointer); }()

// Immediate operations have everything they need or get arg data from program data
#define IMMEDIATE(__opcode, __code, __cycleCount) case NES_CPU_OpCode::__opcode: { __code; tickSkipCount = (__cycleCount); break; }
#define IMMEDIATE_ARG(__opcode, __code, __cycleCount) case NES_CPU_OpCode::__opcode: { uint8_t arg = ram.Get(programCounter); ++programCounter; __code; tickSkipCount = (__cycleCount); break; }

// Zero Page operations get arg data from the first 0xFF bytes of ram
#define ZERO(__opcode, __code, __cycleCount) case NES_CPU_OpCode::__opcode: { uint8_t argLoc = ram.Get(programCounter); ++programCounter; uint8_t arg = ram.Get(argLoc); __code; tickSkipCount = (__cycleCount); break; }
#define ZEROX(__opcode, __code, __cycleCount) case NES_CPU_OpCode::__opcode: { uint16_t argLoc = ((uint16_t)(ram.Get(programCounter)) + X) & 0x00FF; ++programCounter; uint8_t arg = ram.Get(argLoc); __code; tickSkipCount = (__cycleCount); break; }
#define ZEROY(__opcode, __code, __cycleCount) case NES_CPU_OpCode::__opcode: { uint16_t argLoc = ((uint16_t)(ram.Get(programCounter)) + Y) & 0x00FF; ++programCounter; uint8_t arg = ram.Get(argLoc); __code; tickSkipCount = (__cycleCount); break; }

// Absolute operations get arg data from anywhere in ram
#define ABSOLUTE(__opcode, __code, __cycleCount) case NES_CPU_OpCode::__opcode: { uint16_t argLoc = ram.Get(programCounter) + ((uint16_t)(ram.Get(programCounter + 1)) << 8); programCounter += 2; uint8_t arg = ram.Get(argLoc); __code; tickSkipCount = (__cycleCount); break; }
#define ABSOLUTEX(__opcode, __code, __cycleCount) case NES_CPU_OpCode::__opcode: { uint16_t argLoc = ram.Get(programCounter) + ((uint16_t)(ram.Get(programCounter + 1)) << 8); programCounter += 2; uint8_t arg = ram.Get(argLoc + X); __code; tickSkipCount = (__cycleCount) + ((argLoc & 0xFF00) != ((argLoc + X) & 0xFF00)); break; }
#define ABSOLUTEY(__opcode, __code, __cycleCount) case NES_CPU_OpCode::__opcode: { uint16_t argLoc = ram.Get(programCounter) + ((uint16_t)(ram.Get(programCounter + 1)) << 8); programCounter += 2; uint8_t arg = ram.Get(argLoc + Y); __code; tickSkipCount = (__cycleCount) + ((argLoc & 0xFF00) != ((argLoc + Y) & 0xFF00)); break; }

// Branch operations use arg from program data as offset for program counter. Other than the condition, they are all the same
#define BRANCH(__opcode, __condition) case NES_CPU_OpCode::__opcode: { tickSkipCount = 2; if(__condition) { int8_t offset = (int8_t)(ram.Get(programCounter)); ++programCounter; tickSkipCount += 1 + ((programCounter & 0xFF00) != ((programCounter + offset) & 0xFF00)); programCounter += offset; } else ++programCounter; break; }

// Indirect operations get arg from location pointed by a location in ram. Basically... (program data) -> (location in ram) -> arg
// val = PEEK( PEEK((arg+X)&FF) + PEEK((arg+X+1)&FF) << 8 )
#define INDIRECTX(__opcode, __code, __cycleCount) case NES_CPU_OpCode::__opcode: { uint16_t argLocLoc = (ram.Get(programCounter) + X) & 0x00FF; ++programCounter; uint16_t argLoc = ram.Get(argLocLoc) + (ram.Get((argLocLoc + 1) & 0x00FF) << 8); uint8_t arg = ram.Get(argLoc); __code; tickSkipCount = (__cycleCount); break; }
// val = PEEK( PEEK(arg) + PEEK((arg+1)&FF)<<8 + Y )
#define INDIRECTY(__opcode, __code, __cycleCount) case NES_CPU_OpCode::__opcode: { uint16_t argLocLoc = ram.Get(programCounter); ++programCounter; uint16_t argLoc = ram.Get(argLocLoc) + (ram.Get((argLocLoc + 1) & 0x00FF) << 8) + Y; uint8_t arg = ram.Get(argLoc); __code; tickSkipCount = (__cycleCount) + ((argLocLoc & 0xFF00) != ((argLocLoc + 1) & 0xFF00)); break; }

			

		// Add with Carry
		IMMEDIATE_ARG(ADC, int16_t result = (int16_t)(A) + arg + carry; carry = 0xFF < result; overflow = ((result ^ A) & (result ^ arg) & 0x80) != 0; A = (uint8_t)(result); SETZN(A);, 2)
		ZERO(ADC_Z, int16_t result = (int16_t)(A) + arg + carry; carry = 0xFF < result; overflow = ((result ^ A) & (result ^ arg) & 0x80) != 0; A = (uint8_t)(result); SETZN(A);, 3)
		ZEROX(ADC_ZX, int16_t result = (int16_t)(A) + arg + carry; carry = 0xFF < result; overflow = ((result ^ A) & (result ^ arg) & 0x80) != 0; A = (uint8_t)(result); SETZN(A);, 4)
		ABSOLUTE(ADC_A, int16_t result = (int16_t)(A) + arg + carry; carry = 0xFF < result; overflow = ((result ^ A) & (result ^ arg) & 0x80) != 0; A = (uint8_t)(result); SETZN(A);, 4)
		ABSOLUTEX(ADC_AX, int16_t result = (int16_t)(A) + arg + carry; carry = 0xFF < result; overflow = ((result ^ A) & (result ^ arg) & 0x80) != 0; A = (uint8_t)(result); SETZN(A);, 4)
		ABSOLUTEY(ADC_AY, int16_t result = (int16_t)(A) + arg + carry; carry = 0xFF < result; overflow = ((result ^ A) & (result ^ arg) & 0x80) != 0; A = (uint8_t)(result); SETZN(A);, 4)
		INDIRECTX(ADC_IX, int16_t result = (int16_t)(A) + arg + carry; carry = 0xFF < result; overflow = ((result ^ A) & (result ^ arg) & 0x80) != 0; A = (uint8_t)(result); SETZN(A);, 6)
		INDIRECTY(ADC_IY, int16_t result = (int16_t)(A) + arg + carry; carry = 0xFF < result; overflow = ((result ^ A) & (result ^ arg) & 0x80) != 0; A = (uint8_t)(result); SETZN(A);, 5)

		// Bitwise AND
		IMMEDIATE_ARG(AND, A &= arg; SETZN(A);, 2)
		ZERO(AND_Z, A &= arg; SETZN(A);, 3)
		ZEROX(AND_ZX, A &= arg; SETZN(A);, 4)
		ABSOLUTE(AND_A, A &= arg; SETZN(A);, 4)
		ABSOLUTEX(AND_AX, A &= arg; SETZN(A);, 4)
		ABSOLUTEY(AND_AY, A &= arg; SETZN(A);, 4)
		INDIRECTX(AND_IX, A &= arg; SETZN(A);, 6)
		INDIRECTY(AND_IY, A &= arg; SETZN(A);, 5)

		// Arithmetic Shift Left
		IMMEDIATE(ASL, carry = ((A) & 0x80) != 0; A = (A << 1) & 0xFE; SETZN(A);, 2)
		ZERO(ASL_Z, uint8_t copy = arg; carry = ((arg) & 0x80) != 0; arg = (arg << 1) & 0xFE; ram.Set(argLoc, copy); ram.Set(argLoc, arg); SETZN(arg);, 5)
		ZEROX(ASL_ZX, uint8_t copy = arg; carry = ((arg) & 0x80) != 0; arg = (arg << 1) & 0xFE; ram.Set(argLoc, copy); ram.Set(argLoc, arg); SETZN(arg);, 6)
		ABSOLUTE(ASL_A, uint8_t copy = arg; carry = ((arg) & 0x80) != 0; arg = (arg << 1) & 0xFE; ram.Set(argLoc, copy); ram.Set(argLoc, arg); SETZN(arg);, 6)
		ABSOLUTEX(ASL_AX, uint8_t copy = arg; carry = ((arg) & 0x80) != 0; arg = (arg << 1) & 0xFE; ram.Set(argLoc + X, copy); ram.Set(argLoc + X, arg); SETZN(arg);, 7)

		// Branch if Carry Clear
		BRANCH(BCC, !carry)

		// Branch if Carry Set
		BRANCH(BCS, carry)

		// Branch if Equal
		BRANCH(BEQ, zero)

		// Bit Test
		ZERO(BIT_Z, uint8_t temp = A & arg; zero = temp == 0; overflow = (arg & 0x40) != 0; negative = (arg & 0x80) != 0;, 3)
		ABSOLUTE(BIT_A, uint8_t temp = A & arg; zero = temp == 0; overflow = (arg & 0x40) != 0; negative = (arg & 0x80) != 0;, 4)

		// Branch if Minus
		BRANCH(BMI, negative)

		// Branch if Not Equal
		BRANCH(BNE, !zero)

		// Branch if Plus
		BRANCH(BPL, !negative)

		// Break - Software Interrupt
		case NES_CPU_OpCode::BRK:
		{
			++programCounter;
			PUSHSTACK((uint8_t)(programCounter >> 8));
			PUSHSTACK((uint8_t)(programCounter & 0x00FF));

			uint8_t flags = (uint8_t)(carry) |
				((uint8_t)(zero) << 1) |
				((uint8_t)(interruptDisable) << 2) |
				((uint8_t)(decimal) << 3) |
				((uint8_t)(overflow) << 6) |
				((uint8_t)(negative) << 7) |
				0x30;
			PUSHSTACK(flags);

			interruptDisable = true;
			programCounter = ram.Get(0xFFFE) + ((uint16_t)(ram.Get(0xFFFF)) << 8);;
			tickSkipCount = 7;
			break;
		}

		// Branch if Overflow Clear
		BRANCH(BVC, !overflow)

		// Branch if Overflow Set
		BRANCH(BVS, overflow)

		// Clear Carry Flag
		IMMEDIATE(CLC, carry = false;, 2)

		// Clear Decimal Flag
		IMMEDIATE(CLD, decimal = false;, 2)

		// Clear Interrupt Disable Flag
		IMMEDIATE(CLI, interruptDisable = false;, 2)

		// Clear Overflow Flag
		IMMEDIATE(CLV, overflow = false;, 2)

		// Compare A
		IMMEDIATE_ARG(CMP, uint8_t temp = A - arg; carry = A >= arg; zero = A == arg; negative = (temp & 0x80) != 0;, 2)
		ZERO(CMP_Z, uint8_t temp = A - arg; carry = A >= arg; zero = A == arg; negative = (temp & 0x80) != 0;, 3)
		ZEROX(CMP_ZX, uint8_t temp = A - arg; carry = A >= arg; zero = A == arg; negative = (temp & 0x80) != 0;, 4)
		ABSOLUTE(CMP_A, uint8_t temp = A - arg; carry = A >= arg; zero = A == arg; negative = (temp & 0x80) != 0;, 4)
		ABSOLUTEX(CMP_AX, uint8_t temp = A - arg; carry = A >= arg; zero = A == arg; negative = (temp & 0x80) != 0;, 4)
		ABSOLUTEY(CMP_AY, uint8_t temp = A - arg; carry = A >= arg; zero = A == arg; negative = (temp & 0x80) != 0;, 4)
		INDIRECTX(CMP_IX, uint8_t temp = A - arg; carry = A >= arg; zero = A == arg; negative = (temp & 0x80) != 0;, 6)
		INDIRECTY(CMP_IY, uint8_t temp = A - arg; carry = A >= arg; zero = A == arg; negative = (temp & 0x80) != 0;, 5)

		// Compare X
		IMMEDIATE_ARG(CPX, uint8_t temp = X - arg; carry = X >= arg; zero = X == arg; negative = (temp & 0x80) != 0;, 2)
		ZERO(CPX_Z, uint8_t temp = X - arg; carry = X >= arg; zero = X == arg; negative = (temp & 0x80) != 0;, 3)
		ABSOLUTE(CPX_A, uint8_t temp = X - arg; carry = X >= arg; zero = X == arg; negative = (temp & 0x80) != 0;, 4)

		// Compare Y
		IMMEDIATE_ARG(CPY, uint8_t temp = Y - arg; carry = Y >= arg; zero = Y == arg; negative = (temp & 0x80) != 0;, 2)
		ZERO(CPY_Z, uint8_t temp = Y - arg; carry = Y >= arg; zero = Y == arg; negative = (temp & 0x80) != 0;, 3)
		ABSOLUTE(CPY_A, uint8_t temp = Y - arg; carry = Y >= arg; zero = Y == arg; negative = (temp & 0x80) != 0;, 4)

		// Decrement Memory
		ZERO(DEC_Z, ram.Set(argLoc, arg); --arg; ram.Set(argLoc, arg); SETZN(arg);, 5)
		ZEROX(DEC_ZX, ram.Set(argLoc, arg); --arg; ram.Set(argLoc, arg); SETZN(arg);, 6)
		ABSOLUTE(DEC_A, ram.Set(argLoc, arg); --arg; ram.Set(argLoc, arg); SETZN(arg);, 6)
		ABSOLUTEX(DEC_AX, ram.Set(argLoc + X, arg); --arg; ram.Set(argLoc + X, arg); SETZN(arg);, 7)

		// Decrement X
		IMMEDIATE(DEX, --X; SETZN(X), 2)

		// Decrement Y
		IMMEDIATE(DEY, --Y; SETZN(Y), 2)

		// Bitwise Exclusive OR
		IMMEDIATE_ARG(EOR, A ^= arg; SETZN(A);, 2)
		ZERO(EOR_Z, A ^= arg; SETZN(A);, 3)
		ZEROX(EOR_ZX, A ^= arg; SETZN(A);, 4)
		ABSOLUTE(EOR_A, A ^= arg; SETZN(A);, 4)
		ABSOLUTEX(EOR_AX, A ^= arg; SETZN(A);, 4)
		ABSOLUTEY(EOR_AY, A ^= arg; SETZN(A);, 4)
		INDIRECTX(EOR_IX, A ^= arg; SETZN(A);, 6)
		INDIRECTY(EOR_IY, A ^= arg; SETZN(A);, 5)

		// Increment Memory
		ZERO(INC_Z, ram.Set(argLoc, arg); ++arg; ram.Set(argLoc, arg); SETZN(arg);, 5)
		ZEROX(INC_ZX, ram.Set(argLoc, arg); ++arg; ram.Set(argLoc, arg); SETZN(arg);, 6)
		ABSOLUTE(INC_A, ram.Set(argLoc, arg); ++arg; ram.Set(argLoc, arg); SETZN(arg);, 6)
		ABSOLUTEX(INC_AX, ram.Set(argLoc + X, arg); ++arg; ram.Set(argLoc + X, arg); SETZN(arg);, 7)

		// Increment X
		IMMEDIATE(INX, ++X; SETZN(X), 2)

		// Increment Y
		IMMEDIATE(INY, ++Y; SETZN(Y), 2)

		// Jump
		case NES_CPU_OpCode::JMP_A:
		{
			uint16_t loc = ram.Get(programCounter) + ((uint16_t)(ram.Get(programCounter + 1)) << 8);
			programCounter = loc;
			tickSkipCount = 3;
			break;
		}
		case NES_CPU_OpCode::JMP_I:
		{
			uint16_t locLoc = ram.Get(programCounter) + ((uint16_t)(ram.Get(programCounter + 1)) << 8);
			uint16_t loc;
			if((locLoc & 0x00FF) == 0xFF)
				loc = ram.Get(locLoc) + ((uint16_t)(ram.Get(locLoc & 0xFF00)) << 8);
			else
				loc = ram.Get(locLoc) + ((uint16_t)(ram.Get(locLoc + 1)) << 8);
			programCounter = loc;
			tickSkipCount = 5;
			break;
		}

		// Jump to Subroutine
		case NES_CPU_OpCode::JSR_A:
		{
			uint16_t loc = ram.Get(programCounter) + ((uint16_t)(ram.Get(programCounter + 1)) << 8);
			++programCounter;
			PUSHSTACK((uint8_t)(programCounter >> 8));
			PUSHSTACK((uint8_t)(programCounter & 0x00FF));
			programCounter = loc;
			tickSkipCount = 6;
			break;
		}

		// Load A
		IMMEDIATE_ARG(LDA, A = arg; SETZN(A);, 2)
		ZERO(LDA_Z, A = arg; SETZN(A);, 3)
		ZEROX(LDA_ZX, A = arg; SETZN(A);, 4)
		ABSOLUTE(LDA_A, A = arg; SETZN(A);, 4)
		ABSOLUTEX(LDA_AX, A = arg; SETZN(A);, 4)
		ABSOLUTEY(LDA_AY, A = arg; SETZN(A);, 4)
		INDIRECTX(LDA_IX, A = arg; SETZN(A);, 6)
		INDIRECTY(LDA_IY, A = arg; SETZN(A);, 5)

		// Load X
		IMMEDIATE_ARG(LDX, X = arg; SETZN(X);, 2)
		ZERO(LDX_Z, X = arg; SETZN(X);, 3)
		ZEROY(LDX_ZY, X = arg; SETZN(X);, 4)
		ABSOLUTE(LDX_A, X = arg; SETZN(X);, 4)
		ABSOLUTEY(LDX_AY, X = arg; SETZN(X);, 4)

		// Load Y
		IMMEDIATE_ARG(LDY, Y = arg; SETZN(Y);, 2)
		ZERO(LDY_Z, Y = arg; SETZN(Y);, 3)
		ZEROX(LDY_ZX, Y = arg; SETZN(Y);, 4)
		ABSOLUTE(LDY_A, Y = arg; SETZN(Y);, 4)
		ABSOLUTEX(LDY_AX, Y = arg; SETZN(Y);, 4)

		// Logical Shift Right
		IMMEDIATE(LSR, carry = ((A) & 0x01) != 0; A = (A >> 1) & 0x7F; SETZN(A);, 2)
		ZERO(LSR_Z, uint8_t copy = arg; carry = ((arg) & 0x01) != 0; arg = (arg >> 1) & 0x7F; ram.Set(argLoc, copy); ram.Set(argLoc, arg); SETZN(arg);, 5)
		ZEROX(LSR_ZX, uint8_t copy = arg; carry = ((arg) & 0x01) != 0; arg = (arg >> 1) & 0x7F; ram.Set(argLoc, copy); ram.Set(argLoc, arg); SETZN(arg);, 6)
		ABSOLUTE(LSR_A, uint8_t copy = arg; carry = ((arg) & 0x01) != 0; arg = (arg >> 1) & 0x7F; ram.Set(argLoc, copy); ram.Set(argLoc, arg); SETZN(arg);, 6)
		ABSOLUTEX(LSR_AX, uint8_t copy = arg; carry = ((arg) & 0x01) != 0; arg = (arg >> 1) & 0x7F; ram.Set(argLoc + X, copy); ram.Set(argLoc + X, arg); SETZN(arg);, 7)

		// No Operation
		IMMEDIATE(NOP, , 2)

		// Bitwise OR
		IMMEDIATE_ARG(ORA, A |= arg; SETZN(A);, 2)
		ZERO(ORA_Z, A |= arg; SETZN(A);, 3)
		ZEROX(ORA_ZX, A |= arg; SETZN(A);, 4)
		ABSOLUTE(ORA_A, A |= arg; SETZN(A);, 4)
		ABSOLUTEX(ORA_AX, A |= arg; SETZN(A);, 4)
		ABSOLUTEY(ORA_AY, A |= arg; SETZN(A);, 4)
		INDIRECTX(ORA_IX, A |= arg; SETZN(A);, 6)
		INDIRECTY(ORA_IY, A |= arg; SETZN(A);, 5)

		// Push A
		IMMEDIATE(PHA, PUSHSTACK(A);, 3)

		// Push Processor State
		case NES_CPU_OpCode::PHP:
		{
			uint8_t flags = (uint8_t)(carry) |
				((uint8_t)(zero) << 1) |
				((uint8_t)(interruptDisable) << 2) |
				((uint8_t)(decimal) << 3) |
				((uint8_t)(overflow) << 6) |
				((uint8_t)(negative) << 7) |
				0x30;
			PUSHSTACK(flags);
			tickSkipCount = 3;
			break;
		}

		// Pop A
		IMMEDIATE(PLA, A = POPSTACK(); SETZN(A);, 4)

		// Pop Processor State
		case NES_CPU_OpCode::PLP:
		{
			uint8_t flags = POPSTACK();
			carry = (flags & 0x01) != 0;
			zero = (flags & 0x02) != 0;
			interruptDisable = (flags & 0x04) != 0;
			decimal = (flags & 0x08) != 0;
			overflow = (flags & 0x40) != 0;
			negative = (flags & 0x80) != 0;
			tickSkipCount = 4;
			break;
		}

		// Rotate Left
		IMMEDIATE(ROL, bool carryCopy = carry; carry = ((A) & 0x80) != 0; A = (A << 1) | (carryCopy * 0x01); SETZN(A);, 2)
		ZERO(ROL_Z, bool carryCopy = carry; uint8_t copy = arg; carry = ((arg) & 0x80) != 0; arg = (arg << 1) | (carryCopy * 0x01); ram.Set(argLoc, copy); ram.Set(argLoc, arg); SETZN(arg);, 5)
		ZEROX(ROL_ZX, bool carryCopy = carry; uint8_t copy = arg; carry = ((arg) & 0x80) != 0; arg = (arg << 1) | (carryCopy * 0x01); ram.Set(argLoc, copy); ram.Set(argLoc, arg); SETZN(arg);, 6)
		ABSOLUTE(ROL_A, bool carryCopy = carry; uint8_t copy = arg; carry = ((arg) & 0x80) != 0; arg = (arg << 1) | (carryCopy * 0x01); ram.Set(argLoc, copy); ram.Set(argLoc, arg); SETZN(arg);, 6)
		ABSOLUTEX(ROL_AX, bool carryCopy = carry; uint8_t copy = arg; carry = ((arg) & 0x80) != 0; arg = (arg << 1) | (carryCopy * 0x01); ram.Set(argLoc + X, copy); ram.Set(argLoc + X, arg); SETZN(arg);, 7)

		// Rotate Right
		IMMEDIATE(ROR, bool carryCopy = carry; carry = ((A) & 0x01) != 0; A = (A >> 1) | (carryCopy * 0x80); SETZN(A);, 2)
		ZERO(ROR_Z, bool carryCopy = carry; uint8_t copy = arg; carry = ((arg) & 0x01) != 0; arg = (arg >> 1) | (carryCopy * 0x80); ram.Set(argLoc, copy); ram.Set(argLoc, arg); SETZN(arg);, 5)
		ZEROX(ROR_ZX, bool carryCopy = carry; uint8_t copy = arg; carry = ((arg) & 0x01) != 0; arg = (arg >> 1) | (carryCopy * 0x80); ram.Set(argLoc, copy); ram.Set(argLoc, arg); SETZN(arg);, 6)
		ABSOLUTE(ROR_A, bool carryCopy = carry; uint8_t copy = arg; carry = ((arg) & 0x01) != 0; arg = (arg >> 1) | (carryCopy * 0x80); ram.Set(argLoc, copy); ram.Set(argLoc, arg); SETZN(arg);, 6)
		ABSOLUTEX(ROR_AX, bool carryCopy = carry; uint8_t copy = arg; carry = ((arg) & 0x01) != 0; arg = (arg >> 1) | (carryCopy * 0x80); ram.Set(argLoc + X, copy); ram.Set(argLoc + X, arg); SETZN(arg);, 7)

		// Return from Interrupt
		case NES_CPU_OpCode::RTI:
		{
			uint8_t flags = POPSTACK();
			carry = (flags & 0x01) != 0;
			zero = (flags & 0x02) != 0;
			interruptDisable = (flags & 0x04) != 0;
			decimal = (flags & 0x08) != 0;
			overflow = (flags & 0x40) != 0;
			negative = (flags & 0x80) != 0;
			programCounter = POPSTACK();
			programCounter |= (uint16_t)(POPSTACK()) << 8;
			tickSkipCount = 6;
			break;
		}

		// Return from Subroutine
		case NES_CPU_OpCode::RTS:
		{
			programCounter = POPSTACK();
			programCounter |= (uint16_t)(POPSTACK()) << 8;
			++programCounter;
			tickSkipCount = 6;
			break;
		}

		// Subtract with Carry
		IMMEDIATE_ARG(SBC, int16_t result = (int16_t)(A) - arg - !carry; carry = !(result < 0x00); overflow = ((result ^ A) & (result ^ ~arg) & 0x80) != 0; A = (uint8_t)(result); SETZN(A);, 2)
		ZERO(SBC_Z, int16_t result = (int16_t)(A) - arg - !carry; carry = !(result < 0x00); overflow = ((result ^ A) & (result ^ ~arg) & 0x80) != 0; A = (uint8_t)(result); SETZN(A);, 3)
		ZEROX(SBC_ZX, int16_t result = (int16_t)(A) - arg - !carry; carry = !(result < 0x00); overflow = ((result ^ A) & (result ^ ~arg) & 0x80) != 0; A = (uint8_t)(result); SETZN(A);, 4)
		ABSOLUTE(SBC_A, int16_t result = (int16_t)(A) - arg - !carry; carry = !(result < 0x00); overflow = ((result ^ A) & (result ^ ~arg) & 0x80) != 0; A = (uint8_t)(result); SETZN(A);, 4)
		ABSOLUTEX(SBC_AX, int16_t result = (int16_t)(A) - arg - !carry; carry = !(result < 0x00); overflow = ((result ^ A) & (result ^ ~arg) & 0x80) != 0; A = (uint8_t)(result); SETZN(A);, 4)
		ABSOLUTEY(SBC_AY, int16_t result = (int16_t)(A) - arg - !carry; carry = !(result < 0x00); overflow = ((result ^ A) & (result ^ ~arg) & 0x80) != 0; A = (uint8_t)(result); SETZN(A);, 4)
		INDIRECTX(SBC_IX, int16_t result = (int16_t)(A) - arg - !carry; carry = !(result < 0x00); overflow = ((result ^ A) & (result ^ ~arg) & 0x80) != 0; A = (uint8_t)(result); SETZN(A);, 6)
		INDIRECTY(SBC_IY, int16_t result = (int16_t)(A) - arg - !carry; carry = !(result < 0x00); overflow = ((result ^ A) & (result ^ ~arg) & 0x80) != 0; A = (uint8_t)(result); SETZN(A);, 5)

		// Set Carry
		IMMEDIATE(SEC, carry = true;, 2)

		// Set Decimal
		IMMEDIATE(SED, decimal = true;, 2)

		// Set Interrupt Disable
		IMMEDIATE(SEI, interruptDisable = true;, 2)

		// Store A
		case NES_CPU_OpCode::STA_Z: { uint8_t argLoc = ram.Get(programCounter); ++programCounter; ram.Set(argLoc, A); tickSkipCount = 3; break; }
		case NES_CPU_OpCode::STA_ZX: { uint16_t argLoc = ((uint16_t)(ram.Get(programCounter)) + X) & 0x00FF; ++programCounter; ram.Set(argLoc, A); tickSkipCount = 4; break; }
		case NES_CPU_OpCode::STA_A: { uint16_t argLoc = ram.Get(programCounter) + ((uint16_t)(ram.Get(programCounter + 1)) << 8); programCounter += 2; ram.Set(argLoc, A); tickSkipCount = 4; break; }
		case NES_CPU_OpCode::STA_AX: { uint16_t argLoc = ram.Get(programCounter) + ((uint16_t)(ram.Get(programCounter + 1)) << 8); programCounter += 2; ram.Set(argLoc + X, A); tickSkipCount = 5 + ((argLoc & 0xFF00) != ((argLoc + X) & 0xFF00)); break; }
		case NES_CPU_OpCode::STA_AY: { uint16_t argLoc = ram.Get(programCounter) + ((uint16_t)(ram.Get(programCounter + 1)) << 8); programCounter += 2; ram.Set(argLoc + Y, A); tickSkipCount = 5 + ((argLoc & 0xFF00) != ((argLoc + Y) & 0xFF00)); break; }
		case NES_CPU_OpCode::STA_IX: { uint16_t argLocLoc = (ram.Get(programCounter) + X) & 0x00FF; ++programCounter; uint16_t argLoc = ram.Get(argLocLoc) + (ram.Get((argLocLoc + 1) & 0x00FF) << 8); ram.Set(argLoc, A); tickSkipCount = 6; break; }
		case NES_CPU_OpCode::STA_IY: { uint16_t argLocLoc = ram.Get(programCounter); ++programCounter; uint16_t argLoc = ram.Get(argLocLoc) + (ram.Get((argLocLoc + 1) & 0x00FF) << 8) + Y; ram.Set(argLoc, A); tickSkipCount = 6 + ((argLocLoc & 0xFF00) != ((argLocLoc + 1) & 0xFF00)); break; }

		// Store X
		case NES_CPU_OpCode::STX_Z: { uint8_t argLoc = ram.Get(programCounter); ++programCounter; ram.Set(argLoc, X); tickSkipCount = 3; break; }
		case NES_CPU_OpCode::STX_ZY: { uint16_t argLoc = ((uint16_t)(ram.Get(programCounter)) + Y) & 0x00FF; ++programCounter; ram.Set(argLoc, X); tickSkipCount = 4; break; }
		case NES_CPU_OpCode::STX_A: { uint16_t argLoc = ram.Get(programCounter) + ((uint16_t)(ram.Get(programCounter + 1)) << 8); programCounter += 2; ram.Set(argLoc, X); tickSkipCount = 4; break; }

		// Store Y
		case NES_CPU_OpCode::STY_Z: { uint8_t argLoc = ram.Get(programCounter); ++programCounter; ram.Set(argLoc, Y); tickSkipCount = 3; break; }
		case NES_CPU_OpCode::STY_ZX: { uint16_t argLoc = ((uint16_t)(ram.Get(programCounter)) + X) & 0x00FF; ++programCounter; ram.Set(argLoc, Y); tickSkipCount = 4; break; }
		case NES_CPU_OpCode::STY_A: { uint16_t argLoc = ram.Get(programCounter) + ((uint16_t)(ram.Get(programCounter + 1)) << 8); programCounter += 2; ram.Set(argLoc, Y); tickSkipCount = 4; break; }

		// Transfer A to X
		IMMEDIATE(TAX, X = A; SETZN(X);, 2)

		// Transfer A to Y
		IMMEDIATE(TAY, Y = A; SETZN(Y);, 2)

		// Transfer Stack Pointer to X
		IMMEDIATE(TSX, X = stackPointer; SETZN(X);, 2)

		// Transfer X to A
		IMMEDIATE(TXA, A = X; SETZN(A);, 2)

		// Transfer X to Stack Pointer
		IMMEDIATE(TXS, stackPointer = X;, 2)

		// Transfer Y to A
		IMMEDIATE(TYA, A = Y; SETZN(A);, 2)



		// unofficial opcodes
		IMMEDIATE(NOP_E1_1, , 2)
		IMMEDIATE(NOP_E1_2, , 2)
		IMMEDIATE(NOP_E1_3, , 2)
		IMMEDIATE(NOP_E1_4, , 2)
		IMMEDIATE(NOP_E1_5, , 2)
		IMMEDIATE(NOP_E1_6, , 2)

		IMMEDIATE(NOP_E2_1, ++programCounter; , 2)
		IMMEDIATE(NOP_E2_2, ++programCounter; , 2)
		IMMEDIATE(NOP_E2_3, ++programCounter; , 2)
		IMMEDIATE(NOP_E2_4, ++programCounter; , 2)
		IMMEDIATE(NOP_E2_5, ++programCounter; , 2)

		default:
		{
			//message("NES: Unsupported op code encountered. Emulator will now be halted");
			halted = true;
			break;
		}



#undef INDIRECTY
#undef INDIRECTX
#undef BRANCH
#undef ABSOLUTEY
#undef ABSOLUTEX
#undef ABSOLUTE
#undef ZEROY
#undef ZEROX
#undef ZERO
#undef IMMEDIATE_ARG
#undef IMMEDIATE
#undef POPSTACK
#undef PUSHSTACK
#undef SETZN
		};
	}
};
