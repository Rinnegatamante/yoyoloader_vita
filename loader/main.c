/* main.c -- YoYo Loader based on .so loader
 *
 * Copyright (C) 2021 Andy Nguyen
 * Copyright (C) 2022 Rinnegatamante
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.	See the LICENSE file for details.
 */
#define _POSIX_TIMERS
#include <vitasdk.h>
#include <kubridge.h>
#include <vitashark.h>
#include <vitaGL.h>
#include <zlib.h>

#define AL_ALEXT_PROTOTYPES
#include <AL/alext.h>
#include <AL/efx.h>

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <dirent.h>
#include <malloc.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <wchar.h>
#include <wctype.h>
#include <locale.h>

#include <math.h>
#include <math_neon.h>

#include <errno.h>
#include <ctype.h>
#include <setjmp.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "main.h"
#include "config.h"
#include "dialog.h"
#include "so_util.h"
#include "sha1.h"
#include "unzip.h"

#include "openal_patch.h"

#define STBI_MALLOC vglMalloc
#define STBI_REALLOC vglRealloc
#define STBI_FREE vglFree
#define STB_IMAGE_IMPLEMENTATION
#define STB_ONLY_PNG
#include "stb_image.h"

extern int trophies_init();
extern void patch_trophies();
extern void audio_player_play(char *path, int loop);
extern void audio_player_stop();
extern void audio_player_pause();
extern void audio_player_resume();
extern int audio_player_is_playing();
extern int is_gamepad_connected(int id);
extern void send_post_request(const char *url, const char *data);
extern void mem_profiler(void *framebuf);
extern SceUID post_thid;
extern SceUID get_thid;
extern volatile int post_response_code;
extern volatile int get_response_code;
extern volatile uint64_t downloaded_bytes;
extern uint8_t *downloader_mem_buffer;
extern uint8_t *downloader_hdr_buffer;
extern char *post_url;
extern char *get_url;
extern unsigned _newlib_heap_size;

int disableObjectsArray = 0;
int uncached_mem = 0;
int double_buffering = 0;
int forceGL1 = 0;
int forceSplashSkip = 0;
int platTarget = 0;
int forceBilinear = 0;
int has_net = 0;
extern int maximizeMem;
int debugShaders = 0;
int squeeze_mem = 0;
int debugMode = 0;
int disableAudio = 0;
int ime_active = 0;
int msg_active = 0;
int msg_index = 0;
int ime_index = 0;
int post_active = 0;
int post_index = 0;
int get_active = 0;
int get_index = 0;
int setup_ended = 0;

int deltarune_hack = 0;

extern int (*YYGetInt32) (void *args, int idx);
void (*Function_Add)(const char *name, intptr_t func, int argc, char ret);
int (*Java_com_yoyogames_runner_RunnerJNILib_CreateVersionDSMap) (void *env, int a2, int sdk_ver, char *release_version, char *model, char *device, char *manufacturer, char *cpu_abi, char *cpu_abi2, char *bootloader, char *board, char *version, char *region, char *version_name, int has_keyboard);
int (*Java_com_yoyogames_runner_RunnerJNILib_TouchEvent) (void *env, int a2, int type, int id, float x, float y);
float (*Audio_GetTrackPos) (int id);
uint8_t *g_fNoAudio;
int64_t *g_GML_DeltaTime;
uint32_t *g_IOFrameCount;
char **g_pWorkingDirectory;
int *g_TextureScale;

double jni_double = 0.0f;
GLuint main_fb, main_tex = 0xDEADBEEF;
int is_portrait = 0;

char data_path[256];
char data_path_root[256];
char apk_path[256];
char gxp_path[256];

void patch_gamepad();
void GamePadUpdate();

char *translate_frag_shader(const char *string, int size);
char *translate_vert_shader(const char *string, int size);

void recursive_mkdir(char *dir) {
	char *p = dir;
	while (p) {
		char *p2 = strstr(p, "/");
		if (p2) {
			p2[0] = 0;
			sceIoMkdir(dir, 0777);
			p = p2 + 1;
			p2[0] = '/';
		} else break;
	}
}

void loadConfig(const char *game) {
	char configFile[512];
	char buffer[30];
	int value;
#ifdef STANDALONE_MODE
	sprintf(configFile, "app0:yyl.cfg");
#else
	sprintf(configFile, "%s/%s/yyl.cfg", DATA_PATH, game);
#endif
	FILE *config = fopen(configFile, "r");

	if (config) {
		while (EOF != fscanf(config, "%[^=]=%d\n", buffer, &value)) {
			if (strcmp("forceGLES1", buffer) == 0) forceGL1 = value;
			else if (strcmp("forceBilinear", buffer) == 0) forceBilinear = value;
			else if (strcmp("winMode", buffer) == 0) platTarget = value ? 1 : 0; // Retrocompatibility
			else if (strcmp("platTarget", buffer) == 0) platTarget = value;
			else if (strcmp("debugShaders", buffer) == 0) debugShaders = value;
			else if (strcmp("debugMode", buffer) == 0) debugMode = value;
			else if (strcmp("noSplash", buffer) == 0) forceSplashSkip = value;
			else if (strcmp("maximizeMem", buffer) == 0) maximizeMem = value;
			else if (strcmp("netSupport", buffer) == 0) has_net = value;
			else if (strcmp("squeezeMem", buffer) == 0) squeeze_mem = value;
			else if (strcmp("disableAudio", buffer) == 0) disableAudio = value;
			else if (strcmp("uncachedMem", buffer) == 0) uncached_mem = value;
			else if (strcmp("doubleBuffering", buffer) == 0) double_buffering = value;
		}
		fclose(config);
	}
}

extern void *GetPlatformInstance;

static int __stack_chk_guard_fake = 0x42424242;
static char fake_vm[0x1000];
char fake_env[0x1000];

unsigned int _pthread_stack_default_user = 1 * 1024 * 1024;

so_module yoyoloader_mod, cpp_mod;

void *__wrap_memcpy(void *dest, const void *src, size_t n) {
	return sceClibMemcpy(dest, src, n);
}

void *__wrap_memmove(void *dest, const void *src, size_t n) {
	return sceClibMemmove(dest, src, n);
}

void *__wrap_memset(void *s, int c, size_t n) {
	return sceClibMemset(s, c, n);
}

int file_exists(const char *path) {
	SceIoStat stat;
	return sceIoGetstat(path, &stat) >= 0;
}

#if 1
int debugPrintf(char *text, ...) {
	if (!debugMode)
		return 0;

	va_list list;
	static char string[0x8000];

	va_start(list, text);
	vsprintf(string, text, list);
	va_end(list);

	SceUID fd = sceIoOpen("ux0:data/gms/shared/yyl.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
	if (fd >= 0) {
		sceIoWrite(fd, string, strlen(string));
		sceIoClose(fd);
	}

	return 0;
}
#endif

struct android_dirent {
	char pad[18];
	unsigned char d_type;
	char d_name[256];
};

// From https://github.com/kraj/uClibc/blob/master/libc/misc/dirent/scandir.c
int scandir_hook(const char *dir, struct android_dirent ***namelist,
	int (*selector) (const struct dirent *),
	int (*compar) (const struct dirent **, const struct dirent **))
{
	DIR *dp = opendir (dir);
	struct dirent *current;
	struct android_dirent d;
	struct android_dirent *android_current = &d;
	struct android_dirent **names = NULL;
	size_t names_size = 0, pos;
	//int save;

	if (dp == NULL)
		return -1;

	//save = errno;
	//__set_errno (0);

	pos = 0;
	while ((current = readdir (dp)) != NULL) {
		int use_it = selector == NULL;
		
		sceClibMemcpy(android_current->d_name, current->d_name, 256);
		android_current->d_type = SCE_S_ISDIR(current->d_stat.st_mode) ? 4 : 8;

		if (! use_it) {	
			use_it = (*selector)(android_current);
			/* The selector function might have changed errno.
			* It was zero before and it need to be again to make
			* the latter tests work.  */
			//if (! use_it)
			//__set_errno (0);
		}
		if (use_it) {
			struct android_dirent *vnew;
			size_t dsize;

			/* Ignore errors from selector or readdir */
			//__set_errno (0);

			if (pos == names_size)
			{
				struct android_dirent **new;
				if (names_size == 0)
					names_size = 10;
				else
					names_size *= 2;
				new = (struct android_dirent **)vglRealloc(names, names_size * sizeof(struct android_dirent*));
				if (new == NULL)
					break;
				names = new;
			}

			dsize = &android_current->d_name[256+1] - (char*)android_current;
			vnew = (struct android_dirent*)vglMalloc(dsize);
			if (vnew == NULL)
				break;

			names[pos++] = (struct android_dirent*)sceClibMemcpy(vnew, android_current, dsize);
		}
	}

	if (errno != 0) {
		//save = errno;
		closedir (dp);
		while (pos > 0)
			vglFree(names[--pos]);
		vglFree(names);
		//__set_errno (save);
		return -1;
	}

	closedir (dp);
	//__set_errno (save);

	/* Sort the list if we have a comparison function to sort with.  */
	if (compar != NULL)
		qsort (names, pos, sizeof(struct android_dirent*), (__compar_fn_t) compar);
	*namelist = names;
	return pos;
}

int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
	if (!debugMode)
		return 0;

	va_list list;
	static char string[0x8000];

	va_start(list, fmt);
	vsprintf(string, fmt, list);
	va_end(list);

	debugPrintf("[LOG] %s: %s\n", tag, string);
	
	return 0;
}

int __android_log_vprint(int prio, const char *tag, const char *fmt, va_list list) {
	if (!debugMode)
		return 0;
	
	static char string[0x8000];

	vsprintf(string, fmt, list);
	va_end(list);

	debugPrintf("[LOGV] %s: %s\n", tag, string);

	return 0;
}

int ret0(void) {
	return 0;
}

int ret1(void) {
	return 1;
}

int pthread_rwlock_init_fake(pthread_rwlock_t **uid, const pthread_rwlockattr_t *attr) {
	pthread_rwlock_t *l = vglCalloc(1, sizeof(pthread_rwlock_t));
	if (!l)
		return -1;
	
	int ret = pthread_rwlock_init(l, attr);
	if (ret < 0) {
		vglFree(l);
		return -1;
	}

	*uid = l;

	return 0;
}

int pthread_rwlock_destroy_fake(pthread_rwlock_t **uid) {
	if (uid && *uid) {
		pthread_rwlock_destroy(*uid);
		vglFree(*uid);
		*uid = NULL;
	}
	return 0;
}

int pthread_rwlock_rdlock_fake(pthread_rwlock_t **uid) {
	int ret = 0;
	if (!*uid) {
		ret = pthread_rwlock_init_fake(uid, NULL);
	}
	if (ret < 0)
		return ret;
	return pthread_rwlock_rdlock(*uid);
}

int pthread_rwlock_wrlock_fake(pthread_rwlock_t **uid) {
	int ret = 0;
	if (!*uid) {
		ret = pthread_rwlock_init_fake(uid, NULL);
	}
	if (ret < 0)
		return ret;
	return pthread_rwlock_wrlock(*uid);
}

int pthread_rwlock_unlock_fake(pthread_rwlock_t **uid) {
	int ret = 0;
	if (!*uid) {
		ret = pthread_rwlock_init_fake(uid, NULL);
	}
	if (ret < 0)
		return ret;
	return pthread_rwlock_unlock(*uid);
}

int pthread_mutex_init_fake(pthread_mutex_t **uid, const pthread_mutexattr_t *mutexattr) {
	pthread_mutex_t *m = vglCalloc(1, sizeof(pthread_mutex_t));
	if (!m)
		return -1;

	const int recursive = (mutexattr && *(const int *)mutexattr == 1);
	*m = recursive ? PTHREAD_RECURSIVE_MUTEX_INITIALIZER : PTHREAD_MUTEX_INITIALIZER;

	int ret = pthread_mutex_init(m, mutexattr);
	if (ret < 0) {
		vglFree(m);
		return -1;
	}

	*uid = m;

	return 0;
}

int pthread_mutex_destroy_fake(pthread_mutex_t **uid) {
	if (uid && *uid && (uintptr_t)*uid > 0x8000) {
		pthread_mutex_destroy(*uid);
		vglFree(*uid);
		*uid = NULL;
	}
	return 0;
}

int pthread_mutex_lock_fake(pthread_mutex_t **uid) {
	int ret = 0;
	if (!*uid) {
		ret = pthread_mutex_init_fake(uid, NULL);
	} else if ((uintptr_t)*uid == 0x4000) {
		pthread_mutexattr_t attr;
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		ret = pthread_mutex_init_fake(uid, &attr);
		pthread_mutexattr_destroy(&attr);
	} else if ((uintptr_t)*uid == 0x8000) {
		pthread_mutexattr_t attr;
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
		ret = pthread_mutex_init_fake(uid, &attr);
		pthread_mutexattr_destroy(&attr);
	}
	if (ret < 0)
		return ret;
	return pthread_mutex_lock(*uid);
}

int pthread_mutex_unlock_fake(pthread_mutex_t **uid) {
	int ret = 0;
	if (!*uid) {
		ret = pthread_mutex_init_fake(uid, NULL);
	} else if ((uintptr_t)*uid == 0x4000) {
		pthread_mutexattr_t attr;
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		ret = pthread_mutex_init_fake(uid, &attr);
		pthread_mutexattr_destroy(&attr);
	} else if ((uintptr_t)*uid == 0x8000) {
		pthread_mutexattr_t attr;
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
		ret = pthread_mutex_init_fake(uid, &attr);
		pthread_mutexattr_destroy(&attr);
	}
	if (ret < 0)
		return ret;
	return pthread_mutex_unlock(*uid);
}

int pthread_cond_init_fake(pthread_cond_t **cnd, const int *condattr) {
	pthread_cond_t *c = vglCalloc(1, sizeof(pthread_cond_t));
	if (!c)
		return -1;

	*c = PTHREAD_COND_INITIALIZER;

	int ret = pthread_cond_init(c, NULL);
	if (ret < 0) {
		vglFree(c);
		return -1;
	}

	*cnd = c;

	return 0;
}

int pthread_cond_broadcast_fake(pthread_cond_t **cnd) {
	if (!*cnd) {
		if (pthread_cond_init_fake(cnd, NULL) < 0)
			return -1;
	}
	return pthread_cond_broadcast(*cnd);
}

int pthread_cond_signal_fake(pthread_cond_t **cnd) {
	if (!*cnd) {
		if (pthread_cond_init_fake(cnd, NULL) < 0)
			return -1;
	}
	return pthread_cond_signal(*cnd);
}

int pthread_cond_destroy_fake(pthread_cond_t **cnd) {
	if (cnd && *cnd) {
		pthread_cond_destroy(*cnd);
		vglFree(*cnd);
		*cnd = NULL;
	}
	return 0;
}

int pthread_cond_wait_fake(pthread_cond_t **cnd, pthread_mutex_t **mtx) {
	if (!*cnd) {
		if (pthread_cond_init_fake(cnd, NULL) < 0)
			return -1;
	}
	return pthread_cond_wait(*cnd, *mtx);
}

int pthread_cond_timedwait_fake(pthread_cond_t **cnd, pthread_mutex_t **mtx, const struct timespec *t) {
	if (!*cnd) {
		if (pthread_cond_init_fake(cnd, NULL) < 0)
			return -1;
	}
	return pthread_cond_timedwait(*cnd, *mtx, t);
}

int pthread_create_fake(pthread_t *thread, const void *unused, void *entry, void *arg) {
	pthread_t t;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 1024 * 1024);
	return pthread_create(thread, &attr, entry, arg);
}

int pthread_once_fake(volatile int *once_control, void (*init_routine)(void)) {
	if (!once_control || !init_routine)
		return -1;
	if (__sync_lock_test_and_set(once_control, 1) == 0)
		(*init_routine)();
	return 0;
}


int GetCurrentThreadId(void) {
	return sceKernelGetThreadId();
}

extern void *__aeabi_ldiv0;

int GetEnv(void *vm, void **env, int r2) {
	*env = fake_env;
	return 0;
}

int DebugPrintf(int *target, const char *fmt, ...) {
	if (!debugMode)
		return 0;
	
	va_list list;
	static char string[0x8000];

	va_start(list, fmt);
	vsprintf(string, fmt, list);
	va_end(list);

	debugPrintf("[DBG] %s\n", string);
	return 0;
}

void main_loop() {
	int (*Java_com_yoyogames_runner_RunnerJNILib_Process) (void *env, int a2, int w, int h, float accel_x, float accel_y, float accel_z, int keypad_open, int orientation, float refresh_rate) = (void *)so_symbol(&yoyoloader_mod, "Java_com_yoyogames_runner_RunnerJNILib_Process");
	int (*Java_com_yoyogames_runner_RunnerJNILib_InputResult) (void *env, int a2, char *string, int state, int id) = (void *)so_symbol(&yoyoloader_mod, "Java_com_yoyogames_runner_RunnerJNILib_InputResult");
	int (*Java_com_yoyogames_runner_RunnerJNILib_HttpResult) (void *env, int a2, void *result, int responde_code, int id, char *url, void *header) = (void *)so_symbol(&yoyoloader_mod, "Java_com_yoyogames_runner_RunnerJNILib_HttpResult");
	int (*Java_com_yoyogames_runner_RunnerJNILib_canFlip) (void) = (void *)so_symbol(&yoyoloader_mod, "Java_com_yoyogames_runner_RunnerJNILib_canFlip");
	g_IOFrameCount = (uint32_t *)so_symbol(&yoyoloader_mod, "g_IOFrameCount");
	g_GML_DeltaTime = (int64_t *)so_symbol(&yoyoloader_mod, "g_GML_DeltaTime");
	Audio_GetTrackPos = (void *)so_symbol(&yoyoloader_mod, "_Z17Audio_GetTrackPosi");
	
	int lastX[SCE_TOUCH_MAX_REPORT] = {-1, -1, -1, -1, -1, -1, -1, -1};
	int lastY[SCE_TOUCH_MAX_REPORT] = {-1, -1, -1, -1, -1, -1, -1, -1};
	
	setup_ended = 1;
	glReleaseShaderCompiler();
	for (;;) {
		if (post_active) {
			SceKernelThreadInfo info;
			info.size = sizeof(SceKernelThreadInfo);
			int res = sceKernelGetThreadInfo(post_thid, &info);
			if (info.status > SCE_THREAD_DORMANT || res < 0) {
				Java_com_yoyogames_runner_RunnerJNILib_HttpResult(fake_env, 0, downloader_mem_buffer, post_response_code, post_index, post_url, downloader_hdr_buffer);
				free(post_url);
				vglFree(downloader_mem_buffer);
				vglFree(downloader_hdr_buffer);
				post_active = 0;
			}
		}
		if (get_active) {
			SceKernelThreadInfo info;
			info.size = sizeof(SceKernelThreadInfo);
			int res = sceKernelGetThreadInfo(get_thid, &info);
			if (info.status > SCE_THREAD_DORMANT || res < 0) {
				Java_com_yoyogames_runner_RunnerJNILib_HttpResult(fake_env, 0, downloader_mem_buffer, get_response_code, get_index, get_url, downloader_hdr_buffer);
				free(get_url);
				vglFree(downloader_mem_buffer);
				vglFree(downloader_hdr_buffer);
				get_active = 0;
			}
		}
		
		SceMotionSensorState sensor;
		sceMotionGetSensorState(&sensor, 1);
		SceMotionState state;
		sceMotionGetState(&state);
		float orientation[3];
		sceMotionGetBasicOrientation(orientation);
		is_portrait = (int)orientation[0];
		if (is_portrait) {
			if (main_tex == 0xDEADBEEF) {
				glGenTextures(1, &main_tex);
				glBindTexture(GL_TEXTURE_2D, main_tex);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SCREEN_H, SCREEN_W, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
				glGenFramebuffers(1, &main_fb);
				glBindFramebuffer(GL_FRAMEBUFFER, main_fb);
				glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, main_tex, 0);
			}
			glBindFramebuffer(GL_FRAMEBUFFER, main_fb);
			glViewport(0, 0, SCREEN_H, SCREEN_W);
			glScissor(0, 0, SCREEN_H, SCREEN_W);
		} else {
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glViewport(0, 0, SCREEN_W, SCREEN_H);
			glScissor(0, 0, SCREEN_W, SCREEN_H);
		}
		
		if (*g_IOFrameCount >= 1) {
			GamePadUpdate();
		}
		
		SceTouchData touch;
		sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
		for (int i = 0; i < SCE_TOUCH_MAX_REPORT; i++) {
			if (i < touch.reportNum) {
				int x, y;
				if (is_portrait) {
					y = (int)((float)touch.report[i].x * (float)SCREEN_W / 1920.0f);
					x = (int)((float)touch.report[i].y * (float)SCREEN_H / 1088.0f);
					if (is_portrait > 0) {
						y = SCREEN_W - y;
					} else {
						x = SCREEN_H - x;
					}
				} else {
					x = (int)((float)touch.report[i].x * (float)SCREEN_W / 1920.0f);
					y = (int)((float)touch.report[i].y * (float)SCREEN_H / 1088.0f);
				}

				if (lastX[i] == -1 || lastY[i] == -1)
					Java_com_yoyogames_runner_RunnerJNILib_TouchEvent(fake_env, 0, TOUCH_DOWN, i, x, y);
				else if (lastX[i] != x || lastY[i] != y)
					Java_com_yoyogames_runner_RunnerJNILib_TouchEvent(fake_env, 0, TOUCH_MOVE, i, x, y);
				lastX[i] = x;
				lastY[i] = y;
			} else {
				if (lastX[i] != -1 || lastY[i] != -1) {
					Java_com_yoyogames_runner_RunnerJNILib_TouchEvent(fake_env, 0, TOUCH_UP, i, lastX[i], lastY[i]);
					lastX[i] = -1;
					lastY[i] = -1;
				}
			}
		}

		if (!is_portrait)
			Java_com_yoyogames_runner_RunnerJNILib_Process(fake_env, 0, SCREEN_W, SCREEN_H, sensor.accelerometer.x, sensor.accelerometer.y, sensor.accelerometer.z, 0, 0, 60.0f);
		else
			Java_com_yoyogames_runner_RunnerJNILib_Process(fake_env, 0, SCREEN_H, SCREEN_W, sensor.accelerometer.x, sensor.accelerometer.y, sensor.accelerometer.z, 0, 0x3FF00000, 60.0f);	
		if (!Java_com_yoyogames_runner_RunnerJNILib_canFlip || Java_com_yoyogames_runner_RunnerJNILib_canFlip()) {
			if (is_portrait) {
				int prog;
				glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
				glUseProgram(0);
				glEnable(GL_TEXTURE_2D);
				glEnableClientState(GL_VERTEX_ARRAY);
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				glViewport(0, 0, SCREEN_W, SCREEN_H);
				glDisable(GL_SCISSOR_TEST);
				glMatrixMode(GL_PROJECTION);
				glLoadIdentity();
				glOrtho(0, SCREEN_W, SCREEN_H, 0, -1, 1);
				glMatrixMode(GL_MODELVIEW);
				glLoadIdentity();
				float fb_vertices[] = {
			 SCREEN_W, SCREEN_H, 0,
			        0, SCREEN_H, 0,
			        0,        0, 0,
			 SCREEN_W,        0, 0,
				};
				float fb_texcoords[] = {1, 1, 1, 0, 0, 0, 0, 1};
				float fb_texcoords_flipped[] = {0, 0, 0, 1, 1, 1, 1, 0};
				if (is_portrait > 0)
					glTexCoordPointer(2, GL_FLOAT, 0, fb_texcoords);
				else
					glTexCoordPointer(2, GL_FLOAT, 0, fb_texcoords_flipped);
				glVertexPointer(3, GL_FLOAT, 0, fb_vertices);
				
				glBindTexture(GL_TEXTURE_2D, main_tex);
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
				glDisableClientState(GL_VERTEX_ARRAY);
				glDisableClientState(GL_TEXTURE_COORD_ARRAY);
				glUseProgram(prog);
			}
			if (ime_active) {
				char *r = get_ime_dialog_result();
				if (r) {
					Java_com_yoyogames_runner_RunnerJNILib_InputResult(fake_env, 0, r, 1, ime_index);
					ime_active = 0;
				}
				vglSwapBuffers(GL_TRUE);
			} else if (msg_active) {
				if (get_msg_dialog_result()) {
					Java_com_yoyogames_runner_RunnerJNILib_InputResult(fake_env, 0, "OK", 1, msg_index);
					msg_active = 0;
				}
				vglSwapBuffers(GL_TRUE);
			} else {
				vglSwapBuffers(GL_FALSE);
			}
		}
	}
}

void __stack_chk_fail_fake() {
	// Some versions of libyoyo.so apparently stack smash on Startup, with this workaround we prevent the app from crashing
	debugPrintf("Entering main loop after stack smash\n");
	main_loop();
}

double GetPlatform() {
	switch (platTarget) {
	case 1: // Windows
		return 0.0f;
	case 2: // PS4
		return 14.0f;
	default: // Android
		return 4.0f;
	}
}

uint32_t *(*ReadPNGFile) (void *a1, int a2, int *a3, int *a4, int a5);
void (*FreePNGFile) ();
void (*InvalidateTextureState) ();

void LoadTextureFromPNG_generic(uint32_t arg1, uint32_t arg2, uint32_t *flags, uint32_t *tex_id, uint32_t *texture) {
	int width, height;
	uint32_t *data = ReadPNGFile(arg1 , arg2, &width, &height, (*flags & 2) == 0);
	if (data) {
		InvalidateTextureState();
		glGenTextures(1, tex_id);
		glBindTexture(GL_TEXTURE_2D, *tex_id);
		if (width == 2 && height == 1) {
			if (data[0] == 0xFFBEADDE) {
				uint32_t *ext_data;
				uint32_t idx = (data[1] << 8) >> 8;
				char fname[256];
#ifdef STANDALONE_MODE
				sprintf(fname, "app0:assets/%u.pvr", idx);
#else
				sprintf(fname, "%s%u.pvr", data_path, idx);
#endif
				FILE *f = fopen(fname, "rb");
				if (f) {
					debugPrintf("Loading externalized texture %s (Raw ID: 0x%X)\n", fname, data[1]);
					fseek(f, 0, SEEK_END);
					uint32_t size = ftell(f) - 0x34;
					uint32_t metadata_size;
					fseek(f, 0x08, SEEK_SET);
					uint64_t format;
					fread(&format, 1, 8, f);
					fseek(f, 0x18, SEEK_SET);
					fread(&height, 1, 4, f);
					fread(&width, 1, 4, f);
					fseek(f, 0x30, SEEK_SET);
					fread(&metadata_size, 1, 4, f);
					size -= metadata_size;
					ext_data = vglMalloc(size);
					fseek(f, metadata_size, SEEK_CUR);
					fread(ext_data, 1, size, f);
					fclose(f);
					switch (format) {
					case 0x00:
						glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG, width, height, 0, size, ext_data);
						break;
					case 0x01:
						glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG, width, height, 0, size, ext_data);
						break;
					case 0x02:
						glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG, width, height, 0, size, ext_data);
						break;
					case 0x03:
						glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG, width, height, 0, size, ext_data);
						break;
					case 0x04:
						glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_PVRTC_2BPPV2_IMG, width, height, 0, size, ext_data);
						break;
					case 0x05:
						glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_PVRTC_4BPPV2_IMG, width, height, 0, size, ext_data);
						break;
					case 0x06:
						glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, width, height, 0, size, ext_data);
						break;
					case 0x07:
						glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, width, height, 0, size, ext_data);
						break;
					case 0x09:
						glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, width, height, 0, size, ext_data);
						break;
					case 0x0B:
						if (metadata_size == 4) { // Load DXT5 as pre-swizzled
							glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
							SceGxmTexture *gxm_tex = vglGetGxmTexture(GL_TEXTURE_2D);
							vglFree(vglGetTexDataPointer(GL_TEXTURE_2D));
							void *tex_data = vglForceAlloc(size);
							sceClibMemcpy(tex_data, ext_data, size);
							sceGxmTextureInitSwizzledArbitrary(gxm_tex, tex_data, SCE_GXM_TEXTURE_FORMAT_UBC3_ABGR, width, height, 0);
							vglOverloadTexDataPointer(GL_TEXTURE_2D, tex_data);
						} else
							glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, width, height, 0, size, ext_data);
						break;
					default:
						debugPrintf("Unsupported externalized texture format (0x%llX).\n", format);
						break;
					}
				} else {
					debugPrintf("Loading externalized texture %s (Raw ID: 0x%X).\n", fname, data[1]);
#ifdef STANDALONE_MODE
					sprintf(fname, "app0:assets/%u.png", idx);
#else
					sprintf(fname, "%s%u.png", data_path, idx);
#endif
					ext_data = stbi_load(fname, &width, &height, NULL, 4);
					glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, ext_data);
				}
				vglFree(ext_data);
			} else {
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
			}
		} else {
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		}
		*flags = *flags | 0x40;
		FreePNGFile();
		texture[0] = 0x06;
		if (flags != &texture[2]) {
			texture[1] = width;
			texture[2] = height;
		} else {
			texture[1] = ((width * *g_TextureScale - 1) | texture[1] & 0xFFFFE000) & 0xFC001FFF | ((height * *g_TextureScale - 1) << 13);
		}
		debugPrintf("Texture size: %dx%d\n", width, height);
	} else {
		debugPrintf("ERROR: Failed to load a PNG texture!\n");
	}
}

void LoadTextureFromPNG_1(uint32_t *texture, int has_mips) {
	LoadTextureFromPNG_generic(texture[23], texture[24], &texture[5], &texture[6], texture);
}

void LoadTextureFromPNG_2(uint32_t *texture, int has_mips) {
	LoadTextureFromPNG_generic(texture[11], texture[12], &texture[4], &texture[5], texture);
}

void LoadTextureFromPNG_3(uint32_t *texture) {
	LoadTextureFromPNG_generic(texture[9], texture[10], &texture[2], &texture[3], texture);
}

void LoadTextureFromPNG_4(uint32_t *texture) {
	LoadTextureFromPNG_generic(texture[8], texture[9], &texture[2], &texture[3], texture);
}

int image_preload_idx = 0;
uint32_t png_get_IHDR_hook(uint32_t *png_ptr, uint32_t *info_ptr, uint32_t *width, uint32_t *height, int *bit_depth, int *color_type, int *interlace_type, int *compression_type, int *filter_type) {
	if (!png_ptr || !info_ptr || !width || !height)
		return 0;
	
	*width = info_ptr[0];
	*height = info_ptr[1];

	if (bit_depth)
		*bit_depth = *((uint8_t *)info_ptr + 24);

	if (color_type)
		*color_type = *((uint8_t *)info_ptr + 25);

	if (compression_type)
		*compression_type = *((uint8_t *)info_ptr + 26);

	if (filter_type)
		*filter_type = *((uint8_t *)info_ptr + 27);

	if (interlace_type)
		*interlace_type = *((uint8_t *)info_ptr + 28);

	if (!setup_ended && *width == 2 && *height == 1) {
		char fname[256];
#ifdef STANDALONE_MODE
		sprintf(fname, "app0:assets/%d.pvr", image_preload_idx);
#else
		sprintf(fname, "%s%d.pvr", data_path, image_preload_idx);
#endif
		FILE *f = fopen(fname, "rb");
		if (f) {
			fseek(f, 0x18, SEEK_SET);
			fread(height, 1, 4, f);
			fread(width, 1, 4, f);
			fclose(f);
		} else {
#ifdef STANDALONE_MODE
			sprintf(fname, "app0:assets/%d.pvr", image_preload_idx);
#else
			sprintf(fname, "%s%d.png", data_path, image_preload_idx);
#endif
			int dummy;
			stbi_info(fname, width, height, &dummy);
		}
		image_preload_idx++;
	}
	return 1;
}

void SetWorkingDirectory() {
	// This is the smallest to reimplement function after ProcessCommandLine where we can disable audio if required
	if (disableAudio)
		*g_fNoAudio = 1;
	
	if (!*g_pWorkingDirectory)
		*g_pWorkingDirectory = strdup("assets/");
}

void patch_runner(void) {
	FreePNGFile = so_symbol(&yoyoloader_mod, "_Z11FreePNGFilev");
	ReadPNGFile = so_symbol(&yoyoloader_mod, "_Z11ReadPNGFilePviPiS0_b");
	InvalidateTextureState = so_symbol(&yoyoloader_mod, "_Z23_InvalidateTextureStatev");
	
	hook_addr(so_symbol(&yoyoloader_mod, "png_get_IHDR"), (uintptr_t)&png_get_IHDR_hook);
	hook_addr(so_symbol(&yoyoloader_mod, "_Z19SetWorkingDirectoryv"), (uintptr_t)&SetWorkingDirectory);
	
	uint8_t has_mips = 1;
	uint32_t *LoadTextureFromPNG = (uint32_t *)so_symbol(&yoyoloader_mod, "_Z18LoadTextureFromPNGP7Texture10eMipEnable");
	if (!LoadTextureFromPNG) {
		LoadTextureFromPNG = (uint32_t *)so_symbol(&yoyoloader_mod, "_Z18LoadTextureFromPNGP7Texture");
		has_mips = 0;
	}
	
	debugPrintf("LoadTextureFromPNG has signature: 0x%X\n", *LoadTextureFromPNG);
	if (!has_mips) {
		uint32_t *p = LoadTextureFromPNG;
		for (;;) {
			if (*p == 0xE5900020) { // LDR R0, [R0,#0x20]
				debugPrintf("Patching LoadTextureFromPNG to variant #4\n");
				hook_addr(LoadTextureFromPNG, (uintptr_t)&LoadTextureFromPNG_4);
				break;
			} else if (*p == 0xE5900024) { // LDR R0, [R0,#0x24]
				debugPrintf("Patching LoadTextureFromPNG to variant #3\n");
				hook_addr(LoadTextureFromPNG, (uintptr_t)&LoadTextureFromPNG_3);
				break;
			}
			p++;
		}
	} else {
		switch (*LoadTextureFromPNG >> 16) {
		case 0xE92D:
			debugPrintf("Patching LoadTextureFromPNG to variant #1\n");
			hook_addr(LoadTextureFromPNG, (uintptr_t)&LoadTextureFromPNG_1);
			break;
		case 0xE590:
			debugPrintf("Patching LoadTextureFromPNG to variant #2\n");
			hook_addr(LoadTextureFromPNG, (uintptr_t)&LoadTextureFromPNG_2);
			break;
		default:
			fatal_error("Error: Unrecognized LoadTextureFromPNG signature: 0x%08X.", *LoadTextureFromPNG);
			break;
		}
	}

	hook_addr(so_symbol(&yoyoloader_mod, "_ZN9DbgServer4InitEv"), (uintptr_t)&ret0);
	hook_addr(so_symbol(&yoyoloader_mod, "_ZN9DbgServerC2Eb"), (uintptr_t)&ret0);
	hook_addr(so_symbol(&yoyoloader_mod, "_ZN9DbgServerD2Ev"), (uintptr_t)&ret0);
	
	hook_addr(so_symbol(&yoyoloader_mod, "_Z30PackageManagerHasSystemFeaturePKc"), (uintptr_t)&ret0);
	hook_addr(so_symbol(&yoyoloader_mod, "_Z17alBufferDebugNamejPKc"), (uintptr_t)&ret0);
	hook_addr(so_symbol(&yoyoloader_mod, "_ZN13MemoryManager10DumpMemoryEP7__sFILE"), (uintptr_t)&ret0);
	hook_addr(so_symbol(&yoyoloader_mod, "_ZN13MemoryManager10DumpMemoryEPvS0_"), (uintptr_t)&ret0);
	hook_addr(so_symbol(&yoyoloader_mod, "_ZN13MemoryManager10DumpMemoryEPvS0_b"), (uintptr_t)&ret0);

	hook_addr(so_symbol(&yoyoloader_mod, "_Z23YoYo_GetPlatform_DoWorkv"), (uintptr_t)&GetPlatform);
	hook_addr(so_symbol(&yoyoloader_mod, "_Z20GET_YoYo_GetPlatformP9CInstanceiP6RValue"), (uintptr_t)&GetPlatformInstance);
	
	so_symbol_fix_ldmia(&yoyoloader_mod, "_Z11Shader_LoadPhjS_");
	so_symbol_fix_ldmia(&yoyoloader_mod, "_Z10YYGetInt32PK6RValuei");

	if (debugMode) {
		hook_addr(so_symbol(&yoyoloader_mod, "_ZN11TRelConsole6OutputEPKcz"), (uintptr_t)&DebugPrintf);
		hook_addr(so_symbol(&yoyoloader_mod, "_ZN17TErrStreamConsole6OutputEPKcz"), (uintptr_t)&DebugPrintf);
		hook_addr(so_symbol(&yoyoloader_mod, "_Z7YYErrorPKcz"), (uintptr_t)&debugPrintf);
	} else {
		hook_addr(so_symbol(&yoyoloader_mod, "_ZN11TRelConsole6OutputEPKcz"), (uintptr_t)&ret0);
		hook_addr(so_symbol(&yoyoloader_mod, "_ZN17TErrStreamConsole6OutputEPKcz"), (uintptr_t)&ret0);
		hook_addr(so_symbol(&yoyoloader_mod, "_Z7YYErrorPKcz"), (uintptr_t)&ret0);
	}
}

void patch_runner_post_init(void) {
	g_fNoAudio = (uint8_t *)so_symbol(&yoyoloader_mod, "g_fNoAudio");
	g_pWorkingDirectory = (char *)so_symbol(&yoyoloader_mod, "g_pWorkingDirectory");
	g_TextureScale = (int *)so_symbol(&yoyoloader_mod, "g_TextureScale");
	
	int *dbg_csol = (int *)so_symbol(&yoyoloader_mod, "_dbg_csol");
	if (dbg_csol) {
		kuKernelCpuUnrestrictedMemcpy((void *)(*(int *)so_symbol(&yoyoloader_mod, "_dbg_csol") + 0x0C), (void *)(so_symbol(&yoyoloader_mod, "_ZTV11TRelConsole") + 0x14), 4);
		kuKernelCpuUnrestrictedMemcpy((void *)(*(int *)so_symbol(&yoyoloader_mod, "_rel_csol") + 0x0C), (void *)(so_symbol(&yoyoloader_mod, "_ZTV11TRelConsole") + 0x14), 4);
	}
}

extern void *_Znaj;
extern void *_Znwj;
extern void *_ZdlPv;
extern void *_ZdaPv;
extern void *_ZTVN10__cxxabiv117__class_type_infoE;
extern void *_ZTVN10__cxxabiv120__si_class_type_infoE;
extern void *_ZNSt12length_errorD1Ev;
extern void *_ZNSt13runtime_errorD1Ev;
extern void *_ZTVSt12length_error;
extern void *__aeabi_memclr;
extern void *__aeabi_memclr4;
extern void *__aeabi_memclr8;
extern void *__aeabi_memcpy4;
extern void *__aeabi_memcpy8;
extern void *__aeabi_memmove4;
extern void *__aeabi_memmove8;
extern void *__aeabi_memcpy;
extern void *__aeabi_memmove;
extern void *__aeabi_memset;
extern void *__aeabi_memset4;
extern void *__aeabi_memset8;
extern void *__aeabi_atexit;
extern void *__aeabi_idiv;
extern void *__aeabi_idivmod;
extern void *__aeabi_ldivmod;
extern void *__aeabi_uidiv;
extern void *__aeabi_uidivmod;
extern void *__aeabi_uldivmod;
extern void *__aeabi_f2d;
extern void *__aeabi_l2d;
extern void *__aeabi_l2f;
extern void *__aeabi_d2uiz;
extern void *__aeabi_d2lz;
extern void *__aeabi_d2ulz;
extern void *__aeabi_ui2d;
extern void *__aeabi_ul2d;
extern void *__aeabi_ddiv;
extern void *__aeabi_dadd;
extern void *__aeabi_dcmplt;
extern void *__aeabi_dmul;
extern void *__aeabi_dsub;
extern void *__aeabi_dcmpge;
extern void *__aeabi_dcmpgt;
extern void *__aeabi_i2d;
extern void *__cxa_atexit;
extern void *__cxa_finalize;
extern void *__cxa_guard_acquire;
extern void *__cxa_guard_release;
extern void *__cxa_pure_virtual;
extern void *__cxa_allocate_exception;
extern void __cxa_throw(void *thrown_exception, void *tinfo, void (*dest)(void *));
extern void *__gnu_unwind_frame;
extern void *__stack_chk_fail;

char __progname[32] = {0};
int __page_size = 0;

int open(const char *pathname, int flags);

extern const char *BIONIC_ctype_;
extern const short *BIONIC_tolower_tab_;
extern const short *BIONIC_toupper_tab_;

size_t __ctype_get_mb_cur_max() {
	return 1;
}

static FILE __sF_fake[0x100][3];

int stat_hook(const char *pathname, void *statbuf) {
	struct stat st;
	int res = stat(pathname, &st);
	if (res == 0)
		*(uint64_t *)(statbuf + 0x30) = st.st_size;
	return res;
}

typedef struct {
	uint8_t *buf;
	off_t sz;
	int offs;
} AAssetHandle;

AAssetHandle *AAssetManager_open(unzFile apk_file, const char *fname, int mode) {
	debugPrintf("AAssetManager_open %s\n", fname);
	char path[256];
	sprintf(path, "assets/%s", fname);
	int res = unzLocateFile(apk_file, path, NULL);
	if (res != UNZ_OK)
		return NULL;
	AAssetHandle *ret = (AAssetHandle *)vglMalloc(sizeof(AAssetHandle));
	unz_file_info file_info;
	unzGetCurrentFileInfo(apk_file, &file_info, NULL, 0, NULL, 0, NULL, 0);
	ret->sz = file_info.uncompressed_size;
	ret->buf = (uint8_t *)vglMalloc(ret->sz);
	ret->offs = 0;
	unzOpenCurrentFile(apk_file);
	unzReadCurrentFile(apk_file, ret->buf, ret->sz);
	unzCloseCurrentFile(apk_file);
	return ret;
}

void AAsset_close(AAssetHandle *f) {
	if (f) {
		vglFree(f->buf);
		vglFree(f);
	}
}

unzFile AAssetManager_fromJava(void *env, void *obj) {
	return unzOpen(apk_path);
}

int AAsset_read(AAssetHandle *f, void *buf, size_t count) {
	int read_count = (f->offs + count) > f->sz ? (f->sz - f->offs) : count;
	sceClibMemcpy(buf, &f->buf[f->offs], read_count);
	f->offs += read_count;
	return read_count;
}

off_t AAsset_seek(AAssetHandle *f, off_t offs, int whence) {
	switch (whence) {
	case SEEK_SET:
		f->offs = offs;
		break;
	case SEEK_END:
		f->offs = f->sz + offs;
		break;
	case SEEK_CUR:
		f->offs += offs;
		break;
	}
	return f->offs;
}

off_t AAsset_getLength(AAssetHandle *f) {
	return f->sz;
}

int fstat_hook(int fd, void *statbuf) {
	struct stat st;
	int res = fstat(fd, &st);
	if (res == 0)
		*(uint64_t *)(statbuf + 0x30) = st.st_size;
	return res;
}

void *dlopen_hook(const char *filename, int flags) {
	debugPrintf("Opening %s\n", filename);
	if (forceGL1 && strstr(filename, "v2"))
		return NULL;
#if 0	
	if (!strcmp(filename, "libOpenSLES.so"))
		return NULL;
#endif
	return (void *)0xDEADBEEF;
}

void glTexParameteriHook(GLenum target, GLenum pname, GLint param) {
	if (forceBilinear && (pname == GL_TEXTURE_MIN_FILTER || pname == GL_TEXTURE_MAG_FILTER)) {
		param = GL_LINEAR;
	}
	glTexParameteri(target, pname, param);
}

void glTexParameterfHook(GLenum target, GLenum pname, GLfloat param) {
	if (forceBilinear && (pname == GL_TEXTURE_MIN_FILTER || pname == GL_TEXTURE_MAG_FILTER)) {
		param = GL_LINEAR;
	}
	glTexParameteri(target, pname, param);
}

void *retJNI(int dummy) {
	return fake_env;
}

void glBindFramebufferHook(GLenum target, GLuint framebuffer) {
	if (!framebuffer && is_portrait) {
		framebuffer = main_fb;
	}
	glBindFramebuffer(target, framebuffer);
}

const char *gl_ret0[] = {
	"glDeleteRenderbuffers",
	"glDiscardFramebufferEXT",
	"glFramebufferRenderbuffer",
	"glGenRenderbuffers",
	"glGetError",
	"glBindRenderbuffer",
	"glHint",
	"glLightf",
	"glMaterialx",
	"glNormalPointer",
	"glPixelStorei",
	"glRenderbufferStorage",
	"glShadeModel",
};
static size_t gl_numret = sizeof(gl_ret0) / sizeof(*gl_ret0);

void glReadPixelsHook(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *data) {
	if (deltarune_hack)
		glFinish();
	glReadPixels(x, y, width, height, format, type, data);
}

void glShaderSourceHook(GLuint shader, GLsizei count, const GLchar **string, const GLint *length) {	
	if (debugShaders) {
		char glsl_path[256];
		static int shader_idx = 0;
		snprintf(glsl_path, sizeof(glsl_path), "%s/%d.glsl", GLSL_PATH, shader_idx++);
		FILE *file = fopen(glsl_path, "w");
		fprintf(file, "%s", *string);
		fclose(file);
	}
	
	glShaderSource(shader, count, string, length);
}

static so_default_dynlib gl_hook[] = {
	{"glTexParameterf", (uintptr_t)&glTexParameterfHook},
	{"glTexParameteri", (uintptr_t)&glTexParameteriHook},
	{"glBindFramebuffer", (uintptr_t)&glBindFramebufferHook},
	{"glReadPixels", (uintptr_t)&glReadPixelsHook},
	{"glShaderSource", (uintptr_t)&glShaderSourceHook},
};
static size_t gl_numhook = sizeof(gl_hook) / sizeof(*gl_hook);

void *dlsym_hook( void *handle, const char *symbol);

FILE *fopen_hook(char *file, char *mode) {
	char *s = strstr(file, "/ux0:");
	if (s)
		file = s + 1;
	else {
		s = strstr(file, "ux0:");
		if (!s) {
#ifdef STANDALONE_MODE
			s = strstr(file, "app0:");
			if (!s)
#endif
			{
#ifdef STANDALONE_MODE
				FILE *f = NULL;
				if (mode[0] != 'w') {
					char patched_fname[256];
					sprintf(patched_fname, "app0:%s", file);
					f = fopen(patched_fname, mode);
				}
				
				if (f)
					return f;
#endif
				char patched_fname[256];
				sprintf(patched_fname, "%s%s", data_path_root, file);
				return fopen(patched_fname, mode);
			}
		}
	}
	if (mode[0] == 'w')
		recursive_mkdir(file);
	return fopen(file, mode);
}

void *sceClibMemclr(void *dst, SceSize len) {
	return sceClibMemset(dst, 0, len);
}

void *sceClibMemset2(void *dst, SceSize len, int ch) {
	return sceClibMemset(dst, ch, len);
}

#define IS_LEAP(n) ((!(((n) + 1900) % 400) || (!(((n) + 1900) % 4) && (((n) + 1900) % 100))) != 0)
#define days_in_gregorian_cycle ((365 * 400) + 100 - 4 + 1)
static const int length_of_year[2] = { 365, 366 };
static const int julian_days_by_month[2][12] = {
	{0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334},
	{0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335},
};

struct tm *localtime64(const int64_t *time) {
	time_t _t = *time;
	return localtime(&_t);
}

struct tm *gmtime64(const int64_t *time) {
	time_t _t = *time;
	return gmtime(&_t);
}

int64_t mktime64(struct tm *time) {
	return mktime(time);
}

int64_t timegm64(const struct tm *date) {
	int64_t days = 0;
	int64_t seconds = 0;
	int64_t year;
	int64_t orig_year = (int64_t)date->tm_year;
	int cycles  = 0;
	if( orig_year > 100 ) {
		cycles = (orig_year - 100) / 400;
		orig_year -= cycles * 400;
		days += (int64_t)cycles * days_in_gregorian_cycle;
	}
	else if( orig_year < -300 ) {
		cycles = (orig_year - 100) / 400;
		orig_year -= cycles * 400;
		days += (int64_t)cycles * days_in_gregorian_cycle;
	}

	if( orig_year > 70 ) {
		year = 70;
		while( year < orig_year ) {
			days += length_of_year[IS_LEAP(year)];
			year++;
		}
	}
	else if ( orig_year < 70 ) {
		year = 69;
		do {
			days -= length_of_year[IS_LEAP(year)];
			year--;
		} while( year >= orig_year );
	}
	days += julian_days_by_month[IS_LEAP(orig_year)][date->tm_mon];
	days += date->tm_mday - 1;
	seconds = days * 60 * 60 * 24;
	seconds += date->tm_hour * 60 * 60;
	seconds += date->tm_min * 60;
	seconds += date->tm_sec;
	return seconds;
}

int is_prime(int n) {
	if (n <= 3)
		return 1;
	
	if (n % 2 == 0 || n % 3 == 0)
		return 0;
	
	for (int i = 5; i * i <= n; i = i + 6) {
		if (n % i == 0 || n % (i + 2) == 0)
			return 0;
	}
	
	return 1;
}

int _ZNSt6__ndk112__next_primeEj(void *this, int n) {
	if (n <= 1)
		return 2;
	
	while (!is_prime(n)) {
		n++;
	}
	
	return n;
}

void __cxa_throw_hook(void *thrown_exception, void *tinfo, void (*dest)(void *)) {
	if (tinfo == so_symbol(&yoyoloader_mod, "_ZTI14YYGMLException")) {
		void (* YYCatchGMLException)(void *exception) = so_symbol(&yoyoloader_mod, "_Z19YYCatchGMLExceptionRK14YYGMLException");
		YYCatchGMLException(thrown_exception);
		if (dest)
			dest(thrown_exception);
	} else
		__cxa_throw(thrown_exception, tinfo, dest);
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
	return vglMalloc(length);
}

int munmap(void *addr, size_t length) {
	vglFree(addr);
	return 0;
}

int nanosleep_hook(const struct timespec *req, struct timespec *rem) {
	const uint32_t usec = req->tv_sec * 1000 * 1000 + req->tv_nsec / 1000;
	return sceKernelDelayThreadCB(usec);
}

size_t __strlen_chk(const char *s, size_t s_len) {
	return strlen(s);
}

int __vsprintf_chk(char* dest, int flags, size_t dest_len_from_compiler, const char *format, va_list va) {
	return vsprintf(dest, format, va);
}

void *__memmove_chk(void *dest, const void *src, size_t len, size_t dstlen) {
	return memmove(dest, src, len);
}

void *__memset_chk(void *dest, int val, size_t len, size_t dstlen) {
	return memset(dest, val, len);
}

size_t __strlcat_chk (char *dest, char *src, size_t len, size_t dstlen) {
	return strlcat(dest, src, len);
}

size_t __strlcpy_chk (char *dest, char *src, size_t len, size_t dstlen) {
	return strlcpy(dest, src, len);
}

char* __strchr_chk(const char* p, int ch, size_t s_len) {
	return strchr(p, ch);
}

char *__strcat_chk(char *dest, const char *src, size_t destlen) {
	return strcat(dest, src);
}

char *__strrchr_chk(const char *p, int ch, size_t s_len) {
	return strrchr(p, ch);
}

char *__strcpy_chk(char *dest, const char *src, size_t destlen) {
	return strcpy(dest, src);
}

char *__strncat_chk(char *s1, const char *s2, size_t n, size_t s1len) {
	return strncat(s1, s2, n);
}

void *__memcpy_chk(void *dest, const void *src, size_t len, size_t destlen) {
	return memcpy(dest, src, len);
}

int __vsnprintf_chk(char *s, size_t maxlen, int flag, size_t slen, const char *format, va_list args) {
	return vsnprintf(s, maxlen, format, args);
}

int posix_memalign(void **memptr, size_t alignment, size_t size) {
	*memptr = vglMemalign(alignment, size);
	return 0;
}

static so_default_dynlib net_dynlib[] = {
	{ "bind", (uintptr_t)&bind },
	{ "socket", (uintptr_t)&socket },
};

void abort_hook() {
	debugPrintf("abort called by %p %s\n", __builtin_return_address(0));
	sceKernelExitProcess(0);
}

static so_default_dynlib default_dynlib[] = {
	{ "SL_IID_ANDROIDSIMPLEBUFFERQUEUE", (uintptr_t)&SL_IID_ANDROIDSIMPLEBUFFERQUEUE},
	{ "SL_IID_AUDIOIODEVICECAPABILITIES", (uintptr_t)&SL_IID_AUDIOIODEVICECAPABILITIES},
	{ "SL_IID_BUFFERQUEUE", (uintptr_t)&SL_IID_BUFFERQUEUE},
	{ "SL_IID_DYNAMICSOURCE", (uintptr_t)&SL_IID_DYNAMICSOURCE},
	{ "SL_IID_ENGINE", (uintptr_t)&SL_IID_ENGINE},
	{ "SL_IID_LED", (uintptr_t)&SL_IID_LED},
	{ "SL_IID_NULL", (uintptr_t)&SL_IID_NULL},
	{ "SL_IID_METADATAEXTRACTION", (uintptr_t)&SL_IID_METADATAEXTRACTION},
	{ "SL_IID_METADATATRAVERSAL", (uintptr_t)&SL_IID_METADATATRAVERSAL},
	{ "SL_IID_OBJECT", (uintptr_t)&SL_IID_OBJECT},
	{ "SL_IID_OUTPUTMIX", (uintptr_t)&SL_IID_OUTPUTMIX},
	{ "SL_IID_PLAY", (uintptr_t)&SL_IID_PLAY},
	{ "SL_IID_VIBRA", (uintptr_t)&SL_IID_VIBRA},
	{ "SL_IID_VOLUME", (uintptr_t)&SL_IID_VOLUME},
	{ "SL_IID_PREFETCHSTATUS", (uintptr_t)&SL_IID_PREFETCHSTATUS},
	{ "SL_IID_PLAYBACKRATE", (uintptr_t)&SL_IID_PLAYBACKRATE},
	{ "SL_IID_SEEK", (uintptr_t)&SL_IID_SEEK},
	{ "SL_IID_RECORD", (uintptr_t)&SL_IID_RECORD},
	{ "SL_IID_EQUALIZER", (uintptr_t)&SL_IID_EQUALIZER},
	{ "SL_IID_DEVICEVOLUME", (uintptr_t)&SL_IID_DEVICEVOLUME},
	{ "SL_IID_PRESETREVERB", (uintptr_t)&SL_IID_PRESETREVERB},
	{ "SL_IID_ENVIRONMENTALREVERB", (uintptr_t)&SL_IID_ENVIRONMENTALREVERB},
	{ "SL_IID_EFFECTSEND", (uintptr_t)&SL_IID_EFFECTSEND},
	{ "SL_IID_3DGROUPING", (uintptr_t)&SL_IID_3DGROUPING},
	{ "SL_IID_3DCOMMIT", (uintptr_t)&SL_IID_3DCOMMIT},
	{ "SL_IID_3DLOCATION", (uintptr_t)&SL_IID_3DLOCATION},
	{ "SL_IID_3DDOPPLER", (uintptr_t)&SL_IID_3DDOPPLER},
	{ "SL_IID_3DSOURCE", (uintptr_t)&SL_IID_3DSOURCE},
	{ "SL_IID_3DMACROSCOPIC", (uintptr_t)&SL_IID_3DMACROSCOPIC},
	{ "SL_IID_MUTESOLO", (uintptr_t)&SL_IID_MUTESOLO},
	{ "SL_IID_DYNAMICINTERFACEMANAGEMENT", (uintptr_t)&SL_IID_DYNAMICINTERFACEMANAGEMENT},
	{ "SL_IID_MIDIMESSAGE", (uintptr_t)&SL_IID_MIDIMESSAGE},
	{ "SL_IID_MIDIMUTESOLO", (uintptr_t)&SL_IID_MIDIMUTESOLO},
	{ "SL_IID_MIDITEMPO", (uintptr_t)&SL_IID_MIDITEMPO},
	{ "SL_IID_MIDITIME", (uintptr_t)&SL_IID_MIDITIME},
	{ "SL_IID_AUDIODECODERCAPABILITIES", (uintptr_t)&SL_IID_AUDIODECODERCAPABILITIES},
	{ "SL_IID_AUDIOENCODERCAPABILITIES", (uintptr_t)&SL_IID_AUDIOENCODERCAPABILITIES},
	{ "SL_IID_AUDIOENCODER", (uintptr_t)&SL_IID_AUDIOENCODER},
	{ "SL_IID_BASSBOOST", (uintptr_t)&SL_IID_BASSBOOST},
	{ "SL_IID_PITCH", (uintptr_t)&SL_IID_PITCH},
	{ "SL_IID_RATEPITCH", (uintptr_t)&SL_IID_RATEPITCH},
	{ "SL_IID_VIRTUALIZER", (uintptr_t)&SL_IID_VIRTUALIZER},
	{ "SL_IID_VISUALIZATION", (uintptr_t)&SL_IID_VISUALIZATION},
	{ "SL_IID_ENGINECAPABILITIES", (uintptr_t)&SL_IID_ENGINECAPABILITIES},
	{ "SL_IID_THREADSYNC", (uintptr_t)&SL_IID_THREADSYNC},
	{ "SL_IID_ANDROIDEFFECT", (uintptr_t)&SL_IID_ANDROIDEFFECT},
	{ "SL_IID_ANDROIDEFFECTSEND", (uintptr_t)&SL_IID_ANDROIDEFFECTSEND},
	{ "SL_IID_ANDROIDEFFECTCAPABILITIES", (uintptr_t)&SL_IID_ANDROIDEFFECTCAPABILITIES},
	{ "SL_IID_ANDROIDCONFIGURATION", (uintptr_t)&SL_IID_ANDROIDCONFIGURATION},
	{ "slCreateEngine", (uintptr_t)&slCreateEngine },
	{ "AAssetManager_open", (uintptr_t)&AAssetManager_open},
	{ "AAsset_close", (uintptr_t)&AAsset_close},
	{ "AAssetManager_fromJava", (uintptr_t)&AAssetManager_fromJava},
	{ "AAsset_read", (uintptr_t)&AAsset_read},
	{ "AAsset_seek", (uintptr_t)&AAsset_seek},
	{ "AAsset_getLength", (uintptr_t)&AAsset_getLength},
	{ "_tolower_tab_", (uintptr_t)&BIONIC_tolower_tab_},
	{ "_toupper_tab_", (uintptr_t)&BIONIC_toupper_tab_},
	{ "_Znaj", (uintptr_t)&_Znaj },
	{ "_Znwj", (uintptr_t)&_Znwj },
	{ "_ZdaPv", (uintptr_t)&_ZdaPv },
	{ "_ZdlPv", (uintptr_t)&_ZdlPv },
	{ "_ZTVN10__cxxabiv117__class_type_infoE", (uintptr_t)&_ZTVN10__cxxabiv117__class_type_infoE},
	{ "_ZTVN10__cxxabiv120__si_class_type_infoE", (uintptr_t)&_ZTVN10__cxxabiv120__si_class_type_infoE},
	{ "_ZNSt12length_errorD1Ev", (uintptr_t)&_ZNSt12length_errorD1Ev},
	{ "_ZNSt13runtime_errorD1Ev", (uintptr_t)&_ZNSt13runtime_errorD1Ev},
	{ "_ZTVSt12length_error", (uintptr_t)&_ZTVSt12length_error},
	{ "_ZNSt6__ndk112__next_primeEj", &_ZNSt6__ndk112__next_primeEj},
	{ "__aeabi_f2d", (uintptr_t)&__aeabi_f2d },
	{ "__aeabi_l2d", (uintptr_t)&__aeabi_l2d },
	{ "__aeabi_l2f", (uintptr_t)&__aeabi_l2f },
	{ "__aeabi_d2uiz", (uintptr_t)&__aeabi_d2uiz },
	{ "__aeabi_d2ulz", (uintptr_t)&__aeabi_d2ulz },
	{ "__aeabi_d2lz", (uintptr_t)&__aeabi_d2lz },
	{ "__aeabi_ui2d", (uintptr_t)&__aeabi_ui2d },
	{ "__aeabi_ul2d", (uintptr_t)&__aeabi_ul2d },
	{ "__aeabi_i2d", (uintptr_t)&__aeabi_i2d },
	{ "__aeabi_idivmod", (uintptr_t)&__aeabi_idivmod },
	{ "__aeabi_ldivmod", (uintptr_t)&__aeabi_ldivmod },
	{ "__aeabi_uidivmod", (uintptr_t)&__aeabi_uidivmod },
	{ "__aeabi_uldivmod", (uintptr_t)&__aeabi_uldivmod },
	{ "__aeabi_ddiv", (uintptr_t)&__aeabi_ddiv },
	{ "__aeabi_idiv", (uintptr_t)&__aeabi_idiv },
	{ "__aeabi_uidiv", (uintptr_t)&__aeabi_uidiv },
	{ "__aeabi_dadd", (uintptr_t)&__aeabi_dadd },
	{ "__aeabi_dcmplt", (uintptr_t)&__aeabi_dcmplt },
	{ "__aeabi_dcmpge", (uintptr_t)&__aeabi_dcmpge },
	{ "__aeabi_dcmpgt", (uintptr_t)&__aeabi_dcmpgt },
	{ "__aeabi_dmul", (uintptr_t)&__aeabi_dmul },
	{ "__aeabi_dsub", (uintptr_t)&__aeabi_dsub },
	{ "__aeabi_memclr", (uintptr_t)&sceClibMemclr },
	{ "__aeabi_memclr4", (uintptr_t)&sceClibMemclr },
	{ "__aeabi_memclr8", (uintptr_t)&sceClibMemclr },
	{ "__aeabi_memcpy4", (uintptr_t)&sceClibMemcpy },
	{ "__aeabi_memcpy8", (uintptr_t)&sceClibMemcpy },
	{ "__aeabi_memmove4", (uintptr_t)&sceClibMemmove },
	{ "__aeabi_memmove8", (uintptr_t)&sceClibMemmove },
	{ "__aeabi_memcpy", (uintptr_t)&sceClibMemcpy },
	{ "__aeabi_memmove", (uintptr_t)&sceClibMemmove },
	{ "__aeabi_memset", (uintptr_t)&sceClibMemset2 },
	{ "__aeabi_memset4", (uintptr_t)&sceClibMemset2 },
	{ "__aeabi_memset8", (uintptr_t)&sceClibMemset2 },
	{ "__aeabi_atexit", (uintptr_t)&__aeabi_atexit },
	{ "__android_log_print", (uintptr_t)&__android_log_print },
	{ "__android_log_vprint", (uintptr_t)&__android_log_vprint },
	{ "__ctype_get_mb_cur_max", (uintptr_t)&__ctype_get_mb_cur_max },
	{ "__cxa_allocate_exception", (uintptr_t)&__cxa_allocate_exception },
	{ "__cxa_atexit", (uintptr_t)&__cxa_atexit },
	{ "__cxa_finalize", (uintptr_t)&__cxa_finalize },
	{ "__cxa_guard_acquire", (uintptr_t)&__cxa_guard_acquire },
	{ "__cxa_guard_release", (uintptr_t)&__cxa_guard_release },
	{ "__cxa_pure_virtual", (uintptr_t)&__cxa_pure_virtual },
	{ "__cxa_thread_atexit_impl", (uintptr_t)&ret0 },
	{ "__cxa_throw", (uintptr_t)&__cxa_throw_hook },
	{ "__errno", (uintptr_t)&__errno },
	{ "__gnu_unwind_frame", (uintptr_t)&__gnu_unwind_frame },
	{ "__gnu_Unwind_Find_exidx", (uintptr_t)&ret0 },
	{ "__memcpy_chk", (uintptr_t)&__memcpy_chk },
	{ "__memmove_chk", (uintptr_t)&__memmove_chk },
	{ "__memset_chk", (uintptr_t)&__memset_chk },
	{ "__progname", (uintptr_t)&__progname },
	{ "__page_size", (uintptr_t)&__page_size },
	{ "__sF", (uintptr_t)&__sF_fake },
	{ "__stack_chk_fail", (uintptr_t)&__stack_chk_fail_fake },
	{ "__stack_chk_guard", (uintptr_t)&__stack_chk_guard_fake },
	{ "__strcat_chk", (uintptr_t)&__strcat_chk },
	{ "__strchr_chk", (uintptr_t)&__strchr_chk },
	{ "__strcpy_chk", (uintptr_t)&__strcpy_chk },
	{ "__strlcat_chk", (uintptr_t)&__strlcat_chk },
	{ "__strlcpy_chk", (uintptr_t)&__strlcpy_chk },
	{ "__strlen_chk", (uintptr_t)&__strlen_chk },
	{ "__strncat_chk", (uintptr_t)&__strncat_chk },
	{ "__strrchr_chk", (uintptr_t)&__strrchr_chk },
	{ "__vsprintf_chk", (uintptr_t)&__vsprintf_chk },
	{ "__vsnprintf_chk", (uintptr_t)&__vsnprintf_chk },
	{ "_ctype_", (uintptr_t)&BIONIC_ctype_},
	{ "abort", (uintptr_t)&abort_hook },
	//{ "accept", (uintptr_t)&accept },
	{ "acos", (uintptr_t)&acos },
	{ "acosf", (uintptr_t)&acosf },
	{ "alBufferData", (uintptr_t)&alBufferData },
	{ "alDeleteBuffers", (uintptr_t)&alDeleteBuffers },
	{ "alDeleteSources", (uintptr_t)&alDeleteSources },
	{ "alDistanceModel", (uintptr_t)&alDistanceModel },
	{ "alGenBuffers", (uintptr_t)&alGenBuffers },
	{ "alGenSources", (uintptr_t)&alGenSources },
	{ "alcGetCurrentContext", (uintptr_t)&alcGetCurrentContext },
	{ "alGetBufferi", (uintptr_t)&alGetBufferi },
	{ "alGetError", (uintptr_t)&alGetError },
	{ "alGetSourcei", (uintptr_t)&alGetSourcei },
	{ "alGetSourcef", (uintptr_t)&alGetSourcef },
	{ "alIsBuffer", (uintptr_t)&alIsBuffer },
	{ "alListener3f", (uintptr_t)&alListener3f },
	{ "alListenerf", (uintptr_t)&alListenerf },
	{ "alListenerfv", (uintptr_t)&alListenerfv },
	{ "alSource3f", (uintptr_t)&alSource3f },
	{ "alSourcePause", (uintptr_t)&alSourcePause },
	{ "alSourcePlay", (uintptr_t)&alSourcePlay },
	{ "alSourceQueueBuffers", (uintptr_t)&alSourceQueueBuffers },
	{ "alSourceStop", (uintptr_t)&alSourceStop },
	{ "alSourceUnqueueBuffers", (uintptr_t)&alSourceUnqueueBuffers },
	{ "alSourcef", (uintptr_t)&alSourcef },
	{ "alSourcei", (uintptr_t)&alSourcei },
	{ "alcCaptureSamples", (uintptr_t)&alcCaptureSamples },
	{ "alcCaptureStart", (uintptr_t)&alcCaptureStart },
	{ "alcCaptureStop", (uintptr_t)&alcCaptureStop },
	{ "alcCaptureOpenDevice", (uintptr_t)&alcCaptureOpenDevice },
	{ "alcCloseDevice", (uintptr_t)&alcCloseDevice },
	{ "alcCreateContext", (uintptr_t)&alcCreateContext },
	{ "alcGetContextsDevice", (uintptr_t)&alcGetContextsDevice },
	{ "alcGetError", (uintptr_t)&alcGetError },
	{ "alcGetIntegerv", (uintptr_t)&alcGetIntegerv },
	{ "alcGetString", (uintptr_t)&alcGetString },
	{ "alcMakeContextCurrent", (uintptr_t)&alcMakeContextCurrent },
	{ "alcDestroyContext", (uintptr_t)&alcDestroyContext },
	{ "alcOpenDevice", (uintptr_t)&alcOpenDevice },
	{ "alcProcessContext", (uintptr_t)&alcProcessContext },
	{ "alcPauseCurrentDevice", (uintptr_t)&ret0 },
	{ "alcResumeCurrentDevice", (uintptr_t)&ret0 },
	{ "alcSuspendContext", (uintptr_t)&alcSuspendContext },
	{ "asin", (uintptr_t)&asin },
	{ "asinf", (uintptr_t)&asinf },
	{ "asinh", (uintptr_t)&asinh },
	{ "atan", (uintptr_t)&atan },
	{ "atan2", (uintptr_t)&atan2 },
	{ "atan2f", (uintptr_t)&atan2f },
	{ "atanf", (uintptr_t)&atanf },
	{ "atoi", (uintptr_t)&atoi },
	{ "atol", (uintptr_t)&atol },
	{ "atoll", (uintptr_t)&atoll },
	{ "bind", (uintptr_t)&bind },
	{ "bsearch", (uintptr_t)&bsearch },
	{ "btowc", (uintptr_t)&btowc },
	{ "calloc", (uintptr_t)&calloc },
	{ "ceil", (uintptr_t)&ceil },
	{ "ceilf", (uintptr_t)&ceilf },
	{ "clearerr", (uintptr_t)&clearerr },
	{ "clock_gettime", (uintptr_t)&clock_gettime },
	{ "close", (uintptr_t)&close },
	{ "compress", (uintptr_t)&compress },	
	//{ "connect", (uintptr_t)&connect },
	{ "cos", (uintptr_t)&cos },
	{ "cosf", (uintptr_t)&cosf },
	{ "cosh", (uintptr_t)&cosh },
	{ "crc32", (uintptr_t)&crc32 },
	{ "deflate", (uintptr_t)&deflate },
	{ "deflateEnd", (uintptr_t)&deflateEnd },
	{ "deflateInit_", (uintptr_t)&deflateInit_ },
	{ "deflateInit2_", (uintptr_t)&deflateInit2_ },
	{ "deflateReset", (uintptr_t)&deflateReset },
	{ "dlclose", (uintptr_t)&ret0 },
	{ "dlopen", (uintptr_t)&dlopen_hook },
	{ "dlsym", (uintptr_t)&dlsym_hook },
	{ "dlerror", (uintptr_t)&ret0 },
	{ "exit", (uintptr_t)&exit },
	{ "exp", (uintptr_t)&exp },
	{ "expf", (uintptr_t)&expf },
	{ "fclose", (uintptr_t)&fclose },
	{ "fcntl", (uintptr_t)&ret0 },
	{ "fdopen", (uintptr_t)&fdopen },
	{ "feof", (uintptr_t)&feof },
	{ "ferror", (uintptr_t)&ferror },
	{ "fflush", (uintptr_t)&fflush },
	{ "fgetpos", (uintptr_t)&fgetpos },
	{ "fgetc", (uintptr_t)&fgetc },
	{ "fgets", (uintptr_t)&fgets },
	{ "floor", (uintptr_t)&floor },
	{ "floorf", (uintptr_t)&floorf },
	{ "fmax", (uintptr_t)&fmax },
	{ "fmaxf", (uintptr_t)&fmaxf },
	{ "fmin", (uintptr_t)&fmin },
	{ "fminf", (uintptr_t)&fminf },
	{ "fmod", (uintptr_t)&fmod },
	{ "fmodf", (uintptr_t)&fmodf },
	{ "fopen", (uintptr_t)&fopen_hook },
	{ "fprintf", (uintptr_t)&fprintf },
	{ "fputc", (uintptr_t)&fputc },
	{ "fputs", (uintptr_t)&fputs },
	{ "fread", (uintptr_t)&fread },
	{ "free", (uintptr_t)&vglFree },
	{ "freelocale", (uintptr_t)&freelocale },
	//{ "freeaddrinfo", (uintptr_t)&freeaddrinfo },
	{ "frexp", (uintptr_t)&frexp },
	{ "frexpf", (uintptr_t)&frexpf },
	{ "fscanf", (uintptr_t)&fscanf },
	{ "fseek", (uintptr_t)&fseek },
	{ "fseeko", (uintptr_t)&fseeko },
	{ "fstat", (uintptr_t)&fstat_hook },
	{ "ftell", (uintptr_t)&ftell },
	{ "ftello", (uintptr_t)&ftello },
	{ "fwrite", (uintptr_t)&fwrite },
	{ "getaddrinfo", (uintptr_t)&getaddrinfo },
	{ "getc", (uintptr_t)&getc },
	{ "getpid", (uintptr_t)&ret0 },
	{ "getenv", (uintptr_t)&ret0 },
	//{ "getsockopt", (uintptr_t)&getsockopt },
	{ "getwc", (uintptr_t)&getwc },
	{ "gettimeofday", (uintptr_t)&gettimeofday },
	{ "glAlphaFunc", (uintptr_t)&glAlphaFunc },
	{ "glBindBuffer", (uintptr_t)&glBindBuffer },
	{ "glBindFramebufferOES", (uintptr_t)&glBindFramebufferHook },
	{ "glBindTexture", (uintptr_t)&glBindTexture },
	{ "glBlendFunc", (uintptr_t)&glBlendFunc },
	{ "glBufferData", (uintptr_t)&glBufferData },
	{ "glCheckFramebufferStatusOES", (uintptr_t)&glCheckFramebufferStatus },
	{ "glClear", (uintptr_t)&glClear },
	{ "glClearColor", (uintptr_t)&glClearColor },
	{ "glClearDepthf", (uintptr_t)&glClearDepthf },
	{ "glColorMask", (uintptr_t)&glColorMask },
	{ "glColorPointer", (uintptr_t)&glColorPointer },
	{ "glDeleteBuffers", (uintptr_t)&glDeleteBuffers },
	{ "glDeleteFramebuffersOES", (uintptr_t)&glDeleteFramebuffers },
	{ "glDeleteTextures", (uintptr_t)&glDeleteTextures },
	{ "glDepthFunc", (uintptr_t)&glDepthFunc },
	{ "glDepthMask", (uintptr_t)&glDepthMask },
	{ "glDepthRangef", (uintptr_t)&glDepthRangef },
	{ "glDisable", (uintptr_t)&glDisable },
	{ "glDisableClientState", (uintptr_t)&glDisableClientState },
	{ "glDrawArrays", (uintptr_t)&glDrawArrays },
	{ "glEnable", (uintptr_t)&glEnable },
	{ "glEnableClientState", (uintptr_t)&glEnableClientState },
	{ "glFlush", (uintptr_t)&glFlush },
	{ "glFogf", (uintptr_t)&glFogf },
	{ "glFogfv", (uintptr_t)&glFogfv },
	{ "glFramebufferTexture2DOES", (uintptr_t)&glFramebufferTexture2D },
	{ "glFrontFace", (uintptr_t)&glFrontFace },
	{ "glGenBuffers", (uintptr_t)&glGenBuffers },
	{ "glGenFramebuffersOES", (uintptr_t)&glGenFramebuffers },
	{ "glGenTextures", (uintptr_t)&glGenTextures },
	{ "glGetError", (uintptr_t)&glGetError },
	{ "glGetString", (uintptr_t)&glGetString },
	{ "glHint", (uintptr_t)&ret0 },
	{ "glLightModelfv", (uintptr_t)&glLightModelfv },
	{ "glLightf", (uintptr_t)&ret0 },
	{ "glLightfv", (uintptr_t)&glLightfv },
	{ "glLoadIdentity", (uintptr_t)&glLoadIdentity },
	{ "glLoadMatrixf", (uintptr_t)&glLoadMatrixf },
	{ "glMaterialfv", (uintptr_t)&glMaterialfv },
	{ "glMatrixMode", (uintptr_t)&glMatrixMode },
	{ "glNormalPointer", (uintptr_t)&ret0 },
	{ "glPixelStorei", (uintptr_t)&ret0 },
	{ "glPopMatrix", (uintptr_t)&glPopMatrix },
	{ "glPushMatrix", (uintptr_t)&glPushMatrix },
	{ "glReadPixels", (uintptr_t)&glReadPixelsHook },
	{ "glScissor", (uintptr_t)&glScissor },
	{ "glTexCoordPointer", (uintptr_t)&glTexCoordPointer },
	{ "glTexEnvi", (uintptr_t)&glTexEnvi },
	{ "glTexImage2D", (uintptr_t)&glTexImage2D },
	{ "glTexParameterf", (uintptr_t)&glTexParameterfHook },
	{ "glTexParameteri", (uintptr_t)&glTexParameteriHook },
	{ "glVertexPointer", (uintptr_t)&glVertexPointer },
	{ "glViewport", (uintptr_t)&glViewport },
	{ "gmtime64", (uintptr_t)&gmtime64 },
	{ "inet_addr", (uintptr_t)&inet_addr },
	{ "inet_ntoa", (uintptr_t)&inet_ntoa },
	//{ "inet_ntop", (uintptr_t)&inet_ntop },
	//{ "inet_pton", (uintptr_t)&inet_pton },
	{ "inflate", (uintptr_t)&inflate },
	{ "inflateEnd", (uintptr_t)&inflateEnd },
	{ "inflateInit_", (uintptr_t)&inflateInit_ },
	{ "inflateInit2_", (uintptr_t)&inflateInit2_ },
	{ "inflateReset", (uintptr_t)&inflateReset },
	{ "ioctl", (uintptr_t)&ret0 },
	{ "isalnum", (uintptr_t)&isalnum },
	{ "isalpha", (uintptr_t)&isalpha },
	{ "iscntrl", (uintptr_t)&iscntrl },
	{ "islower", (uintptr_t)&islower },
	{ "isnan", (uintptr_t)&isnan },
	{ "ispunct", (uintptr_t)&ispunct },
	{ "isprint", (uintptr_t)&isprint },
	{ "isspace", (uintptr_t)&isspace },
	{ "isupper", (uintptr_t)&isupper },
	{ "iswalpha", (uintptr_t)&iswalpha },
	{ "iswcntrl", (uintptr_t)&iswcntrl },
	{ "iswctype", (uintptr_t)&iswctype },
	{ "iswdigit", (uintptr_t)&iswdigit },
	{ "iswdigit", (uintptr_t)&iswdigit },
	{ "iswlower", (uintptr_t)&iswlower },
	{ "iswprint", (uintptr_t)&iswprint },
	{ "iswpunct", (uintptr_t)&iswpunct },
	{ "iswspace", (uintptr_t)&iswspace },
	{ "iswupper", (uintptr_t)&iswupper },
	{ "iswxdigit", (uintptr_t)&iswxdigit },
	{ "isxdigit", (uintptr_t)&isxdigit },
	{ "ldexp", (uintptr_t)&ldexp },
	{ "ldiv", (uintptr_t)&ldiv },
	//{ "listen", (uintptr_t)&listen },
	{ "llrint", (uintptr_t)&llrint },
	{ "localtime_r", (uintptr_t)&localtime_r },
	{ "localtime64", (uintptr_t)&localtime64 },
	{ "log", (uintptr_t)&log },
	{ "logf", (uintptr_t)&logf },
	{ "log2", (uintptr_t)&log2 },
	{ "log10", (uintptr_t)&log10 },
	{ "log10f", (uintptr_t)&log10f },
	{ "longjmp", (uintptr_t)&longjmp },
	{ "lrand48", (uintptr_t)&lrand48 },
	{ "lrint", (uintptr_t)&lrint },
	{ "lrintf", (uintptr_t)&lrintf },
	{ "lround", (uintptr_t)&lround },
	{ "lroundf", (uintptr_t)&lroundf },
	{ "lseek", (uintptr_t)&lseek },
	{ "malloc", (uintptr_t)&vglMalloc },
	{ "mbtowc", (uintptr_t)&mbtowc },
	{ "mbrlen", (uintptr_t)&mbrlen },
	{ "mbrtowc", (uintptr_t)&mbrtowc },
	{ "mbsrtowcs", (uintptr_t)&mbsrtowcs },
	{ "memalign", (uintptr_t)&vglMemalign },
	{ "memchr", (uintptr_t)&sceClibMemchr },
	{ "memcmp", (uintptr_t)&memcmp },
	{ "memcpy", (uintptr_t)&sceClibMemcpy },
	{ "memmove", (uintptr_t)&sceClibMemmove },
	{ "memset", (uintptr_t)&sceClibMemset },
	{ "mkdir", (uintptr_t)&mkdir },
	{ "mktime", (uintptr_t)&mktime },
	{ "mktime64", (uintptr_t)&mktime64 },
	{ "mmap", (uintptr_t)&mmap },
	{ "modf", (uintptr_t)&modf },
	{ "modff", (uintptr_t)&modff },
	{ "munmap", (uintptr_t)&munmap },
	{ "nanosleep", (uintptr_t)&nanosleep_hook },
	{ "newlocale", (uintptr_t)&newlocale },
	{ "open", (uintptr_t)&open },
	{ "posix_memalign", (uintptr_t)&posix_memalign },
	{ "pow", (uintptr_t)&pow },
	{ "powf", (uintptr_t)&powf },
	{ "printf", (uintptr_t)&debugPrintf },
	{ "pthread_attr_destroy", (uintptr_t)&ret0 },
	{ "pthread_attr_init", (uintptr_t)&ret0 },
	{ "pthread_attr_setdetachstate", (uintptr_t)&ret0 },
	{ "pthread_cond_broadcast", (uintptr_t)&pthread_cond_broadcast_fake},
	{ "pthread_cond_destroy", (uintptr_t)&pthread_cond_destroy_fake},
	{ "pthread_cond_init", (uintptr_t)&pthread_cond_init_fake},
	{ "pthread_cond_wait", (uintptr_t)&pthread_cond_wait_fake},
	{ "pthread_create", (uintptr_t)&pthread_create_fake},
	{ "pthread_join", (uintptr_t)&pthread_join},
	{ "pthread_key_create", (uintptr_t)&pthread_key_create},
	{ "pthread_key_delete", (uintptr_t)&pthread_key_delete},
	{ "pthread_mutex_destroy", (uintptr_t)&pthread_mutex_destroy_fake},
	{ "pthread_mutex_init", (uintptr_t)&pthread_mutex_init_fake},
	{ "pthread_mutex_lock", (uintptr_t)&pthread_mutex_lock_fake},
	{ "pthread_mutex_unlock", (uintptr_t)&pthread_mutex_unlock_fake},
	{ "pthread_mutexattr_destroy", (uintptr_t)&pthread_mutexattr_destroy},
	{ "pthread_mutexattr_init", (uintptr_t)&pthread_mutexattr_init},
	{ "pthread_mutexattr_settype", (uintptr_t)&pthread_mutexattr_settype},
	{ "pthread_rwlock_destroy", (uintptr_t)&pthread_rwlock_destroy_fake },
	{ "pthread_rwlock_init", (uintptr_t)&pthread_rwlock_init_fake },
	{ "pthread_rwlock_rdlock", (uintptr_t)&pthread_rwlock_rdlock_fake },
	{ "pthread_rwlock_unlock", (uintptr_t)&pthread_rwlock_unlock_fake },
	{ "pthread_rwlock_wrlock", (uintptr_t)&pthread_rwlock_wrlock_fake },
	{ "pthread_once", (uintptr_t)&pthread_once_fake},
	{ "pthread_setspecific", (uintptr_t)&pthread_setspecific},
	{ "pthread_getspecific", (uintptr_t)&pthread_getspecific},
	{ "putc", (uintptr_t)&putc },
	{ "putwc", (uintptr_t)&putwc },
	{ "qsort", (uintptr_t)&qsort },
	{ "read", (uintptr_t)&read },
	{ "realloc", (uintptr_t)&vglRealloc },
	//{ "recv", (uintptr_t)&recv },
	//{ "recvfrom", (uintptr_t)&recvfrom },
	{ "remove", (uintptr_t)&sceIoRemove },
	{ "rename", (uintptr_t)&sceIoRename },
	{ "rint", (uintptr_t)&rint },
	{ "round", (uintptr_t)&round },
	{ "roundf", (uintptr_t)&roundf },
	{ "scandir", (uintptr_t)&scandir_hook },
	//{ "send", (uintptr_t)&send },
	//{ "sendto", (uintptr_t)&sendto },
	{ "setenv", (uintptr_t)&ret0 },
	{ "setjmp", (uintptr_t)&setjmp },
	{ "setlocale", (uintptr_t)&ret0 },
	//{ "setsockopt", (uintptr_t)&setsockopt },
	{ "setvbuf", (uintptr_t)&setvbuf },
	{ "sin", (uintptr_t)&sin },
	{ "sincos", (uintptr_t)&sincos },
	{ "sincosf", (uintptr_t)&sincosf },
	{ "sinf", (uintptr_t)&sinf },
	{ "sinh", (uintptr_t)&sinh },
	{ "snprintf", (uintptr_t)&snprintf },
	{ "socket", (uintptr_t)&socket },
	{ "sprintf", (uintptr_t)&sprintf },
	{ "sqrt", (uintptr_t)&sqrt },
	{ "sqrtf", (uintptr_t)&sqrtf },
	{ "srand", (uintptr_t)&srand },
	{ "srand48", (uintptr_t)&srand48 },
	{ "sscanf", (uintptr_t)&sscanf },
	{ "stat", (uintptr_t)&stat_hook },
	{ "strcasecmp", (uintptr_t)&strcasecmp },
	{ "strcat", (uintptr_t)&strcat },
	{ "strchr", (uintptr_t)&strchr },
	{ "strcmp", (uintptr_t)&strcmp },
	{ "strcoll", (uintptr_t)&strcoll },
	{ "strcpy", (uintptr_t)&strcpy },
	{ "strcspn", (uintptr_t)&strcspn },
	{ "strdup", (uintptr_t)&strdup },
	{ "strndup", (uintptr_t)&strndup },
	{ "strerror", (uintptr_t)&strerror },
	{ "strerror_r", (uintptr_t)&strerror_r },
	{ "strftime", (uintptr_t)&strftime },
	{ "strlen", (uintptr_t)&strlen },
	{ "strncasecmp", (uintptr_t)&sceClibStrncasecmp },
	{ "strncat", (uintptr_t)&sceClibStrncat },
	{ "strncmp", (uintptr_t)&sceClibStrncmp },
	{ "strncpy", (uintptr_t)&sceClibStrncpy },
	{ "strpbrk", (uintptr_t)&strpbrk },
	{ "strrchr", (uintptr_t)&sceClibStrrchr },
	{ "strstr", (uintptr_t)&sceClibStrstr },
	{ "strtof", (uintptr_t)&strtof },
	{ "strtod", (uintptr_t)&strtod },
	{ "strtoimax", (uintptr_t)&strtoimax },
	{ "strtok", (uintptr_t)&strtok },
	{ "strtol", (uintptr_t)&strtol },
	{ "strtold", (uintptr_t)&strtold },
	{ "strtoll", (uintptr_t)&strtoll },
	{ "strtoul", (uintptr_t)&strtoul },
	{ "strtoull", (uintptr_t)&strtoull },
	{ "strtoumax", (uintptr_t)&strtoumax },
	{ "strxfrm", (uintptr_t)&strxfrm },
	{ "swprintf", (uintptr_t)&swprintf },
	{ "sysconf", (uintptr_t)&ret0 },
	{ "tan", (uintptr_t)&tan },
	{ "tanf", (uintptr_t)&tanf },
	{ "tanh", (uintptr_t)&tanh },
	{ "time", (uintptr_t)&time },
	{ "timegm64", (uintptr_t)&timegm64 },
	{ "tolower", (uintptr_t)&tolower },
	{ "toupper", (uintptr_t)&toupper },
	{ "towlower", (uintptr_t)&towlower },
	{ "towupper", (uintptr_t)&towupper },
	{ "ungetc", (uintptr_t)&ungetc },
	{ "ungetwc", (uintptr_t)&ungetwc },
	{ "uselocale", (uintptr_t)&uselocale },
	{ "usleep", (uintptr_t)&usleep },
	{ "vasprintf", (uintptr_t)&vasprintf },
	{ "vfprintf", (uintptr_t)&vfprintf },
	{ "vprintf", (uintptr_t)&vprintf },
	{ "vsnprintf", (uintptr_t)&vsnprintf },
	{ "vsprintf", (uintptr_t)&vsprintf },
	{ "vsscanf", (uintptr_t)&vsscanf },
	{ "vswprintf", (uintptr_t)&vswprintf },
	{ "wcrtomb", (uintptr_t)&wcrtomb },
	{ "wcscoll", (uintptr_t)&wcscoll },
	{ "wcscmp", (uintptr_t)&wcscmp },
	{ "wcsncpy", (uintptr_t)&wcsncpy },
	{ "wcsnrtombs", (uintptr_t)&wcsnrtombs },
	{ "wcsftime", (uintptr_t)&wcsftime },
	{ "wcslen", (uintptr_t)&wcslen },
	{ "wcsxfrm", (uintptr_t)&wcsxfrm },
	{ "wcstod", (uintptr_t)&wcstod },
	{ "wcstof", (uintptr_t)&wcstof },
	{ "wcstol", (uintptr_t)&wcstol },
	{ "wcstold", (uintptr_t)&wcstold },
	{ "wcstoll", (uintptr_t)&wcstoll },
	{ "wcstoul", (uintptr_t)&wcstoul },
	{ "wcstoull", (uintptr_t)&wcstoull },
	{ "wctob", (uintptr_t)&wctob },
	{ "wctype", (uintptr_t)&wctype },
	{ "wmemchr", (uintptr_t)&wmemchr },
	{ "wmemcmp", (uintptr_t)&wmemcmp },
	{ "wmemcpy", (uintptr_t)&wmemcpy },
	{ "wmemmove", (uintptr_t)&wmemmove },
	{ "wmemset", (uintptr_t)&wmemset },
	{ "write", (uintptr_t)&write },
};

void *dlsym_hook( void *handle, const char *symbol) {
	for (size_t i = 0; i < gl_numret; ++i) {
		if (!strcmp(symbol, gl_ret0[i])) {
			return ret0;
		}
	}
	for (size_t i = 0; i < gl_numhook; ++i) {
		if (!strcmp(symbol, gl_hook[i].symbol)) {
			return (void *)gl_hook[i].func;
		}
	}
	
	void *func = vglGetProcAddress(symbol);
	
	if (!func) {
		for (size_t i = 0; i < sizeof(default_dynlib) / sizeof(so_default_dynlib); i++) {
			if (!strcmp(symbol, default_dynlib[i].symbol)) {
				return default_dynlib[i].func;
			}
		}
	}
	
	return func;
}

int check_kubridge(void) {
	int search_unk[2];
	return _vshKernelSearchModuleByName("kubridge", search_unk);
}

enum MethodIDs {
	UNKNOWN = 0,
	CALL_EXTENSION_FUNCTION,
	DOUBLE_VALUE,
	GAMEPAD_CONNECTED,
	GAMEPAD_DESCRIPTION,
	GET_UDID,
	GET_DEFAULT_FRAMEBUFFER,
	HTTP_POST,
	HTTP_GET,
	INIT,
	INPUT_STRING_ASYNC,
	OS_GET_INFO,
	PAUSE_MP3,
	PLAY_MP3,
	RESUME_MP3,
	SHOW_MESSAGE,
	SHOW_MESSAGE_ASYNC,
	STOP_MP3,
	PLAYING_MP3,
	GET_MIN_BUFFER_SIZE,
	PLAY,
	STOP,
	RELEASE,
	WRITE
} MethodIDs;

typedef struct {
	char *name;
	int id;
} NameToMethodID;

static NameToMethodID name_to_method_ids[] = {
	{ "CallExtensionFunction", CALL_EXTENSION_FUNCTION },
	{ "doubleValue", DOUBLE_VALUE },
	{ "GamepadConnected", GAMEPAD_CONNECTED },
	{ "GamepadDescription", GAMEPAD_DESCRIPTION },
	{ "GetUDID", GET_UDID },
	{ "GetDefaultFrameBuffer", GET_DEFAULT_FRAMEBUFFER },
	{ "HttpGet", HTTP_GET },
	{ "HttpPost", HTTP_POST },
	{ "<init>", INIT },
	{ "InputStringAsync", INPUT_STRING_ASYNC },
	{ "OsGetInfo", OS_GET_INFO },
	{ "PauseMP3", PAUSE_MP3 },
	{ "PlayMP3", PLAY_MP3 },
	{ "PlayingMP3", PLAYING_MP3 },
	{ "ResumeMP3", RESUME_MP3 },
	{ "ShowMessage", SHOW_MESSAGE },
	{ "ShowMessageAsync", SHOW_MESSAGE_ASYNC },
	{ "StopMP3", STOP_MP3 },
	{ "getMinBufferSize", GET_MIN_BUFFER_SIZE },
	{ "play", PLAY },
	{ "stop", STOP },
	{ "release", RELEASE },
	{ "write", WRITE },
};

int GetMethodID(void *env, void *class, const char *name, const char *sig) {
	for (int i = 0; i < sizeof(name_to_method_ids) / sizeof(NameToMethodID); i++) {
		if (strcmp(name, name_to_method_ids[i].name) == 0) {
			return name_to_method_ids[i].id;
		}
	}

	debugPrintf("Attempted to get an unknown method ID with name %s\n", name);
	return 0;
}

int GetStaticMethodID(void *env, void *class, const char *name, const char *sig) {
	for (int i = 0; i < sizeof(name_to_method_ids) / sizeof(NameToMethodID); i++) {
		if (strcmp(name, name_to_method_ids[i].name) == 0)
			return name_to_method_ids[i].id;
	}

	debugPrintf("Attempted to get an unknown static method ID with name %s\n", name);
	return 0;
}

void CallStaticVoidMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	switch (methodID) {
	case SHOW_MESSAGE:
		debugPrintf(args[0]);
		break;
	case INPUT_STRING_ASYNC:
		init_ime_dialog(args[0], args[1]);
		ime_index = (int)args[2];
		ime_active = 1;
		break;
	case SHOW_MESSAGE_ASYNC:
		init_msg_dialog(args[0]);
		msg_index = (int)args[1];
		msg_active = 1;
		break;
	case HTTP_POST:
		if (has_net) {
			send_post_request(args[0], args[1]);
			post_index = (int)args[2];
			post_active = 1;
		}
		break;
	case HTTP_GET:
		if (has_net) {
			send_get_request(args[0]);
			get_index = (int)args[1];
			get_active = 1;
		}
		break;
	case PLAY_MP3:
		audio_player_play(args[0], args[1]);
		break;
	case STOP_MP3:
		audio_player_stop();
		break;
	case PAUSE_MP3:
		audio_player_pause();
		break;
	case RESUME_MP3:
		audio_player_resume();
		break;
	default:
		if (methodID != UNKNOWN)
			debugPrintf("CallStaticVoidMethodV(%d)\n", methodID);
		break;
	}
}

int CallStaticBooleanMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	switch (methodID) {
	case GAMEPAD_CONNECTED:
		return is_gamepad_connected(args[0]);
	case PLAYING_MP3:
		return audio_player_is_playing();
	default:
		if (methodID != UNKNOWN)
			debugPrintf("CallStaticBooleanMethodV(%d)\n", methodID);
		return 0;
	}
}

int CallStaticByteMethod(void *env, void *obj, int methodID, uintptr_t *args) {
	if (methodID != UNKNOWN)
		debugPrintf("CallStaticByteMethodV(%d)\n", methodID);
	return 0;
}


uint64_t CallLongMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	if (methodID != UNKNOWN)
		debugPrintf("CallLongMethodV(%d)\n", methodID);
	return 0;
}

enum ClassIDs {
	STRING,
	AUDIO_TRACK
};

int FindClass(void *env, const char *name) {
	debugPrintf("FindClass %s\n", name);
	if (!strcmp(name, "java/lang/String")) {
		return STRING;
	} else if (!strcmp(name, "android/media/AudioTrack")) {
		return AUDIO_TRACK;
	}
	return 0x41414141;
}

void *NewGlobalRef(void *env, char *str) {
	return (void *)0x42424242;
}

void *NewWeakGlobalRef(void *env, char *str) {
	return (void *)0x45454545;
}

void DeleteGlobalRef(void *env, char *str) {
}

void *NewObjectV(void *env, void *clazz, int methodID, uintptr_t args) {
	return (void *)0x43434343;
}

void *GetObjectClass(void *env, void *obj) {
	return (void *)0x44444444;
}

char *NewString(void *env, char *bytes) {
	return bytes;
}

char *NewStringUTF(void *env, char *bytes) {
	return bytes;
}

char *GetStringUTFChars(void *env, char *string, int *isCopy) {
	if (isCopy)
		*isCopy = 0;
	return string;
}

int GetJavaVM(void *env, void **vm) {
	*vm = fake_vm;
	return 0;
}

int GetFieldID(void *env, void *clazz, const char *name, const char *sig) {
	return 0;
}

enum {
	UNKNOWN2,
	MANUFACTURER	
};

int GetStaticFieldID(void *env, void *clazz, const char *name, const char *sig) {
	if (!strcmp("MANUFACTURER",name))
		return MANUFACTURER;
	return 0;
}

void *GetStaticObjectField(void *env, void *clazz, int fieldID) {
	static char *r = NULL;
	switch (fieldID) {
	case MANUFACTURER:
		if (!r)
			r = malloc(0x100);
		strcpy(r, "Rinnegatamante");
		return r;
	default:
		return NULL;
	}
}

int GetBooleanField(void *env, void *obj, int fieldID) {
	return 0;
}

void *CallObjectMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	if (methodID != UNKNOWN)
		debugPrintf("CallObjectMethodV(%d)\n", methodID);
	return NULL;
}

typedef struct {
	char *module_name;
	char *method_name;
	int argc;
	double *double_array;
	void *object_array;
} ext_func;

void *CallStaticObjectMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	static char r[512];
	switch (methodID) {
	case GAMEPAD_DESCRIPTION:
		return "Generic Gamepad";
	case GET_UDID:
		return "1234";
	case CALL_EXTENSION_FUNCTION:
		{
			ext_func *f = (ext_func *)args;
			if (!strcmp(f->module_name, "PickMe")) { // Used by Super Mario Maker: World Engine
				if (!strcmp(f->method_name, "getDire1")) {
					sprintf(r, "%s%s/", data_path, (char *)f->object_array);
					recursive_mkdir(r);
					return f->object_array;
				}
			} else if (!strcmp(f->module_name, "NOTCH")) { // Used by Forager
				jni_double = 0.0;
				return &jni_double;
			} else if (!strcmp(f->module_name, "OUYAExt")) { // Used by Angry Ranook
				if (!strcmp(f->method_name, "ouyaIsOUYA")) {
					jni_double = 1.0;
					return &jni_double;
				}
			} else if (!strcmp(f->module_name, "myclass")) { // Used by IMSCARED
				if (!strcmp(f->method_name, "vibrate_start")) {
					return NULL;
				} else if (!strcmp(f->method_name, "getScreenBrightness")) {
					int val;
					if (sceRegMgrGetKeyInt("/CONFIG/DISPLAY/", "brightness", &val) < 0)
						jni_double = -1.0;
					else
						jni_double = ((double)val / 65536.0) * 128.0;
					return &jni_double;
				}
			}
			debugPrintf("Called undefined extension function from module %s with name %s\n", f->module_name, f->method_name);
			return NULL;
		}
	default:
		if (methodID != UNKNOWN)
			debugPrintf("CallStaticObjectMethodV(%d)\n", methodID);
		return NULL;
	}
}

int audio_samplerate;
int audio_channels;
SceUID audio_port;

int CallStaticIntMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	switch (methodID) {
	case GET_DEFAULT_FRAMEBUFFER:
		return 0;
	case GET_MIN_BUFFER_SIZE:
		audio_samplerate = args[0];
		audio_channels = args[1] == 0x03 ? 2 : 1;
		audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_VOICE, 4096 / (2 * audio_channels), audio_samplerate, (SceAudioOutMode)(audio_channels - 1));
		debugPrintf("Asking for %s audio at %dhz samplerate. (Opened port %d)\n", audio_channels == 2 ? "stereo" : "mono", audio_samplerate, audio_port);
		return 4096;
	case OS_GET_INFO:
		return Java_com_yoyogames_runner_RunnerJNILib_CreateVersionDSMap(fake_env, 0, 7, "v1.0", "PSVita", "PSVita", "Sony Computer Entertainment", "armeabi", "armeabi-v7a", "YoYo Loader", "ARM Cortex A9", "v1.0", "Global", "v1.0", 0);
	default:
		if (methodID != UNKNOWN)
			debugPrintf("CallStaticIntMethodV(%d)\n", methodID);
		return 0;
	}
}

double CallStaticDoubleMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	switch (methodID) {
	default:
		if (methodID != UNKNOWN)
			debugPrintf("CallStaticDoubleMethodV(%d)\n", methodID);
		return 0;
	}
}

int CallBooleanMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	switch (methodID) {
	default:
		if (methodID != UNKNOWN)
			debugPrintf("CallBooleanMethodV(%d)\n", methodID);
		return 0;
	}
}

double CallDoubleMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	double r;
	switch (methodID) {
	case DOUBLE_VALUE:
		r = jni_double;
		jni_double = 0.0f;
		return r;
	default:
		if (methodID != UNKNOWN)
			debugPrintf("CallDoubleMethodV(%d)\n", methodID);
		break;
	}
	return 0.0;
}

int CallNonVirtualIntMethod(void *env, void *obj, int classID, int methodID, uintptr_t *args) {
	switch (methodID) {
	case WRITE:
		{
			sceAudioOutOutput(audio_port, (void *)args[0]);
			return (int)args[2];
		}
	default:
		if (methodID != UNKNOWN)
			debugPrintf("CallNonVirtualIntMethod(0x%x, %d)\n", classID, methodID);
		break;
	}
	return 0;
}

void CallNonVirtualVoidMethod(void *env, void *obj, int classID, int methodID, uintptr_t *args) {
	switch (methodID) {
	default:
		if (methodID != UNKNOWN)
			debugPrintf("CallNonVirtualVoidMethod(0x%x, %d)\n", classID, methodID);
		break;
	}
}

void CallVoidMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	switch (methodID) {
	default:
		if (methodID != UNKNOWN)
			debugPrintf("CallVoidMethodV(%d)\n", methodID);
		break;
	}
}

void *NewIntArray(void *env, int size) {
	return vglMalloc(sizeof(int) * size);	
}

void *NewCharArray(void *env, int size) {
	return vglMalloc(sizeof(char) * size);
}

void *NewObjectArray(void *env, int size, int clazz, void *elements) {
	if (disableObjectsArray)
		return NULL;
	void *r = vglMalloc(size);
	if (elements) {
		sceClibMemcpy(r, elements, size);
	}
	return r;
}

void *NewDoubleArray(void *env, int size) {
	return vglMalloc(sizeof(double) * size);	
}

int GetArrayLength(void *env, void *array) {
	return (int)downloaded_bytes;
}

void SetIntArrayRegion(void *env, int *array, int start, int len, int *buf) {
	sceClibMemcpy(&array[start], buf, sizeof(int) * len);
}

void SetDoubleArrayRegion(void *env, double *array, int start, int len, double *buf) {
	sceClibMemcpy(&array[start], buf, sizeof(double) * len);
}

void SetObjectArrayElement(void *env, uint8_t *array, int index, void *val) {
	if (array)
		strcpy(&array[index], val);
}

void GetByteArrayRegion(void *env, uint8_t *array, int start, int len, void *buf) {
	sceClibMemcpy(buf, &array[start], len);
}

int GetIntField(void *env, void *obj, int fieldID) { return 0; }

int PushLocalFrame(void *env, int capacity) {
    return 0;
}

void *PopLocalFrame(void *env, void *obj) {
    return NULL;
}

void *GetPrimitiveArrayCritical(void *env, void *arr, int *isCopy) {
	if (isCopy)
		*isCopy = 0;

    return arr;
}

void ReleasePrimitiveArrayCritical(void *env, void *arr, void *carray, int mode) {
}

static void game_end()
{
#ifdef STANDALONE_MODE
	sceKernelExitProcess(0);
#else
	sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);
#endif
}

static int last_track_id = -1;
static double last_track_pos = 0.f;
static uint32_t last_track_pos_frame = 0;
static void audio_sound_get_track_position(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	if (*g_fNoAudio)
		return;
	
	ret->kind = VALUE_REAL;
	int sound_id = YYGetInt32(args, 0);
	
	if ((last_track_id != sound_id) || (*g_IOFrameCount - last_track_pos_frame > 1)) {
		ret->rvalue.val = Audio_GetTrackPos(sound_id);
	} else {
		if (last_track_pos_frame == *g_IOFrameCount)
			ret->rvalue.val = last_track_pos;
		else {
			ret->rvalue.val = Audio_GetTrackPos(sound_id);
			if (ret->rvalue.val < last_track_pos && fabs(ret->rvalue.val - last_track_pos) < 0.1f)
				ret->rvalue.val = last_track_pos + (double)*g_GML_DeltaTime / 1000000.0f;
		}
	}
	
	last_track_pos_frame = *g_IOFrameCount;
	last_track_id = sound_id;
	last_track_pos = ret->rvalue.val;
}

void *pthread_main(void *arg);

int main(int argc, char *argv[]) {
	pthread_t t;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 2 * 1024 * 1024);
	pthread_create(&t, &attr, pthread_main, NULL);

	return sceKernelExitDeleteThread(0);
}

void *pthread_main(void *arg) {
#if 0
	// Debug
	sceSysmoduleLoadModule(SCE_SYSMODULE_RAZOR_CAPTURE);
#endif
#ifdef HAS_VIDEO_PLAYBACK_SUPPORT
	sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
#endif
	
	// Checking requested game launch
	char game_name[0x200];
#ifndef STANDALONE_MODE
	FILE *f = fopen(LAUNCH_FILE_PATH, "r");
	if (f) {
		size_t size = fread(game_name, 1, 0x200, f);
		fclose(f);
		sceIoRemove(LAUNCH_FILE_PATH);
		game_name[size] = 0;
	} else {
		strcpy(game_name, "test"); // Debug
	}
#else
	sceAppMgrAppParamGetString(0, 12, game_name, 256);
#endif

	// Enabling analogs and touch sampling
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
	sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
	sceMotionReset();
	sceMotionStartSampling();

	// Maximizing clocks
	scePowerSetArmClockFrequency(444);
	scePowerSetBusClockFrequency(222);
	scePowerSetGpuClockFrequency(222);
	scePowerSetGpuXbarClockFrequency(166);
	
	// Populating required strings and creating required folders
	char pkg_name[256];
#ifdef STANDALONE_MODE
	sprintf(apk_path, "app0:game.apk");
#else
	sprintf(apk_path, "%s/%s/game.apk", DATA_PATH, game_name);
#endif
	sprintf(data_path_root, "%s/%s/", DATA_PATH, game_name);
	sprintf(data_path, "%s/%s/assets/", DATA_PATH, game_name);
	recursive_mkdir(data_path);
	
	sprintf(gxp_path, "ux0:data/gms/shared/gxp/%s", game_name);
	sceIoMkdir("ux0:data/gms/shared", 0777);
	sceIoMkdir("ux0:data/gms/shared/gxp", 0777);
	sceIoMkdir("ux0:data/gms/shared/glsl", 0777);
	sceIoMkdir(gxp_path, 0777);
	strcpy(pkg_name, "com.rinnegatamante.loader");

	// Checking for dependencies
	if (check_kubridge() < 0)
		fatal_error("Error: kubridge.skprx is not installed.");
	if (!file_exists("ur0:/data/libshacccg.suprx") && !file_exists("ur0:/data/external/libshacccg.suprx"))
		fatal_error("Error: libshacccg.suprx is not installed.");
	
	// Loading shared C++ executable, if present, from the apk
	GLboolean has_cpp_so = GL_FALSE;
	GLboolean cpp_warn = GL_FALSE;
	unz_file_info file_info;
	unzFile apk_file = unzOpen(apk_path);
	if (!apk_file)
		fatal_error("Error could not find %s.", apk_path);
	int res = unzLocateFile(apk_file, "lib/armeabi-v7a/libc++_shared.so", NULL);
	if (res == UNZ_OK) {
		unzGetCurrentFileInfo(apk_file, &file_info, NULL, 0, NULL, 0, NULL, 0);
		unzOpenCurrentFile(apk_file);
		uint64_t so_size = file_info.uncompressed_size;
		uint8_t *so_buffer = (uint8_t *)malloc(so_size);
		unzReadCurrentFile(apk_file, so_buffer, so_size);
		unzCloseCurrentFile(apk_file);
		res = so_mem_load(&cpp_mod, so_buffer, so_size, LOAD_ADDRESS);
		if (res >= 0) {
			has_cpp_so = GL_TRUE;
		} else {
			cpp_warn = GL_TRUE;
		}
		free(so_buffer);
	}
	
	// Loading ARMv7 executable from the apk
	unzLocateFile(apk_file, "lib/armeabi-v7a/libyoyo.so", NULL);
	unzGetCurrentFileInfo(apk_file, &file_info, NULL, 0, NULL, 0, NULL, 0);
	unzOpenCurrentFile(apk_file);
	uint64_t so_size = file_info.uncompressed_size;
	uint8_t *so_buffer = (uint8_t *)malloc(so_size);
	unzReadCurrentFile(apk_file, so_buffer, so_size);
	unzCloseCurrentFile(apk_file);
	res = so_mem_load(&yoyoloader_mod, so_buffer, so_size, LOAD_ADDRESS + 0x1000000);
	if (res < 0)
		fatal_error("Error could not load lib/armeabi-v7a/libyoyo.so from inside game.apk. (Errorcode: 0x%08X)", res);
	free(so_buffer);
	
	// Loading config file
	char *platforms[] = {
		"Mob",
		"Win",
		"PS4"
	};
	loadConfig(game_name);
	debugPrintf("+--------------------------------------------+\n");
	debugPrintf("|YoYo Loader Setup                           |\n");
	debugPrintf("+--------------------------------------------+\n");
	debugPrintf("|Force GLES1 Mode: %s                         |\n", forceGL1 ? "Y" : "N");
	debugPrintf("|Skip Splashscreen at Boot: %s                |\n", forceSplashSkip ? "Y" : "N");
	debugPrintf("|Platform Target: %s                        |\n", platforms[platTarget]);
	debugPrintf("|Use Uncached Mem: %s                         |\n", uncached_mem ? "Y" : "N");
	debugPrintf("|Run with Extended Mem Mode: %s               |\n", maximizeMem ? "Y" : "N");
	debugPrintf("|Run with Extended Runner Pool: %s            |\n", _newlib_heap_size > 256 * 1024 * 1024 ? "Y" : "N");
	debugPrintf("|Run with Mem Squeezing: %s                   |\n", squeeze_mem ? "Y" : "N");
	debugPrintf("|Use Double Buffering: %s                     |\n", double_buffering ? "Y" : "N");
#ifdef HAS_VIDEO_PLAYBACK_SUPPORT
	debugPrintf("|Enable Video Player: Y                      |\n");
#else
	debugPrintf("|Enable Video Player: N                      |\n");
#endif
	debugPrintf("|Enable Network Features: %s                  |\n", has_net ? "Y" : "N");
	debugPrintf("|Force Bilinear Filtering: %s                 |\n", forceBilinear ? "Y" : "N");
	debugPrintf("|Has custom C++ shared lib: %s                |\n", has_cpp_so ? "Y" : "N");
	debugPrintf("+--------------------------------------------+\n\n\n");
	
	if (cpp_warn) {
		debugPrintf("WARNING: Found libc++_shared.so but failed to load.\n");
	}
	
	if (has_net) {
		// Init Net
		debugPrintf("Initializing sceNet...\n");
		sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
		int ret = sceNetShowNetstat();
		SceNetInitParam initparam;
		if (ret == SCE_NET_ERROR_ENOTINIT) {
			initparam.memory = malloc(141 * 1024);
			initparam.size = 141 * 1024;
			initparam.flags = 0;
			sceNetInit(&initparam);
		}
	} else {
		/* 
		 * FIXME: Object arrays are handled badly and cause crashes in several games.
		 * The only games actually requiring them are the ones using HTTP methods, so we enable them
		 * only if network functionalities are requested.
		 */
		disableObjectsArray = 1;
	}
	
	// Loading splash screen from the apk
	uint8_t *splash_buf = NULL;
	uint32_t splash_size;
	if (!forceSplashSkip && unzLocateFile(apk_file, "assets/splash.png", NULL) == UNZ_OK) {
		unzGetCurrentFileInfo(apk_file, &file_info, NULL, 0, NULL, 0, NULL, 0);
		unzOpenCurrentFile(apk_file);
		splash_size = file_info.uncompressed_size;
		splash_buf = (uint8_t *)malloc(splash_size);
		unzReadCurrentFile(apk_file, splash_buf, splash_size);
		unzCloseCurrentFile(apk_file);
	}
	
	// Loading cpp library if present
	if (has_cpp_so) {
		so_relocate(&cpp_mod);
		so_resolve(&cpp_mod, default_dynlib, sizeof(default_dynlib), 0);
		so_flush_caches(&cpp_mod);
		so_initialize(&cpp_mod);
	}

	// Patching the executable
	so_relocate(&yoyoloader_mod);
	so_resolve(&yoyoloader_mod, default_dynlib, sizeof(default_dynlib), 0);
	if (!has_net)
		so_resolve_with_dummy(&yoyoloader_mod, net_dynlib, sizeof(net_dynlib), 0);
	patch_openal();
	patch_runner();
#ifdef HAS_VIDEO_PLAYBACK_SUPPORT
	patch_video_player();
#endif
	so_flush_caches(&yoyoloader_mod);
	so_initialize(&yoyoloader_mod);
	
	// Initializing vitaGL
	vglSetSemanticBindingMode(VGL_MODE_POSTPONED);
	if (debugMode)
		vglSetDisplayCallback(mem_profiler);
	vglSetupGarbageCollector(127, 0x20000);
	if (squeeze_mem)
		vglSetParamBufferSize(2 * 1024 * 1024);
	if (!uncached_mem)
		vglUseCachedMem(GL_TRUE);
	if (double_buffering)
		vglUseTripleBuffering(GL_FALSE);
	if (maximizeMem)
		vglInitWithCustomThreshold(0, SCREEN_W, SCREEN_H, MEMORY_VITAGL_THRESHOLD_MB * 1024 * 1024, 0, 0, 0, SCE_GXM_MULTISAMPLE_NONE);
	else
		vglInitExtended(0, SCREEN_W, SCREEN_H, MEMORY_VITAGL_THRESHOLD_MB * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
	vgl_booted = 1;
	
	// Applying extra patches to the runner
	patch_runner_post_init();
	Function_Add = (void *)so_symbol(&yoyoloader_mod, "_Z12Function_AddPKcPFvR6RValueP9CInstanceS4_iPS1_Eib");
	if (Function_Add == NULL)
		Function_Add = (void *)so_symbol(&yoyoloader_mod, "_Z12Function_AddPcPFvR6RValueP9CInstanceS3_iPS0_Eib");
	
	Function_Add("game_end", (intptr_t)game_end, 1, 1);
	Function_Add("audio_sound_get_track_position", (intptr_t)audio_sound_get_track_position, 1, 1);
	
	//uint8_t *g_fSuppressErrors = (uint8_t *)so_symbol(&yoyoloader_mod, "g_fSuppressErrors");
	//*g_fSuppressErrors = 1;
	
	patch_gamepad(game_name);
#ifdef STANDALONE_MODE
	int has_trophies = trophies_init();
	if (has_trophies > 0) {
		patch_trophies();
	}
#endif
	so_flush_caches(&yoyoloader_mod);

	// Initializing Java VM and JNI Interface
	memset(fake_vm, 'A', sizeof(fake_vm));
	*(uintptr_t *)(fake_vm + 0x00) = (uintptr_t)fake_vm; // just point to itself...
	*(uintptr_t *)(fake_vm + 0x10) = (uintptr_t)GetEnv;
	*(uintptr_t *)(fake_vm + 0x14) = (uintptr_t)ret0;
	*(uintptr_t *)(fake_vm + 0x18) = (uintptr_t)GetEnv;
	memset(fake_env, 'A', sizeof(fake_env));
	*(uintptr_t *)(fake_env + 0x00) = (uintptr_t)fake_env; // just point to itself...
	*(uintptr_t *)(fake_env + 0x18) = (uintptr_t)FindClass;
	*(uintptr_t *)(fake_env + 0x4C) = (uintptr_t)PushLocalFrame;
	*(uintptr_t *)(fake_env + 0x50) = (uintptr_t)PopLocalFrame;
	*(uintptr_t *)(fake_env + 0x54) = (uintptr_t)NewGlobalRef;
	*(uintptr_t *)(fake_env + 0x58) = (uintptr_t)DeleteGlobalRef;
	*(uintptr_t *)(fake_env + 0x5C) = (uintptr_t)ret0; // DeleteLocalRef
	*(uintptr_t *)(fake_env + 0x68) = (uintptr_t)ret0; // EnsureLocalCapacity
	*(uintptr_t *)(fake_env + 0x74) = (uintptr_t)NewObjectV;
	*(uintptr_t *)(fake_env + 0x7C) = (uintptr_t)GetObjectClass;
	*(uintptr_t *)(fake_env + 0x80) = (uintptr_t)ret1; // IsInstanceOf
	*(uintptr_t *)(fake_env + 0x84) = (uintptr_t)GetMethodID;
	*(uintptr_t *)(fake_env + 0x8C) = (uintptr_t)CallObjectMethodV;
	*(uintptr_t *)(fake_env + 0x98) = (uintptr_t)CallBooleanMethodV;
	*(uintptr_t *)(fake_env + 0xD4) = (uintptr_t)CallLongMethodV;
	*(uintptr_t *)(fake_env + 0xEC) = (uintptr_t)CallDoubleMethodV;
	*(uintptr_t *)(fake_env + 0xF8) = (uintptr_t)CallVoidMethodV;
	*(uintptr_t *)(fake_env + 0x140) = (uintptr_t)CallNonVirtualIntMethod;
	*(uintptr_t *)(fake_env + 0x170) = (uintptr_t)CallNonVirtualVoidMethod;
	*(uintptr_t *)(fake_env + 0x178) = (uintptr_t)GetFieldID;
	*(uintptr_t *)(fake_env + 0x17C) = (uintptr_t)GetBooleanField;
	*(uintptr_t *)(fake_env + 0x190) = (uintptr_t)GetIntField;
	*(uintptr_t *)(fake_env + 0x1C4) = (uintptr_t)GetStaticMethodID;
	*(uintptr_t *)(fake_env + 0x1CC) = (uintptr_t)CallStaticObjectMethodV;
	*(uintptr_t *)(fake_env + 0x1D8) = (uintptr_t)CallStaticBooleanMethodV;
	*(uintptr_t *)(fake_env + 0x1E0) = (uintptr_t)CallStaticByteMethod;
	*(uintptr_t *)(fake_env + 0x208) = (uintptr_t)CallStaticIntMethodV;
	*(uintptr_t *)(fake_env + 0x22C) = (uintptr_t)CallStaticDoubleMethodV;
	*(uintptr_t *)(fake_env + 0x238) = (uintptr_t)CallStaticVoidMethodV;
	*(uintptr_t *)(fake_env + 0x240) = (uintptr_t)GetStaticFieldID;
	*(uintptr_t *)(fake_env + 0x244) = (uintptr_t)GetStaticObjectField;
	*(uintptr_t *)(fake_env + 0x28C) = (uintptr_t)NewString;
	*(uintptr_t *)(fake_env + 0x29C) = (uintptr_t)NewStringUTF;
	*(uintptr_t *)(fake_env + 0x2A4) = (uintptr_t)GetStringUTFChars;
	*(uintptr_t *)(fake_env + 0x2A8) = (uintptr_t)ret0; // ReleaseStringUTFChars
	*(uintptr_t *)(fake_env + 0x2AC) = (uintptr_t)GetArrayLength;
	*(uintptr_t *)(fake_env + 0x2B0) = (uintptr_t)NewObjectArray;
	*(uintptr_t *)(fake_env + 0x2B8) = (uintptr_t)SetObjectArrayElement;
	*(uintptr_t *)(fake_env + 0x2C0) = (uintptr_t)NewCharArray;
	*(uintptr_t *)(fake_env + 0x2CC) = (uintptr_t)NewIntArray;
	*(uintptr_t *)(fake_env + 0x2D8) = (uintptr_t)NewDoubleArray;
	*(uintptr_t *)(fake_env + 0x320) = (uintptr_t)GetByteArrayRegion;
	*(uintptr_t *)(fake_env + 0x34C) = (uintptr_t)SetIntArrayRegion;
	*(uintptr_t *)(fake_env + 0x358) = (uintptr_t)SetDoubleArrayRegion;
	*(uintptr_t *)(fake_env + 0x36C) = (uintptr_t)GetJavaVM;
	*(uintptr_t *)(fake_env + 0x378) = (uintptr_t)GetPrimitiveArrayCritical;
	*(uintptr_t *)(fake_env + 0x37C) = (uintptr_t)ReleasePrimitiveArrayCritical;
	*(uintptr_t *)(fake_env + 0x394) = (uintptr_t)NewWeakGlobalRef;
	
	void (*Java_com_yoyogames_runner_RunnerJNILib_Startup) (void *env, int a2, char *apk_path, char *save_dir, char *pkg_dir, int sleep_margin) = (void *)so_symbol(&yoyoloader_mod, "Java_com_yoyogames_runner_RunnerJNILib_Startup");
	Java_com_yoyogames_runner_RunnerJNILib_CreateVersionDSMap = (void *)so_symbol(&yoyoloader_mod, "Java_com_yoyogames_runner_RunnerJNILib_CreateVersionDSMap");
	Java_com_yoyogames_runner_RunnerJNILib_TouchEvent  = (void *)so_symbol(&yoyoloader_mod, "Java_com_yoyogames_runner_RunnerJNILib_TouchEvent");
	
	// Displaying splash screen
	if (splash_buf) {
		int w, h;
		uint8_t *bg_data = stbi_load_from_memory(splash_buf, splash_size, &w, &h, NULL, 4);
		GLuint bg_image;
		glGenTextures(1, &bg_image);
		glBindTexture(GL_TEXTURE_2D, bg_image);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, bg_data);
		vglFree(bg_data);
		free(splash_buf);
		glEnable(GL_TEXTURE_2D);
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glViewport(0, 0, SCREEN_W, SCREEN_H);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0, SCREEN_W, SCREEN_H, 0, -1, 1);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		float splash_vertices[] = {
			       0,        0, 0,
			SCREEN_W,        0, 0,
			SCREEN_W, SCREEN_H, 0,
			       0, SCREEN_H, 0
		};
		float splash_texcoords[] = {0, 0, 1, 0, 1, 1, 0, 1};
		glVertexPointer(3, GL_FLOAT, 0, splash_vertices);
		glTexCoordPointer(2, GL_FLOAT, 0, splash_texcoords);
		glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		vglSwapBuffers(GL_FALSE);
		glDeleteTextures(1, &bg_image);
	}
	
	// Extracting Game ID
	char game_id[256];
	void *tmp_buf = malloc(32 * 1024 * 1024);
	unzLocateFile(apk_file, "assets/game.droid", NULL);
	unzOpenCurrentFile(apk_file);
	unzReadCurrentFile(apk_file, tmp_buf, 20);
	uint32_t offs;
	unzReadCurrentFile(apk_file, &offs, 4);
	uint32_t target = offs - 28;
	while (target > 32 * 1024 * 1024) {
		unzReadCurrentFile(apk_file, tmp_buf, 32 * 1024 * 1024);
		target -= 32 * 1024 * 1024;
	}
	unzReadCurrentFile(apk_file, tmp_buf, target);
	unzReadCurrentFile(apk_file, &offs, 4);
	unzReadCurrentFile(apk_file, game_id, offs + 1);
	unzClose(apk_file);
	free(tmp_buf);
	debugPrintf("Detected %s as Game ID\n", game_id);
	
	// Enabling game specific gamehacks
	if (!strcmp(game_id, "DELTARUNE")) {
		debugPrintf("Enabling Deltarune specific gamehack!\n");
		deltarune_hack = 1;
	}
	
	// Starting the Runner
	Java_com_yoyogames_runner_RunnerJNILib_Startup(fake_env, 0, apk_path, data_path, pkg_name, 0);
	
	// Entering main loop
	debugPrintf("Startup ended\n");
	main_loop();
	
	return 0;
}
