#include <vitasdk.h>
#include <vitaGL.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CURL *curl_handle = NULL;
static char *bytes_string;
char *post_url, *get_url;
volatile uint64_t downloaded_bytes = 0;
volatile uint64_t downloaded_hdr_bytes = 0;
static volatile uint64_t total_bytes = 0xFFFFFFFF;
static volatile uint64_t total_hdr_bytes = 0xFFFFFFFF;
uint8_t *downloader_mem_buffer = NULL;
uint8_t *downloader_hdr_buffer = NULL;
volatile int post_response_code, get_response_code;
SceUID post_thid, get_thid;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *stream)
{
	uint8_t *dst = &downloader_mem_buffer[downloaded_bytes];
	downloaded_bytes += nmemb;
	if (total_bytes < downloaded_bytes) total_bytes = downloaded_bytes;
	sceClibMemcpy(dst, ptr, nmemb);
	return nmemb;
}

static size_t header_cb(char *ptr, size_t size, size_t nmemb, void *stream)
{
	uint8_t *dst = &downloader_hdr_buffer[downloaded_hdr_bytes];
	downloaded_hdr_bytes += nmemb;
	if (total_hdr_bytes < downloaded_hdr_bytes) total_hdr_bytes = downloaded_hdr_bytes;
	sceClibMemcpy(dst, ptr, nmemb);
	return nmemb;
}

static int send_post_request_thread(unsigned int argc, void *argv)
{
	downloader_mem_buffer = vglMalloc(0x10000);
	downloader_hdr_buffer = vglMalloc(0x1000);
	downloaded_bytes = 0;
	downloaded_hdr_bytes = 0;
	
	char **data = (char **)argv;
	curl_handle = curl_easy_init();
	curl_easy_reset(curl_handle);
	curl_easy_setopt(curl_handle, CURLOPT_URL, data[0]);
	curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, data[1]);
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
	curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, bytes_string); // Dummy
	curl_easy_setopt(curl_handle, CURLOPT_RESUME_FROM, downloaded_bytes);
	curl_easy_setopt(curl_handle, CURLOPT_BUFFERSIZE, 524288);
	curl_easy_perform(curl_handle);
	
	curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &post_response_code);
	downloader_hdr_buffer[total_hdr_bytes] = 0;
	curl_easy_cleanup(curl_handle);
	free(data[0]);
	free(data[1]);
	
	return sceKernelExitDeleteThread(0);
}

static int send_get_request_thread(unsigned int argc, void *argv)
{
	downloader_mem_buffer = vglMalloc(0x100000);
	downloader_hdr_buffer = vglMalloc(0x1000);
	downloaded_bytes = 0;
	downloaded_hdr_bytes = 0;
	
	char **data = (char **)argv;
	curl_handle = curl_easy_init();
	curl_easy_reset(curl_handle);
	curl_easy_setopt(curl_handle, CURLOPT_URL, data[0]);
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
	curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, bytes_string); // Dummy
	curl_easy_setopt(curl_handle, CURLOPT_RESUME_FROM, downloaded_bytes);
	curl_easy_setopt(curl_handle, CURLOPT_BUFFERSIZE, 524288);
	curl_easy_perform(curl_handle);
	
	curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &get_response_code);
	downloader_hdr_buffer[total_hdr_bytes] = 0;
	curl_easy_cleanup(curl_handle);
	free(data[0]);
	
	return sceKernelExitDeleteThread(0);
}

void send_post_request(const char *url, const char *data) {
	const char *args[2] = {strdup(url), strdup(data)};
	post_url = strdup(url);
	post_thid = sceKernelCreateThread("Post Thread", &send_post_request_thread, 0x10000100, 0x100000, 0, 0, NULL);
	sceKernelStartThread(post_thid, sizeof(args), args);
}

void send_get_request(const char *url) {
	const char *args[1] = {strdup(url)};
	get_url = strdup(url);
	get_thid = sceKernelCreateThread("Get Thread", &send_get_request_thread, 0x10000100, 0x100000, 0, 0, NULL);
	sceKernelStartThread(get_thid, sizeof(args), args);
}
