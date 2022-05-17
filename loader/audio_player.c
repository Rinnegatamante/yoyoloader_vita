#include <vitasdk.h>
#include <vitaGL.h>
#include <stdio.h>
#include <malloc.h>
#include <AL/al.h>
#include <AL/alc.h>
#include <mpg123.h>

#define CHUNK_SIZE 176400

#include "main.h"
#include "so_util.h"
#include "unzip.h"

static mpg123_handle *h;
static int inited = 0;
static volatile int is_playing = 0;
static uint8_t *mp3_buffer = NULL;
static int mp3_buffer_pos = 0;
static int mp3_size = 0;
static volatile uint8_t is_stereo;
static volatile uint32_t samplerate;
static ALuint al_buffers[2];
static ALuint al_source = 0xDEADBEEF;
static SceUID critical_section;
static uint8_t *audio_buffer = NULL;
static volatile int is_looping;
extern char apk_path[256];

static ssize_t mp3_read(void *io, void *buffer, size_t nbyte) {
	if (mp3_buffer_pos + nbyte > mp3_size)
		nbyte = mp3_size - mp3_buffer_pos;
	if (nbyte) {
		sceClibMemcpy(buffer, &mp3_buffer[mp3_buffer_pos], nbyte);
		mp3_buffer_pos += nbyte;
	}
	return nbyte;
}

static off_t mp3_seek(void *io, off_t offset, int seek_type) {
	switch (seek_type) {
	case SEEK_SET:
		if (offset < mp3_size)
			mp3_buffer_pos = offset;
		else
			mp3_buffer_pos = mp3_size;
		return mp3_buffer_pos;
		break;
	case SEEK_CUR:
		if (mp3_buffer_pos + offset < mp3_size && mp3_buffer_pos + offset >= 0)
			mp3_buffer_pos += offset;
		else if (mp3_buffer_pos + offset < 0)
			mp3_buffer_pos = 0;
		else
			mp3_buffer_pos = mp3_size;
		return mp3_buffer_pos;
	case SEEK_END:
		if (offset <= 0 && mp3_size + offset >= 0)
			mp3_buffer_pos = mp3_size + offset;
		else
			mp3_buffer_pos = mp3_size;
		return mp3_buffer_pos;
	default:
		return -1;
	}
}

static void mp3_close(void *io) {
	vglFree(mp3_buffer);
}

int mp3_fill(mpg123_handle *handle, uint8_t *buffer, int length, int loops) {
	int err;
	size_t done = 0;
	size_t decoded = 0;
  
	do {
		err = mpg123_read(handle, buffer, length, &done);
		decoded += done;
	} while (done && err != MPG123_OK);
  
	if (decoded < length) {
		if (loops) {
			mpg123_seek(handle, 0, SEEK_SET);
			return mp3_fill(handle, buffer + decoded, length - decoded, loops);
		} else {
			sceClibMemset(buffer + decoded, 0, length - decoded);
		}
	}
  
	return decoded;
}

static int music_thread(unsigned int args, void *arg) {
	uint8_t idx = 0;
	for (;;) {
		sceKernelWaitSema(critical_section, 1, NULL);
		if (is_playing) {
			int processed;
			alGetSourcei(al_source, AL_BUFFERS_PROCESSED, &processed);
			while (processed--) {
				ALuint buffer;
				alSourceUnqueueBuffers(al_source, 1, &buffer);
				if (mp3_fill(h, audio_buffer + CHUNK_SIZE * idx, CHUNK_SIZE, is_looping)) {
					alBufferData(buffer, is_stereo ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16, audio_buffer + CHUNK_SIZE * idx, CHUNK_SIZE, samplerate);
				} else {
					is_playing = 0;
				}
				if (is_playing) {
					alSourceQueueBuffers(al_source, 1, &buffer);
					idx = (idx + 1) % 2;
				}
			}
			
			// Hacky workaround
			ALint state;
			alGetSourcei(al_source, AL_SOURCE_STATE, &state);
			if (state != AL_PLAYING) {
				alSourcePlay(al_source);
			}
		}
		sceKernelSignalSema(critical_section, 1);
		sceKernelDelayThread(100);
	}
}

void audio_player_init() {
	if (!inited) {
		mpg123_init();
		critical_section = sceKernelCreateSema("Critical Section", 0, 1, 1, NULL);
		SceUID musics_updater = sceKernelCreateThread("Musics Updater", &music_thread, 65, 0x10000, 0, 0x40000, NULL);
		sceKernelStartThread(musics_updater, 0, NULL);
		inited = 1;
	}
}
	
void audio_player_play(char *path, int loop) {
	char fname[512];
	sprintf(fname, "res/raw/mp3_%s.mp3", path);
	unz_file_info file_info;
	unzFile apk_file = unzOpen(apk_path);
	unzLocateFile(apk_file, fname, NULL);
	unzGetCurrentFileInfo(apk_file, &file_info, NULL, 0, NULL, 0, NULL, 0);
	unzOpenCurrentFile(apk_file);
	mp3_size = file_info.uncompressed_size;
	mp3_buffer = (uint8_t *)vglMalloc(mp3_size);
	unzReadCurrentFile(apk_file, mp3_buffer, mp3_size);
	unzCloseCurrentFile(apk_file);
	unzClose(apk_file);
		
	audio_player_init();
	h = mpg123_new(NULL, NULL);
	mp3_buffer_pos = 0;
	mpg123_replace_reader_handle(h, mp3_read, mp3_seek, mp3_close);
	mpg123_open_handle(h, NULL);
	mpg123_scan(h);
	long srate; int channels, dummy;
	mpg123_getformat(h, &srate, &channels, &dummy);
	mpg123_format_none(h);
	mpg123_format(h, srate, channels, MPG123_ENC_SIGNED_16);
	is_stereo = channels > 1;
	samplerate = srate;
	alGenBuffers(2, al_buffers);
	if (al_source == 0xDEADBEEF)
		alGenSources(1, &al_source);
	if (!audio_buffer)
		audio_buffer = (uint8_t *)vglMalloc(CHUNK_SIZE * 2);
	sceClibMemset(audio_buffer, 0, 4);
	alBufferData(al_buffers[0], channels > 1 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16, audio_buffer, 4, srate);
	alBufferData(al_buffers[1], channels > 1 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16, audio_buffer, 4, srate);
	alSourcef(al_source, AL_PITCH, 1);
	alSourcef(al_source, AL_GAIN, 1);
	alSource3f(al_source, AL_POSITION, 0, 0, 0);
	alSource3f(al_source, AL_VELOCITY, 0, 0, 0);
	alSourcei(al_source, AL_LOOPING, AL_FALSE);
	alSourcei(al_source, AL_SOURCE_RELATIVE, AL_TRUE);
	alSourceQueueBuffers(al_source, 2, al_buffers);
	alSourcePlay(al_source);
	is_looping = loop;
	sceKernelWaitSema(critical_section, 1, NULL);
	is_playing = 1;
	sceKernelSignalSema(critical_section, 1);
}
		
void audio_player_stop() {
	if (inited) {
		sceKernelWaitSema(critical_section, 1, NULL);
		is_playing = 0;
		sceKernelSignalSema(critical_section, 1);
		alSourceStop(al_source);
		ALuint buffers[2];
		alSourceUnqueueBuffers(al_source, 2, buffers);
		alDeleteBuffers(2, al_buffers);
		mpg123_close(h);
		mpg123_delete(h);
	}
}

void audio_player_pause() {
	if (inited) {
		sceKernelWaitSema(critical_section, 1, NULL);
		if (is_playing) {
			is_playing = 0;
			alSourceStop(al_source);
		}
		sceKernelSignalSema(critical_section, 1);
	}
}

void audio_player_resume() {
	if (inited) {
		sceKernelWaitSema(critical_section, 1, NULL);
		if (!is_playing) {
			is_playing = 1;
			alSourcePlay(al_source);
		}
		sceKernelSignalSema(critical_section, 1);
	}
}

int audio_player_is_playing() {
	int r = 0;
	if (inited) {
		sceKernelWaitSema(critical_section, 1, NULL);
		r = is_playing;
		sceKernelSignalSema(critical_section, 1);
	}
	return r;
}
