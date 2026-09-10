import json, pathlib, sys, statistics, collections
for name in sys.argv[1:]:
    lines=pathlib.Path(name).read_text().splitlines()
    timeline=json.loads(next(l.split(': ',1)[1] for l in lines if l.startswith(('Kestrel pan composition timeline:', 'Kestrel sidebar timeline:'))))
    f=timeline['timestampFrequency']
    lo=(timeline['submittedMoves'][0]['submittedAt']/f+1)*1e9
    hi=timeline['submittedMoves'][-1]['submittedAt']/f*1e9
    records=[json.loads(l.split(': ',1)[1]) for l in lines if l.startswith('Frame pipeline:')]
    records=sorted((r for r in records if lo<=r['ns']<=hi), key=lambda r:r['ns'])
    durations=collections.defaultdict(list); counts=collections.Counter(r['stage'] for r in records)
    starts=collections.defaultdict(list); last={}; by_input=collections.defaultdict(lambda:collections.defaultdict(float))
    for r in records:
        s=r['stage']; t=r['ns']
        if s.endswith('-start'): starts[s[:-6]].append(t)
        if s.endswith('-end') or s in ['publish-deferred','publish-discarded']:
            base=s.rsplit('-',1)[0]
            if starts[base]:
                elapsed=(t-starts[base].pop())/1e6
                durations[s].append(elapsed)
                by_input[r['sequence']][s]+=elapsed
        if s=='raf-start' and 'input-end' in last: durations['input-to-raf'].append((t-last['input-end'])/1e6)
        if s=='publish-start' and 'raf-end' in last: durations['raf-to-publish'].append((t-last['raf-end'])/1e6)
        last[s]=t
    print(name, 'counts',dict(counts))
    for k,vs in durations.items():
        vs.sort();print(k,dict(n=len(vs),median=round(statistics.median(vs),4),p95=round(vs[int((len(vs)-1)*.95)],4),max=round(vs[-1],4)))
    for key in ['layout-end','event-end']:
        vs=sorted(v[key] for v in by_input.values() if 'input-end' in v)
        if vs: print(key+' per input', 'median',statistics.median(vs),'p95',vs[int((len(vs)-1)*.95)])
