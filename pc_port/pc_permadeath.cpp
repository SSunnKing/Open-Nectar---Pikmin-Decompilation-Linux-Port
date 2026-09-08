#include "pc_permadeath.h"

#include "Stream.h"
#include <cstdio>

namespace {
// 'NCT1' -- a magic rather than a bare flag, because the bytes at this offset
// in an original save are zeros that happen to be there, not a field anybody
// wrote. Only an exact match means the block is ours.
const int kMagic   = 0x4E435431;
const int kVersion = 1;

bool sActiveRun = false;
bool sPending   = false;
bool sSlots[3]  = { false, false, false };
}

void pc_permadeath_note_slot(int slot, bool on)
{
	if (slot >= 0 && slot < 3) sSlots[slot] = on;
}

bool pc_permadeath_slot(int slot)
{
	return (slot >= 0 && slot < 3) ? sSlots[slot] : false;
}

void pc_permadeath_clear_slots(void)
{
	for (int i = 0; i < 3; i++) sSlots[i] = false;
}

bool pc_permadeath_active(void) { return sActiveRun; }

void pc_permadeath_set_pending(bool on) { sPending = on; }
bool pc_permadeath_pending(void) { return sPending; }

void pc_permadeath_begin_new_run(void)
{
	sActiveRun = sPending;
	std::printf("permadeath: new run starts %s\n", sActiveRun ? "PERMADEATH" : "normal");
}

void pc_permadeath_write_block(RandomAccessStream& out, bool permadeath)
{
	// The caller has already padded to the checksum trailer, so seek back into
	// the padding, write, and leave the position where it was found.
	const int resume = out.getPosition();
	out.setPosition(PC_SAVE_BLOCK_OFFSET);
	out.writeInt(kMagic);
	out.writeInt(kVersion);
	out.writeInt(permadeath ? 1 : 0);
	out.setPosition(resume);
}

bool pc_permadeath_peek_block(RandomAccessStream& in)
{
	const int resume = in.getPosition();
	in.setPosition(PC_SAVE_BLOCK_OFFSET);
	const int magic = in.readInt();
	const int version = in.readInt();
	const int flag = in.readInt();
	in.setPosition(resume);

	// An original save, or one from a newer port than this build knows how to
	// read. Either way it is not a permadeath run as far as we can tell, and
	// guessing would be worse than treating it as normal.
	if (magic != kMagic || version != kVersion) return false;
	return flag != 0;
}

void pc_permadeath_read_block(RandomAccessStream& in)
{
	sActiveRun = pc_permadeath_peek_block(in);
	std::printf("permadeath: loaded a %s save\n", sActiveRun ? "PERMADEATH" : "normal");
}
