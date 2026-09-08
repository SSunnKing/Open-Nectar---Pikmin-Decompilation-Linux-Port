// The permadeath flag rides in a block inside the save file. Getting that
// wrong corrupts a save, and the only way to find out in-game is to lose one,
// so the round-trip is checked here instead.
#include "pc_permadeath.h"
#include "Stream.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {
int failures = 0;

void check(bool ok, const char* what)
{
	if (!ok) {
		std::printf("FAIL: %s\n", what);
		failures++;
	}
}

// One save file's block, as the game lays it out: 0x8000 bytes, the checksum
// trailer at 0x7FF8, everything the game writes well below our offset.
std::vector<unsigned char> makeFile() { return std::vector<unsigned char>(0x8000, 0); }
}

int main()
{
	// A file the original game wrote has zeros where our block goes, and must
	// read back as an ordinary run rather than as an uninitialised one.
	{
		std::vector<unsigned char> file = makeFile();
		RamStream in(file.data(), (int)file.size());
		pc_permadeath_set_pending(true);
		pc_permadeath_begin_new_run();
		check(pc_permadeath_active(), "pending choice starts the run");
		pc_permadeath_read_block(in);
		check(!pc_permadeath_active(), "a save with no block is a normal run");
	}

	// Round-trip, both values.
	for (int i = 0; i < 2; i++) {
		const bool value = (i != 0);
		std::vector<unsigned char> file = makeFile();
		RamStream out(file.data(), (int)file.size());
		out.setPosition(0x7FF8); // where the game leaves it, after padding
		pc_permadeath_write_block(out, value);
		check(out.getPosition() == 0x7FF8, "writing the block restores the position");

		RamStream in(file.data(), (int)file.size());
		in.setPosition(0x7FF8);
		pc_permadeath_read_block(in);
		check(pc_permadeath_active() == value, "the flag survives a round trip");
		check(in.getPosition() == 0x7FF8, "reading the block restores the position");
	}

	// The block must not touch what the game writes, nor the checksum trailer.
	{
		std::vector<unsigned char> file = makeFile();
		for (int i = 0; i < 0x7000; i++) file[i] = (unsigned char)(i & 0xFF);
		std::vector<unsigned char> before = file;
		RamStream out(file.data(), (int)file.size());
		out.setPosition(0x7FF8);
		pc_permadeath_write_block(out, true);
		check(std::memcmp(file.data(), before.data(), 0x7000) == 0,
		      "the game's own data is untouched");
		check(std::memcmp(file.data() + 0x7FF8, before.data() + 0x7FF8, 8) == 0,
		      "the checksum trailer is untouched");
		// And it has to sit inside the checksummed region, or a corrupted flag
		// would pass verification.
		check(PC_SAVE_BLOCK_OFFSET + 12 <= 0x7FF8, "the block is covered by the checksum");
	}

	// A newer port's block must not be guessed at.
	{
		std::vector<unsigned char> file = makeFile();
		RamStream out(file.data(), (int)file.size());
		out.setPosition(0x7FF8);
		pc_permadeath_write_block(out, true);
		file[PC_SAVE_BLOCK_OFFSET + 7] = 99; // bump the version
		RamStream in(file.data(), (int)file.size());
		pc_permadeath_read_block(in);
		check(!pc_permadeath_active(), "an unknown block version reads as normal");
	}

	if (failures == 0) std::printf("pc_permadeath_test: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
