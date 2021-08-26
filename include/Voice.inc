#if defined _Voice_included
 #endinput
#endif
#define _Voice_included

native bool IsClientTalking(int iClient);

public Extension __ext_Voice =
{
	name = "Voice",
	file = "Voice.ext",
#if defined AUTOLOAD_EXTENSIONS
	autoload = 1,
#else
	autoload = 0,
#endif
#if defined REQUIRE_EXTENSIONS
	required = 1,
#else
	required = 0,
#endif
};

#if !defined REQUIRE_EXTENSIONS
public void __ext_Voice_SetNTVOptional()
{
    MarkNativeAsOptional("IsClientTalking");
}
#endif
