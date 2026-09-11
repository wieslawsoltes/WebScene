"""Analyze Metal presented times within the Foco Kestrel pan benchmark window."""
import argparse
import json
import pathlib
import re
import statistics
p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=pathlib.Path);p.add_argument('trace',type=pathlib.Path)
a=p.parse_args();log=a.log.read_text()
start=float(re.search(r'pan-start steadySeconds=([\d.]+)',log)[1]);end=float(re.search(r'pan-end steadySeconds=([\d.]+)',log)[1])
rows=[json.loads(line) for line in a.trace.read_text().splitlines()]
rows=[r for r in rows if start<=r['callbackTime']<=end and r['presented']>0]
if len(rows)<2:raise SystemExit('No usable actual-presentation timestamps; callback cadence is not a substitute')
times=[r['presented'] for r in rows];gaps=[(b-a)*1000 for a,b in zip(times,times[1:])]
print(json.dumps({'actual_presentations':len(rows),'pan_seconds':end-start,
    'presentation_hz':(len(times)-1)/(times[-1]-times[0]),
    'median_interval_ms':statistics.median(gaps),'p95_interval_ms':sorted(gaps)[int(.95*len(gaps))],
    'max_interval_ms':max(gaps),'submit_to_present_median_ms':statistics.median((r['presented']-r['submitted'])*1000 for r in rows),
    'note':'Submit-to-present measures GPU presentation pipeline latency, not input-to-photon latency.'},indent=2))
