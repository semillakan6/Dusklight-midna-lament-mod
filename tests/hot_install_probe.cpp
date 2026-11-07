// Optional Windows-only regression driver. Never included in production bundles.
// Calls the same staged-install entry point used by the in-game mod browser.
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <cstdlib>
#include <cstdio>
#include "dusk/mods/updates.hpp"
#include "mods/service.hpp"
#include "mods/svc/host.h"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.h"
#include "Z2AudioLib/Z2SeqMgr.h"
#include "Z2AudioLib/Z2SoundMgr.h"
#include "JSystem/JAudio2/JASCriticalSection.h"
DEFINE_MOD();
IMPORT_SERVICE(HostService, host);
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(LogService, test_log);
namespace {
// Layout of the pinned 2.0.3 ModOperation, whose owning shared_ptr is returned
// through the Windows member-function result buffer (as in lifecycle_probe).
struct Operation { enum class State : unsigned char { Pending, Succeeded, Failed }; State state; std::string message; };
void* (*loader)()=nullptr;
void (*install)(void*,std::shared_ptr<const Operation>*,std::filesystem::path,std::optional<dusk::mods::UpdatePrecondition>)=nullptr;
Z2SeqMgr** seq=nullptr;
Z2SoundMgr** sound=nullptr;
u32* channels=nullptr;
u8** buffer=nullptr;
void (*lock)(void*)=nullptr,(*unlock)(void*)=nullptr;
using Clock=std::chrono::steady_clock;
Clock::time_point since;
std::shared_ptr<const Operation> operation;
std::filesystem::path staged;
bool requested=false,completed=false,observed=false;
unsigned failures=0;
struct Lock { alignas(JASCriticalSection) unsigned char data[sizeof(JASCriticalSection)]; Lock(){lock(data);} ~Lock(){unlock(data);} };
void report(bool ok,const char* text){char s[300];std::snprintf(s,sizeof(s),"HOT INSTALL %s: %s",ok?"PASS":"FAIL",text);test_log->info(mod_ctx,s);if(!ok)++failures;}
}
extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError* error) {
 const char* path=std::getenv("MIDNA_TEST_STAGED_PACKAGE");
 if(!path)return mods::set_error(error,MOD_ERROR,"MIDNA_TEST_STAGED_PACKAGE must name a disposable staged package");
 staged=path;
 auto resolve=[](const char* name,auto& fn){void* p=nullptr;if(svc_hook->resolve(mod_ctx,name,&p,nullptr)!=MOD_OK)return false;fn=reinterpret_cast<std::remove_reference_t<decltype(fn)>>(p);return true;};
 if(!resolve("dusk::mods::ModLoader::instance",loader)||!resolve("dusk::mods::ModLoader::request_install",install)||
    !resolve("?sInstance@?$JASGlobalInstance@VZ2SeqMgr@@@@2PEAVZ2SeqMgr@@EA",seq)||
    !resolve("?sInstance@?$JASGlobalInstance@VZ2SoundMgr@@@@2PEAVZ2SoundMgr@@EA",sound)||
    !resolve("?sChannelMax@JASAramStream@@2IA",channels)||!resolve("?sReadBuffer@JASAramStream@@2PEAEEA",buffer)||
    !resolve("??0JASCriticalSection@@QEAA@XZ",lock)||!resolve("??1JASCriticalSection@@QEAA@XZ",unlock))
  return mods::set_error(error,MOD_ERROR,"Live-install validation symbols unavailable");
 since=Clock::now();return MOD_OK;
}
MOD_EXPORT ModResult mod_update(ModError*) {
 const float age=std::chrono::duration<float>(Clock::now()-since).count();
 if(!requested && age>=10 && seq && *seq && *buffer) {
  if(std::getenv("MIDNA_TEST_WAIT_FOR_LAMENT") && (*seq)->getMainBgmID()!=Z2BGM_MIDNA_SOS)return MOD_OK;
  {Lock guard;report(*channels==2,"first activation starts with the native two-channel reader");}
  requested=true;test_log->info(mod_ctx,"HOT INSTALL REQUEST: invoking browser staged-install path");
  install(loader(),&operation,staged,std::nullopt);
 }
 if(requested && !completed && operation && operation->state!=Operation::State::Pending){
  completed=true;report(operation->state==Operation::State::Succeeded,"staged package installed and activated");
  if(!operation->message.empty())test_log->info(mod_ctx,operation->message.c_str());
  {Lock guard;report(*channels>=4,"live installation expands the existing reader to four channels");}
 }
 if(completed && !observed && seq && *seq && (*seq)->getMainBgmID()==Z2BGM_MIDNA_SOS){
  Lock guard;
  for(auto* link=(*sound)->getStreamMgr()->getStreamList()->getFirst();link;link=link->getNext()){
   auto* s=link->getObject();
   if(s->inner_.aramStream_.mChannelNum==4 && s->isPrepared() && !s->isStopping()){
    report(true,"replacement prepares four synchronized channels after live installation");observed=true;
    char text[100];std::snprintf(text,sizeof(text),"HOT INSTALL COMPLETE: %u failures",failures);test_log->info(mod_ctx,text);break;
   }
  }
 }
 return MOD_OK;
}
MOD_EXPORT ModResult mod_shutdown(ModError*){return MOD_OK;}
}
