#include "apu.c"

namespace
{
#include "ColorTable.inl"
#include "Audio+Controller.inl"
#include "Cart.inl"
#include "PPU.inl"
#include "Ram.inl"
#include "CPU.inl"
}

class NESEmu : public GameBase
{
	NES_Cart_Base *cart;
	NES_APUIO apuIO;
	NES_PPU ppu;
	NES_Ram ram;
	NES_CPU cpu;
	bool addOAMCopyCycles;

	GLuint GLTexture;

	struct OnFrameSet
	{
		uint16_t address = 0;
		uint8_t value = 0;
	};
	std::vector<OnFrameSet> onFrameSetList;
	float audioMultiplier;

	static uint8_t GetMemoryFunc(uint16_t address, void *userdata)
	{
		NES_Ram *ram = reinterpret_cast<NES_Ram*>(userdata);
		return ram->Get(address);
	}

	void DoFrame_Internal(void)
	{
		bool inFrameLoop = true;
		while(inFrameLoop && !cpu.halted)
		{
			ppu.Tick(inFrameLoop, cpu.waitingNMI);
			ppu.Tick(inFrameLoop, cpu.waitingNMI);
			ppu.Tick(inFrameLoop, cpu.waitingNMI);
			cpu.Tick(ram);
			apuIO.Tick(cpu.waitingIRQ, cpu.tickSkipCount);

			if(addOAMCopyCycles)
			{
				cpu.tickSkipCount += 513 + !ppu.isEvenFrame;
				addOAMCopyCycles = false;
			}
			cart->OnCycleReset();
		}
	}

public:
	NESEmu() : cart(nullptr), apuIO(GetMemoryFunc, &ram), ram(ppu, apuIO, addOAMCopyCycles, cpu.halted), addOAMCopyCycles(false), GLTexture(0), audioMultiplier(1.0f) {}

	void Init(std::string &config)
	{
		int gettingFileSequence = 0;
		FILE *gameFile = nullptr;
		FILE *stateFile = nullptr;

		SimpleParse(config.data(), 
			[&](char const *single)
			{
				switch(gettingFileSequence++)
				{
				case 0: gameFile = fopen(single, "rb"); break;
				case 1: stateFile = fopen(single, "rb"); break;
				}
			}, [&](char const *key, char const *value)
			{
				if(strcmp(key, "AudioMultiplier") == 0)
					audioMultiplier = std::atof(value);
			}, [&](char const *command, char const *key, char const *value)
			{
				if(strcmp(command, "OnFrameSet") == 0)
				{
					int address = std::atoi(key);
					int ivalue = std::atoi(value);
					if(0 <= address && address < 0xFFFF)
					{
						OnFrameSet temp;
						temp.address = uint16_t(address);
						temp.value = uint8_t(ivalue);
						onFrameSetList.push_back(temp);
					}
				}
			});

		if(gameFile)
		{
			fseek(gameFile, 0, SEEK_END);
			int rom_size = ftell(gameFile);
			uint8_t *rom_data = (uint8_t*)malloc(rom_size);
			rewind(gameFile);
			for(int i=0 ; i<rom_size ; ++i)
			{
				short sample = 0;
				fread(&sample, 1, 1, gameFile);
				rom_data[i] = sample;
			}
			
			if(rom_size < 16)
				return;// false;

			if(!(rom_data[0] == 'N' && rom_data[1] == 'E' && rom_data[2] == 'S' && rom_data[3] == 0x1A))
				return;// false;

			int32_t programChunks = rom_data[4];
			int32_t tileChunks = rom_data[5];

			// trainer not supported until needed
			if((rom_data[6] & 0x04) != 0)
			{
				return;// false;
			}

			if(rom_size < 16 + (programChunks * 16 * 1024) + (tileChunks * 8 * 1024))
				return;// false;

			uint8_t mapper = ((rom_data[6] & 0xF0) >> 4) | (rom_data[7] & 0xF0);
			switch(mapper)
			{
			case 0: cart = new NES_Cart_NROM(rom_data); break;
			case 1: cart = new NES_Cart_SxROM(rom_data); break;
			case 2: cart = new NES_Cart_UxROM(rom_data); break;
			default: return;// false;
			}

			ram.cart = cart;
			ppu.cart = ram.cart;
			cpu.Reset(ram);

			fclose(gameFile);
		}

		// k heres the thing, I'm running out of time for the first event so this is hacky but its enough for SMB
		for(int i=0 ; i<60 ; ++i)
			DoFrame_Internal();
		apuIO.controllerState[0].start = true;
		for(int i=0 ; i<8 ; ++i)
			DoFrame_Internal();
		apuIO.controllerState[0].start = false;

		if(stateFile)
		{
			// cook frames
			// TODO make this configurable
			//for(int i=0 ; i<10 ; ++i)
			//	sb_tick(&game,&emu, &ram);
			//fread(&emu.cpu, sizeof(sb_gb_cpu_t), 1, stateFile);
			//fread(&emu.mem, sizeof(sb_gb_mem_t), 1, stateFile);
			//fread(&emu.lcd, sizeof(sb_lcd_ppu_t), 1, stateFile);
			//fread(&emu.timers, sizeof(sb_timer_t), 1, stateFile);
			fclose(stateFile);
		}
	}

	void InitRender(void)
	{
		GLTexture = CommonVideo::GetTexture(256);
	}

	virtual void Tick(void)
	{
		DoFrame_Internal();
		apuIO.StageAudio();

		for(auto & code : onFrameSetList)
			ram.Set(code.address, code.value, true);
	}

	void SetControllerState(bool a, bool b, bool l, bool r, bool up, bool down, bool left, bool right)
	{
		NES_APUIO::Controller &c = apuIO.controllerState[0];
		c.left = left;
		c.right = right;
		c.up = up;
		c.down = down;
		c.a = a;
		c.b = b;
		c.start = false;
	}

	void SetStartThisFrame(void)
	{
		apuIO.controllerState[0].start = true;
	}

	virtual void Render(void)
	{
		CommonVideo::DrawRenderStuff(GLTexture, 256, &ppu.screen[0][0].r, 256, 240);
	}
	
#if defined(CGS_DEBUG_COMMANDS)
	virtual void SaveState(void) {}
#endif
};
