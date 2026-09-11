/* Planar constraint authoring, named parameters and explicit solver diagnostics. */
(function(root){
'use strict';
const K=root.Kestrel,C=K.Constraints,E=K.Expressions,P=K.Production,U=K.UI,esc=U.escape;
const cmds=[['constraint-add','Add constraint','GEOMCONSTRAINT','ortho'],['constraint-manage','Manage constraints','CONSTRAINTS','properties'],['parameter-edit','Parameters','PARAMETERS','table'],['constraint-solve','Solve sketch','SOLVE','snap'],['constraint-toggle','Enable / disable solving','CONSTRAINTMODE','lock'],['constraint-hints','Constraint bars','SHOWCONSTRAINTS','text'],['constraint-clear','Remove constraints','DELCONSTRAINT','delete'],['constraint-demo','Parametric rectangle','PARAMDEMO','rectangle']];
for(const[id,label,alias,icon]of cmds)U.commands.push({id,label,alias,icon,description:label+' — planar sketch parameters'});
for(const type of ['horizontal','vertical','coincident','parallel','perpendicular','tangent','concentric','equal','fixed'])U.commands.push({id:'gc-'+type,label:type[0].toUpperCase()+type.slice(1),alias:'GC'+type.replace('-','').toUpperCase(),icon:'ortho',description:'Create '+type+' geometric constraint'});
U.groups.Parametric=[{name:'Geometric',columns:[['gc-horizontal','gc-vertical','gc-coincident'],['gc-parallel','gc-perpendicular','gc-tangent'],['gc-concentric','gc-equal','gc-fixed']]},{name:'Dimensions and parameters',large:['constraint-add','parameter-edit']},{name:'Solve and inspect',large:['constraint-solve'],columns:[['constraint-manage','constraint-toggle','constraint-clear'],['constraint-hints']]},{name:'Example',large:['constraint-demo']}];
function select(key,label,rows,value){return `<div class="form-field"><label>${esc(label)}</label><select name="${key}">${rows.map(([v,t])=>`<option value="${esc(v)}"${String(v)===String(value)?' selected':''}>${esc(t)}</option>`).join('')}</select></div>`;}
const notice='<div class="dialog-note">Lines, straight polylines, points, circles and arcs on one saved sketch plane. Point anchors: start, end, mid, center, position, or a zero-based vertex index. Segment indices are zero-based. Tangency to an arc uses its underlying circle. Failed edits roll back.</div>';
function reportHTML(r){return `<table class="report-table"><tr><td>Status</td><td>${esc(r.status)}</td></tr><tr><td>Participating variables / equations</td><td>${r.variables} / ${r.equations}</td></tr><tr><td>Numerical rank / degrees of freedom</td><td>${r.rank} / ${r.degreesOfFreedom}</td></tr><tr><td>Redundant equations</td><td>${r.redundantEquations}</td></tr><tr><td>Maximum normalized residual</td><td>${r.maxResidual??0}</td></tr></table><p>Rank describes the local Jacobian, not a proof of global constraint independence. Unreferenced geometry is not included. Failure to converge is not a proof that a sketch has no solution.</p>`;}
K.installConstraintsUI=function(App){

 const draw=App.prototype.drawOverlay;
 App.prototype.drawOverlay=function(ctx){
  draw.call(this,ctx);const st=C.state(this.doc);if(!st||this.constraintHints===false||this.sheet)return;
  const anchor=(r,segment=false)=>{const e=this.doc.byId.get(r.entity);if(!e||!this.doc.visible(e))return null;
   if(e.center){if(e.type==='ARC'&&['start','end','mid'].includes(r.point))return K.Geo.conicPoint(e,r.point==='end'?e.endAngle:r.point==='mid'?(e.startAngle||0)+K.Math.sweep(e.startAngle||0,e.endAngle)/2:e.startAngle||0);return e.center;}
   if(e.position)return e.position;const i=r.segment||0;if(segment||r.point==='mid')return K.Math.V.lerp(e.points[i],e.points[(i+1)%e.points.length],.5);return e.points[r.point==='end'?e.points.length-1:Number.isInteger(r.point)?r.point:0];
  };
  let values;try{values=E.parameters(st.parameters);}catch(_){return;}
  const cells=new Map();ctx.save();ctx.font='11px "Segoe UI",sans-serif';ctx.textBaseline='middle';ctx.setLineDash([]);
  for(const c of st.constraints.slice(0,128)){
   if(c.suppressed)continue;const a=anchor(c.a,['horizontal','vertical','parallel','perpendicular','collinear','equal','angle'].includes(c.type));if(!a)continue;const b=c.b?anchor(c.b):null,p=this.camera.project(b?K.Math.V.lerp(a,b,.5):a);if(p[0]<0||p[1]<0||p[0]>this.renderer.width||p[1]>this.renderer.height)continue;
   const k=Math.round(p[0]/28)+':'+Math.round(p[1]/28),n=cells.get(k)||0;cells.set(k,n+1);let text=({horizontal:'H',vertical:'V',fixed:'FIX', 'fix-entity':'FIX',coincident:'●',parallel:'∥',perpendicular:'⊥',equal:'=',concentric:'◎',tangent:'T'})[c.type]||c.type;
   if(['distance','distance-x','distance-y','radius','diameter','angle'].includes(c.type)){try{const value=E.evaluate(c.value,name=>values.get(name));text=String(c.value)+' = '+Number(value.toFixed(4))+(c.type==='angle'?'°':' '+(st.units||this.doc.units));}catch(_){text='Invalid expression';}}
   const w=ctx.measureText(text).width+12,x=Math.max(2,Math.min(this.renderer.width-w-2,p[0]+12)),y=Math.max(10,Math.min(this.renderer.height-12,p[1]-18-n*22));
   ctx.fillStyle=this.theme==='light'?'#e1f2f6':'#152d3a';ctx.fillRect(x,y-9,w,18);ctx.strokeStyle=st.enabled?'#54bfce':'#88929d';ctx.strokeRect(x,y-9,w,18);ctx.fillStyle=this.theme==='light'?'#164a57':'#c2eff5';ctx.fillText(text,x+6,y);
  }ctx.restore();
 };
 const prior=App.prototype.run;
 App.prototype.run=async function(id){
  if(!cmds.some(c=>c[0]===id)&&!id.startsWith('gc-'))return prior.call(this,id);
  this.cancel(false);const app=this,doc=this.doc,field=(...a)=>this.field(...a),s=C.state(doc),sel=doc.selected(true);
  if(id==='constraint-add'||id.startsWith('gc-')){
   const ents=doc.entities.filter(e=>['LINE','POLYLINE','POINT','CIRCLE','ARC'].includes(e.type));if(!ents.length)throw Error('Draw sketch geometry first.');
   const labels=ents.map((e,i)=>[e.id,`${i+1}. ${e.type} — ${e.id}`]),initial=id.startsWith('gc-')?id.slice(3):'distance';
   const refs=['a','b','c'].map((k,i)=>{const e=sel[i]||sel[0]||ents[i]||ents[0],anchor=e.center?'center':e.position?'position':i===1?'end':'start';return `<fieldset><legend>${['First','Second','Symmetry axis'][i]} reference</legend><div class="form-grid">${select(k,'Entity',labels,e.id)}${field(k+'Point','Point anchor',anchor,'text')}${field(k+'Segment','Segment index',0,'number','min="0" step="1"')}</div></fieldset>`;}).join('');
   this.dialog({title:'Add sketch constraint',wide:true,html:notice+`<p>Length expressions use <strong>${esc(s?.units||doc.units)}</strong>; angles use degrees. The plane is captured from UCS when the first constraint is added.</p>`+select('type','Constraint type',Object.keys(C.TYPES).map(t=>[t,t]),initial)+refs+field('value','Dimension expression (for distance, radius, diameter or angle)','20','text','maxlength="512"')+'<label class="form-check"><input type="checkbox" name="internal">Internal circle–circle tangency</label>',onSubmit:f=>{const c={type:f.type,value:f.value,internal:!!f.internal};for(let i=0;i<C.TYPES[c.type];i++){const k=['a','b','c'][i],e=doc.byId.get(f[k]);let anchor=f[k+'Point'].trim();if(/^\d+$/.test(anchor))anchor=Number(anchor);c[k]={entity:f[k],point:anchor};if(e.points)c[k].segment=Number(f[k+'Segment']);}C.add(doc,c);app.toast('Sketch solved: '+doc.constraintReport.status+'; '+doc.constraintReport.degreesOfFreedom+' degrees of freedom.');}});return;
  }
  if(id==='parameter-edit'){
   const rows=s?.parameters||[],values=E.parameters(rows);
   this.dialog({title:'Named parameters',wide:true,html:`<p>Length constraints interpret values in <strong>${esc(s?.units||doc.units)}</strong>. Use dependent expressions such as <code>Width / 2</code>. Trigonometric functions use radians; <code>deg</code> converts degrees.</p><table class="report-table"><thead><tr><th>Name</th><th>Expression</th><th>Value</th><th>Delete</th></tr></thead><tbody>${rows.map((r,i)=>`<tr><td>${esc(r.name)}</td><td><input name="expression${i}" value="${esc(r.expression)}" maxlength="512" aria-label="Expression for ${esc(r.name)}"></td><td>${values.get(r.name)}</td><td><input type="checkbox" name="remove${i}" aria-label="Delete ${esc(r.name)}"></td></tr>`).join('')}</tbody></table><div class="form-grid">${field('name','New parameter name','','text','maxlength="48"')}${field('expression','New parameter expression','100','text','maxlength="512"')}</div><div class="dialog-note">Deleting a referenced parameter, introducing a cycle, or making the sketch inconsistent rejects the whole edit. Saving a .kcad file preserves parameters and constraints.</div>`,onSubmit:f=>doc.transaction('Edit sketch parameters',()=>{const st=C.state(doc,true);st.parameters=rows.filter((r,i)=>!f['remove'+i]).map(r=>({name:r.name,expression:f['expression'+rows.indexOf(r)]}));if(f.name.trim())st.parameters.push({name:f.name.trim(),expression:f.expression});E.parameters(st.parameters);})});return;
  }
  if(id==='constraint-manage'){
   const rows=s?.constraints||[];if(!rows.length){app.toast('No sketch constraints.');return;}
   this.dialog({title:'Constraint manager',wide:true,html:notice+`<p>Solving is ${s.enabled?'enabled':'disabled'}. Values use ${esc(s.units||doc.units)} and degrees.</p><table class="report-table"><thead><tr><th>Constraint / reference</th><th>Expression</th><th>Suppress</th><th>Delete</th></tr></thead><tbody>${rows.map((c,i)=>`<tr><td>${esc(c.type)}<br><small>${esc(c.a.entity)}${c.b?' → '+esc(c.b.entity):''}</small></td><td>${['distance','distance-x','distance-y','radius','diameter','angle'].includes(c.type)?`<input name="value${i}" value="${esc(c.value)}" maxlength="512" aria-label="Dimension ${i+1}">`:'—'}</td><td><input type="checkbox" name="suppress${i}" ${c.suppressed?'checked':''} aria-label="Suppress ${i+1}"></td><td><input type="checkbox" name="delete${i}" aria-label="Delete ${i+1}"></td></tr>`).join('')}</tbody></table>`,onSubmit:f=>doc.transaction('Edit sketch constraints',()=>{C.state(doc,true).constraints=rows.filter((c,i)=>!f['delete'+i]).map(c=>{const i=rows.indexOf(c),r={...c,suppressed:!!f['suppress'+i]};if(f['value'+i]!=null)r.value=f['value'+i];return r;});})});return;
  }
  if(id==='constraint-solve'){
   let r;doc.transaction('Solve constrained sketch',()=>{r=C.solve(doc);if(!r.converged)throw Error('Sketch did not converge; no geometry was changed. Conflicting residuals: '+r.conflicts.map(c=>c.type).join(', '));});
   this.dialog({title:'Sketch solver report',html:reportHTML(r),closeOnly:true,submit:'Close'});return;
  }
  if(id==='constraint-hints'){this.constraintHints=this.constraintHints===false;this.invalidate();return;}
  if(id==='constraint-toggle'){doc.transaction('Toggle constraint solving',()=>{const st=C.state(doc,true);st.enabled=!st.enabled;});app.toast(C.state(doc).enabled?'Constraint solving enabled.':'Constraint solving disabled; geometry can be edited freely.');return;}
  if(id==='constraint-clear'){this.dialog({title:'Remove all sketch constraints?',html:'<p>Geometry is retained. All sketch constraints and parameters will be removed. Undo restores them.</p>',submit:'Remove constraints',onSubmit:()=>doc.transaction('Remove sketch constraints',()=>{delete P.ensure(doc).parametric;})});return;}
  if(id==='constraint-demo'){
   const d=new K.Drawing('Parametric bracket profile');d.currentLayer=d.layers[0].id;
   d.transaction('Create parametric rectangle',()=>{const e=d.add({type:'POLYLINE',closed:true,points:[[0,0,0],[100,0,0],[100,60,0],[0,60,0]]});const st=C.state(d,true);st.parameters=[{name:'Width',expression:'100'},{name:'Height',expression:'Width * 0.6'}];const ref=(p,segment=0)=>({entity:e.id,point:p,segment});let k=0;const add=(type,a,b,value,target)=>st.constraints.push({id:'demo'+(++k),type,a,...(b?{b}:{}),...(value!=null?{value}:{}),...(target?{target}:{})});add('fixed',ref(0),null,null,[0,0]);add('horizontal',ref(0,0));add('vertical',ref(1,1));add('horizontal',ref(2,2));add('vertical',ref(3,3));add('distance-x',ref(0),ref(1),'Width');add('distance-y',ref(0),ref(3),'Height');});
   app.addDocument(d);app.setView('top');app.fit(false);app.toast('Change Width in Parameters; Height follows its expression.');return;
  }
 };
};
})(window);
