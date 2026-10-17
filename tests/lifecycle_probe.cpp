// Disposable-profile validation driver. Never shipped as an installed mod.
#include "mods/service.hpp"
#include "mods/svc/host.h"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.h"
#include "Z2AudioLib/Z2SoundMgr.h"
#include "Z2AudioLib/Z2SeqMgr.h"
#include "JSystem/JAudio2/JASCriticalSection.h"
#include <chrono>
#include <memory>
#include <string_view>
#include <cstdio>
#include <cstring>
DEFINE_MOD();
IMPORT_SERVICE(HostService, host);
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(LogService, test_log);
DEFINE_HOOK_SYMBOL("Z2SeqMgr::processBgmFramework", void(Z2SeqMgr*), Music);
namespace {
constexpr std::string_view id="local.ruben.midna_lament_battle";
Z2SeqMgr** seq=nullptr;
Z2SoundMgr** sound=nullptr;
void* (*loader)()=nullptr;
void (*enable)(void*,std::string_view)=nullptr, (*disable)(void*,std::string_view)=nullptr;
// Windows member functions place this before a nontrivial return buffer.
void (*reload)(void*,std::shared_ptr<void>*,std::string_view)=nullptr;
void (*start)(Z2SeqMgr*,u32,u32,s32)=nullptr;
void (*lock)(void*)=nullptr,(*unlock)(void*)=nullptr;
using Clock=std::chrono::steady_clock;
Clock::time_point since;
int stage=-1, detached=0;
unsigned failures=0, wraps=0;
u32 lastSample=0;
JAIStream* current=nullptr;
bool trackLoops=false, ready=false;
struct Lock {
 alignas(JASCriticalSection) unsigned char memory[sizeof(JASCriticalSection)];
 Lock(){lock(memory);} ~Lock(){unlock(memory);}
};
void report(bool ok,const char* name) {
 char text[220];std::snprintf(text,sizeof(text),"LIFECYCLE %s: %s",ok?"PASS":"FAIL",name);
 test_log->info(mod_ctx,text);if(!ok)++failures;
}
void detachedMod(ModContext*,ModContext*,const char* subject,ModLifecycleEvent,void*) {
 if(std::strcmp(subject,id.data())==0) ++detached;
}
JAIStream* activeStream() {
 if(!sound || !*sound)return nullptr;
 for(auto* link=(*sound)->getStreamMgr()->getStreamList()->getFirst();link;link=link->getNext()) {
  auto* s=link->getObject();
  if(s->inner_.aramStream_.mChannelNum==4 && !s->isStopping())return s;
 }
 return nullptr;
}
void music(ModContext*,void*,void*,void*) {
 if(!trackLoops)return;
 Lock guard;
 auto* s=activeStream();
 if(!s || !s->isPrepared())return;
 if(s!=current){current=s;lastSample=0;}
 const u32 sample=s->inner_.aramStream_.mReadSample;
 if(sample<lastSample && lastSample>9000000) {
  ++wraps;
  report(s->inner_.aramStream_.mChannelNum==4,"four channels remain active across native loop");
  char text[150];std::snprintf(text,sizeof(text),"NATIVE LOOP %u: sample %u -> %u; same stream=%p",wraps,lastSample,sample,s);
  test_log->info(mod_ctx,text);
 }
 lastSample=sample;
}
}
extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError* error) {
 auto resolve=[](const char* name,auto& fn) {
  void* p=nullptr;if(svc_hook->resolve(mod_ctx,name,&p,nullptr)!=MOD_OK)return false;
  fn=reinterpret_cast<std::remove_reference_t<decltype(fn)>>(p);return true;
 };
 if(!resolve("?sInstance@?$JASGlobalInstance@VZ2SeqMgr@@@@2PEAVZ2SeqMgr@@EA",seq)||
    !resolve("?sInstance@?$JASGlobalInstance@VZ2SoundMgr@@@@2PEAVZ2SoundMgr@@EA",sound)||
    !resolve("dusk::mods::ModLoader::instance",loader)||
    !resolve("dusk::mods::ModLoader::request_enable",enable)||
    !resolve("dusk::mods::ModLoader::request_disable",disable)||
    !resolve("dusk::mods::ModLoader::request_reload",reload)||
    !resolve("Z2SeqMgr::bgmStart",start)||
    !resolve("??0JASCriticalSection@@QEAA@XZ",lock)||
    !resolve("??1JASCriticalSection@@QEAA@XZ",unlock))
  return mods::set_error(error,MOD_ERROR,"Validation driver symbol resolution failed");
 uint64_t watch=0;host->watch_mod_lifecycle(mod_ctx,detachedMod,nullptr,&watch);
 HookOptions options=HOOK_OPTIONS_INIT;options.priority=-2000;
 since=Clock::now();return mods::hook::add_post<Music>(music,&options);
}
MOD_EXPORT ModResult mod_update(ModError*) {
 // Start the clock only after the real story section is playing. The title
 // screen does not run the same audio framework and is not a valid SOS test.
 if(!seq || !*seq)return MOD_OK;
 if(!ready) {
  Lock g;auto* stream=activeStream();
  if((*seq)->getMainBgmID()!=Z2BGM_MIDNA_SOS || !stream || !stream->isPrepared())return MOD_OK;
  since=Clock::now();ready=true;
 }
 const float age=std::chrono::duration<float>(Clock::now()-since).count();
 const int next=age<7?-1:static_cast<int>((age-7)/4);
 if(next==stage || next>8 || !seq || !*seq)return MOD_OK;
 stage=next;
 if(stage==0){Lock g;report(activeStream()!=nullptr,"story section is ready for lifecycle checks");}
 if(stage==1){Lock g;report(activeStream()!=nullptr,"production mod plays registered stream");disable(loader(),id);}
 if(stage==2){Lock g;report(detached>=1 && !activeStream(),"disable unloads mod and stops stream");
             const float main=(*seq)->mMainBgmHandle?(*seq)->mMainBgmHandle->getAuxiliary().params_.mVolume:0;
             const u32 subId=(*seq)->getSubBgmID();
             const bool battle=subId==Z2BGM_BATTLE_NORMAL || subId==Z2BGM_BATTLE_TWILIGHT;
             const float sub=(*seq)->mSubBgmHandle?(*seq)->mSubBgmHandle->getAuxiliary().params_.mVolume:0;
             char text[180];std::snprintf(text,sizeof(text),"NATIVE RESTORE: main=%08x sub=%08x main_gain=%.6f sub_gain=%.6f",
                (*seq)->getMainBgmID(),subId,main,sub);test_log->info(mod_ctx,text);
             // Native battles fade the main track to zero. The selected battle
             // sequence, rather than Lament, must then be audible after disable.
             report(main>0 || (battle && sub>0),"disable restores native selected music");enable(loader(),id);}
 if(stage==3){Lock g;report(activeStream()!=nullptr,"enable restarts replacement");std::shared_ptr<void> op;reload(loader(),&op,id);}
 if(stage==4){Lock g;report(detached>=2 && activeStream(),"reload detaches old DLL and starts replacement");}
 if(stage==5){Lock g;trackLoops=true;current=activeStream();lastSample=0;test_log->info(mod_ctx,"NATIVE LOOP monitoring started");}
 if(stage==6){char text[100];std::snprintf(text,sizeof(text),"LIFECYCLE COMPLETE: %u failures",failures);test_log->info(mod_ctx,text);}
 return MOD_OK;
}
MOD_EXPORT ModResult mod_shutdown(ModError*) {
 char text[100];std::snprintf(text,sizeof(text),"NATIVE LOOP COMPLETE: %u wraps, %u failures",wraps,failures);
 test_log->info(mod_ctx,text);return MOD_OK;
}
}
