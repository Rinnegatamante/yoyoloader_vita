#include <vitasdk.h>
#include <vitaGL.h>
#include <imgui_vita.h>
#include <curl/curl.h>
#include <stdio.h>
#include <string>
#include "../loader/zip.h"
#include "../loader/unzip.h"
#include "strings.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_ONLY_PNG
#include "../loader/stb_image.h"

#define DATA_PATH "ux0:data/gms"
#define LAUNCH_FILE_PATH DATA_PATH "/launch.txt"
#define TEMP_DOWNLOAD_NAME "ux0:data/yyl.tmp"
#define LOG_DOWNLOAD_NAME "ux0:data/gms/yyl.log"

#define VERSION "0.1"
#define FUNC_TO_NAME(x) #x
#define stringify(x) FUNC_TO_NAME(x)
#define MIN(x, y) (x) < (y) ? (x) : (y)

#define NUM_OPTIONS 12
#define NUM_DB_CHUNKS 3
#define MEM_BUFFER_SIZE (32 * 1024 * 1024)
#define FILTER_MODES_NUM 6

int init_interactive_msg_dialog(const char *msg) {
	SceMsgDialogUserMessageParam msg_param;
	memset(&msg_param, 0, sizeof(msg_param));
	msg_param.buttonType = SCE_MSG_DIALOG_BUTTON_TYPE_YESNO;
	msg_param.msg = (SceChar8 *)msg;

	SceMsgDialogParam param;
	sceMsgDialogParamInit(&param);
	_sceCommonDialogSetMagicNumber(&param.commonParam);
	param.mode = SCE_MSG_DIALOG_MODE_USER_MSG;
	param.userMsgParam = &msg_param;

	return sceMsgDialogInit(&param);
}

enum {
	SCE_SYSTEM_PARAM_LANG_UKRAINIAN = 20,
};

extern void video_open(const char *path);
extern GLuint video_get_frame(int *width, int *height);
extern void video_close();

extern "C" {
	int debugPrintf(const char *fmt, ...) {return 0;}
	void fatal_error(const char *fmt, ...);
};

void DrawDownloaderDialog(int index, float downloaded_bytes, float total_bytes, char *text, int passes, bool self_contained);
void DrawExtractorDialog(int index, float file_extracted_bytes, float extracted_bytes, float file_total_bytes, float total_bytes, char *filename, int num_files);
void DrawChangeListDialog(FILE *f);
void DrawExtrapolatorDialog(char *game);

// Auto updater passes
enum {
	UPDATER_CHECK_UPDATES,
	UPDATER_DOWNLOAD_CHANGELIST,
	UPDATER_DOWNLOAD_UPDATE,
	NUM_UPDATE_PASSES
};

int _newlib_heap_size_user = 256 * 1024 * 1024;

static CURL *curl_handle = NULL;
static volatile uint64_t total_bytes = 0xFFFFFFFF;
static volatile uint64_t downloaded_bytes = 0;
static volatile uint8_t downloader_pass = 1;
static bool anim_download_request = false;
static bool needs_extended_font = false;
uint8_t *generic_mem_buffer = nullptr;
static FILE *fh;
char *bytes_string;
SceUID banner_thid;
int console_language;

struct CompatibilityList {
	char name[128];
	bool playable;
	bool ingame_plus;
	bool ingame_low;
	bool crash;
	bool slow;
	CompatibilityList *next;
};

struct GameSelection {
	char name[128];
	char game_id[128];
	float size;
	bool bilinear;
	bool gles1;
	bool skip_splash;
	bool compress_textures;
	bool fake_win_mode;
	bool debug_mode;
	bool debug_shaders;
	bool mem_extended;
	bool newlib_extended;
	bool video_support;
	bool has_net;
	bool squeeze_mem;
	CompatibilityList *status;
	GameSelection *next;
};

static CompatibilityList *comp = nullptr;
static GameSelection *old_hovered = NULL;

int filter_idx = 0;
const char *filter_modes[] = {
	lang_strings[STR_NO_FILTER],
	lang_strings[STR_PLAYABLE],
	lang_strings[STR_INGAME_PLUS],
	lang_strings[STR_INGAME_MINUS],
	lang_strings[STR_CRASH],
	lang_strings[STR_NO_TAGS]
};

// Filter modes enum
enum {
	FILTER_DISABLED,
	FILTER_PLAYABLE,
	FILTER_INGAME_PLUS,
	FILTER_INGAME_MINUS,
	FILTER_CRASH,
	FILTER_NO_TAGS
};

bool filterGames(GameSelection *p) {
	if (!p->status) return filter_idx != FILTER_NO_TAGS;
	else {
		if (filter_idx == FILTER_NO_TAGS) return true;
		else if ((!p->status->playable && filter_idx == FILTER_PLAYABLE) ||
			(!p->status->ingame_plus && filter_idx == FILTER_INGAME_PLUS) ||
			(!p->status->ingame_low && filter_idx == FILTER_INGAME_MINUS) ||
			(!p->status->crash && filter_idx == FILTER_CRASH)) {
			return true;
		}
	}
	return false;
}

void AppendCompatibilityDatabase(const char *file) {
	FILE *f = fopen(file, "rb");
	if (f) {
		fseek(f, 0, SEEK_END);
		uint64_t len = ftell(f);
		fseek(f, 0, SEEK_SET);
		char *buffer = (char*)malloc(len + 1);
		fread(buffer, 1, len, f);
		buffer[len] = 0;
		char *ptr = buffer;
		char *end, *end2;
		do {
			ptr = strstr(ptr, "\"title\":");
			if (ptr) {
				ptr += 10;
				end2 = strstr(ptr, "\"");
				ptr = strstr(ptr, "[") + 1;
				if (ptr && ptr < end2) {
					end = strstr(ptr, "]");
					CompatibilityList *node = (CompatibilityList*)malloc(sizeof(CompatibilityList));
				
					// Extracting title
					sceClibMemcpy(node->name, ptr, end - ptr);
					node->name[end - ptr] = 0;
				
					// Extracting tags
					bool perform_slow_check = true;
					ptr += 1000; // Let's skip some data to improve performances
					ptr = strstr(ptr, "\"labels\":");
					ptr = strstr(ptr + 150, "\"name\":");
					ptr += 9;
					if (ptr[0] == 'P') {
						node->playable = true;
						node->ingame_low = false;
						node->ingame_plus = false;
						node->crash = false;
					} else if (ptr[0] == 'C') {
						node->playable = false;
						node->ingame_low = false;
						node->ingame_plus = false;
						node->slow = false;
						node->crash = true;
						perform_slow_check = false;
					} else {
						node->playable = false;
						node->crash = false;
						end = strstr(ptr, "\"");
						if ((end - ptr) == 13) {
							node->ingame_plus = true;
							node->ingame_low = false;
						}else {
							node->ingame_low = true;
							node->ingame_plus = false;
						}
					}
					ptr += 120; // Let's skip some data to improve performances
					if (perform_slow_check) {
						end = ptr;
						ptr = strstr(ptr, "]");
						if ((ptr - end) > 200) node->slow = true;
						else node->slow = false;
					}
				
					ptr += 350; // Let's skip some data to improve performances
					node->next = comp;
					comp = node;
				} else {
					ptr = end2 + 1500;
				}
			}
		} while (ptr);
		fclose(f);
		free(buffer);
	}
}

CompatibilityList *SearchForCompatibilityData(const char *name) {
	CompatibilityList *node = comp;
	char tmp[128];
	sprintf(tmp, name);
	while (node) {
		if (strcmp(node->name, tmp) == 0) return node;
		node = node->next;
	}
	return nullptr;
}

extern "C" {
void *__wrap_memcpy(void *dest, const void *src, size_t n) {
	return sceClibMemcpy(dest, src, n);
}

void *__wrap_memmove(void *dest, const void *src, size_t n) {
	return sceClibMemmove(dest, src, n);
}

void *__wrap_memset(void *s, int c, size_t n) {
	return sceClibMemset(s, c, n);
}
}

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *stream)
{
	uint8_t *dst = &generic_mem_buffer[downloaded_bytes];
	downloaded_bytes += nmemb;
	if (total_bytes < downloaded_bytes) total_bytes = downloaded_bytes;
	sceClibMemcpy(dst, ptr, nmemb);
	return nmemb;
}

static size_t header_cb(char *buffer, size_t size, size_t nitems, void *userdata)
{
	char *ptr = strcasestr(buffer, "Content-Length");
	if (ptr != NULL) sscanf(ptr, "Content-Length: %llu", &total_bytes);
	return nitems;
}

static void startDownload(const char *url)
{
	curl_easy_reset(curl_handle);
	curl_easy_setopt(curl_handle, CURLOPT_URL, url);
	curl_easy_setopt(curl_handle, CURLOPT_HTTPGET, 1L);
	curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.36");
	curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl_handle, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
	curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, bytes_string); // Dummy
	curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb);
	curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, bytes_string); // Dummy*/
	curl_easy_setopt(curl_handle, CURLOPT_RESUME_FROM, downloaded_bytes);
	curl_easy_setopt(curl_handle, CURLOPT_BUFFERSIZE, 524288);
	struct curl_slist *headerchunk = NULL;
	headerchunk = curl_slist_append(headerchunk, "Accept: */*");
	headerchunk = curl_slist_append(headerchunk, "Content-Type: application/json");
	headerchunk = curl_slist_append(headerchunk, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.36");
	headerchunk = curl_slist_append(headerchunk, "Content-Length: 0");
	curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headerchunk);
	curl_easy_perform(curl_handle);
}

const char *sort_modes_str[] = {
	lang_strings[STR_NAME_ASC],
	lang_strings[STR_NAME_DESC],
};
int sort_idx = 0;
int old_sort_idx = -1;

void loadConfig(GameSelection *g) {
	char configFile[512];
	char buffer[30];
	int value;
	
	sprintf(configFile, "%s/%s/yyl.cfg", DATA_PATH, g->name);
	FILE *config = fopen(configFile, "r");

	if (config) {
		while (EOF != fscanf(config, "%[^=]=%d\n", buffer, &value)) {
			if (strcmp("forceGLES1", buffer) == 0) g->gles1 = (bool)value;
			else if (strcmp("forceBilinear", buffer) == 0) g->bilinear = (bool)value;
			else if (strcmp("winMode", buffer) == 0) g->fake_win_mode = (bool)value;
			else if (strcmp("debugShaders", buffer) == 0) g->debug_shaders = (bool)value;
			else if (strcmp("compressTextures", buffer) == 0) g->compress_textures = (bool)value;
			else if (strcmp("debugMode", buffer) == 0) g->debug_mode = (bool)value;
			else if (strcmp("noSplash", buffer) == 0) g->skip_splash = (bool)value;
			else if (strcmp("maximizeMem", buffer) == 0) g->mem_extended = (bool)value;
			else if (strcmp("maximizeNewlib", buffer) == 0) g->newlib_extended = (bool)value;
			else if (strcmp("videoSupport", buffer) == 0) g->video_support = (bool)value;
			else if (strcmp("netSupport", buffer) == 0) g->has_net = (bool)value;
			else if (strcmp("squeezeMem", buffer) == 0) g->squeeze_mem = (bool)value;
		}
		fclose(config);
	} else {
		sceClibMemset(&g->bilinear, 0, sizeof(bool) * NUM_OPTIONS);
	}
}

void loadSelectorConfig() {
	char configFile[512];
	char buffer[30];
	int value;
	
	sprintf(configFile, "%s/shared/yyl.cfg", DATA_PATH);
	FILE *config = fopen(configFile, "r");

	if (config) {
		while (EOF != fscanf(config, "%[^=]=%d\n", buffer, &value)) {
			if (strcmp("sortMode", buffer) == 0) sort_idx = value;
			else if (strcmp("language", buffer) == 0) console_language = value;
		}
		fclose(config);
	}
}

void swap_games(GameSelection *a, GameSelection *b) {
	GameSelection tmp;
	
	// Swapping everything except next leaf pointer
	sceClibMemcpy(&tmp, a, sizeof(GameSelection) - 4);
	sceClibMemcpy(a, b, sizeof(GameSelection) - 4);
	sceClibMemcpy(b, &tmp, sizeof(GameSelection) - 4);
}

void sort_gamelist(GameSelection *start) { 
	// Checking for empty list
	if (start == NULL) 
		return; 
	
	int swapped; 
	GameSelection *ptr1; 
	GameSelection *lptr = NULL; 
  
	do { 
		swapped = 0; 
		ptr1 = start; 
  
		while (ptr1->next != lptr && ptr1->next) {
			switch (sort_idx) {
			case 0:
				if (strcasecmp(ptr1->name,ptr1->next->name) < 0) {
					swap_games(ptr1, ptr1->next); 
					swapped = 1; 
				}
				break;
			case 1:
				if (strcasecmp(ptr1->name,ptr1->next->name) > 0) {
					swap_games(ptr1, ptr1->next); 
					swapped = 1; 
				}
				break;
			default:
				break;
			}
			ptr1 = ptr1->next; 
		} 
		lptr = ptr1; 
	} while (swapped); 
}

char *launch_item = nullptr;

GameSelection *games = nullptr;

char ver_str[64];
char settings_str[512];
float ver_len = 0.0f;
bool calculate_ver_len = true;
bool is_config_invoked = false;
uint32_t oldpad;
bool extracting = false;

volatile int cur_idx = 0;
volatile int tot_idx = -1;
volatile float saved_size = -1.0f;
int optimizer_thread(unsigned int argc, void *argv) {
	char *game = (char *)argv;
	char apk_path[256], tmp_path[256], fname[512];
	sprintf(apk_path, "ux0:data/gms/%s/game.apk", game);
	sprintf(tmp_path, "ux0:data/gms/%s/game.tmp", game);
	
	SceIoStat stat;
	sceIoGetstat(apk_path, &stat);
	uint32_t orig_size = stat.st_size;
	
	unz_global_info global_info;
	unz_file_info file_info;
	unzFile src_file = unzOpen(apk_path);
	unzGetGlobalInfo(src_file, &global_info);
	unzGoToFirstFile(src_file);
	zipFile dst_file = zipOpen(tmp_path, APPEND_STATUS_CREATE);
	cur_idx = 0;
	tot_idx = global_info.number_entry;
	for (uint32_t zip_idx = 0; zip_idx < global_info.number_entry; ++zip_idx) {
		unzGetCurrentFileInfo(src_file, &file_info, fname, 512, NULL, 0, NULL, 0);
		if ((strstr(fname, "assets/") && fname[strlen(fname) - 1] != '/') || !strcmp(fname, "lib/armeabi-v7a/libyoyo.so")) {
			if (strstr(fname, ".ogg") || strstr(fname, ".mp4")) {
				zipOpenNewFileInZip(dst_file, fname, NULL, NULL, 0, NULL, 0, NULL, 0, Z_NO_COMPRESSION);
			} else {
				/*
				 * HACK: There's some issue in zlib seemingly that makes zip_fread to fail after some reads on some specific files.
				 * if they are compressed. Until a proper solution is found, we hack those files to be only stored to bypass the issue.
				 */
				bool needs_hack = false;
				const char *blacklist[] = {
					"english.ini", // AM2R
					"yugothib.ttf" // JackQuest
				};
				for (int i = 0; i < (sizeof(blacklist) / sizeof(blacklist[0])); i++) {
					if (strstr(fname, blacklist[i])) {
						zipOpenNewFileInZip(dst_file, fname, NULL, NULL, 0, NULL, 0, NULL, 0, Z_NO_COMPRESSION);
						needs_hack = true;
						break;
					}
				}
				if (!needs_hack)
					zipOpenNewFileInZip(dst_file, fname, NULL, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_DEFAULT_COMPRESSION);
			}
			uint32_t executed_bytes = 0;
			unzOpenCurrentFile(src_file);
			while (executed_bytes < file_info.uncompressed_size) {
				uint32_t read_size = (file_info.uncompressed_size - executed_bytes) > MEM_BUFFER_SIZE ? MEM_BUFFER_SIZE : (file_info.uncompressed_size - executed_bytes);
				unzReadCurrentFile(src_file, generic_mem_buffer, read_size);
				zipWriteInFileInZip(dst_file, generic_mem_buffer, read_size);
				executed_bytes += read_size;
			}
			unzCloseCurrentFile(src_file);
			zipCloseFileInZip(dst_file);
		}
		unzGoToNextFile(src_file);
		cur_idx++;
	}
	unzClose(src_file);
	zipClose(dst_file, NULL);
	sceIoRemove(apk_path);
	sceIoRename(tmp_path, apk_path);
	
	sceIoGetstat(apk_path, &stat);
	saved_size = (float)(orig_size - stat.st_size) / (1024.0f * 1024.0f);
	return sceKernelExitDeleteThread(0);
}

static int compatListThread(unsigned int args, void *arg) {
	char url[512], dbname[64];
	curl_handle = curl_easy_init();
	for (int i = 1; i <= NUM_DB_CHUNKS; i++) {
		downloader_pass = i;
		sprintf(dbname, "ux0:data/gms/shared/db%d.json", i);
		sprintf(url, "https://api.github.com/repos/Rinnegatamante/YoYo-Loader-Vita-Compatibility/issues?state=open&page=%d&per_page=100", i);
		downloaded_bytes = 0;

		// FIXME: Workaround since GitHub Api does not set Content-Length
		SceIoStat stat;
		sceIoGetstat(dbname, &stat);
		total_bytes = stat.st_size;

		startDownload(url);

		if (downloaded_bytes > 12 * 1024) {
			fh = fopen(dbname, "wb");
			fwrite(generic_mem_buffer, 1, downloaded_bytes, fh);
			fclose(fh);
		}
		downloaded_bytes = total_bytes;
	}
	curl_easy_cleanup(curl_handle);
	sceKernelExitDeleteThread(0);
	return 0;
}

void OptimizeApk(char *game) {
	tot_idx = -1;
	saved_size = -1.0f;
	extracting = true;
	SceUID optimizer_thid = sceKernelCreateThread("Optimizer Thread", &optimizer_thread, 0x10000100, 0x100000, 0, 0, NULL);
	sceKernelStartThread(optimizer_thid, strlen(game) + 1, game);
}

static int updaterThread(unsigned int args, void *arg) {
	bool update_detected = false;
	char url[512];
	curl_handle = curl_easy_init();
	for (int i = UPDATER_CHECK_UPDATES; i < NUM_UPDATE_PASSES; i++) {
		downloader_pass = i;
#ifdef STABLE_BUILD
		if (i == UPDATER_CHECK_UPDATES) sprintf(url, "https://api.github.com/repos/Rinnegatamante/yoyoloader_vita/releases/tags/Stable");
#else
		if (i == UPDATER_CHECK_UPDATES) sprintf(url, "https://api.github.com/repos/Rinnegatamante/yoyoloader_vita/releases/tags/Nightly");
#endif
		else if (!update_detected) break;
		downloaded_bytes = 0;

		// FIXME: Workaround since GitHub Api does not set Content-Length
		total_bytes = i == UPDATER_DOWNLOAD_UPDATE ? 2 * 1024 * 1024 : 20 * 1024; /* 2 MB / 20 KB */
		
		startDownload(url);
		if (downloaded_bytes > 4 * 1024) {
			if (i == UPDATER_CHECK_UPDATES) {
				char target_commit[7];
				snprintf(target_commit, 6, strstr((char*)generic_mem_buffer, "body") + 10);
				if (strncmp(target_commit, stringify(GIT_VERSION), 5)) {
					sprintf(url, "https://api.github.com/repos/Rinnegatamante/yoyoloader_vita/compare/%s...%s", stringify(GIT_VERSION), target_commit);
					update_detected = true;
				}
			} else if (i == UPDATER_DOWNLOAD_CHANGELIST) {
				fh = fopen(LOG_DOWNLOAD_NAME, "wb");
				fwrite((const void*)generic_mem_buffer, 1, downloaded_bytes, fh);
				fclose(fh);
#ifdef STABLE_BUILD
				sprintf(url, "https://github.com/Rinnegatamante/yoyoloader_vita/releases/download/Stable/YoYoLoader.vpk");
#else
				sprintf(url, "https://github.com/Rinnegatamante/yoyoloader_vita/releases/download/Nightly/YoYoLoader.vpk");
#endif
			}
		}
	}
	if (update_detected) {
		if (downloaded_bytes > 12 * 1024) {
			fh = fopen(TEMP_DOWNLOAD_NAME, "wb");
			fwrite((const void*)generic_mem_buffer, 1, downloaded_bytes, fh);
			fclose(fh);
		}
	}
	curl_easy_cleanup(curl_handle);
	return sceKernelExitDeleteThread(0);
}

static int bannerThread(unsigned int args, void *arg) {
	char url[512];
	curl_handle = curl_easy_init();
	sprintf(url, "https://github.com/Rinnegatamante/yoyoloader_vita/releases/download/Nightly/banners.zip");
	downloaded_bytes = 0;
	total_bytes = 20 * 1024; /* 20 KB */
	startDownload(url);
	if (downloaded_bytes > 4 * 1024) {
		fh = fopen(TEMP_DOWNLOAD_NAME, "wb");
		fwrite((const void*)generic_mem_buffer, 1, downloaded_bytes, fh);
		fclose(fh);
	}
	curl_easy_cleanup(curl_handle);
	return sceKernelExitDeleteThread(0);
}

static int fontThread(unsigned int args, void *arg) {
	char url[512];
	curl_handle = curl_easy_init();
	sprintf(url, "https://github.com/Rinnegatamante/yoyoloader_vita/blob/main/Roboto_ext.ttf?raw=true");
	downloaded_bytes = 0;
	total_bytes = 20 * 1024; /* 20 KB */
	startDownload(url);
	int response_code;
	curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
	
	if (response_code == 200) {
		fh = fopen("ux0:data/gms/shared/Roboto_ext.ttf", "wb");
		fwrite((const void*)generic_mem_buffer, 1, downloaded_bytes, fh);
		fclose(fh);
	}
	curl_easy_cleanup(curl_handle);
	return sceKernelExitDeleteThread(0);
}

static int animBannerThread(unsigned int args, void *arg) {
	char *argv = (char *)arg;
	char url[512], final_url[512] = "";
	curl_handle = curl_easy_init();
	sprintf(url, "https://github.com/Rinnegatamante/yoyoloader_vita_trailers/blob/main/trailers/%s.mp4?raw=true", argv);
	char *space = strstr(url, " ");
	char *s = url;
	while (space) {
		space[0] = 0;
		sprintf(final_url, "%s%s%%20", final_url, s);
		space[0] = ' ';
		s = space + 1;
		space = strstr(s, " ");
	}
	sprintf(final_url, "%s%s", final_url, s);
	downloaded_bytes = 0;
	total_bytes = 20 * 1024; /* 20 KB */
	startDownload(final_url);
	int response_code;
	curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
	
	if (response_code == 200) {
		sprintf(url, "ux0:data/gms/shared/anim/%s.mp4", argv);
		fh = fopen(url, "wb");
		fwrite((const void*)generic_mem_buffer, 1, downloaded_bytes, fh);
		fclose(fh);
	}
	curl_easy_cleanup(curl_handle);
	return sceKernelExitDeleteThread(0);
}

static char fname[512], ext_fname[512], read_buffer[8192];

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

void extract_file(char *file, char *dir) {
	unz_global_info global_info;
	unz_file_info file_info;
	unzFile zipfile = unzOpen(file);
	unzGetGlobalInfo(zipfile, &global_info);
	unzGoToFirstFile(zipfile);
	uint64_t total_extracted_bytes = 0;
	uint64_t curr_extracted_bytes = 0;
	uint64_t curr_file_bytes = 0;
	int num_files = global_info.number_entry;
	for (int zip_idx = 0; zip_idx < num_files; ++zip_idx) {
		unzGetCurrentFileInfo(zipfile, &file_info, fname, 512, NULL, 0, NULL, 0);
		total_extracted_bytes += file_info.uncompressed_size;
		if ((zip_idx + 1) < num_files) unzGoToNextFile(zipfile);
	}
	unzGoToFirstFile(zipfile);
	for (int zip_idx = 0; zip_idx < num_files; ++zip_idx) {
		unzGetCurrentFileInfo(zipfile, &file_info, fname, 512, NULL, 0, NULL, 0);
		sprintf(ext_fname, "%s%s", dir, fname); 
		const size_t filename_length = strlen(ext_fname);
		if (ext_fname[filename_length - 1] != '/') {
			curr_file_bytes = 0;
			unzOpenCurrentFile(zipfile);
			recursive_mkdir(ext_fname);
			FILE *f = fopen(ext_fname, "wb");
			while (curr_file_bytes < file_info.uncompressed_size) {
				int rbytes = unzReadCurrentFile(zipfile, read_buffer, 8192);
				if (rbytes > 0) {
					fwrite(read_buffer, 1, rbytes, f);
					curr_extracted_bytes += rbytes;
					curr_file_bytes += rbytes;
				}
				DrawExtractorDialog(zip_idx + 1, curr_file_bytes, curr_extracted_bytes, file_info.uncompressed_size, total_extracted_bytes, fname, num_files);
			}
			fclose(f);
			unzCloseCurrentFile(zipfile);
		}
		if ((zip_idx + 1) < num_files) unzGoToNextFile(zipfile);
	}
	unzClose(zipfile);
	ImGui::GetIO().MouseDrawCursor = true;
}

int file_exists(const char *path) {
	SceIoStat stat;
	return sceIoGetstat(path, &stat) >= 0;
}

static bool is_downloading_banners = false;
static bool is_downloading_anim_banner = false;
static bool has_preview_icon = false;
static int preview_width, preview_height, preview_x, preview_y;
GLuint preview_icon = 0;
static int animated_preview_delayer = 0;
#define ANIMATED_PREVIEW_DELAY 60
#define PREVIEW_PADDING 6
#define PREVIEW_HEIGHT 160.0f
#define PREVIEW_WIDTH  394.0f
void LoadAnimatedPreview(GameSelection *game) {
	if (animated_preview_delayer < ANIMATED_PREVIEW_DELAY) {
		if (animated_preview_delayer == 0)
			video_close();
		animated_preview_delayer++;
	} else if (animated_preview_delayer == ANIMATED_PREVIEW_DELAY) {
		animated_preview_delayer++;
		
		char banner_path[256];
	
		sprintf(banner_path, "ux0:data/gms/shared/anim/%s.mp4", game->game_id);
		FILE *f = fopen(banner_path, "rb");
		if (f) {
			fclose(f);
			video_open(banner_path);
		} else if (anim_download_request) {
			is_downloading_anim_banner = true;
			banner_thid = sceKernelCreateThread("Anim Banners Downloader", &animBannerThread, 0x10000100, 0x100000, 0, 0, NULL);
			sceKernelStartThread(banner_thid, strlen(game->game_id) + 1, game->game_id);
		}
		
		anim_download_request = false;
	}
}

bool LoadPreview(GameSelection *game) {
	if (old_hovered == game)
		return has_preview_icon;
	old_hovered = game;

	bool ret = false;
	animated_preview_delayer = 0;
	
	char banner_path[256];
	sprintf(banner_path, "ux0:data/gms/shared/banners/%s.png", game->game_id);
	uint8_t *icon_data = stbi_load(banner_path, &preview_width, &preview_height, NULL, 4);
	if (icon_data) {
		if (!preview_icon) glGenTextures(1, &preview_icon);
		glBindTexture(GL_TEXTURE_2D, preview_icon);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, preview_width, preview_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, icon_data);
		float scale = MIN(PREVIEW_WIDTH / (float)preview_width, PREVIEW_HEIGHT / (float)preview_height);
		preview_width = scale * (float)preview_width;
		preview_height = scale * (float)preview_height;
		preview_x = (PREVIEW_WIDTH - preview_width) / 2;
		preview_y = (PREVIEW_HEIGHT - preview_height) / 2;
		free(icon_data);
		return true;
	}
	
	return false;
}

void setTranslation(int idx) {
	char langFile[LANG_STR_SIZE * 2];
	char identifier[LANG_ID_SIZE], buffer[LANG_STR_SIZE];
	
	switch (idx) {
	case SCE_SYSTEM_PARAM_LANG_ITALIAN:
		sprintf(langFile, "app0:lang/Italian.ini");
		break;
	case SCE_SYSTEM_PARAM_LANG_SPANISH:
		sprintf(langFile, "app0:lang/Spanish.ini");
		break;
	case SCE_SYSTEM_PARAM_LANG_GERMAN:
		sprintf(langFile, "app0:lang/German.ini");
		break;
	case SCE_SYSTEM_PARAM_LANG_PORTUGUESE_PT: // FIXME: Temporarily using Brazilian one
	case SCE_SYSTEM_PARAM_LANG_PORTUGUESE_BR:
		sprintf(langFile, "app0:lang/Portuguese_BR.ini");
		break;
	case SCE_SYSTEM_PARAM_LANG_RUSSIAN:
		sprintf(langFile, "app0:lang/Russian.ini");
		break;
	case SCE_SYSTEM_PARAM_LANG_JAPANESE:
		sprintf(langFile, "app0:lang/Japanese.ini");
		needs_extended_font = true;
		break;
	case SCE_SYSTEM_PARAM_LANG_UKRAINIAN:
		sprintf(langFile, "app0:lang/Ukrainian.ini");
		break;
	default:
		sprintf(langFile, "app0:lang/English.ini");
		break;
	}
	
	FILE *config = fopen(langFile, "r");
	if (config)
	{
		while (EOF != fscanf(config, "%[^=]=%[^\n]\n", identifier, buffer))
		{
			for (int i = 0; i < LANG_STRINGS_NUM; i++) {
				if (strcmp(lang_identifiers[i], identifier) == 0) {
					char *newline = nullptr, *p = buffer;
					while (newline = strstr(p, "\\n")) {
						newline[0] = '\n';
						int len = strlen(&newline[2]);
						memmove(&newline[1], &newline[2], len);
						newline[len + 1] = 0;
						p++;
					}
					strcpy(lang_strings[i], buffer);
				}
			}
		}
		fclose(config);
	}
}

void setImguiTheme() {
	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	ImVec4 col_area = ImVec4(0.047f, 0.169f, 0.059f, 0.44f);
	ImVec4 col_main = ImVec4(0.2f, 0.627f, 0.169f, 0.86f);
	style.Colors[ImGuiCol_ChildWindowBg]         = ImVec4(col_area.x, col_area.y, col_area.z, 0.00f);
	style.Colors[ImGuiCol_FrameBg]               = ImVec4(col_area.x, col_area.y, col_area.z, 1.00f);
	style.Colors[ImGuiCol_FrameBgHovered]        = ImVec4(col_main.x, col_main.y, col_main.z, 0.68f);
	style.Colors[ImGuiCol_FrameBgActive]         = ImVec4(col_main.x, col_main.y, col_main.z, 1.00f);
	style.Colors[ImGuiCol_TitleBg]               = ImVec4(col_main.x, col_main.y, col_main.z, 0.45f);
	style.Colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(col_main.x, col_main.y, col_main.z, 0.35f);
	style.Colors[ImGuiCol_TitleBgActive]         = ImVec4(col_main.x, col_main.y, col_main.z, 0.78f);
	style.Colors[ImGuiCol_MenuBarBg]             = ImVec4(col_area.x, col_area.y, col_area.z, 0.57f);
	style.Colors[ImGuiCol_ScrollbarBg]           = ImVec4(col_area.x, col_area.y, col_area.z, 1.00f);
	style.Colors[ImGuiCol_ScrollbarGrab]         = ImVec4(col_main.x, col_main.y, col_main.z, 0.31f);
	style.Colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(col_main.x, col_main.y, col_main.z, 0.78f);
	style.Colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(col_main.x, col_main.y, col_main.z, 1.00f);
	style.Colors[ImGuiCol_CheckMark]             = ImVec4(col_main.x, col_main.y, col_main.z, 0.80f);
	style.Colors[ImGuiCol_SliderGrab]            = ImVec4(col_main.x, col_main.y, col_main.z, 0.24f);
	style.Colors[ImGuiCol_SliderGrabActive]      = ImVec4(col_main.x, col_main.y, col_main.z, 1.00f);
	style.Colors[ImGuiCol_Button]                = ImVec4(col_main.x, col_main.y, col_main.z, 0.44f);
	style.Colors[ImGuiCol_ButtonHovered]         = ImVec4(col_main.x, col_main.y, col_main.z, 0.86f);
	style.Colors[ImGuiCol_ButtonActive]          = ImVec4(col_main.x, col_main.y, col_main.z, 1.00f);
	style.Colors[ImGuiCol_Header]                = ImVec4(col_main.x, col_main.y, col_main.z, 0.76f);
	style.Colors[ImGuiCol_HeaderHovered]         = ImVec4(col_main.x, col_main.y, col_main.z, 0.86f);
	style.Colors[ImGuiCol_HeaderActive]          = ImVec4(col_main.x, col_main.y, col_main.z, 1.00f);
	style.Colors[ImGuiCol_ResizeGrip]            = ImVec4(col_main.x, col_main.y, col_main.z, 0.20f);
	style.Colors[ImGuiCol_ResizeGripHovered]     = ImVec4(col_main.x, col_main.y, col_main.z, 0.78f);
	style.Colors[ImGuiCol_ResizeGripActive]      = ImVec4(col_main.x, col_main.y, col_main.z, 1.00f);
	style.Colors[ImGuiCol_PlotLinesHovered]      = ImVec4(col_main.x, col_main.y, col_main.z, 1.00f);
	style.Colors[ImGuiCol_PlotHistogramHovered]  = ImVec4(col_main.x, col_main.y, col_main.z, 1.00f);
	style.Colors[ImGuiCol_TextSelectedBg]        = ImVec4(col_main.x, col_main.y, col_main.z, 0.43f);
	style.Colors[ImGuiCol_NavHighlight]          = ImVec4(col_main.x, col_main.y, col_main.z, 0.86f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::GetIO().MouseDrawCursor = false;
}

int main(int argc, char *argv[]) {
	sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);
	
	sceIoMkdir("ux0:data/gms", 0777);
	sceIoMkdir("ux0:data/gms/shared", 0777);
	sceIoMkdir("ux0:data/gms/shared/anim", 0777);
	
	if (!file_exists("ur0:/data/libshacccg.suprx") && !file_exists("ur0:/data/external/libshacccg.suprx"))
		fatal_error(lang_strings[STR_SHACCCG_ERROR]);
		
	// Initializing sceAppUtil
	SceAppUtilInitParam appUtilParam;
	SceAppUtilBootParam appUtilBootParam;
	memset(&appUtilParam, 0, sizeof(SceAppUtilInitParam));
	memset(&appUtilBootParam, 0, sizeof(SceAppUtilBootParam));
	sceAppUtilInit(&appUtilParam, &appUtilBootParam);
	sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &console_language);
	
	// Initializing sceCommonDialog
	SceCommonDialogConfigParam cmnDlgCfgParam;
	sceCommonDialogConfigParamInit(&cmnDlgCfgParam);
	cmnDlgCfgParam.language = (SceSystemParamLang)console_language;
	sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, (int *)&cmnDlgCfgParam.enterButtonAssign);
	sceCommonDialogSetConfigParam(&cmnDlgCfgParam);

	loadSelectorConfig();
	setTranslation(console_language);
	
	// Initializing sceNet
	generic_mem_buffer = (uint8_t*)malloc(MEM_BUFFER_SIZE);
	sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
	int ret = sceNetShowNetstat();
	SceNetInitParam initparam;
	if (ret == SCE_NET_ERROR_ENOTINIT) {
		initparam.memory = malloc(1024 * 1024);
		initparam.size = 1024 * 1024;
		initparam.flags = 0;
		sceNetInit(&initparam);
	}
	
	GameSelection *hovered = nullptr;
	vglInitExtended(0, 960, 544, 0x1800000, SCE_GXM_MULTISAMPLE_4X);
	ImGui::CreateContext();
	SceKernelThreadInfo info;
	info.size = sizeof(SceKernelThreadInfo);
	int res = 0;
	
	if (needs_extended_font) {
		static const ImWchar ranges[] = { // All languages with chinese included
			0x0020, 0x00FF, // Basic Latin + Latin Supplement
			0x0100, 0x024F, // Latin Extended
			0x0370, 0x03FF, // Greek
			0x0400, 0x052F, // Cyrillic + Cyrillic Supplement
			0x0590, 0x05FF, // Hebrew
			0x1E00, 0x1EFF, // Latin Extended Additional
			0x2000, 0x206F, // General Punctuation
			0x2DE0, 0x2DFF, // Cyrillic Extended-A
			0x3000, 0x30FF, // CJK Symbols and Punctuations, Hiragana, Katakana
			0x31F0, 0x31FF, // Katakana Phonetic Extensions
			0x4E00, 0x9FAF, // CJK Ideograms
			0xA640, 0xA69F, // Cyrillic Extended-B
			0xFF00, 0xFFEF, // Half-width characters
			0,
		};
		SceIoStat stat;
		if (sceIoGetstat("ux0:data/gms/shared/Roboto_ext.ttf", &stat)) {
			ImGui_ImplVitaGL_Init();
			setImguiTheme();
			SceUID thd = sceKernelCreateThread("Font Downloader", &fontThread, 0x10000100, 0x100000, 0, 0, NULL);
			sceKernelStartThread(thd, 0, NULL);
			do {
				DrawDownloaderDialog(1, downloaded_bytes, total_bytes, "Downloading required font, please wait...", 1, true);
				res = sceKernelGetThreadInfo(thd, &info);
			} while (info.status <= SCE_THREAD_DORMANT && res >= 0);
			ImGui::GetIO().Fonts->Clear();
			ImGui_ImplVitaGL_InvalidateDeviceObjects();
		} else
			ImGui_ImplVitaGL_Init();
		ImGui::GetIO().Fonts->AddFontFromFileTTF("ux0:/data/gms/shared/Roboto_ext.ttf", 14.0f, NULL, ranges);
	} else {
		ImGui_ImplVitaGL_Init();
		static const ImWchar ranges[] = { // All languages except chinese
			0x0020, 0x00FF, // Basic Latin + Latin Supplement
			0x0100, 0x024F, // Latin Extended
			0x0370, 0x03FF, // Greek
			0x0400, 0x052F, // Cyrillic + Cyrillic Supplement
			0x0590, 0x05FF, // Hebrew
			0x1E00, 0x1EFF, // Latin Extended Additional
			0x2DE0, 0x2DFF, // Cyrillic Extended-A
			0xA640, 0xA69F, // Cyrillic Extended-B
			0,
		};
		ImGui::GetIO().Fonts->AddFontFromFileTTF("app0:/Roboto.ttf", 14.0f, NULL, ranges);
	}
	
	ImGui_ImplVitaGL_TouchUsage(false);
	ImGui_ImplVitaGL_GamepadUsage(true);
	setImguiTheme();
	FILE *f;
	
	// Check if YoYo Loader has been launched with a custom bubble
	bool skip_updates_check = strstr(stringify(GIT_VERSION), "dirty") != nullptr;
	char boot_params[1024];
	sceAppMgrGetAppParam(boot_params);
	if (strstr(boot_params,"psgm:play") && strstr(boot_params, "&param=")) {
		skip_updates_check = true;
		launch_item = strstr(boot_params, "&param=") + 7;
	}
	
	// Checking for updates
	if (!skip_updates_check) {
		SceUID thd = sceKernelCreateThread("Auto Updater", &updaterThread, 0x10000100, 0x100000, 0, 0, NULL);
		sceKernelStartThread(thd, 0, NULL);
		do {
			if (downloader_pass == UPDATER_CHECK_UPDATES)
				DrawDownloaderDialog(downloader_pass + 1, downloaded_bytes, total_bytes, lang_strings[STR_SEARCH_UPDATES], NUM_UPDATE_PASSES, true);
			else if (downloader_pass == UPDATER_DOWNLOAD_CHANGELIST)
				DrawDownloaderDialog(downloader_pass + 1, downloaded_bytes, total_bytes, lang_strings[STR_CHANGELIST], NUM_UPDATE_PASSES, true);
			else
				DrawDownloaderDialog(downloader_pass + 1, downloaded_bytes, total_bytes, lang_strings[STR_UPDATE], NUM_UPDATE_PASSES, true);
			res = sceKernelGetThreadInfo(thd, &info);
		} while (info.status <= SCE_THREAD_DORMANT && res >= 0);
		total_bytes = 0xFFFFFFFF;
		downloaded_bytes = 0;
		downloader_pass = 1;
		
		// Found an update, extracting and installing it
		f = fopen(TEMP_DOWNLOAD_NAME, "r");
		if (f) {
			sceAppMgrUmount("app0:");
			fclose(f);
			extract_file(TEMP_DOWNLOAD_NAME, "ux0:app/YYOLOADER/");
			sceIoRemove(TEMP_DOWNLOAD_NAME);
			sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);
		}
	
		// Showing changelist
		f = fopen(LOG_DOWNLOAD_NAME, "r");
		if (f) {
			DrawChangeListDialog(f);
			sceIoRemove(LOG_DOWNLOAD_NAME);
		}
	}
	
	// Downloading compatibility list
	bool skip_compat_update = false;
	if  (!skip_compat_update) {
		SceUID thd = sceKernelCreateThread("Compat List Updater", &compatListThread, 0x10000100, 0x100000, 0, 0, NULL);
		sceKernelStartThread(thd, 0, NULL);
		do {
			DrawDownloaderDialog(downloader_pass, downloaded_bytes, total_bytes, lang_strings[STR_COMPAT_LIST], NUM_DB_CHUNKS, true);
			res = sceKernelGetThreadInfo(thd, &info);
		} while (info.status <= SCE_THREAD_DORMANT && res >= 0);
	}
	
	for (int i = 1; i <= NUM_DB_CHUNKS; i++) {
		char dbname[64];
		sprintf(dbname, "ux0:data/gms/shared/db%d.json", i);
		AppendCompatibilityDatabase(dbname);
	}
	
	SceUID fd = sceIoDopen("ux0:data/gms");
	SceIoDirent g_dir;
	while (sceIoDread(fd, &g_dir) > 0) {
		char apk_name[256];
		SceIoStat stat;
		sprintf(apk_name, "ux0:data/gms/%s/game.apk", g_dir.d_name);
		if (!sceIoGetstat(apk_name, &stat)) {
			DrawExtrapolatorDialog(g_dir.d_name);
			GameSelection *g = (GameSelection *)malloc(sizeof(GameSelection));
			char id_path[256];
			sprintf(id_path, "ux0:data/gms/%s/id.txt", g_dir.d_name);
			FILE *f2 = fopen(id_path, "r");
			if (f2) {
				int len = fread(g->game_id, 1, 128, f2);
				g->game_id[len] = 0;
				fclose(f2);
			} else {
				unzFile apk_file = unzOpen(apk_name);
				unzLocateFile(apk_file, "assets/game.droid", NULL);
				unzOpenCurrentFile(apk_file);
				unzReadCurrentFile(apk_file, generic_mem_buffer, 20);
				uint32_t offs;
				unzReadCurrentFile(apk_file, &offs, 4);
				uint32_t target = offs - 28;
				while (target > MEM_BUFFER_SIZE) {
					unzReadCurrentFile(apk_file, generic_mem_buffer, MEM_BUFFER_SIZE);
					target -= MEM_BUFFER_SIZE;
				}
				unzReadCurrentFile(apk_file, generic_mem_buffer, target);
				unzReadCurrentFile(apk_file, &offs, 4);
				unzReadCurrentFile(apk_file, g->game_id, offs + 1);
				if (!strcmp(g->game_id, "Runner")) {
					unzReadCurrentFile(apk_file, &offs, 4);
					unzReadCurrentFile(apk_file, generic_mem_buffer, offs + 1);
					unzReadCurrentFile(apk_file, &offs, 4);
					unzReadCurrentFile(apk_file, g->game_id, offs + 1);
				}
				f2 = fopen(id_path, "w");
				fwrite(g->game_id, 1, strlen(g->game_id), f2);
				fclose(f2);
				unzCloseCurrentFile(apk_file);
				unzClose(apk_file);
			}
			strcpy(g->name, g_dir.d_name);
			g->size = (float)stat.st_size / (1024.0f * 1024.0f);
			loadConfig(g);
			g->status = SearchForCompatibilityData(g->game_id);
			g->next = games;
			games = g;
		}
	}
	sceIoDclose(fd);
	
	while (!launch_item) {
		if (old_sort_idx != sort_idx) {
			old_sort_idx = sort_idx;
			sort_gamelist(games);
		}
		
		ImGui_ImplVitaGL_NewFrame();
		
		if (ImGui::BeginMainMenuBar()) {
			ImGui::Text("YoYo Loader Vita");
			if (calculate_ver_len) {
				calculate_ver_len = false;
				sprintf(ver_str, "v.%s (%s)", VERSION, stringify(GIT_VERSION));
				ImVec2 ver_sizes = ImGui::CalcTextSize(ver_str);
				ver_len = ver_sizes.x;
			}
			ImGui::SetCursorPosX(950 - ver_len);
			ImGui::Text(ver_str); 
			ImGui::EndMainMenuBar();
		}
		
		ImGui::SetNextWindowPos(ImVec2(0, 19), ImGuiSetCond_Always);
		ImGui::SetNextWindowSize(ImVec2(553, 524), ImGuiSetCond_Always);
		ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);
		
		ImGui::AlignTextToFramePadding();
		ImGui::Text(lang_strings[STR_FILTER_BY]);
		ImGui::SameLine();
		ImGui::PushItemWidth(-1.0f);
		if (ImGui::BeginCombo("##combo", filter_modes[filter_idx])) {
			for (int n = 0; n < FILTER_MODES_NUM; n++) {
				bool is_selected = filter_idx == n;
				if (ImGui::Selectable(filter_modes[n], is_selected))
					filter_idx = n;
				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::PopItemWidth();
		ImGui::Separator();
		
		GameSelection *g = games;
		ImVec2 config_pos;
		while (g) {
			if (filter_idx != FILTER_DISABLED && filterGames(g)) {
				g = g->next;
				continue;
			}
			if (ImGui::Button(g->name, ImVec2(-1.0f, 0.0f))) {
				launch_item = g->name;
			}
			if (ImGui::IsItemHovered()) {
				hovered = g;
			}
			g = g->next;
		}
		ImGui::End();
		
		SceCtrlData pad;
		sceCtrlPeekBufferPositive(0, &pad, 1);
		if (pad.buttons & SCE_CTRL_TRIANGLE && !(oldpad & SCE_CTRL_TRIANGLE) && hovered && !extracting && !is_downloading_banners && !is_downloading_anim_banner) {
			is_config_invoked = !is_config_invoked;
			sprintf(settings_str, "%s - %s", hovered->name, lang_strings[STR_SETTINGS]);
			saved_size = -1.0f;
		} else if (pad.buttons & SCE_CTRL_SQUARE && !(oldpad & SCE_CTRL_SQUARE) && !is_config_invoked && !is_downloading_banners && !is_downloading_anim_banner) {
			is_downloading_banners = true;
			banner_thid = sceKernelCreateThread("Banners Downloader", &bannerThread, 0x10000100, 0x100000, 0, 0, NULL);
			sceKernelStartThread(banner_thid, 0, NULL);
		} else if (pad.buttons & SCE_CTRL_LTRIGGER && !(oldpad & SCE_CTRL_LTRIGGER) && !is_config_invoked) {
			sort_idx -= 1;
			if (sort_idx < 0)
				sort_idx = (sizeof(sort_modes_str) / sizeof(sort_modes_str[0])) - 1;
		} else if (pad.buttons & SCE_CTRL_RTRIGGER && !(oldpad & SCE_CTRL_RTRIGGER) && !is_config_invoked) {
			sort_idx = (sort_idx + 1) % (sizeof(sort_modes_str) / sizeof(sort_modes_str[0]));
		} else if (pad.buttons & SCE_CTRL_SELECT && !(oldpad & SCE_CTRL_SELECT)) {
			video_close();
			animated_preview_delayer = ANIMATED_PREVIEW_DELAY;
			anim_download_request = true;
		}
		oldpad = pad.buttons;
		
		if (is_downloading_banners) {
			res = sceKernelGetThreadInfo(banner_thid, &info);
			if (info.status > SCE_THREAD_DORMANT || res < 0) {
				glViewport(0, 0, static_cast<int>(ImGui::GetIO().DisplaySize.x), static_cast<int>(ImGui::GetIO().DisplaySize.y));
				ImGui::Render();
				ImGui_ImplVitaGL_RenderDrawData(ImGui::GetDrawData());
				vglSwapBuffers(GL_FALSE);
				extract_file(TEMP_DOWNLOAD_NAME, "ux0:data/gms/shared/");
				ImGui_ImplVitaGL_NewFrame();
				sceIoRemove(TEMP_DOWNLOAD_NAME);
				is_downloading_banners = false;
			} else {
				DrawDownloaderDialog(1, downloaded_bytes, total_bytes, lang_strings[STR_BANNERS], 1, false);
			}
		} else if (is_downloading_anim_banner) {
			res = sceKernelGetThreadInfo(banner_thid, &info);
			if (info.status > SCE_THREAD_DORMANT || res < 0) {
				glViewport(0, 0, static_cast<int>(ImGui::GetIO().DisplaySize.x), static_cast<int>(ImGui::GetIO().DisplaySize.y));
				ImGui::Render();
				ImGui_ImplVitaGL_RenderDrawData(ImGui::GetDrawData());
				vglSwapBuffers(GL_FALSE);
				ImGui_ImplVitaGL_NewFrame();
				is_downloading_anim_banner = false;
			} else {
				DrawDownloaderDialog(1, downloaded_bytes, total_bytes, lang_strings[STR_ANIM_BANNER], 1, false);
			}
		} else if (is_config_invoked) {
			const char *desc = nullptr;
			
			ImGui::SetNextWindowPos(ImVec2(50, 30), ImGuiSetCond_Always);
			ImGui::SetNextWindowSize(ImVec2(860, 500), ImGuiSetCond_Always);
			ImGui::Begin(settings_str, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
			ImGui::Checkbox(lang_strings[STR_GLES1], &hovered->gles1);
			if (ImGui::IsItemHovered())
				desc = lang_strings[STR_GLES1_DESC];
			ImGui::Checkbox(lang_strings[STR_FAKE_WIN], &hovered->fake_win_mode);
			if (ImGui::IsItemHovered())
				desc = lang_strings[STR_FAKE_WIN_DESC];
			ImGui::Checkbox(lang_strings[STR_EXTRA_MEM], &hovered->mem_extended);
			if (ImGui::IsItemHovered())
				desc = lang_strings[STR_EXTRA_MEM_DESC];
			ImGui::Checkbox(lang_strings[STR_EXTRA_POOL], &hovered->newlib_extended);
			if (ImGui::IsItemHovered())
				desc = lang_strings[STR_EXTRA_POOL_DESC];
			ImGui::Checkbox(lang_strings[STR_SQUEEZE], &hovered->squeeze_mem);
			if (ImGui::IsItemHovered())
				desc = lang_strings[STR_SQUEEZE_DESC];
			ImGui::Checkbox(lang_strings[STR_VIDEO_PLAYER], &hovered->video_support);
			if (ImGui::IsItemHovered())
				desc = lang_strings[STR_VIDEO_PLAYER_DESC];
			ImGui::Checkbox(lang_strings[STR_NETWORK], &hovered->has_net);
			if (ImGui::IsItemHovered())
				desc = lang_strings[STR_NETWORK_DESC];
			ImGui::Separator();
			ImGui::Checkbox(lang_strings[STR_BILINEAR], &hovered->bilinear);
			if (ImGui::IsItemHovered())
				desc = lang_strings[STR_BILINEAR_DESC];
			ImGui::Checkbox(lang_strings[STR_COMPRESS], &hovered->compress_textures);
			if (ImGui::IsItemHovered())
				desc = lang_strings[STR_COMPRESS_DESC];
			ImGui::Separator();
			ImGui::Checkbox(lang_strings[STR_SPLASH_SKIP], &hovered->skip_splash);
			if (ImGui::IsItemHovered())
				desc = lang_strings[STR_SPLASH_SKIP_DESC];
			ImGui::Separator();
			ImGui::Checkbox(lang_strings[STR_DEBUG_MODE], &hovered->debug_mode);
			if (ImGui::IsItemHovered())
				desc = lang_strings[STR_DEBUG_MODE_DESC];
			ImGui::Checkbox(lang_strings[STR_DEBUG_SHADERS], &hovered->debug_shaders);
			if (ImGui::IsItemHovered())
				desc = lang_strings[STR_DEBUG_SHADERS_DESC];
			ImGui::Separator();
			if (ImGui::Button(lang_strings[STR_OPTIMIZE])) {
				if (!extracting)
					OptimizeApk(hovered->name);
			}
			if (ImGui::IsItemHovered())
				desc = lang_strings[STR_OPTIMIZE_DESC];
			if (saved_size != -1.0f) {
				ImGui::Text(" ");
				ImGui::Text(lang_strings[STR_OPTIMIZE_END]);
				ImGui::Text("%s %.2f MBs!", lang_strings[STR_REDUCED], saved_size);
				if (extracting)
					hovered->size -= saved_size;
				extracting = false;
			} else if (extracting) {
				ImGui::Text(" ");
				ImGui::Text(lang_strings[STR_OPTIMIZATION]);
				if (tot_idx > 0)
					ImGui::ProgressBar((float)cur_idx / float(tot_idx), ImVec2(200, 0));
				else
					ImGui::ProgressBar(0.0f, ImVec2(200, 0));
			}
			ImGui::SetCursorPosY(460);
			if (desc)
				ImGui::TextWrapped(desc);
			ImGui::End();
		}
		
		ImGui::SetNextWindowPos(ImVec2(553, 19), ImGuiSetCond_Always);
		ImGui::SetNextWindowSize(ImVec2(407, 524), ImGuiSetCond_Always);
		ImGui::Begin("Info Window", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);
		if (hovered) {
			has_preview_icon = LoadPreview(hovered);
			LoadAnimatedPreview(hovered);
			int anim_w, anim_h;
			GLuint anim_icon = video_get_frame(&anim_w, &anim_h);
			if (anim_icon != 0xDEADBEEF) {
				// FIXME: For now we hardcode 284x160 since sceAvPlayer seems to align the output texture to 288x160 producing artifacts
				ImGui::SetCursorPos(ImVec2((PREVIEW_WIDTH - 284) / 2 + PREVIEW_PADDING, PREVIEW_PADDING)); 
				ImGui::Image((void*)anim_icon, ImVec2(anim_w, preview_height), ImVec2(0, 0), ImVec2(0.98765f, 1));
			} else if (has_preview_icon) {
				ImGui::SetCursorPos(ImVec2(preview_x + PREVIEW_PADDING, preview_y + PREVIEW_PADDING));
				ImGui::Image((void*)preview_icon, ImVec2(preview_width, preview_height));
			}
			ImGui::Text("%s: %s", lang_strings[STR_GAME_ID], hovered->game_id);
			ImGui::Text("%s: %.2f MBs", lang_strings[STR_SIZE], hovered->size);
			if (hovered->status) {
				if (hovered->status->playable) {
					ImGui::SameLine();
					ImVec2 tsize = ImGui::CalcTextSize(lang_strings[STR_PLAYABLE]);
					ImGui::SetCursorPosX(395 - tsize.x);
					ImGui::TextColored(ImVec4(0, 0.75f, 0, 1.0f), lang_strings[STR_PLAYABLE]);
				} else if (hovered->status->ingame_plus) {
					ImGui::SameLine();
					ImVec2 tsize = ImGui::CalcTextSize(lang_strings[STR_INGAME_PLUS]);
					ImGui::SetCursorPosX(395 - tsize.x);
					ImGui::TextColored(ImVec4(1.0f, 1.0f, 0, 1.0f), lang_strings[STR_INGAME_PLUS]);
				} else if (hovered->status->ingame_low) {
					ImGui::SameLine();
					ImVec2 tsize = ImGui::CalcTextSize(lang_strings[STR_INGAME_MINUS]);
					ImGui::SetCursorPosX(395 - tsize.x);
					ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.25f, 1.0f), lang_strings[STR_INGAME_MINUS]);
				} else if (hovered->status->crash) {
					ImGui::SameLine();
					ImVec2 tsize = ImGui::CalcTextSize(lang_strings[STR_CRASH]);
					ImGui::SetCursorPosX(395 - tsize.x);
					ImGui::TextColored(ImVec4(1.0f, 0, 0, 1.0f), lang_strings[STR_CRASH]);
				}
			}
			ImGui::Separator();
			ImGui::Text("%s: %s", lang_strings[STR_GLES1], hovered->gles1 ? lang_strings[STR_YES] : lang_strings[STR_NO]);
			ImGui::Text("%s: %s", lang_strings[STR_FAKE_WIN], hovered->fake_win_mode ? lang_strings[STR_YES] : lang_strings[STR_NO]);
			ImGui::Text("%s: %s", lang_strings[STR_EXTRA_MEM], hovered->mem_extended ? lang_strings[STR_YES] : lang_strings[STR_NO]);
			ImGui::Text("%s: %s", lang_strings[STR_EXTRA_POOL], hovered->newlib_extended ? lang_strings[STR_YES] : lang_strings[STR_NO]);
			ImGui::Text("%s: %s", lang_strings[STR_SQUEEZE], hovered->squeeze_mem ? lang_strings[STR_YES] : lang_strings[STR_NO]);
			ImGui::Text("%s: %s", lang_strings[STR_VIDEO_PLAYER], hovered->video_support ? lang_strings[STR_YES] : lang_strings[STR_NO]);
			ImGui::Text("%s: %s", lang_strings[STR_NETWORK], hovered->has_net ? lang_strings[STR_YES] : lang_strings[STR_NO]);
			ImGui::Separator();
			ImGui::Text("%s: %s", lang_strings[STR_BILINEAR], hovered->bilinear ? lang_strings[STR_YES] : lang_strings[STR_NO]);
			ImGui::Text("%s: %s", lang_strings[STR_COMPRESS], hovered->compress_textures ? lang_strings[STR_YES] : lang_strings[STR_NO]);
			ImGui::Separator();
			ImGui::Text("%s: %s", lang_strings[STR_SPLASH_SKIP], hovered->skip_splash ? lang_strings[STR_YES] : lang_strings[STR_NO]);
			ImGui::Separator();
			ImGui::Text("%s: %s", lang_strings[STR_DEBUG_MODE], hovered->debug_mode ? lang_strings[STR_YES] : lang_strings[STR_NO]);
			ImGui::Text("%s: %s", lang_strings[STR_DEBUG_SHADERS], hovered->debug_shaders ? lang_strings[STR_YES] : lang_strings[STR_NO]);
			ImGui::TextColored(ImVec4(0.702f, 0.863f, 0.067f, 1.00f), lang_strings[STR_SETTINGS_INSTR]);
			ImGui::TextColored(ImVec4(0.702f, 0.863f, 0.067f, 1.00f), lang_strings[STR_ANIM_BANNER_INSTR]);
		}
		ImGui::SetCursorPosY(470);
		ImGui::Text("%s: %s", lang_strings[STR_SORT], sort_modes_str[sort_idx]);
		ImGui::TextColored(ImVec4(0.702f, 0.863f, 0.067f, 1.00f), lang_strings[STR_SORT_INSTR]);
		ImGui::TextColored(ImVec4(0.702f, 0.863f, 0.067f, 1.00f), lang_strings[STR_BANNERS_INSTR]);
		ImGui::End();
		
		glViewport(0, 0, static_cast<int>(ImGui::GetIO().DisplaySize.x), static_cast<int>(ImGui::GetIO().DisplaySize.y));
		ImGui::Render();
		ImGui_ImplVitaGL_RenderDrawData(ImGui::GetDrawData());
		vglSwapBuffers(GL_FALSE);
	}

	f = fopen(LAUNCH_FILE_PATH, "w+");
	fwrite(launch_item, 1, strlen(launch_item), f);
	fclose(f);
	
	char config_path[256];
	sprintf(config_path, "ux0:data/gms/%s/yyl.cfg", launch_item);
	f = fopen(config_path, "w+");
	fprintf(f, "%s=%d\n", "forceGLES1", (int)hovered->gles1);
	fprintf(f, "%s=%d\n", "noSplash", (int)hovered->skip_splash);
	fprintf(f, "%s=%d\n", "forceBilinear", (int)hovered->bilinear);
	fprintf(f, "%s=%d\n", "winMode", (int)hovered->fake_win_mode);
	fprintf(f, "%s=%d\n", "compressTextures", (int)hovered->compress_textures);
	fprintf(f, "%s=%d\n", "debugMode", (int)hovered->debug_mode);
	fprintf(f, "%s=%d\n", "debugShaders", (int)hovered->debug_shaders);
	fprintf(f, "%s=%d\n", "maximizeMem", (int)hovered->mem_extended);
	fprintf(f, "%s=%d\n", "maximizeNewlib", (int)hovered->newlib_extended);
	fprintf(f, "%s=%d\n", "videoSupport", (int)hovered->video_support);
	fprintf(f, "%s=%d\n", "netSupport", (int)hovered->has_net);
	fprintf(f, "%s=%d\n", "squeezeMem", (int)hovered->squeeze_mem);
	fclose(f);
	
	sprintf(config_path, "ux0:data/gms/shared/yyl.cfg");
	f = fopen(config_path, "w+");
	fprintf(f, "%s=%d\n", "sortMode", sort_idx);
	fprintf(f, "%s=%d\n", "language", console_language);
	fclose(f);
	
	sprintf(config_path, "ux0:data/gms/%s/keys.ini", launch_item);
	SceIoStat stat;
	if (sceIoGetstat(config_path, &stat)) {
		char url[512], final_url[512] = "";
		curl_handle = curl_easy_init();
		sprintf(url, "https://github.com/Rinnegatamante/yoyoloader_vita/blob/main/keymaps/%s.ini?raw=true", launch_item);
		char *space = strstr(url, " ");
		char *s = url;
		while (space) {
			space[0] = 0;
			sprintf(final_url, "%s%s%%20", final_url, s);
			space[0] = ' ';
			s = space + 1;
			space = strstr(s, " ");
		}
		sprintf(final_url, "%s%s", final_url, s);
		downloaded_bytes = 0;
		total_bytes = 20 * 1024; /* 20 KB */
		startDownload(final_url);
		int response_code;
		curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
		if (response_code == 200) {
			init_interactive_msg_dialog(lang_strings[STR_KEYMAP]);
			while (sceMsgDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) {
				vglSwapBuffers(GL_TRUE);
			}
			SceMsgDialogResult res;
			memset(&res, 0, sizeof(SceMsgDialogResult));
			sceMsgDialogGetResult(&res);
			if (res.buttonId == SCE_MSG_DIALOG_BUTTON_ID_YES) {
				fh = fopen(config_path, "wb");
				fwrite((const void*)generic_mem_buffer, 1, downloaded_bytes, fh);
				fclose(fh);
			}
			sceMsgDialogTerm();		
		}
		curl_easy_cleanup(curl_handle);
	}

	sceAppMgrLoadExec(hovered->video_support ? "app0:/loader2.bin" : "app0:/loader.bin", NULL, NULL);
	return 0;
}
