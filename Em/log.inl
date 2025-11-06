namespace Log
{
	emscripten_lock_t lock = EMSCRIPTEN_LOCK_T_STATIC_INITIALIZER;
	char buffer[(16 * 1024) + 4] = {0};
	int writePos = 0;
}

#define WriteLog(...) { emscripten_lock_busyspin_waitinf_acquire(&Log::lock); snprintf(Log::buffer + Log::writePos, (16*1024) - Log::writePos, __VA_ARGS__); Log::writePos += strlen(Log::buffer + Log::writePos); emscripten_lock_release(&Log::lock); }

void FlushLog(void)
{
	if(0 < Log::writePos)
	{
		emscripten_lock_busyspin_waitinf_acquire(&Log::lock);
		printf("%s", Log::buffer);
		Log::writePos = 0;
		emscripten_lock_release(&Log::lock);
	}
}
