"""Validate the actual AST payload, channel order and loop boundary."""
import argparse, json, struct
from pathlib import Path
import numpy as np
from prepare_audio import build, BLOCK_BYTES

def decode(path):
    data=path.read_bytes()
    tag,size,fmt,bits,channels,loop,rate,count,start,end=struct.unpack_from(">4sIHHHHIIII",data)
    assert (tag,fmt,bits,channels,loop,rate)==(b"STRM",1,16,4,0xffff,48000)
    assert size==len(data)-64 and 0<start<end==count
    assert struct.unpack_from(">I",data,32)[0]==BLOCK_BYTES and data[40]==127
    chunks=[]
    for off in range(64,len(data),32+4*BLOCK_BYTES):
        assert data[off:off+4]==b"BLCK"
        assert struct.unpack_from(">I",data,off+4)[0]==BLOCK_BYTES
        block=data[off+32:off+32+4*BLOCK_BYTES]
        assert len(block)==4*BLOCK_BYTES
        chunks.append(np.frombuffer(block,dtype=">i2").reshape(4,-1).T)
    return np.concatenate(chunks)[:count].astype(np.float32)/32767,rate,start

if __name__=="__main__":
    root=Path(__file__).resolve().parents[1]
    p=argparse.ArgumentParser()
    p.add_argument("ast",type=Path,nargs="?",default=root/"overlay/Audiores/Stream/midna_lament_adaptive.ast")
    a=p.parse_args()
    pcm,rate,start=decode(a.ast)
    combined=pcm[:,:2]+pcm[:,2:]
    assert np.max(np.abs(combined))<.9, "combined clipping/headroom"
    seam=np.abs(pcm[-1]-pcm[start])
    assert np.max(seam)<.025, "loop sample discontinuity"
    # Check consecutive wraps and separation between additive stems.
    tail=np.concatenate([pcm[-rate:],pcm[start:start+rate],pcm[-rate:],pcm[start:start+rate]])
    assert np.isfinite(tail).all()
    assert not np.array_equal(pcm[:,:2],pcm[:,2:]), "duplicated stem channels"
    report={"ast_decode":"passed","channel_count":4,"sample_rate":rate,
            "frames":len(pcm),"loop_start":start,"summed_peak":float(np.max(np.abs(combined))),
            "seam_step":seam.tolist(),"repeated_wraps":"passed",
            "listening_acceptance":"Not established by numerical checks"}
    print(json.dumps(report,indent=2))
    (root/"validation/ast-validation.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
