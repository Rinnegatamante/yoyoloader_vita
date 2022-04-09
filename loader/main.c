/* main.c -- GMS Loader based on .so loader
 *
 * Copyright (C) 2021 Andy Nguyen
 * Copyright (C) 2022 Rinnegatamante
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.	See the LICENSE file for details.
 */

#include <vitasdk.h>
#include <kubridge.h>
#include <vitashark.h>
#include <vitaGL.h>
#include <zlib.h>

#define AL_ALEXT_PROTOTYPES
#include <AL/alext.h>
#include <AL/efx.h>

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

#define STB_IMAGE_IMPLEMENTATION
#define STB_ONLY_PNG
#include "stb_image.h"

int forceGL1 = 0;
int forceSplashSkip = 0;
int forceMainThread = 0;
int forceWinMode = 0;
int forceBilinear = 0;
int maximizeMem = 0;
int debugShaders = 0;
int debugMode = 0;

char data_path[256];

void patch_gamepad();
void GamePadUpdate();

char *translate_frag_shader(const char *string, int size);
char *translate_vert_shader(const char *string, int size);

void loadConfig(const char *game) {
	char configFile[512];
	char buffer[30];
	int value;
	
	sprintf(configFile, "%s/%s/yyl.cfg", DATA_PATH, game);
	FILE *config = fopen(configFile, "r");

	if (config) {
		while (EOF != fscanf(config, "%[^=]=%d\n", buffer, &value)) {
			if (strcmp("forceGLES1", buffer) == 0) forceGL1 = value;
			else if (strcmp("forceBilinear", buffer) == 0) forceBilinear = value;
			else if (strcmp("winMode", buffer) == 0) forceWinMode = value;
			else if (strcmp("singleThreaded", buffer) == 0) forceMainThread = value;
			else if (strcmp("debugShaders", buffer) == 0) debugShaders = value;
			else if (strcmp("debugMode", buffer) == 0) debugMode = value;
			else if (strcmp("noSplash", buffer) == 0) forceSplashSkip = value;
			else if (strcmp("maximizeMem", buffer) == 0) maximizeMem = value;
		}
		fclose(config);
	}
}

extern void *GetPlatformInstance;

static int __stack_chk_guard_fake = 0x42424242;
ALCdevice *ALDevice;
ALvoid *ALContext;

static char fake_vm[0x1000];
static char fake_env[0x1000];

int _newlib_heap_size_user = MEMORY_NEWLIB_MB * 1024 * 1024;

unsigned int _pthread_stack_default_user = 1 * 1024 * 1024;

so_module yoyoloader_mod;

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

// From https://github.com/kraj/uClibc/blob/master/libc/misc/dirent/scandir.c
int scandir(const char *dir, struct dirent ***namelist,
	int (*selector) (const struct dirent *),
	int (*compar) (const struct dirent **, const struct dirent **))
{
	DIR *dp = opendir (dir);
	struct dirent *current;
	struct dirent **names = NULL;
	size_t names_size = 0, pos;
	//int save;

	if (dp == NULL)
	return -1;

	//save = errno;
	//__set_errno (0);

	pos = 0;
	while ((current = readdir (dp)) != NULL) {
	int use_it = selector == NULL;

	if (! use_it)
	{
		use_it = (*selector) (current);
		/* The selector function might have changed errno.
		 * It was zero before and it need to be again to make
		 * the latter tests work.  */
		//if (! use_it)
		//__set_errno (0);
	}
	if (use_it)
	{
		struct dirent *vnew;
		size_t dsize;

		/* Ignore errors from selector or readdir */
		//__set_errno (0);

		if (pos == names_size)
		{
		struct dirent **new;
		if (names_size == 0)
			names_size = 10;
		else
			names_size *= 2;
		new = (struct dirent **) realloc (names,
					names_size * sizeof (struct dirent *));
		if (new == NULL)
			break;
		names = new;
		}

		dsize = &current->d_name[256+1] - (char*)current;
		vnew = (struct dirent *) malloc (dsize);
		if (vnew == NULL)
		break;

		names[pos++] = (struct dirent *) memcpy (vnew, current, dsize);
	}
	}

	if (errno != 0)
	{
	//save = errno;
	closedir (dp);
	while (pos > 0)
		free (names[--pos]);
	free (names);
	//__set_errno (save);
	return -1;
	}

	closedir (dp);
	//__set_errno (save);

	/* Sort the list if we have a comparison function to sort with.  */
	if (compar != NULL)
	qsort (names, pos, sizeof (struct dirent *), (__compar_fn_t) compar);
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

int clock_gettime(int clk_ik, struct timespec *t) {
	struct timeval now;
	int rv = gettimeofday(&now, NULL);
	if (rv)
		return rv;
	t->tv_sec = now.tv_sec;
	t->tv_nsec = now.tv_usec * 1000;
	return 0;
}

int pthread_mutex_init_fake(pthread_mutex_t **uid, const pthread_mutexattr_t *mutexattr) {
	pthread_mutex_t *m = calloc(1, sizeof(pthread_mutex_t));
	if (!m)
		return -1;

	const int recursive = (mutexattr && *(const int *)mutexattr == 1);
	*m = recursive ? PTHREAD_RECURSIVE_MUTEX_INITIALIZER : PTHREAD_MUTEX_INITIALIZER;

	int ret = pthread_mutex_init(m, mutexattr);
	if (ret < 0) {
		free(m);
		return -1;
	}

	*uid = m;

	return 0;
}

int pthread_mutex_destroy_fake(pthread_mutex_t **uid) {
	if (uid && *uid && (uintptr_t)*uid > 0x8000) {
		pthread_mutex_destroy(*uid);
		free(*uid);
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
	pthread_cond_t *c = calloc(1, sizeof(pthread_cond_t));
	if (!c)
		return -1;

	*c = PTHREAD_COND_INITIALIZER;

	int ret = pthread_cond_init(c, NULL);
	if (ret < 0) {
		free(c);
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
		free(*cnd);
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
	return pthread_create(thread, NULL, entry, arg);
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

enum {
	TOUCH_DOWN,
	TOUCH_UP,
	TOUCH_MOVE
};



void main_loop() {
	int (*Java_com_yoyogames_runner_RunnerJNILib_Process) (void *env, int a2, int w, int h, float accel_x, float accel_y, float accel_z, int keypad_open, int orientation, float refresh_rate) = (void *)so_symbol(&yoyoloader_mod, "Java_com_yoyogames_runner_RunnerJNILib_Process");
	int (*Java_com_yoyogames_runner_RunnerJNILib_TouchEvent) (void *env, int a2, int type, int id, float x, float y) = (void *)so_symbol(&yoyoloader_mod, "Java_com_yoyogames_runner_RunnerJNILib_TouchEvent");
	int lastX[SCE_TOUCH_MAX_REPORT] = {-1, -1, -1, -1, -1, -1, -1, -1};
	int lastY[SCE_TOUCH_MAX_REPORT] = {-1, -1, -1, -1, -1, -1, -1, -1};
	int (*Java_com_yoyogames_runner_RunnerJNILib_canFlip) (void) = (void *)so_symbol(&yoyoloader_mod, "Java_com_yoyogames_runner_RunnerJNILib_canFlip");
	
	int *g_IOFrameCount = (int *)so_symbol(&yoyoloader_mod, "g_IOFrameCount");
	
	for (;;) {
		SceTouchData touch;
		sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
		for (int i = 0; i < SCE_TOUCH_MAX_REPORT; i++) {
			if (i < touch.reportNum) {
				int x = (int)((float)touch.report[i].x * (float)SCREEN_W / 1920.0f);
				int y = (int)((float)touch.report[i].y * (float)SCREEN_H / 1088.0f);

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
		
		if (*g_IOFrameCount >= 1) {
			GamePadUpdate();
		}
		
		SceMotionSensorState sensor;
		sceMotionGetSensorState(&sensor, 1);
		Java_com_yoyogames_runner_RunnerJNILib_Process(fake_env, 0, SCREEN_W, SCREEN_H, sensor.accelerometer.x, sensor.accelerometer.y, sensor.accelerometer.z, 0, 0, 60.0f);
		if (Java_com_yoyogames_runner_RunnerJNILib_canFlip())
			vglSwapBuffers(GL_FALSE);
	}
}

void __stack_chk_fail_fake() {
	// Some versions of libyoyo.so apparently stack smash on Startup, with this workaround we prevent the app from crashing
	debugPrintf("Entering main loop after stack smash\n");
	main_loop();
}

double GetPlatform() {
	return forceWinMode ? 0.0f : 4.0f;
}

/*int (*zip_name_locate)(void *handle, const char *fname, uint32_t flags);
int (*zip_fopen_index)(void *handle, int index, uint32_t flags);

int zip_fopen(int handle, const char *fname, uint32_t flags) {
	char real_fname[256];
	char *s = strstr(fname, "ux0:");
	if (s) {
		s[0] = 0;
		sprintf(real_fname, "%s%s", fname, s + strlen(data_path));
		fname = real_fname;
		s[0] = 'u';
	}
	
	int idx;
	if ((idx = zip_name_locate(handle, fname, flags)) < 0)
		return NULL;

	return zip_fopen_index(handle, idx, flags);
}*/

void patch_runner(void) {
	hook_addr(so_symbol(&yoyoloader_mod, "_Z30PackageManagerHasSystemFeaturePKc"), (uintptr_t)&ret0);
	hook_addr(so_symbol(&yoyoloader_mod, "_Z17alBufferDebugNamejPKc"), (uintptr_t)&ret0);
	hook_addr(so_symbol(&yoyoloader_mod, "_ZN13MemoryManager10DumpMemoryEP7__sFILE"), (uintptr_t)&ret0);
	hook_addr(so_symbol(&yoyoloader_mod, "_ZN13MemoryManager10DumpMemoryEPvS0_"), (uintptr_t)&ret0);
	hook_addr(so_symbol(&yoyoloader_mod, "_Z23YoYo_GetPlatform_DoWorkv"), (uintptr_t)&GetPlatform);
	hook_addr(so_symbol(&yoyoloader_mod, "_Z20GET_YoYo_GetPlatformP9CInstanceiP6RValue"), (uintptr_t)&GetPlatformInstance);
	
	//zip_name_locate = so_symbol(&yoyoloader_mod, "zip_name_locate");
	//zip_fopen_index = so_symbol(&yoyoloader_mod, "zip_fopen_index");
	//hook_addr(so_symbol(&yoyoloader_mod, "zip_fopen"), (uintptr_t)&zip_fopen);
	
	// Debug
	if (debugMode)
		hook_addr(so_symbol(&yoyoloader_mod, "_ZN11TRelConsole6OutputEPKcz"), (uintptr_t)&DebugPrintf);
}

void patch_runner_post_init(void) {
	// Debug
	if (debugMode) {
		int *dbg_csol = (int *)so_symbol(&yoyoloader_mod, "_dbg_csol");
		if (dbg_csol) {
			kuKernelCpuUnrestrictedMemcpy((void *)(*(int *)so_symbol(&yoyoloader_mod, "_dbg_csol") + 0x0C), (void *)(so_symbol(&yoyoloader_mod, "_ZTV11TRelConsole") + 0x14), 4);
			kuKernelCpuUnrestrictedMemcpy((void *)(*(int *)so_symbol(&yoyoloader_mod, "_rel_csol") + 0x0C), (void *)(so_symbol(&yoyoloader_mod, "_ZTV11TRelConsole") + 0x14), 4);
		}
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
extern void *__cxa_pure_virtual;
extern void *__cxa_allocate_exception;
extern void *__cxa_throw;
extern void *__gnu_unwind_frame;
extern void *__stack_chk_fail;

char __progname[32] = {0};

int open(const char *pathname, int flags);

extern const char *BIONIC_ctype_;
extern const short *BIONIC_tolower_tab_;
extern const short *BIONIC_toupper_tab_;

static FILE __sF_fake[0x100][3];

int stat_hook(const char *pathname, void *statbuf) {
	struct stat st;
	int res = stat(pathname, &st);
	if (res == 0)
		*(uint64_t *)(statbuf + 0x30) = st.st_size;
	return res;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd,
					 off_t offset) {
	return malloc(length);
}

int munmap(void *addr, size_t length) {
	free(addr);
	return 0;
}

void *AAssetManager_open(void *mgr, const char *filename, int mode) {
	return NULL;
}

void *AAsset_close() {
	return NULL;
}

void *AAssetManager_fromJava() {
	return NULL;
}

void *AAsset_read() {
	return NULL;
}

void *AAsset_seek() {
	return NULL;
}

void *AAsset_getLength() {
	return NULL;
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
	if (forceGL1 == 1 && strstr(filename, "v2"))
		return NULL;
	return (void *)0xDEADBEEF;
}

void glShaderSourceHook(GLuint shader, GLsizei count, const GLchar **string, const GLint *length) {
	uint32_t sha1[5];
	SHA1_CTX ctx;
	
	int size = length ? *length : strlen(*string);
	sha1_init(&ctx);
	sha1_update(&ctx, (uint8_t *)*string, size);
	sha1_final(&ctx, (uint8_t *)sha1);

	char sha_name[64];
	snprintf(sha_name, sizeof(sha_name), "%08x%08x%08x%08x%08x", sha1[0], sha1[1], sha1[2], sha1[3], sha1[4]);

	char gxp_path[128], glsl_path[128];;
	snprintf(gxp_path, sizeof(gxp_path), "%s/%s.gxp", GXP_PATH, sha_name);

	FILE *file = fopen(gxp_path, "rb");
	if (!file) {
		debugPrintf("Could not find %s\n", gxp_path);
	
		char *cg_shader;
		int type;
		glGetShaderiv(shader, GL_SHADER_TYPE, &type);
		if (type == GL_FRAGMENT_SHADER) {
			cg_shader = translate_frag_shader(*string, size);
		} else {
			cg_shader = translate_vert_shader(*string, size);
		}
	
		glShaderSource(shader, 1, &cg_shader, NULL);
		glCompileShader(shader);
		int compiled;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
		
		// Debug
		if (debugShaders) {
			snprintf(glsl_path, sizeof(glsl_path), "%s/%s.cg", GLSL_PATH, sha_name);
			debugPrintf("Saving translated output on %s\n", glsl_path);
			file = fopen(glsl_path, "w");
			if (file) {
				fwrite(cg_shader, 1, strlen(cg_shader), file);
				fclose(file);
			}
		}
		free(cg_shader);

		if (!compiled) {
			debugPrintf("Translated shader has errors... Falling back to default shader!\n");
			snprintf(glsl_path, sizeof(glsl_path), "%s/%s.glsl", GLSL_PATH, sha_name);
			file = fopen(glsl_path, "w");
			if (file) {
				fwrite(*string, 1, size, file);
				fclose(file);
			}
			snprintf(gxp_path, sizeof(gxp_path), "%s/%s.gxp", GXP_PATH, type == GL_FRAGMENT_SHADER ? "bb4a9846ba51f476c322f32ddabf6461bc63cc5e" : "eb3eaf87949a211f2cec6acdae6f5d94ba13301e");
			file = fopen(gxp_path, "rb");
		} else {
			debugPrintf("Translated shader successfully compiled!\n");
			void *bin = malloc(0x8000);
			int bin_len;
			vglGetShaderBinary(shader, 0x8000, &bin_len, bin);
			file = fopen(gxp_path, "wb");
			fwrite(bin, 1, bin_len, file);
			fclose(file);
			free(bin);
			return;		
		}
	}

	if (file) {
		size_t shaderSize;
		void *shaderBuf;

		fseek(file, 0, SEEK_END);
		shaderSize = ftell(file);
		fseek(file, 0, SEEK_SET);

		shaderBuf = malloc(shaderSize);
		fread(shaderBuf, 1, shaderSize, file);
		fclose(file);

		glShaderBinary(1, &shader, 0, shaderBuf, shaderSize);

		free(shaderBuf);
	}
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

const char *gl_ret0[] = {
	"glPixelStorei",
	"glDiscardFramebufferEXT",
	"glGetFramebufferAttachmentParameteriv",
	"glMaterialx",
	"glNormalPointer",
	"glLightf",
	"glGetFramebufferAttachmentParameterivOES",
	"glCompileShader",
	"glGenRenderbuffer",
	"glDeleteRenderbuffers",
	"glFramebufferRenderbuffer",
	"glRenderbufferStorage",
	"glHint"
};
static size_t gl_numret = sizeof(gl_ret0) / sizeof(*gl_ret0);

static so_default_dynlib gl_hook[] = {
	{"glShaderSource", (uintptr_t)&glShaderSourceHook},
	{"glTexParameteri", (uintptr_t)&glTexParameteriHook},
	{"glTexParameterf", (uintptr_t)&glTexParameterfHook},
};
static size_t gl_numhook = sizeof(gl_hook) / sizeof(*gl_hook);

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
	return vglGetProcAddress(symbol);
}

FILE *fopen_hook(char *file, char *mode) {
	char *s = strstr(file, "/ux0:");
	if (s)
		return fopen(&s[1], mode);
	return fopen(file, mode);
}

void *sceClibMemclr(void *dst, SceSize len) {
	return sceClibMemset(dst, 0, len);
}

void *sceClibMemset2(void *dst, SceSize len, int ch) {
	return sceClibMemset(dst, ch, len);
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
	const uint32_t usec = req->tv_sec * 1000 * 1000 + req->tv_nsec / 1000;
	return sceKernelDelayThreadCB(usec);
}

ALCcontext *alcCreateContextHook(ALCdevice *dev, const ALCint *unused);
ALCdevice *alcOpenDeviceHook(void *unused);

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

static so_default_dynlib default_dynlib[] = {
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
	{ "__cxa_allocate_exception", (uintptr_t)&__cxa_allocate_exception },
	{ "__cxa_atexit", (uintptr_t)&__cxa_atexit },
	{ "__cxa_finalize", (uintptr_t)&__cxa_finalize },
	{ "__cxa_pure_virtual", (uintptr_t)&__cxa_pure_virtual },
	{ "__cxa_throw", (uintptr_t)&__cxa_throw },
	{ "__errno", (uintptr_t)&__errno },
	{ "__gnu_unwind_frame", (uintptr_t)&__gnu_unwind_frame },
	{ "__gnu_Unwind_Find_exidx", (uintptr_t)&ret0 },
	{ "__progname", (uintptr_t)&__progname },
	{ "__sF", (uintptr_t)&__sF_fake },
	{ "__stack_chk_fail", (uintptr_t)&__stack_chk_fail_fake },
	{ "__stack_chk_guard", (uintptr_t)&__stack_chk_guard_fake },
	{ "_ctype_", (uintptr_t)&BIONIC_ctype_},
	{ "abort", (uintptr_t)&abort },
	{ "acos", (uintptr_t)&acos },
	{ "acosf", (uintptr_t)&acosf },
	{ "alBufferData", (uintptr_t)&alBufferData },
	{ "alDeleteBuffers", (uintptr_t)&alDeleteBuffers },
	{ "alDeleteSources", (uintptr_t)&alDeleteSources },
	{ "alDistanceModel", (uintptr_t)&alDistanceModel },
	{ "alGenBuffers", (uintptr_t)&alGenBuffers },
	{ "alGenSources", (uintptr_t)&alGenSources },
	{ "alcGetCurrentContext", (uintptr_t)&alcGetCurrentContext },
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
	{ "alcCreateContext", (uintptr_t)&alcCreateContextHook },
	{ "alcGetContextsDevice", (uintptr_t)&alcGetContextsDevice },
	{ "alcGetError", (uintptr_t)&alcGetError },
	{ "alcGetIntegerv", (uintptr_t)&alcGetIntegerv },
	{ "alcGetString", (uintptr_t)&alcGetString },
	{ "alcMakeContextCurrent", (uintptr_t)&alcMakeContextCurrent },
	{ "alcDestroyContext", (uintptr_t)&alcDestroyContext },
	{ "alcOpenDevice", (uintptr_t)&alcOpenDeviceHook },
	{ "alcProcessContext", (uintptr_t)&alcProcessContext },
	{ "alcPauseCurrentDevice", (uintptr_t)&ret0 },
	{ "alcResumeCurrentDevice", (uintptr_t)&ret0 },
	{ "alcSuspendContext", (uintptr_t)&alcSuspendContext },
	{ "asin", (uintptr_t)&asin },
	{ "asinf", (uintptr_t)&asinf },
	{ "atan", (uintptr_t)&atan },
	{ "atan2", (uintptr_t)&atan2 },
	{ "atan2f", (uintptr_t)&atan2f },
	{ "atanf", (uintptr_t)&atanf },
	{ "atoi", (uintptr_t)&atoi },
	{ "atol", (uintptr_t)&atol },
	{ "atoll", (uintptr_t)&atoll },
	{ "bsearch", (uintptr_t)&bsearch },
	{ "btowc", (uintptr_t)&btowc },
	{ "calloc", (uintptr_t)&calloc },
	{ "ceil", (uintptr_t)&ceil },
	{ "ceilf", (uintptr_t)&ceilf },
	{ "clearerr", (uintptr_t)&clearerr },
	{ "clock_gettime", (uintptr_t)&clock_gettime },
	{ "close", (uintptr_t)&close },
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
	{ "fmod", (uintptr_t)&fmod },
	{ "fmodf", (uintptr_t)&fmodf },
	{ "fopen", (uintptr_t)&fopen_hook },
	{ "fprintf", (uintptr_t)&fprintf },
	{ "fputc", (uintptr_t)&fputc },
	{ "fputs", (uintptr_t)&fputs },
	{ "fread", (uintptr_t)&fread },
	{ "free", (uintptr_t)&free },
	{ "frexp", (uintptr_t)&frexp },
	{ "frexpf", (uintptr_t)&frexpf },
	{ "fscanf", (uintptr_t)&fscanf },
	{ "fseek", (uintptr_t)&fseek },
	{ "fseeko", (uintptr_t)&fseeko },
	{ "fstat", (uintptr_t)&fstat_hook },
	{ "ftell", (uintptr_t)&ftell },
	{ "ftello", (uintptr_t)&ftello },
	{ "fwrite", (uintptr_t)&fwrite },
	{ "getc", (uintptr_t)&getc },
	{ "getenv", (uintptr_t)&ret0 },
	{ "getwc", (uintptr_t)&getwc },
	{ "gettimeofday", (uintptr_t)&gettimeofday },
	{ "gmtime64", (uintptr_t)&gmtime64 },
	{ "inflate", (uintptr_t)&inflate },
	{ "inflateEnd", (uintptr_t)&inflateEnd },
	{ "inflateInit_", (uintptr_t)&inflateInit_ },
	{ "inflateInit2_", (uintptr_t)&inflateInit2_ },
	{ "inflateReset", (uintptr_t)&inflateReset },
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
	{ "llrint", (uintptr_t)&llrint },
	{ "localtime_r", (uintptr_t)&localtime_r },
	{ "localtime64", (uintptr_t)&localtime64 },
	{ "log", (uintptr_t)&log },
	{ "log10", (uintptr_t)&log10 },
	{ "longjmp", (uintptr_t)&longjmp },
	{ "lrand48", (uintptr_t)&lrand48 },
	{ "lrint", (uintptr_t)&lrint },
	{ "lrintf", (uintptr_t)&lrintf },
	{ "lseek", (uintptr_t)&lseek },
	{ "malloc", (uintptr_t)&malloc },
	{ "mbrtowc", (uintptr_t)&mbrtowc },
	{ "memchr", (uintptr_t)&sceClibMemchr },
	{ "memcmp", (uintptr_t)&memcmp },
	{ "memcpy", (uintptr_t)&sceClibMemcpy },
	{ "memmove", (uintptr_t)&sceClibMemmove },
	{ "memset", (uintptr_t)&sceClibMemset },
	{ "mkdir", (uintptr_t)&mkdir },
	{ "mktime", (uintptr_t)&mktime },
	{ "mktime64", (uintptr_t)&mktime64 },
	{ "mmap", (uintptr_t)&mmap},
	{ "munmap", (uintptr_t)&munmap},
	{ "modf", (uintptr_t)&modf },
	{ "modff", (uintptr_t)&modff },
	{ "nanosleep", (uintptr_t)&nanosleep },
	{ "open", (uintptr_t)&open },
	{ "pow", (uintptr_t)&pow },
	{ "powf", (uintptr_t)&powf },
	{ "printf", (uintptr_t)&printf },
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
	{ "pthread_setspecific", (uintptr_t)&pthread_setspecific},
	{ "pthread_getspecific", (uintptr_t)&pthread_getspecific},
	{ "putc", (uintptr_t)&putc },
	{ "putwc", (uintptr_t)&putwc },
	{ "qsort", (uintptr_t)&qsort },
	{ "read", (uintptr_t)&read },
	{ "realloc", (uintptr_t)&realloc },
	{ "remove", (uintptr_t)&sceIoRemove },
	{ "rint", (uintptr_t)&rint },
	{ "scandir", (uintptr_t)&scandir },
	{ "setenv", (uintptr_t)&ret0 },
	{ "setjmp", (uintptr_t)&setjmp },
	{ "setlocale", (uintptr_t)&ret0 },
	{ "setvbuf", (uintptr_t)&setvbuf },
	{ "sin", (uintptr_t)&sin },
	{ "sincosf", (uintptr_t)&sincosf },
	{ "sinf", (uintptr_t)&sinf },
	{ "sinh", (uintptr_t)&sinh },
	{ "snprintf", (uintptr_t)&snprintf },
	{ "sprintf", (uintptr_t)&sprintf },
	{ "sqrt", (uintptr_t)&sqrt },
	{ "sqrtf", (uintptr_t)&sqrtf },
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
	{ "strftime", (uintptr_t)&strftime },
	{ "strlen", (uintptr_t)&strlen },
	{ "strncasecmp", (uintptr_t)&sceClibStrncasecmp },
	{ "strncat", (uintptr_t)&sceClibStrncat },
	{ "strncmp", (uintptr_t)&sceClibStrncmp },
	{ "strncpy", (uintptr_t)&sceClibStrncpy },
	{ "strpbrk", (uintptr_t)&strpbrk },
	{ "strrchr", (uintptr_t)&sceClibStrrchr },
	{ "strstr", (uintptr_t)&sceClibStrstr },
	{ "strtod", (uintptr_t)&strtod },
	{ "strtok", (uintptr_t)&strtok },
	{ "strtol", (uintptr_t)&strtol },
	{ "strtoll", (uintptr_t)&strtoll },
	{ "strtoul", (uintptr_t)&strtoul },
	{ "strxfrm", (uintptr_t)&strxfrm },
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
	{ "usleep", (uintptr_t)&usleep },
	{ "vasprintf", (uintptr_t)&vasprintf },
	{ "vfprintf", (uintptr_t)&vfprintf },
	{ "vprintf", (uintptr_t)&vprintf },
	{ "vsnprintf", (uintptr_t)&vsnprintf },
	{ "vsprintf", (uintptr_t)&vsprintf },
	{ "vswprintf", (uintptr_t)&vswprintf },
	{ "wcrtomb", (uintptr_t)&wcrtomb },
	{ "wcscoll", (uintptr_t)&wcscoll },
	{ "wcscmp", (uintptr_t)&wcscmp },
	{ "wcsncpy", (uintptr_t)&wcsncpy },
	{ "wcsftime", (uintptr_t)&wcsftime },
	{ "wcslen", (uintptr_t)&wcslen },
	{ "wcsxfrm", (uintptr_t)&wcsxfrm },
	{ "wctob", (uintptr_t)&wctob },
	{ "wctype", (uintptr_t)&wctype },
	{ "wmemchr", (uintptr_t)&wmemchr },
	{ "wmemcmp", (uintptr_t)&wmemcmp },
	{ "wmemcpy", (uintptr_t)&wmemcpy },
	{ "wmemmove", (uintptr_t)&wmemmove },
	{ "wmemset", (uintptr_t)&wmemset },
	{ "write", (uintptr_t)&write },
};

int check_kubridge(void) {
	int search_unk[2];
	return _vshKernelSearchModuleByName("kubridge", search_unk);
}

enum MethodIDs {
	UNKNOWN = 0,
	INIT,
	GET_UDID
} MethodIDs;

typedef struct {
	char *name;
	enum MethodIDs id;
} NameToMethodID;

static NameToMethodID name_to_method_ids[] = {
	{ "<init>", INIT },
	{ "GetUDID", GET_UDID },
};

int GetMethodID(void *env, void *class, const char *name, const char *sig) {
	debugPrintf("GetMethodID: %s\n", name);

	for (int i = 0; i < sizeof(name_to_method_ids) / sizeof(NameToMethodID); i++) {
		if (strcmp(name, name_to_method_ids[i].name) == 0) {
			return name_to_method_ids[i].id;
		}
	}

	return UNKNOWN;
}

int GetStaticMethodID(void *env, void *class, const char *name, const char *sig) {
	debugPrintf("GetStaticMethodID: %s\n", name);
	
	for (int i = 0; i < sizeof(name_to_method_ids) / sizeof(NameToMethodID); i++) {
		if (strcmp(name, name_to_method_ids[i].name) == 0)
			return name_to_method_ids[i].id;
	}

	return UNKNOWN;
}

void CallStaticVoidMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
}

int CallStaticBooleanMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	return 0;
}

int CallStaticByteMethod(void *env, void *obj, int methodID, uintptr_t *args) {
	return 0;
}


uint64_t CallLongMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	return 0;
}

void *FindClass(void) {
	return (void *)0x41414141;
}

void *NewGlobalRef(void *env, char *str) {
	return (void *)0x42424242;
}

void DeleteGlobalRef(void *env, char *str) {
}

void *NewObjectV(void *env, void *clazz, int methodID, uintptr_t args) {
	return (void *)0x43434343;
}

void *GetObjectClass(void *env, void *obj) {
	return (void *)0x44444444;
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
	return NULL;
}

void *CallStaticObjectMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	static char *r = NULL;
	switch (methodID) {
	case GET_UDID:
		if (!r)
			r = malloc(0x10);
		strcpy(r, "1234");
		return r;
	default:
		return NULL;
	}
}

int CallStaticIntMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	switch (methodID) {
	default:
		return 0;
	}
}

int CallBooleanMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	switch (methodID) {
	default:
		return 0;
	}
}

double CallDoubleMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	return 0.0;
}

void CallVoidMethodV(void *env, void *obj, int methodID, uintptr_t *args) {
	switch (methodID) {
	default:
		break;
	}
}

void *NewIntArray(void *env, int size) {
	return malloc(sizeof(int) * size);	
}

void *NewObjectArray(void *env, int size) {
	return NULL;	
}

void *NewDoubleArray(void *env, int size) {
	return malloc(sizeof(double) * size);	
}

void SetIntArrayRegion(void *env, int *array, int start, int len, int *buf) {
	sceClibMemcpy(&array[start], buf, sizeof(int) * len);
}

void SetDoubleArrayRegion(void *env, double *array, int start, int len, double *buf) {
	sceClibMemcpy(&array[start], buf, sizeof(double) * len);
}

void SetObjectArrayElement(void *env, void *array, int size, void *val) {
}

int GetIntField(void *env, void *obj, int fieldID) { return 0; }

int gms_main(unsigned int argc, void *argv) {
	// Checking requested game launch
	char game_name[0x200];
	FILE *f = fopen(LAUNCH_FILE_PATH, "r");
	if (f) {
		size_t size = fread(game_name, 1, 0x200, f);
		fclose(f);
		sceIoRemove(LAUNCH_FILE_PATH);
		game_name[size] = 0;
	} else {
		strcpy(game_name, "AM2R"); // Debug
	}
	
	// Enabling analogs and touch sampling
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
	sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);
	sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
	sceMotionStartSampling();

	// Maximizing clocks
	scePowerSetArmClockFrequency(444);
	scePowerSetBusClockFrequency(222);
	scePowerSetGpuClockFrequency(222);
	scePowerSetGpuXbarClockFrequency(166);
	
	// Populating required strings and creating required folders
	char apk_path[256], pkg_name[256];
	sprintf(apk_path, "%s/%s/game.apk", DATA_PATH, game_name);
	sprintf(data_path, "%s/%s/assets/", DATA_PATH, game_name);
	
	strcpy(pkg_name, "com.rinnegatamante.loader");
	sceIoMkdir(data_path, 0777);
	sceIoMkdir("ux0:data/gms/shared", 0777);
	sceIoMkdir("ux0:data/gms/shared/gxp", 0777);
	sceIoMkdir("ux0:data/gms/shared/glsl", 0777);

	// Checking for dependencies
	if (check_kubridge() < 0)
		fatal_error("Error kubridge.skprx is not installed.");
	if (!file_exists("ur0:/data/libshacccg.suprx") && !file_exists("ur0:/data/external/libshacccg.suprx"))
		fatal_error("Error libshacccg.suprx is not installed.");
	
	// Loading ARMv7 executable from the apk
	unz_file_info file_info;
	unzFile apk_file = unzOpen(apk_path);
	unzLocateFile(apk_file, "lib/armeabi-v7a/libyoyo.so", NULL);
	unzGetCurrentFileInfo(apk_file, &file_info, NULL, 0, NULL, 0, NULL, 0);
	unzOpenCurrentFile(apk_file);
	uint64_t so_size = file_info.uncompressed_size;
	uint8_t *so_buffer = (uint8_t *)malloc(so_size);
	unzReadCurrentFile(apk_file, so_buffer, so_size);
	unzCloseCurrentFile(apk_file);
	if (so_mem_load(&yoyoloader_mod, so_buffer, so_size, LOAD_ADDRESS) < 0)
		fatal_error("Error could not load lib/armeabi-v7a/libyoyo.so from inside game.apk.");
	free(so_buffer);
	
	// Loading config file
	loadConfig(game_name);

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
	unzClose(apk_file);

	// Patching the executable
	so_relocate(&yoyoloader_mod);
	so_resolve(&yoyoloader_mod, default_dynlib, sizeof(default_dynlib), 0);
	patch_openal();
	patch_runner();
	so_flush_caches(&yoyoloader_mod);
	so_initialize(&yoyoloader_mod);
	patch_runner_post_init();
	patch_gamepad();
	so_flush_caches(&yoyoloader_mod);
	
	// Initializing vitaGL
	vglSetupGarbageCollector(127, 0x20000);
	if (maximizeMem)
		vglInitWithCustomThreshold(0, SCREEN_W, SCREEN_H, MEMORY_VITAGL_THRESHOLD_MB * 1024 * 1024, 0, 0, 0, SCE_GXM_MULTISAMPLE_4X);
	else
		vglInitExtended(0, SCREEN_W, SCREEN_H, MEMORY_VITAGL_THRESHOLD_MB * 1024 * 1024, SCE_GXM_MULTISAMPLE_4X);
	vgl_booted = 1;

	// Initializing Java VM and JNI Interface
	memset(fake_vm, 'A', sizeof(fake_vm));
	*(uintptr_t *)(fake_vm + 0x00) = (uintptr_t)fake_vm; // just point to itself...
	*(uintptr_t *)(fake_vm + 0x10) = (uintptr_t)GetEnv;
	*(uintptr_t *)(fake_vm + 0x14) = (uintptr_t)ret0;
	*(uintptr_t *)(fake_vm + 0x18) = (uintptr_t)GetEnv;
	memset(fake_env, 'A', sizeof(fake_env));
	*(uintptr_t *)(fake_env + 0x00) = (uintptr_t)fake_env; // just point to itself...
	*(uintptr_t *)(fake_env + 0x18) = (uintptr_t)FindClass;
	*(uintptr_t *)(fake_env + 0x54) = (uintptr_t)NewGlobalRef;
	*(uintptr_t *)(fake_env + 0x58) = (uintptr_t)DeleteGlobalRef;
	*(uintptr_t *)(fake_env + 0x5C) = (uintptr_t)ret0; // DeleteLocalRef
	*(uintptr_t *)(fake_env + 0x68) = (uintptr_t)ret0; // EnsureLocalCapacity
	*(uintptr_t *)(fake_env + 0x74) = (uintptr_t)NewObjectV;
	*(uintptr_t *)(fake_env + 0x7C) = (uintptr_t)GetObjectClass;
	*(uintptr_t *)(fake_env + 0x84) = (uintptr_t)GetMethodID;
	*(uintptr_t *)(fake_env + 0x8C) = (uintptr_t)CallObjectMethodV;
	*(uintptr_t *)(fake_env + 0x98) = (uintptr_t)CallBooleanMethodV;
	*(uintptr_t *)(fake_env + 0xD4) = (uintptr_t)CallLongMethodV;
	*(uintptr_t *)(fake_env + 0xEC) = (uintptr_t)CallDoubleMethodV;
	*(uintptr_t *)(fake_env + 0xF8) = (uintptr_t)CallVoidMethodV;
	*(uintptr_t *)(fake_env + 0x178) = (uintptr_t)GetFieldID;
	*(uintptr_t *)(fake_env + 0x17C) = (uintptr_t)GetBooleanField;
	*(uintptr_t *)(fake_env + 0x190) = (uintptr_t)GetIntField;
	*(uintptr_t *)(fake_env + 0x1C4) = (uintptr_t)GetStaticMethodID;
	*(uintptr_t *)(fake_env + 0x1CC) = (uintptr_t)CallStaticObjectMethodV;
	*(uintptr_t *)(fake_env + 0x1D8) = (uintptr_t)CallStaticBooleanMethodV;
	*(uintptr_t *)(fake_env + 0x1E0) = (uintptr_t)CallStaticByteMethod;
	*(uintptr_t *)(fake_env + 0x208) = (uintptr_t)CallStaticIntMethodV;
	*(uintptr_t *)(fake_env + 0x238) = (uintptr_t)CallStaticVoidMethodV;
	*(uintptr_t *)(fake_env + 0x240) = (uintptr_t)GetStaticFieldID;
	*(uintptr_t *)(fake_env + 0x244) = (uintptr_t)GetStaticObjectField;
	*(uintptr_t *)(fake_env + 0x29C) = (uintptr_t)NewStringUTF;
	*(uintptr_t *)(fake_env + 0x2A4) = (uintptr_t)GetStringUTFChars;
	*(uintptr_t *)(fake_env + 0x2A8) = (uintptr_t)ret0; // ReleaseStringUTFChars
	*(uintptr_t *)(fake_env + 0x2B0) = (uintptr_t)NewObjectArray;
	*(uintptr_t *)(fake_env + 0x2B8) = (uintptr_t)SetObjectArrayElement;
	*(uintptr_t *)(fake_env + 0x2CC) = (uintptr_t)NewIntArray;
	*(uintptr_t *)(fake_env + 0x2D8) = (uintptr_t)NewDoubleArray;
	*(uintptr_t *)(fake_env + 0x34C) = (uintptr_t)SetIntArrayRegion;
	*(uintptr_t *)(fake_env + 0x358) = (uintptr_t)SetDoubleArrayRegion;
	*(uintptr_t *)(fake_env + 0x36C) = (uintptr_t)GetJavaVM;
	
	int (*Java_com_yoyogames_runner_RunnerJNILib_RenderSplash) (void *env, int a2, char *apk_path, char *fname, int w, int h, signed int tex_w, signed int tex_h, signed int png_w, signed int png_h) = (void *)so_symbol(&yoyoloader_mod, "Java_com_yoyogames_runner_RunnerJNILib_RenderSplash");
	int (*Java_com_yoyogames_runner_RunnerJNILib_Startup) (void *env, int a2, char *apk_path, char *save_dir, char *pkg_dir, int sleep_margin) = (void *)so_symbol(&yoyoloader_mod, "Java_com_yoyogames_runner_RunnerJNILib_Startup");
	
	// Displaying splash screen
	if (splash_buf) {
		int w, h;
		uint8_t *bg_data = stbi_load_from_memory(splash_buf, splash_size, &w, &h, NULL, 4);
		GLuint bg_image;
		glGenTextures(1, &bg_image);
		glBindTexture(GL_TEXTURE_2D, bg_image);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, bg_data);
		free(bg_data);
		free(splash_buf);
		if (forceGL1)
			forceGL1 = 2;
		Java_com_yoyogames_runner_RunnerJNILib_RenderSplash(fake_env, 0, apk_path, "splash.png", SCREEN_W, SCREEN_H, SCREEN_W, SCREEN_H, SCREEN_W, SCREEN_H);
		if (forceGL1) {
			forceGL1 = 1;
			*(int *)so_symbol(&yoyoloader_mod, "g_GL_funcs_imported") = 0;
			glUseProgram(0);
		}
		vglSwapBuffers(GL_FALSE);
		glDeleteTextures(1, &bg_image);
	}
	
	// Starting the Runner
	Java_com_yoyogames_runner_RunnerJNILib_Startup(fake_env, 0, apk_path, data_path, pkg_name, 0);
	
	// Entering main loop
	debugPrintf("Startup ended\n");
	main_loop();

	return 0;
}

int main(int argc, char **argv)
{
	// Debug
#if 0
	sceSysmoduleLoadModule(9); // Razor Capture
#endif
	ALCint attrlist[6];
	attrlist[0] = ALC_FREQUENCY;
	attrlist[1] = 44100;
	attrlist[2] = ALC_SYNC;
	attrlist[3] = AL_FALSE;
	attrlist[4] = 0;

	ALDevice	= alcOpenDevice(NULL);
	if (ALDevice == NULL)
		debugPrintf("Error while opening AL device\n");
	ALContext = alcCreateContext(ALDevice, attrlist);
	if (ALContext == NULL)
		debugPrintf("Error while creating AL context\n");
	if (!alcMakeContextCurrent(ALContext))
		debugPrintf("Error while making AL context current\n");
	
	if (forceMainThread) {
		gms_main(0, NULL);
	} else {
		SceUID main_thread = sceKernelCreateThread("YoyoLoader", gms_main, 0x40, 0x800000, 0, 0, NULL);
		if (main_thread >= 0){
			sceKernelStartThread(main_thread, 0, NULL);
			sceKernelWaitThreadEnd(main_thread, NULL, NULL);
		}
	}
}
