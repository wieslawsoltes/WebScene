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
    def test_opacity_clamps_but_negative_flex_factors_fail(self):
        for value,expected in [('-0.5','0.0f'),('2','1.0f'),('5e-1','0.5f'),('50%','0.5f'),('-20%','0.0f'),('+2e2%','1.0f')]:
            result,out=self.compile('<div></div>', 'div { opacity:'+value+'; }')
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('s.set_opacity('+expected+')',out.read_text())
        for value in ['50%%','%','1e%','50 %']:
            result,_=self.compile('<div></div>', 'div { opacity:'+value+'; }')
            self.assertNotEqual(result.returncode,0,value)
        for name in ['flex-grow','flex-shrink']:
            result,_=self.compile('<div></div>', 'div { '+name+':-0.5; }')
            self.assertNotEqual(result.returncode,0)
            self.assertIn('numeric value out of range',result.stderr)

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
    def test_background_none_resets_background(self):
        result,out=self.compile('<div></div>', 'div { background:#ffffff; background:none; }')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('s.reset_background();',out.read_text())
        result,_=self.compile('<div></div>', 'div { background-color:none; }')
        self.assertNotEqual(result.returncode,0)

    def test_calc_rejects_invalid_length_products(self):
        for value in ['calc(10px / 0)', 'calc(10px * 2px)', 'calc(2 / 10px)']:
            result,_=self.compile('<div></div>', 'div { left:'+value+'; }')
            self.assertNotEqual(result.returncode,0,value)

    def test_inset_shorthand(self):
        for value,expected in [('1px',[1,1,1,1]),('1px 2px',[1,2,1,2]),
                               ('1px 2px 3px',[1,2,3,2]),('1px 2px 3px 4px',[1,2,3,4])]:
            result,out=self.compile('<div></div>', 'div { position:absolute; inset:'+value+'; }')
            self.assertEqual(result.returncode,0,result.stderr)
            for side,number in zip(['top','right','bottom','left'],expected):
                self.assertIn('set_'+side+'({'+str(number)+'.0f',out.read_text())
        result,_=self.compile('<div></div>', 'div { inset:auto -2px 10% 0; }')
        self.assertEqual(result.returncode,0,result.stderr)
        result,_=self.compile('<div></div>', 'div { inset:1px 2px 3px 4px 5px; }')
        self.assertNotEqual(result.returncode,0)

    def test_grid_fraction_numeric_forms(self):
        for tracks in ['.5fr +1fr', '5e-1fr 1E+0fr', 'minmax(0px,.5fr) 1fr']:
            result,out=self.compile('<div></div>', 'div { display:grid; grid-template-columns:'+tracks+'; }')
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('0.5f',out.read_text())
        for tracks in ['-.5fr', 'minmax(0px,-.5fr)', '1efr', '1.fr']:
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
        self.assertIn('in rule: div',result.stderr)
        self.assertIn('has',result.stderr)
        self.assertIn('3 distinct unsupported constructs',result.stdout)
        self.assertEqual(list(source.parent.iterdir()),[source])
        source.write_text('@media (max-width:400px) { div { cursor:pointer; } } div { cursor:pointer; }')
        result=subprocess.run([UIC,'--check-css',source],capture_output=True,text=True)
        self.assertEqual(result.returncode,1)
        self.assertIn('@media (max-width:400px) > div',result.stderr)
        self.assertIn('1 distinct unsupported constructs',result.stdout)
        source.write_text('/* header */\n@media (max-width:400px) {\n  div {\n    cursor:pointer;\n    cursor:pointer;\n  }\n}\n')
        result=subprocess.run([UIC,'--check-css',source],capture_output=True,text=True)
        self.assertEqual(result.returncode,1)
        self.assertIn(str(source)+':4:5',result.stderr)
        self.assertIn(str(source)+':5:5',result.stderr)
        self.assertIn('1 distinct unsupported constructs',result.stdout)
        source.write_text('/* header */\n@media print {\n  div::before { color:black; }\n}\n@import "other.css";\n')
        result=subprocess.run([UIC,'--check-css',source],capture_output=True,text=True)
        self.assertEqual(result.returncode,1)
        for location in [':2:1',':3:3',':5:1']:
            self.assertIn(str(source)+location,result.stderr)
        self.assertIn('3 distinct unsupported constructs',result.stdout)
        source.write_text('div { display:grid; grid-template-columns:repeat(3,1fr); }')
        result=subprocess.run([UIC,'--check-css',source],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
    def test_media_numeric_grammar_audit_and_compile_agree(self):
        for condition,generated in [('(min-width:.5px)', ',0.5f,1e+09f,0,0.0f,1e+09f}'),
                                    ('(MAX-WIDTH: +2E2PX)', ',0.0f,200.0f,0,0.0f,1e+09f}'),
                                    ('( min-height : 0 )', ',0.0f,1e+09f,0,0.0f,1e+09f}'),
                                    ('(max-height:-1px)', ',0.0f,1e+09f,0,0.0f,-1.0f}')]:
            css='@media '+condition+' { div { width:1px; } }'
            result,out=self.compile('<div></div>',css)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn(generated,out.read_text())
            source=out.with_suffix('.css');source.write_text(css)
            audit=subprocess.run([UIC,'--check-css',source],capture_output=True,text=True)
            self.assertEqual(audit.returncode,0,audit.stderr)
        for value in ['1.', '1e', '2', '1 px', '1e999px', 'auto', '10%']:
            css='@media (min-width:'+value+') { div { width:1px; } }'
            result,out=self.compile('<div></div>',css)
            self.assertNotEqual(result.returncode,0,value)
            source=out.with_suffix('.css');source.write_text(css)
            audit=subprocess.run([UIC,'--check-css',source],capture_output=True,text=True)
            self.assertNotEqual(audit.returncode,0,value)

    def test_preview_linked_css_reports_each_exact_location(self):
        folder=tempfile.TemporaryDirectory();self.addCleanup(folder.cleanup)
        root=pathlib.Path(folder.name)
        css=root/'linked.css'
        css.write_text('/* audit */\n@media print { div { width:1px; } }\ndiv {\n  filter:blur(2px);\n  filter:blur(3px);\n}\n')
        source=root/'view.html'
        source.write_text('<html><head><link rel="stylesheet" href="linked.css"></head><body><div></div></body></html>')
        result=subprocess.run([UIC,source,root/'view.cppm','--module','audit.view','--preview'],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        for line,column in [(2,1),(4,3),(5,3)]:
            self.assertIn(str(css)+':'+str(line)+':'+str(column)+': warning: preview:',result.stderr)
        self.assertEqual(result.stderr.count(': warning: preview:'),3)

    def test_repeated_elements_use_parser_lines(self):
        folder=tempfile.TemporaryDirectory();self.addCleanup(folder.cleanup)
        root=pathlib.Path(folder.name);source=root/'view.html';output=root/'view.hpp'
        source.write_text('<html><body>\n<div>first</div>\n<div>second</div>\n<section>\n<div>third</div>\n</section>\n</body></html>')
        result=subprocess.run([UIC,source,output],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        generated=output.read_text()
        for line in [2,3,4,5]:
            self.assertIn('#line '+str(line)+' "'+str(source)+'"',generated)

    def test_element_rejections_use_current_parser_line(self):
        for element,message in [('<input>', 'unsupported Native Web element'),
                                ('<script></script>', 'excludes scripts'),
                                ('<template></template>', 'requires a nonempty id'),
                                ('<link>', 'only local stylesheet links')]:
            folder=tempfile.TemporaryDirectory();self.addCleanup(folder.cleanup)
            root=pathlib.Path(folder.name);source=root/'view.html';output=root/'view.hpp'
            source.write_text('<html><body>\n<div>first</div>\n<div>second</div>\n'+element+'\n</body></html>')
            result=subprocess.run([UIC,source,output],capture_output=True,text=True)
            self.assertNotEqual(result.returncode,0)
            self.assertIn(str(source)+':4:1: error:',result.stderr)
            self.assertIn(message,result.stderr)
            self.assertFalse(output.exists())

    def test_inline_errors_anchor_to_owning_element(self):
        folder=tempfile.TemporaryDirectory();self.addCleanup(folder.cleanup)
        root=pathlib.Path(folder.name);source=root/'view.html';output=root/'view.hpp'
        source.write_text('<html><body>\n<div style="width:1px">first</div>\n<div style="width:bogus">second</div>\n</body></html>')
        result=subprocess.run([UIC,source,output],capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0)
        self.assertIn(str(source)+':3:1: error:',result.stderr)
        self.assertFalse(output.exists())
        preview=subprocess.run([UIC,source,output,'--preview'],capture_output=True,text=True)
        self.assertEqual(preview.returncode,0,preview.stderr)
        self.assertIn(str(source)+':3:1: warning: preview: width: bogus:',preview.stderr)

    def test_css_syntax_error_location(self):
        folder=tempfile.TemporaryDirectory();self.addCleanup(folder.cleanup)
        source=pathlib.Path(folder.name)/'invalid.css'
        source.write_text('div {\n  broken;\n  also-broken;\n}\n')
        result=subprocess.run([UIC,'--check-css',source],capture_output=True,text=True)
        self.assertEqual(result.returncode,1)
        self.assertIn(str(source)+':2:9: error: invalid CSS syntax',result.stderr)
        self.assertIn('2 parse errors',result.stderr)

    def test_external_stylesheet_build_locations(self):
        folder=tempfile.TemporaryDirectory();self.addCleanup(folder.cleanup)
        root=pathlib.Path(folder.name);source=root/'view.html';css=root/'style.css'
        source.write_text('<link rel="stylesheet" href="style.css"><div></div>')
        for text,location in [('div {\n  cursor:pointer;\n}',':2:3'),('\n  div::before { color:black; }',':2:3'),('div {\n  broken;\n}',':2:9')]:
            css.write_text(text)
            result=subprocess.run([UIC,source,root/'view.hpp'],capture_output=True,text=True)
            self.assertEqual(result.returncode,1)
            self.assertIn(str(css.resolve())+location+': error:',result.stderr)

    def test_preserves_whitespace_text_nodes(self):
        result,out=self.compile('<div><span>A</span> <span>B</span></div><div>  </div>')
        self.assertEqual(result.returncode,0,result.stderr)
        generated=out.read_text()
        self.assertRegex(generated, r'd\.text\(n[0-9]+," "\);')
        self.assertRegex(generated, r'd\.text\(n[0-9]+,"  "\);')

    def test_hidden_attribute(self):
        for value in ['', 'hidden', 'false']:
            result,out=self.compile('<div hidden="'+value+'">Hidden</div>')
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('"hidden",',out.read_text())
        result,_=self.compile('<div hidden="UNTIL-FOUND">Hidden</div>')
        self.assertNotEqual(result.returncode,0)
        self.assertIn('find/reveal support',result.stderr)

    def test_layout_keywords_are_ascii_case_insensitive(self):
        for name,value in [('display','inline-flex'),('flex-direction','column'),('align-items','flex-start'),('justify-content','space-between'),('position','absolute'),('box-sizing','border-box')]:
            lower,out=self.compile('<div></div>', 'div {'+name+':'+value+';}')
            self.assertEqual(lower.returncode,0,lower.stderr)
            expected=[line for line in out.read_text().splitlines() if 'd.add_rule' in line]
            upper,out=self.compile('<div></div>', 'div {'+name.upper()+':'+value.upper()+';}')
            self.assertEqual(upper.returncode,0,upper.stderr)
            self.assertEqual(expected,[line for line in out.read_text().splitlines() if 'd.add_rule' in line])

    def test_length_unit_case(self):
        for unit in ['PX','EM','REM','VW','VH','DVW','DVH']:
            result,out=self.compile('<div></div>', 'div { width:2'+unit+'; height:var(--Size); --Size:3'+unit+'; }')
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('"--Size"',out.read_text())
            self.assertNotIn('parse_length',out.read_text())
        for value in ['1e999PX','1PPX','2 PX']:
            result,_=self.compile('<div></div>', 'div { width:'+value+'; }')
            self.assertNotEqual(result.returncode,0,value)

    def test_pixel_property_grammar(self):
        for name in ['font-size','line-height','letter-spacing','word-spacing','border-left-width']:
            for value in ['+.5e1PX','-0px','0']:
                result,_=self.compile('<div></div>', 'div {'+name+':'+value+';}')
                self.assertEqual(result.returncode,0,name+':'+value+result.stderr)
            for value in ['1e999px','1em','2 PX']:
                result,_=self.compile('<div></div>', 'div {'+name+':'+value+';}')
                self.assertNotEqual(result.returncode,0,name+':'+value)
        result,out=self.compile('<div></div>', 'div {font-size:+.5e1PX;}')
        self.assertIn('s.set_font_size(5.0f)',out.read_text())

    def test_length_property_ranges(self):
        for name in ['width','height','min-width','max-height','padding','gap','flex-basis','border-radius']:
            for value in ['-1px','-.5%','-2EM']:
                result,_=self.compile('<div></div>', 'div {'+name+':'+value+';}')
                self.assertNotEqual(result.returncode,0,name+':'+value)
            result,_=self.compile('<div></div>', 'div {'+name+':-0px;}')
            self.assertEqual(result.returncode,0,result.stderr)
        for name in ['padding','gap','border-radius']:
            result,_=self.compile('<div></div>', 'div {'+name+':auto;}')
            self.assertNotEqual(result.returncode,0,name)
        for name in ['margin','left','right','top','bottom']:
            result,_=self.compile('<div></div>', 'div {'+name+':-2px;}')
            self.assertEqual(result.returncode,0,result.stderr)

    def test_literal_auto_case(self):
        for name in ['width','height','left','margin','inset','flex-basis']:
            result,out=self.compile('<div></div>', 'div {'+name+':AuTo;}')
            self.assertEqual(result.returncode,0,result.stderr)
            if name == 'margin':
                self.assertIn('set_margin_left_auto(true)',out.read_text())
        for name in ['padding','gap','border-radius']:
            result,_=self.compile('<div></div>', 'div {'+name+':AUTO;}')
            self.assertNotEqual(result.returncode,0,name)
        result,out=self.compile('<div></div>', 'div {font-family:AUTO;}')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('set_font_family("AUTO")',out.read_text())

    def test_font_weight_keywords(self):
        for value,expected in [('normal',400),('NORMAL',400),('bold',700),('BoLd',700),('INHERIT',0),('UNSET',0)]:
            result,out=self.compile('<div>Text</div>', 'div {font-weight:'+value+';}')
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('set_font_weight('+str(expected)+')',out.read_text())
        for value in ['boldish','0','1001']:
            result,_=self.compile('<div>Text</div>', 'div {font-weight:'+value+';}')
            self.assertNotEqual(result.returncode,0,value)

    def test_translation_transform_profile(self):
        for value in ['none','NONE','translate(10px)','translate(50%, -2px)','translateX(-50%)','translateY(7px)','TRANSLATEX(+.5e1PX)']:
            result,out=self.compile('<div></div>', 'div {transform:'+value+';}')
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('set_translation(',out.read_text())
        for value in ['translate(1px,2px,3px)','translate(1px 2px)','translate(auto)','translateX(auto)','translateY(2)','translateX(1px) rotate(2deg)','translateX(1px,2px)']:
            result,_=self.compile('<div></div>', 'div {transform:'+value+';}')
            self.assertNotEqual(result.returncode,0,value)

    def test_overflow_and_text_keyword_case(self):
        for name,value in [('overflow','hidden auto'),('overflow-x','clip'),('overflow-y','scroll'),('text-align','center'),('white-space','pre-wrap'),('text-transform','capitalize')]:
            result,out=self.compile('<div>Text</div>', 'div {'+name+':'+value+';}')
            self.assertEqual(result.returncode,0,result.stderr)
            expected=[line for line in out.read_text().splitlines() if 'd.add_rule' in line]
            result,out=self.compile('<div>Text</div>', 'div {'+name+':'+value.upper()+';}')
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual(expected,[line for line in out.read_text().splitlines() if 'd.add_rule' in line])

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
    def test_border_width_and_style_shorthands(self):
        result,out=self.compile('<div></div>', 'div { border-width:.5px +2e0px 0 -0px; border-style:solid none hidden solid; }')
        self.assertEqual(result.returncode,0,result.stderr)
        generated=out.read_text()
        for side,value in [('top','true'),('right','false'),('bottom','false'),('left','true')]:
            self.assertIn('set_border_'+side+'_solid('+value+')',generated)
        for value in ['.5px solid #fff','+2e0px solid black','-0px solid white','+0e0 solid black']:
            result,_=self.compile('<div></div>', 'div { border:'+value+'; }')
            self.assertEqual(result.returncode,0,result.stderr)
        for name,value in [('border-width','-1px'),('border-width','2'),('border-width','1%'),('border-width','1px 2px 3px 4px 5px'),('border-style','solid dashed'),('border','-1px solid black')]:
            result,_=self.compile('<div></div>', 'div { '+name+':'+value+'; }')
            self.assertNotEqual(result.returncode,0,name+':'+value)

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
    def test_compound_negation(self):
        for selector in ['div:not([type=checkbox])','div:not(.hidden, #excluded)','div:not(:not(.visible))']:
            result,out=self.compile('<div></div>', selector+' { width:10px; }')
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('.parts.front()',out.read_text())
        result,_=self.compile('<div></div>', 'div:not(section > div) { width:10px; }')
        self.assertNotEqual(result.returncode,0)
    def test_flex_shorthand_numeric_grammar_matches_longhands(self):
        for grow,shrink,basis in [('.5','+2','10px'), ('5e-1','2E+0','10px'),
                                  ('+0.5','2','1e1px'), ('-0','+0','-0px')]:
            shorthand,out=self.compile('<div></div>', 'div { flex:'+grow+' '+shrink+' '+basis+'; }')
            self.assertEqual(shorthand.returncode,0,shorthand.stderr)
            longhands,expanded=self.compile('<div></div>', 'div { flex-grow:'+grow+'; flex-shrink:'+shrink+'; flex-basis:'+basis+'; }')
            self.assertEqual(longhands.returncode,0,longhands.stderr)
            import re
            setters=lambda text: re.findall(r's\.set_flex_(?:grow|shrink|basis)\([^;]+;',text)
            self.assertEqual(setters(out.read_text()),setters(expanded.read_text()))
        for value in ['.5', '+.5', '5e-1']:
            result,out=self.compile('<div></div>', 'div { flex:'+value+'; }')
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('set_flex_grow(0.5f)',out.read_text())
        for value in ['-1', '1 -2', '1 1 -2px', '1e', '1.', '1 2 3px 4', '1e999']:
            result,_=self.compile('<div></div>', 'div { flex:'+value+'; }')
            self.assertNotEqual(result.returncode,0,value)

    def test_flex_shorthand(self):
        for value in ['1','2','auto','none','initial','1 0 20px','1 30%','2 3']:
            result,out=self.compile('<div></div>', 'div { flex:'+value+'; flex-wrap:wrap; }')
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('set_flex_basis',out.read_text())
        for value in ['-1','1 -2 0','1 1 -2px','1 2 3 4']:
            result,_=self.compile('<div></div>', 'div { flex:'+value+'; }')
            self.assertNotEqual(result.returncode,0)
    def test_stroke_width_numeric_grammar(self):
        for value in ['.5', '+5e-1', '1E+1PX', '-0', '-0px', '25%']:
            result,out=self.compile('<svg></svg>', 'svg { stroke-width:'+value+'; }')
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('set_svg_stroke_width("'+value.lower()+'")',out.read_text())
        for value in ['-1', '-.5px', '-1%', '1e999', '1.', '1 px', '1em', 'NaN', '1%%']:
            result,_=self.compile('<svg></svg>', 'svg { stroke-width:'+value+'; }')
            self.assertNotEqual(result.returncode,0,value)

    def test_font_numeric_forms_and_family_case(self):
        for value in ['.5px/1.2 MixedCaseFont', '+5e-1PX/+1.2 MixedCaseFont',
                      '.5px/NORMAL MixedCaseFont', '0/-0 MixedCaseFont']:
            result,out=self.compile('<div></div>', 'div { font:'+value+'; }')
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('set_font_family("MixedCaseFont")',out.read_text())
        for value in ['-1px MixedCaseFont', '1px/-1 MixedCaseFont',
                      '1e999px MixedCaseFont', '1px/1e999 MixedCaseFont',
                      '1 MixedCaseFont', '1.px MixedCaseFont']:
            result,_=self.compile('<div></div>', 'div { font:'+value+'; }')
            self.assertNotEqual(result.returncode,0,value)
        for value in ['0','-0','+0','0e1']:
            result,out=self.compile('<div></div>', 'div { line-height:'+value+'; }')
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('set_line_height(-3.0f)',out.read_text())

    def test_font_shorthand(self):
        for value in ['inherit','10px Consolas,"SFMono-Regular",monospace','10px/19px Consolas,monospace','10px/1.5 monospace']:
            result,out=self.compile('<div></div>', 'div { font:'+value+'; }')
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('set_font_family',out.read_text())
            self.assertIn('set_line_height',out.read_text())
        for value in ['10px','italic 10px monospace','-10px monospace']:
            result,_=self.compile('<div></div>', 'div { font:'+value+'; }')
            self.assertNotEqual(result.returncode,0)
    def test_preview_is_explicit_and_reports_omissions(self):
        result,out=self.compile('<div>Hello</div>', 'div { color-scheme:dark; width:20px; }')
        self.assertNotEqual(result.returncode,0)
        result=subprocess.run([UIC,out.with_name('view.html'),out,'--preview'],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('warning: preview:',result.stderr)
        self.assertIn('color-scheme',result.stderr)
        self.assertIn('set_width',out.read_text())
    def test_dynamic_viewport_units(self):
        result,out=self.compile('<div></div>', 'div { height:100dvh; width:50dvw; }')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertNotIn('100dvh',out.read_text())
    def test_generated_namespace(self):
        result,out=self.compile('<div></div>')
        module=out.with_suffix('.cppm')
        result=subprocess.run([UIC,out.with_name('view.html'),module,'--module','app.tabs','--namespace','app_tabs'],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('namespace app_tabs',module.read_text())
        self.assertNotIn('namespace compiled_ui',module.read_text())
    def test_stacking_and_pointer_properties(self):
        result,out=self.compile('<div></div>', 'div { z-index:-3; pointer-events:none; visibility:hidden; }')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('set_z_index(-3)',out.read_text())
        for value in ['2.5','2147483648','bogus']:
            result,_=self.compile('<div></div>', 'div { z-index:'+value+'; }')
            self.assertNotEqual(result.returncode,0)
    def test_transparent_color_mix(self):
        result,out=self.compile('<div></div>', 'div { border-bottom:1px solid color-mix(in srgb,var(--accent) 25%,transparent); }')
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('color_with_opacity',out.read_text())
        result,_=self.compile('<div></div>', 'div { color:color-mix(in srgb,#fff 101%,transparent); }')
        self.assertNotEqual(result.returncode,0)
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
