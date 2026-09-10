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
    def test_grid_tracks_compile_without_runtime_parsing(self):
        result,out=self.compile('<div></div>', 'div { display:grid; grid-template-columns:222px minmax(250px, 1fr) 252px; grid-template-rows:auto 1fr; }')
        self.assertEqual(result.returncode,0,result.stderr)
        generated=out.read_text()
        self.assertIn('set_grid_template_columns',generated)
        self.assertIn('grid_track::sizing::minmax',generated)
        self.assertIn('grid_track::sizing::fractional',generated)
        self.assertNotIn('parse_',generated)
        self.assertNotIn('250px',generated)
        for tracks in ['-1px 1fr', 'minmax(1px)', 'bogus', '1fr -2px']:
            result,_=self.compile('<div></div>', 'div { grid-template-columns:'+tracks+'; }')
            self.assertNotEqual(result.returncode,0,tracks)
    def test_grid_repeat_is_expanded_at_build_time(self):
        result,out=self.compile('<div></div>', 'div { display:grid; grid-template-columns:repeat(3,1fr); }')
        self.assertEqual(result.returncode,0,result.stderr)
        generated=out.read_text()
        self.assertEqual(generated.count('grid_track::sizing::fractional'),3)
        self.assertNotIn('repeat(',generated)
        result,out=self.compile('<div></div>', 'div { grid-template-columns:10px repeat(2, 20px minmax(0, 1fr)); }')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(out.read_text().count('grid_track::sizing::minmax'),2)
        for tracks in ['repeat(0,1fr)', 'repeat(-1,1fr)', 'repeat(2,none)', 'repeat(2,repeat(2,1fr))', 'repeat(2.5,1fr)', 'repeat(1025,1fr)']:
            result,_=self.compile('<div></div>', 'div { grid-template-columns:'+tracks+'; }')
            self.assertNotEqual(result.returncode,0,tracks)
    def test_control_state_selectors(self):
        result,out=self.compile('<button>Go</button>', 'button:active { width:20px; } button:disabled { opacity:0.34; } button:focus-visible { width:30px; }')
        self.assertEqual(result.returncode,0,result.stderr)
    def test_text_metrics(self):
        for value,expected in [('normal','-2.0f'),('1.5','-4.5f'),('30px','30.0f'),('inherit','-1.0f')]:
            result,out=self.compile('<p>Text</p>', 'p { line-height:'+value+'; letter-spacing:1.05px; color:inherit; }')
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('set_line_height('+expected+')',out.read_text())
        for value in ['-1px','-1','bogus']:
            result,_=self.compile('<p>Text</p>', 'p { line-height:'+value+'; }')
            self.assertNotEqual(result.returncode,0)
    def test_css_audit_reports_multiple_gaps_without_output(self):
        folder=tempfile.TemporaryDirectory();self.addCleanup(folder.cleanup)
        source=pathlib.Path(folder.name)/'audit.css'
        source.write_text('div { filter:blur(2px); cursor:pointer; } p:has(a) { width:20px; }')
        result=subprocess.run([UIC,'--check-css',source],capture_output=True,text=True)
        self.assertEqual(result.returncode,1)
        self.assertIn('filter',result.stderr)
        self.assertIn('cursor',result.stderr)
        self.assertIn('has',result.stderr)
        self.assertIn('3 distinct unsupported constructs',result.stdout)
        self.assertEqual(list(source.parent.iterdir()),[source])
        source.write_text('div { display:grid; grid-template-columns:repeat(3,1fr); }')
        result=subprocess.run([UIC,'--check-css',source],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
    def test_custom_property_expressions(self):
        result,out=self.compile('<div></div>', ':root { --accent:#5ac6d2; --border:1px solid var(--accent, var(--missing, #fff)); }')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('variable_expression::kind::reference',out.read_text())
        self.assertNotIn('var(',out.read_text())
        self.assertIn('nullptr,"--accent"',out.read_text())
    def test_variable_property_lowering(self):
        result,out=self.compile('<div></div>', ':root { --width:222px; --accent:#5ac6d2; } div { width:var(--width); color:var(--accent, #fff); }')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('s.evaluate(',out.read_text())
        self.assertNotIn('parse_',out.read_text())
        self.assertNotIn('var(',out.read_text())
    def test_grid_variable_lowering(self):
        result,out=self.compile('<div></div>', ':root { --left-width:222px; --right-width:252px; } div { display:grid; grid-template-columns:var(--left-width) minmax(250px,1fr) var(--right-width); }')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('s.evaluate(',out.read_text())
        self.assertNotIn('parse_',out.read_text())
    def test_border_color_lowering(self):
        for value in ['#c69a6655','transparent','currentColor','var(--accent)']:
            result,out=self.compile('<div></div>', 'div { border-color:'+value+'; }')
            self.assertEqual(result.returncode,0,result.stderr)
            for side in ['left','top','right','bottom']:
                self.assertIn('set_border_'+side+'_color',out.read_text())
            self.assertNotIn('parse_',out.read_text())
    def test_solid_border_shorthand(self):
        for value in ['0','none','1px solid var(--line)','3px solid #c69a66']:
            result,out=self.compile('<div></div>', 'div { border:'+value+'; }')
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('set_border_left_width',out.read_text())
        result,_=self.compile('<div></div>', 'div { border:1px dashed red; }')
        self.assertNotEqual(result.returncode,0)
    def test_outer_shadow(self):
        for value in ['none','0 30px 100px #0007','var(--shadow)','0 4px 10px var(--color)']:
            result,out=self.compile('<div></div>', 'div { box-shadow:'+value+'; }')
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('set_box_shadow',out.read_text())
        result,_=self.compile('<div></div>', 'div { box-shadow:inset 0 0 1px #fff; }')
        self.assertNotEqual(result.returncode,0)
    def test_overflow_and_text_layout(self):
        result,out=self.compile('<div>Text</div>', 'div { overflow:hidden auto; text-align:center; white-space:nowrap; text-transform:uppercase; }')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('set_overflow_x(webscene::native_web::overflow_mode::hidden)',out.read_text())
        self.assertIn('set_overflow_y(webscene::native_web::overflow_mode::automatic)',out.read_text())
        for declaration in ['overflow:bogus','overflow:hidden auto scroll','text-align:bogus','white-space:bogus']:
            result,_=self.compile('<div></div>', 'div {'+declaration+';}')
            self.assertNotEqual(result.returncode,0)
    def test_structural_selectors(self):
        for selector in ['first-child','last-child','only-child']:
            result,out=self.compile('<div></div>', 'div:'+selector+' { width:10px; }')
            self.assertEqual(result.returncode,0,result.stderr)
    def test_font_family_compiles(self):
        result,out=self.compile('<p>Hello</p>', 'p { font-family: Arial, sans-serif; }')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('s.set_font_family("Arial, sans-serif")',out.read_text())
    def test_short_hex_colors(self):
        for color,expected in [('#fff',0xffffffff),('#0005',0x55),('#aBc',0xaabbccff),('#1234',0x11223344)]:
            result,out=self.compile('<p>Hello</p>', 'p { color: '+color+'; }')
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('s.set_foreground_rgba('+str(expected)+'u)',out.read_text())
        for color in ['#12','#12345','#1234567','#ggg']:
            result,_=self.compile('<p>Hello</p>', 'p { color: '+color+'; }')
            self.assertNotEqual(result.returncode,0)
    def test_module_output(self):
        result,out=self.compile('<button id="go">Hello</button>')
        self.assertEqual(result.returncode,0,result.stderr)
        module=out.with_suffix('.cppm')
        command=[UIC,out.with_name('view.html'),module,'--module','app.views.main']
        result=subprocess.run(command,capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        text=module.read_text()
        self.assertIn('export module app.views.main;',text)
        self.assertIn('export namespace compiled_ui',text)
        self.assertNotIn('#pragma once',text)
        subprocess.run(command,check=True)
        self.assertEqual(text,module.read_text())
        for name in ['', '.app', 'app.', 'app..view', '3app', 'app;bad']:
            result=subprocess.run(command[:-1]+[name],capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0,name)
            self.assertIn('Invalid module name',result.stderr)
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
    def test_template_scripts_rejected(self):
        result,_=self.compile('<template id="item"><script>alert(1)</script></template>')
        self.assertNotEqual(result.returncode,0)
    def test_template_global_ids_rejected(self):
        result,_=self.compile('<template id="item"><div id="duplicate"></div></template>')
        self.assertNotEqual(result.returncode,0);self.assertIn('data-ref',result.stderr)
    def test_template_duplicate_references_rejected(self):
        result,_=self.compile('<template id="item"><div data-ref="x"></div><div data-ref="x"></div></template>')
        self.assertNotEqual(result.returncode,0)
    def test_nested_templates_rejected(self):
        result,_=self.compile('<template id="item"><div><template id="nested"></template></div></template>')
        self.assertNotEqual(result.returncode,0)
if __name__=='__main__':unittest.main()
