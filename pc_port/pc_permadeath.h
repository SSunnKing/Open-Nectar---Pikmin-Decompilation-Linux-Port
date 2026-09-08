#ifndef PC_PERMADEATH_H
#define PC_PERMADEATH_H

#include "types.h"

class RandomAccessStream;

/*
 * Permadeath is a property of a save file, not of the port's configuration:
 * it is chosen once when the file is created and then travels with it, so
 * loading a normal file cannot put you in a run that deletes itself, and
 * loading a permadeath file cannot quietly turn the rule off.
 *
 * The flag rides in a small block at a fixed offset near the end of the save
 * file, in space the original game pads with zeros and inside the region its
 * checksum already covers. An original save has no block there and reads back
 * as a normal run; a build without this port reads our files as it always did.
 */

/// Byte offset of the port block inside a save file's 0x8000 block. The game
/// writes up to about 0x6E20 and its checksum trailer starts at 0x7FF8.
#define PC_SAVE_BLOCK_OFFSET (0x7F00)

/* Per-slot flags for the file-select screen, filled when the card is listed.
   The screen needs to mark a file before it is loaded, and loading is exactly
   what it must not do to answer the question. */
void pc_permadeath_note_slot(int slot, bool on);
bool pc_permadeath_slot(int slot);
void pc_permadeath_clear_slots(void);

/// True while the run in progress is a permadeath run.
bool pc_permadeath_active(void);

/// Sets what a newly created file will be. Chosen on the new-game screen.
void pc_permadeath_set_pending(bool on);
bool pc_permadeath_pending(void);

/// Applies the pending choice to the run that is starting. Called where a new
/// game initialises its play state.
void pc_permadeath_begin_new_run(void);

/// Writes the port block. Call with the stream positioned anywhere: it seeks.
void pc_permadeath_write_block(RandomAccessStream& out, bool permadeath);

/// Reads the port block and adopts it as the current run's rule. A file with
/// no block is a normal run.
void pc_permadeath_read_block(RandomAccessStream& in);

/// Reads the block without adopting it, for listing files.
bool pc_permadeath_peek_block(RandomAccessStream& in);

#endif // PC_PERMADEATH_H
