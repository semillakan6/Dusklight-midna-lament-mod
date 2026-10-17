import numpy as np, soundfile as sf, json, sys
from scipy.signal import stft, resample_poly, correlate, find_peaks
xs=[sf.read(p,dtype="float32",always_2d=True)[0] for p in sys.argv[1:]]
feats=[]
for x in xs:
 y=resample_poly(x.mean(axis=1),1,4)
 f,t,z=stft(y,12000,nperseg=4096,noverlap=2896)
 m=np.abs(z)
 notes=np.rint(69+12*np.log2(np.maximum(f,1)/440)).astype(int)
 chroma=np.array([np.sum(m[(f>80)&(f<3000)&(notes%12==i)]**2,axis=0) for i in range(12)]).T
 chroma=np.sqrt(chroma)
 chroma/=np.linalg.norm(chroma,axis=1,keepdims=True)+1e-10
 feats.append(chroma)
f=np.concatenate(feats,axis=1)
rank=[]
for s in range(50,1000):
 for e in range(1850,2110):
  score=np.mean((f[s-30:s+30]-f[e-30:e+30])**2)
  rank.append((float(score),s/10,e/10))
rank.sort()
out=[]
for c in rank:
 if all(abs(c[1]-d[1])>2 or abs(c[2]-d[2])>2 for d in out): out.append(c)
 if len(out)==12: break
print(json.dumps(out))
for x in xs:
 rms=np.sqrt(np.mean(x[:len(x)//480*480].reshape(-1,480,2)**2,axis=(1,2)))
 onset=np.maximum(np.diff(np.log(rms+1e-6)),0)
 c=correlate(onset,onset,method="fft",mode="full")[len(onset)-1:]
 peaks,_=find_peaks(c[25:150])
 best=sorted([(float(c[i+25]),60/((i+25)/100)) for i in peaks],reverse=True)[:8]
 print("tempo_candidates",best)
