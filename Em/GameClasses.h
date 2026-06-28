class SMBEmu : NESEmu
{
public:
	bool Load(void)
	{
		for(int i=0 ; i<60 ; ++i)
			DoFrame_Internal();
		NESEmu::SetStartThisFrame();
		for(int i=0 ; i<8 ; ++i)
			DoFrame_Internal();
		NESEmu::SetControllerState(false, false, false, false, false, false, false, false, false);
		return true;
	}
	void Tick(void)
	{
		// set lives to 9
		ram.Set(1882, 8, true);
		NESEmu::Tick();
	}
	static GameBase *MakeFunction()
	{
		return new SMBEmu();
	}
};

struct CustomGameClass
{
	char const *code;
	int codeLength;
	GameBase *(*makeFunction)(void);
} CustomGameClassList[] =
{
	{"SMB", 4, SMBEmu::MakeFunction},
};
int CustomGameClassListSize = 1;
