/* Native carriers to editable drawing geometry: source-preserving transactions. */
(function(root){
'use strict';const K=root.Kestrel,U=K.UI,C=K.NativeCurves,{M,V}=K.Math;
const commands=[['native-edges','Extract editable edges','XEDGES','copy'],['native-section-curves','Editable section curves','SECTIONCURVES','section']];
for(const [id,label,alias,icon]of commands)U.commands.push({id,label,alias,icon,description:label+' — native geometry, not display polylines'});
U.groups.Solids.push({name:'Drafting from solids',columns:[commands.map(c=>c[0])]});
K.installNativeCurvesUI=function(App){const run=App.prototype.run;
 App.prototype.run=async function(id,...args){id=commands.find(c=>c[2]===String(id).toUpperCase())?.[0]||id;if(!commands.some(c=>c[0]===id))return run.call(this,id,...args);
  try{
   this.cancel(false);const doc=this.doc,entities=doc.selected(false),revision=doc.revision,section=id==='native-section-curves',field=(...a)=>this.field(...a);
   C.prepare(doc,entities);const defaults='<p>Sources are retained unchanged. New independent LINE, ARC, CIRCLE, ELLIPSE and rational SPLINE geometry uses the current layer. Unsupported carriers reject instead of silently approximating.</p>';
   const html=section?field('origin','UCS plane origin XYZ','0,0,0','text')+field('normal','UCS plane normal XYZ','0,0,1','text'):field('indices','Edge indices (blank = all)','', 'text')+'<p>Explicit indices require one body. SOLIDINFO / native edge editing shows current topology; indices are not persistent face references.</p>';
   this.dialog({title:section?'Extract editable section curves':'Extract editable native edges',wide:true,html:defaults+html,submit:'Extract curves',onSubmit:async f=>{
    if(this.doc!==doc||doc.revision!==revision)throw Error('Drawing changed; reopen extraction.');
    const p={mode:section?'section':'edges'},active=document.querySelector(section?'#modal [name="origin"]':'#modal [name="indices"]');
    if(section){const frame=K.Production.frame(K.Production.ensure(doc).ucs),xyz=s=>K.Production.point(String(s).split(',').map(Number));p.origin=M.point(frame,xyz(f.origin));p.normal=M.point(frame,xyz(f.normal),0);}
    else if(f.indices.trim()){const tokens=f.indices.split(',').map(s=>s.trim());if(tokens.some(s=>!/^\d+$/.test(s)))throw Error('Enter comma-separated nonnegative edge indices.');p.edges=tokens.map(Number);}
    const session=C.prepare(doc,entities,p),result=await C.run(session);
    if(this.doc!==doc||!active.isConnected||!document.querySelector('#modal').open)throw Error('Extraction dialog changed; no geometry was applied.');
    const added=C.retain(session,result);this.selectionChanged();this.toast(added.length?`${added.length} editable curves; ${result.degenerateEdges} degenerate edges omitted.`:'The section produced no curve edges.');
   }});
  }catch(error){this.fail(error);}
 };
};
})(window);
