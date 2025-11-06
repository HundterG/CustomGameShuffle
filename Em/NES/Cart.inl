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
							currentVBanks[0] = tileBanks[currentVBankIndexs[0] & 0xFE];
							currentVBanks[1] = tileBanks[currentVBankIndexs[0] | 0x01];
						}
						else
						{
							currentVBanks[0] = tileBanks[currentVBankIndexs[0]];
							currentVBanks[1] = tileBanks[currentVBankIndexs[1]];
						}
					}
					else if(address < 0xC000)
					{
						// first vram bank
						currentVBankIndexs[0] = shiftRegister;
						if(tileMapperType == 0)
						{
							currentVBanks[0] = tileBanks[currentVBankIndexs[0] & 0xFE];
							currentVBanks[1] = tileBanks[currentVBankIndexs[0] | 0x01];
						}
						else
							currentVBanks[0] = tileBanks[currentVBankIndexs[0]];
					}
					else if(address < 0xE000)
					{
						// second vram bank
						currentVBankIndexs[1] = shiftRegister;
						if(tileMapperType != 0)
							currentVBanks[1] = tileBanks[currentVBankIndexs[1]];
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
