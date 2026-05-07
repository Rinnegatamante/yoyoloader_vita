#include <vitasdk.h>
#include <vitaGL.h>
#include <stdio.h>
#include <malloc.h>
#include <string.h>

#define FB_ALIGNMENT 0x40000
#define ALIGN_MEM(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

enum {
	PLAYER_INACTIVE,
	PLAYER_ACTIVE,
	PLAYER_STOP,
};

SceAvPlayerHandle movie_player;

int player_state = PLAYER_INACTIVE;

GLuint movie_frame[5];
uint8_t movie_frame_idx = 0;
SceGxmTexture *movie_tex[5];
GLuint movie_fs;
GLuint movie_vs;
GLuint movie_prog;

SceUID audio_thid;
int audio_new;
int audio_port;
int audio_len;
int audio_freq;
int audio_mode;
bool first_frame = true;

void *mem_alloc(void *p, uint32_t align, uint32_t size) {
	return memalign(align, size);
}

void mem_free(void *p, void *ptr) {
	free(ptr);
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
		audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, 1024, 48000, SCE_AUDIO_OUT_MODE_STEREO);
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
		sceAudioOutSetConfig(audio_port, audio_len, audio_freq, (SceAudioOutMode)audio_mode);
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

void video_close() {
	if (player_state == PLAYER_ACTIVE) {
		sceAvPlayerStop(movie_player);
		sceKernelWaitThreadEnd(audio_thid, NULL, NULL);
		sceAvPlayerClose(movie_player);
		movie_audio_shutdown();
		player_state = PLAYER_INACTIVE;
		glDeleteTextures(5, movie_frame);
	}
}

void video_open(const char *path) {
	first_frame = true;
	glGenTextures(5, movie_frame);
	for (int i = 0; i < 5; i++) {
		glBindTexture(GL_TEXTURE_2D, movie_frame[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		movie_tex[i] = vglGetGxmTexture(GL_TEXTURE_2D);
		vglFree(vglGetTexDataPointer(GL_TEXTURE_2D));
	}
	
	movie_audio_init();
	
	SceAvPlayerInitData playerInit;
	memset(&playerInit, 0, sizeof(SceAvPlayerInitData));

	playerInit.memoryReplacement.allocate = mem_alloc;
	playerInit.memoryReplacement.deallocate = mem_free;
	playerInit.memoryReplacement.allocateTexture = gpu_alloc;
	playerInit.memoryReplacement.deallocateTexture = gpu_free;

	playerInit.basePriority = 0xA0;
	playerInit.numOutputVideoFrameBuffers = 5;
	playerInit.autoStart = GL_TRUE;
#if 0
	playerInit.debugLevel = 3;
#endif

	movie_player = sceAvPlayerInit(&playerInit);

	sceAvPlayerAddSource(movie_player, path);

	audio_thid = sceKernelCreateThread("movie_audio_thread", movie_audio_thread, 0x10000100 - 10, 0x4000, 0, 0, NULL);
	sceKernelStartThread(audio_thid, 0, NULL);
	
	player_state = PLAYER_ACTIVE;
}

GLuint video_get_frame(int *width, int *height) {
	if (player_state == PLAYER_ACTIVE) {
		if (sceAvPlayerIsActive(movie_player)) {
			SceAvPlayerFrameInfo frame;
			if (sceAvPlayerGetVideoData(movie_player, &frame)) {
				movie_frame_idx = (movie_frame_idx + 1) % 5;
				sceGxmTextureInitLinear(
					movie_tex[movie_frame_idx],
					frame.pData,
					SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1,
					frame.details.video.width,
					frame.details.video.height, 0);
				*width = frame.details.video.width;
				*height = frame.details.video.height;
				sceGxmTextureSetMinFilter(movie_tex[movie_frame_idx], SCE_GXM_TEXTURE_FILTER_LINEAR);
				sceGxmTextureSetMagFilter(movie_tex[movie_frame_idx], SCE_GXM_TEXTURE_FILTER_LINEAR);
				first_frame = false;
			}
			return first_frame ? 0xDEADBEEF : movie_frame[movie_frame_idx];
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
	}
	
	return 0xDEADBEEF;
}