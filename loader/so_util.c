/* so_util.c -- utils to load and hook .so modules
 *
 * Copyright (C) 2021 Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.	See the LICENSE file for details.
 */

#include <vitasdk.h>
#include <kubridge.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "dialog.h"
#include "so_util.h"

#ifndef SCE_KERNEL_MEMBLOCK_TYPE_USER_RX
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RX (0x0C20D050)
#endif

typedef struct b_enc {
	union {
		struct __attribute__((__packed__)) {
			int imm24: 24;
			unsigned int l: 1; // Branch with Link flag
			unsigned int enc: 3; // 0b101
			unsigned int cond: 4; // 0b1110
		} bits;
		uint32_t raw;
	};
} b_enc;

typedef struct ldst_enc {
	union {
		struct __attribute__((__packed__)) {
			int imm12: 12;
			unsigned int rt: 4; // Source/Destination register
			unsigned int rn: 4; // Base register
			unsigned int bit20_1: 1; // 0: store to memory, 1: load from memory
			unsigned int w: 1; // 0: no write-back, 1: write address into base
			unsigned int b: 1; // 0: word, 1: byte
			unsigned int u: 1; // 0: subtract offset from base, 1: add to base
			unsigned int p: 1; // 0: post indexing, 1: pre indexing
			unsigned int enc: 3;
			unsigned int cond: 4;
		} bits;
		uint32_t raw;
	};
} ldst_enc;

#define B_RANGE ((1 << 24) - 1)
#define B_OFFSET(x) (x + 8) // branch jumps into addr - 8, so range is biased forward
#define B(PC, DEST) ((b_enc){.bits = {.cond = 0b1110, .enc = 0b101, .l = 0, .imm24 = (((intptr_t)DEST-(intptr_t)PC) / 4) - 2}})
#define LDR_OFFS(RT, RN, IMM) ((ldst_enc){.bits = {.cond = 0b1110, .enc = 0b010, .p = 1, .u = (IMM >= 0), .b = 0, .w = 0, .bit20_1 = 1, .rn = RN, .rt = RT, .imm12 = (IMM >= 0) ? IMM : -IMM}})

#define PATCH_SZ 0x10000 //64 KB-ish arenas
static so_module *head = NULL, *tail = NULL;

so_hook hook_thumb(uintptr_t addr, uintptr_t dst) {
	so_hook h;
	//printf("THUMB HOOK\n");
	if (addr == 0)
		return;
	h.thumb_addr = addr;
	addr &= ~1;
	if (addr & 2) {
		uint16_t nop = 0xbf00;
		kuKernelCpuUnrestrictedMemcpy((void *)addr, &nop, sizeof(nop));
		addr += 2;
		//printf("THUMB UNALIGNED\n");
	}
	
	h.addr = addr;
	h.patch_instr[0] = 0xf000f8df; // LDR PC, [PC]
	h.patch_instr[1] = dst;
	kuKernelCpuUnrestrictedMemcpy(&h.orig_instr, (void *)addr, sizeof(h.orig_instr));
	kuKernelCpuUnrestrictedMemcpy((void *)addr, h.patch_instr, sizeof(h.patch_instr));

	return h;
}

so_hook hook_arm(uintptr_t addr, uintptr_t dst) {
	//printf("ARM HOOK\n");
	if (addr == 0)
		return;
	so_hook h;
	h.thumb_addr = 0;
	h.addr = addr;
	h.patch_instr[0] = 0xe51ff004; // LDR PC, [PC, #-0x4]
	h.patch_instr[1] = dst;
	kuKernelCpuUnrestrictedMemcpy(&h.orig_instr, (void *)addr, sizeof(h.orig_instr));
	kuKernelCpuUnrestrictedMemcpy((void *)addr, h.patch_instr, sizeof(h.patch_instr));

	return h;
}

so_hook hook_addr(uintptr_t addr, uintptr_t dst) {
	if (addr == 0)
		return;
	if (addr & 1)
		return hook_thumb(addr, dst);
	else
		return hook_arm(addr, dst);
}

void so_flush_caches(so_module *mod) {
	kuKernelFlushCaches((void *)mod->text_base, mod->text_size);
}

int _so_load(so_module *mod, SceUID so_blockid, void *so_data, uintptr_t load_addr) {
	int res = 0;
	uintptr_t data_addr = 0;
	
	if (memcmp(so_data, ELFMAG, SELFMAG) != 0) {
		res = -1;
		goto err_free_so;
	}

	mod->ehdr = (Elf32_Ehdr *)so_data;
	mod->phdr = (Elf32_Phdr *)((uintptr_t)so_data + mod->ehdr->e_phoff);
	mod->shdr = (Elf32_Shdr *)((uintptr_t)so_data + mod->ehdr->e_shoff);

	mod->shstr = (char *)((uintptr_t)so_data + mod->shdr[mod->ehdr->e_shstrndx].sh_offset);

	for (int i = 0; i < mod->ehdr->e_phnum; i++) {
		if (mod->phdr[i].p_type == PT_LOAD) {
			void *prog_data;
			size_t prog_size;

			if ((mod->phdr[i].p_flags & PF_X) == PF_X) {
				// Allocate arena for code patches, trampolines, etc
				// Sits exactly under the desired allocation space
				mod->patch_size = ALIGN_MEM(PATCH_SZ, mod->phdr[i].p_align);
				SceKernelAllocMemBlockKernelOpt opt;
				memset(&opt, 0, sizeof(SceKernelAllocMemBlockKernelOpt));
				opt.size = sizeof(SceKernelAllocMemBlockKernelOpt);
				opt.attr = 0x1;
				opt.field_C = (SceUInt32)load_addr - mod->patch_size;
				res = mod->patch_blockid = kuKernelAllocMemBlock("rx_block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RX, mod->patch_size, &opt);
				if (res < 0) {
					debugPrintf("Failed at allocating a patch block of %d bytes on address 0x%08X\n", mod->patch_size, opt.field_C);
					goto err_free_so;
				} else {
					debugPrintf("Patch Block: Addr: 0x%08X Size: %d\n", opt.field_C, mod->patch_size);
				}

				sceKernelGetMemBlockBase(mod->patch_blockid, &mod->patch_base);
				mod->patch_head = mod->patch_base;
				
				prog_size = ALIGN_MEM(mod->phdr[i].p_memsz, mod->phdr[i].p_align);
				memset(&opt, 0, sizeof(SceKernelAllocMemBlockKernelOpt));
				opt.size = sizeof(SceKernelAllocMemBlockKernelOpt);
				opt.attr = 0x1;
				opt.field_C = (SceUInt32)load_addr;
				res = mod->text_blockid = kuKernelAllocMemBlock("rx_block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RX, prog_size, &opt);
				if (res < 0) {
					debugPrintf("Failed at allocating a prog block of %d bytes on address 0x%08X\n", prog_size, opt.field_C);
					goto err_free_so;
				} else {
					debugPrintf("Prog Block: Addr: 0x%08X Size: %d\n", opt.field_C, prog_size);
				}

				sceKernelGetMemBlockBase(mod->text_blockid, &prog_data);

				mod->phdr[i].p_vaddr += (Elf32_Addr)prog_data;

				mod->text_base = mod->phdr[i].p_vaddr;
				mod->text_size = mod->phdr[i].p_memsz;
		
				// Use the .text segment padding as a code cave
				// Word-align it to make it simpler for instruction arena allocation
				mod->cave_size = ALIGN_MEM(prog_size - mod->phdr[i].p_memsz, 0x4);
				mod->cave_base = mod->cave_head = prog_data + mod->phdr[i].p_memsz;
				mod->cave_base = ALIGN_MEM(mod->cave_base, 0x4);
				mod->cave_head = mod->cave_base;
				debugPrintf("code cave: %d bytes (@0x%08X).\n", mod->cave_size, mod->cave_base);

				data_addr = (uintptr_t)prog_data + prog_size;
			} else {
				if (data_addr == 0)
					goto err_free_so;

				if (mod->n_data >= MAX_DATA_SEG)
					goto err_free_data;

				prog_size = ALIGN_MEM(mod->phdr[i].p_memsz + mod->phdr[i].p_vaddr - (data_addr - mod->text_base), mod->phdr[i].p_align);

				SceKernelAllocMemBlockKernelOpt opt;
				memset(&opt, 0, sizeof(SceKernelAllocMemBlockKernelOpt));
				opt.size = sizeof(SceKernelAllocMemBlockKernelOpt);
				opt.attr = 0x1;
				opt.field_C = (SceUInt32)data_addr;
				res = mod->data_blockid[mod->n_data] = kuKernelAllocMemBlock("rw_block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, prog_size, &opt);
				if (res < 0) {
					debugPrintf("Failed at allocating a rw block of %d bytes on address 0x%08X\n", prog_size, opt.field_C);
					goto err_free_text;
				} else {
					debugPrintf("RW Block: Addr: 0x%08X Size: %d\n", opt.field_C, prog_size);
				}

				sceKernelGetMemBlockBase(mod->data_blockid[mod->n_data], &prog_data);
				data_addr = (uintptr_t)prog_data + prog_size;

				mod->phdr[i].p_vaddr += (Elf32_Addr)mod->text_base;

				mod->data_base[mod->n_data] = mod->phdr[i].p_vaddr;
				mod->data_size[mod->n_data] = mod->phdr[i].p_memsz;
				mod->n_data++;
			}

			char *zero = malloc(prog_size - mod->phdr[i].p_filesz);
			memset(zero, 0, prog_size - mod->phdr[i].p_filesz);
			kuKernelCpuUnrestrictedMemcpy(prog_data + mod->phdr[i].p_filesz, zero, prog_size - mod->phdr[i].p_filesz);
			free(zero);

			kuKernelCpuUnrestrictedMemcpy((void *)mod->phdr[i].p_vaddr, (void *)((uintptr_t)so_data + mod->phdr[i].p_offset), mod->phdr[i].p_filesz);
		}
	}

	for (int i = 0; i < mod->ehdr->e_shnum; i++) {
		char *sh_name = mod->shstr + mod->shdr[i].sh_name;
		uintptr_t sh_addr = mod->text_base + mod->shdr[i].sh_addr;
		size_t sh_size = mod->shdr[i].sh_size;
		if (strcmp(sh_name, ".dynamic") == 0) {
			mod->dynamic = (Elf32_Dyn *)sh_addr;
			mod->num_dynamic = sh_size / sizeof(Elf32_Dyn);
		} else if (strcmp(sh_name, ".dynstr") == 0) {
			mod->dynstr = (char *)sh_addr;
		} else if (strcmp(sh_name, ".dynsym") == 0) {
			mod->dynsym = (Elf32_Sym *)sh_addr;
			mod->num_dynsym = sh_size / sizeof(Elf32_Sym);
		} else if (strcmp(sh_name, ".rel.dyn") == 0) {
			mod->reldyn = (Elf32_Rel *)sh_addr;
			mod->num_reldyn = sh_size / sizeof(Elf32_Rel);
		} else if (strcmp(sh_name, ".rel.plt") == 0) {
			mod->relplt = (Elf32_Rel *)sh_addr;
			mod->num_relplt = sh_size / sizeof(Elf32_Rel);
		} else if (strcmp(sh_name, ".init_array") == 0) {
			mod->init_array = (void *)sh_addr;
			mod->num_init_array = sh_size / sizeof(void *);
		} else if (strcmp(sh_name, ".hash") == 0) {
			mod->hash = (void *)sh_addr;
		}
	}

	if (mod->dynamic == NULL ||
		mod->dynstr == NULL ||
		mod->dynsym == NULL ||
		mod->reldyn == NULL ||
		mod->relplt == NULL) {
		res = -2;
		goto err_free_data;
	}

	for (int i = 0; i < mod->num_dynamic; i++) {
		switch (mod->dynamic[i].d_tag) {
		case DT_SONAME:
			mod->soname = mod->dynstr + mod->dynamic[i].d_un.d_ptr;
			break;
		default:
			break;
		}
	}

	sceKernelFreeMemBlock(so_blockid);

	if (!head && !tail) {
		head = mod;
		tail = mod;
	} else {
		tail->next = mod;
		tail = mod;
	}

	return 0;

err_free_data:
	for (int i = 0; i < mod->n_data; i++)
		sceKernelFreeMemBlock(mod->data_blockid[i]);
err_free_text:
	sceKernelFreeMemBlock(mod->text_blockid);
err_free_so:
	sceKernelFreeMemBlock(so_blockid);

	return res;
}

int so_mem_load(so_module *mod, void *buffer, size_t so_size, uintptr_t load_addr) {
	SceUID so_blockid;
	void *so_data;

	memset(mod, 0, sizeof(so_module));

	so_blockid = sceKernelAllocMemBlock("so block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, (so_size + 0xfff) & ~0xfff, NULL);
	if (so_blockid < 0)
		return so_blockid;

	sceKernelGetMemBlockBase(so_blockid, &so_data);
	sceClibMemcpy(so_data, buffer, so_size);
	
	return _so_load(mod, so_blockid, so_data, load_addr);
}

int so_file_load(so_module *mod, const char *filename, uintptr_t load_addr) {
	SceUID so_blockid;
	void *so_data;

	memset(mod, 0, sizeof(so_module));

	SceUID fd = sceIoOpen(filename, SCE_O_RDONLY, 0);
	if (fd < 0)
		return fd;

	size_t so_size = sceIoLseek(fd, 0, SCE_SEEK_END);
	sceIoLseek(fd, 0, SCE_SEEK_SET);

	so_blockid = sceKernelAllocMemBlock("so block", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, (so_size + 0xfff) & ~0xfff, NULL);
	if (so_blockid < 0)
		return so_blockid;

	sceKernelGetMemBlockBase(so_blockid, &so_data);

	sceIoRead(fd, so_data, so_size);
	sceIoClose(fd);

	return _so_load(mod, so_blockid, so_data, load_addr);
}

int so_relocate(so_module *mod) {
	for (int i = 0; i < mod->num_reldyn + mod->num_relplt; i++) {
		Elf32_Rel *rel = i < mod->num_reldyn ? &mod->reldyn[i] : &mod->relplt[i - mod->num_reldyn];
		Elf32_Sym *sym = &mod->dynsym[ELF32_R_SYM(rel->r_info)];
		uintptr_t *ptr = (uintptr_t *)(mod->text_base + rel->r_offset);

		int type = ELF32_R_TYPE(rel->r_info);
		switch (type) {
		case R_ARM_ABS32:
			if (sym->st_shndx != SHN_UNDEF)
				*ptr += mod->text_base + sym->st_value;
			break;
		case R_ARM_RELATIVE:
			*ptr += mod->text_base;
			break;
		case R_ARM_GLOB_DAT:
		case R_ARM_JUMP_SLOT:
		{
			if (sym->st_shndx != SHN_UNDEF)
				*ptr = mod->text_base + sym->st_value;
			break;
		}
		default:
			fatal_error("Error unknown relocation type %x\n", type);
			break;
		}
	}

	return 0;
}

uintptr_t so_resolve_link(so_module *mod, const char *symbol) {
	for (int i = 0; i < mod->num_dynamic; i++) {
		switch (mod->dynamic[i].d_tag) {
		case DT_NEEDED:
		{
			so_module *curr = head;
			while (curr) {
				if (curr != mod && strcmp(curr->soname, mod->dynstr + mod->dynamic[i].d_un.d_ptr) == 0) {
					uintptr_t link = so_symbol(curr, symbol);
					if (link)
						return link;
				}
				curr = curr->next;
			}

			break;
		}
		default:
			break;
		}
	}

	return 0;
}

void reloc_err(uintptr_t got0)
{
	// Find to which module this missing symbol belongs
	int found = 0;
	so_module *curr = head;
	while (curr && !found) {
		for (int i = 0; i < curr->n_data; i++)
			if ((got0 >= curr->data_base[i]) && (got0 <= (uintptr_t)(curr->data_base[i] + curr->data_size[i])))
				found = 1;
		
		if (!found)
			curr = curr->next;
	}

	if (curr) {
		// Attempt to find symbol name and then display error
		for (int i = 0; i < curr->num_reldyn + curr->num_relplt; i++) {
			Elf32_Rel *rel = i < curr->num_reldyn ? &curr->reldyn[i] : &curr->relplt[i - curr->num_reldyn];
			Elf32_Sym *sym = &curr->dynsym[ELF32_R_SYM(rel->r_info)];
			uintptr_t *ptr = (uintptr_t *)(curr->text_base + rel->r_offset);

			int type = ELF32_R_TYPE(rel->r_info);
			switch (type) {
				case R_ARM_JUMP_SLOT:
				{
					if (got0 == (uintptr_t)ptr) {
						fatal_error("Unknown symbol \"%s\" (%p).\n", curr->dynstr + sym->st_name, (void*)got0);
					}
					break;
				}
			}
		}
	}

	// Ooops, this shouldn't have happened.
	fatal_error("Unknown symbol \"???\" (%p).\n", (void*)got0);
}

__attribute__((naked)) void plt0_stub()
{
	register uintptr_t got0 asm("r12");
	reloc_err(got0);
}

int so_resolve(so_module *mod, so_default_dynlib *default_dynlib, int size_default_dynlib, int default_dynlib_only) {
	for (int i = 0; i < mod->num_reldyn + mod->num_relplt; i++) {
		Elf32_Rel *rel = i < mod->num_reldyn ? &mod->reldyn[i] : &mod->relplt[i - mod->num_reldyn];
		Elf32_Sym *sym = &mod->dynsym[ELF32_R_SYM(rel->r_info)];
		uintptr_t *ptr = (uintptr_t *)(mod->text_base + rel->r_offset);

		int type = ELF32_R_TYPE(rel->r_info);
		switch (type) {
		case R_ARM_ABS32:
		case R_ARM_GLOB_DAT:
		case R_ARM_JUMP_SLOT:
		{
			if (sym->st_shndx == SHN_UNDEF) {
				int resolved = 0;
				if (!default_dynlib_only) {
					uintptr_t link = so_resolve_link(mod, mod->dynstr + sym->st_name);
					if (link) {
						// debugPrintf("Resolved from dependencies: %s\n", mod->dynstr + sym->st_name);
						if (type == R_ARM_ABS32)
							*ptr += link;
						else
							*ptr = link;
						resolved = 1;
					}
				}

				for (int j = 0; j < size_default_dynlib / sizeof(so_default_dynlib); j++) {
					if (strcmp(mod->dynstr + sym->st_name, default_dynlib[j].symbol) == 0) {
						*ptr = default_dynlib[j].func;
						resolved = 1;
						break;
					}
				}
				
				if (!resolved) {
					void *f = vglGetProcAddress(mod->dynstr + sym->st_name);
					if (f) {
						*ptr = f;
						resolved = 1;
						break;
					}
				}

				if (!resolved) {
					if (type == R_ARM_JUMP_SLOT) {
						debugPrintf("Unresolved import: %s\n", mod->dynstr + sym->st_name);
						*ptr = (uintptr_t)&plt0_stub;
					}
					else {
						//printf("Unresolved import: %s\n", mod->dynstr + sym->st_name);
					}
				}
			}

			break;
		}
		default:
			break;
		}
	}

	return 0;
}

int so_resolve_with_dummy(so_module *mod, so_default_dynlib *default_dynlib, int size_default_dynlib, int default_dynlib_only) {
	for (int i = 0; i < mod->num_reldyn + mod->num_relplt; i++) {
		Elf32_Rel *rel = i < mod->num_reldyn ? &mod->reldyn[i] : &mod->relplt[i - mod->num_reldyn];
		Elf32_Sym *sym = &mod->dynsym[ELF32_R_SYM(rel->r_info)];
		uintptr_t *ptr = (uintptr_t *)(mod->text_base + rel->r_offset);

		int type = ELF32_R_TYPE(rel->r_info);
		switch (type) {
		case R_ARM_ABS32:
		case R_ARM_GLOB_DAT:
		case R_ARM_JUMP_SLOT:
		{
			if (sym->st_shndx == SHN_UNDEF) {
				for (int j = 0; j < size_default_dynlib / sizeof(so_default_dynlib); j++) {
					if (strcmp(mod->dynstr + sym->st_name, default_dynlib[j].symbol) == 0) {
						*ptr = &ret0;
						break;
					}
				}
			}

			break;
		}
		default:
			break;
		}
	}

	return 0;
}

void so_initialize(so_module *mod) {
	for (int i = 0; i < mod->num_init_array; i++) {
		if (mod->init_array[i])
			mod->init_array[i]();
	}
}

uint32_t so_hash(const uint8_t *name) {
	uint64_t h = 0, g;
	while (*name) {
		h = (h << 4) + *name++;
		if ((g = (h & 0xf0000000)) != 0)
			h ^= g >> 24;
		h &= 0x0fffffff;
	}
	return h;
}

static int so_symbol_index(so_module *mod, const char *symbol)
{
	if (mod->hash) {
		uint32_t hash = so_hash((const uint8_t *)symbol);
		uint32_t nbucket = mod->hash[0];
		uint32_t *bucket = &mod->hash[2];
		uint32_t *chain = &bucket[nbucket];
		for (int i = bucket[hash % nbucket]; i; i = chain[i]) {
			if (mod->dynsym[i].st_shndx == SHN_UNDEF)
				continue;
			if (mod->dynsym[i].st_info != SHN_UNDEF && strcmp(mod->dynstr + mod->dynsym[i].st_name, symbol) == 0)
				return i;
		}
	}

	for (int i = 0; i < mod->num_dynsym; i++) {
		if (mod->dynsym[i].st_shndx == SHN_UNDEF)
			continue;
		if (mod->dynsym[i].st_info != SHN_UNDEF && strcmp(mod->dynstr + mod->dynsym[i].st_name, symbol) == 0)
			return i;
	}

	return -1;
}

/*
 * alloc_arena: allocates space on either patch or cave arenas, 
 * range: maximum range from allocation to dst (ignored if NULL)
 * dst: destination address
*/
static uintptr_t so_alloc_arena(so_module *so, uintptr_t range, uintptr_t dst, size_t sz) {
	// Is address in range?
	#define inrange(lsr, gtr, range) \
		(((uintptr_t)(range) == (uintptr_t)NULL) || ((uintptr_t)(range) >= ((uintptr_t)(gtr) - (uintptr_t)(lsr))))
	// Space left on block
	#define blkavail(type) (so->type##_size - (so->type##_head - so->type##_base))
	
	// keep allocations 4-byte aligned for simplicity
	sz = ALIGN_MEM(sz, 4);

	if (sz <= (blkavail(patch)) && inrange(so->patch_base, dst, range)) {
		so->patch_head += sz;
		return (so->patch_head - sz);
	} else if (sz <= (blkavail(cave)) && inrange(dst, so->cave_base, range)) {
		so->cave_head += sz;
		return (so->cave_head - sz);
	}

	return (uintptr_t)NULL;
}

static void trampoline_ldm(so_module *mod, uint32_t *dst) {
	uint32_t trampoline[1];
	uint32_t funct[20] = {0xFAFAFAFA};
	uint32_t *ptr = funct;

	int cur = 0;
	int baseReg = ((*dst) >> 16) & 0xF;
	int bitMask = (*dst) & 0xFFFF;

	uint32_t stored = NULL;
	for (int i = 0; i < 16; i++) {
		if (bitMask & (1 << i)) {
			// If the register we're reading the offset from is the same as the one we're writing,
			// delay it to the very end so that the base pointer ins't clobbered
			if (baseReg == i)
				stored = LDR_OFFS(i, baseReg, cur).raw;
			else
				*ptr++ = LDR_OFFS(i, baseReg, cur).raw;
			cur += 4;
		}
	}

	// Perform the delayed load if needed
	if (stored) {
		*ptr++ = stored;
	}

	*ptr++ = 0xe51ff004; // LDR PC, [PC, -0x4] ; jmp to [dst+0x4]
	*ptr++ = dst+1; // .dword <...>	; [dst+0x4]

	size_t trampoline_sz =	((uintptr_t)ptr - (uintptr_t)&funct[0]);
	uintptr_t patch_addr = so_alloc_arena(mod, B_RANGE, B_OFFSET(dst), trampoline_sz);

	if (!patch_addr) {
		fatal_error("Failed to patch LDMIA at 0x%08X, unable to allocate space.\n", dst);
	}
	
	// Create sign extended relative address rel_addr
	trampoline[0] = B(dst, patch_addr).raw;

	kuKernelCpuUnrestrictedMemcpy((void*)patch_addr, funct, trampoline_sz);
	kuKernelCpuUnrestrictedMemcpy(dst, trampoline, sizeof(trampoline));
}

uintptr_t so_symbol(so_module *mod, const char *symbol) {
	int index = so_symbol_index(mod, symbol);
	if (index == -1)
		return NULL;

	return mod->text_base + mod->dynsym[index].st_value;
}

void so_symbol_fix_ldmia(so_module *mod, const char *symbol) {
	// This is meant to work around crashes due to unaligned accesses (SIGBUS :/) due to certain
	// kernels not having the fault trap enabled, e.g. certain RK3326 Odroid Go Advance clone distros.
	// TODO:: Maybe enable this only with a config flag? maybe with a list of known broken functions?
	// Known to trigger on GM:S's "_Z11Shader_LoadPhjS_" - if it starts happening on other places,
	// might be worth enabling it globally.
	
	int idx = so_symbol_index(mod, symbol);
	if (idx == -1)
		return;

	uintptr_t st_addr = mod->text_base + mod->dynsym[idx].st_value;
	for (uintptr_t addr = st_addr; addr < st_addr + mod->dynsym[idx].st_size; addr+=4) {
		uint32_t inst = *(uint32_t*)(addr);
		
		//Is this an LDMIA instruction with a R0-R12 base register?
		if (((inst & 0xFFF00000) == 0xE8900000) && (((inst >> 16) & 0xF) < 13) ) {
			debugPrintf("Found possibly misaligned LDMIA on 0x%08X, trying to fix it... (instr: 0x%08X, to 0x%08X)\n", addr, *(uint32_t*)addr, mod->patch_head);
			trampoline_ldm(mod, addr);
		}
	}
}
