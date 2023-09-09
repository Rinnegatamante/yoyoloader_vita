/* gamepad.c -- Gamepad code
 *
 * Copyright (C) 2022 Rinnegatamante
 * Copyright (C) 2022 JohnnyonFlame
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.	See the LICENSE file for details.
 */

// This file has been hugely derived from https://github.com/JohnnyonFlame/droidports/tree/master/ports/gmloader
#include <vitasdk.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "main.h"
#include "so_util.h"

#define NUM_BUTTONS 16

#define IS_AXIS_BOUNDS (axis >= 0 && axis < 4)
#define IS_BTN_BOUNDS (btn >= 0 && btn < NUM_BUTTONS)
#define IS_CONTROLLER_BOUNDS (id >= 0 && id < 4)

#define ANALOG_DEADZONE 30

extern so_module yoyoloader_mod;
extern int platTarget;
extern char fake_env[0x1000];

int (*YYGetInt32) (void *args, int idx);
int (*CreateDsMap) (int a1, char *type, int a3, int a4, char *desc, char *type2, double id, int a8);
void (*GamepadUpdateM) ();
void (*ProcessVirtualKeys) ();
void (*IO_UpdateM) ();
void (*CreateAsynEventWithDSMap) (int dsMap, int a2);
void (*CheckKeyPressed) (retval_t *ret, void *self, void *other, int argc, retval_t *args);
int (*Java_com_yoyogames_runner_RunnerJNILib_KeyEvent) (void *env, int a2, int state, int key_code, int unicode_key, int source);
extern void (*Function_Add)(const char *name, intptr_t func, int argc, char ret);
int *g_MousePosX, *g_MousePosY, *g_DoMouseButton;

enum {
	DISABLED,
	CAMERA_MODE,
	CURSOR_MODE
};

int has_click_emulation = DISABLED;
int analog_as_mouse = DISABLED;
int analog_as_keys = DISABLED;
int has_kb_mapping = DISABLED;
char keyboard_mapping[NUM_BUTTONS];
int is_key_pressed[NUM_BUTTONS] = {0};
enum {
	CROSS_BTN,
	CIRCLE_BTN,
	SQUARE_BTN,
	TRIANGLE_BTN,
	L1_BTN,
	R1_BTN,
	L2_BTN,
	R2_BTN,
	SELECT_BTN,
	START_BTN,
	L3_BTN,
	R3_BTN,
	UP_BTN,
	DOWN_BTN,
	LEFT_BTN,
	RIGHT_BTN,
	LEFT_ANALOG,
	RIGHT_ANALOG,
	UNK_BTN = 0xFF
};

typedef struct {
	char key_name[32];
	char key_value;
} key_map;

key_map special_keys[] = {
	{"ENTER", 13},
	{"SHIFT", 16},
	{"CTRL", 17},
	{"ALT", 18},
	{"ESC", 27},
	{"BACKSPACE", 8},
	{"TAB", 9},
	{"PRINTSCREEN", 44},
	{"LEFT", 21},
	{"RIGHT", 22},
	{"UP", 19},
	{"DOWN", 20},
	{"HOME", 36},
	{"END", 35},
	{"DEL", 46},
	{"INS", 45},
	{"PAGEUP", 33},
	{"PAGEDOWN", 34},
	{"F1", 112},
	{"F2", 113},
	{"F3", 114},
	{"F4", 115},
	{"F5", 116},
	{"F6", 117},
	{"F7", 118},
	{"F8", 119},
	{"F9", 120},
	{"F10", 121},
	{"F11", 122},
	{"F12", 123},
	{"NUMPAD0", 96},
	{"NUMPAD1", 97},
	{"NUMPAD2", 98},
	{"NUMPAD3", 99},
	{"NUMPAD4", 100},
	{"NUMPAD5", 101},
	{"NUMPAD6", 102},
	{"NUMPAD7", 103},
	{"NUMPAD8", 104},
	{"NUMPAD9", 105}
};

typedef enum GamepadButtonState {
	GAMEPAD_BUTTON_STATE_UP = -1,
	GAMEPAD_BUTTON_STATE_NEUTRAL = 0,
	GAMEPAD_BUTTON_STATE_HELD = 1,
	GAMEPAD_BUTTON_STATE_DOWN = 2
} GamepadButtonState;

typedef struct Gamepad {
	int is_available; 
	double buttons[16];
	double deadzone;
	double axis[4];
} Gamepad;

Gamepad yoyo_gamepads[4];

int is_gamepad_connected(int id) {
	return yoyo_gamepads[id].is_available;
}

void GetPlatformInstance(void *self, int n, retval_t *args) {
	args[0].kind = VALUE_REAL;
	
	switch (platTarget) {
	case 1: // Windows
		args[0].rvalue.val = 0.0f;
		break;
	case 2: // PS4
		args[0].rvalue.val = 14.0f;
		break;
	default: // Android
		args[0].rvalue.val = 4.0f;
		break;
	}
}

void gamepad_is_supported(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_BOOL;
	ret->rvalue.val = 1.0f;
}

void gamepad_get_device_count(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_REAL;
	ret->rvalue.val = 4.0f;
}

void gamepad_is_connected(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_REAL;
	int id = (int)args[0].rvalue.val;
	
	if (!IS_CONTROLLER_BOUNDS) {
		ret->rvalue.val = 0.0f;
		return;
	}
	
	ret->rvalue.val = (yoyo_gamepads[id].is_available) ? 1.0f : 0.0f;
}

void gamepad_get_description(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ref_t *ref = malloc(sizeof(ref_t));

	*ref = (ref_t){
		.m_refCount = 1,
		.m_size = strlen("Xbox 360 Controller (XInput STANDARD GAMEPAD)"),
		.m_thing = strdup("Xbox 360 Controller (XInput STANDARD GAMEPAD)")
	};

	ret->kind = VALUE_STRING;
	ret->rvalue.str = ref;
}

void gamepad_get_button_threshold(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_REAL;
	ret->rvalue.val = 0.5f;
}

void gamepad_set_button_threshold(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
}

void gamepad_axis_count(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_REAL;
	ret->rvalue.val = 4.f;
}

void gamepad_set_axis_deadzone(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	int id = (int)args[0].rvalue.val;
	double deadzone = args[1].rvalue.val;
	
	if (!IS_CONTROLLER_BOUNDS) {
		return;
	}

	yoyo_gamepads[id].deadzone = deadzone;
}

void gamepad_get_axis_deadzone(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_REAL;
	int id = (int)args[0].rvalue.val;
	
	if (!IS_CONTROLLER_BOUNDS) {
		ret->rvalue.val = 0.0f;
		return;
	}
 
	ret->rvalue.val = yoyo_gamepads[id].deadzone;
}

void gamepad_axis_value(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_REAL;
	int id = (int)args[0].rvalue.val;
	int axis = (int)(args[1].rvalue.val - ((double)(32785.0f)));
	
	if (!IS_CONTROLLER_BOUNDS || !IS_AXIS_BOUNDS) {
		ret->rvalue.val = 0.0f;
		return;
	}

	ret->rvalue.val = yoyo_gamepads[id].axis[axis];
	if (fabs(ret->rvalue.val) < yoyo_gamepads[id].deadzone)
		ret->rvalue.val = 0.0f;
}

void gamepad_button_check(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_REAL;
	int id = (int)args[0].rvalue.val;
	int btn = (int)(args[1].rvalue.val - ((double)(32769.0f)));
	
	if (!IS_CONTROLLER_BOUNDS || !IS_BTN_BOUNDS) {
		ret->rvalue.val = 0.0f;
		return;
	}

	ret->rvalue.val = (yoyo_gamepads[id].buttons[btn] > 0) ? 1.0f : 0.0f;
}

void gamepad_button_check_pressed(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_REAL;
	int id = (int)args[0].rvalue.val;
	int btn = (int)(args[1].rvalue.val - ((double)(32769.0f)));
	
	if (!IS_CONTROLLER_BOUNDS || !IS_BTN_BOUNDS) {
		ret->rvalue.val = 0.0f;
		return;
	}

	ret->rvalue.val = (yoyo_gamepads[id].buttons[btn] == 2) ? 1.0f : 0.0f;
}

void gamepad_button_check_released(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_REAL;
	int id = (int)args[0].rvalue.val;
	int btn = (int)(args[1].rvalue.val - ((double)(32769.0f)));
	
	if (!IS_CONTROLLER_BOUNDS || !IS_BTN_BOUNDS) {
		ret->rvalue.val = 0.0f;
		return;
	}

	ret->rvalue.val = (yoyo_gamepads[id].buttons[btn] == -1) ? 1.0f : 0.0f;
}

void gamepad_button_count(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_REAL;
	ret->rvalue.val = 16.f;
}

void gamepad_button_value(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_REAL;
	int id = (int)args[0].rvalue.val;
	int btn = (int)(args[1].rvalue.val - ((double)(32769.0f)));
	
	if (!IS_CONTROLLER_BOUNDS || !IS_BTN_BOUNDS) {
		ret->rvalue.val = 0.0f;
		return;
	}

	ret->rvalue.val = (yoyo_gamepads[id].buttons[btn] > 0) ? 1.0f : 0.0f;
}

void gamepad_set_vibration(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
}

void gamepad_set_colour(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
}

void mouse_set(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	*g_MousePosX = YYGetInt32(args, 0);
    *g_MousePosY = YYGetInt32(args, 1);
}

void mouse_get_x(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_REAL;
	ret->rvalue.val = *g_MousePosX;
}

void mouse_get_y(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	ret->kind = VALUE_REAL;
	ret->rvalue.val = *g_MousePosY;
}

int GamePadCheck(int startup) {
	if (sceCtrlIsMultiControllerSupported() && (platTarget != 0)) {
		int num_controllers = 0;
		SceCtrlPortInfo ctrl_state;
		sceCtrlGetControllerPortInfo(&ctrl_state);
		for (int i = 1; i < 5; i++) {
			int yoyo_port = i - 1;
			if (ctrl_state.port[i] != SCE_CTRL_TYPE_UNPAIRED) {
				num_controllers++;
				if (!yoyo_gamepads[yoyo_port].is_available) {
					yoyo_gamepads[yoyo_port].is_available = 1;
					if (!startup) {
						int dsMap = CreateDsMap(2, "event_type", 0, 0, "gamepad discovered", "pad_index", (double)yoyo_port, 0);
						CreateAsynEventWithDSMap(dsMap, 0x4B);
					}
				}
			} else if (yoyo_gamepads[yoyo_port].is_available) {
				yoyo_gamepads[yoyo_port].is_available = 0;
				if (!startup) {
					int dsMap = CreateDsMap(2, "event_type", 0, 0, "gamepad lost", "pad_index", (double)yoyo_port, 0);
					CreateAsynEventWithDSMap(dsMap, 0x4B);
				}
			}
		}
		return num_controllers;
	} else {
		yoyo_gamepads[platTarget != 0 ? 0 : 1].is_available = 1;
		return 1;
	}
}

void GamePadRestart() {
	for (int i = 0; i < 4; i++) {
		if (yoyo_gamepads[i].is_available) {
			int dsMap = CreateDsMap(2, "event_type", 0, 0, "gamepad discovered", "pad_index", (double)i, 0);
			CreateAsynEventWithDSMap(dsMap, 0x4B);
		}
	}
}

static int update_button(int new_state, int old_state) {
	if (new_state == GAMEPAD_BUTTON_STATE_HELD) {
		if (old_state == GAMEPAD_BUTTON_STATE_NEUTRAL)
			return GAMEPAD_BUTTON_STATE_DOWN;
		return GAMEPAD_BUTTON_STATE_HELD;
	} else {
		if (old_state == GAMEPAD_BUTTON_STATE_HELD)
			return GAMEPAD_BUTTON_STATE_UP;
		return GAMEPAD_BUTTON_STATE_NEUTRAL;
	}
}

void GamePadUpdate() {
	if (has_click_emulation)
		*g_DoMouseButton = 0;
	int num_controllers = GamePadCheck(0);
	
	for (int i = 0; i < 4; i++) {
		if (!yoyo_gamepads[i].is_available)
			continue;

		SceCtrlData pad;
		int port = (num_controllers == 1 && i == (platTarget != 0 ? 0 : 1)) ? 0 : (i + 1);
		sceCtrlPeekBufferPositiveExt2(port, &pad, 1);
	
		uint8_t new_states[NUM_BUTTONS] = {
			pad.buttons & SCE_CTRL_CROSS ? 1 : 0,
			pad.buttons & SCE_CTRL_CIRCLE ? 1 : 0,
			pad.buttons & SCE_CTRL_SQUARE ? 1 : 0,
			pad.buttons & SCE_CTRL_TRIANGLE ? 1 : 0,
			pad.buttons & SCE_CTRL_L1 ? 1 : 0,
			pad.buttons & SCE_CTRL_R1 ? 1 : 0,
			pad.buttons & SCE_CTRL_L2 ? 1 : 0,
			pad.buttons & SCE_CTRL_R2 ? 1 : 0,
			pad.buttons & SCE_CTRL_SELECT ? 1 : 0,
			pad.buttons & SCE_CTRL_START ? 1 : 0,
			pad.buttons & SCE_CTRL_L3 ? 1 : 0,
			pad.buttons & SCE_CTRL_R3 ? 1 : 0,
			pad.buttons & SCE_CTRL_UP ? 1 : 0,
			pad.buttons & SCE_CTRL_DOWN ? 1 : 0,
			pad.buttons & SCE_CTRL_LEFT ? 1 : 0,
			pad.buttons & SCE_CTRL_RIGHT ? 1 : 0
		};
		
#ifndef STANDALONE_MODE
		if (new_states[SELECT_BTN] && new_states[START_BTN] && new_states[L1_BTN] && new_states[R1_BTN])
			sceAppMgrLoadExec("app0:eboot.bin", NULL, NULL);
#endif

		if (num_controllers == 1) {
			// Rearpad support for L2/R2/L3/R3 emulation
			SceTouchData touch;
			sceTouchPeek(SCE_TOUCH_PORT_BACK, &touch, 1);
			for (int j = 0; j < touch.reportNum; j++) {
				int x = touch.report[j].x;
				int y = touch.report[j].y;
				if (x > 960) {
					if (y > 544) {
						new_states[R3_BTN] = 1;
					} else {
						new_states[R2_BTN] = 1;
					}
				} else {
					if (y > 544) {
						new_states[L3_BTN] = 1;
					} else {
						new_states[L2_BTN] = 1;
					}
				}
			}
		}
		
		int leftClickState = 0;
		int rightClickState = 0;
		if (has_kb_mapping) {
			for (int j = 0; j < NUM_BUTTONS; j++) {
				if (keyboard_mapping[j] != UNK_BTN) {
					if (analog_as_keys && j >= UP_BTN && j <= RIGHT_BTN) {
						if ((j == LEFT_BTN && pad.lx < 127 - ANALOG_DEADZONE) ||
							(j == RIGHT_BTN && pad.lx > 127 + ANALOG_DEADZONE) ||
							(j == UP_BTN && pad.ly < 127 - ANALOG_DEADZONE) ||
							(j == DOWN_BTN && pad.ly > 127 + ANALOG_DEADZONE)) {
							is_key_pressed[j] = 1;
							Java_com_yoyogames_runner_RunnerJNILib_KeyEvent(fake_env, 0, !is_key_pressed[j], keyboard_mapping[j], keyboard_mapping[j], 0x101);
						} else if (is_key_pressed[j] || new_states[j]) {
							is_key_pressed[j] = new_states[j];
							if (keyboard_mapping[j] == 0x01) // Left Mouse Click
								leftClickState = is_key_pressed[j];
							else if (keyboard_mapping[j] == 0x02) // Right Mouse Click
								rightClickState = is_key_pressed[j];
							else
								Java_com_yoyogames_runner_RunnerJNILib_KeyEvent(fake_env, 0, !is_key_pressed[j], keyboard_mapping[j], keyboard_mapping[j], 0x101);
						}
					} else if (is_key_pressed[j] || new_states[j]) {
						is_key_pressed[j] = new_states[j];
						if (keyboard_mapping[j] == 0x01) // Left Mouse Click
							leftClickState = is_key_pressed[j];
						else if (keyboard_mapping[j] == 0x02) // Right Mouse Click
							rightClickState = is_key_pressed[j];
						else
							Java_com_yoyogames_runner_RunnerJNILib_KeyEvent(fake_env, 0, !is_key_pressed[j], keyboard_mapping[j], keyboard_mapping[j], 0x101);
					}
				} else {
					yoyo_gamepads[i].buttons[j] = (double)update_button(new_states[j], (int)yoyo_gamepads[i].buttons[j]);
				}
			}
		} else {
			for (int j = 0; j < NUM_BUTTONS; j++) {
				yoyo_gamepads[i].buttons[j] = (double)update_button(new_states[j], (int)yoyo_gamepads[i].buttons[j]);
			}
		}
	
		yoyo_gamepads[i].axis[0] = (double)((int)pad.lx - 127) / 127.0f;
		yoyo_gamepads[i].axis[1] = (double)((int)pad.ly - 127) / 127.0f;
		yoyo_gamepads[i].axis[2] = (double)((int)pad.rx - 127) / 127.0f;
		yoyo_gamepads[i].axis[3] = (double)((int)pad.ry - 127) / 127.0f;
		
		static int oldMousePosX = SCREEN_W / 2;
		static int oldMousePosY = SCREEN_H / 2;
		if (analog_as_mouse == CAMERA_MODE) {
			if (pad.rx > 127 - ANALOG_DEADZONE && pad.rx < 127 + ANALOG_DEADZONE)
				*g_MousePosX = SCREEN_W / 2;
			else
				*g_MousePosX = (pad.rx * SCREEN_W) / 255;
			if (pad.ry > 127 - ANALOG_DEADZONE && pad.ry < 127 + ANALOG_DEADZONE)
				*g_MousePosY = SCREEN_H / 2;
			else
				*g_MousePosY = (pad.ry * SCREEN_H) / 255;
		} else if (analog_as_mouse == CURSOR_MODE) {
			if (pad.rx > 127 - ANALOG_DEADZONE && pad.rx < 127 + ANALOG_DEADZONE)
				*g_MousePosX = oldMousePosX;
			else {
				int normalized_x = (int)pad.rx - 127;
				*g_MousePosX += (normalized_x >> 2);
			}
			if (pad.ry > 127 - ANALOG_DEADZONE && pad.ry < 127 + ANALOG_DEADZONE)
				*g_MousePosY = oldMousePosY;
			else {
				int normalized_y = (int)pad.ry - 127;
				*g_MousePosY += (normalized_y >> 2);
			}
			if (*g_MousePosX < 0)
				*g_MousePosX = 0;
			else if (*g_MousePosX > SCREEN_W)	
				*g_MousePosX = SCREEN_W;
			if (*g_MousePosY < 0)
				*g_MousePosY = 0;
			else if (*g_MousePosY > SCREEN_H)
				*g_MousePosY = SCREEN_H;
			oldMousePosX = *g_MousePosX;
			oldMousePosY = *g_MousePosY;
		}
		if (leftClickState)
			*g_DoMouseButton = *g_DoMouseButton | 0x01;
		if (rightClickState)
			*g_DoMouseButton = *g_DoMouseButton | 0x80000002;
	}
}

void map_key(int key, const char *val) {
	if (strlen(val) > 1 && val[1] != '\r') {
		if (!strncmp("CODE", val, 4)) {
			keyboard_mapping[key] = (char)strtol(&val[4], NULL, 10);
			debugPrintf("Mapped button id %d to keycode %hhd.\n", key, keyboard_mapping[key]);
		} else if (!strncmp("MOUSE", &val[1], 5)) {
			has_click_emulation = 1;
			keyboard_mapping[key] = val[0] == 'L' ? 0x01 : 0x02;
			debugPrintf("Mapped button id %d to %s mouse click.\n", key, val[0] == 'L' ? "left" : "right");
		} else {
			for (int i = 0; i < sizeof(special_keys) / sizeof(special_keys[0]); i++) {
				if (strncmp(special_keys[i].key_name, val, strlen(special_keys[i].key_name)) == 0) {
					keyboard_mapping[key] = special_keys[i].key_value;
					debugPrintf("Mapped button id %d to key '%s'.\n", key, special_keys[i].key_name);
					break;
				}
			}
		}
	} else {
		if (val[0] == 'C') // C somehow doesn't seem to get properly detected so we fake it to another key instead and patch the check function
			keyboard_mapping[key] = 'O';
		else
			keyboard_mapping[key] = val[0];
		debugPrintf("Mapped button id %d to key '%c'\n", key, val[0]);
	}
}

void map_analog(int idx, const char *val) {
	if (strncmp("ON", val, 2) == 0) {
		if (idx == LEFT_ANALOG)
			analog_as_keys = 1;
		else {
			if (val[2] == '2')
				analog_as_mouse = CURSOR_MODE;
			else
				analog_as_mouse = CAMERA_MODE;
		}
	}
}

static void keyboard_check_pressed(retval_t *ret, void *self, void *other, int argc, retval_t *args) {
	int key = (int)args[0].rvalue.val;
	if (key == 'C')
		args[0].rvalue.val = 79.0f; // 'O' key
	CheckKeyPressed(ret, self, other, argc, args);
}

void IO_Update() {
	IO_UpdateM();
	GamepadUpdateM();
	ProcessVirtualKeys();
}

void patch_gamepad(const char *game_name) {
	IO_UpdateM = (void *)so_symbol(&yoyoloader_mod, "_Z10IO_UpdateMv");
	GamepadUpdateM = (void *)so_symbol(&yoyoloader_mod, "_Z14GamepadUpdateMv");
	ProcessVirtualKeys = (void *)so_symbol(&yoyoloader_mod, "_Z18ProcessVirtualKeysv");
	CheckKeyPressed = (void *)so_symbol(&yoyoloader_mod, "_Z17F_CheckKeyPressedR6RValueP9CInstanceS2_iPS_");
	CreateDsMap = (void *)so_symbol(&yoyoloader_mod, "_Z11CreateDsMapiz");
	CreateAsynEventWithDSMap = (void *)so_symbol(&yoyoloader_mod, "_Z24CreateAsynEventWithDSMapii");
	Java_com_yoyogames_runner_RunnerJNILib_KeyEvent = (void *)so_symbol(&yoyoloader_mod, "Java_com_yoyogames_runner_RunnerJNILib_KeyEvent");
	GamePadCheck(1);
	
	Function_Add("gamepad_is_supported", (intptr_t)gamepad_is_supported, 0, 1);
	Function_Add("gamepad_get_device_count", (intptr_t)gamepad_get_device_count, 0, 1);
	Function_Add("gamepad_is_connected", (intptr_t)gamepad_is_connected, 1, 1);
	Function_Add("gamepad_get_description", (intptr_t)gamepad_get_description, 1, 1);
	Function_Add("gamepad_get_button_threshold", (intptr_t)gamepad_get_button_threshold, 1, 1);
	Function_Add("gamepad_set_button_threshold", (intptr_t)gamepad_set_button_threshold, 2, 1);
	Function_Add("gamepad_get_axis_deadzone", (intptr_t)gamepad_get_axis_deadzone, 1, 1);
	Function_Add("gamepad_set_axis_deadzone", (intptr_t)gamepad_set_axis_deadzone, 2, 1);
	Function_Add("gamepad_button_count", (intptr_t)gamepad_button_count, 1, 1);
	Function_Add("gamepad_button_check", (intptr_t)gamepad_button_check, 2, 1);
	Function_Add("gamepad_button_check_pressed", (intptr_t)gamepad_button_check_pressed, 2, 1);
	Function_Add("gamepad_button_check_released", (intptr_t)gamepad_button_check_released, 2, 1);
	Function_Add("gamepad_button_value", (intptr_t)gamepad_button_value, 2, 1);
	Function_Add("gamepad_axis_count", (intptr_t)gamepad_axis_count, 1, 1);
	Function_Add("gamepad_axis_value", (intptr_t)gamepad_axis_value, 2, 1);
	Function_Add("gamepad_set_vibration", (intptr_t)gamepad_set_vibration, 3, 1);
	Function_Add("gamepad_set_color", (intptr_t)gamepad_set_colour, 2, 1);
	Function_Add("gamepad_set_colour", (intptr_t)gamepad_set_colour, 2, 1);
	hook_addr(so_symbol(&yoyoloader_mod, "_Z14GamePadRestartv"), (intptr_t)GamePadRestart);
	hook_addr(so_symbol(&yoyoloader_mod, "_Z9IO_Updatev"), (intptr_t)IO_Update);
	
	YYGetInt32 = (void *)so_symbol(&yoyoloader_mod, "_Z10YYGetInt32PK6RValuei");
	g_MousePosX = (int *)so_symbol(&yoyoloader_mod, "g_MousePosX");
	g_MousePosY = (int *)so_symbol(&yoyoloader_mod, "g_MousePosY");
	g_DoMouseButton = (int *)so_symbol(&yoyoloader_mod, "g_DoMouseButton");
	
	Function_Add("display_mouse_set", (intptr_t)mouse_set, 2, 0);
	Function_Add("window_mouse_set", (intptr_t)mouse_set, 2, 0);
	Function_Add("display_mouse_get_x", (intptr_t)mouse_get_x, 1, 0);
	Function_Add("display_mouse_get_y", (intptr_t)mouse_get_y, 1, 0);
	Function_Add("window_mouse_get_x", (intptr_t)mouse_get_x, 1, 0);
	Function_Add("window_mouse_get_y", (intptr_t)mouse_get_y, 1, 0);
	Function_Add("keyboard_check_pressed", (intptr_t)keyboard_check_pressed, 1, 0);
	
#ifdef STANDALONE_MODE
	FILE *f = fopen("app0:keys.ini", "r");
#else
	char fname[512];
	sprintf(fname, "ux0:data/gms/%s/keys.ini", game_name);
	FILE *f = fopen(fname, "r");
#endif
	if (f) {
		debugPrintf("Keyboard mapping file found!\n");
		sceClibMemset(keyboard_mapping, UNK_BTN, NUM_BUTTONS);
		char buffer[30], buffer2[30];
		while (EOF != fscanf(f, "%[^=]=%[^\n]\n", buffer, buffer2)) {
			if (strcmp("CROSS", buffer) == 0) map_key(CROSS_BTN, buffer2);
			else if (strcmp("CIRCLE", buffer) == 0) map_key(CIRCLE_BTN, buffer2);
			else if (strcmp("SQUARE", buffer) == 0) map_key(SQUARE_BTN, buffer2);
			else if (strcmp("TRIANGLE", buffer) == 0) map_key(TRIANGLE_BTN, buffer2);
			else if (strcmp("UP", buffer) == 0) map_key(UP_BTN, buffer2);
			else if (strcmp("DOWN", buffer) == 0) map_key(DOWN_BTN, buffer2);
			else if (strcmp("LEFT", buffer) == 0) map_key(LEFT_BTN, buffer2);
			else if (strcmp("RIGHT", buffer) == 0) map_key(RIGHT_BTN, buffer2);
			else if (strcmp("SELECT", buffer) == 0) map_key(SELECT_BTN, buffer2);
			else if (strcmp("START", buffer) == 0) map_key(START_BTN, buffer2);
			else if (strcmp("L1", buffer) == 0) map_key(L1_BTN, buffer2);
			else if (strcmp("R1", buffer) == 0) map_key(R1_BTN, buffer2);
			else if (strcmp("L2", buffer) == 0) map_key(L2_BTN, buffer2);
			else if (strcmp("R2", buffer) == 0) map_key(R2_BTN, buffer2);
			else if (strcmp("L3", buffer) == 0) map_key(L3_BTN, buffer2);
			else if (strcmp("R3", buffer) == 0) map_key(R3_BTN, buffer2);
			else if (strcmp("RANALOG", buffer) == 0) map_analog(RIGHT_ANALOG, buffer2);
			else if (strcmp("LANALOG", buffer) == 0) map_analog(LEFT_ANALOG, buffer2);
		}
		fclose(f);
		has_kb_mapping = 1;
	}
}
