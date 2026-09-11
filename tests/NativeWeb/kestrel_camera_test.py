import json, math, pathlib, subprocess, sys
expected=json.loads(pathlib.Path(__file__).with_name('kestrel_camera_reference.json').read_text())['values']
actual=[[float(x) for x in line.split()] for line in subprocess.check_output([sys.argv[1]],text=True).splitlines()]
assert len(actual)==len(expected)
for row,(a,b) in enumerate(zip(actual,expected)):
    assert len(a)==len(b)
    for column,(x,y) in enumerate(zip(a,b)):
        assert math.isclose(x,y,rel_tol=1e-9,abs_tol=1e-8),(row,column,x,y)
print('Kestrel camera: 154 upstream reference values passed')
