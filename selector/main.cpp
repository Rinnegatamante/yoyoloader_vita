#include <vitasdk.h>
#include <vitaGL.h>
#include <imgui_vita.h>
#include <curl/curl.h>
#include <stdio.h>
#include <string>
#include "../loader/zip.h"
#include "../loader/unzip.h"

#define DATA_PATH "ux0:data/gms"
#define LAUNCH_FILE_PATH DATA_PATH "/launch.txt"
#define TEMP_DOWNLOAD_NAME "ux0:data/yyl.tmp"
#define LOG_DOWNLOAD_NAME "ux0:data/gms/yyl.log"

#define VERSION "0.1"
#define FUNC_TO_NAME(x) #x
#define stringify(x) FUNC_TO_NAME(x)
#define MIN(x, y) (x) < (y) ? (x) : (y)

extern "C" {
	int debugPrintf(const char *fmt, ...) {return 0;}
	void fatal_error(const char *fmt, ...);
};

void DrawDownloaderDialog(int index, float downloaded_bytes, float total_bytes, char *text, int passes);
void DrawExtractorDialog(int index, float file_extracted_bytes, float extracted_bytes, float file_total_bytes, float total_bytes, char *filename, int num_files);
void DrawChangeListDialog(FILE *f);

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
uint8_t *downloader_mem_buffer = nullptr;
static FILE *fh;
char *bytes_string;

struct GameSelection {
	char name[128];
	float size;
	bool bilinear;
	bool gles1;
	bool skip_splash;
	bool single_thread;
	bool fake_win_mode;
	bool debug_mode;
	bool debug_shaders;
	bool mem_extended;
	bool newlib_extended;
	GameSelection *next;
};

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
	uint8_t *dst = &downloader_mem_buffer[downloaded_bytes];
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
			else if (strcmp("debugMode", buffer) == 0) g->debug_mode = (bool)value;
			else if (strcmp("noSplash", buffer) == 0) g->skip_splash = (bool)value;
			else if (strcmp("maximizeMem", buffer) == 0) g->mem_extended = (bool)value;
			else if (strcmp("maximizeNewlib", buffer) == 0) g->newlib_extended = (bool)value;
		}
		fclose(config);
	} else {
		sceClibMemset(&g->bilinear, 0, sizeof(bool) * 8);
	}
}

enum {
	FORCE_GLES1,
	BILINEAR_FILTER,
	FAKE_WIN_MODE,
	DEBUG_SHADERS,
	DEBUG_MODE,
	DISABLE_SPLASH,
	EXTRA_MEM_MODE,
	OPTIMIZE_APK,
	EXTEND_NEWLIB
};

const char *options_descs[] = {
	"Enforces GLES1 as rendering backend mode. May improve performances or make a game go further when crashing.",
	"Enforces bilinear filtering on textures.",
	"Fakes the reported target mode to the Runner as Windows. Some games require it to properly handle inputs.",
	"Enables dumping of attempted shader translations by the built-in GLSL to CG shader translator in ux0:data/gms/shared/glsl.",
	"Enables debug logging in ux0:data/gms/shared/yyl.log.",
	"Disables splashscreen rendering at game boot.",
	"Allows the Runner to use approximately extra 12 MBs of memory. May break some debugging tools.",
	"Reduces apk size by removing unnecessary data inside it and improves performances by recompressing files one by one depending on their expected use.",
	"Increases the size of the memory pool available for the Runner. May solve some crashes.",
};

const char *sort_modes_str[] = {
	"Name (Ascending)",
	"Name (Descending)"
};
int sort_idx = 0;
int old_sort_idx = -1;

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
			void *buffer = malloc(file_info.uncompressed_size);
			unzOpenCurrentFile(src_file);
			unzReadCurrentFile(src_file, buffer, file_info.uncompressed_size);
			unzCloseCurrentFile(src_file);
			if (strstr(fname, ".ogg")) {
				zipOpenNewFileInZip(dst_file, fname, NULL, NULL, 0, NULL, 0, NULL, 0, Z_NO_COMPRESSION);
			} else {
				zipOpenNewFileInZip(dst_file, fname, NULL, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_DEFAULT_COMPRESSION);
			}
			zipWriteInFileInZip(dst_file, buffer, file_info.uncompressed_size);
			zipCloseFileInZip(dst_file);
			free(buffer);
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
				snprintf(target_commit, 6, strstr((char*)downloader_mem_buffer, "body") + 10);
				if (strncmp(target_commit, stringify(GIT_VERSION), 5)) {
					sprintf(url, "https://api.github.com/repos/Rinnegatamante/yoyoloader_vita/compare/%s...%s", stringify(GIT_VERSION), target_commit);
					update_detected = true;
				}
			} else if (i == UPDATER_DOWNLOAD_CHANGELIST) {
				fh = fopen(LOG_DOWNLOAD_NAME, "wb");
				fwrite((const void*)downloader_mem_buffer, 1, downloaded_bytes, fh);
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
			fwrite((const void*)downloader_mem_buffer, 1, downloaded_bytes, fh);
			fclose(fh);
		}
	}
	curl_easy_cleanup(curl_handle);
	sceKernelExitDeleteThread(0);
	return 0;
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

int main(int argc, char *argv[]) {
	sceIoMkdir("ux0:data/gms", 0777);
	
	if (!file_exists("ur0:/data/libshacccg.suprx") && !file_exists("ur0:/data/external/libshacccg.suprx"))
		fatal_error("Error libshacccg.suprx is not installed.");
	
	GameSelection *hovered = nullptr;
	vglInitExtended(0, 960, 544, 0x1800000, SCE_GXM_MULTISAMPLE_4X);
	ImGui::CreateContext();
	ImGui_ImplVitaGL_Init();
	ImGui_ImplVitaGL_TouchUsage(false);
	ImGui_ImplVitaGL_GamepadUsage(true);
	ImGui::StyleColorsDark();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::GetIO().MouseDrawCursor = false;
	
	SceKernelThreadInfo info;
	info.size = sizeof(SceKernelThreadInfo);
	int res = 0;
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
		sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
		int ret = sceNetShowNetstat();
		SceNetInitParam initparam;
		if (ret == SCE_NET_ERROR_ENOTINIT) {
			initparam.memory = malloc(1024 * 1024);
			initparam.size = 1024 * 1024;
			initparam.flags = 0;
			sceNetInit(&initparam);
		}
	
		downloader_mem_buffer = (uint8_t*)malloc(32 * 1024 * 1024);
		SceUID thd = sceKernelCreateThread("Auto Updater", &updaterThread, 0x10000100, 0x100000, 0, 0, NULL);
		sceKernelStartThread(thd, 0, NULL);
		do {
			if (downloader_pass == UPDATER_CHECK_UPDATES) DrawDownloaderDialog(downloader_pass + 1, downloaded_bytes, total_bytes, "Checking for updates", NUM_UPDATE_PASSES);
			else if (downloader_pass == UPDATER_DOWNLOAD_CHANGELIST) DrawDownloaderDialog(downloader_pass + 1, downloaded_bytes, total_bytes, "Downloading Changelist", NUM_UPDATE_PASSES);
			else DrawDownloaderDialog(downloader_pass + 1, downloaded_bytes, total_bytes, "Downloading an update", NUM_UPDATE_PASSES);
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
		free(downloader_mem_buffer);
	}
	
	SceUID fd = sceIoDopen("ux0:data/gms");
	SceIoDirent g_dir;
	while (sceIoDread(fd, &g_dir) > 0) {
		char apk_name[256];
		SceIoStat stat;
		sprintf(apk_name, "ux0:data/gms/%s/game.apk", g_dir.d_name);
		if (!sceIoGetstat(apk_name, &stat)) {
			GameSelection *g = (GameSelection *)malloc(sizeof(GameSelection));
			strcpy(g->name, g_dir.d_name);
			g->size = (float)stat.st_size / (1024.0f * 1024.0f);
			loadConfig(g);
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
		ImGui::SetNextWindowSize(ImVec2(560, 524), ImGuiSetCond_Always);
		ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);
		
		GameSelection *g = games;
		ImVec2 config_pos;
		while (g) {
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
		if (pad.buttons & SCE_CTRL_TRIANGLE && !(oldpad & SCE_CTRL_TRIANGLE) && hovered && !extracting) {
			is_config_invoked = !is_config_invoked;
			sprintf(settings_str, "%s - Settings", hovered->name);
			saved_size = -1.0f;
		} else if (pad.buttons & SCE_CTRL_LTRIGGER && !(oldpad & SCE_CTRL_LTRIGGER) && !is_config_invoked) {
			sort_idx -= 1;
			if (sort_idx < 0)
				sort_idx = (sizeof(sort_modes_str) / sizeof(sort_modes_str[0])) - 1;
		} else if (pad.buttons & SCE_CTRL_RTRIGGER && !(oldpad & SCE_CTRL_RTRIGGER) && !is_config_invoked) {
			sort_idx = (sort_idx + 1) % (sizeof(sort_modes_str) / sizeof(sort_modes_str[0]));
		}
		oldpad = pad.buttons;
		
		if (is_config_invoked) {
			const char *desc = nullptr;
			
			ImGui::SetNextWindowPos(ImVec2(50, 30), ImGuiSetCond_Always);
			ImGui::SetNextWindowSize(ImVec2(860, 400), ImGuiSetCond_Always);
			ImGui::Begin(settings_str, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
			ImGui::Checkbox("Force GLES1 Mode", &hovered->gles1);
			if (ImGui::IsItemHovered())
				desc = options_descs[FORCE_GLES1];
			ImGui::Checkbox("Fake Windows as Platform", &hovered->fake_win_mode);
			if (ImGui::IsItemHovered())
				desc = options_descs[FAKE_WIN_MODE];
			ImGui::Checkbox("Run with Extended Mem Mode", &hovered->mem_extended);
			if (ImGui::IsItemHovered())
				desc = options_descs[EXTRA_MEM_MODE];
			ImGui::Checkbox("Run with Extended Runner Pool", &hovered->newlib_extended);
			if (ImGui::IsItemHovered())
				desc = options_descs[EXTEND_NEWLIB];
			ImGui::Separator();
			ImGui::Checkbox("Force Bilinear Filtering", &hovered->bilinear);
			if (ImGui::IsItemHovered())
				desc = options_descs[BILINEAR_FILTER];
			ImGui::Separator();
			ImGui::Checkbox("Skip Splashscreen at Boot", &hovered->skip_splash);
			if (ImGui::IsItemHovered())
				desc = options_descs[DISABLE_SPLASH];
			ImGui::Separator();
			ImGui::Checkbox("Run with Debug Mode", &hovered->debug_mode);
			if (ImGui::IsItemHovered())
				desc = options_descs[DEBUG_MODE];
			ImGui::Checkbox("Run with Shaders Debug Mode", &hovered->debug_shaders);
			if (ImGui::IsItemHovered())
				desc = options_descs[DEBUG_SHADERS];
			ImGui::Separator();
			if (ImGui::Button("Optimize Apk")) {
				if (!extracting)
					OptimizeApk(hovered->name);
			}
			if (ImGui::IsItemHovered())
				desc = options_descs[OPTIMIZE_APK];
			if (saved_size != -1.0f) {
				ImGui::Text(" ");
				ImGui::Text("Optimization completed!");
				ImGui::Text("Reduced apk size by %.2f MBs!", saved_size);
				if (extracting)
					hovered->size -= saved_size;
				extracting = false;
			} else if (extracting) {
				ImGui::Text(" ");
				ImGui::Text("Optimization in progress, please wait...");
				if (tot_idx > 0)
					ImGui::ProgressBar((float)cur_idx / float(tot_idx), ImVec2(200, 0));
				else
					ImGui::ProgressBar(0.0f, ImVec2(200, 0));
			}
			ImGui::SetCursorPosY(360);
			if (desc)
				ImGui::TextWrapped(desc);
			ImGui::End();
		}
		
		ImGui::SetNextWindowPos(ImVec2(560, 19), ImGuiSetCond_Always);
		ImGui::SetNextWindowSize(ImVec2(400, 524), ImGuiSetCond_Always);
		ImGui::Begin("Info Window", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);
		if (hovered) {
			ImGui::Text("APK Size: %.2f MBs", hovered->size);
			ImGui::Separator();
			ImGui::Text("Force GLES1 Mode: %s", hovered->gles1 ? "Yes" : "No");
			ImGui::Text("Fake Windows as Platform: %s", hovered->fake_win_mode ? "Yes" : "No");
			ImGui::Text("Run with Extended Mem Mode: %s", hovered->mem_extended ? "Yes" : "No");
			ImGui::Text("Run with Extended Runner Pool: %s", hovered->newlib_extended ? "Yes" : "No");
			ImGui::Separator();
			ImGui::Text("Force Bilinear Filtering: %s", hovered->bilinear ? "Yes" : "No");
			ImGui::Separator();
			ImGui::Text("Skip Splashscreen at Boot: %s", hovered->skip_splash ? "Yes" : "No");
			ImGui::Separator();
			ImGui::Text("Run with Debug Mode: %s", hovered->debug_mode ? "Yes" : "No");
			ImGui::Text("Run with Shaders Debug Mode: %s", hovered->debug_shaders ? "Yes" : "No");
			ImGui::TextColored(ImVec4(1.0f,1.0f,0.0f,1.0f), "Press Triangle to change settings");
		}
		ImGui::SetCursorPosY(480);
		ImGui::Text("Sort Mode: %s", sort_modes_str[sort_idx]);
		ImGui::TextColored(ImVec4(1.0f,1.0f,0.0f,1.0f), "Press L/R to change sorting mode");
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
	fprintf(f, "%s=%d\n", "debugMode", (int)hovered->debug_mode);
	fprintf(f, "%s=%d\n", "debugShaders", (int)hovered->debug_shaders);
	fprintf(f, "%s=%d\n", "maximizeMem", (int)hovered->mem_extended);
	fprintf(f, "%s=%d\n", "maximizeNewlib", (int)hovered->newlib_extended);
	fclose(f);
	
	if (hovered->newlib_extended) {
		f = fopen("ux0:data/gms/newlib.cfg", "w+");
		fprintf(f, "1");
		fclose(f);
	}

	sceAppMgrLoadExec("app0:/loader.bin", NULL, NULL);
	return 0;
}
