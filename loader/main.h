#ifndef __MAIN_H__
#define __MAIN_H__

#include <psp2/touch.h>
#include "config.h"
#include "so_util.h"

extern so_module twom_mod;

int debugPrintf(char *text, ...);

int ret0();

int sceKernelChangeThreadCpuAffinityMask(SceUID thid, int cpuAffinityMask);

SceUID _vshKernelSearchModuleByName(const char *, int *);

extern SceTouchPanelInfo panelInfoFront, panelInfoBack;

#endif
