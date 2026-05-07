#include <vitasdk.h>
#include <vitaGL.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>

#include "main.h"
#include "so_util.h"
#include "unzip.h"

#include "shaders/movie_f.h"
#include "shaders/movie_v.h"

#ifdef HAS_VIDEO_PLAYBACK_SUPPORT
#define FB_ALIGNMENT 0x40000

extern char apk_path[256];
extern so_module yoyoloader_mod;

enum {
	PLAYER_INACTIVE,
	PLAYER_ACTIVE,
	PLAYER_STOP,
};

SceAvPlayerHandle movie_player;

int player_state = PLAYER_INACTIVE;

GLuint movie_frame[2];
uint8_t movie_frame_idx = 0;
SceGxmTexture *movie_tex[2];
GLuint movie_fs;
GLuint movie_vs;
GLuint movie_prog;

SceUID audio_thid;
int audio_new;
int audio_port;
int audio_len;
int audio_freq;
int audio_mode;

float movie_pos[8] = {
	-1.0f, 1.0f,
	-1.0f, -1.0f,
	 1.0f, 1.0f,
	 1.0f, -1.0f
};

float movie_texcoord[8] = {
	0.0f, 1.0f,
	0.0f, 0.0f,
	1.0f, 1.0f,
	1.0f, 0.0f
};

unzFile apk_file;
unz_file_info file_info;

double video_w, video_h;

void *mem_alloc(void *p, uint32_t align, uint32_t size) {
	return memalign(align, size);
}

void mem_free(void *p, void *ptr) {
	free(ptr);
}

int open_file_cb(void *p, const char *file) {
	int method;
	apk_file = unzOpen(apk_path);
	unzLocateFile(apk_file, file, NULL);
	unzGetCurrentFileInfo(apk_file, &file_info, NULL, 0, NULL, 0, NULL, 0);
	return unzOpenCurrentFile2(apk_file, &method, NULL, 1);
}

int close_file_cb(void *p) {
	unzCloseCurrentFile(apk_file);
	return unzClose(apk_file);
}

int read_file_cb(void *p, uint8_t *buf, uint64_t off, uint32_t len) {
	if (off != unztell64(apk_file)) {
		unzseek64(apk_file, off + 2, SEEK_SET); // FIXME: Some files need this '+ 2', some don't, figure out what is going on here
	}
	return unzReadCurrentFile(apk_file, buf, len);
}

uint64_t size_file_cb(void *p) {
	return file_info.uncompressed_size;
}

void *gpu_alloc(void *p, uint32_t align, uint32_t size) {
	if (align < FB_ALIGNMENT) {
		align = FB_ALIGNMENT;
	}
	size = ALIGN_MEM(size, align);
	return vglAlloc(size, VGL_MEM_SLOW);
}

void gpu_free(void *p, void *ptr) {
	glFinish();
	vglFree(ptr);
}

void movie_audio_init(void) {
	audio_port = -1;
	for (int i = 0; i < 8; i++) {
		if (sceAudioOutGetConfig(i, SCE_AUDIO_OUT_CONFIG_TYPE_LEN) >= 0) {
			audio_port = i;
			break;
		}
	}

	if (audio_port == -1) {
		audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 1024, 48000, SCE_AUDIO_OUT_MODE_STEREO);
		audio_new = 1;
	} else {
		audio_len = sceAudioOutGetConfig(audio_port, SCE_AUDIO_OUT_CONFIG_TYPE_LEN);
		audio_freq = sceAudioOutGetConfig(audio_port, SCE_AUDIO_OUT_CONFIG_TYPE_FREQ);
		audio_mode = sceAudioOutGetConfig(audio_port, SCE_AUDIO_OUT_CONFIG_TYPE_MODE);
		audio_new = 0;
	}
}

void movie_audio_shutdown(void) {
	if (audio_new) {
		sceAudioOutReleasePort(audio_port);
	} else {
		sceAudioOutSetConfig(audio_port, audio_len, audio_freq, audio_mode);
	}
}

int movie_audio_thread(SceSize args, void *argp) {
	SceAvPlayerFrameInfo frame;
	sceClibMemset(&frame, 0, sizeof(SceAvPlayerFrameInfo));

	while (player_state == PLAYER_ACTIVE && sceAvPlayerIsActive(movie_player)) {
		if (sceAvPlayerGetAudioData(movie_player, &frame)) {
			sceAudioOutSetConfig(audio_port, 1024, frame.details.audio.sampleRate, frame.details.audio.channelCount == 1 ? SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO);
			sceAudioOutOutput(audio_port, frame.pData);
		} else {
			sceKernelDelayThread(1000);
		}
	}

	return sceKernelExitDeleteThread(0);
}

int YYVideoOpen(const char *path) {
	video_w = 0.0f;
	video_h = 0.0f;
	
	char final_path[256];
	sprintf(final_path, "assets/%s", path);
	
	glGenTextures(2, movie_frame);
	for (int i = 0; i < 2; i++) {
		glBindTexture(GL_TEXTURE_2D, movie_frame[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SCREEN_W, SCREEN_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		movie_tex[i] = vglGetGxmTexture(GL_TEXTURE_2D);
		vglFree(vglGetTexDataPointer(GL_TEXTURE_2D));
	}

	movie_vs = glCreateShader(GL_VERTEX_SHADER);
	glShaderBinary(1, &movie_vs, 0, movie_v, size_movie_v);

	movie_fs = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderBinary(1, &movie_fs, 0, movie_f, size_movie_f);

	movie_prog = glCreateProgram();
	glAttachShader(movie_prog, movie_vs);
	glAttachShader(movie_prog, movie_fs);
	glBindAttribLocation(movie_prog, 0, "inPos");
	glBindAttribLocation(movie_prog, 1, "inTex");
	glLinkProgram(movie_prog);
	glUniform1i(glGetUniformLocation(movie_prog, "tex"), 0);
	
	movie_audio_init();
	
	SceAvPlayerInitData playerInit;
	memset(&playerInit, 0, sizeof(SceAvPlayerInitData));

	playerInit.memoryReplacement.allocate = mem_alloc;
	playerInit.memoryReplacement.deallocate = mem_free;
	playerInit.memoryReplacement.allocateTexture = gpu_alloc;
	playerInit.memoryReplacement.deallocateTexture = gpu_free;

	playerInit.fileReplacement.objectPointer = NULL;
	playerInit.fileReplacement.open = open_file_cb;
	playerInit.fileReplacement.close = close_file_cb;
	playerInit.fileReplacement.readOffset = read_file_cb;
	playerInit.fileReplacement.size = size_file_cb;

	playerInit.basePriority = 0xA0;
	playerInit.numOutputVideoFrameBuffers = 2;
	playerInit.autoStart = GL_TRUE;
#if 0
	playerInit.debugLevel = 3;
#endif

	movie_player = sceAvPlayerInit(&playerInit);

	sceAvPlayerAddSource(movie_player, final_path);

	audio_thid = sceKernelCreateThread("movie_audio_thread", movie_audio_thread, 0x10000100 - 10, 0x4000, 0, 0, NULL);
	sceKernelStartThread(audio_thid, 0, NULL);

	player_state = PLAYER_ACTIVE;
	return 1;
}

int YYVideoDraw(void *buffer, int width, int height) {
	if (player_state == PLAYER_ACTIVE) {
		if (sceAvPlayerIsActive(movie_player)) {
			SceAvPlayerFrameInfo frame;
			if (sceAvPlayerGetVideoData(movie_player, &frame)) {
				movie_frame_idx = (movie_frame_idx + 1) % 2;
				sceGxmTextureInitLinear(
					movie_tex[movie_frame_idx],
					frame.pData,
					SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1,
					frame.details.video.width,
					frame.details.video.height, 0);
				video_w = (double)frame.details.video.width;
				video_h = (double)frame.details.video.height;
				sceGxmTextureSetMinFilter(movie_tex[movie_frame_idx], SCE_GXM_TEXTURE_FILTER_LINEAR);
				sceGxmTextureSetMagFilter(movie_tex[movie_frame_idx], SCE_GXM_TEXTURE_FILTER_LINEAR);
			}
			glBindTexture(GL_TEXTURE_2D, movie_frame[movie_frame_idx]);
			glUseProgram(movie_prog);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
			glEnableVertexAttribArray(0);
			glEnableVertexAttribArray(1);
			glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, &movie_pos[0]);
			glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, &movie_texcoord[0]);
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
			glUseProgram(0);
		} else {
			player_state = PLAYER_STOP;
		}
	}

	if (player_state == PLAYER_STOP) {
		sceAvPlayerStop(movie_player);
		sceKernelWaitThreadEnd(audio_thid, NULL, NULL);
		sceAvPlayerClose(movie_player);
		movie_audio_shutdown();
		player_state = PLAYER_INACTIVE;
		glClear(GL_COLOR_BUFFER_BIT);
	}
	
	return player_state == PLAYER_STOP;
}

double YYVideoW() {
	return video_w;
}

double YYVideoH() {
	return video_h;
}

double YYVideoStatus() {
	return (double)(player_state == PLAYER_STOP);
}

void patch_video_player() {
	hook_addr(so_symbol(&yoyoloader_mod, "_Z11YYVideoOpenPKc"), (uintptr_t)&YYVideoOpen);
	hook_addr(so_symbol(&yoyoloader_mod, "_Z11YYVideoDrawPvii"), (uintptr_t)&YYVideoDraw);
	hook_addr(so_symbol(&yoyoloader_mod, "_Z8YYVideoWv"), (uintptr_t)&YYVideoW);
	hook_addr(so_symbol(&yoyoloader_mod, "_Z8YYVideoHv"), (uintptr_t)&YYVideoH);
	hook_addr(so_symbol(&yoyoloader_mod, "_Z13YYVideoStatusv"), (uintptr_t)&YYVideoStatus);
}
#endif
