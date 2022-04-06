#include <vitasdk.h>
#include <vitaGL.h>
#include <imgui_vita.h>
#include <stdio.h>
#include <string>

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

#define LAUNCH_FILE_PATH "ux0:data/gms/launch.txt"

char *desc = nullptr;
char *launch_item = nullptr;

struct GameSelection {
	char name[128];
	GameSelection *next;
};

GameSelection *games = nullptr;

int main(int argc, char *argv[]) {
	vglInitExtended(0, 960, 544, 0x1800000, SCE_GXM_MULTISAMPLE_4X);
	ImGui::CreateContext();
	ImGui_ImplVitaGL_Init();
	ImGui_ImplVitaGL_TouchUsage(false);
	ImGui_ImplVitaGL_GamepadUsage(true);
	ImGui::StyleColorsDark();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

	ImGui::GetIO().MouseDrawCursor = false;
	
	SceUID fd = sceIoDopen("ux0:data/gms");
	SceIoDirent g_dir;
	while (sceIoDread(fd, &g_dir) > 0) {
		if (strcmp(g_dir.d_name, "shared")) {
			GameSelection *g = (GameSelection *)malloc(sizeof(GameSelection));
			strcpy(g->name, g_dir.d_name);
			g->next = games;
			games = g;
		}
	}
	sceIoDclose(fd);
	
	while (!launch_item) {
		desc = nullptr;
		ImGui_ImplVitaGL_NewFrame();

		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiSetCond_Always);
		ImGui::SetNextWindowSize(ImVec2(960, 544), ImGuiSetCond_Always);
		ImGui::Begin("##main", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);
		
		GameSelection *g = games;
		while (g) {
			if (ImGui::Button(g->name, ImVec2(-1.0f, 0.0f))) {
				launch_item = g->name;
			}
			g = g->next;
		}

		ImGui::End();
	
		glViewport(0, 0, static_cast<int>(ImGui::GetIO().DisplaySize.x), static_cast<int>(ImGui::GetIO().DisplaySize.y));
		ImGui::Render();
		ImGui_ImplVitaGL_RenderDrawData(ImGui::GetDrawData());
		vglSwapBuffers(GL_FALSE);
	}

	FILE *f = fopen(LAUNCH_FILE_PATH, "w+");
	fwrite(launch_item, 1, strlen(launch_item), f);
	fclose(f);
	sceAppMgrLoadExec("app0:/loader.bin", NULL, NULL);
	return 0;
}
