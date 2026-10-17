"""Collect observer-only section-save evidence; keep exploratory attempts separate."""
import argparse,hashlib,json,re
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument("--profile",type=Path,required=True)
p.add_argument("--bundle",type=Path,required=True)
p.add_argument("--baseline-profile",type=Path)
p.add_argument("--out",type=Path,default=Path(__file__).resolve().parents[1]/"validation")
a=p.parse_args()
smoke=json.loads((a.profile/"smoke-report.json").read_text(encoding="utf-8"))
assert smoke["original_save_and_config_hashes_unchanged"] and smoke["game_exit_code"]==0
log="\n".join(f.read_text(encoding="utf-8",errors="replace") for f in sorted((a.profile/"logs").glob("*.log")))
evidence=[line for line in log.splitlines() if "SECTION " in line or "Adaptive " in line or "HANDOFF " in line or "RECOVERY " in line]
match=re.search(r"SECTION COMPLETE: (\d+) checks, (\d+) failures, (\d+) native entries, (\d+) native exits",log)
assert match,"Observer did not finish"
checks,failures,entries,exits=map(int,match.groups())
recovery_match=re.search(r"RECOVERY COMPLETE: (\d+) frames, (\d+) failures, (\d+) episodes",log)
assert recovery_match,"Recovery regression observer did not finish"
recovery_frames,recovery_failures,recovery_episodes=map(int,recovery_match.groups())
assert recovery_frames>0 and recovery_failures==0 and recovery_episodes>0

assert checks>0 and failures==0 and entries>=2 and exits>=1,(checks,failures,entries,exits)
states=[]
for line in evidence:
 if "SECTION state " in line:
  values=dict(re.findall(r"(\w+)=([^\s]+)",line))
  for key in ("t","near","far","off","count","state","sample"):values[key]=int(values[key])
  for key in ("base","combat","gain"):values[key]=float(values[key])
  states.append(values)
assert len({s["stream"] for s in states})==1
assert all(s["main"]=="0100005f" and s["base"]==1 for s in states)
assert any(s["combat"]==1 and s["near"]>0 for s in states)
assert any(s["combat"]==0 for s in states)
assert all(b["sample"]>f["sample"] for f,b in zip(states,states[1:]))
tested=a.profile/"test-mods"/"midna_lament_battle.dusk"
bundle_hash=hashlib.sha256(a.bundle.read_bytes()).hexdigest()
assert hashlib.sha256(tested.read_bytes()).hexdigest()==bundle_hash
handoff=None
m=re.search(r"HANDOFF COMPLETE: (\d+) native mixes, (\d+) audible native mixes, (\d+) ready starts, (\d+) starts checked",log)
if m:
 mixes,audible,ready,starts=map(int,m.groups())
 first=re.search(r"HANDOFF first native mix: volume=([\d.]+) replacement_prepared=(\d)",log)
 selected=re.search(r"HANDOFF music selected: replacement_prepared=(\d) cursor=(\d+)",log)
 assert mixes>0 and audible==0 and ready==starts and starts>0,(mixes,audible,ready,starts)
 assert first and float(first[1])==0 and first[2]=="1"
 assert selected and selected[1]=="1" and int(selected[2])==0
 handoff={"native_mixes_checked":mixes,"audible_native_mixes":audible,
          "prepared_at_music_selection":True,"opening_cursor_at_selection":0,
          "first_native_mix_volume":float(first[1]),"ready_starts":ready,"starts_checked":starts}
 if a.baseline_profile:
  baseline="\n".join(f.read_text(encoding="utf-8",errors="replace") for f in (a.baseline_profile/"logs").glob("*.log"))
  b=re.search(r"HANDOFF COMPLETE: (\d+) native mixes, (\d+) audible native mixes, (\d+) ready starts, (\d+) starts checked",baseline)
  assert b and int(b[2])>0 and int(b[3])==0
  handoff["v1_0_0_baseline_audible_native_mixes"]=int(b[2])
  handoff["v1_0_0_baseline_ready_starts"]=int(b[3])
report={"target":"Dusklight 2.0.3.0","save_slot":1,"stage":"F_SP122",
 "section_save_sha256":smoke["section_save_sha256"],"production_bundle_sha256":bundle_hash,
 "handoff":handoff,"observer_only":True,"music_or_battle_state_overrides":False,"actor_creation_or_movement":False,
 "checks":checks,"failures":failures,"recovery_frames":recovery_frames,"recovery_failures":recovery_failures,"recovery_episodes":recovery_episodes,"native_combat_entries":entries,"native_combat_exits":exits,
 "native_music_selection":"0100005f (Z2BGM_MIDNA_SOS)","states":states,
 "original_saves_config_and_supplied_gci_unchanged":True,
 "copies_of_installed_mods_present":smoke["includes_copies_of_installed_mods"],
 "game_exit_code":smoke["game_exit_code"],"profile":str(a.profile.resolve()),
 "limits":["Not a player-controlled full story playthrough or listening acceptance",
           "Enemy defeat, room/cutscene traversal, actual death/reload and revival remain interactive checks"]}
a.out.mkdir(parents=True,exist_ok=True)
(a.out/"section-validation.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
(a.out/"section-evidence.log").write_text("\n".join(evidence)+"\n",encoding="utf-8")
print(json.dumps({k:v for k,v in report.items() if k!="states"},indent=2))
