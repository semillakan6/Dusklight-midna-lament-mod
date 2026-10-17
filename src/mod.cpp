#include <future>
#include "mods/service.hpp"
#include "mods/svc/audio_res.h"
#include "mods/svc/host.h"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.h"
#include "JSystem/JAudio2/JASCriticalSection.h"
#include "JSystem/JAudio2/JASTaskThread.h"
#include "JSystem/JAudio2/JAISoundChild.h"
#include "JSystem/JAudio2/JAISeq.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "Z2AudioLib/Z2SeqMgr.h"
#include "Z2AudioLib/Z2SoundMgr.h"
#include "Z2AudioLib/Z2SoundObjMgr.h"
#include "Z2AudioLib/Z2SceneMgr.h"
#include "controller.hpp"
#include "native_symbols.hpp"
#include <chrono>
#include <cstring>


namespace native {
void (*moveVolume)(JAISoundParamsMove*, f32, u32) = nullptr;
void (*releaseSound)(JAISoundHandle*) = nullptr;
void (*stopSound)(JAISound*, u32) = nullptr;
s32 (*lockPrepared)(JAISoundStatus_*) = nullptr;
s32 (*unlockPrepared)(JAISoundStatus_*) = nullptr;
int (*sendTask)(JASTaskThread*, JASThreadCallback, void*) = nullptr;
u32 *blockSize = nullptr, *channelMax = nullptr;
u8** readBuffer = nullptr;
JASTaskThread** loadThread = nullptr;
JKRHeap** systemHeap = nullptr;
Z2SoundMgr** soundMgr = nullptr;
Z2SeqMgr** seqMgr = nullptr;
Z2SceneMgr** sceneMgr = nullptr;
}
// Release builds keep several public-header functions in their symbol database
// without exporting them. These local thunks use the official resolver. No game
// executable patch or replacement import library is needed.
void JAISoundParamsMove::moveVolume(f32 value, u32 steps) {
    native::moveVolume(this, value, steps);
}
void JAISoundHandle::releaseSound() {
    if (sound_ && native::releaseSound) native::releaseSound(this);
}
void JAISound::stop(u32 frames) { native::stopSound(this, frames); }
s32 JAISoundStatus_::lockWhenPrepared() { return native::lockPrepared(this); }
s32 JAISoundStatus_::unlockIfLocked() { return native::unlockPrepared(this); }
int JASTaskThread::sendCmdMsg(JASThreadCallback cb, void* msg) {
    return native::sendTask(this, cb, msg);
}

DEFINE_MOD();
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(HostService, svc_host);
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(AudioResService, svc_audio_res);

DEFINE_HOOK_SYMBOL("Z2SeqMgr::processBgmFramework", void(Z2SeqMgr*), MusicUpdate);
DEFINE_HOOK_SYMBOL("Z2SoundObjMgr::searchEnemy", void(Z2SoundObjMgr*), EnemySearch);
DEFINE_HOOK_SYMBOL("Z2SeqMgr::checkBgmIDPlaying", bool(Z2SeqMgr*, u32), CheckMusic);
DEFINE_HOOK_SYMBOL("JASAramStream::initSystem", void(u32, u32), InitStreams);

// Pointer spelling preserves the reference ABI and avoids the SDK's const-ref
// trampoline limitation. The mix parameters are never changed by this hook.
DEFINE_HOOK_SYMBOL("JAISeq::mixOut_", void(JAISeq*, const JASSoundParams*, JAISoundActivity), NativeMix);
DEFINE_HOOK_SYMBOL("Z2SeqMgr::bgmStart", void(Z2SeqMgr*, u32, u32, s32), MusicSelected);
DEFINE_HOOK_SYMBOL("Z2SceneMgr::setSceneName", void(Z2SceneMgr*, char*, s32, s32), SceneSelected);

namespace {
JAISoundHandle replacement;
AudioSoundTableHandle streamRegistration = 0;
u16 streamIndex = 0;
lament::CombatFade fade;
lament::MainGainGuard mainGainGuard;
unsigned searchDepth = 0;
bool shuttingDown = false;
bool originalMuted = false;
bool startWarned = false;
using Clock = std::chrono::steady_clock;
using LockFn = void (*)(void*);
LockFn lockAudio = nullptr, unlockAudio = nullptr;
// The release's lock ctor/dtor are local symbols, so resolve them through the
// official symbol service instead of adding them to the game's import table.
class AudioLock {
    alignas(JASCriticalSection) unsigned char storage[sizeof(JASCriticalSection)];
public:
    AudioLock() { lockAudio(storage); }
    ~AudioLock() { unlockAudio(storage); }
};
Clock::time_point lastUpdate;
bool haveTime = false;
bool scenePreload = false;

bool battleId(u32 id) {
    return lament::isBattle(id, Z2BGM_BATTLE_NORMAL, Z2BGM_BATTLE_TWILIGHT);
}
float commonGain(Z2SeqMgr* seq) {
    return seq->mAllBgmMaster.get() * seq->mBgmPause.get() *
           seq->mFanfareMute.get() * seq->mWindStone.get() * seq->mTwilightGateVol;
}
float sceneGain(Z2SeqMgr* seq) {
    return commonGain(seq) * seq->mSceneBgm.get() * seq->mStreamBgmMaster.get() *
           seq->field_0x84.get() * seq->field_0xa4.get();
}
void restoreOriginal(Z2SeqMgr* seq) {
    if (!originalMuted || !seq) return;
    if (seq->getMainBgmID() == Z2BGM_MIDNA_SOS)
        seq->mMainBgmHandle->getAuxiliary().moveVolume(sceneGain(seq) * seq->mMainBgmMaster.get(), 0);
    if (battleId(seq->getSubBgmID()))
        seq->mSubBgmHandle->getAuxiliary().moveVolume(
            commonGain(seq) * seq->mSubBgmMaster.get() * seq->mStreamBgmMaster.get(), 0);
    originalMuted = false;
}
void stopReplacement(Z2SeqMgr* seq) {
    scenePreload = false;
    if (replacement) {
        replacement->stop(0);
        replacement.releaseSound();
        svc_log->info(mod_ctx, "Adaptive stream stopped");
    }
    restoreOriginal(seq);
    fade.reset();
    mainGainGuard.reset();
    haveTime = false;
}
bool startReplacement(Z2SeqMgr* seq) {
    // v2.0.3 JAIStreamMgr::startSound returns false even on success.
    // The attached handle is the reliable outcome, including async preparation.
    (*native::soundMgr)->startSound(JAISoundID(0x02000000u | streamIndex), &replacement, nullptr);
    if (!replacement) {
        if (!startWarned) svc_log->warn(mod_ctx, "Stream slot unavailable; keeping native audio until it is free");
        startWarned = true;
        return false;
    }
    // Initialize gains before the asynchronous stream starts. Never let the
    // four channels start at their default full volume.
    replacement->lockWhenPrepared();
    replacement->getAuxiliary().moveVolume(0.0f, 0);
    for (int i = 0; i < 4; ++i) {
        auto* child = replacement->getChild(i);
        if (!child) {
            stopReplacement(seq);
            svc_host->fail(mod_ctx, MOD_ERROR, "Unable to allocate stream channel controls");
            return false;
        }
        child->mMove.moveVolume(i < 2 ? 1.0f : 0.0f, 0);
    }
    fade.reset();
    mainGainGuard.reset();
    haveTime = false;
    startWarned = false;
    svc_log->info(mod_ctx, "Adaptive stream preparing (base L/R + battle L/R)");
    return true;
}

#ifdef LAMENT_RUNTIME_PROBE
#include "../tests/runtime_probe.hpp"
#endif

void updateMusic(ModContext*, void* args, void*, void*) {
    if (shuttingDown) return;
    auto* seq = mods::arg<Z2SeqMgr*>(args, 0);
    AudioLock audioLock;
#ifdef LAMENT_RUNTIME_PROBE
    if (probe::holdBattle) {
        seq->mFlags.mBattleBgmOff=false;
        seq->mBattleSeqCount=0;
        seq->mBattleSeqState=1;
    }
    struct ProbeAtExit { Z2SeqMgr* seq; ~ProbeAtExit() { probe::tick(seq); } } probeAtExit{seq};
#endif
    if (seq->getMainBgmID() != Z2BGM_MIDNA_SOS ||
        !seq->mMainBgmHandle || seq->mMainBgmHandle->isStopping()) {
        // Scene selection can precede the gameplay BGM by several loading frames.
        // Keep that stream locked at sample zero while the outgoing music plays.
        if (scenePreload && replacement && !replacement->isStopping() &&
            (*native::sceneMgr)->BGM_ID == Z2BGM_MIDNA_SOS) return;
        stopReplacement(seq);
        return;
    }
    scenePreload = false;
    if (replacement && replacement->isStopping()) stopReplacement(seq);
    if (!replacement && !startReplacement(seq)) return;
    // Own the handoff as soon as the stream handle is attached. Waiting for
    // async preparation here leaked the native intro for a few audio frames.
    seq->mMainBgmHandle->getAuxiliary().moveVolume(0.0f, 0);
    if (battleId(seq->getSubBgmID())) seq->mSubBgmHandle->getAuxiliary().moveVolume(0.0f, 0);
    originalMuted = true;
    if (!replacement->isPrepared()) return;

    replacement->unlockIfLocked();
    const auto now = Clock::now();
    float dt = haveTime ? std::chrono::duration<float>(now - lastUpdate).count() : 0.0f;
    lastUpdate = now;
    haveTime = true;
    const bool paused = seq->mMainBgmHandle->isPaused() ||
        (*native::soundMgr)->getSeqMgr()->mActivity.field_0x0.flags.flag2;
    // Native stream-manager pause handles the shared playback cursor.
    replacement->pause(seq->mMainBgmHandle->isPaused());
    if (paused) dt = 0.0f;
    dt = std::clamp(dt, 0.0f, 0.1f);

    const bool combat = lament::combatRequested(seq->getSubBgmID(),
        Z2BGM_BATTLE_NORMAL, Z2BGM_BATTLE_TWILIGHT, seq->mBattleSeqCount,
        seq->mFlags.mBattleBgmOff, (*native::sceneMgr)->isSceneExist());
    fade.setCombat(combat);
    const float battleGain = fade.advance(dt);
    replacement->getChild(2)->mMove.moveVolume(battleGain, 0);
    replacement->getChild(3)->mMove.moveVolume(battleGain, 0);

    // Bypass the main BGM's combat-only fade, while retaining ducking due to
    // dialogue, menus, fanfares, scene fades and a non-battle sub-BGM takeover.
    const bool battle = battleId(seq->getSubBgmID());
    const bool battleContext = battle || seq->mBattleSeqState != 0 || seq->mBattleSeqCount != 0;
    const bool otherSubMusic = seq->getSubBgmID() != 0xffffffffu && !battle;
    const float mainGain = mainGainGuard.select(battleContext, otherSubMusic,
        seq->mMainBgmMaster.get(), seq->mMainBgmMaster.getDest());
    replacement->getAuxiliary().moveVolume(
        sceneGain(seq) * mainGain * seq->mMainBgmHandle->getFader()->getIntensity(), 0);
    seq->mMainBgmHandle->getAuxiliary().moveVolume(0.0f, 0);
    if (battle) seq->mSubBgmHandle->getAuxiliary().moveVolume(0.0f, 0);
    originalMuted = true;
}
void prepareScene(ModContext*, void* args, void*, void*) {
    if (shuttingDown || !(*native::soundMgr) || !(*native::seqMgr)) return;
    auto* scene = mods::arg<Z2SceneMgr*>(args, 0);
    auto* seq = (*native::seqMgr);
    AudioLock audioLock;
    if (scene->BGM_ID == Z2BGM_MIDNA_SOS && seq->getMainBgmID() != Z2BGM_MIDNA_SOS) {
        if (replacement && replacement->isStopping()) stopReplacement(seq);
        if (!replacement) scenePreload = startReplacement(seq);
        if (scenePreload) svc_log->info(mod_ctx, "Adaptive stream preloading during scene setup; cursor locked at opening");
    } else if (scenePreload) {
        stopReplacement(seq);
    }
}
void selectMusic(ModContext* ctx, void* args, void* result, void* userData) {
    if (shuttingDown) return;
    auto* seq = mods::arg<Z2SeqMgr*>(args, 0);
    AudioLock audioLock;
    if (seq->getMainBgmID() != Z2BGM_MIDNA_SOS) {
        if (scenePreload && mods::arg<u32>(args, 1) != 0xffffffffu) stopReplacement(seq);
        return;
    }
    const bool ready = replacement && replacement->isPrepared();
    // Initialize replacement gain and suppress the native sequence before the
    // first music-framework tick, including direct cutscene BGM selections.
    updateMusic(ctx, args, result, userData);
    svc_log->info(mod_ctx, ready ? "Adaptive handoff: preloaded stream ready at music selection" :
                                 "Adaptive handoff: native intro suppressed while stream prepares");
}
HookAction silenceNativeMix(ModContext*, void* args, void*, void*) {
    if (shuttingDown || !(*native::seqMgr)) return HOOK_CONTINUE;
    auto* seq = (*native::seqMgr);
    auto* sound = mods::arg<JAISeq*>(args, 0);
    AudioLock audioLock;
    if (seq->getMainBgmID() != Z2BGM_MIDNA_SOS || !seq->mMainBgmHandle ||
        seq->mMainBgmHandle->isStopping()) return HOOK_CONTINUE;
    const bool main = seq->mMainBgmHandle.getSound() == sound;
    const bool battle = seq->mSubBgmHandle.getSound() == sound && battleId(seq->getSubBgmID());
    if (!main && !battle) return HOOK_CONTINUE;
    // Also cover a native mixer call before bgmStart's post hook returns, and
    // callback-driven sequence mixes between music-framework ticks.
    if (!replacement && !startReplacement(seq)) return HOOK_CONTINUE;
    if (replacement->isStopping()) return HOOK_CONTINUE;
    sound->getAuxiliary().moveVolume(0.0f, 0);
    originalMuted = true;
    return HOOK_CONTINUE;
}
HookAction beginEnemySearch(ModContext*, void*, void*, void*) {
    // The user's nonstop-Lament option returns early from searchEnemy().
    // Bypass only its music-ID query in this call, without changing saved config.
    if (auto* seq = (*native::seqMgr); seq && seq->getMainBgmID() == Z2BGM_MIDNA_SOS)
        ++searchDepth;
    return HOOK_CONTINUE;
}
void endEnemySearch(ModContext*, void*, void*, void*) {
    if (searchDepth) --searchDepth;
}
HookAction checkMusic(ModContext*, void* args, void* result, void*) {
    if (searchDepth && mods::arg<u32>(args, 1) == Z2BGM_MIDNA_SOS) {
        *static_cast<bool*>(result) = false;
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}
HookAction initializeStreams(ModContext*, void* args, void*, void*) {
    auto& maxChannels = mods::arg_ref<u32>(args, 1);
    if (maxChannels < 4) maxChannels = 4;
    return HOOK_CONTINUE;
}
struct ResizeJob { std::promise<bool> done; };
void resizeOnDvdThread(void* raw) {
    auto* job = static_cast<ResizeJob*>(raw);
    bool ok = true;
    // All reads into sReadBuffer run on this serialized worker. Swapping here
    // avoids freeing/changing a buffer while a DVD read is still using it.
    if ((*native::channelMax) < 4) {
        auto* buffer = static_cast<u8*>(JKRHeap::alloc(
            ((*native::blockSize) + 0x20) * 4, 32, (*native::systemHeap)));
        ok = buffer != nullptr;
        if (ok) {
            (*native::readBuffer) = buffer;
            (*native::channelMax) = 4;
        }
    }
    job->done.set_value(ok);
}
bool ensureFourChannels() {
    if (!(*native::readBuffer) || (*native::channelMax) >= 4) return true;
    if (!(*native::loadThread)) return false;
    ResizeJob job;
    auto done = job.done.get_future();
    if (!(*native::loadThread)->sendCmdMsg(resizeOnDvdThread, &job)) return false;
    // Do not unload the callback or destroy its job while it is queued.
    return done.get();
}
}

extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError* error) {
    svc_log->info(mod_ctx, svc_host->version);
    const char* version = svc_host->version;
    if (*version == 'v') ++version;
    if (std::strncmp(version, "2.0.3", 5) != 0 ||
        (version[5] && std::strcmp(version + 5, ".0") != 0 && version[5] != '-' && version[5] != '+' && version[5] != ' '))
        return mods::set_error(error, MOD_UNAVAILABLE, "This build requires Dusklight 2.0.3");
    const auto resolve = [](const char* name, auto& target) {
        void* address = nullptr;
        if (svc_hook->resolve(mod_ctx, name, &address, nullptr) != MOD_OK) {
            svc_log->error(mod_ctx, name);
            return false;
        }
        target = reinterpret_cast<std::remove_reference_t<decltype(target)>>(address);
        return true;
    };
    if (!resolve(native::symbol("moveVolume"), native::moveVolume) ||
        !resolve(native::symbol("releaseSound"), native::releaseSound) ||
        !resolve(native::symbol("stopSound"), native::stopSound) ||
        !resolve(native::symbol("lockPrepared"), native::lockPrepared) ||
        !resolve(native::symbol("unlockPrepared"), native::unlockPrepared) ||
        !resolve(native::symbol("sendTask"), native::sendTask) ||
        !resolve(native::symbol("blockSize"), native::blockSize) ||
        !resolve(native::symbol("channelMax"), native::channelMax) ||
        !resolve(native::symbol("readBuffer"), native::readBuffer) ||
        !resolve(native::symbol("loadThread"), native::loadThread) ||
        !resolve(native::symbol("systemHeap"), native::systemHeap) ||
        !resolve(native::symbol("soundMgr"), native::soundMgr) ||
        !resolve(native::symbol("seqMgr"), native::seqMgr) ||
        !resolve(native::symbol("sceneMgr"), native::sceneMgr))
        return mods::set_error(error, MOD_UNAVAILABLE, "Unable to resolve release audio symbols");
    void* lock = nullptr;
    void* unlock = nullptr;
    if (svc_hook->resolve(mod_ctx, native::symbol("lockAudio"), &lock, nullptr) != MOD_OK ||
        svc_hook->resolve(mod_ctx, native::symbol("unlockAudio"), &unlock, nullptr) != MOD_OK)
        return mods::set_error(error, MOD_UNAVAILABLE, "Unable to resolve the native audio lock");
    lockAudio = reinterpret_cast<LockFn>(lock);
    unlockAudio = reinterpret_cast<LockFn>(unlock);
    shuttingDown = false;
    HookOptions last = HOOK_OPTIONS_INIT;
    last.priority = -1000; // Apply gain after ordinary music hooks.
    ModResult result = mods::hook::add_pre<InitStreams>(initializeStreams);
    if (result != MOD_OK || !ensureFourChannels())
        return mods::set_error(error, MOD_ERROR, "Unable to prepare four-channel audio reader");

    AudioSoundTableStreamInfo info = *svc_audio_res->default_stream_info;
    info.volume = 1.0f;
    info.pan_parameters[0] = STREAM_PAN_LEFT;
    info.pan_parameters[1] = STREAM_PAN_RIGHT;
    info.pan_parameters[2] = STREAM_PAN_LEFT;
    info.pan_parameters[3] = STREAM_PAN_RIGHT;
    // v2.0.3 maps true to STREAM_ALT. Its scene controller preserves ALT
    // streams across same-BGM room changes; main-ID changes are handled above.
    info.stop_on_scene_change = true;
    result = svc_audio_res->add_sound_table_stream(mod_ctx,
        "/Audiores/Stream/midna_lament_adaptive.ast", &info, &streamRegistration, &streamIndex);
    if (result != MOD_OK)
        return mods::set_error(error, result, "Unable to register adaptive music");

    result = mods::hook::add_post<MusicUpdate>(updateMusic, &last);
    if (result == MOD_OK) result = mods::hook::add_pre<EnemySearch>(beginEnemySearch);
    if (result == MOD_OK) result = mods::hook::add_post<EnemySearch>(endEnemySearch);
    if (result == MOD_OK) result = mods::hook::add_pre<CheckMusic>(checkMusic);
    if (result == MOD_OK) result = mods::hook::add_post<SceneSelected>(prepareScene, &last);
    if (result == MOD_OK) result = mods::hook::add_post<MusicSelected>(selectMusic, &last);
    if (result == MOD_OK) result = mods::hook::add_pre<NativeMix>(silenceNativeMix, &last);
    if (result != MOD_OK)
        return mods::set_error(error, result, "Unable to install scoped music hooks");
    static_assert(lament::LamentId == Z2BGM_MIDNA_SOS);
    svc_log->info(mod_ctx, "Ready: Dusklight 2.0.3; combat fades 0.5s / 1.5s");
    return MOD_OK;
}
MOD_EXPORT ModResult mod_update(ModError*) { return MOD_OK; }
MOD_EXPORT ModResult mod_shutdown(ModError*) {
    shuttingDown = true;
    if (!lockAudio || !unlockAudio) return MOD_OK;
    AudioLock audioLock;
    stopReplacement((*native::seqMgr));
    searchDepth = 0;
    if (streamRegistration) {
        svc_audio_res->remove_sound_table(mod_ctx, streamRegistration);
        streamRegistration = 0;
    }
    // Expanded reader memory belongs to the game, so async stop tasks and any
    // other multichannel streams remain safe after this DLL is unloaded.
    return MOD_OK;
}
}
