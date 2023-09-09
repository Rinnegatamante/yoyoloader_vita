#include "strings.h"

char lang_identifiers[LANG_STRINGS_NUM][LANG_ID_SIZE] = {
	FOREACH_STR(GET_STRING)
};

// This is properly populated so that emulator won't crash if an user launches it without language INI files.
char lang_strings[LANG_STRINGS_NUM][LANG_STR_SIZE] = {
	"Yes", // STR_YES
	"No", // STR_NO
	"Enforces GLES1 as rendering backend mode. May improve performances or make a game go further when crashing.", // STR_GLES1_DESC
	"Enforces bilinear filtering on textures.", // STR_BILINEAR_DESC
	"Sets the reported target mode to the Runner as the specified platform. Some games require a specific target platform to properly handle inputs.", // STR_PLAT_TARGET_DESC
	"Enables dumping of attempted shader translations by the built-in GLSL to CG shader translator in ux0:data/gms/shared/glsl.", // STR_DEBUG_SHADERS_DESC
	"Enables debug logging in ux0:data/gms/shared/yyl.log.", // STR_DEBUG_MODE_DESC
	"Disables splashscreen rendering at game boot.",  // STR_SPLASH_SKIP_DESC
	"Allows the Runner to use approximately extra 12 MBs of memory. May break some debugging tools and breaks virtual keyboard usage from games.",  // STR_EXTRA_MEM_DESC
	"Reduces apk size by removing unnecessary data inside it and improves performances by recompressing files one by one depending on their expected use.", // STR_OPTIMIZE_DESC
	"Increases the size of the memory pool available for the Runner. May solve some crashes.",  // STR_EXTRA_POOL_DESC
	"Compresses textures during externalization of assets. Reduces memory usage of the game and speedups textures loading process but increases storage usage.", // STR_COMPRESS_DESC
	"Enables Video Player implementation in the Runner at the cost of potentially reducing the total amount of memory available for the game.", // STR_VIDEO_PLAYER_DESC
	"Enables network functionalities implementation in the Runner at the cost of potentially reducing the total amount of memory available for the game.", // STR_NETWORK_DESC
	"Makes the Loader setup vitaGL with the lowest amount possible of dedicated memory for internal buffers. Increases available mem for the game at the cost of potential performance loss.",  // STR_SQUEEZE_DESC
	"Checking for updates", // STR_SEARCH_UPDATES
	"Downloading Changelist", // STR_CHANGELIST
	"Downloading an update", // STR_UPDATE
	"Downloading compatibility list database", // STR_COMPAT_LIST
	"Settings", // STR_SETTINGS
	"Downloading banners", // STR_BANNERS
	"Force GLES1 Mode", // STR_GLES1
	"Platform Mode", // STR_PLAT_TARGET
	"Run with Extended Mem Mode", // STR_EXTRA_MEM
	"Run with Extended Runner Pool", // STR_EXTRA_POOL
	"Run with Mem Squeezing", // STR_SQUEEZE
	"Enable Video Support", // STR_VIDEO_PLAYER
	"Enable Network Features", // STR_NETWORK
	"Force Bilinear Filtering", // STR_BILINEAR
	"Compress Textures", // STR_COMPRESS
	"Skip Splashscreen at Boot", // STR_SPLASH_SKIP
	"Run with Debug Mode", // STR_DEBUG_MODE
	"Run with Shaders Debug Mode", // STR_DEBUG_SHADERS
	"Optimize Apk", // STR_OPTIMIZE
	"Optimization completed!", // STR_OPTIMIZE_END
	"Reduced apk size by", // STR_REDUCED
	"Optimization in progress, please wait...", // STR_OPTIMIZATION
	"Game ID", // STR_GAME_ID
	"APK Size", // STR_SIZE
	"Playable", // STR_PLAYABLE
	"Ingame +", // STR_INGAME_PLUS
	"Ingame -", // STR_INGAME_MINUS
	"Crash", // STR_CRASH
	"Sort Mode", // STR_SORT
	"Press Triangle to change settings", // STR_SETTINGS_INSTR
	"Press L/R to change sorting mode", // STR_SORT_INSTR
	"Press Square to update banners collection", // STR_BANNERS_INSTR
	"Name (Ascending)", // STR_NAME_ASC
	"Name (Descending)", // STR_NAME_DESC
	"Error: libshacccg.suprx is not installed.", // STR_SHACCCG_ERROR
	"Extracting archive", // STR_EXTRACTING
	"Extracting missing Game IDs", // STR_GAME_ID_EXTR
	"What's New", // STR_NEWS
	"Continue", // STR_CONTINUE
	"Downloading animated banner", // STR_ANIM_BANNER
	"Press Select to download animated banner or restart it", // STR_ANIM_BANNER_INSTR
	"Filter by: ", // STR_FILTER_BY
	"No Filter", // STR_NO_FILTER
	"No Tags", // STR_NO_TAGS
	"This game has a community keymap available. Do you wish to install it?", // STR_KEYMAP
	"Externalizing sound files from game.droid...", // STR_EXTERNALIZE_DROID
	"Externalizing sound files from extra audiogroup", // STR_EXTERNALIZE_AUDIOGROUP
	"Optimize Apk with Sounds Externalization", // STR_EXTERNALIZE
	"Reduces apk size by removing unnecessary data inside it and by externalizing and compressing all game's audio files, and improves performances by recompressing files one by one depending on their expected use.", // STR_EXTERNALIZE_DESC
	"Disable Audio", // STR_AUDIO
	"Disables audio playback in order to reduce memory usage of the game.", // STR_AUDIO_DESC
	"Use Double Buffering", // STR_DOUBLE_BUFFERING
	"Use double buffering instead of triple buffering. Lowers mem usage but may cause some artifacts.", // STR_DOUBLE_BUFFERING_DESC
	"Use Uncached Memory", // STR_UNCACHED_MEM
	"Makes the game use mostly uncached memory internally. Reduces GPU workload and can potentially fasten memory copies but increases CPU workload." // STR_UNCACHED_MEM_DESC
};
