#pragma once
// Compiled only with LAMENT_RUNTIME_PROBE=ON. Never included in the delivered DLL.
// Exercises audio calls in a disposable profile. It writes no save data.
#include "JSystem/JAudio2/JAIStream.h"
#include <cstdio>
namespace probe {
Clock::time_point started = Clock::now();
int phase = -1;
JAISound* first = nullptr;
bool holdBattle = false;
void (*bgmStart)(Z2SeqMgr*,u32,u32,s32) = nullptr;
void (*subStart)(Z2SeqMgr*,u32) = nullptr;
void (*subStop)(Z2SeqMgr*) = nullptr;
u32 savedMusic = 0;
unsigned errors = 0;
void check(bool condition, const char* name) {
    char buffer[200];
    std::snprintf(buffer,sizeof(buffer),"PROBE %s: %s", condition ? "PASS" : "FAIL", name);
    svc_log->info(mod_ctx,buffer);
    if (!condition) ++errors;
}
void snapshot(Z2SeqMgr* seq) {
    char buffer[240];
    auto* stream = replacement ? replacement->asStream() : nullptr;
    std::snprintf(buffer,sizeof(buffer),
       "PROBE state phase=%d main=%08x sub=%08x stream=%p prepared=%d gain=%.3f channels=%u samples=%u",
       phase,seq->getMainBgmID(),seq->getSubBgmID(),replacement.getSound(),
       replacement ? replacement->isPrepared() : false,fade.value(),
       stream ? stream->inner_.aramStream_.mChannelNum : 0,
       stream ? stream->inner_.aramStream_.mReadSample : 0);
    svc_log->info(mod_ctx,buffer);
}
void tick(Z2SeqMgr* seq) {
    const float age=std::chrono::duration<float>(Clock::now()-started).count();
    int next = age < 7 ? -1 : static_cast<int>((age-7)/3);
    if (next > 18) return;
    if (next != phase) {
        phase=next;
        if (!bgmStart) {
            void* address=nullptr;
            if (svc_hook->resolve(mod_ctx,"Z2SeqMgr::bgmStart",&address,nullptr)!=MOD_OK) return;
            bgmStart=reinterpret_cast<decltype(bgmStart)>(address);
            svc_hook->resolve(mod_ctx,"Z2SeqMgr::subBgmStart",&address,nullptr);
            subStart=reinterpret_cast<decltype(subStart)>(address);
            svc_hook->resolve(mod_ctx,"Z2SeqMgr::subBgmStop",&address,nullptr);
            subStop=reinterpret_cast<decltype(subStop)>(address);
        }
        switch(phase) {
        case 0:
            savedMusic=seq->getMainBgmID();
            check(!replacement,"ordinary music has no replacement stream");
            bgmStart(seq,Z2BGM_MIDNA_SOS,0,0); break;
        case 1:
            snapshot(seq);
            check(replacement && replacement->isPrepared(),"Lament stream prepared");
            if (replacement) {
                first=replacement.getSound();
                auto* stream=replacement->asStream();
                check(stream && stream->inner_.aramStream_.mChannelNum==4,"AST plays four synchronized channels");
                check(replacement->getChild(0)->mMove.params_.mVolume==1 &&
                      replacement->getChild(2)->mMove.params_.mVolume==0,"base audible, combat initially silent");
            }
            holdBattle=true; subStart(seq,Z2BGM_BATTLE_NORMAL); break;
        case 2:
            snapshot(seq);
            check(replacement.getSound()==first && fade.value()>.99f,"combat reaches full gain without restart");
            check(seq->mMainBgmHandle->getAuxiliary().params_.mVolume==0 &&
                seq->mSubBgmHandle && seq->mSubBgmHandle->getAuxiliary().params_.mVolume==0,
                "native Lament and battle are muted");
            holdBattle=false; subStop(seq); break;
        case 3:
            snapshot(seq); check(replacement.getSound()==first && fade.value()<.01f,
                "retreat reaches silence without restart");
            holdBattle=true; subStart(seq,Z2BGM_BATTLE_NORMAL); break;
        case 4:
            snapshot(seq); check(replacement.getSound()==first && fade.value()>.99f,
                "repeated battle reaches full gain without restart");
            seq->mBgmPause.move(.25f,0); break;
        case 5:
            check(replacement && replacement->getAuxiliary().params_.mVolume<.3f,
                "menu attenuation reaches replacement");
            seq->mBgmPause.move(1,0); seq->mFanfareID=Z2BGM_ITEM_GET; seq->mFanfareCount=250; seq->mFanfareMute.move(.2f,0); break;
        case 6:
            check(replacement && replacement->getAuxiliary().params_.mVolume<.25f,
                "fanfare ducking reaches replacement");
            seq->mFanfareID=0; seq->mFanfareCount=0; seq->mFanfareMute.move(1,0); seq->mSceneBgm.move(.4f,0); break;
        case 7:
            check(replacement && replacement->getAuxiliary().params_.mVolume<.45f,
                "scene attenuation reaches replacement");
            seq->mSceneBgm.move(1,0); seq->mMainBgmHandle->pause(true); break;
        case 8:
            check(replacement && replacement->isPaused(),"pause reaches replacement");
            seq->mMainBgmHandle->pause(false); holdBattle=false; subStop(seq); break;
        case 9:
            check(replacement && !replacement->isPaused(),"unpause restores replacement");
            check(fade.value()<.01f,"enemy defeat leaves combat silent");
            stopReplacement(seq);
            check(!replacement && seq->mMainBgmHandle->getAuxiliary().params_.mVolume>0,
                "stop restores native audio"); break;
        case 10:
            check(replacement && replacement->isPrepared(),"replacement starts again cleanly");
            bgmStart(seq,Z2BGM_GAME_OVER,0,0); break;
        case 11:
            check(!replacement,"death music stops replacement");
            bgmStart(seq,Z2BGM_MIDNA_SOS,0,0); break;
        case 12:
            check(replacement && replacement->isPrepared(),"Lament reentry starts replacement");
            bgmStart(seq,savedMusic,0,0); break;
        case 13:
            check(!replacement,"leaving Lament stops replacement");
            subStart(seq,Z2BGM_BATTLE_NORMAL);
            check(!replacement && seq->mSubBgmHandle,
                  "ordinary battle starts its native sound handle"); break;
        case 14:
            check(!replacement,"ordinary battle is outside replacement scope");
            subStop(seq); break;
        case 15: {
            char message[100]; std::snprintf(message,sizeof(message),"PROBE COMPLETE: %u failures",errors);
            svc_log->info(mod_ctx,message); break; }
        }
    }
    if (holdBattle) {
        seq->mFlags.mBattleBgmOff=false;
        seq->mBattleSeqCount=0;
        seq->mBattleSeqState=1;
    }
}
}
