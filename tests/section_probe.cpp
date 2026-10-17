// Optional observer for the supplied story save, loaded only in disposable profiles.
// No BGM calls, combat-state overrides, actor creation, movement or save writes.
#include "mods/service.hpp"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.h"
#include "Z2AudioLib/Z2SeqMgr.h"
#include "Z2AudioLib/Z2SoundMgr.h"
#include "Z2AudioLib/Z2SoundObjMgr.h"
#include "Z2AudioLib/Z2SceneMgr.h"
#include "JSystem/JAudio2/JASCriticalSection.h"
#include "JSystem/JAudio2/JAISoundChild.h"
#include "JSystem/JAudio2/JAISeq.h"
#include <chrono>
#include <cstdio>
DEFINE_MOD();
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(LogService, test_log);
DEFINE_HOOK_SYMBOL("Z2SeqMgr::processBgmFramework", void(Z2SeqMgr*), Music);
DEFINE_HOOK_SYMBOL("JAISeq::mixOut_", void(JAISeq*, const JASSoundParams*, JAISoundActivity), SequenceMix);
DEFINE_HOOK_SYMBOL("Z2SeqMgr::bgmStart", void(Z2SeqMgr*,u32,u32,s32), StartMusic);
namespace {
using Clock=std::chrono::steady_clock;
Clock::time_point since,changed;
bool started=false, target=false;
int lastSecond=-1;
unsigned failures=0,checks=0,entries=0,exits=0;
unsigned nativeMixes=0,audibleNativeMixes=0,starts=0,readyStarts=0;
Z2SceneMgr** scene=nullptr;
Z2SeqMgr** sequence=nullptr;
Z2SoundMgr** sound=nullptr;
Z2SoundObjMgr** objects=nullptr;
void (*lock)(void*)=nullptr,(*unlock)(void*)=nullptr;
JAIStream* initial=nullptr;
u32 lastSample=0;
struct Lock {
 alignas(JASCriticalSection) unsigned char memory[sizeof(JASCriticalSection)];
 Lock(){lock(memory);} ~Lock(){unlock(memory);}
};
void report(bool ok,const char* name) {
 ++checks;if(!ok)++failures;
 char text[240];std::snprintf(text,sizeof(text),"SECTION %s: %s",ok?"PASS":"FAIL",name);
 test_log->info(mod_ctx,text);
}
JAIStream* activeStream() {
 if(!sound || !*sound)return nullptr;
 for(auto* l=(*sound)->getStreamMgr()->getStreamList()->getFirst();l;l=l->getNext()) {
  auto* s=l->getObject();
  if(s->inner_.aramStream_.mChannelNum==4 && !s->isStopping())return s;
 }
 return nullptr;
}
void inspectNativeMix(ModContext*,void* args,void*,void*) {
 Lock guard;auto* seq=*sequence;auto* sound=mods::arg<JAISeq*>(args,0);
 if(!seq || seq->getMainBgmID()!=Z2BGM_MIDNA_SOS || seq->mMainBgmHandle.getSound()!=sound)return;
 ++nativeMixes;
 const float volume=sound->inner_.mSoundParams.mVolume;
 if(volume>0.000001f)++audibleNativeMixes;
 if(nativeMixes==1) {
  char text[150];std::snprintf(text,sizeof(text),"HANDOFF first native mix: volume=%.6f replacement_prepared=%d",
    volume,activeStream() && activeStream()->isPrepared());
  test_log->info(mod_ctx,text);
 }
}
void inspectStart(ModContext*,void* args,void*,void*) {
 if(mods::arg<u32>(args,1)!=Z2BGM_MIDNA_SOS)return;
 Lock guard;++starts;auto* stream=activeStream();
 if(stream && stream->isPrepared())++readyStarts;
 char text[160];std::snprintf(text,sizeof(text),"HANDOFF music selected: replacement_prepared=%d cursor=%u",
  stream && stream->isPrepared(),stream?stream->inner_.aramStream_.mReadSample:0);
 test_log->info(mod_ctx,text);
}
void music(ModContext*,void*,void*,void*) {
 Lock guard;auto* seq=*sequence;auto* stream=activeStream();auto now=Clock::now();
 if(!started && seq->getMainBgmID()==Z2BGM_MIDNA_SOS && stream && stream->isPrepared()) {
  started=true;since=changed=now;initial=stream;
  report(true,"supplied slot selects Lament and prepares four channels naturally");
 }
 if(!started || seq->getMainBgmID()!=Z2BGM_MIDNA_SOS)return;
 bool battle=seq->getSubBgmID()==Z2BGM_BATTLE_NORMAL || seq->getSubBgmID()==Z2BGM_BATTLE_TWILIGHT;
 bool requested=battle && seq->mBattleSeqCount==0 && !seq->mFlags.mBattleBgmOff && (*scene)->isSceneExist();
 if(requested!=target) {
  target=requested;changed=now;
  if(target)++entries;else ++exits;
  test_log->info(mod_ctx,target?"SECTION native combat entered":"SECTION native combat exited");
 }
 float age=std::chrono::duration<float>(now-since).count();
 int second=static_cast<int>(age);
 if(second==lastSecond)return;
 lastSecond=second;
 report(stream && stream==initial && stream->isPrepared(),"same prepared stream retained");
 if(!stream)return;
 const u32 sample=stream->inner_.aramStream_.mReadSample;
 report(second==0 || sample>lastSample || (lastSample>9000000 && sample<lastSample),
        "native shared cursor continues advancing");
 lastSample=sample;
 float base=stream->getChild(0)->mMove.params_.mVolume;
 float combat=stream->getChild(2)->mMove.params_.mVolume;
 report(base==1 && stream->getChild(1)->mMove.params_.mVolume==1,"base channels stay audible during native combat");
 report(combat>=0 && combat<=1 && stream->getChild(3)->mMove.params_.mVolume==combat,
        "combat channels retain matched valid gains");
 report(seq->mMainBgmHandle && seq->mMainBgmHandle->getAuxiliary().params_.mVolume==0 &&
        (!battle || (seq->mSubBgmHandle && seq->mSubBgmHandle->getAuxiliary().params_.mVolume==0)),
        "native Lament and selected battle audio suppressed");
 float settled=std::chrono::duration<float>(now-changed).count();
 if(settled>2)report(target?combat>.99f:combat<.01f,"settled combat gain follows actual native battle state");
 char text[310];
 std::snprintf(text,sizeof(text),"SECTION state t=%d main=%08x sub=%08x near=%u far=%u off=%d count=%u state=%u stream=%p base=%.3f combat=%.3f gain=%.3f sample=%u",
    second,seq->getMainBgmID(),seq->getSubBgmID(),(*objects)->getEnemyNumNear(),
    (*objects)->getEnemyNumVeryFar(),seq->mFlags.mBattleBgmOff,seq->mBattleSeqCount,
    seq->mBattleSeqState,stream,base,combat,stream->getAuxiliary().params_.mVolume,sample);
 test_log->info(mod_ctx,text);
}
}
extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError* error) {
 auto resolve=[](const char* name,auto& fn) {
  void* p=nullptr;if(svc_hook->resolve(mod_ctx,name,&p,nullptr)!=MOD_OK)return false;
  fn=reinterpret_cast<std::remove_reference_t<decltype(fn)>>(p);return true;
 };
 if(!resolve("?sInstance@?$JASGlobalInstance@VZ2SeqMgr@@@@2PEAVZ2SeqMgr@@EA",sequence) ||
    !resolve("?sInstance@?$JASGlobalInstance@VZ2SoundMgr@@@@2PEAVZ2SoundMgr@@EA",sound) ||
    !resolve("?sInstance@?$JASGlobalInstance@VZ2SceneMgr@@@@2PEAVZ2SceneMgr@@EA",scene) ||
    !resolve("?sInstance@?$JASGlobalInstance@VZ2SoundObjMgr@@@@2PEAVZ2SoundObjMgr@@EA",objects) ||
    !resolve("??0JASCriticalSection@@QEAA@XZ",lock) ||
    !resolve("??1JASCriticalSection@@QEAA@XZ",unlock))
  return mods::set_error(error,MOD_ERROR,"Story observer symbols unavailable");
 HookOptions options=HOOK_OPTIONS_INIT;options.priority=-2000;
 auto r=mods::hook::add_post<Music>(music,&options);
 if(r==MOD_OK)r=mods::hook::add_post<SequenceMix>(inspectNativeMix,&options);
 if(r==MOD_OK)r=mods::hook::add_post<StartMusic>(inspectStart,&options);
 return r;
}
MOD_EXPORT ModResult mod_update(ModError*) {return MOD_OK;}
MOD_EXPORT ModResult mod_shutdown(ModError*) {
 char text[180];
 std::snprintf(text,sizeof(text),"HANDOFF COMPLETE: %u native mixes, %u audible native mixes, %u ready starts, %u starts checked",
   nativeMixes,audibleNativeMixes,readyStarts,starts);
 test_log->info(mod_ctx,text);
 std::snprintf(text,sizeof(text),"SECTION COMPLETE: %u checks, %u failures, %u native entries, %u native exits",
   checks,failures,entries,exits);
 test_log->info(mod_ctx,text);return MOD_OK;
}
}
