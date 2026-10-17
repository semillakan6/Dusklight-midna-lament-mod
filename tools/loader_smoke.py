"""Launch a COPIED release in a disposable profile; original saves are hashed.
--game, --mods, and --profile must be outside --source-profile.
The game may reuse its normal graphics caches, but never its normal config/saves.
"""
import argparse,subprocess,json,shutil,hashlib,time,ctypes,zipfile
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument("--game",type=Path,required=True)
p.add_argument("--mods",type=Path,required=True)
p.add_argument("--profile",type=Path,required=True)
p.add_argument("--source-profile",type=Path,required=True)
p.add_argument("--seconds",type=int,default=20)
p.add_argument("--load-save",type=int,default=0)
p.add_argument("--section-save",type=Path)
p.add_argument("--backend",default="d3d12",choices=["d3d12","vulkan","null"])
p.add_argument("--include-installed-mods",action="store_true")
a=p.parse_args()
source=a.source_profile.resolve()
for destination in (a.game.resolve(),a.mods.resolve(),a.profile.resolve()):
 if destination==source or source in destination.parents:
  raise SystemExit("Refusing to run any test inside the actual user profile")
if a.profile.resolve() in source.parents:
 raise SystemExit("The disposable profile cannot contain the source profile")
def fingerprint():
 paths=[source/"config.json"]+[p for region in ("USA","EUR","JAP") for p in (source/region).rglob("*") if p.is_file()]
 return {str(p.relative_to(source)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
before=fingerprint()
section_hash=hashlib.sha256(a.section_save.read_bytes()).hexdigest() if a.section_save else None
a.profile.mkdir(parents=True,exist_ok=True)
config=json.loads((source/"config.json").read_text(encoding="utf-8"))
def enabled_key(mod_id):return "mod."+mod_id.replace("_","__").replace(".","_")+".enabled"
for setting in list(config):
 if setting.startswith("mod.") and setting.endswith(".enabled"):config[setting]=False
for package in (a.game.parent/"mods").glob("*.dusk"):
 with zipfile.ZipFile(package) as z:config[enabled_key(json.loads(z.read("mod.json"))["id"])]=False
for package in a.mods.glob("*.dusk"):
 with zipfile.ZipFile(package) as z:config[enabled_key(json.loads(z.read("mod.json"))["id"])]=True
config["game.pauseOnFocusLost"]=False
(a.profile/"config.json").write_text(json.dumps(config,indent=2),encoding="utf-8")
if a.section_save:
 data=a.section_save.read_bytes()
 assert data[:6]==b"GZ2E01" and data[8:40].split(b"\0")[0]==b"gczelda2", "Expected a US Twilight Princess GCI"
 assert len(data)==64+int.from_bytes(data[56:58],"big")*8192,"Invalid GCI block count"
 card=a.profile/"USA"/"Card A"
 card.mkdir(parents=True,exist_ok=True)
 shutil.copy2(a.section_save,card/"01-GZ2E-gczelda2.gci")
elif a.load_save:
 shutil.copytree(source/"USA",a.profile/"USA",dirs_exist_ok=True)
mods=a.mods
if a.include_installed_mods:
 mods=a.profile/"test-mods"
 shutil.copytree(source/"mods",mods,dirs_exist_ok=True)
 for bundle in a.mods.glob("*.dusk"):
  shutil.copy2(bundle,mods/bundle.name)
args=[str(a.game),"--user-dir",str(a.profile),"--mods",str(mods),
      "--backend",a.backend,"--log-dir",str(a.profile/"logs")]
if a.load_save: args+=["--load-save",str(a.load_save)]
timed_out=False
start=time.monotonic()
process=None
try:
 with (a.profile/"stdout.log").open("wb") as log:
  startup=subprocess.STARTUPINFO()
  startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW
  startup.wShowWindow=0
  process=subprocess.Popen(args,cwd=a.game.parent,stdout=log,stderr=subprocess.STDOUT,startupinfo=startup)
  print("Started isolated process",process.pid,flush=True)
  try: process.wait(timeout=a.seconds)
  except subprocess.TimeoutExpired:
   timed_out=True
   # Terminate ONLY the child started above, never any pre-existing game.
   # Ask only our child window to close, allowing loader shutdown/log flushing.
   user32=ctypes.windll.user32
   callback=ctypes.WINFUNCTYPE(ctypes.c_bool,ctypes.c_void_p,ctypes.c_void_p)
   def close_child_window(hwnd,unused):
    pid=ctypes.c_ulong()
    user32.GetWindowThreadProcessId(ctypes.c_void_p(hwnd),ctypes.byref(pid))
    if pid.value==process.pid: user32.PostMessageW(ctypes.c_void_p(hwnd),0x10,0,0)
    return True
   user32.EnumWindows(callback(close_child_window),0)
   try: process.wait(timeout=8)
   except subprocess.TimeoutExpired:
    process.terminate();process.wait(timeout=10)
  print("Test timeout" if timed_out else "Game exited",process.returncode,flush=True)
finally:
 after=fingerprint()
 unchanged=before==after
 if a.section_save: assert hashlib.sha256(a.section_save.read_bytes()).hexdigest()==section_hash,"Source section save changed"
 report={"profile":str(a.profile.resolve()),"original_profile":str(source),
         "original_save_and_config_hashes_unchanged":unchanged,
         "original_file_hashes":before,"section_save_sha256":section_hash,"elapsed_seconds":round(time.monotonic()-start,2),
         "terminated_at_test_timeout":timed_out,"game_exit_code":process.returncode if process else None,
         "includes_copies_of_installed_mods":a.include_installed_mods}
 (a.profile/"smoke-report.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
 assert unchanged,"Original save/config changed during test"
 print("Original save/config hashes unchanged:",unchanged,flush=True)
for log in sorted((a.profile/"logs").glob("*.log")):
 for line in log.read_text(encoding="utf-8",errors="replace").splitlines():
  if any(w in line.lower() for w in ["midna_lament","adaptive","failed:","fatal","probe ","lifecycle ","native loop","section "]):
   print(line)

if process and process.returncode != 0: raise SystemExit(f"Isolated game exited unsuccessfully: {process.returncode}")
