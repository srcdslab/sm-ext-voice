#if defined _Voice_included
 #endinput
#endif
#define _Voice_included

/**
 *
 * Checks whether a client is speaking or not.
 *
 * @param client 	The client index to check against.
 *
 * @error 			Client is not valid, ingame, or client is a bot.
 *
 * @return 			True if client is speaking, false otherwise.
 *
 */

native bool IsClientTalking(int client);

/**
 *
 * Called when a client starts speaking.
 *
 *	@param client	Client index.
 *
 * @noreturn
 *
 */

forward void OnClientSpeakingStart(int client);

/**
 *
 * Called when a client ends speaking.
 *
 * @param client	Client index.
 *
 * @noreturn
 *
 */
forward void OnClientSpeakingEnd(int client);

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
