/* Configurable block definitions, typed instance properties and action authoring. */
(function(root){
'use strict';
const K=root.Kestrel,D=K.DynamicBlocks,P=K.Production,U=K.UI,esc=U.escape;
const commands=[
 ['dynamic-properties','Block parameters','BPROPERTIES · DYNBLOCK','properties'],
 ['dynamic-definition','Dynamic definition','BDEFINE · BEDITDYNAMIC','drawing'],
 ['dynamic-action','Add block action','BACTION','move'],
 ['dynamic-lookup','Match lookup properties','BLOOKUPMATCH','properties'],
 ['dynamic-reset','Reset block parameters','BRESET','undo'],
 ['dynamic-demo','Configurable plate','DYNAMICDEMO','rectangle']
];
for(const[id,label,alias,icon]of commands)U.commands.push({id,label,alias,icon,description:label+' — native configurable blocks'});
U.groups.Blocks=[{name:'Reusable content',large:['block-create','block-insert'],columns:[['block-edit','block-save','attributes']]},{name:'Dynamic behavior',large:['dynamic-properties','dynamic-definition'],columns:[['dynamic-action','dynamic-reset','dynamic-lookup']]},{name:'Example',large:['dynamic-demo']}];
const select=(key,label,rows,value)=>`<div class="form-field"><label>${esc(label)}</label><select name="${key}">${rows.map(([v,t])=>`<option value="${esc(v)}"${String(v)===String(value)?' selected':''}>${esc(t)}</option>`).join('')}</select></div>`;
const json=(key,value,label)=>`<div class="form-field full"><label>${esc(label)}</label><textarea name="${key}" rows="16" spellcheck="false" style="font-family:monospace">${esc(JSON.stringify(value,null,2))}</textarea></div>`;
function starter(b){
 const points=b.entities.flatMap(e=>K.Geo.geometry(e).points||[]);let span=100;
 if(points.length){let lo=Infinity,hi=-Infinity;for(const p of points){lo=Math.min(lo,p[0]);hi=Math.max(hi,p[0]);}span=Math.max(1,hi-lo);}
 return {version:1,parameters:[{name:'Size',type:'length',default:span,min:span/10,max:span*10}],actions:[{type:'scale',targets:[...b.entities.map(e=>e.id),...(b.attributes||[]).map(a=>'attribute:'+a.tag)],factor:'Size / '+span,center:[0,0,0]}]};
}
function demo(){
 const doc=new K.Drawing('Configurable fabrication plates');doc.currentLayer='0';
 const b={id:K.uid('block'),name:'Configurable plate',entities:[
  {id:'plate',type:'POLYLINE',closed:true,points:[[0,0,0],[100,0,0],[100,60,0],[0,60,0]],layer:'0',color:'byblock'},
  {id:'hole',type:'CIRCLE',center:[80,30,0],radius:3,layer:'0',color:'byblock'},
  {id:'mark',type:'LINE',points:[[10,10,0],[15,10,0]],layer:'construction',color:'bylayer'}
 ],attributes:[{tag:'PART',value:'PLATE ${Width} x ${Height}',position:[0,-10,0],height:4}],dynamic:{version:1,
 parameters:[{name:'Width',type:'length',default:100,min:60,max:300,step:10},{name:'Height',type:'length',default:60,min:30,max:200,step:10},{name:'Rotation',type:'angle',default:0,min:-360,max:360},{name:'Flipped',type:'boolean',default:false},{name:'Detail',type:'enum',default:'Detailed',values:['Detailed','Outline']},{name:'Count',type:'integer',default:3,min:1,max:12},{name:'Pitch',type:'length',default:15,min:5,max:100},{name:'Fastener',type:'enum',default:'M6',values:['M6','M8','M10']},{name:'HoleRadius',type:'length',default:3,min:1,max:10},{name:'Area',type:'number',default:6000,expression:'Width * Height'}],
 lookups:[{parameter:'Fastener',rows:[{value:'M6',set:{HoleRadius:3}},{value:'M8',set:{HoleRadius:4}},{value:'M10',set:{HoleRadius:5}}]}],
 actions:[{type:'stretch',targets:['plate'],min:[50,-1,-1],max:[101,61,1],x:'Width - 100'},
 {type:'stretch',targets:['plate'],min:[-1,31,-1],max:[301,61,1],y:'Height - 60'},
 {type:'scale',targets:['hole'],factor:'HoleRadius / 3',center:[80,30,0]},
 {type:'move',targets:['hole'],x:'Width - 100',y:'(Height - 60) / 2'},
 {type:'array',targets:['mark'],columns:'Count',rows:1,dx:'Pitch',dy:0},
 {type:'visibility',parameter:'Detail',targets:['plate','hole','mark'],states:{Detailed:['plate','hole','mark'],Outline:['plate']}},
 {type:'rotate',targets:['plate','hole','mark','attribute:PART'],angle:'Rotation',center:[0,0,0]},
 {type:'flip',targets:['plate','hole','mark','attribute:PART'],parameter:'Flipped',normal:[1,0,0],origin:[0,0,0]}]}};
 doc.transaction('Create configurable plates',()=>{
  doc.production.blocks.push(b);
  doc.add('INSERT',{block:b.id,matrix:Array.from(K.Math.M.identity()),attributes:{},parameters:{}});
  doc.add('INSERT',{block:b.id,matrix:Array.from(K.Math.M.translation(180,0,0)),attributes:{PART:'WIDE ${Width}'},parameters:{Width:160,Height:90,Fastener:'M10',Count:6}});
 });return doc;
}
D.demo=demo;
K.installDynamicUI=function(App){
 const originalRun=App.prototype.run,originalInspector=App.prototype.refreshInspector;
 const fields=(app,b,e,prefix='db_')=>{
  const state=D.resolve(b,e.parameters||{});
  return b.dynamic.parameters.map(p=>{
   const key=prefix+p.name,value=state.values[p.name],disabled=state.driven.has(p.name)?' disabled':'';
   if(p.type==='enum')return select(key,p.name,p.values.map(v=>[v,v]),value);
   if(p.type==='boolean')return `<label class="form-check"><input name="${key}" type="checkbox"${value?' checked':''}${disabled}>${esc(p.name)}</label>`;
   return app.field(key,p.name+(state.driven.has(p.name)?' (driven)':''),value,'number',`step="${p.step||'any'}"${p.min!=null?' min="'+p.min+'"':''}${p.max!=null?' max="'+p.max+'"':''}${disabled}`);
  }).join('');
 };
 App.prototype.refreshInspector=function(){
  originalInspector.call(this);const selected=this.doc.selected(true);if(selected.length!==1||selected[0].type!=='INSERT')return;
  const e=selected[0],b=P.ensure(this.doc).blocks.find(b=>b.id===e.block);if(!b?.dynamic)return;
  const section=document.createElement('section');section.className='property-section';section.id='dynamic-block-inspector';
  section.innerHTML='<h3>Block parameters</h3>'+fields(this,b,e)+'<button type="button" class="button secondary" data-action="dynamic-properties">All block properties</button>';
  section.addEventListener('change',event=>{const input=event.target;if(!input.name?.startsWith('db_'))return;
   const name=input.name.slice(3),p=b.dynamic.parameters.find(p=>p.name===name);if(!p)return;
   try{D.setValues(this.doc,e.id,{[name]:p.type==='boolean'?input.checked:p.type==='enum'?input.value:Number(input.value)});}
   catch(error){this.fail(error);this.refreshInspector();}
  });document.getElementById('inspector').append(section);
 };
 App.prototype.run=async function(id,...args){
  if(!commands.some(c=>c[0]===id))return originalRun.call(this,id,...args);
  const doc=this.doc,app=this,field=(...args)=>app.field(...args);
  try{
   if(id==='dynamic-demo'){app.addDocument(demo());app.setView('top');app.setRibbon('Blocks');app.fit(false);return;}
   const selected=doc.selected(true);if(selected.length!==1||selected[0].type!=='INSERT')throw Error('Select one editable block instance.');
   const e=selected[0],b=P.ensure(doc).blocks.find(b=>b.id===e.block);if(!b)throw Error('Missing block definition.');
   const revision=doc.revision,guard=()=>{if(app.doc!==doc||doc.revision!==revision)throw Error('The drawing changed while this dialog was open. Reopen the block tool.');};
   if(id==='dynamic-definition'){
    this.dialog({title:'Dynamic block definition — '+b.name,wide:true,html:'<p>Parameters control ordered actions in definition-local coordinates. Geometry is recomputed from the unchanged definition for each instance. Edit geometry with BEDIT; use BACTION for the action form.</p>'+json('definition',b.dynamic||starter(b),'Native dynamic schema')+'<details><summary>Available geometry targets</summary><pre>'+esc(b.entities.map(x=>x.id+' — '+x.type).concat((b.attributes||[]).map(a=>'attribute:'+a.tag)).join('\n'))+'</pre></details><div class="dialog-note">Native .kcad retains this behavior. DXF exports evaluated static block variants, not Autodesk dynamic action objects.</div>',onSubmit:f=>{guard();D.setDefinition(doc,b.id,JSON.parse(f.definition));}});return;
   }
   if(!b.dynamic)throw Error('Use Dynamic definition to configure this block first.');
   if(id==='dynamic-properties'){
    this.dialog({title:'Block parameters — '+b.name,wide:true,html:'<p>These values affect only the selected instance. Driven values come from expressions or lookup tables.</p><div class="form-grid">'+fields(app,b,e)+'</div>',onSubmit:f=>{guard();const state=D.resolve(b,e.parameters||{}),values={};for(const p of b.dynamic.parameters)if(!state.driven.has(p.name))values[p.name]=p.type==='boolean'?!!f['db_'+p.name]:p.type==='enum'?f['db_'+p.name]:Number(f['db_'+p.name]);D.setValues(doc,e.id,values,true);}});return;
   }
   if(id==='dynamic-lookup') {
    const tables=b.dynamic.lookups||[];
    if(!tables.length) throw Error('This block has no lookup table. Add one in Dynamic definition.');
    const state=D.resolve(b,e.parameters||{});
    const tableFields=t=>Object.keys(t.rows[0].set).map(name=>{
     const p=b.dynamic.parameters.find(p=>p.name===name),key='lookupOut_'+name,value=state.values[name];
     return p.type==='boolean'?`<label class="form-check"><input name="${key}" type="checkbox"${value?' checked':''}>${esc(name)}</label>`:field(key,name,value,'number','step="any" required');
    }).join('');
    const tablePreview=t=>'<table class="data-table"><thead><tr><th>'+esc(t.parameter)+'</th>'+Object.keys(t.rows[0].set).map(k=>'<th>'+esc(k)+'</th>').join('')+'</tr></thead><tbody>'+t.rows.map(r=>'<tr><td>'+esc(r.value)+'</td>'+Object.keys(t.rows[0].set).map(k=>'<td>'+esc(String(r.set[k]))+'</td>').join('')+'</tr>').join('')+'</tbody></table>';
    this.dialog({title:'Match lookup properties — '+b.name,wide:true,html:'<p>Choose the unique table row matching these output values. This updates the lookup selector and all its dependent geometry together; unmatched or ambiguous values leave the drawing unchanged.</p><div class="form-grid">'+select('lookup','Lookup table',tables.map(t=>[t.parameter,t.parameter]),tables[0].parameter)+'</div><div id="lookup-match-fields" class="form-grid">'+tableFields(tables[0])+'</div><details><summary>Available rows</summary><div id="lookup-match-rows">'+tablePreview(tables[0])+'</div></details>',onSubmit:f=>{
     guard();const t=tables.find(t=>t.parameter===f.lookup);if(!t)throw Error('Missing lookup table.');
     const criteria={};for(const key of Object.keys(t.rows[0].set)){
      const p=b.dynamic.parameters.find(p=>p.name===key);
      if(p.type!=='boolean'&&String(f['lookupOut_'+key]??'').trim()==='')throw Error('Enter '+key+'.');
      criteria[key]=p.type==='boolean'?!!f['lookupOut_'+key]:Number(f['lookupOut_'+key]);
     }
     D.setLookupValues(doc,e.id,t.parameter,criteria);
    }});
    document.querySelector('#modal [name="lookup"]').addEventListener('change',event=>{
     const t=tables.find(t=>t.parameter===event.target.value);if(!t)return;
     document.getElementById('lookup-match-fields').innerHTML=tableFields(t);
     document.getElementById('lookup-match-rows').innerHTML=tablePreview(t);
    });return;
   }
   if(id==='dynamic-reset'){this.dialog({title:'Reset block parameters?',html:'<p>Restore this instance’s default parameters. Its position, attributes and other instances are retained. Undo restores the values.</p>',onSubmit:()=>{guard();D.setValues(doc,e.id,{},true);}});return;}
   const targetRows=[...b.entities.map(x=>[x.id,x.type+' — '+x.id]),...(b.attributes||[]).map(a=>['attribute:'+a.tag,'Attribute '+a.tag])];
   this.dialog({title:'Add dynamic action',wide:true,html:'<p>The action is appended to this definition and affects every instance. Translation fields are offsets from definition geometry, rotations use degrees, stretch windows use local XYZ.</p><div class="form-grid">'+select('type','Action',D.ACTION_TYPES.filter(t=>t!=='visibility').map(t=>[t,t]),'move')+field('x','Move/stretch X expression','0','text')+field('y','Move/stretch Y expression','0','text')+field('z','Move/stretch Z expression','0','text')+field('angle','Rotation angle expression','0','text')+field('length','Polar stretch final-length expression','100','text')+field('baseLength','Polar stretch baseline length',100,'number','min=0.0000001 step=any')+field('direction','Polar stretch reference direction XYZ','1,0,0','text')+field('multiplier','Polar stretch distance multiplier','1','text')+field('count','Polar item-count expression','4','text')+field('fill','Polar fill-angle expression','360','text')+field('base','Polar shared base XYZ (when not rotating)','0,0,0','text')+'<label class="form-check"><input name="rotateItems" type="checkbox" checked>Rotate polar items</label>'+field('factor','Scale factor expression','1','text')+field('center','Center / flip origin XYZ','0,0,0','text')+field('normal','Rotation axis / flip normal XYZ','0,0,1','text')+select('parameter','Boolean flip parameter',b.dynamic.parameters.filter(p=>p.type==='boolean').map(p=>[p.name,p.name]))+field('min','Stretch window minimum XYZ','0,0,0','text')+field('max','Stretch window maximum XYZ','100,100,0','text')+field('columns','Array columns expression','2','text')+field('rows','Array rows expression','1','text')+field('dx','Array X spacing expression','20','text')+field('dy','Array Y spacing expression','20','text')+'</div><h3>Target geometry</h3>'+targetRows.map(([target,label],i)=>`<label class="form-check"><input type="checkbox" name="target${i}" checked>${esc(label)}</label>`).join('')+'<details><summary>Polar stretch member modes</summary><p>Frame: stretch eligible vertices or move insertion anchors/fully enclosed conics. Move whole: translate the entire member, regardless of frame (required for native solids). Rotate only: do not change its reach. All targeted members rotate.</p>'+targetRows.map(([target,label],i)=>select('mode'+i,label,[['frame','Use stretch frame'],['move','Move whole'],['rotate','Rotate only']],'frame')).join('')+'</details>',onSubmit:f=>{
    guard();const xyz=s=>P.point(String(s).split(',').map(Number));const a={type:f.type,targets:targetRows.filter((_,i)=>f['target'+i]).map(r=>r[0])};
    if(['move','stretch'].includes(a.type))Object.assign(a,{x:f.x,y:f.y,z:f.z});
    if(a.type==='stretch')Object.assign(a,{min:xyz(f.min),max:xyz(f.max)});
    if(a.type==='rotate')Object.assign(a,{angle:f.angle,center:xyz(f.center),axis:xyz(f.normal)});
    if(a.type==='polar-stretch')Object.assign(a,{length:f.length,baseLength:Number(f.baseLength),angle:f.angle,multiplier:f.multiplier,center:xyz(f.center),axis:xyz(f.normal),direction:xyz(f.direction),min:xyz(f.min),max:xyz(f.max),rotateOnly:targetRows.filter((r,i)=>f['target'+i]&&f['mode'+i]==='rotate').map(r=>r[0]),moveOnly:targetRows.filter((r,i)=>f['target'+i]&&f['mode'+i]==='move').map(r=>r[0])});
    if(a.type==='scale')Object.assign(a,{factor:f.factor,center:xyz(f.center)});
    if(a.type==='flip')Object.assign(a,{parameter:f.parameter,origin:xyz(f.center),normal:xyz(f.normal)});
    if(a.type==='array')Object.assign(a,{columns:f.columns,rows:f.rows,dx:f.dx,dy:f.dy});
    if(a.type==='polar-array')Object.assign(a,{count:f.count,angle:f.fill,center:xyz(f.center),axis:xyz(f.normal),rotateItems:!!f.rotateItems,...(!f.rotateItems?{base:xyz(f.base)}:{})});
    const definition=K.clone(b.dynamic);definition.actions.push(a);D.setDefinition(doc,b.id,definition);
   }});
  }catch(error){this.fail(error);}
 };
 const oldIO=App.prototype.io;
 App.prototype.io=function(operation,...args){if(operation==='write-dxf'&&this.doc.production.blocks.some(b=>b.dynamic))this.log('DXF','Dynamic blocks are exported as evaluated static variants. Save .kcad to retain parameters and actions.');return oldIO.call(this,operation,...args);};
};
})(window);
