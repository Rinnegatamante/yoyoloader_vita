#ifndef __SO_UTIL_H__
#define __SO_UTIL_H__

#include <psp2common/types.h>
#include "elf.h"

#define MAX_DATA_SEG 4

#define USE_KUBRIDGE // Comment out to use VM domain instead
#define HAVE_VITAGL // Comment out to disable automatic GL symbols resolution with vitaGL

#ifndef USE_KUBRIDGE
extern SceUID vm_blk;
#endif

/**
 * @brief Runtime state of a loaded Android shared library (.so).
 */
typedef struct so_module {
	struct so_module *next;

	SceUID patch_blockid, text_blockid, data_blockid[MAX_DATA_SEG];
	uintptr_t patch_base, patch_head, cave_base, cave_head, text_base, plt_base;
	uintptr_t load_addr, data_base[MAX_DATA_SEG], exidx_base;
	size_t patch_size, cave_size, text_size, data_size[MAX_DATA_SEG], plt_size, exidx_size;
	int n_data;

	Elf32_Ehdr *ehdr;
	Elf32_Phdr *phdr;
	Elf32_Shdr *shdr;

	Elf32_Dyn *dynamic;
	Elf32_Sym *dynsym;
	Elf32_Rel *reldyn;
	Elf32_Rel *relplt;

	void (** init_array)(void);
	uint32_t *hash;

	uint32_t num_dynamic;
	uint32_t num_dynsym;
	uint32_t num_reldyn;
	uint32_t num_relplt;
	uint32_t num_init_array;

	char *soname;
	char *shstr;
	char *dynstr;
} so_module;

/**
 * @brief Saved state for a single function hook.
 */
typedef struct {
	uintptr_t addr;
	uintptr_t thumb_addr;
	uint32_t orig_instr[2];
	uint32_t patch_instr[2];
} so_hook;

/**
 * @brief Maps an unresolved dynamic symbol name to a function pointer.
 *
 * An array of these entries forms the "dynamic lib" used with `so_resolve`.
 */
typedef struct {
	const char *symbol;
	uintptr_t func;
} so_default_dynlib;

/**
 * @brief Installs a hook on a Thumb-mode function.
 *
 * @param addr  Address of the Thumb function to hook.
 * @param dst   Destination address to redirect execution to.
 * @return      so_hook handle that captures the original instructions.
 */
so_hook hook_thumb(uintptr_t addr, uintptr_t dst);

/**
 * @brief Installs a hook on an ARM-mode function.
 *
 * @param addr  4-byte aligned address of the ARM function to hook.
 * @param dst   Destination address to redirect execution to.
 * @return      so_hook handle that captures the original instructions.
 */
so_hook hook_arm(uintptr_t addr, uintptr_t dst);

/**
 * @brief Installs a hook on a function of either ARM or Thumb mode.
 *
 * Calls `hook_thumb()` if @p addr is odd, otherwise `hook_arm()`.
 *
 * @param addr  Address of the function to hook.
 * @param dst   Destination address to redirect execution to.
 * @return      so_hook handle that captures the original instructions.
 */
so_hook hook_addr(uintptr_t addr, uintptr_t dst);

/**
 * @brief Flushes the instruction and data caches for a module's text segment.
 *
 * Must be called after applying patches so the CPU sees the updated
 * instructions.
 *
 * @param mod  Module whose text segment should be flushed.
 */
void so_flush_caches(const so_module *mod);

/**
 * @brief Loads a .so file from the filesystem into a fixed virtual address.
 *
 * Allocates memory blocks for the text and data segments at @p load_addr,
 * copies the file contents in, and parses ELF metadata into @p mod.
 *
 * Call `so_relocate()` and `so_resolve()` after this before executing any code
 * from the module.
 *
 * @param mod        Output module struct to populate.
 * @param filename   Path to the .so file to load.
 * @param load_addr  Virtual address at which to map the text segment.
 * @return           0 on success, negative error code on failure.
 */
int so_file_load(so_module *mod, const char *filename, uintptr_t load_addr);

/**
 * @brief Loads a .so from a memory buffer into a fixed virtual address.
 *
 * Equivalent to so_file_load() but reads from an in-memory buffer instead of
 * a file.
 *
 * @param mod        Output module struct to populate .
 * @param buffer     Pointer to the .so image in memory.
 * @param so_size    Size of the .so image in bytes.
 * @param load_addr  Virtual address at which to map the text segment.
 * @return           0 on success, negative error code on failure.
 */
int so_mem_load(so_module *mod, const void *buffer, size_t so_size, uintptr_t load_addr);

/**
 * @brief Applies all ELF relocations for a loaded module.
 *
 * @param mod  Module to relocate.
 * @return     Always 0.
 */
int so_relocate(const so_module *mod);

/**
 * @brief NOP calls to given function addresses across the whole module.
 *
 * @param mod                  Module whose imports should be resolved.
 * @param addresses            List of symbols point to function calls to NOP.
 * @param addresses_num        Number of addresses passed with @p addresses
 * @return                     The number of function calls successfully NOP-ed.
 */
int so_nop_calls(const so_module *mod, uintptr_t *addresses, int addresses_num);

/**
 * @brief Resolves undefined dynamic symbols against a default library table.
 *
 * For each undefined symbol in @p mod's relocations:
 *   1. If @p default_dynlib_only is 0, searches loaded dependency modules first.
 *   2. Falls back to the @p default_dynlib table.
 *   3. If still unresolved, symbols are redirected to an error-logging stub.
 *
 * @param mod                  Module whose imports should be resolved.
 * @param default_dynlib       Table of symbol-name to function-pointer mappings.
 * @param size_default_dynlib  Size of @p default_dynlib in bytes.
 * @param default_dynlib_only  0 to skip inter-module resolution, 1 to use it.
 * @return                     Always 0.
 */
int so_resolve(const so_module *mod, const so_default_dynlib *default_dynlib, int size_default_dynlib, int default_dynlib_only);

/**
 * @brief Resolves symbols found in @p default_dynlib, replaces all others
 * with a return-0 stub.
 *
 * @param mod                  Module whose imports should be resolved.
 * @param default_dynlib       Table of symbol-name to function-pointer mappings.
 * @param size_default_dynlib  Size of @p default_dynlib in bytes.
 * @param default_dynlib_only  Unused.
 * @return                     Always 0.
 */
int so_resolve_with_dummy(const so_module *mod, const so_default_dynlib *default_dynlib, int size_default_dynlib, int default_dynlib_only);

/**
 * @brief Patches misaligned LDMIA instructions inside a named symbol's code.
 *
 * Scans every instruction word in the named symbol and replaces each LDMIA
 * with a trampoline that performs individual aligned word-sized loads.
 *
 * @param mod     Module containing the symbol to scan.
 * @param symbol  Name of the symbol whose body should be scanned and patched.
 */
void so_symbol_fix_ldmia(so_module *mod, const char *symbol);

/**
 * @brief Calls all functions listed in a module's .init_array section.
 *
 * Call this after `so_relocate()` and `so_resolve()` are complete.
 *
 * @param mod  Module to initialize.
 */
void so_initialize(const so_module *mod);

/**
 * @brief Frees allocated memory blocks for a given module.
 *
 * @param mod  Module to finalize.
 */
void so_finalize(const so_module *mod);

/**
 * @brief Looks up an exported dynamic symbol by name in a module.
 *
 * @param mod     Module to search.
 * @param symbol  Name of the symbol to look up.
 * @return        Absolute virtual address of the symbol, or 0 if not found.
 */
uintptr_t so_symbol(const so_module *mod, const char *symbol);

/**
 * @brief Looks up a PLT trampoline symbol by name of the target symbol in a module.
 *
 * @param mod     Module to search.
 * @param symbol  Name of the symbol to look up.
 * @return        Absolute virtual address of the trampoline symbol, or 0 if not found.
 */
uintptr_t so_trampoline_symbol(const so_module *mod, const char *symbol);

/**
 * @brief Calls the original (unhooked) function from within a hook handler.
 *
 * Temporarily restores the original instructions at the hook site, calls the
 * original function with the supplied arguments, then re-applies the patch
 * and flushes the instruction cache before returning.
 *
 * @param type  Return type of the hooked function.
 * @param h     so_hook handle returned by hook_thumb() / hook_arm().
 * @param ...   Arguments to forward to the original function.
 * @return      The original function's return value, of type @p type.
 */
#ifdef USE_KUBRIDGE
#define SO_CONTINUE(type, h, ...) ({ \
	sceClibMemcpy((void *)h.addr, h.orig_instr, sizeof(h.orig_instr)); \
	kuKernelFlushCaches((void *)h.addr, sizeof(h.orig_instr)); \
	type r = h.thumb_addr ? ((type(*)())h.thumb_addr)(__VA_ARGS__) : ((type(*)())h.addr)(__VA_ARGS__); \
	sceClibMemcpy((void *)h.addr, h.patch_instr, sizeof(h.patch_instr)); \
	kuKernelFlushCaches((void *)h.addr, sizeof(h.patch_instr)); \
	r; \
})
#else
#define SO_CONTINUE(type, h, ...) ({ \
	sceClibMemcpy((void *)h.addr, h.orig_instr, sizeof(h.orig_instr)); \
	sceKernelSyncVMDomain(vm_blk, (void *)h.addr, sizeof(h.orig_instr)); \
	type r = h.thumb_addr ? ((type(*)())h.thumb_addr)(__VA_ARGS__) : ((type(*)())h.addr)(__VA_ARGS__); \
	sceClibMemcpy((void *)h.addr, h.patch_instr, sizeof(h.patch_instr)); \
	sceKernelSyncVMDomain(vm_blk, (void *)h.addr, sizeof(h.patch_instr)); \
	r; \
})
#endif

#endif // __SO_UTIL_H__
