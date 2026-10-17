"""Check a native mod against the matching official 2.0.3 release, without running it."""
import argparse,hashlib,json,re,struct,subprocess,urllib.request,zipfile
from pathlib import Path


ASSETS={
 'windows-amd64':'win32-x86_64.zip','windows-arm64':'win32-arm64.zip',
 'linux-x86_64':'linux-x86_64.AppImage','linux-aarch64':'linux-arm64.AppImage',
 'macos-arm64':'macos-arm64.zip','macos-x86_64':'macos-x86_64.zip',
 'ios-arm64':'ios-arm64.ipa','android-aarch64':'android-arm64.apk'}
AST_SHA='40475629963d17f40db783512d6dc92a0e77181b0d593c5cf14f79cf81d0adfb'
ROOT=Path(__file__).resolve().parents[1]

def require(value,message):
 if not value:raise RuntimeError(message)

def parse(path):
 import lief
 binary=lief.parse(str(path));require(binary is not None,'Cannot parse '+str(path));return binary

def symbol_names(binary,kind):
 result=set()
 for attr in (kind+"_symbols",kind+"_functions"):
  if hasattr(binary,attr):result.update(s.name for s in getattr(binary,attr))
 return result

def architecture(binary,platform):
 text=str(getattr(binary.header,'machine',getattr(binary.header,'machine_type',getattr(binary.header,'cpu_type','')))).upper()
 arm=platform.endswith(('arm64','aarch64'))
 require(('ARM64' in text or 'AARCH64' in text) if arm else ('AMD64' in text or 'X86_64' in text),f'Wrong architecture: {platform}: {text}')
 return text

def game_symbols(binary):
 import zstandard
 for section in binary.sections:
  data=bytes(section.content)
  if data.startswith(b'SYMGEN\0\0'):
   magic,version,compression,unpacked,packed,_,_,count=struct.unpack('<8sIIQQI32sI',data[:72])
   require(version==2 and compression in (0,1),'Unexpected symbol database format')
   payload=data[72:72+packed]
   if compression:payload=zstandard.ZstdDecompressor().decompress(payload,max_output_size=unpacked)
   names=payload[count*24:];result={}
   for _,rva,offset,flags in struct.iter_unpack('<QQII',payload[:count*24]):
    name=names[offset:names.index(b'\0',offset)].decode('utf-8')
    result[name]=(rva,flags)
   return result
 raise RuntimeError('Official game has no readable embedded symbol database')

def acquire_game(platform,folder):
 folder.mkdir(parents=True,exist_ok=True)
 filename='Dusklight-v2.0.3-'+ASSETS[platform];archive=folder/filename
 if not archive.exists():
  urllib.request.urlretrieve('https://github.com/TwilitRealm/dusklight/releases/download/v2.0.3/'+filename,archive)
 extracted=folder/'extracted'
 if not extracted.exists():
  if zipfile.is_zipfile(archive):
   with zipfile.ZipFile(archive) as z:z.extractall(extracted)
  else:
   data=archive.read_bytes();offset=data.find(b'hsqs')
   require(offset>=0,'AppImage has no SquashFS payload')
   subprocess.run(['unsquashfs','-no-progress','-d',str(extracted),'-o',str(offset),str(archive)],check=True,stdout=subprocess.DEVNULL)
 candidates=[p for p in extracted.rglob('*') if p.is_file() and p.name.lower() in ('dusklight','dusklight.exe','libmain.so')]
 require(bool(candidates),'No game binary extracted')
 for candidate in candidates:
  try:
   binary=parse(candidate);architecture(binary,platform);game_symbols(binary)
   return candidate
  except RuntimeError:pass
 raise RuntimeError('No matching game binary with symbol database')

def requirements(platform):
 header=(ROOT/'src/native_symbols.hpp').read_text()
 records=re.findall(r'\{"([^"]+)", "([^"]+)", "([^"]+)"\}',header)
 selected={label:(msvc if platform.startswith('windows-') else itanium) for label,msvc,itanium in records}
 source=(ROOT/'src/mod.cpp').read_text()
 hooks=re.findall(r'DEFINE_HOOK_SYMBOL\("([^"]+)"',source)
 return selected,hooks

def validate(bundle,platform,game,report):
 stage=report.parent/('library-'+platform);stage.mkdir(parents=True,exist_ok=True)
 with zipfile.ZipFile(bundle) as z:
  require(z.testzip() is None,'ZIP CRC failure')
  name='lib/'+platform+('/mod.dll' if platform.startswith('windows-') else '/mod.so')
  library=stage/Path(name).name;data=z.read(name);library.write_bytes(data)
  require(all(marker not in data for marker in (b'PROBE COMPLETE',b'SECTION COMPLETE',b'LIFECYCLE COMPLETE')),'Shipping library contains a test driver')
  require(hashlib.sha256(z.read('overlay/Audiores/Stream/midna_lament_adaptive.ast')).hexdigest()==AST_SHA,'Audio changed')
 binary=parse(library);arch=architecture(binary,platform)
 exported={name.lstrip('_') for name in symbol_names(binary,'exported')}
 require({'mod_initialize','mod_update','mod_shutdown','mod_meta'}<=exported,'Missing lifecycle/metadata exports')
 host=parse(game);architecture(host,platform);database=game_symbols(host)
 selected,hooks=requirements(platform);resolved={}
 for label,name in selected.items():
  require(name in database,'Missing required '+label+': '+name)
  rva,flags=database[name];require(rva and not flags&16,'Unavailable symbol: '+name)
  resolved[label]={'name':name,'rva':rva,'flags':flags}
 for name in hooks:
  require(name in database,'Missing hook: '+name)
  rva,flags=database[name];require(rva and not flags&(16|32),'Unavailable or partially inlined hook: '+name)
  resolved[name]={'rva':rva,'flags':flags}
 # Undefined game symbols may bind only to host exports. Runtime/system imports
 # are handled by platform linkers; game classes carry Itanium/MSVC names.
 imports=symbol_names(binary,'imported')
 host_exports=symbol_names(host,'exported')
 missing=[]
 for name in imports:
  raw=name[1:] if platform.startswith(('macos-','ios-')) and name.startswith('__Z') else name
  if raw.startswith('?') or re.match(r'_Z.*(?:JAI|JAS|JKR|Z2)',raw):
   if name not in host_exports and raw not in host_exports:missing.append(name)
 require(not missing,'Missing game dynamic exports: '+repr(missing))
 result={'platform':platform,'architecture':arch,'game_version':'2.0.3','bundle_sha256':hashlib.sha256(bundle.read_bytes()).hexdigest(),'audio_sha256':AST_SHA,'required_symbols':resolved,'lifecycle_exports':'passed','game_dynamic_imports':'passed','test_injection':'absent','runtime_test':'not performed by this static check'}
 report.parent.mkdir(parents=True,exist_ok=True);report.write_text(json.dumps(result,indent=2)+'\n')
 print('PASS:',platform,'-',len(resolved),'required symbols')

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--platform',choices=ASSETS,required=True);p.add_argument('--bundle',type=Path,required=True);p.add_argument('--game',type=Path);p.add_argument('--cache',type=Path,default=Path('build/release'));p.add_argument('--report',type=Path,required=True)
 a=p.parse_args();game=a.game or acquire_game(a.platform,a.cache)
 validate(a.bundle,a.platform,game,a.report)
