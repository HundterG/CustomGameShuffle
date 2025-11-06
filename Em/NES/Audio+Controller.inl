// Audio and controllers are combined for some reason
struct NES_APUIO
{
	struct Controller
	{
		bool left = false, right = false, up = false, down = false, a = false, b = false, start = false, select = false;
		bool aOnly = false;
		uint8_t index = 0;
	} controllerState[2];

	APU apu;

	NES_APUIO(uint8_t (*GetMemoryFunc)(uint16_t address, void *userdata), void *userdata)
	{
		init_APU(&apu);
		apu.GetMemoryFunc = GetMemoryFunc;
		apu.userdata = userdata;
	}

	void Set(uint16_t address, uint8_t value, bool extrinsic)
	{
		if(extrinsic)
			return;

		switch(address)
		{
		case 0x00: set_pulse_ctrl(&apu.pulse1, value); break;
		case 0x01: set_pulse_sweep(&apu.pulse1, value); break;
		case 0x02: set_pulse_timer(&apu.pulse1, value); break;
		case 0x03: set_pulse_length_counter(&apu.pulse1, value); break;

		case 0x04: set_pulse_ctrl(&apu.pulse2, value); break;
		case 0x05: set_pulse_sweep(&apu.pulse2, value); break;
		case 0x06: set_pulse_timer(&apu.pulse2, value); break;
		case 0x07: set_pulse_length_counter(&apu.pulse2, value); break;

		case 0x08: set_tri_counter(&apu.triangle, value); break;
		case 0x0A: set_tri_timer_low(&apu.triangle, value); break;
		case 0x0B: set_tri_length(&apu.triangle, value); break;

		case 0x0C: set_noise_ctrl(&apu.noise, value); break;
		case 0x0E: set_noise_period(&apu, value); break;
		case 0x0F: set_noise_length(&apu.noise, value); break;

		case 0x10: set_dmc_ctrl(&apu, value); break;
		case 0x11: set_dmc_da(&apu.dmc, value); break;
		case 0x12: set_dmc_addr(&apu.dmc, value); break;
		case 0x13: set_dmc_length(&apu.dmc, value); break;

		case 0x15: set_status(&apu, value); break;
		case 0x17: set_frame_counter_ctrl(&apu, value); break;

		case 0x16:
		{
			bool aOnly = value & 0x01;
			controllerState[0].aOnly = controllerState[1].aOnly = aOnly;
			if(aOnly == false)
				controllerState[0].index = controllerState[1].index = 0;
			break;
		}
		}
	}

	void Get(uint16_t address, uint8_t &value, bool extrinsic)
	{
		if(address == 0x16 || address == 0x17)
		{
			Controller &c = controllerState[address - 0x16];
			if(c.aOnly == true)
				value = c.a;
			else
			{
				switch(c.index % 8)
				{
				case 0: value = c.a; break;
				case 1: value = c.b; break;
				case 2: value = c.select; break;
				case 3: value = c.start; break;
				case 4: value = c.up; break;
				case 5: value = c.down; break;
				case 6: value = c.left; break;
				case 7: value = c.right; break;
				}
				if(extrinsic == false)
					++c.index;
			}
		}
		else if(address == 0x15)
		{
			value = read_apu_status(&apu);
		}
	}

	void Tick(bool &doInterrupt, int &tickSkipCount)
	{
		apu.doIRQ = 0;
		apu.doTickSkip = 0;
		execute_apu(&apu);
		if(apu.doIRQ != 0)
			doInterrupt = true;
		if(apu.doTickSkip != 0)
			tickSkipCount += 3;
	}

	void StageAudio(void)
	{
		int16_t stageBuff[AUDIO_BUFF_SIZE];
		int size = 0;
		queue_audio(&apu, stageBuff, &size);
		// loop through and apply multiplyer
		::StageAudio(stageBuff, size);
	}
};