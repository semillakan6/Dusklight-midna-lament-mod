"""Inspect the official release's embedded symbol database."""
import argparse,struct,re,json
from pathlib import Path
import pefile,zstandard
def symbols(exe):
 pe=pefile.PE(str(exe))
 data=next(s.get_data() for s in pe.sections if s.Name.rstrip(b"\0")==b".symdb")
 magic,version,compression,unpacked,packed,buildsize,buildid,count=struct.unpack("<8sIIQQI32sI",data[:72])
 assert magic.startswith(b"SYMGEN") and version==2 and compression==1,(magic,version,compression)
 payload=zstandard.ZstdDecompressor().decompress(data[72:72+packed],max_output_size=unpacked)
 strings=payload[count*24:]
 result={}
 for _,rva,off,flags in struct.iter_unpack("<QQII",payload[:count*24]):
  name=strings[off:strings.index(b"\0",off)].decode("utf-8",errors="replace")
  result[name]=(rva,flags)
 return result
if __name__=="__main__":
 p=argparse.ArgumentParser();p.add_argument("exe",type=Path);p.add_argument("pattern")
 a=p.parse_args()
 for name,(rva,flags) in symbols(a.exe).items():
  if re.search(a.pattern,name): print(hex(rva),hex(flags),name)
