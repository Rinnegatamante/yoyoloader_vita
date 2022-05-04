#ifndef __SO_UTIL_H__
#define __SO_UTIL_H__

#include "elf.h"

#define ALIGN_MEM(x, align) (((x) + ((align) - 1)) & ~((align) - 1))
#define MAX_DATA_SEG 4

typedef struct so_module {
  struct so_module *next;

  SceUID patch_blockid, text_blockid, data_blockid[MAX_DATA_SEG];
  uintptr_t patch_base, patch_head, cave_base, cave_head, text_base, data_base[MAX_DATA_SEG];
  size_t patch_size, cave_size, text_size, data_size[MAX_DATA_SEG];
  int n_data;

  Elf32_Ehdr *ehdr;
  Elf32_Phdr *phdr;
  Elf32_Shdr *shdr;

  Elf32_Dyn *dynamic;
  Elf32_Sym *dynsym;
  Elf32_Rel *reldyn;
  Elf32_Rel *relplt;

  int (** init_array)(void);
  uint32_t *hash;

  int num_dynamic;
  int num_dynsym;
  int num_reldyn;
  int num_relplt;
  int num_init_array;

  char *soname;
  char *shstr;
  char *dynstr;
} so_module;

typedef struct {
  char *symbol;
  uintptr_t func;
} so_default_dynlib;

void hook_thumb(uintptr_t addr, uintptr_t dst);
void hook_arm(uintptr_t addr, uintptr_t dst);
void hook_addr(uintptr_t addr, uintptr_t dst);

void so_flush_caches(so_module *mod);
int so_file_load(so_module *mod, const char *filename, uintptr_t load_addr);
int so_mem_load(so_module *mod, void * buffer, size_t so_size, uintptr_t load_addr);
int so_relocate(so_module *mod);
int so_resolve(so_module *mod, so_default_dynlib *default_dynlib, int size_default_dynlib, int default_dynlib_only);
int so_resolve_with_dummy(so_module *mod, so_default_dynlib *default_dynlib, int size_default_dynlib, int default_dynlib_only);
void so_symbol_fix_ldmia(so_module *mod, const char *symbol);
void so_initialize(so_module *mod);
uintptr_t so_symbol(so_module *mod, const char *symbol);

#endif
