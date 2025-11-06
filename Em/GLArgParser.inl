template <typename Fn1, typename Fn2, typename Fn3>
void SimpleParse(char *string, Fn1 unaryComandFn, Fn2 binaryCommandFn, Fn3 trinaryCommandFn)
{
	if(string == nullptr)
		return;

	char *walk = string;
	char *cmdStartPos = nullptr;
	char *equalPos = nullptr;
	char *colinPos = nullptr;

	auto workFn = [&]()
	{
		char tempWalk = *walk;
		*walk = 0;
		if(colinPos != nullptr)
		{
			if(equalPos != nullptr)
			{
				*equalPos = 0;
				*colinPos = 0;
				trinaryCommandFn(cmdStartPos, colinPos+1, equalPos+1);
				*colinPos = ':';
				*equalPos = '=';
			}
		}
		else if(equalPos != nullptr)
		{
			*equalPos = 0;
			binaryCommandFn(cmdStartPos, equalPos+1);
			*equalPos = '=';
		}
		else
		{
			unaryComandFn(cmdStartPos);
		}
		*walk = tempWalk;
		cmdStartPos = nullptr;
		equalPos = nullptr;
		colinPos = nullptr;
	};

	for(;;)
	{
		if(cmdStartPos == nullptr)
			cmdStartPos = walk;
		if(*walk == ':')
			colinPos = walk;
		else if(*walk == '=')
			equalPos = walk;
		else if(*walk == ' ')
			workFn();
		else if(*walk == 0)
		{
			workFn();
			return;
		}
		++walk;
	}
}
