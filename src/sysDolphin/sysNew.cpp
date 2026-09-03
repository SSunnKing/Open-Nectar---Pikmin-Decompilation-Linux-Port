#include "sysNew.h"

#include "DebugLog.h"
#include "MemStat.h"
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>

/**
 * @todo: Documentation
 * @note UNUSED Size: 00009C
 */
DEFINE_ERROR(4)

/**
 * @todo: Documentation
 * @note UNUSED Size: 0000F0
 */
DEFINE_PRINT("sysNew");

#if defined(PIKI_PC_PORT)
// Tracks allocations made through the PC operator new so delete can match
// them. The fixed bucket table and intrusive headers never allocate through
// operator new, avoiding recursion during boot and static initialisation.
//
// Why everything goes to the C heap: the game's AyuHeap arenas are reset by
// moving a cursor, which requires every allocation between resets to die at
// once. Native secondary threads (DVD worker, SDL audio, card worker) would
// allocate into whatever heap the main thread has active, and a concurrent
// reset would corrupt them. Until allocation carries per-thread heap
// context, routing global new into arenas is unsafe; game systems that need
// arena lifetimes go through System::alloc explicitly.
struct alignas(std::max_align_t) BootBlockHeader {
	u32 mMagic;
	u32 mPad;
	BootBlockHeader* mNext;
	size_t mSize;
	size_t mPad2;
};

static const u32 BOOT_MAGIC = 0x50694B31; // "PiK1"
static constexpr size_t BOOT_BUCKET_COUNT = 4096;

static std::mutex sAllocMutex;
static BootBlockHeader* sBootBuckets[BOOT_BUCKET_COUNT] = {};
static size_t sLiveAllocations = 0;
static size_t sLiveBytes = 0;
static size_t sPeakAllocations = 0;
static size_t sPeakBytes = 0;
static size_t sTotalAllocations = 0;
static size_t sTotalFrees = 0;
static size_t sUnknownFrees = 0;
static bool sDumpRegistered = false;

static size_t allocationBucket(const void* ptr)
{
	uintptr_t value = reinterpret_cast<uintptr_t>(ptr);
	value >>= 4;
	value ^= value >> 17;
	return value & (BOOT_BUCKET_COUNT - 1);
}

void piki_pc_dump_alloc_stats(void)
{
	std::lock_guard<std::mutex> lock(sAllocMutex);
	fprintf(stderr,
	        "[PC Alloc] live=%zu bytes=%zu peak=%zu/%zu total=%zu frees=%zu unknown-frees=%zu\n",
	        sLiveAllocations, sLiveBytes, sPeakAllocations, sPeakBytes,
	        sTotalAllocations, sTotalFrees, sUnknownFrees);
}

void* piki_pc_alloc(size_t size)
{
	if (size == 0) {
		size = 1;
	}
	if (size & 0x3) {
		if (size > std::numeric_limits<size_t>::max() - 3) {
			throw std::bad_alloc();
		}
		size = (size + 3) & ~static_cast<size_t>(0x3);
	}
	if (size > std::numeric_limits<size_t>::max() - sizeof(BootBlockHeader)) {
		throw std::bad_alloc();
	}

	void* raw = std::malloc(sizeof(BootBlockHeader) + size);
	if (!raw) {
		ERROR("allocation of %zu bytes failed", size);
		throw std::bad_alloc();
	}

	BootBlockHeader* header = static_cast<BootBlockHeader*>(raw);
	header->mMagic          = BOOT_MAGIC;
	header->mNext           = nullptr;
	header->mSize           = size;

	void* result = header + 1;
	{
		std::lock_guard<std::mutex> lock(sAllocMutex);
		const size_t bucket = allocationBucket(result);
		header->mNext = sBootBuckets[bucket];
		sBootBuckets[bucket] = header;
		sLiveAllocations++;
		sLiveBytes += size;
		sTotalAllocations++;
		if (sLiveAllocations > sPeakAllocations) sPeakAllocations = sLiveAllocations;
		if (sLiveBytes > sPeakBytes) sPeakBytes = sLiveBytes;
		if (!sDumpRegistered) {
			std::atexit(piki_pc_dump_alloc_stats);
			sDumpRegistered = true;
		}
	}
	std::memset(result, 0, size);
	return result;
}

void piki_pc_free(void* ptr)
{
	if (!ptr) {
		return;
	}

	std::lock_guard<std::mutex> lock(sAllocMutex);
	const size_t bucket = allocationBucket(ptr);
	for (BootBlockHeader** link = &sBootBuckets[bucket]; *link; link = &(*link)->mNext) {
		BootBlockHeader* header = *link;
		if (header->mMagic == BOOT_MAGIC && header + 1 == ptr) {
			*link = header->mNext;
			header->mMagic = 0;
			sLiveAllocations--;
			sLiveBytes -= header->mSize;
			sTotalFrees++;
			std::free(header);
			return;
		}
	}
	sUnknownFrees++;
}

void* operator new(size_t size)
{
	return piki_pc_alloc(size);
}

void* operator new[](size_t size)
{
	return piki_pc_alloc(size);
}

void operator delete(void* ptr) noexcept
{
	piki_pc_free(ptr);
}

void operator delete[](void* ptr) noexcept
{
	piki_pc_free(ptr);
}

void operator delete(void* ptr, size_t) noexcept
{
	piki_pc_free(ptr);
}

void operator delete[](void* ptr, size_t) noexcept
{
	piki_pc_free(ptr);
}
#endif

/**
 * @todo: Documentation
 */
void* System::alloc(size_t size)
{
	void* result = nullptr;
	System* system = gsys;
	if (!system) {
		fprintf(stderr, "[PC Port Fatal] System::alloc(%zu) called without an active System instance\n", size);
		abort();
	}
	if (size & 0x3) {
		size = (size + 3) & ~0x3;
	}

	if (system->mActiveHeapIdx >= 0) {
		AyuHeap* heap = &system->mHeaps[system->mActiveHeapIdx];
		if (size == 0) {
			PRINT("trying to allocate %d bytes on heap\n", 0);
		}
		result = heap->push(size);
		if (!result) {
			ERROR("new[] %d failed in heap '%s'", size, heap->mName);
		}

		if (size == 0 || system->mForcePrint) {
			bool print             = system->mTogglePrint;
			system->mTogglePrint = TRUE;
			system->mTogglePrint = print;
		}

		MemInfo* info = system->mCurrMemInfo;
		while (info) {
			info->mMemorySize += size;
			info = static_cast<MemInfo*>(info->mParent);
		}

		if ((u32)result & 0x3) {
			ERROR("acquired memory not long aligned %08x!!\n", (u32)result);
		}

		u32* resPtr = (u32*)result;
		int length  = size / 4;
		for (int i = 0; i < length; i++) {
			resPtr[i] = 0;
		}
	} else {
#if defined(WIN32)
		// The DLL uses `GlobalAlloc` here and has an ERROR if that fails.  This branch of code is probably DLL exclusive,
		// since the GCN can't just ask WinAPI for unlimited memory.  We'll see once JPN Demo version matching begins.
		if (!result) {
			ERROR("new[] %d failed", size);
		}
#else
		ERROR("no heap specified\n");
#endif
	}

	return result;
}

/**
 * @todo: Documentation
 * @note UNUSED Size: 000044 (Matching by size)
 */
#if defined(PIKI_PC_PORT)
void* operator new(size_t size, PikiAlignment requestedAlignment)
#else
void* operator new(size_t size, int requestedAlignment)
#endif
{
	int alignment =
#if defined(PIKI_PC_PORT)
	    requestedAlignment.value;
#else
	    requestedAlignment;
#endif
	uintptr_t alloc  = reinterpret_cast<uintptr_t>(System::alloc(size + alignment));
	uintptr_t result = (alloc + (alignment - 1)) & ~static_cast<uintptr_t>(alignment - 1);
	return (void*)result;
}

/**
 * @todo: Documentation
 */
#if defined(PIKI_PC_PORT)
void* operator new[](size_t size, PikiAlignment requestedAlignment)
#else
void* operator new[](size_t size, int requestedAlignment)
#endif
{
	int alignment =
#if defined(PIKI_PC_PORT)
	    requestedAlignment.value;
#else
	    requestedAlignment;
#endif
	uintptr_t alloc  = reinterpret_cast<uintptr_t>(System::alloc(size + alignment));
	uintptr_t result = (alloc + (alignment - 1)) & ~static_cast<uintptr_t>(alignment - 1);
	return (void*)result;
}

/**
 * @todo: Documentation
 */
void dummy_delete(void*)
{
}

/**
 * @todo: Documentation
 */
void dummy_delete_arr(void*)
{
}
