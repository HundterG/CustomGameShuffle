struct NES_Cart_Base
{
	int32_t programChunks;
	uint8_t *startOfProgram;
	int32_t tileChunks;
	uint8_t *startOfTile;
	uint8_t *data;

	NES_Cart_Base(uint8_t *rom)
	{
		data = rom;
		programChunks = rom[4];
		startOfProgram = rom + 16;
		tileChunks = rom[5];
		startOfTile = startOfProgram + (0x4000 * programChunks);
	}

	~NES_Cart_Base()
	{
		//if(data)
		//	HSTL::Internal::FreeMem(data);
	}

	virtual void Set(uint16_t address, uint8_t value, bool extrinsic) = 0;
	virtual void Get(uint16_t address, uint8_t &value, bool extrinsic) = 0;
	virtual void SetV(uint16_t address, uint8_t value, bool extrinsic) = 0;
	virtual void GetV(uint16_t address, uint8_t &value, bool extrinsic) = 0;
	virtual void SetIO(uint16_t address, uint8_t value){}
	virtual uint8_t GetIO(uint16_t address){ return 0; }
	virtual void OnCycleReset(void) {}
};

struct NES_Cart_NROM : public NES_Cart_Base
{
	uint8_t extraRAM[8 * 1024];
	uint8_t extraVRAM[12 * 1024]; // normally 8kb but made 12kb because I put ppu internal ram on the cart
	uint8_t mirrorType;

	NES_Cart_NROM(uint8_t *rom) : NES_Cart_Base(rom)
	{
		mirrorType = rom[6] & 0x01;
	}

	void Set(uint16_t address, uint8_t value, bool)
	{
		if(0x6000 <= address && address < 0x8000)
			extraRAM[address - 0x6000] = value;
	}
	void Get(uint16_t address, uint8_t &value, bool)
	{
		if(0x6000 <= address && address < 0x8000)
			value = extraRAM[address - 0x6000];
		else if(0x8000 <= address)
		{
			if(programChunks == 1)
				value = startOfProgram[(address - 0x8000) & 0x3FFF];
			else
				value = startOfProgram[address - 0x8000];
		}
	}
	void SetV(uint16_t address, uint8_t value, bool)
	{
		if(0x3000 <= address)
			address -= 0x1000;

		if(mirrorType == 1)
		{
			if(0x2800 <= address && address < 0x3000)
				address -= 0x0800;
		}
		else
		{
			if((0x2400 <= address && address < 0x2800) || (0x2C00 <= address && address < 0x3000))
				address -= 0x0400;
		}

		if(tileChunks == 0)
			extraVRAM[address] = value;
		else if(0x2000 <= address)
			extraVRAM[address] = value;
	}
	void GetV(uint16_t address, uint8_t &value, bool)
	{
		if(0x3000 <= address)
			address -= 0x1000;

		if(mirrorType == 1)
		{
			if(0x2800 <= address && address < 0x3000)
				address -= 0x0800;
		}
		else
		{
			if((0x2400 <= address && address < 0x2800) || (0x2C00 <= address && address < 0x3000))
				address -= 0x0400;
		}

		if(tileChunks == 0)
			value = extraVRAM[address];
		else if(address < 0x2000)
			value = startOfTile[address];
		else
			value = extraVRAM[address];
	}
};

struct NES_Cart_SxROM : public NES_Cart_Base
{
	uint8_t extraRAM[8 * 1024]; // Technically this should be 32KB. No game is reported to use it though in this cart type
	uint8_t extraVRAM[12 * 1024]; // normally 8kb but made 12kb because I put ppu internal ram on the cart
	uint8_t *programBanks[16];
	uint8_t *currentPBanks[2];
	uint8_t *tileBanks[32];
	uint8_t *currentVBanks[2];
	uint8_t currentVBankIndexs[2];
	uint8_t programMapperType;
	uint8_t programMapperIndex;
	uint8_t tileMapperType;
	uint8_t shiftRegister;
	uint8_t shiftRegisterSize;
	uint8_t mirrorType;
	bool registerOpen;

	NES_Cart_SxROM(uint8_t *rom) : NES_Cart_Base(rom)
	{
		programBanks[0] = startOfProgram;
		for(int i=1 ; i<16 ; ++i)
		{
			if(i < programChunks)
				programBanks[i] = programBanks[i-1] + (16 * 1024);
			else
				programBanks[i] = nullptr;
		}

		tileBanks[0] = startOfTile;
		for(int i=1 ; i<32 ; ++i)
		{
			if(i < tileChunks)
				tileBanks[i] = tileBanks[i-1] + (4 * 1024);
			else
				tileBanks[i] = nullptr;
		}

		currentPBanks[0] = startOfProgram;
		currentPBanks[1] = programBanks[programChunks - 1];
		currentVBanks[0] = tileBanks[0];
		currentVBanks[1] = tileBanks[0];
		currentVBankIndexs[0] = 0;
		currentVBankIndexs[1] = 0;
		programMapperType = 3;
		programMapperIndex = 0;
		tileMapperType = 0;

		shiftRegister = 0;
		shiftRegisterSize = 0;
		mirrorType = rom[6] & 0x01;
		registerOpen = true;
	}

	void SetBanks(void)
	{
		if(programMapperType <= 1)
		{
			uint8_t index = programMapperIndex & 0xFE;
			currentPBanks[0] = programBanks[index];
			currentPBanks[1] = programBanks[index + 1];
		}
		else if(programMapperType == 2)
		{
			currentPBanks[0] = startOfProgram;
			currentPBanks[1] = programBanks[programMapperIndex];
		}
		else
		{
			currentPBanks[0] = programBanks[programMapperIndex];
			currentPBanks[1] = programBanks[programChunks - 1];
		}
	}

	void Set(uint16_t address, uint8_t value, bool extrinsic)
	{
		if(0x8000 <= address && registerOpen && extrinsic == false)
		{
			// The Magic - NGL, this seems really dangerous
			if((value & 0x80) == 0)
			{
				shiftRegister = (shiftRegister >> 1) | ((value & 0x01) << 4);
				++shiftRegisterSize;

				if(shiftRegisterSize == 5)
				{
					if(address < 0xA000)
					{
						// modes
						// mirroring?
						tileMapperType = (shiftRegister & 0x10) >> 4;
						programMapperType = (shiftRegister & 0x0C) >> 2;
						SetBanks();
						if(tileMapperType == 0)
						{
							currentVBanks[0] = tileBanks[(currentVBankIndexs[0] & 0xFE) % tileChunks];
							currentVBanks[1] = tileBanks[(currentVBankIndexs[0] | 0x01) % tileChunks];
						}
						else
						{
							currentVBanks[0] = tileBanks[currentVBankIndexs[0] % tileChunks];
							currentVBanks[1] = tileBanks[currentVBankIndexs[1] % tileChunks];
						}
					}
					else if(address < 0xC000)
					{
						// first vram bank
						currentVBankIndexs[0] = shiftRegister;
						if(tileMapperType == 0)
						{
							currentVBanks[0] = tileBanks[(currentVBankIndexs[0] & 0xFE) % tileChunks];
							currentVBanks[1] = tileBanks[(currentVBankIndexs[0] | 0x01) % tileChunks];
						}
						else
							currentVBanks[0] = tileBanks[currentVBankIndexs[0] % tileChunks];
					}
					else if(address < 0xE000)
					{
						// second vram bank
						currentVBankIndexs[1] = shiftRegister;
						if(tileMapperType != 0)
							currentVBanks[1] = tileBanks[currentVBankIndexs[1] % tileChunks];
					}
					else
					{
						// program bank
						// extra ram is set here too with 0x10 but it is always active here
						programMapperIndex = shiftRegister & 0x0F;
						SetBanks();
					}

					shiftRegister = 0;
					shiftRegisterSize = 0;
				}
			}
			else
			{
				// reset
				programMapperType = 3;
				shiftRegister = 0;
				shiftRegisterSize = 0;
				SetBanks();
			}

			// guard against multiple sets on one cpu cycle
			registerOpen = false;
		}
		else if(0x6000 <= address && address < 0x8000)
			extraRAM[address - 0x6000] = value;
	}
	void Get(uint16_t address, uint8_t &value, bool)
	{
		if(0xC000 <= address)
			value = currentPBanks[1][(address - 0xC000)];
		else if(0x8000 <= address)
			value = currentPBanks[0][(address - 0x8000)];
		else if(0x6000 <= address)
			value = extraRAM[address - 0x6000];
	}
	void OnCycleReset(void)
	{
		registerOpen = true;
	}
	void SetV(uint16_t address, uint8_t value, bool)
	{
		if(0x3000 <= address)
			address -= 0x1000;

		if(mirrorType == 1)
		{
			if(0x2800 <= address && address < 0x3000)
				address -= 0x0800;
		}
		else
		{
			if((0x2400 <= address && address < 0x2800) || (0x2C00 <= address && address < 0x3000))
				address -= 0x0400;
		}

		//if(tileChunks == 0)
		if(address < 0x3000)
			extraVRAM[address] = value;
	}
	void GetV(uint16_t address, uint8_t &value, bool)
	{
		if(0x3000 <= address)
			address -= 0x1000;

		if(mirrorType == 1)
		{
			if(0x2800 <= address && address < 0x3000)
				address -= 0x0800;
		}
		else
		{
			if((0x2400 <= address && address < 0x2800) || (0x2C00 <= address && address < 0x3000))
				address -= 0x0400;
		}

		if(tileChunks == 0)
			value = extraVRAM[address];
		else
		{
			if(address < 0x1000)
				value = currentVBanks[0][address];
			else if(address < 0x2000)
				value = currentVBanks[1][address - 0x1000];
			else if(address < 0x3000)
				value = extraVRAM[address];
		}
	}
};

struct NES_Cart_UxROM : public NES_Cart_Base
{
	uint8_t extraVRAM[12 * 1024]; // normally 8kb but made 12kb because I put ppu internal ram on the cart
	uint8_t mirrorType;
	uint8_t *programBanks[256];
	uint8_t bank;

	NES_Cart_UxROM(uint8_t *rom) : NES_Cart_Base(rom)
	{
		mirrorType = rom[6] & 0x01;
		programBanks[0] = startOfProgram;
		for(int i=1 ; i<256 ; ++i)
		{
			if(i < programChunks)
				programBanks[i] = programBanks[i-1] + (16 * 1024);
			else
				programBanks[i] = nullptr;
		}
		bank = 0;
	}
	
	void Set(uint16_t address, uint8_t value, bool extrinsic)
	{
		if(extrinsic)
			return;
		
		if(0x8000 <= address)
			bank = value;
	}
	void Get(uint16_t address, uint8_t &value, bool)
	{
		if(0xC000 <= address)
			value = programBanks[programChunks-1][(address - 0xC000)];
		else if(0x8000 <= address)
			value = programBanks[bank][(address - 0x8000)];
	}
	void SetV(uint16_t address, uint8_t value, bool)
	{
		if(0x3000 <= address)
			address -= 0x1000;

		if(mirrorType == 1)
		{
			if(0x2800 <= address && address < 0x3000)
				address -= 0x0800;
		}
		else
		{
			if((0x2400 <= address && address < 0x2800) || (0x2C00 <= address && address < 0x3000))
				address -= 0x0400;
		}

		extraVRAM[address] = value;
	}
	void GetV(uint16_t address, uint8_t &value, bool)
	{
		if(0x3000 <= address)
			address -= 0x1000;

		if(mirrorType == 1)
		{
			if(0x2800 <= address && address < 0x3000)
				address -= 0x0800;
		}
		else
		{
			if((0x2400 <= address && address < 0x2800) || (0x2C00 <= address && address < 0x3000))
				address -= 0x0400;
		}

		value = extraVRAM[address];
	}
};

struct NES_Cart_CNROM : public NES_Cart_NROM
{
	uint8_t *tileBanks[4];
	uint8_t *currentVBank;

	NES_Cart_CNROM(uint8_t *rom) : NES_Cart_NROM(rom)
	{
		currentVBank = tileBanks[0] = startOfTile;
		for(int i=1 ; i<4 ; ++i)
		{
			if(i < tileChunks)
				tileBanks[i] = tileBanks[i-1] + (8 * 1024);
			else
				tileBanks[i] = nullptr;
		}
	}

	void Set(uint16_t address, uint8_t value, bool e)
	{
		if(0x8000 <= address)
		{
			uint8_t progValue = 0;
			if(programChunks == 1)
				progValue = startOfProgram[(address - 0x8000) & 0x3FFF];
			else
				progValue = startOfProgram[address - 0x8000];
			value = (value & progValue) & 0x03;
			currentVBank = tileBanks[value];
		}
		else
			NES_Cart_NROM::Set(address, value, e);
	}
	void GetV(uint16_t address, uint8_t &value, bool e)
	{
		if(address < 0x2000)
			value = currentVBank[address];
		else
			NES_Cart_NROM::GetV(address, value, e);
	}
};

struct NES_Cart_Namco_210 : public NES_Cart_Base
{
	uint8_t extraRAM[8 * 1024];
	uint8_t extraVRAM[12 * 1024]; // normally 8kb but made 12kb because I put ppu internal ram on the cart
	uint8_t *programBanks[64];
	uint8_t *currentPBanks[4];
	uint8_t *tileBanks[256];
	uint8_t *currentVBanks[8];

	bool namcoRegister = false;

	NES_Cart_Namco_210(uint8_t *rom) : NES_Cart_Base(rom)
	{
		programBanks[0] = startOfProgram;
		for(int i=1 ; i<64 ; ++i)
		{
			if(i < programChunks*2)
				programBanks[i] = programBanks[i-1] + (8 * 1024);
			else
				programBanks[i] = nullptr;
		}

		tileBanks[0] = startOfTile;
		for(int i=1 ; i<256 ; ++i)
		{
			if(i < tileChunks*8)
				tileBanks[i] = tileBanks[i-1] + (1024);
			else
				tileBanks[i] = nullptr;
		}

		currentPBanks[0] = startOfProgram;
		currentPBanks[1] = startOfProgram;
		currentPBanks[2] = startOfProgram;
		currentPBanks[3] = programBanks[(programChunks*2) - 1];
		currentVBanks[0] = tileBanks[0];
		currentVBanks[1] = tileBanks[0];
		currentVBanks[2] = tileBanks[0];
		currentVBanks[3] = tileBanks[0];
		currentVBanks[4] = tileBanks[0];
		currentVBanks[5] = tileBanks[0];
		currentVBanks[6] = tileBanks[0];
		currentVBanks[7] = tileBanks[0];
	}

	void Set(uint16_t address, uint8_t value, bool extrinsic)
	{
		if(0x8000 <= address)
		{
			if(address < 0xC000)
			{
				address = (address - 0x8000) / 0x0800;
				currentVBanks[address] = tileBanks[value];
			}
			else if(address < 0xC800)
			{} // Enables prog ram. Ignored because its always enabled
			else if(0xE000 <= address && address < 0xF800)
			{
				address = (address - 0xE000) / 0x0800;
				currentPBanks[address] = programBanks[value & 0x3F];
			}
		}
	}

	void Get(uint16_t address, uint8_t &value, bool)
	{
		if(0xE000 <= address)
			value = currentPBanks[3][(address - 0xE000)];
		else if(0xC000 <= address)
			value = currentPBanks[2][(address - 0xC000)];
		else if(0xA000 <= address)
			value = currentPBanks[1][(address - 0xA000)];
		else if(0x8000 <= address)
			value = currentPBanks[0][(address - 0x8000)];
		else if(0x6000 <= address)
			value = extraRAM[address - 0x6000];
	}

	void SetV(uint16_t address, uint8_t value, bool)
	{
		if(0x3000 <= address)
			address -= 0x1000;

		if(address < 0x3000)
			extraVRAM[address] = value;
	}

	void GetV(uint16_t address, uint8_t &value, bool)
	{
		if(0x3000 <= address)
			address -= 0x1000;

		if(tileChunks == 0)
			value = extraVRAM[address];
		else
		{
			if(address < 0x0400)
				value = currentVBanks[0][address];
			else if(address < 0x0800)
				value = currentVBanks[1][address - 0x0400];
			else if(address < 0x0C00)
				value = currentVBanks[2][address - 0x0800];
			else if(address < 0x1000)
				value = currentVBanks[3][address - 0x0C00];
			else if(address < 0x1400)
				value = currentVBanks[4][address - 0x1000];
			else if(address < 0x1800)
				value = currentVBanks[5][address - 0x1400];
			else if(address < 0x1C00)
				value = currentVBanks[6][address - 0x1800];
			else if(address < 0x2000)
				value = currentVBanks[7][address - 0x1C00];
			else if(address < 0x3000)
				value = extraVRAM[address];
		}
	}

	void SetIO(uint16_t address, uint8_t value)
	{
		if(address == 0x16)
			namcoRegister = ((value & 0x01) == 0) ? true : false;
	}

	uint8_t GetIO(uint16_t address)
	{
		if(address == 0x17)
		{
			uint8_t value = namcoRegister;
			return value << 1;
		}
		return 0;
	}
};
