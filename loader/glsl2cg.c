/* glsl2cg.c -- GLSL to CG shaders translator
 *
 * Copyright (C) 2022 Rinnegatamante
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#include <vitasdk.h>
#include <stdio.h>
#include <stdlib.h>

#include "main.h"

enum {
	VARYING_TEXCOORD,
	VARYING_TEXCOORD3,
	VARYING_COLOR,
	VARYING_FOG
};

char *perform_static_analysis(const char *string, int size) {
	debugPrintf("glsl2cg: Static analysis pass started...\n");
	char *p = string;
	char *new_src = (char *)vglMalloc(0x8000);
	strcpy(new_src, "#define saturate(a) __saturate(a)\n#define texture __texture\n");
	char *p2 = &new_src[60];
	char *lowp = strstr(p, "lowp");
	char *mediump = strstr(p, "mediump");
	char *highp = strstr(p, "highp");
	char *precision = strstr(p, "precision");
	char *texture2d = strstr(p, "texture2D");
	char *fract = strstr(p, "fract(");
	char *mix = strstr(p, "mix(");
	char *vec2 = strstr(p, "vec2");
	char *vec3 = strstr(p, "vec3");
	char *vec4 = strstr(p, "vec4");
	char *mat2 = strstr(p, "mat2");
	char *mat3 = strstr(p, "mat3");
	char *mat4 = strstr(p, "mat4");
	char *modu = strstr(p, "mod(");
	char *atan = strstr(p, "atan(");
	char *cons = strstr(p, "const ");
	for (;;) {
		// Updating any symbol that requires such
		lowp = (lowp != NULL && lowp < p) ? strstr(p, "lowp") : lowp;
		mediump = (mediump != NULL && mediump < p) ? strstr(p, "mediump") : mediump;
		highp = (highp != NULL && highp < p) ? strstr(p, "highp") : highp;
		precision = (precision != NULL && precision < p) ? strstr(p, "precision") : precision;
		texture2d = (texture2d != NULL && texture2d < p) ? strstr(p, "texture2D") : texture2d;
		fract = (fract != NULL && fract < p) ? strstr(p, "fract(") : fract;
		mix = (mix != NULL && mix < p) ? strstr(p, "mix(") : mix;
		vec2 = (vec2 != NULL && vec2 < p) ? strstr(p, "vec2") : vec2;
		vec3 = (vec3 != NULL && vec3 < p) ? strstr(p, "vec3") : vec3;
		vec4 = (vec4 != NULL && vec4 < p) ? strstr(p, "vec4") : vec4;
		mat2 = (mat2 != NULL && mat2 < p) ? strstr(p, "mat2") : mat2;
		mat3 = (mat3 != NULL && mat3 < p) ? strstr(p, "mat3") : mat3;
		mat4 = (mat4 != NULL && mat4 < p) ? strstr(p, "mat4") : mat4;
		modu = (modu != NULL && modu < p) ? strstr(p, "mod(") : modu;
		atan = (atan != NULL && atan < p) ? strstr(p, "atan(") : atan;
		cons = (cons != NULL && cons < p) ? strstr(p, "const ") : cons;
		
		// Detecting closest symbol
		char *lower_symbol = ((lowp < mediump && lowp != NULL) || mediump == NULL) ? lowp : mediump;
		lower_symbol = ((highp < lower_symbol && highp != NULL) || lower_symbol == NULL) ? highp : lower_symbol;
		lower_symbol = ((precision < lower_symbol && precision != NULL) || lower_symbol == NULL) ? precision : lower_symbol;
		lower_symbol = ((texture2d < lower_symbol && texture2d != NULL) || lower_symbol == NULL) ? texture2d : lower_symbol;
		lower_symbol = ((fract < lower_symbol && fract != NULL) || lower_symbol == NULL) ? fract : lower_symbol;
		lower_symbol = ((mix < lower_symbol && mix != NULL) || lower_symbol == NULL) ? mix : lower_symbol;
		lower_symbol = ((vec2 < lower_symbol && vec2 != NULL) || lower_symbol == NULL) ? vec2 : lower_symbol;
		lower_symbol = ((vec3 < lower_symbol && vec3 != NULL) || lower_symbol == NULL) ? vec3 : lower_symbol;
		lower_symbol = ((vec4 < lower_symbol && vec4 != NULL) || lower_symbol == NULL) ? vec4 : lower_symbol;
		lower_symbol = ((mat2 < lower_symbol && mat2 != NULL) || lower_symbol == NULL) ? mat2 : lower_symbol;
		lower_symbol = ((mat3 < lower_symbol && mat3 != NULL) || lower_symbol == NULL) ? mat3 : lower_symbol;
		lower_symbol = ((mat4 < lower_symbol && mat4 != NULL) || lower_symbol == NULL) ? mat4 : lower_symbol;
		lower_symbol = ((modu < lower_symbol && modu != NULL) || lower_symbol == NULL) ? modu : lower_symbol;
		lower_symbol = ((atan < lower_symbol && atan != NULL) || lower_symbol == NULL) ? atan : lower_symbol;
		lower_symbol = ((cons < lower_symbol && cons != NULL) || lower_symbol == NULL) ? cons : lower_symbol;
		
		// Handling symbol
		if (!lower_symbol) {
			memcpy(p2, p, string + size - p);
			p2 += string + size - p;
			p2[0] = 0;
			break;
		} else {
			if (lower_symbol == cons) {
				memcpy(p2, p, lower_symbol - p);
				p2 += lower_symbol - p;
				p2[0] = 's';
				p2[1] = 't';
				p2[2] = 'a';
				p2[3] = 't';
				p2[4] = 'i';
				p2[5] = 'c';
				p2 += 6;
				p = lower_symbol + 5;
			} else if (lower_symbol == atan) {
				memcpy(p2, p, lower_symbol - p + 4);
				p2 += lower_symbol - p + 4;
				p2[0] = '2';
				p2++;
				p = lower_symbol + 4;
			} else if (lower_symbol == modu) {
				memcpy(p2, p, lower_symbol - p);
				p2 += lower_symbol - p;
				p2[0] = 'f';
				p2++;
				memcpy(p2, lower_symbol, 4);
				p2 += 4;
				p = lower_symbol + 4;
			} else if (lower_symbol == lowp) {
				memcpy(p2, p, lower_symbol - p);
				p2 += lower_symbol - p;
				p = lower_symbol + 4;
			} else if (lower_symbol == mediump) {
				memcpy(p2, p, lower_symbol - p);
				p2 += lower_symbol - p;
				p = lower_symbol + 7;
			} else if (lower_symbol == highp) {
				memcpy(p2, p, lower_symbol - p);
				p2 += lower_symbol - p;
				p = lower_symbol + 5;
			} else if (lower_symbol == precision) {
				memcpy(p2, p, lower_symbol - p);
				p2 += lower_symbol - p;
				p2[0] = '/';
				p2[1] = '/';
				p2[2] = 'p';
				p2 += 3;
				p = lower_symbol + 1;
			} else if (lower_symbol == texture2d) {
				lower_symbol += 3;
				memcpy(p2, p, lower_symbol - p);
				p2 += lower_symbol - p;
				p2[0] = '2';
				p2[1] = 'D';
				p2 += 2;
				p = lower_symbol + 6;
			} else if (lower_symbol == fract) {
				lower_symbol += 4;
				memcpy(p2, p, lower_symbol - p);
				p2 += lower_symbol - p;
				p = lower_symbol + 1;
			} else if (lower_symbol == mix) {
				memcpy(p2, p, lower_symbol - p);
				p2 += lower_symbol - p;
				p2[0] = 'l';
				p2[1] = 'e';
				p2[2] = 'r';
				p2[3] = 'p';
				p2 += 4;
				p = lower_symbol + 3;
			} else if (lower_symbol == vec2) {
				memcpy(p2, p, lower_symbol - p);
				p2 += lower_symbol - p;
				p2[0] = 'f';
				p2[1] = 'l';
				p2[2] = 'o';
				p2[3] = 'a';
				p2[4] = 't';
				p2[5] = '2';
				p2 += 6;
				p = lower_symbol + 4;
			} else if (lower_symbol == vec3) {
				memcpy(p2, p, lower_symbol - p);
				p2 += lower_symbol - p;
				p2[0] = 'f';
				p2[1] = 'l';
				p2[2] = 'o';
				p2[3] = 'a';
				p2[4] = 't';
				p2[5] = '3';
				p2 += 6;
				p = lower_symbol + 4;
			} else if (lower_symbol == vec4) {
				memcpy(p2, p, lower_symbol - p);
				p2 += lower_symbol - p;
				p2[0] = 'f';
				p2[1] = 'l';
				p2[2] = 'o';
				p2[3] = 'a';
				p2[4] = 't';
				p2[5] = '4';
				p2 += 6;
				p = lower_symbol + 4;
			} else if (lower_symbol == mat2) {
				memcpy(p2, p, lower_symbol - p);
				p2 += lower_symbol - p;
				p2[0] = 'f';
				p2[1] = 'l';
				p2[2] = 'o';
				p2[3] = 'a';
				p2[4] = 't';
				p2[5] = '2';
				p2[6] = 'x';
				p2[7] = '2';
				p2 += 8;
				p = lower_symbol + 4;
			} else if (lower_symbol == mat3) {
				memcpy(p2, p, lower_symbol - p);
				p2 += lower_symbol - p;
				p2[0] = 'f';
				p2[1] = 'l';
				p2[2] = 'o';
				p2[3] = 'a';
				p2[4] = 't';
				p2[5] = '3';
				p2[6] = 'x';
				p2[7] = '3';
				p2 += 8;
				p = lower_symbol + 4;
			} else if (lower_symbol == mat4) {
				memcpy(p2, p, lower_symbol - p);
				p2 += lower_symbol - p;
				p2[0] = 'f';
				p2[1] = 'l';
				p2[2] = 'o';
				p2[3] = 'a';
				p2[4] = 't';
				p2[5] = '4';
				p2[6] = 'x';
				p2[7] = '4';
				p2 += 8;
				p = lower_symbol + 4;
			}
		}
	}
	
	return new_src;
}

char *translate_frag_shader(const char *string, int size) {
	debugPrintf("glsl2cg: Attempting to automatically translate the fragment shader...\n");
	
	// Static analysis
	char *new_src = perform_static_analysis(string, size);
	
	// Detecting main function
	debugPrintf("glsl2cg: Locating main function...\n");
	char *p = new_src;
	char *main_f = strstr(p, "void main()");
	if (!main_f)
		main_f = strstr(p, "void main(void)");
	char varyings[32][32];
	int varyings_type[32];
	int num_varyings = 0;
	char *new_src2 = (char *)vglMalloc(0x8000);
	char *p3 = new_src2;
	char *last_end = NULL;
	char *p2 = strstr(p, "varying");
	if (p2) {
		memcpy(p3, p, p2 - p);
		p3 += p2 - p;
	
		// Analyzing varyings
		debugPrintf("glsl2cg: Analyzing varyings...\n");
		for (;;) {
			p2 = strstr(p, "varying");
			if (p2) {
				char *end = strstr(p2, ";");
				last_end = end;
				char *s = strstr(p2, " ") + 1;
				for (;;) {
					if (!strncmp(s, "float2", 6)) {
						varyings_type[num_varyings] = VARYING_TEXCOORD;
					} else if (!strncmp(s, "float3", 6)) {
						varyings_type[num_varyings] = VARYING_TEXCOORD3;
					} else if (!strncmp(s, "float4", 6)) {
						varyings_type[num_varyings] = VARYING_COLOR;
					} else if (!strncmp(s, "float", 5)) {
						varyings_type[num_varyings] = VARYING_FOG;
					}
					char *space = strstr(s, " ");
					if (space > end) {
						memcpy(varyings[num_varyings], s, end - s);
						varyings[num_varyings][end - s] = 0;
						break;
					} else {
						s = space + 1;
					}
				}
				num_varyings++;
				p = p2 + 1;
			} else {
				break;
			}
		}
	} else {
		last_end = p - 1;
	}
	
	// Rewriting main function
	debugPrintf("glsl2cg: Rewriting main function...\n");
	last_end++;
	memcpy(p3, last_end, main_f - last_end + 10);
	p3 += main_f - last_end + 10;
	const char *fragcolor = "float4 out gl_FragColor : COLOR, float4 gl_FragCoord : WPOS";
	memcpy(p3, fragcolor, strlen(fragcolor));
	p3 += strlen(fragcolor);
	int texcoord_id = 0;
	for (int i = 0; i < num_varyings; i++) {
		char var[64] = {0};
		switch (varyings_type[i]) {
		case VARYING_FOG:
			sprintf(var, ",float %s : FOG", varyings[i]);
			break;
		case VARYING_TEXCOORD:
			sprintf(var, ",float2 %s : TEXCOORD%d", varyings[i], texcoord_id++);
			break;
		case VARYING_TEXCOORD3:
			sprintf(var, ",float3 %s : TEXCOORD%d", varyings[i], texcoord_id++);
			break;
		case VARYING_COLOR:
			sprintf(var, ",float4 %s : COLOR", varyings[i]);
			break;
		default:
			debugPrintf("Invalid varying detected for %s\n", varyings[i]);
			break;
		}
		memcpy(p3, var, strlen(var));
		p3 += strlen(var);
	}
	if (main_f[10] == 'v')
		main_f += 4;
	memcpy(p3, main_f + 10, strlen(new_src) - ((main_f + 10) - new_src));
	p3 += strlen(new_src) - ((main_f + 10) - new_src);
	p3[0] = 0;
	
	debugPrintf("glsl2cg: Translation process completed!\n");
	vglFree(new_src);
	return new_src2;
}

char *translate_vert_shader(char *string, int size) {
	debugPrintf("glsl2cg: Attempting to automatically translate the vertex shader...\n");
	
	// Static analysis
	char *new_src = perform_static_analysis(string, size);
	
	// Detecting main function
	debugPrintf("glsl2cg: Locating main function...\n");
	char *p = new_src;
	char *main_f = strstr(p, "void main()");
	if (!main_f)
		main_f = strstr(p, "void main(void)");
	char varyings[32][32];
	char attributes[32][32];
	int varyings_type[32];
	int num_varyings = 0;
	int num_attributes = 0;
	char *new_src2 = (char *)vglMalloc(0x8000);
	char *p3 = new_src2;
	char *last_end = NULL;
	char *p2 = strstr(p, "attribute");
	memcpy(p3, p, p2 - p);
	p3 += p2 - p;
	
	// Analyzing varyings
	debugPrintf("glsl2cg: Analyzing attributes...\n");
	for (;;) {
		p2 = strstr(p, "attribute");
		if (p2) {
			char *end = strstr(p2, ";");
			last_end = end;
			char *s = strstr(p2, " ") + 1;
			memcpy(attributes[num_attributes], s, end - s);
			attributes[num_attributes][end - s] = 0;
			num_attributes++;
			p = p2 + 1;
		} else {
			break;
		}
	}
	p2 = strstr(p, "varying");
	if (p2) {
		last_end++;
		memcpy(p3, last_end, p2 - last_end);
		p3 += p2 - last_end;
	
		// Analyzing attributes
		debugPrintf("glsl2cg: Analyzing varyings...\n");
		for (;;) {
			p2 = strstr(p, "varying");
			if (p2) {
				char *end = strstr(p2, ";");
				last_end = end;
				char *s = strstr(p2, " ") + 1;
				for (;;) {
					if (!strncmp(s, "float2", 6)) {
						varyings_type[num_varyings] = VARYING_TEXCOORD;
					} else if (!strncmp(s, "float3", 6)) {
						varyings_type[num_varyings] = VARYING_TEXCOORD3;
					} else if (!strncmp(s, "float4", 6)) {
						varyings_type[num_varyings] = VARYING_COLOR;
					} else if (!strncmp(s, "float", 5)) {
						varyings_type[num_varyings] = VARYING_FOG;
					}
					char *space = strstr(s, " ");
					if (space > end) {
						memcpy(varyings[num_varyings], s, end - s);
						varyings[num_varyings][end - s] = 0;
						break;
					} else {
						s = space + 1;
					}
				}
				num_varyings++;
				p = p2 + 1;
			} else {
				break;
			}
		}
	}
	
	// Rewriting main function
	debugPrintf("glsl2cg: Rewriting main function...\n");
	last_end++;
	memcpy(p3, last_end, main_f - last_end + 10);
	p3 += main_f - last_end + 10;
	const char *header = "float4 out gl_Position : POSITION, float out gl_PointSize : PSIZE";
	memcpy(p3, header, strlen(header));
	p3 += strlen(header);
	for (int i = 0; i < num_attributes; i++) {
		p3[0] = ',';
		p3++;
		memcpy(p3, attributes[i], strlen(attributes[i]));
		p3 += strlen(attributes[i]);
	}
	int texcoord_id = 0;
	for (int i = 0; i < num_varyings; i++) {
		char var[64] = {0};
		switch (varyings_type[i]) {
		case VARYING_FOG:
			sprintf(var, ",float out %s : FOG", varyings[i]);
			break;
		case VARYING_TEXCOORD:
			sprintf(var, ",float2 out %s : TEXCOORD%d", varyings[i], texcoord_id++);
			break;
		case VARYING_TEXCOORD3:
			sprintf(var, ",float3 out %s : TEXCOORD%d", varyings[i], texcoord_id++);
			break;
		case VARYING_COLOR:
			sprintf(var, ",float4 out %s : COLOR", varyings[i]);
			break;
		default:
			debugPrintf("Invalid varying detected for %s\n", varyings[i]);
			break;
		}
		memcpy(p3, var, strlen(var));
		p3 += strlen(var);
	}
	if (main_f[10] == 'v')
		main_f += 4;
	memcpy(p3, main_f + 10, strlen(new_src) - ((main_f + 10) - new_src));
	p3 += strlen(new_src) - ((main_f + 10) - new_src);
	p3[0] = 0;
	vglFree(new_src);
	
	// Handling matrices multiplications
	p = new_src2;
	p2 = strstr(p, "gm_Matrices") + 11; // Skipping first occurrance since it's its declaration
	if (p2 == (char *)11) { // No gm_Matrices, probably some internal shader (?)
		debugPrintf("glsl2cg: Translation process completed!\n");	
		return new_src2;
	}
	debugPrintf("glsl2cg: Patching matrices operations...\n");
	new_src = (char *)vglMalloc(0x8000);
	char *p5 = new_src;
	for (;;) {
		p2 = strstr(p2, "gm_Matrices");
		if (p2) {
			memcpy(p5, p, p2 - p);
			p5 += p2 - p;
			p3 = strstr(p2, "]") + 1;
			char *p4 = strstr(p3, "*");
			if (p4 - p3 <= 3) {
				p4++;
				p5[0] = 'm';
				p5[1] = 'u';
				p5[2] = 'l';
				p5[3] = '(';
				p5 += 4;
				char *p6 = strstr(p4, ";");
				char *p7 = strstr(p4, ")");
				char *p8 = strstr(p4, "(");
				if (p8 != NULL && p8 < p7) p7 = p6;
		        p6 = (p6 < p7 || p7 == NULL) ? p6 : p7;
				memcpy(p5, p4, p6 - p4);
				p5 += p6 - p4;
				p5[0] = ',';
				p5++;
				memcpy(p5, p2, p3 - p2);
				p5 += p3 - p2;
				p5[0] = ')';
				p5++;
				p = p6;
			}
			p2++;
		} else {
			break;
		}
	}
	memcpy(p5, p, strlen(new_src2) - (p - new_src2));
	p5 += strlen(new_src2) - (p - new_src2);
	p5[0] = 0;
	debugPrintf("glsl2cg: Translation process completed!\n");
	vglFree(new_src2);
	return new_src;
}
