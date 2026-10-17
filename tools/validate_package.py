"""Validate native imports and the packaged AST against Dusklight 2.0.3."""
import argparse,json,zipfile,re
from pathlib import Path
import pefile
from release_symbols import symbols
from native_manifest import validate_manifest
p=argparse.ArgumentParser()
p.add_argument("bundle",type=Path); p.add_argument("game",type=Path)
p.add_argument("--report",type=Path,default=Path(__file__).resolve().parents[1]/"validation/package-validation.json")
a=p.parse_args()
with zipfile.ZipFile(a.bundle) as z:
 assert z.testzip() is None
 manifest=json.loads(z.read("mod.json"))
 assert manifest["id"]=="local.ruben.midna_lament_battle"
 metadata=validate_manifest(manifest,z.read("lib/windows-amd64/mod.dll"))
 dll=pefile.PE(data=z.read("lib/windows-amd64/mod.dll"))
 assert dll.FILE_HEADER.Machine==0x8664
 lifecycle={e.name.decode() for e in dll.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
 assert {"mod_initialize","mod_update","mod_shutdown"}<=lifecycle
 host=pefile.PE(str(a.game))
 exports={e.name for e in host.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
 missing=[]; imported=[]
 for entry in dll.DIRECTORY_ENTRY_IMPORT:
  if entry.dll.lower()==b"dusklight.exe":
   for symbol in entry.imports:
    imported.append(symbol.name.decode())
    if symbol.name not in exports: missing.append(symbol.name.decode())
 assert not missing,missing
 source=(Path(__file__).resolve().parents[1]/"src/mod.cpp").read_text(encoding="utf-8")
 required=set(re.findall(r'resolve\("([^"]+)"',source))
 required.update(re.findall(r'svc_hook->resolve\(mod_ctx, "([^"]+)"',source))
 required.update(re.findall(r'DEFINE_HOOK_SYMBOL\("([^"]+)"',source))
 database=symbols(a.game)
 resolved={name:{"rva":database[name][0],"flags":database[name][1]} for name in sorted(required) if name in database}
 assert len(resolved)==len(required), sorted(required-set(resolved))
 assert all(not value["flags"]&16 and value["rva"] for value in resolved.values()),"Ambiguous or null symbols"
 assert all(not resolved[name]["flags"]&32 for name in re.findall(r'DEFINE_HOOK_SYMBOL\("([^"]+)"',source)),"Partially inline hook target"
 assert all(marker not in z.read("lib/windows-amd64/mod.dll") for marker in (b"PROBE COMPLETE",b"SECTION COMPLETE",b"LIFECYCLE COMPLETE")),"Test injection in shipping DLL"
 ast=z.read("overlay/Audiores/Stream/midna_lament_adaptive.ast")
 assert ast.startswith(b"STRM") and len(ast)>1000000
 report={"json_native_metadata_matches_dll":"passed","native_abi":metadata["abi"],"service_imports":metadata["imports"],"service_exports":metadata["exports"],"zip_crc":"passed","windows_x64_dll":"passed","lifecycle_exports":"passed",
         "game_imports_checked":len(imported),"missing_game_exports":missing,
         "release_symbols_checked":len(resolved),"release_symbols":resolved,"test_injection":"absent",
         "packed_ast_bytes":len(ast),"bundle_bytes":a.bundle.stat().st_size,
         "target":"Dusklight 2.0.3 / 40457c6adb381928e4b5fef6ed459ed291edd5e2"}
 a.report.parent.mkdir(parents=True,exist_ok=True)
 a.report.write_text(json.dumps(report,indent=2),encoding="utf-8")
 print(json.dumps(report,indent=2))
