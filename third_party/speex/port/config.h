/* DTMF-Signer's build configuration for libspeex (instead of the one
 * autotools generates): narrowband only, fixed point everywhere (the
 * firmware, the simulator and the encoding tool decode the same bits), no
 * float API, no stdio, and memory from spx_arena (port/include/spx_arena.h). */
#define FIXED_POINT 1
#define DISABLE_FLOAT_API 1
#define DISABLE_WARNINGS 1
#define DISABLE_NOTIFICATIONS 1
#define RELEASE 1
#define VAR_ARRAYS 1
#define EXPORT
#define SPEEX_VERSION "1.2.1"
#define OS_SUPPORT_CUSTOM 1
#define SPEEX_MAJOR_VERSION 1
#define SPEEX_MINOR_VERSION 2
#define SPEEX_MICRO_VERSION 1
#define SPEEX_EXTRA_VERSION ""
