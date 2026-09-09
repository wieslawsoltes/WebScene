"""Measure synchronous CSS-variable resizing without a renderer or GPU.

Usage: python benchmarks/css-variable-resize.py /absolute/path/to/native/library
The library directory must contain its matching ICU data and bootstrap snapshot.
Each sample changes a grid track and a fixed toast offset, then verifies both. Setup and 20
warm-up updates are excluded; the remaining 60 samples include style and layout.
"""
import argparse
import ctypes as c
import json
import pathlib
import statistics
import time

SOURCE = r"""
document.body.innerHTML='<style>:root{--size:252px;--color:#123456}.shell{display:grid;grid-template-columns:200px minmax(250px,1fr) var(--size)}.item{color:var(--color);font-size:12px;padding:2px;border:1px solid #333}.item:nth-child(2n){background:#eee}.item span{font-weight:bold}.right{width:100%}.toast{position:fixed;right:calc(var(--size) + 16px);width:20px;height:20px}</style><div class="shell"><div id="left"></div><div id="center"></div><div class="right" id="right"></div></div><div class="toast" id="toast"></div>';
document.getElementById('center').innerHTML=Array.from({length:1000},(_,i)=>'<div class="item"><span>Item '+i+'</span></div>').join('');
const root=document.documentElement, right=document.getElementById('right');
const samples=[];
for(let i=0;i<80;i++){
  const start=performance.now();
  root.style.setProperty('--size',(252+i%60)+'px');
  const width=right.offsetWidth;
  if(width!==252+i%60)throw Error('Incorrect width '+width);
  const inset=innerWidth-document.getElementById('toast').getBoundingClientRect().right;
  if(Math.abs(inset-(252+i%60+16))>.1)throw Error('Incorrect toast inset '+inset);
  if(i>=20)samples.push(performance.now()-start);
}
console.log('RESIZE_BENCHMARK:'+JSON.stringify(samples));
"""


def measure(library):
    lib = c.CDLL(str(library))
    lib.webscene_engine_create.argtypes = [c.c_uint32]
    lib.webscene_engine_create.restype = c.c_void_p
    lib.webscene_engine_destroy.argtypes = [c.c_void_p]
    lib.webscene_engine_execute_script.argtypes = [
        c.c_void_p, c.c_char_p, c.c_size_t, c.c_char_p, c.c_size_t]
    lib.webscene_engine_configure_diagnostics.argtypes = [
        c.c_void_p, c.c_uint32, c.c_void_p, c.c_void_p]
    lib.webscene_engine_take_console_message.argtypes = [
        c.c_void_p, c.c_void_p, c.c_size_t]
    lib.webscene_engine_take_console_message.restype = c.c_size_t
    lib.webscene_engine_copy_last_error.argtypes = [
        c.c_void_p, c.c_void_p, c.c_size_t]
    lib.webscene_engine_copy_last_error.restype = c.c_size_t
    engine = lib.webscene_engine_create(0)
    if not engine:
        raise RuntimeError("Engine creation failed")
    try:
        # Enable the existing pull-based console channel for one result.
        lib.webscene_engine_configure_diagnostics(engine, 4, None, None)
        source = SOURCE.encode()
        name = b"css-resize-benchmark.js"
        if not lib.webscene_engine_execute_script(
                engine, source, len(source), name, len(name)):
            raise RuntimeError("Benchmark script rejected")
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            size = lib.webscene_engine_take_console_message(engine, None, 0)
            if size:
                buffer = c.create_string_buffer(size)
                lib.webscene_engine_take_console_message(engine, buffer, size)
                message = buffer.value.decode()
                if "RESIZE_BENCHMARK:" in message:
                    samples = sorted(json.loads(message.split("RESIZE_BENCHMARK:", 1)[1]))
                    return {
                        "library": str(library),
                        "samples": len(samples),
                        "median_ms": statistics.median(samples),
                        "p95_ms": samples[int(.95 * (len(samples) - 1))],
                    }
            time.sleep(.01)
        buffer = c.create_string_buffer(16384)
        lib.webscene_engine_copy_last_error(engine, buffer, len(buffer))
        raise RuntimeError("Benchmark timed out: " + buffer.value.decode())
    finally:
        lib.webscene_engine_destroy(engine)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=lambda value: pathlib.Path(value).resolve())
    print(json.dumps(measure(parser.parse_args().library)))
