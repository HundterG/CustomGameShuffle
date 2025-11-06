struct NES_Ram
{
	NES_Ram(NES_PPU &ppu, NES_APUIO &apuIO, bool &DMACycleFlag, bool &CPUHaltFlag) : PPU(ppu), APUIO(apuIO), cart(nullptr), lastGet(0), addDMACyclesToCPU(DMACycleFlag), haltCPU(CPUHaltFlag) {}
	uint8_t mainRAM[2 * 1024];
	NES_PPU &PPU;
	NES_APUIO &APUIO;
	NES_Cart_Base *cart;
	uint8_t lastGet;
	bool &addDMACyclesToCPU;
	bool &haltCPU;

	void Set(uint16_t address, uint8_t value, bool extrinsic = false)
	{
		if(address <= 0x1FFF)
			mainRAM[address & 0x07FF] = value;
		else if(address <= 0x3FFF)
			PPU.Set(address & 0x07, value, extrinsic);
		else if(address == 0x4014 && extrinsic == false)
		{
			if(value == 0x40)
			{
				haltCPU = true;
				return;
			}
			address = uint16_t(value) << 8;
			uint8_t buffer[256];
			for(int i=0 ; i<256 ; ++i)
			{
				buffer[i] = Get(address, true);
				++address;
			}
			PPU.DoOAMCopy(buffer);
			addDMACyclesToCPU = true;
		}
		else if(address <= 0x401F)
			APUIO.Set(address - 0x4000, value, extrinsic);
		else if(cart != nullptr)
			cart->Set(address, value, extrinsic);
	}

	uint8_t Get(uint16_t address, bool extrinsic = false)
	{
		uint8_t thisGet = lastGet;
		if(address <= 0x1FFF)
			thisGet = mainRAM[address & 0x07FF];
		else if(address <= 0x3FFF)
			PPU.Get(address & 0x07, thisGet, extrinsic);
		else if(address <= 0x401F)
			APUIO.Get(address - 0x4000, thisGet, extrinsic);
		else if(cart != nullptr)
			cart->Get(address, thisGet, extrinsic);

		if(!extrinsic)
			lastGet = thisGet;
		
		return thisGet;
	}
};