#include <errno.h>
#include <reent.h>
#include <vitasdk.h>

#include "config.h"

extern unsigned int _newlib_heap_size_user __attribute__((weak));

static int _newlib_heap_memblock;
unsigned _newlib_heap_size;
static char *_newlib_heap_base, *_newlib_heap_end, *_newlib_heap_cur;
static SceKernelLwMutexWork _newlib_sbrk_mutex;

void * _sbrk_r(struct _reent *reent, ptrdiff_t incr)
{
	if (sceKernelLockLwMutex(&_newlib_sbrk_mutex, 1, 0) < 0)
		goto fail;
	if (!_newlib_heap_base || _newlib_heap_cur + incr >= _newlib_heap_end)
	{
		sceKernelUnlockLwMutex(&_newlib_sbrk_mutex, 1);
fail:
		reent->_errno = ENOMEM;
		return (void*) -1;
	}

	char *prev_heap_end = _newlib_heap_cur;
	_newlib_heap_cur += incr;

	sceKernelUnlockLwMutex(&_newlib_sbrk_mutex, 1);
	return (void*) prev_heap_end;
}

void _init_vita_heap(void)
{
	// Create a mutex to use inside _sbrk_r
	if (sceKernelCreateLwMutex(&_newlib_sbrk_mutex, "sbrk mutex", 0, 0, 0) < 0)
	{
		goto failure;
	}
	
#if 0 // Devkit mode
	_newlib_heap_size = 570 * 1024 * 1024;
#else
	char t[0x8000];
	int size;
#ifdef STANDALONE_MODE
	SceUID fd = sceIoOpen("app0:yyl.cfg", SCE_O_RDONLY, 0777);
#else
	SceUID fd = sceIoOpen(LAUNCH_FILE_PATH, SCE_O_RDONLY, 0777);
	if (fd > 0) {
		size = sceIoRead(fd, t, 0x200);
		sceIoClose(fd);
		t[size] = 0;
	} else {
		sceClibSnprintf(t, 0x200, "%s", "AM2R");
	}
	sceClibSnprintf(&t[0x1000], 0x6000, "%s/%s/yyl.cfg", DATA_PATH, t);
	fd = sceIoOpen(&t[0x1000], SCE_O_RDONLY, 0777);
#endif
	if (fd > 0)
	{
		size = sceIoRead(fd, t, 0x8000);
		sceIoClose(fd);
		t[size] = 0;
		char *s = sceClibStrstr(t, "maximizeNewlib=");
		if (s && s[15] == '1') {
			_newlib_heap_size = 300 * 1024 * 1024;
		} else {
			_newlib_heap_size = 240 * 1024 * 1024;
		}
	}
	else
	{
		_newlib_heap_size = 240 * 1024 * 1024;
	}
#endif
	_newlib_heap_memblock = sceKernelAllocMemBlock("Newlib heap", 0x0c20d060, _newlib_heap_size, 0);
	if (_newlib_heap_memblock < 0)
	{
		goto failure;
	}
	if (sceKernelGetMemBlockBase(_newlib_heap_memblock, (void**)&_newlib_heap_base) < 0)
	{
		goto failure;
	}
	_newlib_heap_end = _newlib_heap_base + _newlib_heap_size;
	_newlib_heap_cur = _newlib_heap_base;

	return;
failure:
	_newlib_heap_memblock = 0;
	_newlib_heap_base = 0;
	_newlib_heap_cur = 0;
}

void _free_vita_heap(void)
{
	// Destroy the sbrk mutex
	sceKernelDeleteLwMutex(&_newlib_sbrk_mutex);

	// Free the heap memblock to avoid memory leakage.
	sceKernelFreeMemBlock(_newlib_heap_memblock);

	_newlib_heap_memblock = 0;
	_newlib_heap_base = 0;
	_newlib_heap_cur = 0;
}
