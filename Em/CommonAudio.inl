#define SOUNDQUEUESAMPLESIZE (3 * 1024)

class SoundQueue_Mono
{
	emscripten_lock_t lock = EMSCRIPTEN_LOCK_T_STATIC_INITIALIZER;
	uint32_t start;
	uint32_t end;
	int16_t last;
	bool underflowSinceLastQueue;
	int overflowSize;
	int queueChunkSize;
	int16_t buffer[SOUNDQUEUESAMPLESIZE];
	int16_t overflow[1024];

public:
	SoundQueue_Mono(int initalChunkSize) : start(0), end(0), last(0), underflowSinceLastQueue(true), overflowSize(0), queueChunkSize(initalChunkSize) {}

	void GetBuffer(float outSamples[], int samplesToGet)
	{
		emscripten_lock_waitinf_acquire(&lock);
		bool underflowDetected = false;

		for(unsigned int i=0 ; i<samplesToGet ; ++i)
		{
			if(start == end)
			{
				outSamples[i] = last / 32768.0f;
				underflowDetected = true;
			}
			else
			{
				last = buffer[start];
				++start;
				if(SOUNDQUEUESAMPLESIZE <= start)
					start = 0;
				outSamples[i] = last / 32768.0f;
			}
		}

		if(underflowDetected == true && underflowSinceLastQueue == false)
		{
			++queueChunkSize;
			underflowSinceLastQueue = true;
		}

		emscripten_lock_release(&lock);
	}

	void QueueBuffer(int16_t inSamples[], int sampleCount)
	{
		emscripten_lock_waitinf_acquire(&lock);
		underflowSinceLastQueue = false;

		if(0 < overflowSize)
		{
			int newOverflowSize = 0;
			for(int i=0 ; i<overflowSize ; ++i)
			{
				if((end+1) % SOUNDQUEUESAMPLESIZE == start)
				{
					overflow[newOverflowSize] = overflow[i];
					++newOverflowSize;
				}
				else
				{
					buffer[end] = overflow[i];
					++end;
					if(SOUNDQUEUESAMPLESIZE <= end)
						end = 0;
				}
			}
			overflowSize = newOverflowSize;
		}
		
		{
			float index = 0, step = float(sampleCount) / queueChunkSize;
			for(int i=0 ; i<queueChunkSize ; ++i)
			{
				if((end+1) % SOUNDQUEUESAMPLESIZE == start)
				{
					for(; i<queueChunkSize && overflowSize<1024 ; ++i)
					{
						overflow[overflowSize] = inSamples[int(index)];
						index += step;
						++overflowSize;
					}
					--queueChunkSize;
					break;
				}

				buffer[end] = inSamples[int(index)];
				index += step;
				++end;
				if(SOUNDQUEUESAMPLESIZE <= end)
					end = 0;
			}
		}

		emscripten_lock_release(&lock);
	}
};

namespace Sound
{
	uint32_t GlobalSampleRate = 0;
	bool SoundEnabled = false;

	// good enough default value
	SoundQueue_Mono queue = SoundQueue_Mono(700);
	EMSCRIPTEN_WEBAUDIO_T audioCTX;
}

void StageAudio(int16_t stageBuff[], int size)
{
	if(Sound::SoundEnabled)
	{
		Sound::queue.QueueBuffer(stageBuff, size);
	}
}

bool AudioCallback(int numInputs, const AudioSampleFrame *inputs, int numOutputs, AudioSampleFrame *outputs, int numParams, const AudioParamFrame *params, void *userData)
{
	Sound::queue.GetBuffer(outputs->data, outputs->samplesPerChannel);
	return true;
}

void OnAudioCreate2(EMSCRIPTEN_WEBAUDIO_T audioContext, bool success, void *userData)
{
	if(!success)
		return;

	EmscriptenAudioWorkletNodeCreateOptions options;
	options.numberOfInputs = 0;
	options.numberOfOutputs = 1;
	options.outputChannelCounts = &options.numberOfOutputs;

	EMSCRIPTEN_AUDIO_WORKLET_NODE_T nodeCTX = emscripten_create_wasm_audio_worklet_node(audioContext, "CGSAUDIO", &options, AudioCallback, userData);
	emscripten_audio_node_connect(nodeCTX, audioContext, 0, 0);
}

void OnAudioCreate(EMSCRIPTEN_WEBAUDIO_T audioContext, bool success, void *userData)
{
	if(!success)
		return;

	WebAudioWorkletProcessorCreateOptions options;
	options.name = "CGSAUDIO";
	options.numAudioParams = 0;
	options.audioParamDescriptors = NULL;

	emscripten_create_wasm_audio_worklet_processor_async(audioContext, &options, OnAudioCreate2, userData);
}

void EmptyAudioCB(EMSCRIPTEN_WEBAUDIO_T audioContext, AUDIO_CONTEXT_STATE state, void *userData1){}

void InitAudio(void)
{
	Sound::GlobalSampleRate = EM_ASM_INT({
		var sr = 0;
		try
		{
			var AudioContext = window.AudioContext || window.webkitAudioContext;
			var ctx = new AudioContext();
			sr = ctx.sampleRate;
			ctx.close();
		}
		catch(e){}
		return sr;
	});

	if(0 < Sound::GlobalSampleRate)
	{
		Sound::SoundEnabled = true;
		Sound::queue = SoundQueue_Mono(Sound::GlobalSampleRate / 60);

		EmscriptenWebAudioCreateAttributes options;
		options.latencyHint = "interactive";
		options.sampleRate = Sound::GlobalSampleRate;
		Sound::audioCTX = emscripten_create_audio_context(&options);
		emscripten_start_wasm_audio_worklet_thread_async(Sound::audioCTX, memalign(16, 4 * 1024), 4 * 1024, OnAudioCreate, nullptr);
		emscripten_resume_audio_context_async(Sound::audioCTX, EmptyAudioCB, NULL);
	}
}

#undef SOUNDBUFFERSIZE
