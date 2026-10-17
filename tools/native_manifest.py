"""Read service metadata from a Windows DLL without loading or executing it."""
import re,struct
import pefile

def native_metadata(dll_bytes):
 pe=pefile.PE(data=dll_bytes)
 assert pe.FILE_HEADER.Machine==0x8664,"Expected the Windows x64 build"
 sections=[s for s in pe.sections if s.Name.rstrip(b"\0")==b"modmeta"]
 assert len(sections)==1,"Expected one native metadata section"
 section=sections[0]
 data=section.get_data()[:section.Misc_VirtualSize]
 offset=0;headers=[];imports=[];exports=[]
 while offset<len(data):
  assert offset+8<=len(data),"Truncated metadata record"
  if data[offset:offset+8]==bytes(8):
   offset+=8;continue
  size,kind,flags=struct.unpack_from("<HBB",data,offset)
  assert size>=8 and size%8==0 and offset+size<=len(data),"Invalid metadata record bounds"
  record=data[offset:offset+size]
  if kind==1:
   assert size==8,"Invalid ABI header"
   headers.append(struct.unpack_from("<I",record,4)[0])
  elif kind in (2,3):
   assert size==80 and flags in (0,1),"Invalid native service record"
   major,minor=struct.unpack_from("<HH",record,4)
   name=record[16:80]
   assert b"\0" in name,"Unterminated service ID"
   service_id=name.split(b"\0",1)[0].decode("ascii")
   assert re.fullmatch(r"[a-z0-9._]+",service_id),"Invalid service ID"
   if kind==2:
    imports.append({"service_id":service_id,"major":major,"min_minor":minor,"optional":bool(flags&1)})
   else:
    exports.append({"service_id":service_id,"major":major,"minor":minor,"deferred":bool(flags&1)})
  offset+=size
 assert len(headers)==1,"Expected exactly one native ABI declaration"
 key=lambda row:(row["service_id"],row["major"])
 for rows in (imports,exports):
  assert len({key(row) for row in rows})==len(rows),"Duplicate native service declarations"
 return {"abi":headers[0],"imports":sorted(imports,key=key),"exports":sorted(exports,key=key)}

def validate_manifest(manifest,dll_bytes):
 expected=native_metadata(dll_bytes)
 assert type(manifest.get("abi")) is int,"Native mod.json must declare numeric abi"
 for name in ("imports","exports"):
  assert isinstance(manifest.get(name),list),"Native mod.json must declare "+name+" as an array"
  assert all(isinstance(row,dict) for row in manifest[name]),"Invalid service declarations"
  for row in manifest[name]:
   assert type(row.get("major")) is int and 0<=row["major"]<=65535
   minor="min_minor" if name=="imports" else "minor"
   flag="optional" if name=="imports" else "deferred"
   assert type(row.get(minor)) is int and 0<=row[minor]<=65535
   assert type(row.get(flag)) is bool
 key=lambda row:(row["service_id"],row["major"])
 actual={"abi":manifest["abi"],"imports":sorted(manifest["imports"],key=key),
         "exports":sorted(manifest["exports"],key=key)}
 assert actual==expected,("mod.json ABI/service declarations differ from the compiled DLL",actual,expected)
 return expected
