#ifndef __CONFIG_H__
#define __CONFIG_H__

#define DEBUG

#if 0 // Devkit mode
#define LOAD_ADDRESS 0xA8000000
#else
#define LOAD_ADDRESS 0x98000000
#endif

#define MEMORY_VITAGL_THRESHOLD_MB 12

#define DATA_PATH "ux0:data/gms"
#define GLSL_PATH DATA_PATH "/shared/glsl"
#define LAUNCH_FILE_PATH DATA_PATH "/launch.txt"

#define SCREEN_W 960
#define SCREEN_H 544

#endif
