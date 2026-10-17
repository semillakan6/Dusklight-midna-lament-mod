"""Read-only stem analysis and repeated-phrase candidates."""
import argparse, json
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly, stft, correlate
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument("base"); p.add_argument("battle")
a=p.parse_args()
tracks=[]
for path in (a.base,a.battle):
    x,sr=sf.read(path,dtype="float32",always_2d=True)
    tracks.append(x)
    print(json.dumps({"file":Path(path).name,"rate":sr,"frames":len(x),"channels":x.shape[1],
                      "peak":float(np.max(np.abs(x))),"end_rms":float(np.sqrt(np.mean(x[-sr:]**2)))}))
assert tracks[0].shape==tracks[1].shape and sr==48000
print("summed_peak",float(np.max(np.abs(tracks[0]+tracks[1]))))
# 100 ms spectral frames describe musical content rather than sample phase.
features=[]
for x in tracks:
    y=resample_poly(x.mean(axis=1),1,8)
    f,t,z=stft(y,6000,nperseg=2048,noverlap=1448)
    magnitude=np.abs(z)
    bands=[]
    for lo,hi in zip(np.geomspace(50,3000,33)[:-1],np.geomspace(50,3000,33)[1:]):
        bands.append(np.log1p(200*magnitude[(f>=lo)&(f<hi)].mean(axis=0)))
    feat=np.stack(bands,axis=1)
    feat/=np.linalg.norm(feat,axis=1,keepdims=True)+1e-8
    features.append(feat)
features=np.concatenate(features,axis=1)
candidates=[]
# Compare four seconds before and after each candidate join.
for start in range(100,800):
    head=features[start-40:start+40]
    ends=np.arange(max(1500,start+900),2090)
    scores=np.array([np.mean((head-features[end-40:end+40])**2) for end in ends])
    best=int(np.argmin(scores))
    candidates.append((float(scores[best]),start/10,ends[best]/10))
candidates.sort()
selected=[]
for c in candidates:
    if all(abs(c[1]-d[1])>2 or abs(c[2]-d[2])>2 for d in selected):
        selected.append(c)
    if len(selected)==12: break
print("phrase_candidates",json.dumps(selected))
# Compare percussive/base onset patterns for an offset sanity check.
env=[]
for x in tracks:
    rms=np.sqrt(np.mean(x[:len(x)//480*480].reshape(-1,480,2)**2,axis=(1,2)))
    env.append(np.maximum(0,np.diff(np.log(rms+1e-5))))
corr=correlate(env[1]-env[1].mean(),env[0]-env[0].mean(),mode="full",method="fft")
mid=len(env[0])-1
local=corr[mid-50:mid+51]
print("envelope_offset_seconds",int(np.argmax(local)-50)/100)
