#ifndef __CONFIG_H__
#define __CONFIG_H__

#define DEBUG

#define LOAD_ADDRESS 0x98000000

#define MEMORY_NEWLIB_MB 240
#define MEMORY_VITAGL_THRESHOLD_MB 12

#define DATA_PATH "ux0:data/blask"
#define SO_PATH DATA_PATH "/libyoyo.so"
#define APK_PATH DATA_PATH "/game.apk"
#define GXP_PATH DATA_PATH "/gxp"
#define GLSL_PATH DATA_PATH "/glsl"

#define SCREEN_W 960
#define SCREEN_H 544

#endif
