/* Native edge carriers become editable drafting curves; snapshots, never mesh fits. */
(function(root){
'use strict';
const K=root.Kestrel,N=K.Kernel,{V}=K.Math;
const finite=n=>typeof n==='number'&&Number.isFinite(n);
const xyz=p=>Array.isArray(p)&&p.length===3&&p.every(n=>finite(n)&&Math.abs(n)<=1e8);
function spline(e){
 const p=e.controlPoints,d=e.degree,k=e.knots,w=e.weights;
 if(!Array.isArray(p)||p.length<2||p.length>2000||!p.every(xyz)||!Number.isInteger(d)||d<1||d>Math.min(10,p.length-1))throw Error('Invalid native spline poles or degree.');
 if(!Array.isArray(k)||k.length!==p.length+d+1||!k.every((v,i)=>finite(v)&&(!i||v>=k[i-1]))||!(k.at(-1)>k[0]))throw Error('Invalid native spline knot sequence.');
 const span=k.at(-1)-k[0];if(!finite(span))throw Error('Spline parameter span overflow.');
 if(k.slice(0,d+1).some(v=>v!==k[0])||k.slice(-d-1).some(v=>v!==k.at(-1))||k[d+1]===k[0]||k[p.length-1]===k.at(-1))throw Error('Native splines require clamped nonperiodic knots.');
 let m=0,last;for(const v of k.slice(d+1,-d-1)){m=v===last?m+1:1;last=v;if(m>d)throw Error('Invalid interior spline multiplicity.');}
 if(w!==undefined&&(!Array.isArray(w)||w.length!==p.length||!w.every(v=>finite(v)&&v>0)||Math.min(...w)/Math.max(...w)<1e-12))throw Error('Invalid native spline weights.');
 return e;
}
function curve(e){
 const bad=()=>{throw Error('Invalid native drafting curve.');};
 const keys={LINE:['type','points'],CIRCLE:['type','center','normal','axisX','axisY','radius'],ARC:['type','center','normal','axisX','axisY','radius','startAngle','endAngle'],ELLIPSE:['type','center','normal','axisX','axisY','rx','ry','startAngle','endAngle'],SPLINE:['type','controlPoints','degree','knots','weights','closed']};
 if(!e||!keys[e.type]||Object.keys(e).some(k=>!keys[e.type].includes(k)))bad();
 if(e.type==='SPLINE'){spline(e);if(typeof e.closed!=='boolean'||e.closed&&V.dist(e.controlPoints[0],e.controlPoints.at(-1))>1e-6)bad();}
 else if(e.type==='LINE'){if(!Array.isArray(e.points)||e.points.length!==2||!e.points.every(xyz)||V.dist(...e.points)<=1e-12)bad();}
 else {
  if(![e.center,e.normal,e.axisX,e.axisY].every(xyz))bad();
  const rx=V.len(e.axisX),ry=V.len(e.axisY),norm=V.len(e.normal);
  if(rx<=0||ry<=0||Math.abs(norm-1)>1e-7||Math.abs(V.dot(e.axisX,e.axisY))>1e-8*rx*ry||V.dist(V.norm(V.cross(e.axisX,e.axisY)),e.normal)>1e-7)bad();
  const near=(a,b)=>finite(a)&&a>0&&Math.abs(a-b)<=1e-8*b;
  if(e.type==='ELLIPSE'?!near(e.rx,rx)||!near(e.ry,ry):!near(e.radius,rx)||!near(e.radius,ry))bad();
  if((e.startAngle===undefined)!==(e.endAngle===undefined))bad();
  if(e.type==='ARC'||e.endAngle!==undefined){if(!finite(e.startAngle)||!finite(e.endAngle)||e.endAngle<=e.startAngle||e.endAngle-e.startAngle>Math.PI*2+1e-10||Math.max(Math.abs(e.startAngle),Math.abs(e.endAngle))>1e12)bad();}
 }
 return K.clone(e);
}
function options(params={}){
 const mode=params.mode??'edges';if(!['edges','section'].includes(mode))throw Error('Choose edge extraction or plane section.');
 const out={mode};
 if(mode==='section'){
  if(!xyz(params.origin)||!xyz(params.normal)||V.len(params.normal)<1e-12)throw Error('Invalid section plane.');
  out.origin=params.origin.slice();out.normal=params.normal.slice();
 }
 if(params.edges!==undefined){if(mode!=='edges'||!Array.isArray(params.edges)||!params.edges.length||params.edges.length>2000||params.edges.some(i=>!Number.isInteger(i)||i<0||i>=20000)||new Set(params.edges).size!==params.edges.length)throw Error('Invalid current edge indices.');out.edges=params.edges.slice();}
 return out;
}
function prepare(doc,entities,params={}){
 if(!Array.isArray(entities)||!entities.length||entities.length>32||new Set(entities.map(e=>e.id)).size!==entities.length||entities.some(e=>doc.byId.get(e.id)!==e||!doc.visible(e)))throw Error('Select 1–32 distinct visible current native bodies.');
 const p=options(params);if(p.edges&&entities.length!==1)throw Error('Edge indices require one native body.');
 const inputs=entities.map(e=>K.clone(N.input(e))),revision=doc.revision;
 return {doc,params:p,inputs,guard(){if(doc.revision!==revision||entities.some(e=>doc.byId.get(e.id)!==e))throw Error('Drawing changed; rerun native curve extraction.');}};
}
function validate(result,session){
 const bad=()=>{throw Error('Invalid native curve response.');};
 if(!result||result.provider!=='OCCT'||result.schema!==1||result.operation!=='extract-curves'||result.mode!==session.params.mode||result.bodyCount!==session.inputs.length||!Array.isArray(result.curves)||result.curves.length>2000||result.curveCount!==result.curves.length||!Number.isInteger(result.degenerateEdges)||result.degenerateEdges<0||result.degenerateEdges>2000||result.curveCount+result.degenerateEdges>2000)bad();
 if(result.mode==='edges'&&result.curveCount+result.degenerateEdges!==(session.params.edges?.length??session.inputs.reduce((n,s)=>n+s.edges.length,0)))bad();
 const seen=new Set();let poles=0;
 return result.curves.map(row=>{if(!row||!Number.isInteger(row.sourceIndex)||row.sourceIndex<0||row.sourceIndex>=session.inputs.length||!Number.isInteger(row.edgeIndex)||row.edgeIndex<0||row.edgeIndex>=20000||result.mode==='edges'&&row.edgeIndex>=session.inputs[row.sourceIndex].edges.length)bad();
  const key=row.sourceIndex+':'+row.edgeIndex;if(seen.has(key)||session.params.edges&&!session.params.edges.includes(row.edgeIndex))bad();seen.add(key);
  const e=curve(row.entity);poles+=(e.controlPoints||[]).length;if(poles>20000)bad();return e;
 });
}
async function run(session){session.guard();const r=await N.request('extract-curves',session.inputs,session.params);session.guard();validate(r,session);return r;}
function retain(session,result){
 session.guard();const outputs=validate(result,session),d=session.doc,l=d.layerMap.get(d.currentLayer);
 if(!l?.visible||l.locked)throw Error('Choose an unlocked visible current layer for extracted curves.');
 if(!outputs.length)return [];
 let added;d.transaction('Extract native '+session.params.mode+' curves',()=>{added=outputs.map(e=>d.add(e));d.selection=new Set(added.map(e=>e.id));});return added;
}
// Existing circle/polyline profiles keep their public schema. Rational splines
// are transferred as control data, not fitted again through display samples.
const profile=N.profile;N.profile=function(e){
 if(e.type==='SPLINE'){
  const pts=e.controlPoints||e.points;if(!Array.isArray(pts))throw Error('Spline control points are required.');
  const degree=e.degree??Math.min(3,pts.length-1);if(!Number.isInteger(degree)||degree<1||degree>Math.min(10,pts.length-1))throw Error('Invalid spline profile degree.');
  const out={controlPoints:K.clone(pts),degree,knots:e.knots?.slice()||K.Geo.uniformKnots(pts.length,degree),...(e.weights?{weights:e.weights.slice()}:{})};spline(out);return {type:'spline',...out};
 }
 if(e.type==='ELLIPSE'&&e.endAngle!=null&&K.Math.sweep(e.startAngle||0,e.endAngle)<Math.PI*2-1e-10)throw Error('An open elliptical arc is not a closed native profile.');
 return profile(e);
};
K.NativeCurves={curve,spline,options,prepare,validate,run,retain};
})(typeof window!=='undefined'?window:globalThis);
