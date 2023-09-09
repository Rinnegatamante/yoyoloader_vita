/* gamepad.c -- Trophies code
 *
 * Copyright (C) 2023 Rinnegatamante
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.	See the LICENSE file for details.
 */

// This file has been hugely derived from https://github.com/JohnnyonFlame/droidports/tree/master/ports/gmloader
#include <vitasdk.h>
#include <vitaGL.h>
#include <stdio.h>

#include "main.h"

extern so_module yoyoloader_mod;

static char comm_id[12] = {0};
static char signature[160] = {0xb9,0xdd,0xe1,0x3b,0x01,0x00};

static int trp_ctx;
static int plat_id = -1;

typedef struct {
	int sdkVersion;
	SceCommonDialogParam commonParam;
	int context;
	int options;
	uint8_t reserved[128];
} SceNpTrophySetupDialogParam;

typedef struct {
	uint32_t unk[4];
} SceNpTrophyUnlockState;
SceNpTrophyUnlockState trophies_unlocks;

int sceNpTrophyInit(void *unk);
int sceNpTrophyCreateContext(int *context, char *commId, char *commSign, uint64_t options);
int sceNpTrophySetupDialogInit(SceNpTrophySetupDialogParam *param);
SceCommonDialogStatus sceNpTrophySetupDialogGetStatus();
int sceNpTrophySetupDialogTerm();
int sceNpTrophyCreateHandle(int *handle);
int sceNpTrophyDestroyHandle(int handle);
int sceNpTrophyUnlockTrophy(int ctx, int handle, int id, int *plat_id);
int sceNpTrophyGetTrophyUnlockState(int ctx, int handle, SceNpTrophyUnlockState *state, uint32_t *count);

int trophies_available = 0;
char *(*YYGetString) (void *args, int idx);
extern int (*YYGetInt32) (void *args, int idx);
float (*YYGetFloat) (void *args, int idx);

typedef struct {
	char name[256];
	uint32_t id;
} steam_ach;
steam_ach achievements[128];
int num_ach = 0;

void load_achievements_list() {
	int value;
	
	FILE *config = fopen("app0:trophies.lst", "r");

	if (config) {
		while (EOF != fscanf(config, "%[^=]=%u\n", achievements[num_ach].name, &achievements[num_ach].id)) {
			printf("%s is %u\n", achievements[num_ach].name, achievements[num_ach].id);
			num_ach++;
		}
		fclose(config);
	}
}

uint32_t get_achievement_id(char *name) {
	for (int i = 0; i < num_ach; i++) {
		if (!strcmp(name, achievements[i].name)) {
			return achievements[i].id;
		}
	}
	
	return 0xDEADBEEF;
}

volatile int trp_id;
SceUID trp_request_mutex, trp_delivered_mutex;
int trophies_unlocker(SceSize args, void *argp) {
	for (;;) {
		sceKernelWaitSema(trp_request_mutex, 1, NULL);
		int local_trp_id = trp_id;
		int trp_handle;
		sceNpTrophyCreateHandle(&trp_handle);
		sceNpTrophyUnlockTrophy(trp_ctx, trp_handle, local_trp_id, &plat_id);
		sceKernelSignalSema(trp_delivered_mutex, 1);
		sceNpTrophyDestroyHandle(trp_handle);
	}
}

extern int8_t game_idx;
int trophies_init() {
	SceIoStat st;
	if (sceIoGetstat("app0:trophies.lst", &st) < 0) {
		return 0;
	}
	load_achievements_list();
	
	// Starting sceNpTrophy
	sceAppMgrAppParamGetString(0, 12, comm_id, 256);
	sceSysmoduleLoadModule(SCE_SYSMODULE_NP_TROPHY);
	sceNpTrophyInit(NULL);
	int res = sceNpTrophyCreateContext(&trp_ctx, comm_id, signature, 0);
	if (res < 0) {
#ifdef DEBUG
		printf("sceNpTrophyCreateContext returned 0x%08X\n", res);
#endif	
		return res;
	}
	SceNpTrophySetupDialogParam setupParam;
	sceClibMemset(&setupParam, 0, sizeof(SceNpTrophySetupDialogParam));
	_sceCommonDialogSetMagicNumber(&setupParam.commonParam);
	setupParam.sdkVersion = PSP2_SDK_VERSION;
	setupParam.options = 0;
	setupParam.context = trp_ctx;
	sceNpTrophySetupDialogInit(&setupParam);
	static int trophy_setup = SCE_COMMON_DIALOG_STATUS_RUNNING;
	while (trophy_setup == SCE_COMMON_DIALOG_STATUS_RUNNING) {
		trophy_setup = sceNpTrophySetupDialogGetStatus();
		vglSwapBuffers(GL_TRUE);
	}
	sceNpTrophySetupDialogTerm();
	
	// Starting trophy unlocker thread
	trp_delivered_mutex = sceKernelCreateSema("trps delivery", 0, 1, 1, NULL);
	trp_request_mutex = sceKernelCreateSema("trps request", 0, 0, 1, NULL);
	SceUID tropies_unlocker_thd = sceKernelCreateThread("trophies unlocker", &trophies_unlocker, 0x10000100, 0x10000, 0, 0, NULL);
	sceKernelStartThread(tropies_unlocker_thd, 0, NULL);
	
	// Getting current trophy unlocks state
	int trp_handle;
	uint32_t dummy;
	sceNpTrophyCreateHandle(&trp_handle);
	sceNpTrophyGetTrophyUnlockState(trp_ctx, trp_handle, &trophies_unlocks, &dummy);
	sceNpTrophyDestroyHandle(trp_handle);
	
	trophies_available = 1;
	return 1;
}

uint8_t trophies_is_unlocked(uint32_t id) {
	if (trophies_available) {
		return (trophies_unlocks.unk[id >> 5] & (1 << (id & 31))) > 0;
	}
	return 0;
}

void trophies_unlock(uint32_t id) {
	if (trophies_available && !trophies_is_unlocked(id)) {
		trophies_unlocks.unk[id >> 5] |= (1 << (id & 31));
		sceKernelWaitSema(trp_delivered_mutex, 1, NULL);
		trp_id = id;
		sceKernelSignalSema(trp_request_mutex, 1);
	}
}

void steam_get_achievement(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	char *ach_name = YYGetString(args, 0);
	ret->rvalue.val = trophies_is_unlocked(get_achievement_id(ach_name)) ? 1.0f : 0.0f;
}

void steam_set_achievement(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	char *ach_name = YYGetString(args, 0);
	trophies_unlock(get_achievement_id(ach_name));
}

void achievements_available(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->rvalue.val = trophies_available ? 1.0f : 0.0f;
}

void steam_stats_ready(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->rvalue.val = trophies_available ? 1.0f : 0.0f;
}

void steam_initialised(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->rvalue.val = trophies_available ? 1.0f : 0.0f;
}

void achievements_post(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	float progress = YYGetFloat(args, 1);
	if (progress == 100.0f) {
		char *ach_name = YYGetString(args, 0);
		trophies_unlock(get_achievement_id(ach_name));
	}
}

void psn_unlock_trophy(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	int trp_id = YYGetInt32(args, 1);
	trophies_unlock(trp_id);
}

extern void (*Function_Add)(const char *name, intptr_t func, int argc, char ret);
void patch_trophies() {
	YYGetString = (void *)so_symbol(&yoyoloader_mod, "_Z11YYGetStringPK6RValuei");
	YYGetFloat = (void *)so_symbol(&yoyoloader_mod, "_Z10YYGetFloatPK6RValuei");
	
	Function_Add("steam_set_achievement", steam_set_achievement, 1, 0);
	Function_Add("steam_get_achievement", steam_get_achievement, 1, 0);
	Function_Add("steam_stats_ready", steam_stats_ready, 0, 0);
	Function_Add("steam_initialised", steam_initialised, 0, 0);
	Function_Add("achievement_available", achievements_available, 0, 0);
	Function_Add("YoYo_AchievementsAvailable", achievements_available, 0, 0);
	Function_Add("achievement_post", achievements_post, 2, 0);
	Function_Add("YoYo_PostAchievement", achievements_post, 2, 0);
	Function_Add("psn_unlock_trophy", psn_unlock_trophy, 2, 0);
}
