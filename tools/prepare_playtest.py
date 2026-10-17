"""Prepare or open an isolated interactive playtest. Never imports a save into
the real profile. The launcher rechecks original save/config hashes each run.
"""
import argparse,hashlib,json,shutil,subprocess,sys
from pathlib import Path

def sha(path):
 return hashlib.sha256(path.read_bytes()).hexdigest()

def fingerprint(source):
 paths=[source/"config.json"]+[p for region in ("USA","EUR","JAP")
        for p in (source/region).rglob("*") if p.is_file()]
 return {str(p.relative_to(source)):sha(p) for p in paths}

def outside(path,source):
 path=path.resolve()
 if path==source or source in path.parents or path in source.parents:
  raise SystemExit("The test destination must be separate from the actual profile")

def run(manifest):
 info=json.loads(manifest.read_text(encoding="utf-8"))
 source=Path(info["source_profile"]).resolve()
 profile=Path(info["profile"]).resolve()
 game=Path(info["game"]).resolve()
 supplied=Path(info["section_save"]).resolve()
 for path in (profile,game):outside(path,source)
 if manifest.resolve()!=profile/"playtest.json":
  raise SystemExit("Manifest must belong to the isolated profile")
 before=fingerprint(source);save_hash=sha(supplied)
 print("ISOLATED PLAYTEST: "+str(profile),flush=True)
 print("Playing slot 1 of the copied Archive-08 save. Close the game to finish.",flush=True)
 args=[str(game),"--user-dir",str(profile),"--mods",str(profile/"test-mods"),
       "--log-dir",str(profile/"logs"),"--load-save","1","--backend","d3d12"]
 result=subprocess.run(args,cwd=game.parent)
 unchanged=before==fingerprint(source) and save_hash==sha(supplied)
 report={"game_exit_code":result.returncode,"original_saves_config_and_section_file_unchanged":unchanged,
         "original_file_hashes":before,"section_save_sha256":save_hash}
 (profile/"interactive-report.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
 print("Original saves, config and supplied GCI unchanged:",unchanged,flush=True)
 if not unchanged:raise SystemExit("Original-file hash comparison failed")
 return result.returncode

def main():
 p=argparse.ArgumentParser()
 p.add_argument("--run-manifest",type=Path)
 p.add_argument("--game",type=Path)
 p.add_argument("--bundle",type=Path)
 p.add_argument("--profile",type=Path)
 p.add_argument("--source-profile",type=Path)
 p.add_argument("--section-save",type=Path)
 p.add_argument("--launcher",type=Path)
 a=p.parse_args()
 if a.run_manifest:return run(a.run_manifest)
 for key in ("game","bundle","profile","source_profile","section_save","launcher"):
  if getattr(a,key) is None:p.error("--"+key.replace("_","-")+" is required")
 source=a.source_profile.resolve()
 for path in (a.game,a.bundle,a.profile,a.launcher):outside(path,source)
 if a.profile.exists():raise SystemExit("Choose a fresh disposable profile; refusing to overwrite one")
 data=a.section_save.read_bytes()
 if data[:6]!=b"GZ2E01" or data[8:40].split(b"\0")[0]!=b"gczelda2" or len(data)!=64+int.from_bytes(data[56:58],"big")*8192:
  raise SystemExit("Expected a valid US Twilight Princess GCI")
 before=fingerprint(source);save_hash=sha(a.section_save)
 a.profile.mkdir(parents=True)
 config=json.loads((source/"config.json").read_text(encoding="utf-8"))
 config["mod.local_ruben_midna_lament_battle.enabled"]=True
 config["game.pauseOnFocusLost"]=True
 (a.profile/"config.json").write_text(json.dumps(config,indent=2),encoding="utf-8")
 card=a.profile/"USA"/"Card A";card.mkdir(parents=True)
 shutil.copy2(a.section_save,card/"01-GZ2E-gczelda2.gci")
 mods=a.profile/"test-mods"
 shutil.copytree(source/"mods",mods)
 # All optional test drivers remain outside this interactive profile.
 for forbidden in ("section_probe.dusk","lifecycle_probe.dusk"):
  if (mods/forbidden).exists():raise SystemExit("Real mod directory unexpectedly contains a validation driver")
 shutil.copy2(a.bundle,mods/"midna_lament_battle.dusk")
 info={"game":str(a.game.resolve()),"profile":str(a.profile.resolve()),"source_profile":str(source),
       "section_save":str(a.section_save.resolve()),"section_save_sha256":save_hash,
       "mod_sha256":sha(a.bundle),"contains_only_production_mod_and_copies_of_installed_mods":True}
 manifest=a.profile/"playtest.json"
 manifest.write_text(json.dumps(info,indent=2),encoding="utf-8")
 # Each value is quoted as one cmd.exe argument; reject command metacharacters.
 values=[sys.executable,str(Path(__file__).resolve()),str(manifest.resolve())]
 if any(any(c in v for c in '%!\r\n"&|<>^') for v in values):
  raise SystemExit("Launcher paths contain unsupported command characters")
 a.launcher.parent.mkdir(parents=True,exist_ok=True)
 command=" ".join('"'+v+'"' for v in values[:2])+' --run-manifest "'+values[2]+'"'
 a.launcher.write_text("@echo off\r\n"+command+"\r\npause\r\n",encoding="utf-8",newline="")
 assert before==fingerprint(source) and save_hash==sha(a.section_save),"Original file changed"
 print("Prepared isolated launcher:",a.launcher.resolve())
 print("Copied profile:",a.profile.resolve())
 return 0

if __name__=="__main__":raise SystemExit(main())
