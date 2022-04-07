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

#define IS_AXIS_BOUNDS (axis >= 0 && axis < 4)
#define IS_BTN_BOUNDS  (btn  >= 0 && btn  < 16)
#define IS_CONTROLLER_BOUNDS (id >= 0 && id < 4)

extern so_module gmsloader_mod;

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

typedef enum retval_type {
	VALUE_REAL = 0,
	VALUE_STRING = 1,
	VALUE_ARRAY = 2,
	VALUE_PTR = 3,
	VALUE_VEC3 = 4,
	VALUE_UNDEFINED = 5,
	VALUE_OBJECT = 6,
	VALUE_INT32 = 7,
	VALUE_VEC4 = 8,
	VALUE_MATRIX = 9,
	VALUE_INT64 = 10,
	VALUE_ACCESSOR = 11,
	VALUE_JSNULL = 12,
	VALUE_BOOL = 13,
	VALUE_ITERATOR = 14,
	VALUE_REF = 15,
	VALUE_UNSET = 0x0ffffff
} retval_type;

typedef struct ref_t {
	void *m_thing;
	int m_refCount;
	int m_size;
} ref_t;

typedef struct retval_t {
	union {
		int v32;
		long long v64;
		double val;
		ref_t *str;
	} rvalue;

	int flags;
	retval_type kind;
} retval_t;

Gamepad yoyo_gamepads[4];

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
	ret->rvalue.val = (id == 0) ? 1.0f : 0.0f;
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

void GamePadRestart() {
	int (*CreateDsMap)(int a1, char *type, int a3, int a4, char *desc, char *type2, double id, int a8) = (void *)so_symbol(&gmsloader_mod, "_Z11CreateDsMapiz");
	void (*CreateAsynEventWithDSMap)(int dsMap, int a2) = (void *)so_symbol(&gmsloader_mod, "_Z24CreateAsynEventWithDSMapii");

	for (int i = 0; i < 4; i++) {
		if (yoyo_gamepads[i].is_available) {
			int dsMap = CreateDsMap(2, "event_type", 0, 0, "gamepad discovered", "pad_index", (double)i, 0);
			CreateAsynEventWithDSMap(dsMap, 0x4b);
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
	SceCtrlData pad;
	
	// TODO: Add multiple controllers support
	sceCtrlPeekBufferPositiveExt2(0, &pad, 1);
	
	uint8_t new_states[16] = {};
	int k = 0;
	new_states[k++] = pad.buttons & SCE_CTRL_CROSS ? 1 : 0;
	new_states[k++] = pad.buttons & SCE_CTRL_CIRCLE ? 1 : 0;
	new_states[k++] = pad.buttons & SCE_CTRL_SQUARE ? 1 : 0;
	new_states[k++] = pad.buttons & SCE_CTRL_TRIANGLE ? 1 : 0;
	new_states[k++] = pad.buttons & SCE_CTRL_L1 ? 1 : 0;
	new_states[k++] = pad.buttons & SCE_CTRL_R1 ? 1 : 0;
	new_states[k++] = pad.buttons & SCE_CTRL_L2 ? 1 : 0;
	new_states[k++] = pad.buttons & SCE_CTRL_R2 ? 1 : 0;
	new_states[k++] = pad.buttons & SCE_CTRL_SELECT ? 1 : 0;
	new_states[k++] = pad.buttons & SCE_CTRL_START ? 1 : 0;
	new_states[k++] = pad.buttons & SCE_CTRL_L3 ? 1 : 0;
	new_states[k++] = pad.buttons & SCE_CTRL_R3 ? 1 : 0;
	new_states[k++] = pad.buttons & SCE_CTRL_UP ? 1 : 0;
	new_states[k++] = pad.buttons & SCE_CTRL_DOWN ? 1 : 0;
	new_states[k++] = pad.buttons & SCE_CTRL_LEFT ? 1 : 0;
	new_states[k++] = pad.buttons & SCE_CTRL_RIGHT ? 1 : 0;
	
	for (int j = 0; j < 16; j++) {
		yoyo_gamepads[0].buttons[j] = (double)update_button(new_states[j], (int)yoyo_gamepads[0].buttons[j]);
	}
	
	k = 0;
	yoyo_gamepads[0].axis[k++] = (double)((int)pad.lx - 127) / 127.0f;
	yoyo_gamepads[0].axis[k++] = (double)((int)pad.ly - 127) / 127.0f;
	yoyo_gamepads[0].axis[k++] = (double)((int)pad.rx - 127)	/ 127.0f;
	yoyo_gamepads[0].axis[k++] = (double)((int)pad.ry - 127) / 127.0f;
}

void patch_gamepad() {
	void (*Function_Add)(const char *name, intptr_t func, int argc, char ret) = (void *)so_symbol(&gmsloader_mod, "_Z12Function_AddPKcPFvR6RValueP9CInstanceS4_iPS1_Eib");
	if (Function_Add == NULL)
		Function_Add = (void *)so_symbol(&gmsloader_mod, "_Z12Function_AddPcPFvR6RValueP9CInstanceS3_iPS0_Eib");
	Function_Add("gamepad_is_supported", gamepad_is_supported, 0, 1);
	Function_Add("gamepad_get_device_count", gamepad_get_device_count, 0, 1);
	Function_Add("gamepad_is_connected", gamepad_is_connected, 1, 1);
	Function_Add("gamepad_get_description", gamepad_get_description, 1, 1);
	Function_Add("gamepad_get_button_threshold", gamepad_get_button_threshold, 1, 1);
	Function_Add("gamepad_set_button_threshold", gamepad_set_button_threshold, 2, 1);
	Function_Add("gamepad_get_axis_deadzone", gamepad_get_axis_deadzone, 1, 1);
	Function_Add("gamepad_set_axis_deadzone", gamepad_set_axis_deadzone, 2, 1);
	Function_Add("gamepad_button_count", gamepad_button_count, 1, 1);
	Function_Add("gamepad_button_check", gamepad_button_check, 2, 1);
	Function_Add("gamepad_button_check_pressed", gamepad_button_check_pressed, 2, 1);
	Function_Add("gamepad_button_check_released", gamepad_button_check_released, 2, 1);
	Function_Add("gamepad_button_value", gamepad_button_value, 2, 1);
	Function_Add("gamepad_axis_count", gamepad_axis_count, 1, 1);
	Function_Add("gamepad_axis_value", gamepad_axis_value, 2, 1);
	Function_Add("gamepad_set_vibration", gamepad_set_vibration, 3, 1);
	Function_Add("gamepad_set_color", gamepad_set_colour, 2, 1);
	Function_Add("gamepad_set_colour", gamepad_set_colour, 2, 1);
	hook_addr(so_symbol(&gmsloader_mod, "_Z14GamePadRestartv"), GamePadRestart);
}
