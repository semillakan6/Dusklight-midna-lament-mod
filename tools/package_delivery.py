"""Package an already verified eight-platform artifact; never install into a real profile."""
import argparse,hashlib,json,shutil,subprocess,sys,zipfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--bundle',type=Path,required=True);p.add_argument('--validation-dir',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[1]
subprocess.run([sys.executable,str(root/'tools/validate_combined.py'),str(a.bundle),'--reports',str(a.validation_dir)],check=True)
a.out.mkdir(parents=True,exist_ok=True)
shutil.copy2(a.bundle,a.out/'midna_lament_battle.dusk')
for name in ('README.md','CHANGELOG.md','LICENSE','THIRD_PARTY_NOTICES.md'):shutil.copy2(root/name,a.out/name)
shutil.copytree(a.validation_dir,a.out/'validation',dirs_exist_ok=True)
with zipfile.ZipFile(a.out/'midna-lament-source.zip','w',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
 for folder in ('src','tests','tools','cmake','overlay','.github'):
  for path in sorted((root/folder).rglob('*')):
   if path.is_file() and '__pycache__' not in path.parts and path.suffix!='.pyc':archive.write(path,'midna-lament/'+path.relative_to(root).as_posix())
 for name in ('CMakeLists.txt','mod.json.in','.gitignore','README.md','CHANGELOG.md','LICENSE','THIRD_PARTY_NOTICES.md'):archive.write(root/name,'midna-lament/'+name)
checks={p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in a.out.iterdir() if p.is_file() and p.name!='SHA256.json'}
(a.out/'SHA256.json').write_text(json.dumps(checks,indent=2)+'\n');print(json.dumps(checks,indent=2))
