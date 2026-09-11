/* 3D constraint authoring, named dimensions and rigid datum mating. */
(function(root){
'use strict';const K=root.Kestrel,C=K.SpatialConstraints,U=K.UI,esc=U.escape,{V,M}=K.Math;
const commands=[['spatial-joints','Joint limits / drive','JOINTLIMITS','settings'],['spatial-add','3D constraint','3DCONSTRAINT','snap'],['spatial-mate','Assembly mate','3DMATE','link'],['spatial-distance','3D driving dimension','3DDIM','dimension'],['spatial-fix','Fix selected bodies','3DFIX','lock'],['spatial-manage','Constraints','3DCONSTRAINTS','properties'],['spatial-parameters','3D parameters','3DPARAMETERS','settings'],['spatial-solve','Solve in 3D','3DSOLVE','check'],['spatial-report','3D degrees of freedom','3DDOF','info'],['spatial-example','3D assembly example','3DASSEMBLY','box']];
for(const[id,label,alias,icon]of commands)U.commands.push({id,label,alias,icon,description:label+' — native browser free-space geometry and rigid poses'});
U.groups.Assembly=[{name:'Spatial constraints',large:['spatial-add','spatial-mate'],columns:[['spatial-distance','spatial-fix','spatial-solve']]},{name:'Design',large:['spatial-parameters','spatial-manage'],columns:[['spatial-joints','spatial-report','spatial-example']]}];
const menu=(key,label,items,value)=>`<div class="form-field"><label>${esc(label)}</label><select name="${key}">${items.map(([v,t])=>`<option value="${esc(v)}"${String(v)===String(value)?' selected':''}>${esc(t)}</option>`).join('')}</select></div>`;
function xyz(s){const v=String(s).split(',').map(x=>Number(x.trim()));if(v.length!==3||!v.every(Number.isFinite))throw Error('Enter three finite comma-separated coordinates.');return v;}
const supported=e=>C.RIGID.has(e.type)||C.FLEX.has(e.type);
function initialReference(e){return C.RIGID.has(e.type)?{entity:e.id,local:[0,0,0]}:{entity:e.id,point:e.type==='POINT'?'position':'start'};}
function demo(){
 const d=new K.Drawing('Spatial lift assembly');d.currentLayer='0';const base=d.add({...K.Geo.box([-40,-25,0],80,50,10),color:'#52798f'}),carrier=d.add({...K.Geo.box([-12,-10,0],24,20,12),color:'#e9ae55'});
 d.transaction('Define rigid slider assembly',()=>{const s=C.state(d,true);s.parameters=[{name:'Lift',expression:'25'}];s.constraints=[
  {id:K.uid('3d'),type:'fixed-entity',a:initialReference(base),target:Array.from(M.identity())},
  {id:K.uid('3d'),type:'slider',a:initialReference(carrier),b:{entity:base.id,local:[0,0,10]}},
  {id:K.uid('3d'),type:'distance-z',a:{entity:base.id,local:[0,0,10]},b:initialReference(carrier),value:'Lift'}
 ];});d.selection=new Set([carrier.id]);return d;
}
K.SpatialDemo=demo;
K.installSpatialUI=function(App){const run=App.prototype.run,inspector=App.prototype.refreshInspector;
 App.prototype.spatialDialog=function(kind='coincident'){
  const app=this,doc=this.doc,revision=doc.revision,field=(...a)=>this.field(...a),selected=doc.selected(true).filter(supported),all=doc.entities.filter(supported),entities=[['world','World datum'],...all.map(e=>[e.id,e.type+' · '+e.id])];if(!all.length)throw Error('Draw points/lines or create mesh/native bodies or blocks first.');
  const rows=['a','b','c'].map((k,i)=>`<fieldset id="spatial-ref-${k}"><legend>Reference ${k.toUpperCase()}</legend><div class="form-grid">${menu(k+'Entity','Drawing object / datum',entities,selected[i]?.id||(i===0?all[0].id:'world'))}${field(k+'Point','Point anchor: start / end / mid / index / vertex:index',selected[i]?.type==='POINT'?'position':'start','text')}${field(k+'Segment','Polyline segment index',0,'number','min="0" step="1"')}${field(k+'Local','Body-local XYZ or world datum XYZ','0,0,0','text')}${field(k+'Axis','Axis XYZ (blank = segment direction or body/world Z)','','text')}${field(k+'XAxis','Frame X axis (fastened / slider)','1,0,0','text')}</div></fieldset>`).join('');
  this.dialog({title:'Create 3D constraint / mate',wide:true,html:`<p>Flexible points and lines use true world XYZ. Meshes, native bodies and INSERTs move as rigid bodies. Body datums are local to their retained frame; world datums are independent of the active UCS. Angles use degrees.</p><div class="form-grid">${menu('kind','Relationship',Object.keys(C.TYPES).map(x=>[x,x]),kind)}${field('value','Dimension / parameter expression',kind==='angle'?90:kind==='length-ratio'?1:0,'text')}</div>${rows}`,onSubmit:f=>{
   if(this.doc!==doc||doc.revision!==revision)throw Error('Drawing changed; reopen the 3D constraint dialog.');
   const type=f.kind,spec={type};for(const k of ['a','b','c'].slice(0,C.TYPES[type])){const e=doc.byId.get(f[k+'Entity']);let r;
    if(f[k+'Entity']==='world')r={world:xyz(f[k+'Local'])};else{if(!e||!doc.editable(e)&&!doc.layer(e).locked)throw Error('Reference is no longer available.');r={entity:e.id};
     if(C.RIGID.has(e.type)){const m=/^vertex:(\d+)$/.exec(String(f[k+'Point']).trim());if(m)r.vertex=Number(m[1]);else r.local=xyz(f[k+'Local']);}
     else{r.point=e.type==='POINT'?'position':/^\d+$/.test(f[k+'Point'])?Number(f[k+'Point']):f[k+'Point'];if(e.points)r.segment=Number(f[k+'Segment']);}
    }if(f[k+'Axis'].trim())r.axis=xyz(f[k+'Axis']);if(f[k+'XAxis'].trim())r.xaxis=xyz(f[k+'XAxis']);spec[k]=r;
   }if(C.DIM.has(type))spec.value=f.value;C.add(doc,spec);app.selectionChanged();app.toast('3D constraint solved: '+doc.spatialReport.degreesOfFreedom+' remaining local DOF');
  }});
  const select=document.querySelector('#modal [name="kind"]');function show(){const n=C.TYPES[select.value];['a','b','c'].forEach((k,i)=>document.getElementById('spatial-ref-'+k).hidden=i>=n);document.querySelector('#modal [name="value"]').closest('.form-field').hidden=!C.DIM.has(select.value);}select.addEventListener('change',show);show();
 };
 App.prototype.run=async function(id,...args){id=['DRIVEJOINT','JOINTS'].includes(String(id).toUpperCase())?'spatial-joints':commands.find(c=>c[2]===String(id).toUpperCase())?.[0]||id;if(!commands.some(c=>c[0]===id))return run.call(this,id,...args);
  try{const doc=this.doc,revision=doc.revision,field=(...a)=>this.field(...a),fresh=()=>{if(this.doc!==doc||doc.revision!==revision)throw Error('Drawing changed; reopen 3D editing.');};
   if(id==='spatial-example'){const d=demo();this.addDocument(d);d.selection=new Set([d.entities[1].id]);this.camera.setView('iso');this.setStyle('shaded-edges');this.selectionChanged();this.fit();return;}
   if(['spatial-add','spatial-mate','spatial-distance'].includes(id))return this.spatialDialog(id==='spatial-mate'?'plane-mate':id==='spatial-distance'?'distance':'coincident');
   if(id==='spatial-fix'){const selected=doc.selected(true);if(!selected.length||selected.some(e=>!supported(e)))throw Error('Select editable points, straight lines, meshes or blocks.');doc.transaction('Fix 3D selection',()=>{const s=C.state(doc,true);for(const e of selected)s.constraints.push({id:K.uid('spatial'),type:'fixed-entity',a:initialReference(e),target:C.RIGID.has(e.type)?Array.from(C.frame(e)):K.clone((e.points||[e.position]).flat())});});this.selectionChanged();return;}
   if(id==='spatial-solve'){doc.transaction('Solve 3D constraints',()=>C.enforce(doc));this.selectionChanged();this.toast(doc.spatialReport?.status||'No active 3D equations.');return;}
   if(id==='spatial-joints'){
    const s=C.state(doc),joints=(s?.constraints||[]).filter(c=>C.JOINT.has(c.type));
    if(!joints.length)throw Error('Create a hinge or slider mate using Assembly mate first.');
    const positions=C.solve(doc,{apply:false}).joints||[];
    this.dialog({title:'Joint travel limits and driving positions',wide:true,html:`<p>Slider travel is A minus B along B's axis, in ${esc(s.units)} parameter units. Hinge rotation is signed about B's axis, in degrees; use an interval strictly inside (-180, 180). A blank bound is open. An enabled driver holds a position; turn it off to restore free motion within the limits.</p><p>Limits stop outward travel, not inward motion. Equal minimum and maximum lock the coordinate. No collision/contact simulation is implied.</p>${joints.map((c,i)=>{
     const l=c.limits||{},d=c.drive||{},r=positions.find(r=>r.id===c.id);
     return `<fieldset><legend>${esc(c.type)} · ${esc(c.id)}</legend><p>${r?`Measured: ${Number(r.coordinate).toPrecision(8)} ${esc(r.units)}${r.atMinimum?' · minimum stop':''}${r.atMaximum?' · maximum stop':''}`:'No travel settings yet.'}${c.suppressed?' · suppressed':''}</p><div class="form-grid"><label><input name="limit${i}" type="checkbox"${l.enabled?' checked':''}>Enable travel limits</label>${field('min'+i,'Minimum expression (blank = unbounded)',l.min??'','text')}${field('max'+i,'Maximum expression (blank = unbounded)',l.max??'','text')}<label><input name="drive${i}" type="checkbox"${d.enabled?' checked':''}>Enable position driver</label>${field('position'+i,'Driving position expression',d.value??'0','text')}</div></fieldset>`;
    }).join('')}`,onSubmit:f=>{fresh();doc.transaction('Edit joint limits and drives',()=>{
     for(let i=0;i<joints.length;i++){
      const c=C.state(doc).constraints.find(c=>c.id===joints[i].id),min=f['min'+i].trim(),max=f['max'+i].trim();
      if(!min&&!max){if(f['limit'+i])throw Error('Specify at least one bound before enabling limits.');delete c.limits;}
      else c.limits={enabled:!!f['limit'+i],...(min?{min}:{}),...(max?{max}:{})};
      c.drive={enabled:!!f['drive'+i],value:f['position'+i].trim()||'0'};
     }
    });this.selectionChanged();this.toast('Joint settings solved; out-of-range drives are rejected.');}});return;
   }
   if(id==='spatial-parameters'){const rows=C.state(doc)?.parameters||[];this.dialog({title:'3D driving parameters',wide:true,html:`<p>Named expressions support arithmetic and whitelisted mathematical functions. Length values use the saved parameter units; angles are degrees. Cycles and invalid dimensions roll back.</p><div class="form-field"><label>Parameters (JSON name/expression rows)</label><textarea name="parameters" rows="12" style="width:100%;font-family:monospace">${esc(JSON.stringify(rows,null,2))}</textarea></div>`,onSubmit:f=>{fresh();const rows=JSON.parse(f.parameters);K.Expressions.parameters(rows);doc.transaction('Edit 3D parameters',()=>C.state(doc,true).parameters=rows);this.selectionChanged();}});return;}
   if(id==='spatial-report'){const r=C.solve(doc,{apply:false});return void this.dialog({title:'Spatial degrees of freedom',wide:true,closeOnly:true,html:`<p>${esc(r.status)} · ${r.variables} free scalars · ${r.equations} equations · local rank ${r.rank} · <strong>${r.degreesOfFreedom} remaining DOF</strong></p><p>${r.redundantEquations} redundant equality components; ${r.inequalities||0} unilateral bounds. DOF counts bilateral equations; a limit stop may still allow inward travel. This numerical local diagnostic is not a proof of global satisfiability. Algebraic vector equations also contain redundant components.</p><pre style="white-space:pre-wrap">${esc(JSON.stringify(r,null,2))}</pre>`});}
   if(id==='spatial-manage'){const s=C.state(doc)||{enabled:true,constraints:[]};this.dialog({title:'Manage 3D constraints',wide:true,html:`<label><input name="enabled" type="checkbox"${s.enabled?' checked':''}>Enable the 3D solver</label><p>Check Delete to remove a relationship, or Suppress to retain it without driving geometry. Conflicting changes leave the drawing unchanged.</p><table class="report-table"><thead><tr><th>Relationship</th><th>Expression</th><th>Suppress</th><th>Delete</th></tr></thead><tbody>${s.constraints.map((c,i)=>`<tr><td>${esc(c.type)}<small> ${esc(c.id)}</small></td><td>${C.DIM.has(c.type)?`<input name="value${i}" value="${esc(c.value??0)}">`:'—'}</td><td><input name="suppress${i}" type="checkbox"${c.suppressed?' checked':''}></td><td><input name="delete${i}" type="checkbox"></td></tr>`).join('')}</tbody></table>`,onSubmit:f=>{fresh();doc.transaction('Edit 3D constraints',()=>{const dest=C.state(doc,true);dest.enabled=!!f.enabled;dest.constraints=dest.constraints.filter((c,i)=>{c.suppressed=!!f['suppress'+i];if(C.DIM.has(c.type))c.value=f['value'+i];return !f['delete'+i];});});this.selectionChanged();}});return;}
  }catch(e){this.fail(e);}
 };
 App.prototype.refreshInspector=function(...args){const out=inspector.apply(this,args),s=C.state(this.doc);if(s&&this.doc.selected().some(e=>s.constraints.some(c=>[c.a,c.b,c.c].some(r=>r?.entity===e.id)))){const panel=document.getElementById('inspector'),div=document.createElement('div');div.className='property-hint';div.textContent='Spatial constraints: '+(this.doc.spatialReport?.status||'saved equations')+'. Assembly → 3D degrees of freedom reports the current system.';panel.append(div);}return out;};
};
})(window);
