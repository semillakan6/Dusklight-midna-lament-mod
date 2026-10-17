"""Build a synchronized four-channel PCM16 AST and audio previews.
Original exports are read only. Requires numpy and soundfile.
"""
import argparse, hashlib, json, struct
from pathlib import Path
import numpy as np
import soundfile as sf

BLOCK_BYTES = 0x2760  # native TP stream-reader block size, per channel

def write_ast(path, samples, rate, loop_start):
    count, channels = samples.shape
    assert channels == 4 and rate == 48000
    frames_per_block = BLOCK_BYTES // 2
    pcm = np.rint(np.clip(samples, -1, 1) * 32767).astype(">i2")
    header = bytearray(64)
    struct.pack_into(">4sIHHHHIIII", header, 0, b"STRM", 0, 1, 16, channels,
                     0xffff, rate, count, loop_start, count)
    struct.pack_into(">I", header, 32, BLOCK_BYTES)
    header[40] = 127
    blocks = []
    for offset in range(0, count, frames_per_block):
        n = min(frames_per_block, count - offset)
        # Keep the final block padded to a full native block. Playback ends at
        # loop_end, not at the padding; this also keeps random-access offsets valid.
        data = b"".join(pcm[offset:offset+n, c].tobytes() +
                        bytes(BLOCK_BYTES - n*2) for c in range(channels))
        blocks.append(struct.pack(">4sI", b"BLCK", BLOCK_BYTES) + bytes(24) + data)
    struct.pack_into(">I", header, 4, sum(map(len, blocks)))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + b"".join(blocks))

def build(base_path, battle_path, output, validation, start, end, overlap):
    base, rate = sf.read(base_path, dtype="float32", always_2d=True)
    battle, battle_rate = sf.read(battle_path, dtype="float32", always_2d=True)
    if rate != 48000 or battle_rate != rate or base.shape != battle.shape or base.shape[1] != 2:
        raise ValueError("Expected aligned, equally long 48 kHz stereo stems")
    if not np.isfinite(base).all() or not np.isfinite(battle).all():
        raise ValueError("Non-finite input samples")
    a, b, n = [round(v * rate) for v in (start, end, overlap)]
    if not (0 <= a < a+n < b-n < b <= len(base)):
        raise ValueError("Invalid shared loop bounds")
    stems = np.concatenate([base, battle], axis=1)
    original_peak = float(np.max(np.abs(base + battle)))
    edited = stems[:b].copy()
    # A linear complementary blend avoids boosting correlated material.
    # Smoothstep has zero slope at both ends. The head advances during the
    # crossfade, so the AST wraps to a+n rather than replaying the overlap.
    t = np.linspace(0, 1, n, endpoint=True, dtype=np.float32)[:,None]
    w = t*t*(3-2*t)
    edited[b-n:b] = edited[b-n:b]*(1-w) + stems[a:a+n]*w
    peak = max(float(np.max(np.abs(edited[:,:2]))),
               float(np.max(np.abs(edited[:,:2]+edited[:,2:]))))
    gain = min(1.0, 10**(-1/20) / peak)
    edited *= gain
    loop_start = a+n
    write_ast(output, edited, rate, loop_start)
    validation.mkdir(parents=True, exist_ok=True)
    before = edited[-rate*8:]
    after = edited[loop_start:loop_start+rate*8]
    join = np.concatenate([before, after])
    sf.write(validation/"loop-base.wav",join[:,:2],rate,subtype="PCM_16")
    sf.write(validation/"loop-combined.wav",join[:,:2]+join[:,2:],rate,subtype="PCM_16")
    # Demonstrate entry at 6 s, retreat at 10 s and reversal at 10.7 s.
    preview = edited[rate*30:rate*48]
    env = np.zeros(len(preview),dtype=np.float32)
    value=0.0; origin=0.0; target=0.0; elapsed=0; duration=.5
    events={6*rate:True,10*rate:False,round(10.7*rate):True,13*rate:False}
    for i in range(len(env)):
        if i in events:
            origin=value; target=float(events[i]); elapsed=0
            duration=.5 if target else 1.5
        elapsed=min(duration,elapsed+1/rate)
        value=origin+(target-origin)*(elapsed/duration)
        env[i]=value
    sf.write(validation/"combat-transition.wav",preview[:,:2]+preview[:,2:]*env[:,None],
             rate,subtype="PCM_16")
    report = {
        "inputs": [{"name": p.name,"sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
                   for p in (base_path,battle_path)],
        "input_frames":len(base),"sample_rate":rate,"input_channels_per_stem":2,
        "ast_channels":4,"ast_frames":len(edited),"first_pass_seconds":len(edited)/rate,
        "source_loop_head_seconds":start,"source_loop_end_seconds":end,
        "overlap_seconds":overlap,"loop_start_sample":loop_start,
        "loop_start_seconds":loop_start/rate,
        "loop_period_seconds":(len(edited)-loop_start)/rate,
        "original_summed_peak":original_peak,"shared_gain":gain,
        "shared_gain_db":float(20*np.log10(gain)),
        "encoded_combined_peak":float(np.max(np.abs(edited[:,:2]+edited[:,2:]))),
        "boundary_max_sample_step":float(np.max(np.abs(edited[-1]-edited[loop_start]))),
        "loop_selection":"Shared harmonic phrase match, with a complementary 2.7 s blend",
        "listening_acceptance":"Pending human audition of the loop and in-game playback",
        "ast_sha256":hashlib.sha256(output.read_bytes()).hexdigest()
    }
    (validation/"audio-report.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(json.dumps(report,indent=2))

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--base",type=Path,required=True)
    p.add_argument("--battle",type=Path,required=True)
    root=Path(__file__).resolve().parents[1]
    p.add_argument("--output",type=Path,default=root/"overlay/Audiores/Stream/midna_lament_adaptive.ast")
    p.add_argument("--validation",type=Path,default=root/"validation")
    p.add_argument("--loop-head",type=float,default=78.8)
    p.add_argument("--loop-end",type=float,default=200.6)
    p.add_argument("--overlap",type=float,default=2.7)
    a=p.parse_args()
    build(a.base,a.battle,a.output,a.validation,a.loop_head,a.loop_end,a.overlap)
