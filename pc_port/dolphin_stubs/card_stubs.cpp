/** Native filesystem implementation of the GameCube memory-card API. */
#include "Dolphin/card.h"
#include "Dolphin/dvd.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
constexpr std::uintmax_t kCapacity = 16u * 1024u * 1024u;
constexpr s32 kSectorSize = 0x2000;
s32 sLastResult[2] = { CARD_RESULT_READY, CARD_RESULT_READY };
s32 sTransferred[2] = {};

bool validChannel(s32 channel) { return channel >= 0 && channel < 2; }
fs::path root(s32 channel) { return fs::path("save") / (channel == 0 ? "card0" : "card1"); }
fs::path dataPath(s32 channel, const std::string& name) { return root(channel) / name; }
fs::path metaPath(s32 channel, const std::string& name) { return root(channel) / (".meta_" + name); }

bool ensureCard(s32 channel)
{
	if (!validChannel(channel)) return false;
	std::error_code error;
	fs::create_directories(root(channel), error);
	return !error;
}

std::string safeName(const char* source)
{
	if (!source) return {};
	std::string name(source, strnlen(source, CARD_FILENAME_MAX));
	for (char& c : name) if (c == '/' || c == '\\') c = '_';
	if (name == "." || name == "..") name.insert(name.begin(), '_');
	return name;
}

std::vector<std::string> entries(s32 channel)
{
	std::vector<std::string> result;
	if (!ensureCard(channel)) return result;
	std::error_code error;
	for (const auto& entry : fs::directory_iterator(root(channel), error)) {
		const std::string name = entry.path().filename().string();
		if (entry.is_regular_file() && name.rfind(".meta_", 0) != 0) result.push_back(name);
	}
	std::sort(result.begin(), result.end());
	if (result.size() > CARD_MAX_FILE) result.resize(CARD_MAX_FILE);
	return result;
}

u32 cardTimeNow()
{
	using namespace std::chrono;
	const auto unixTime = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
	return unixTime > 946684800 ? static_cast<u32>(unixTime - 946684800) : 0;
}

CARDStat defaultStat(s32 channel, const std::string& name)
{
	CARDStat stat{};
	std::strncpy(stat.fileName, name.c_str(), CARD_FILENAME_MAX - 1);
	std::error_code error;
	stat.length = static_cast<u32>(fs::file_size(dataPath(channel, name), error));
	stat.time = cardTimeNow();
	if (DVDDiskID* disk = DVDGetCurrentDiskID()) {
		std::memcpy(stat.gameName, disk->gameName, sizeof(stat.gameName));
		std::memcpy(stat.company, disk->company, sizeof(stat.company));
	}
	stat.iconAddr = stat.commentAddr = 0xffffffffu;
	stat.offsetData = stat.length;
	return stat;
}

CARDStat loadStat(s32 channel, const std::string& name)
{
	CARDStat stat = defaultStat(channel, name), stored{};
	std::ifstream input(metaPath(channel, name), std::ios::binary);
	if (input.read(reinterpret_cast<char*>(&stored), sizeof(stored))) stat = stored;
	std::strncpy(stat.fileName, name.c_str(), CARD_FILENAME_MAX - 1);
	stat.fileName[CARD_FILENAME_MAX - 1] = '\0';
	std::error_code error;
	stat.length = static_cast<u32>(fs::file_size(dataPath(channel, name), error));
	return stat;
}

bool saveStat(s32 channel, const std::string& name, const CARDStat& source)
{
	CARDStat stat = source;
	std::strncpy(stat.fileName, name.c_str(), CARD_FILENAME_MAX - 1);
	stat.fileName[CARD_FILENAME_MAX - 1] = '\0';
	std::ofstream output(metaPath(channel, name), std::ios::binary | std::ios::trunc);
	return !!output.write(reinterpret_cast<const char*>(&stat), sizeof(stat));
}

s32 finish(s32 channel, s32 result, CARDCallback callback = nullptr)
{
	if (validChannel(channel)) sLastResult[channel] = result;
	if (callback) callback(channel, result);
	return result;
}

bool resolve(const CARDFileInfo* info, std::string& name)
{
	if (!info || !validChannel(info->chan)) return false;
	const auto files = entries(info->chan);
	if (info->fileNo < 0 || static_cast<size_t>(info->fileNo) >= files.size()) return false;
	name = files[info->fileNo];
	return true;
}
} // namespace

extern "C" {

void CARDInit(void)
{
	ensureCard(0);
	printf("[PC Port] CARDInit() - persistent filesystem card: save/card0\n");
}

BOOL CARDProbe(s32 channel) { return validChannel(channel) && ensureCard(channel); }
s32 CARDProbeEx(s32 channel, s32* memSize, s32* sectorSize)
{
	if (memSize) *memSize = 128;
	if (sectorSize) *sectorSize = kSectorSize;
	return finish(channel, CARDProbe(channel) ? CARD_RESULT_READY : CARD_RESULT_NOCARD);
}
s32 CARDMountAsync(s32 channel, CARDMemoryCard*, CARDCallback, CARDCallback callback)
{
	return finish(channel, ensureCard(channel) ? CARD_RESULT_READY : CARD_RESULT_NOCARD, callback);
}
s32 CARDMount(s32 channel, CARDMemoryCard*, CARDCallback)
{
	return finish(channel, ensureCard(channel) ? CARD_RESULT_READY : CARD_RESULT_NOCARD);
}
s32 CARDUnmount(s32 channel) { return finish(channel, CARD_RESULT_READY); }

s32 CARDOpen(s32 channel, const char* fileName, CARDFileInfo* info)
{
	const std::string wanted = safeName(fileName);
	const auto files = entries(channel);
	const auto item = std::find(files.begin(), files.end(), wanted);
	if (!info || item == files.end()) return finish(channel, CARD_RESULT_NOFILE);
	info->chan = channel;
	info->fileNo = static_cast<s32>(item - files.begin());
	info->offset = 0;
	info->length = static_cast<s32>(loadStat(channel, wanted).length);
	info->iBlock = 0;
	return finish(channel, CARD_RESULT_READY);
}
s32 CARDFastOpen(s32 channel, s32 fileNo, CARDFileInfo* info)
{
	const auto files = entries(channel);
	if (!info || fileNo < 0 || static_cast<size_t>(fileNo) >= files.size()) return finish(channel, CARD_RESULT_NOFILE);
	info->chan = channel; info->fileNo = fileNo; info->offset = 0;
	info->length = static_cast<s32>(loadStat(channel, files[fileNo]).length); info->iBlock = 0;
	return finish(channel, CARD_RESULT_READY);
}
s32 CARDClose(CARDFileInfo* info) { return finish(info ? info->chan : 0, CARD_RESULT_READY); }

s32 CARDCreate(s32 channel, const char* fileName, u32 size, CARDFileInfo* info)
{
	const std::string name = safeName(fileName);
	if (name.empty() || name.size() >= CARD_FILENAME_MAX) return finish(channel, CARD_RESULT_NAMETOOLONG);
	if (!ensureCard(channel)) return finish(channel, CARD_RESULT_NOCARD);
	if (fs::exists(dataPath(channel, name))) return finish(channel, CARD_RESULT_EXIST);
	if (entries(channel).size() >= CARD_MAX_FILE) return finish(channel, CARD_RESULT_LIMIT);
	s32 freeBytes = 0, freeFiles = 0;
	CARDFreeBlocks(channel, &freeBytes, &freeFiles);
	if (size > static_cast<u32>(freeBytes)) return finish(channel, CARD_RESULT_INSSPACE);
	std::ofstream output(dataPath(channel, name), std::ios::binary | std::ios::trunc);
	if (!output) return finish(channel, CARD_RESULT_IOERROR);
	if (size) { output.seekp(size - 1); output.put('\0'); }
	output.close();
	CARDStat stat = defaultStat(channel, name); stat.length = size; saveStat(channel, name, stat);
	return CARDOpen(channel, name.c_str(), info);
}
s32 CARDCreateAsync(s32 channel, const char* name, u32 size, CARDFileInfo* info, CARDCallback callback)
{
	const s32 result = CARDCreate(channel, name, size, info); if (callback) callback(channel, result); return result;
}

s32 CARDRead(CARDFileInfo* info, void* address, s32 length, s32 offset)
{
	std::string name;
	if (!address || length < 0 || offset < 0 || !resolve(info, name)) return finish(info ? info->chan : 0, CARD_RESULT_NOFILE);
	std::ifstream input(dataPath(info->chan, name), std::ios::binary);
	input.seekg(offset);
	if (!input.read(static_cast<char*>(address), length)) return finish(info->chan, CARD_RESULT_IOERROR);
	info->offset = offset + length; sTransferred[info->chan] = length;
	return finish(info->chan, CARD_RESULT_READY);
}
s32 CARDReadAsync(CARDFileInfo* info, void* address, s32 length, s32 offset, CARDCallback callback)
{
	const s32 result = CARDRead(info, address, length, offset); if (callback) callback(info ? info->chan : 0, result); return result;
}
s32 CARDWrite(CARDFileInfo* info, void* address, s32 length, s32 offset)
{
	std::string name;
	if (!address || length < 0 || offset < 0 || !resolve(info, name)) return finish(info ? info->chan : 0, CARD_RESULT_NOFILE);
	std::fstream output(dataPath(info->chan, name), std::ios::binary | std::ios::in | std::ios::out);
	output.seekp(offset);
	if (!output.write(static_cast<const char*>(address), length)) return finish(info->chan, CARD_RESULT_IOERROR);
	output.flush();
	info->offset = offset + length; info->length = std::max(info->length, info->offset); sTransferred[info->chan] = length;
	CARDStat stat = loadStat(info->chan, name); stat.length = info->length; stat.time = cardTimeNow(); saveStat(info->chan, name, stat);
	return finish(info->chan, CARD_RESULT_READY);
}
s32 CARDWriteAsync(CARDFileInfo* info, void* address, s32 length, s32 offset, CARDCallback callback)
{
	const s32 result = CARDWrite(info, address, length, offset); if (callback) callback(info ? info->chan : 0, result); return result;
}
s32 CARDGetXferredBytes(s32 channel) { return validChannel(channel) ? sTransferred[channel] : 0; }

s32 CARDFastDelete(s32 channel, s32 fileNo)
{
	const auto files = entries(channel);
	if (fileNo < 0 || static_cast<size_t>(fileNo) >= files.size()) return finish(channel, CARD_RESULT_NOFILE);
	std::error_code error;
	fs::remove(dataPath(channel, files[fileNo]), error); if (error) return finish(channel, CARD_RESULT_IOERROR);
	fs::remove(metaPath(channel, files[fileNo]), error);
	return finish(channel, error ? CARD_RESULT_IOERROR : CARD_RESULT_READY);
}
s32 CARDFastDeleteAsync(s32 channel, s32 fileNo, CARDCallback callback)
{
	const s32 result = CARDFastDelete(channel, fileNo); if (callback) callback(channel, result); return result;
}

s32 CARDRename(s32 channel, const char* oldName, const char* newName)
{
	const std::string oldSafe = safeName(oldName), newSafe = safeName(newName);
	if (!fs::exists(dataPath(channel, oldSafe))) return finish(channel, CARD_RESULT_NOFILE);
	if (fs::exists(dataPath(channel, newSafe))) return finish(channel, CARD_RESULT_EXIST);
	std::error_code error;
	fs::rename(dataPath(channel, oldSafe), dataPath(channel, newSafe), error);
	if (error) return finish(channel, CARD_RESULT_IOERROR);
	if (fs::exists(metaPath(channel, oldSafe))) fs::rename(metaPath(channel, oldSafe), metaPath(channel, newSafe), error);
	CARDStat stat = loadStat(channel, newSafe); saveStat(channel, newSafe, stat);
	return finish(channel, error ? CARD_RESULT_IOERROR : CARD_RESULT_READY);
}
s32 CARDRenameAsync(s32 channel, const char* oldName, const char* newName, CARDCallback callback)
{
	const s32 result = CARDRename(channel, oldName, newName); if (callback) callback(channel, result); return result;
}

s32 CARDGetStatus(s32 channel, s32 fileNo, CARDStat* stat)
{
	const auto files = entries(channel);
	if (!stat || fileNo < 0 || static_cast<size_t>(fileNo) >= files.size()) return finish(channel, CARD_RESULT_NOFILE);
	*stat = loadStat(channel, files[fileNo]);
	return finish(channel, CARD_RESULT_READY);
}
s32 CARDSetStatus(s32 channel, s32 fileNo, CARDStat* stat)
{
	const auto files = entries(channel);
	if (!stat || fileNo < 0 || static_cast<size_t>(fileNo) >= files.size()) return finish(channel, CARD_RESULT_NOFILE);
	CARDStat copy = *stat; copy.length = loadStat(channel, files[fileNo]).length; copy.time = cardTimeNow();
	return finish(channel, saveStat(channel, files[fileNo], copy) ? CARD_RESULT_READY : CARD_RESULT_IOERROR);
}
s32 CARDSetStatusAsync(s32 channel, s32 fileNo, CARDStat* stat, CARDCallback callback)
{
	const s32 result = CARDSetStatus(channel, fileNo, stat); if (callback) callback(channel, result); return result;
}

s32 CARDGetSerialNo(s32 channel, u64* serial) { if (serial) *serial = 0x50494b4d494e0001ULL; return finish(channel, CARD_RESULT_READY); }
s32 CARDGetSectorSize(s32 channel, u32* size) { if (size) *size = kSectorSize; return finish(channel, CARD_RESULT_READY); }
s32 CARDFormat(s32 channel)
{
	if (!validChannel(channel)) return finish(channel, CARD_RESULT_NOCARD);
	std::error_code error; fs::remove_all(root(channel), error); ensureCard(channel);
	return finish(channel, error ? CARD_RESULT_IOERROR : CARD_RESULT_READY);
}
s32 CARDFormatAsync(s32 channel, CARDCallback callback) { const s32 result = CARDFormat(channel); if (callback) callback(channel, result); return result; }
s32 CARDFreeBlocks(s32 channel, s32* bytesUnused, s32* filesUnused)
{
	std::uintmax_t used = 0; const auto files = entries(channel); std::error_code error;
	for (const auto& name : files) { used += fs::file_size(dataPath(channel, name), error); error.clear(); }
	if (bytesUnused) *bytesUnused = static_cast<s32>(used < kCapacity ? kCapacity - used : 0);
	if (filesUnused) *filesUnused = CARD_MAX_FILE - static_cast<s32>(files.size());
	return finish(channel, CARD_RESULT_READY);
}
s32 CARDGetResultCode(s32 channel) { return validChannel(channel) ? sLastResult[channel] : CARD_RESULT_NOCARD; }
s32 CARDCheck(s32 channel) { return finish(channel, ensureCard(channel) ? CARD_RESULT_READY : CARD_RESULT_NOCARD); }
s32 CARDCheckAsync(s32 channel, CARDCallback callback) { const s32 result = CARDCheck(channel); if (callback) callback(channel, result); return result; }
s32 CARDCheckExAsync(s32 channel, s32* bytes, CARDCallback callback) { if (bytes) *bytes = 0; return CARDCheckAsync(channel, callback); }

void __CARDSetDiskID(DVDDiskID*) { }
void __CARDDefaultApiCallback(s32, s32) { }
void __CARDSyncCallback(s32, s32) { }
u16 __CARDGetFontEncode() { return CARD_ENCODE_ANSI; }
s32 __CARDSync(s32 channel) { return CARDGetResultCode(channel); }

} // extern "C"
