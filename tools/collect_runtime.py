"""Collect runtime evidence from successful disposable-profile tests."""
import argparse,json,re
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument("--states",type=Path,required=True)
p.add_argument("--lifecycle",type=Path,required=True)
p.add_argument("--final",type=Path)
a=p.parse_args()
root=Path(__file__).resolve().parents[1]
def read(profile):
 reports=json.loads((profile/"smoke-report.json").read_text())
 assert reports["original_save_and_config_hashes_unchanged"]
 logs="\n".join(log.read_text(encoding="utf-8",errors="replace") for log in (profile/"logs").glob("*.log"))
 assert "APPLICATION CRASHED" not in logs
 return reports,logs
states,state_log=read(a.states);life,life_log=read(a.lifecycle)
assert "PROBE COMPLETE: 0 failures" in state_log
assert "LIFECYCLE COMPLETE: 0 failures" in life_log
assert "PROBE FAIL:" not in state_log and "LIFECYCLE FAIL:" not in life_log
loops=re.findall(r"NATIVE LOOP (\d+): sample (\d+) -> (\d+); same stream=(\w+)",life_log)
assert len(loops)>=3,loops
assert len({row[3] for row in loops})==1
report={
 "audio_state_checks":re.findall(r"PROBE PASS: (.+)",state_log),
 "lifecycle_checks":re.findall(r"LIFECYCLE PASS: (.+)",life_log),
 "native_loop_wraps":[{"loop":int(row[0]),"last_sample":int(row[1]),"next_sample":int(row[2]),"stream":row[3]} for row in loops],
 "original_saves_and_config_unchanged":True,
 "state_probe_profile":str(a.states),"lifecycle_profile":str(a.lifecycle),
 "installed_mod_copies":states["includes_copies_of_installed_mods"],
 "test_method":"Injected audio-controller states plus real loader disable/enable/reload and full-length native AST loops",
 "limitations":["Listening acceptance pending","Actual story-section enemies, room/cutscene traversal, death/reload and revival playthrough pending"],
 "scene_retention_note":"STREAM_ALT registration added after the long loop run; AST payload and playback implementation unchanged."
}
if a.final:
 final,log=read(a.final)
 assert "Ready: Dusklight 2.0.3" in log
 report["final_package_loader"]="passed"
 report["final_package_profile"]=str(a.final)
(root/"validation/runtime-validation.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
(root/"validation/runtime-evidence.log").write_text("\n".join(line for log in (state_log,life_log) for line in log.splitlines()
 if any(text in line for text in ("PROBE PASS:","PROBE COMPLETE","LIFECYCLE PASS:","LIFECYCLE COMPLETE","NATIVE LOOP ")))+"\n",encoding="utf-8")
print(json.dumps({k:v for k,v in report.items() if k not in ("audio_state_checks","lifecycle_checks")},indent=2))
