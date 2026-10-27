#include "Pumpkin.h"

/**
    The source: a jack-o'-lantern on every LED tile.

    Listed directly in the plugin's MODULE target, not in pumpkin_core: the
    harness links the core and must not register a plugin of its own. The core
    is an OBJECT library because this registers itself from a file-scope
    constructor nothing references (see CMakeLists.txt).

    `SW Pumpkin` is ten characters; the FFGL name field is char[ 16 ] and not
    null-terminated. `oxbow probe` reads it back the way a host does.
*/
static CFFGLPluginInfo PluginInfo(
	PluginFactory< pumpkin::PumpkinPlugin >,// Create method
	"PK01",                                 // Plugin unique ID of maximum length 4
	"SW Pumpkin",                           // Plugin name
	2,                                      // API major version number
	1,                                      // API minor version number
	0,                                      // Plugin major version number
	1,                                      // Plugin minor version number
	FF_SOURCE,                              // Plugin type
	"The test card's pumpkin patch: a jack-o'-lantern on every LED tile, each carved with one of six faces, "
	"its candle flickering and its face blinking, winking or chomping in its own time. Same seed, same tiles, "
	"same patch as the test card; on the Wall clock, the same flicker too.",
	"Pumpkin FFGL source"                   // About
);

extern "C" const char* PumpkinBuildStamp()
{
	return "pumpkin " PUMPKIN_VERSION ", built " __DATE__ " " __TIME__;
}
