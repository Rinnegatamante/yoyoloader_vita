#ifndef __CONFIG_H__
#define __CONFIG_H__

#define DEBUG

#define LOAD_ADDRESS 0x98000000

#if 0 // Devkit
#define MEMORY_NEWLIB_MB 300
#else
#define MEMORY_NEWLIB_MB 240
#endif
#define MEMORY_VITAGL_THRESHOLD_MB 12

#define DATA_PATH "ux0:data/gms"
#define GXP_PATH DATA_PATH "/shared/gxp"
#define GLSL_PATH DATA_PATH "/shared/glsl"
#define LAUNCH_FILE_PATH DATA_PATH "/launch.txt"

#define SCREEN_W 960
#define SCREEN_H 544

#endif
