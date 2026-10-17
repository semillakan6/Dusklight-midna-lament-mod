"""Final distribution gate after the official symgen merge."""
import argparse,hashlib,json,zipfile
from pathlib import Path
from validate_platform import ASSETS,AST_SHA,require
p=argparse.ArgumentParser();p.add_argument('bundle',type=Path);p.add_argument('--reports',type=Path,required=True);a=p.parse_args()
with zipfile.ZipFile(a.bundle) as z:
 require(z.testzip() is None,'ZIP CRC failure')
 names=[n for n in z.namelist() if not n.endswith('/')]
 expected={'mod.json','overlay/Audiores/Stream/midna_lament_adaptive.ast'}|{'lib/'+platform+('/mod.dll' if platform.startswith('windows-') else '/mod.so') for platform in ASSETS}
 require(set(names)==expected and len(names)==len(expected),'Unexpected/missing/duplicate package entries')
 m=json.loads(z.read('mod.json'))
 require(m['id']=='local.ruben.midna_lament_battle' and m['version']=='1.0.3' and m['author']=='Nebur','Wrong identity')
 expected_imports={'audio_res':(1,0),'game':(2,0),'hook':(1,1),'host':(2,2),'log':(1,0)}
 require(type(m.get('abi')) is int and m['abi']==1 and m.get('exports')==[],'Wrong native ABI/exports')
 actual={i['service_id'].removeprefix('dev.twilitrealm.dusklight.'):(i['major'],i['min_minor']) for i in m.get('imports',[])}
 require(actual==expected_imports and len(m['imports'])==5 and all(i['optional'] is False for i in m['imports']),'Wrong native service imports')
 require(hashlib.sha256(z.read('overlay/Audiores/Stream/midna_lament_adaptive.ast')).hexdigest()==AST_SHA,'Audio changed')
for platform in ASSETS:
 report=json.loads((a.reports/(platform+'.json')).read_text())
 require(report['platform']==platform and len(report['required_symbols'])==23,'Missing platform compatibility evidence')
result={'file':a.bundle.name,'version':'1.0.3','sha256':hashlib.sha256(a.bundle.read_bytes()).hexdigest(),'bytes':a.bundle.stat().st_size,'platforms':sorted(ASSETS),'manifest_and_payload':'passed','native_metadata':'verified by symgen against all eight libraries','runtime':'See separate isolated runtime report; this check does not run the game.'}
(a.bundle.parent/'SHA256.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
