#include <stdlib.h>
#include <stdio.h>

#include <vitasdk.h>
#include <vitaGL.h>
#include <imgui_vita.h>

#define SCR_WIDTH 960
#define SCR_HEIGHT 544

static char *sizes[] = {
	"B",
	"KB",
	"MB",
	"GB"
};

static float format(float len) {
	while (len > 1024) len = len / 1024.0f;
	return len;
}

static uint8_t quota(uint64_t len) {
	uint8_t ret = 0;
	while (len > 1024) {
		ret++;
		len = len / 1024;
	}
	return ret;
}

void DrawDownloaderDialog(int index, float downloaded_bytes, float total_bytes, char *text, int passes) {
	ImGui_ImplVitaGL_NewFrame();
	
	char msg[512];
	sprintf(msg, "%s (%d / %d)", text, index, passes);
	ImVec2 pos = ImGui::CalcTextSize(msg);

	ImGui::SetNextWindowPos(ImVec2((SCR_WIDTH / 2) - 200, (SCR_HEIGHT / 2) - 50), ImGuiSetCond_Always);
	ImGui::SetNextWindowSize(ImVec2(400, 100), ImGuiSetCond_Always);
	ImGui::Begin("", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);
	
	ImGui::SetCursorPos(ImVec2((400 - pos.x) / 2, 20));
	ImGui::Text(msg);
	if (total_bytes < 4000000000.0f) {
		sprintf(msg, "%.2f %s / %.2f %s", format(downloaded_bytes), sizes[quota(downloaded_bytes)], format(total_bytes), sizes[quota(total_bytes)]);
		pos = ImGui::CalcTextSize(msg);
		ImGui::SetCursorPos(ImVec2((400 - pos.x) / 2, 40));
		ImGui::Text(msg);
		ImGui::SetCursorPos(ImVec2(100, 60));
		ImGui::ProgressBar(downloaded_bytes / total_bytes, ImVec2(200, 0));
	} else {
		sprintf(msg, "%.2f %s", format(downloaded_bytes), sizes[quota(downloaded_bytes)]);
		pos = ImGui::CalcTextSize(msg);
		ImGui::SetCursorPos(ImVec2((400 - pos.x) / 2, 50));
		ImGui::Text(msg);
	}
	
	ImGui::End();
	glViewport(0, 0, static_cast<int>(ImGui::GetIO().DisplaySize.x), static_cast<int>(ImGui::GetIO().DisplaySize.y));
	ImGui::Render();
	ImGui_ImplVitaGL_RenderDrawData(ImGui::GetDrawData());
	vglSwapBuffers(GL_FALSE);
}

void DrawExtractorDialog(int index, float file_extracted_bytes, float extracted_bytes, float file_total_bytes, float total_bytes, char *filename, int num_files) {
	ImGui_ImplVitaGL_NewFrame();
	
	char msg1[256], msg2[256];
	sprintf(msg1, "%s (%d / %d)", "Extracting archive", index, num_files);
	sprintf(msg2, "%s (%.2f %s / %.2f %s)", filename, format(file_extracted_bytes), sizes[quota(file_extracted_bytes)], format(file_total_bytes), sizes[quota(file_total_bytes)]);
	ImVec2 pos1 = ImGui::CalcTextSize(msg1);
	ImVec2 pos2 = ImGui::CalcTextSize(msg2);
	
	ImGui::GetIO().MouseDrawCursor = false;
	ImGui::SetNextWindowPos(ImVec2((SCR_WIDTH / 2) - 200, (SCR_HEIGHT / 2) - 50), ImGuiSetCond_Always);
	ImGui::SetNextWindowSize(ImVec2(400, 100), ImGuiSetCond_Always);
	ImGui::Begin("", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);
	ImGui::SetCursorPos(ImVec2((400 - pos1.x) / 2, 20));
	ImGui::Text(msg1);
	ImGui::SetCursorPos(ImVec2((400 - pos2.x) / 2, 40));
	ImGui::Text(msg2);
	ImGui::SetCursorPos(ImVec2(100, 60));
	ImGui::ProgressBar(extracted_bytes / total_bytes, ImVec2(200, 0));
	
	ImGui::End();
	glViewport(0, 0, static_cast<int>(ImGui::GetIO().DisplaySize.x), static_cast<int>(ImGui::GetIO().DisplaySize.y));
	ImGui::Render();
	ImGui_ImplVitaGL_RenderDrawData(ImGui::GetDrawData());
	vglSwapBuffers(GL_FALSE);
	sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
}

struct ChangeList {
	char msg[256];
	ChangeList *next;
};
static ChangeList *lst = nullptr;

void AppendChangeListEntries(FILE *f) {
	fseek(f, 0, SEEK_END);
	uint64_t len = ftell(f) - 5000; // Let's skip some data to improve performances
	fseek(f, 5000, SEEK_SET);
	char *buffer = (char*)malloc(len + 1);
	fread(buffer, 1, len, f);
	buffer[len] = 0;
	char *ptr = strstr(buffer, "\"commits\":");
	char *end;
	do {
		ptr = strstr(ptr, "\"message\":");
		if (ptr) {
			ChangeList *node = (ChangeList *)malloc(sizeof(ChangeList));
				
			// Extracting message
			ptr += 12;
			end = strstr(ptr, "\"");
			char *tptr = strstr(ptr, "\\n\\n");
			if (tptr && tptr < end)
				end = tptr;
			strcpy(node->msg, "- ");
			sceClibMemcpy(&node->msg[2], ptr, end - ptr);
			node->msg[end - ptr + 2] = 0;
				
			ptr += 1000; // Let's skip some data to improve performances
			node->next = lst;
			lst = node;
		}
	} while (ptr);
	fclose(f);
	free(buffer);
}

void DrawChangeListDialog(FILE *f) {
	AppendChangeListEntries(f);
	
	bool show_list = true;
	while (show_list) {
		glClear(GL_COLOR_BUFFER_BIT);
		ImGui_ImplVitaGL_NewFrame();
	
		ImGui::GetIO().MouseDrawCursor = false;
		ImGui::SetNextWindowPos(ImVec2(30.0f, 10.0f), ImGuiSetCond_Always);
		ImGui::SetNextWindowSize(ImVec2(900.0f, 524.0f), ImGuiSetCond_Always);
		ImGui::Begin("What's New", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);
	
		ChangeList *l = lst;
		while (l) {
			ImGui::TextWrapped(l->msg);
			l = l->next;
		}
		
		ImGui::Separator();
		if (ImGui::Button("Continue"))
			show_list = false;
	
		ImGui::End();
		glViewport(0, 0, static_cast<int>(ImGui::GetIO().DisplaySize.x), static_cast<int>(ImGui::GetIO().DisplaySize.y));
		ImGui::Render();
		ImGui_ImplVitaGL_RenderDrawData(ImGui::GetDrawData());
		vglSwapBuffers(GL_FALSE);
		sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
	}
}