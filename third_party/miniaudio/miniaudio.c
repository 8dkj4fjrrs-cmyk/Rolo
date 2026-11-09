/* miniaudio's implementation, built once for Rolo (see README.md).
 *
 * stb_vorbis adds Ogg Vorbis decoding. miniaudio's documented order: the
 * decoder's declarations first, then miniaudio, then the decoder's code. */

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
