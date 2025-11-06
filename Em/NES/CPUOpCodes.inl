enum class NES_CPU_OpCode
{
	// Add with Carry
	ADC = 0x69,
	// Add with Carry - Zero Page
	ADC_Z = 0x65,
	// Add with Carry - Zero Page X
	ADC_ZX = 0x75,
	// Add with Carry - Absolute
	ADC_A = 0x6D,
	// Add with Carry - Absolute X
	ADC_AX = 0x7D,
	// Add with Carry - Absolute Y
	ADC_AY = 0x79,
	// Add with Carry - Indirect X
	ADC_IX = 0x61,
	// Add with Carry - Indirect Y
	ADC_IY = 0x71,

	// Bitwise AND
	AND = 0x29,
	// Bitwise AND - Zero Page
	AND_Z = 0x25,
	// Bitwise AND - Zero Page X
	AND_ZX = 0x35,
	// Bitwise AND - Absolute
	AND_A = 0x2D,
	// Bitwise AND - Absolute X
	AND_AX = 0x3D,
	// Bitwise AND - Absolute Y
	AND_AY = 0x39,
	// Bitwise AND - Indirect X
	AND_IX = 0x21,
	// Bitwise AND - Indirect Y
	AND_IY = 0x31,

	// Arithmetic Shift Left
	ASL = 0x0A,
	// Arithmetic Shift Left - Zero Page
	ASL_Z = 0x06,
	// Arithmetic Shift Left - Zero Page X
	ASL_ZX = 0x16,
	// Arithmetic Shift Left - Absolute
	ASL_A = 0x0E,
	// Arithmetic Shift Left - Absolute X
	ASL_AX = 0x1E,

	// Branch if Carry Clear
	BCC = 0x90,

	// Branch if Carry Set
	BCS = 0xB0,

	// Branch if Equal
	BEQ = 0xF0,

	// Bit Test - Zero Page
	BIT_Z = 0x24,
	// Bit Test - Absolute
	BIT_A = 0x2C,

	// Branch if Minus
	BMI = 0x30,

	// Branch if Not Equal
	BNE = 0xD0,

	// Branch if Plus
	BPL = 0x10,

	// Break - Software Interrupt
	BRK = 0x00,

	// Branch if Overflow Clear
	BVC = 0x50,

	// Branch if Overflow Set
	BVS = 0x70,

	// Clear Carry Flag
	CLC = 0x18,

	// Clear Decimal Flag
	CLD = 0xD8,

	// Clear Interrupt Disable Flag
	CLI = 0x58,

	// Clear Overflow Flag
	CLV = 0xB8,

	// Compare A
	CMP = 0xC9,
	// Compare A - Zero Page
	CMP_Z = 0xC5,
	// Compare A - Zero Page X
	CMP_ZX = 0xD5,
	// Compare A - Absolute
	CMP_A = 0xCD,
	// Compare A - Absolute X
	CMP_AX = 0xDD,
	// Compare A - Absolute Y
	CMP_AY = 0xD9,
	// Compare A - Indirect X
	CMP_IX = 0xC1,
	// Compare A - Indirect Y
	CMP_IY = 0xD1,

	// Compare X
	CPX = 0xE0,
	// Compare X - Zero Page
	CPX_Z = 0xE4,
	// Compare X - Absolute
	CPX_A = 0xEC,

	// Compare Y
	CPY = 0xC0,
	// Compare Y - Zero Page
	CPY_Z = 0xC4,
	// Compare Y - Absolute
	CPY_A = 0xCC,

	// Decrement Memory - Zero Page
	DEC_Z = 0xC6,
	// Decrement Memory - Zero Page X
	DEC_ZX = 0xD6,
	// Decrement Memory - Absolute
	DEC_A = 0xCE,
	// Decrement Memory - Absolute X
	DEC_AX = 0xDE,

	// Decrement X
	DEX = 0xCA,

	// Decrement Y
	DEY = 0x88,

	// Bitwise Exclusive OR
	EOR = 0x49,
	// Bitwise Exclusive OR - Zero Page
	EOR_Z = 0x45,
	// Bitwise Exclusive OR - Zero Page X
	EOR_ZX = 0x55,
	// Bitwise Exclusive OR - Absolute
	EOR_A = 0x4D,
	// Bitwise Exclusive OR - Absolute X
	EOR_AX = 0x5D,
	// Bitwise Exclusive OR - Absolute Y
	EOR_AY = 0x59,
	// Bitwise Exclusive OR - Indirect X
	EOR_IX = 0x41,
	// Bitwise Exclusive OR - Indirect Y
	EOR_IY = 0x51,

	// Increment Memory - Zero Page
	INC_Z = 0xE6,
	// Increment Memory - Zero Page X
	INC_ZX = 0xF6,
	// Increment Memory - Absolute
	INC_A = 0xEE,
	// Increment Memory - Absolute X
	INC_AX = 0xFE,

	// Increment X
	INX = 0xE8,

	// Increment Y
	INY = 0xC8,

	// Jump - Absolute
	JMP_A = 0x4C,
	// Jump - Indirect
	JMP_I = 0x6C,

	// Jump to Subroutine - Absolute
	JSR_A = 0x20,

	// Load A
	LDA = 0xA9,
	// Load A - Zero Page
	LDA_Z = 0xA5,
	// Load A - Zero Page X
	LDA_ZX = 0xB5,
	// Load A - Absolute
	LDA_A = 0xAD,
	// Load A - Absolute X
	LDA_AX = 0xBD,
	// Load A - Absolute Y
	LDA_AY = 0xB9,
	// Load A - Indirect X
	LDA_IX = 0xA1,
	// Load A - Indirect Y
	LDA_IY = 0xB1,

	// Load X
	LDX = 0xA2,
	// Load X - Zero Page
	LDX_Z = 0xA6,
	// Load X - Zero Page Y
	LDX_ZY = 0xB6,
	// Load X - Absolute
	LDX_A = 0xAE,
	// Load X - Absolute Y
	LDX_AY = 0xBE,

	// Load Y
	LDY = 0xA0,
	// Load Y - Zero Page
	LDY_Z = 0xA4,
	// Load Y - Zero Page X
	LDY_ZX = 0xB4,
	// Load Y - Absolute
	LDY_A = 0xAC,
	// Load Y - Absolute X
	LDY_AX = 0xBC,

	// Logical Shift Right
	LSR = 0x4A,
	// Logical Shift Right - Zero Page
	LSR_Z = 0x46,
	// Logical Shift Right - Zero Page X
	LSR_ZX = 0x56,
	// Logical Shift Right - Absolute
	LSR_A = 0x4E,
	// Logical Shift Right - Absolute X
	LSR_AX = 0x5E,

	// No Operation
	NOP = 0xEA,

	// Bitwise OR
	ORA = 0x09,
	// Bitwise OR - Zero Page
	ORA_Z = 0x05,
	// Bitwise OR - Zero Page X
	ORA_ZX = 0x15,
	// Bitwise OR - Absolute
	ORA_A = 0x0D,
	// Bitwise OR - Absolute X
	ORA_AX = 0x1D,
	// Bitwise OR - Absolute Y
	ORA_AY = 0x19,
	// Bitwise OR - Indirect X
	ORA_IX = 0x01,
	// Bitwise OR - Indirect Y
	ORA_IY = 0x11,

	// Push A
	PHA = 0x48,

	// Push Processor State
	PHP = 0x08,

	// Pop A
	PLA = 0x68,

	// Pop Processor State
	PLP = 0x28,

	// Rotate Left
	ROL = 0x2A,
	// Rotate Left - Zero Page
	ROL_Z = 0x26,
	// Rotate Left - Zero Page X
	ROL_ZX = 0x36,
	// Rotate Left - Absolute
	ROL_A = 0x2E,
	// Rotate Left - Absolute X
	ROL_AX = 0x3E,

	// Rotate Right
	ROR = 0x6A,
	// Rotate Right - Zero Page
	ROR_Z = 0x66,
	// Rotate Right - Zero Page X
	ROR_ZX = 0x76,
	// Rotate Right - Absolute
	ROR_A = 0x6E,
	// Rotate Right - Absolute X
	ROR_AX = 0x7E,

	// Return from Interrupt
	RTI = 0x40,

	// Return from Subroutine
	RTS = 0x60,

	// Subtract with Carry
	SBC = 0xE9,
	// Subtract with Carry - Zero Page
	SBC_Z = 0xE5,
	// Subtract with Carry - Zero Page X
	SBC_ZX = 0xF5,
	// Subtract with Carry - Absolute
	SBC_A = 0xED,
	// Subtract with Carry - Absolute X
	SBC_AX = 0xFD,
	// Subtract with Carry - Absolute Y
	SBC_AY = 0xF9,
	// Subtract with Carry - Indirect X
	SBC_IX = 0xE1,
	// Subtract with Carry - Indirect Y
	SBC_IY = 0xF1,

	// Set Carry
	SEC = 0x38,

	// Set Decimal
	SED = 0xF8,

	// Set Interrupt Disable
	SEI = 0x78,

	// Store A - Zero Page
	STA_Z = 0x85,
	// Store A - Zero Page X
	STA_ZX = 0x95,
	// Store A - Absolute
	STA_A = 0x8D,
	// Store A - Absolute X
	STA_AX = 0x9D,
	// Store A - Absolute Y
	STA_AY = 0x99,
	// Store A - Indirect X
	STA_IX = 0x81,
	// Store A - Indirect Y
	STA_IY = 0x91,

	// Store X - Zero Page
	STX_Z = 0x86,
	// Store X - Zero Page Y
	STX_ZY = 0x96,
	// Store X - Absolute
	STX_A = 0x8E,

	// Store Y - Zero Page
	STY_Z = 0x84,
	// Store Y - Zero Page X
	STY_ZX = 0x94,
	// Store Y - Absolute
	STY_A = 0x8C,

	// Transfer A to X
	TAX = 0xAA,

	// Transfer A to Y
	TAY = 0xA8,

	// Transfer Stack Pointer to X
	TSX = 0xBA,

	// Transfer X to A
	TXA = 0x8A,

	// Transfer X to Stack Pointer
	TXS = 0x9A,

	// Transfer Y to A
	TYA = 0x98,



	// UnOfficial OpCodes
	// After close inspection, I have decided not to implement these
	// They seem more like glitches in the hardware rather than hidden functions
	// I will add the extra NOP codes though
	NOP_E1_1 = 0x1A,
	NOP_E1_2 = 0x3A,
	NOP_E1_3 = 0x5A,
	NOP_E1_4 = 0x7A,
	NOP_E1_5 = 0xDA,
	NOP_E1_6 = 0xFA,

	NOP_E2_1 = 0x80,
	NOP_E2_2 = 0x82,
	NOP_E2_3 = 0x89,
	NOP_E2_4 = 0xC2,
	NOP_E2_5 = 0xE2,
};
