/* Single translation unit for stb_vorbis's implementation - every other
 * caller only ever sees the header-only declarations (STB_VORBIS_HEADER_ONLY),
 * so the real decoder body lives here exactly once. */
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.h"
