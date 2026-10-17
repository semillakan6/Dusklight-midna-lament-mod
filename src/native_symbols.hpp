#pragma once
#include <string_view>
namespace native {
struct Symbol { const char* label; const char* msvc; const char* itanium; };
inline constexpr Symbol symbols[] = {
    {"moveVolume", "?moveVolume@JAISoundParamsMove@@QEAAXMI@Z", "_ZN18JAISoundParamsMove10moveVolumeEfj"},
    {"releaseSound", "?releaseSound@JAISoundHandle@@QEAAXXZ", "_ZN14JAISoundHandle12releaseSoundEv"},
    {"stopSound", "?stop@JAISound@@QEAAXI@Z", "_ZN8JAISound4stopEj"},
    {"lockPrepared", "?lockWhenPrepared@JAISoundStatus_@@QEAAHXZ", "_ZN15JAISoundStatus_16lockWhenPreparedEv"},
    {"unlockPrepared", "?unlockIfLocked@JAISoundStatus_@@QEAAHXZ", "_ZN15JAISoundStatus_14unlockIfLockedEv"},
    {"sendTask", "?sendCmdMsg@JASTaskThread@@QEAAHP6AXPEAX@Z0@Z", "_ZN13JASTaskThread10sendCmdMsgEPFvPvES0_"},
    {"blockSize", "?sBlockSize@JASAramStream@@2IA", "_ZN13JASAramStream10sBlockSizeE"},
    {"channelMax", "?sChannelMax@JASAramStream@@2IA", "_ZN13JASAramStream11sChannelMaxE"},
    {"readBuffer", "?sReadBuffer@JASAramStream@@2PEAEEA", "_ZN13JASAramStream11sReadBufferE"},
    {"loadThread", "?sLoadThread@JASAramStream@@2PEAVJASTaskThread@@EA", "_ZN13JASAramStream11sLoadThreadE"},
    {"systemHeap", "?sSystemHeap@JKRHeap@@2PEAV1@EA", "_ZN7JKRHeap11sSystemHeapE"},
    {"soundMgr", "?sInstance@?$JASGlobalInstance@VZ2SoundMgr@@@@2PEAVZ2SoundMgr@@EA", "_ZN17JASGlobalInstanceI10Z2SoundMgrE9sInstanceE"},
    {"seqMgr", "?sInstance@?$JASGlobalInstance@VZ2SeqMgr@@@@2PEAVZ2SeqMgr@@EA", "_ZN17JASGlobalInstanceI8Z2SeqMgrE9sInstanceE"},
    {"sceneMgr", "?sInstance@?$JASGlobalInstance@VZ2SceneMgr@@@@2PEAVZ2SceneMgr@@EA", "_ZN17JASGlobalInstanceI10Z2SceneMgrE9sInstanceE"},
    {"lockAudio", "??0JASCriticalSection@@QEAA@XZ", "_ZN18JASCriticalSectionC1Ev"},
    {"unlockAudio", "??1JASCriticalSection@@QEAA@XZ", "_ZN18JASCriticalSectionD1Ev"},
};
constexpr const char* symbol(std::string_view label) {
    for (const auto& entry : symbols) if (entry.label == label) {
#if defined(_WIN32)
        return entry.msvc;
#else
        return entry.itanium;
#endif
    }
    return nullptr;
}
}
