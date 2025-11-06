#define SE_AUDIO_SAMPLE_RATE Sound::GlobalSampleRate
#include "gba.h"
#include "gb.h"

class GBEmu : public GameBase
{
	sb_emu_state_t game;
	sb_gb_t emu;
	gb_scratch_t ram;
	GLuint GLTexture = 0;

	struct OnFrameSet
	{
		uint16_t address = 0;
		uint8_t value = 0;
	};
	std::vector<OnFrameSet> onFrameSetList;
	float audioMultiplier = 0.2f;

public:
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
			game.rom_size = ftell(gameFile);
			game.rom_data = (uint8_t*)malloc(game.rom_size);
			rewind(gameFile);
			for(int i=0 ; i<game.rom_size ; ++i)
			{
				short sample = 0;
				fread(&sample, 1, 1, gameFile);
				game.rom_data[i] = sample;
			}
			std::memset(&emu, 0, sizeof(emu));
			sb_load_rom(&game,&emu,&ram);
			game.system = SYSTEM_GB;
			game.rom_loaded = true;
			game.run_mode = SB_MODE_RUN;
			game.step_frames = 1;
			game.render_frame = true;
			fclose(gameFile);
		}

		if(stateFile)
		{
			// cook frames
			// TODO make this configurable
			for(int i=0 ; i<10 ; ++i)
				sb_tick(&game,&emu, &ram);
			fread(&emu.cpu, sizeof(sb_gb_cpu_t), 1, stateFile);
			fread(&emu.mem, sizeof(sb_gb_mem_t), 1, stateFile);
			fread(&emu.lcd, sizeof(sb_lcd_ppu_t), 1, stateFile);
			fread(&emu.timers, sizeof(sb_timer_t), 1, stateFile);
			fclose(stateFile);
		}
	}

	void InitRender(void)
	{
		GLTexture = CommonVideo::GetTexture(256);
	}

	void Tick(void)
	{
		uint8_t palette[4*3] = { 0xff,0xff,0xff,0xAA,0xAA,0xAA,0x55,0x55,0x55,0x00,0x00,0x00 };
		for(int i=0;i<12;++i)emu.dmg_palette[i]=palette[i];
		game.audio_ring_buff.read_ptr = 0;
		game.audio_ring_buff.write_ptr = 0;
		for(int i=0;i<SB_AUDIO_RING_BUFFER_SIZE;++i)game.audio_ring_buff.data[i]=0;

		sb_tick(&game,&emu, &ram);

		int16_t stageBuff[1024 + 4];
		int readSize = 0, writeSize = 0;
		int max = sb_ring_buffer_size(&game.audio_ring_buff) / 2;
		for(; readSize < max && writeSize < 1024 ; ++readSize)
		{
			if(readSize % 64 == 0)
			{
				stageBuff[writeSize] = stageBuff[writeSize-1];
				++writeSize;
			}
			stageBuff[writeSize] = int16_t((game.audio_ring_buff.data[(game.audio_ring_buff.read_ptr)%SB_AUDIO_RING_BUFFER_SIZE] + game.audio_ring_buff.data[(game.audio_ring_buff.read_ptr+1)%SB_AUDIO_RING_BUFFER_SIZE]) * audioMultiplier);
			++writeSize;
			game.audio_ring_buff.read_ptr += 2;
		}
		StageAudio(stageBuff, writeSize);

		for(auto & code : onFrameSetList)
			sb_store8(&emu, code.address, code.value);
	}

	void SetControllerState(bool a, bool b, bool l, bool r, bool up, bool down, bool left, bool right)
	{
		game.joy.inputs[SE_KEY_LEFT] = left;
		game.joy.inputs[SE_KEY_RIGHT] = right;
		game.joy.inputs[SE_KEY_UP] = up;
		game.joy.inputs[SE_KEY_DOWN] = down;
		game.joy.inputs[SE_KEY_A] = a;
		game.joy.inputs[SE_KEY_B] = b;
		game.joy.inputs[SE_KEY_START] = false;
	}

	void SetStartThisFrame(void)
	{
		game.joy.inputs[SE_KEY_START] = true;
	}

	void Render(void)
	{
		CommonVideo::DrawRenderStuff(GLTexture, 256, ram.framebuffer, SB_LCD_W, SB_LCD_H);
	}

#if defined(CGS_DEBUG_COMMANDS)
	void SaveState(void)
	{
		char buffer[sizeof(sb_gb_cpu_t) + sizeof(sb_gb_mem_t) + sizeof(sb_lcd_ppu_t) + sizeof(sb_timer_t)];
		memcpy(buffer, &emu.cpu, sizeof(sb_gb_cpu_t));
		memcpy(buffer + sizeof(sb_gb_cpu_t), &emu.mem, sizeof(sb_gb_mem_t));
		memcpy(buffer + sizeof(sb_gb_cpu_t) + sizeof(sb_gb_mem_t), &emu.lcd, sizeof(sb_lcd_ppu_t));
		memcpy(buffer + sizeof(sb_gb_cpu_t) + sizeof(sb_gb_mem_t) + sizeof(sb_lcd_ppu_t), &emu.timers, sizeof(sb_timer_t));
		QueueDebugDownload(buffer, sizeof(buffer));
	}
#endif
};

class GBAEmu : public GameBase
{
	sb_emu_state_t game;
	gba_t emu;
	gba_scratch_t ram;
	GLuint GLTexture = 0;

	struct OnFrameSet
	{
		uint32_t address = 0;
		uint8_t value = 0;
	};
	std::vector<OnFrameSet> onFrameSetList;
	float audioMultiplier = 0.25f;

public:
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
					{
						OnFrameSet temp;
						temp.address = uint32_t(address);
						temp.value = uint8_t(ivalue);
						onFrameSetList.push_back(temp);
					}
				}
			});

		if(gameFile)
		{
			fseek(gameFile, 0, SEEK_END);
			game.rom_size = ftell(gameFile);
			game.rom_data = (uint8_t*)malloc(game.rom_size);
			rewind(gameFile);
			for(int i=0 ; i<game.rom_size ; ++i)
			{
				short sample = 0;
				fread(&sample, 1, 1, gameFile);
				game.rom_data[i] = sample;
			}
			std::memset(&emu, 0, sizeof(emu));
			gba_load_rom(&game,&emu,&ram);
			game.system = SYSTEM_GBA;
			game.rom_loaded = true;
			game.run_mode = SB_MODE_RUN;
			game.step_frames = 1;
			game.render_frame = true;
			fclose(gameFile);
		}

		if(stateFile)
		{
			// cook frames
			// TODO make this configurable
			for(int i=0 ; i<10 ; ++i)
				gba_tick(&game,&emu, &ram);
			fread(&emu.cpu, sizeof(arm7_t), 1, stateFile);
			fread(&emu.mem, sizeof(gba_mem_t), 1, stateFile);
			fclose(stateFile);
		}
	}

	void InitRender(void)
	{
		GLTexture = CommonVideo::GetTexture(256);
	}

	void Tick(void)
	{
		game.audio_ring_buff.read_ptr = 0;
		game.audio_ring_buff.write_ptr = 0;
		for(int i=0;i<SB_AUDIO_RING_BUFFER_SIZE;++i)game.audio_ring_buff.data[i]=0;

		gba_tick(&game,&emu, &ram);

		// stage draw buffer ram.framebuffer GBA_LCD_W GBA_LCD_H

		int16_t stageBuff[1024 + 4];
		int readSize = 0, writeSize = 0;
		int max = sb_ring_buffer_size(&game.audio_ring_buff) / 2;
		for(; readSize < max && writeSize < 1024 ; ++readSize)
		{
			if(readSize % 64 == 0)
			{
				stageBuff[writeSize] = stageBuff[writeSize-1];
				++writeSize;
			}
			stageBuff[writeSize] = int16_t((game.audio_ring_buff.data[(game.audio_ring_buff.read_ptr)%SB_AUDIO_RING_BUFFER_SIZE] + game.audio_ring_buff.data[(game.audio_ring_buff.read_ptr+1)%SB_AUDIO_RING_BUFFER_SIZE]) * audioMultiplier);
			++writeSize;
			game.audio_ring_buff.read_ptr += 2;
		}
		StageAudio(stageBuff, writeSize);

		for(auto & code : onFrameSetList)
			gba_store8_debug(&emu, code.address, code.value);
	}

	void SetControllerState(bool a, bool b, bool l, bool r, bool up, bool down, bool left, bool right)
	{
		game.joy.inputs[SE_KEY_LEFT] = left;
		game.joy.inputs[SE_KEY_RIGHT] = right;
		game.joy.inputs[SE_KEY_UP] = up;
		game.joy.inputs[SE_KEY_DOWN] = down;
		game.joy.inputs[SE_KEY_A] = a;
		game.joy.inputs[SE_KEY_B] = b;
		game.joy.inputs[SE_KEY_L] = l;
		game.joy.inputs[SE_KEY_R] = r;
		game.joy.inputs[SE_KEY_START] = false;
	}

	void SetStartThisFrame(void)
	{
		game.joy.inputs[SE_KEY_START] = true;
	}

	void Render(void)
	{
		CommonVideo::DrawRenderStuff(GLTexture, 256, ram.framebuffer, GBA_LCD_W, GBA_LCD_H);
	}

#if defined(CGS_DEBUG_COMMANDS)
	void SaveState(void)
	{
		char buffer[sizeof(arm7_t) + sizeof(gba_mem_t)];
		memcpy(buffer, &emu.cpu, sizeof(arm7_t));
		memcpy(buffer + sizeof(arm7_t), &emu.mem, sizeof(gba_mem_t));
		QueueDebugDownload(buffer, sizeof(buffer));
	}
#endif
};
