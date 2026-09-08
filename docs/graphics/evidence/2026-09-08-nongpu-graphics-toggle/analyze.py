import json,pathlib,random,statistics,hashlib
root=pathlib.Path('artifacts/graphics-nongpu-abba-20260908')
m=json.loads((root/'manifest.json').read_text());assert m['status']=='captured'
for entry in m['order']:
 assert hashlib.sha256((root/entry['file']).read_bytes()).hexdigest()==entry['sha256']
samples={name:[json.loads(p.read_text()) for p in sorted((root/name).glob('*.json'))] for name in ['control','candidate']}
assert all(len(s)==20 for s in samples.values())
assert all(s['options']==samples['control'][0]['options'] for group in samples.values() for s in group)
def val(s,path):
 for key in path.split('.'):s=s[key]
 return s
paths=['startup.prewarmMilliseconds','startup.warmContextCreateMilliseconds.mean','startup.firstSceneMilliseconds.mean','idle.processCpuMilliseconds','timerAndAnimationFrame.elapsedMilliseconds','timerAndAnimationFrame.processCpuMilliseconds','consoleHeavy.elapsedMilliseconds','consoleHeavy.processCpuMilliseconds','representativeWorkload.elapsedMilliseconds','representativeWorkload.processCpuMilliseconds','memory.populatedViewsWorkingSetBytes','memory.workloadWorkingSetBytes','managedAllocations.ordinaryViewConstructionBytes','managedAllocations.blankLifecycleBytes.median']
metrics={}
for path in paths:
 a=[val(s,path) for s in samples['control']];b=[val(s,path) for s in samples['candidate']]
 av,bv=statistics.median(a),statistics.median(b)
 rng=random.Random(20260908);ratios=[]
 for _ in range(10000):
  blocks=[rng.randrange(10) for _ in range(10)]
  indices=[2*k+j for k in blocks for j in [0,1]]
  x=statistics.median(a[i] for i in indices);y=statistics.median(b[i] for i in indices)
  if x>0:ratios.append(y/x)
 ratios.sort();interval=[ratios[249],ratios[9749]] if len(ratios)==10000 else None
 status='inconclusive'
 if interval:status='within-5-percent' if interval[1]<=1.05 else 'regression' if interval[0]>1.05 else 'inconclusive'
 metrics[path]={'controlMedian':av,'candidateMedian':bv,'ratio':bv/av if av else None,'blockBootstrap95RatioInterval':interval,'classification':status}
exact={}
for path in ['timerAndAnimationFrame.timersFired','timerAndAnimationFrame.animationFramesInvoked','consoleHeavy.calls','consoleHeavy.completionSignals','representativeWorkload.completionSignals']:
 groups={k:sorted(set(val(s,path) for s in v)) for k,v in samples.items()};exact[path]=groups
 assert len(groups['control'])==1 and groups['control']==groups['candidate'],(path,groups)
report={'scope':'same-source graphics OFF versus ON','status':'measured; epic baseline gate remains incomplete','method':'20 fresh processes per variant, ten ABBA blocks; 10000 paired block bootstrap resamples of median ratios; 5% boundary. Intervals crossing boundary are inconclusive, not passing.','metrics':metrics,'exactWork':exact,'limitations':['Not pre-epic baseline','Manual application windows remained open; system contention may affect measurements','Probe reports engine/CPU behavior, not physical presentation','Reported V8 heap counters are zero and cannot establish V8 memory equivalence','No Windows/Linux measurement']}
(root/'comparison.json').write_text(json.dumps(report,indent=2)+'\n')
for k,v in metrics.items():print(k,round(v['ratio'],3) if v['ratio'] else None,v['classification'])
