"""Gate a replacement package on the reproduced first-install crash and fixed runs."""
import argparse, hashlib, json, re
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--old-profile',type=Path,required=True)
p.add_argument('--profile',type=Path,action='append',required=True)
p.add_argument('--bundle',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
a=p.parse_args()
def logs(profile):
 return '\n'.join(p.read_text(encoding='utf-8',errors='replace') for p in sorted((profile/'logs').glob('*.log')))
old_report=json.loads((a.old_profile/'smoke-report.json').read_text())
old=logs(a.old_profile)
assert old_report['game_exit_code']!=0 and old_report['original_save_and_config_hashes_unchanged']
assert 'first activation starts with the native two-channel reader' in old
assert 'HOT INSTALL REQUEST' in old and 'Aborting due to allocation failure!' in old
sha=hashlib.sha256(a.bundle.read_bytes()).hexdigest()
runs=[];evidence=[]
for profile in a.profile:
 report=json.loads((profile/'smoke-report.json').read_text())
 log=logs(profile)
 assert report['game_exit_code']==0 and report['original_save_and_config_hashes_unchanged']
 assert 'HOT INSTALL FAIL' not in log and '[FATAL' not in log
 for marker in ('HOT INSTALL PASS: first activation starts with the native two-channel reader',
                'HOT INSTALL PASS: staged package installed and activated',
                'HOT INSTALL PASS: live installation expands the existing reader to four channels'):
  assert marker in log,(profile.name,marker)
 installed=profile/'test-mods/local.ruben.midna_lament_battle.dusk'
 assert hashlib.sha256(installed.read_bytes()).hexdigest()==sha
 lifecycle='HOT INSTALL LIFECYCLE COMPLETE: 0 failures' in log
 if lifecycle:
  for text in ('disable preserves the published reader buffer','enable reuses the published reader buffer','reload preserves the published reader buffer'):
   assert 'HOT INSTALL PASS: '+text in log
 native_checks=re.findall(r'LIFECYCLE PASS: (.+)',log)
 if native_checks:assert 'LIFECYCLE COMPLETE: 0 failures' in log and 'LIFECYCLE FAIL:' not in log
 playback='HOT INSTALL PASS: replacement prepares four synchronized channels after live installation' in log
 runs.append({'scenario':profile.name,'checks':log.count('HOT INSTALL PASS:'),'failures':0,
              'reader_started_at_two_channels':True,'installed_via_browser_staging_entrypoint':True,
              'exact_delivery_bundle':True,'buffer_lifetime_disable_enable_reload':lifecycle,
              'replacement_playback_observed':playback,'native_lifecycle_checks':native_checks,'game_exit_code':0,
              'original_saves_and_configuration_unchanged':True})
 evidence+=['Scenario: '+profile.name]+[line for line in log.splitlines() if 'HOT INSTALL ' in line or 'Ready: Dusklight' in line or 'LIFECYCLE ' in line or 'NATIVE RESTORE:' in line]
assert any(r['buffer_lifetime_disable_enable_reload'] for r in runs),'Extended lifecycle regression run required'
assert any(r['replacement_playback_observed'] for r in runs),'Live-installed replacement playback required'
assert any(len(r['native_lifecycle_checks'])>=6 for r in runs),'Native restoration and loader lifecycle checks required'
result={'version':'1.0.4','production_bundle_sha256':sha,'game_version':'2.0.3.0',
        'old_build_reproduced':{'version':'1.0.3','exit_code':old_report['game_exit_code'],'error':'JKRExpHeap allocation failure during first live activation'},
        'fixed_build_runs':runs,'limits':['Windows runtime tested; other platforms compiled and statically checked.',
        'Uses the real browser staging/install function with a locally supplied package; network download and website review are not exercised.']}
a.out.mkdir(parents=True,exist_ok=True)
(a.out/'live-install-validation.json').write_text(json.dumps(result,indent=2)+'\n')
(a.out/'live-install-evidence.log').write_text('\n'.join(evidence)+'\n')
print(json.dumps(result,indent=2))
