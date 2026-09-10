import pathlib
import subprocess
import sys
import tempfile
import unittest
UIC=pathlib.Path(sys.argv.pop(1)).resolve()
class CompilerTests(unittest.TestCase):
    def compile(self,body,css=''):
        folder=tempfile.TemporaryDirectory();self.addCleanup(folder.cleanup)
        root=pathlib.Path(folder.name);source=root/'view.html';output=root/'view.hpp'
        source.write_text('<!doctype html>\n<html><head><style>'+css+'</style></head><body>'+body+'</body></html>')
        result=subprocess.run([UIC,source,output],capture_output=True,text=True)
        return result,output
    def test_compiled_values_and_determinism(self):
        result,out=self.compile('<button id="go">Hello &amp; goodbye</button>','#go { width: 20px; color: #123456; }')
        self.assertEqual(result.returncode,0,result.stderr);text=out.read_text();self.assertIn('Hello & goodbye',text);self.assertIn('20.0f',text)
        self.assertNotIn('webscene_native::',text);self.assertNotIn('s.width',text);self.assertNotIn('parse_',text);self.assertNotIn('20px',text)
        source=out.with_name('view.html');subprocess.run([UIC,source,out],check=True);self.assertEqual(text,out.read_text())
    def test_unsupported_property_has_source_diagnostic(self):
        result,out=self.compile('<div></div>','div { filter: blur(2px); }');self.assertNotEqual(result.returncode,0);self.assertIn(':2:1: error:',result.stderr);self.assertIn('filter',result.stderr);self.assertFalse(out.exists())
    def test_scripts_rejected(self):
        result,_=self.compile('<script>alert(1)</script>');self.assertNotEqual(result.returncode,0)
    def test_js_attributes_rejected(self):
        result,_=self.compile('<button onclick="x()">Go</button>');self.assertNotEqual(result.returncode,0)
    def test_duplicate_ids_rejected(self):
        result,_=self.compile('<div id="x"></div><div id="x"></div>');self.assertNotEqual(result.returncode,0);self.assertIn('duplicate id',result.stderr)
    def test_unknown_selector_rejected(self):
        result,_=self.compile('<div></div>','div:has(button) { width: 1px; }');self.assertNotEqual(result.returncode,0)
    def test_missing_resource_rejected(self):
        result,_=self.compile('<link rel="stylesheet" href="missing.css">');self.assertNotEqual(result.returncode,0)
    def test_nonzero_unitless_length_rejected(self):
        result,_=self.compile('<div></div>','div { width: 10; }');self.assertNotEqual(result.returncode,0)
if __name__=='__main__':unittest.main()
