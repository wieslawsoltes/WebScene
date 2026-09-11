/* Kestrel CAD — free-space point/line and rigid-body constraints, original MIT implementation.
 * Solver residuals are true 3D, never projections into the active UCS. */
(function(root){
'use strict';
const K=root.Kestrel,{V,M}=K.Math,P=K.Production,E=K.Expressions,clone=K.clone;
const TYPES=Object.freeze({'coincident':2,'distance':2,'distance-x':2,'distance-y':2,'distance-z':2,
 'fixed-point':1,'fixed-entity':1,'parallel':2,'same-direction':2,'opposed':2,'perpendicular':2,'angle':2,
 'equal-length':2,'length-ratio':2,'point-on-line':2,'point-plane':2,'plane-mate':2,'coaxial':2,
 'midpoint':2,'symmetric':3,'hinge':2,'slider':2,'fastened':2});
const DIM=new Set(['distance','distance-x','distance-y','distance-z','point-plane','plane-mate','angle','length-ratio']);
const JOINT=new Set(['hinge','slider']);
const RIGID=new Set(['MESH','INSERT']),FLEX=new Set(['LINE','POLYLINE','POINT']);
const limits=Object.freeze({variables:192,scalars:768,constraints:256,parameters:128,iterations:100});
const own=(o,k)=>Object.prototype.hasOwnProperty.call(o,k);
const number=(n,label='number',max=1e12)=>{if(typeof n!=='number'||!Number.isFinite(n)||Math.abs(n)>max)throw Error('Invalid spatial '+label+'.');return n;};
const point=p=>{if(!Array.isArray(p)||p.length!==3)throw Error('A spatial point needs three coordinates.');p.forEach(n=>number(n,'coordinate'));return p;};
const axis=a=>{point(a);if(V.len(a)<1e-10)throw Error('A spatial direction must be nonzero.');return a;};
function matrix(m){if(!Array.isArray(m)||m.length!==16||!m.every(v=>Number.isFinite(v)&&Math.abs(v)<=1e12)||!M.inverse(m)||Math.abs(m[3])+Math.abs(m[7])+Math.abs(m[11])>1e-10||Math.abs(m[15]-1)>1e-10)throw Error('Invalid spatial affine frame.');return m;}
function state(doc,create=false){const p=P.ensure(doc);if(!p.spatial&&create)p.spatial={version:1,enabled:true,units:doc.units,parameters:[],constraints:[]};return p.spatial;}
const refs=c=>['a','b','c'].slice(0,TYPES[c.type]).map(k=>c[k]);
function entityValues(e){return (e.type==='POINT'?[e.position]:e.points).flat();}
function validate(data){
 const s=data.production?.spatial;if(!s)return;
 if(s.version!==1||typeof s.enabled!=='boolean'||!own(P.MM,s.units)||!Array.isArray(s.constraints)||s.constraints.length>limits.constraints)throw Error('Invalid spatial constraint settings.');
 const params=E.parameters(s.parameters),ents=new Map(data.entities.map(e=>[e.id,e])),ids=new Set();
 const lookup=n=>{if(!params.has(n))throw Error('Unknown spatial parameter: '+n);return params.get(n);};
 const referenced=new Set();
 for(const c of s.constraints){
  if(!c||!own(TYPES,c.type)||typeof c.id!=='string'||!/^[-\w.:]{1,128}$/.test(c.id)||ids.has(c.id))throw Error('Invalid spatial constraint type or identifier.');ids.add(c.id);
  if(c.measureAxis)axis(c.measureAxis);
  if(c.suppressed!=null&&typeof c.suppressed!=='boolean')throw Error('Invalid spatial suppression.');
  let count=0;
  for(const r of refs(c)){
   if(!r||typeof r!=='object'||Array.isArray(r)||!!r.entity===!!r.world)throw Error('Choose one entity reference or world datum.');
   if(r.axis)axis(r.axis);if(r.xaxis)axis(r.xaxis);
   if(r.world){point(r.world);if(r.local||r.point!=null||r.segment!=null||r.vertex!=null)throw Error('World datums cannot contain entity anchors.');continue;}
   count++;const e=ents.get(r.entity);if(!e||!RIGID.has(e.type)&&!FLEX.has(e.type))throw Error('Spatial references require a point, line, straight polyline, mesh or block.');if(!c.suppressed)referenced.add(e.id);
   if(e.constraintFrame)matrix(e.constraintFrame);
   if(e.type==='POLYLINE'&&e.bulges?.some(x=>Math.abs(x)>1e-12))throw Error('Spatial flexible polylines must have straight segments.');
   if(RIGID.has(e.type)){
    if(r.point!=null||r.segment!=null)throw Error('Rigid references use local XYZ or a mesh vertex.');
    if(r.vertex!=null){if(e.type!=='MESH'||!Number.isInteger(r.vertex)||r.vertex<0||r.vertex>=e.vertices.length||r.local)throw Error('Invalid spatial mesh vertex.');}else point(r.local||[0,0,0]);
   }else{
    if(r.local||r.vertex!=null)throw Error('Flexible geometry uses point/segment anchors.');
    const n=e.type==='POINT'?1:e.points.length;
    if(e.type==='POINT'){if(r.point!=null&&r.point!=='position')throw Error('POINT uses the position anchor.');}
    else if(r.point!=null&&!['start','end','mid'].includes(r.point)&&!(Number.isInteger(r.point)&&r.point>=0&&r.point<n))throw Error('Invalid spatial point index.');
    if(r.segment!=null&&(!e.points||!Number.isInteger(r.segment)||r.segment<0||r.segment>=n-(e.closed?0:1)))throw Error('Invalid spatial segment index.');
   }
  }
  if(!count)throw Error('A constraint must reference drawing geometry.');
  if(['fastened','slider'].includes(c.type)||c.type==='hinge'&&(c.limits||c.drive))for(const r of refs(c)){if(V.len(V.cross(V.norm(r.axis||[0,0,1]),V.norm(r.xaxis||[1,0,0])))<1e-7)throw Error('A mating frame needs two independent axes.');}
  if(DIM.has(c.type)){const v=E.evaluate(c.value??0,lookup);if(c.type==='distance'&&v<0||c.type==='length-ratio'&&v<=0||c.type==='angle'&&(v<0||v>180))throw Error('Spatial dimension is outside its valid range.');}
  if(c.limits!=null||c.drive!=null){
   if(!JOINT.has(c.type))throw Error('Travel limits and drives require a hinge or slider.');
   for(const r of refs(c))if(r.entity&&!RIGID.has(ents.get(r.entity).type))throw Error('Limited or driven joints require rigid body datums.');
   jointSettings(c,lookup);
  }
  if(c.type==='fixed-point')point(c.target);
  if(c.type==='fixed-entity'){
   const e=ents.get(c.a.entity);if(!e)throw Error('Fix an entity, not a world datum.');
   if(RIGID.has(e.type))matrix(c.target);else if(!Array.isArray(c.target)||c.target.length!==entityValues(e).length||!c.target.every(Number.isFinite))throw Error('Invalid fixed spatial geometry.');
  }
 }
 // Two independent nonlinear solvers must not silently overwrite each other.
 const planar=data.production?.parametric;
 if(s.enabled&&planar?.enabled)for(const c of planar.constraints.filter(c=>!c.suppressed))for(const r of [c.a,c.b,c.c])if(r&&referenced.has(r.entity)&&!data.layers.find(l=>l.id===ents.get(r.entity).layer)?.locked)throw Error('Free geometry cannot participate in both planar and spatial systems. Suppress or separate one system first.');
}
function frame(e){return e.constraintFrame||Array.from(M.identity());}
function anchor(e,r){
 if(RIGID.has(e.type))return r.vertex!=null?e.vertices[r.vertex]:M.point(frame(e),r.local||[0,0,0]);
 if(e.type==='POINT')return e.position;
 const n=e.points.length,j=r.segment||0;
 return r.point==='mid'?V.lerp(e.points[j],e.points[(j+1)%n],.5):e.points[r.point==='end'?n-1:Number.isInteger(r.point)?r.point:0];
}
function evaluateValue(s,c,doc,values){const v=E.evaluate(c.value??0,n=>{if(!values.has(n))throw Error('Unknown spatial parameter: '+n);return values.get(n);});return ['angle','length-ratio'].includes(c.type)?v:v*P.MM[s.units]/P.MM[doc.units];}
// Joint intervals are unilateral: interior coordinates remain free. A driver is
// an optional bilateral equation and never silently changes the stored limits.
function jointSettings(c,lookup){
 const out={enabled:false,min:null,max:null,drive:null};
 if(c.limits!=null){
  const l=c.limits;
  if(!l||typeof l!=='object'||Array.isArray(l)||typeof l.enabled!=='boolean'||Object.keys(l).some(k=>!['enabled','min','max'].includes(k)))throw Error('Invalid joint limit settings.');
  out.enabled=l.enabled;
  for(const key of ['min','max'])if(l[key]!=null)out[key]=number(E.evaluate(l[key],lookup),'joint '+key);
  if(out.min==null&&out.max==null)throw Error('Specify a minimum or maximum joint limit.');
  if(out.min!=null&&out.max!=null&&out.min>out.max)throw Error('Joint minimum exceeds maximum.');
 }
 if(c.drive!=null){
  const d=c.drive;
  if(!d||typeof d!=='object'||Array.isArray(d)||typeof d.enabled!=='boolean'||d.value==null||Object.keys(d).some(k=>!['enabled','value'].includes(k)))throw Error('Invalid joint drive settings.');
  const v=number(E.evaluate(d.value,lookup),'joint drive');
  if(c.type==='hinge'&&(v<=-180||v>=180))throw Error('Hinge drive must lie strictly between -180 and 180 degrees.');
  if(d.enabled)out.drive=v;
 }
 if(c.type==='hinge'&&[out.min,out.max].some(v=>v!=null&&(v<=-180||v>=180)))throw Error('Hinge limits must lie strictly between -180 and 180 degrees; wrapped intervals are not supported.');
 if(out.enabled&&out.drive!=null&&(out.min!=null&&out.drive<out.min||out.max!=null&&out.drive>out.max))throw Error('Joint drive is outside its enabled travel limits.');
 return out;
}
// Transport A's transverse axis onto B's normal before measuring signed twist.
// Unlike direct projection, this also handles a starting frame tilted by 90 deg.
function jointAngle(za,xa,zb,xb){
 xa=V.norm(V.sub(xa,V.mul(za,V.dot(xa,za))));
 xb=V.norm(V.sub(xb,V.mul(zb,V.dot(xb,zb))));
 const cross=V.cross(za,zb),sin=V.len(cross),cos=Math.max(-1,Math.min(1,V.dot(za,zb)));
 if(sin>1e-12)xa=M.point(M.rotation(Math.atan2(sin,cos),cross),xa,0);
 else if(cos<0)xa=M.point(M.rotation(Math.PI,xb),xa,0);
 return Math.atan2(V.dot(zb,V.cross(xb,xa)),V.dot(xb,xa));
}
function compile(doc){
 const s=state(doc);validate(doc.serialize());const active=s.constraints.filter(c=>!c.suppressed),objects=new Map(),values=E.parameters(s.parameters),targets=new Map(active.filter(c=>DIM.has(c.type)).map(c=>[c.id,evaluateValue(s,c,doc,values)]));
 const joints=new Map(active.filter(c=>JOINT.has(c.type)).map(c=>{
  const v=jointSettings(c,n=>values.get(n)),factor=c.type==='hinge'?Math.PI/180:P.MM[s.units]/P.MM[doc.units];
  for(const k of ['min','max','drive'])if(v[k]!=null)v[k]*=factor;
  return [c.id,v];
 }));
 const used=[...new Set(active.flatMap(c=>refs(c).map(r=>r.entity).filter(Boolean)))];
 const all=[];let scalars=0;
 for(const id of used){const e=doc.byId.get(id),rigid=RIGID.has(e.type);scalars+=rigid?6:entityValues(e).length;if(scalars>limits.scalars)throw Error('Spatial referenced-geometry scalar limit exceeded.');
  if(rigid){const f=frame(e),positions=active.flatMap(c=>refs(c)).filter(r=>r.entity===id).map(r=>anchor(e,r));all.push(...positions);objects.set(id,{e,rigid,frame:f,pivot:positions[0]||M.point(f,[0,0,0])});}
  else{const pts=e.type==='POINT'?[e.position]:e.points;all.push(...pts);objects.set(id,{e,rigid,pts});}
 }
 for(const c of active)for(const r of refs(c))if(r.world)all.push(r.world);
 let low=[Infinity,Infinity,Infinity],high=[-Infinity,-Infinity,-Infinity];for(const p of all)for(let i=0;i<3;i++){low[i]=Math.min(low[i],p[i]);high[i]=Math.max(high[i],p[i]);}
 if(!all.length){low=[0,0,0];high=[1,1,1];}
 const origin=V.lerp(low,high,.5),scale=Math.max(1,V.dist(low,high),...active.filter(c=>['distance','point-plane','plane-mate'].includes(c.type)).map(c=>Math.abs(targets.get(c.id))),...active.filter(c=>c.type==='slider').flatMap(c=>{const j=joints.get(c.id);return [j.min,j.max,j.drive].filter(v=>v!=null).map(Math.abs);}));
 const normalize=p=>V.mul(V.sub(p,origin),1/scale),world=p=>V.add(origin,V.mul(p,scale));
 const fixed=new Map();for(const c of active)if(c.type==='fixed-entity'&&!fixed.has(c.a.entity))fixed.set(c.a.entity,c);
 const x=[];
 for(const o of objects.values()){
  o.locked=doc.layer(o.e).locked;o.fix=fixed.get(o.e.id);o.constant=o.locked||!!o.fix;
  if(o.rigid){o.initial=Array(6).fill(0);o.fixedMatrix=o.fix&&!o.locked?M.multiply(o.fix.target,M.inverse(o.frame)):M.identity();}
  else{o.initial=(o.fix&&!o.locked?Array.from({length:o.fix.target.length/3},(_,i)=>o.fix.target.slice(i*3,i*3+3)):o.pts).flatMap(normalize);}
  o.indices=o.initial.map(v=>o.constant?-1:x.push(v)-1);
 }
 if(x.length>limits.variables)throw Error('Spatial solver limit: '+limits.variables+' free scalar variables.');
 const val=(o,x)=>o.indices.map((i,j)=>i<0?o.initial[j]:x[i]);
 function pose(o,x){if(o.constant)return o.fixedMatrix;const v=val(o,x),rot=v.slice(3),a=V.len(rot);return M.multiply(M.translation(...v.slice(0,3).map(v=>v*scale)),M.around(o.pivot,a>1e-14?M.rotation(a,rot):M.identity()));}
 function context(x){
  const cache=new Map();for(const o of objects.values())cache.set(o.e.id,o.rigid?pose(o,x):val(o,x));
  const pt=r=>{if(r.world)return normalize(r.world);const o=objects.get(r.entity),v=cache.get(r.entity);if(o.rigid)return normalize(M.point(v,anchor(o.e,r)));if(o.e.type==='POINT')return v.slice(0,3);const n=v.length/3,i=r.point==='end'?n-1:Number.isInteger(r.point)?r.point:0,j=r.segment||0;return r.point==='mid'?V.lerp(v.slice(j*3,j*3+3),v.slice(((j+1)%n)*3,((j+1)%n)*3+3),.5):v.slice(i*3,i*3+3);};
  const segment=r=>{const o=objects.get(r.entity);if(!o||!o.e.points||o.rigid)throw Error('This constraint needs a flexible line segment.');const i=r.segment||0;return [pt({...r,point:i}),pt({...r,point:(i+1)%o.e.points.length})];};
  const dir=(r,xaxis=false)=>{
   let d=r[xaxis?'xaxis':'axis'];const o=objects.get(r.entity);
   if(!d&&!xaxis&&o&&!o.rigid&&o.e.points){const[a,b]=segment(r);d=V.sub(b,a);}else{d=d||(xaxis?[1,0,0]:[0,0,1]);if(o?.rigid)d=M.point(cache.get(r.entity),M.point(o.frame,d,0),0);}
   if(V.len(d)<1e-10)throw Error('Degenerate spatial direction.');return V.norm(d);
  };
  const length=r=>{const[a,b]=segment(r);return V.dist(a,b);};
  return {pt,dir,length,segment,cache};
 }
 function coordinate(c,ctx){return c.type==='hinge'?jointAngle(ctx.dir(c.a),ctx.dir(c.a,true),ctx.dir(c.b),ctx.dir(c.b,true)):V.dot(V.sub(ctx.pt(c.a),ctx.pt(c.b)),ctx.dir(c.b))*scale;}
 function jointResidual(c,ctx,includeLimits,activeBounds){
  const j=joints.get(c.id);if(!j||j.drive==null&&(!j.enabled||!includeLimits&&!(j.min!=null&&j.min===j.max)))return[];
  const v=coordinate(c,ctx),unit=c.type==='hinge'?1:scale,r=[];
  if(j.drive!=null)r.push((v-j.drive)/unit);
  if(j.enabled&&j.min!=null&&j.min===j.max){r.push((v-j.min)/unit);return r;}
  if(includeLimits&&j.enabled){const flags=activeBounds?.get(c.id);if(j.min!=null)r.push((flags?(flags.min?v-j.min:0):Math.min(0,v-j.min))/unit);if(j.max!=null)r.push((flags?(flags.max?v-j.max:0):Math.max(0,v-j.max))/unit);}
  return r;
 }
 function residualFor(c,ctx,includeLimits=true,activeBounds=null){
  const {pt,dir,length,segment,cache}=ctx,a=c.a,b=c.b,p=pt(a),q=b?pt(b):null,d=q?V.sub(p,q):null,v=targets.get(c.id),z=()=>dir(a),w=()=>dir(b);
  const orient=()=>[...V.sub(z(),w()),...V.sub(dir(a,true),dir(b,true))];
  switch(c.type){
   case 'coincident':return d;
   case 'distance':return v===0?d:[V.len(d)-v/scale];
   case 'distance-x':case 'distance-y':case 'distance-z':return [V.dot(V.sub(q,p),V.norm(c.measureAxis||({x:[1,0,0],y:[0,1,0],z:[0,0,1]})[c.type.at(-1)]))-v/scale];
   case 'fixed-point':return V.sub(p,normalize(c.target));
   case 'fixed-entity':{
    const o=objects.get(a.entity);if(o.fix===c&&!o.locked)return[];
    if(!o.rigid)return cache.get(a.entity).map((v,i)=>v-(c.target[i]-origin[i%3])/scale);
    const f=M.multiply(cache.get(a.entity),o.frame);return [[0,0,0],[1,0,0],[0,1,0],[0,0,1]].flatMap((p,i)=>V.mul(V.sub(M.point(f,p),M.point(c.target,p)),i?1:1/scale));
   }
   case 'parallel':return V.cross(z(),w());
   case 'same-direction':return V.sub(z(),w());
   case 'opposed':return V.add(z(),w());
   case 'perpendicular':return [V.dot(z(),w())];
   case 'angle':return v===0?V.sub(z(),w()):v===180?V.add(z(),w()):[V.dot(z(),w())-Math.cos(v*Math.PI/180)];
   case 'equal-length':return [length(a)-length(b)];
   case 'length-ratio':return [length(a)-v*length(b)];
   case 'point-on-line':return V.cross(d,w());
   case 'point-plane':return [V.dot(d,w())-v/scale];
   case 'plane-mate':return [...V.add(z(),w()),V.dot(d,w())-v/scale];
   case 'coaxial':return [...V.cross(z(),w()),...V.cross(d,w())];
   case 'midpoint':return V.sub(p,V.lerp(...segment(b),.5));
   case 'symmetric':{const n=dir(c.c),o=pt(c.c);return V.sub(V.sub(p,V.mul(n,2*V.dot(V.sub(p,o),n))),q);}
   case 'hinge':return [...d,...V.sub(z(),w()),...jointResidual(c,ctx,includeLimits,activeBounds)];
   case 'slider':return [...orient(),...V.cross(d,w()),...jointResidual(c,ctx,includeLimits,activeBounds)];
   case 'fastened':return [...orient(),...d];
   default:throw Error('Unknown spatial constraint.');
  }
 }
 const residual=(x,includeLimits=true,activeBounds=null)=>{const ctx=context(x),r=active.flatMap(c=>residualFor(c,ctx,includeLimits,activeBounds));if(r.length>limits.scalars||r.some(v=>!Number.isFinite(v)))throw Error('Invalid or oversized spatial residual system.');return r;};
 // Freeze the inequality active set only while differentiating. Central
 // differences across a stop otherwise halve its slope and overshoot inward.
 const linearized=x=>{const ctx=context(x),bounds=new Map();
  for(const c of active)if(JOINT.has(c.type)){
   const j=joints.get(c.id);if(!j.enabled)continue;
   const v=coordinate(c,ctx);bounds.set(c.id,{min:j.min!=null&&v<j.min,max:j.max!=null&&v>j.max});
  }
  return y=>residual(y,true,bounds);
 };
 const valid=x=>x.every(v=>Number.isFinite(v)&&Math.abs(v)<1e10);
 const apply=x=>{
  for(const o of objects.values()){
   if(o.locked)continue;
   if(o.rigid){const m=pose(o,x);if(Array.from(m).some((v,i)=>Math.abs(v-M.identity()[i])>1e-11))doc.replace(o.e.id,K.Geo.transform({...o.e,constraintFrame:Array.from(o.frame)},m));}
   else{const v=val(o,x),pts=Array.from({length:v.length/3},(_,i)=>world(v.slice(i*3,i*3+3)));const original=o.e.type==='POINT'?[o.e.position]:o.e.points;if(pts.some((p,i)=>V.dist(p,original[i])>1e-11)){const e=clone(o.e);if(e.type==='POINT')e.position=pts[0];else e.points=pts;doc.replace(e.id,e);}}
  }doc.reindex();doc.cache=new WeakMap();
 };
 const jointReports=x=>{const ctx=context(x);return active.filter(c=>JOINT.has(c.type)&&(c.limits||c.drive)).map(c=>{
  const j=joints.get(c.id),v=coordinate(c,ctx),unit=c.type==='hinge'?Math.PI/180:1,tolerance=1e-8*(c.type==='hinge'?1:scale);
  return {id:c.id,type:c.type,coordinate:v/unit,units:c.type==='hinge'?'deg':doc.units,
   minimum:j.min==null?null:j.min/unit,maximum:j.max==null?null:j.max/unit,limitsEnabled:j.enabled,driven:j.drive!=null,
   atMinimum:j.enabled&&j.min!=null&&Math.abs(v-j.min)<=tolerance,atMaximum:j.enabled&&j.max!=null&&Math.abs(v-j.max)<=tolerance};
 });};
 return {x,residual,linearized,valid,apply,active,context,residualFor,scale,jointReports};
}
function jacobian(fn,x,r){const out=r.map(()=>Array(x.length).fill(0));for(let c=0;c<x.length;c++){const h=1e-6*Math.max(1,Math.abs(x[c])),a=x.slice(),b=x.slice();a[c]+=h;b[c]-=h;const ra=fn(a),rb=fn(b);for(let k=0;k<r.length;k++)out[k][c]=(ra[k]-rb[k])/(2*h);}return out;}
function rank(j,n){const a=j.map(r=>r.slice());let row=0;for(let col=0;col<n&&row<a.length;col++){let k=row;for(let i=row+1;i<a.length;i++)if(Math.abs(a[i][col])>Math.abs(a[k][col]))k=i;if(Math.abs(a[k][col])<1e-7)continue;[a[row],a[k]]=[a[k],a[row]];const v=a[row][col];for(let c=col;c<n;c++)a[row][c]/=v;for(let i=row+1;i<a.length;i++){const f=a[i][col];for(let c=col;c<n;c++)a[i][c]-=f*a[row][c];}row++;}return row;}
function linear(a,b){const n=b.length,t=a.map((r,i)=>[...r,b[i]]);for(let i=0;i<n;i++){let k=i;for(let j=i+1;j<n;j++)if(Math.abs(t[j][i])>Math.abs(t[k][i]))k=j;if(Math.abs(t[k][i])<1e-20)return null;[t[i],t[k]]=[t[k],t[i]];for(let j=i+1;j<n;j++){const f=t[j][i]/t[i][i];for(let c=i;c<=n;c++)t[j][c]-=f*t[i][c];}}const x=Array(n).fill(0);for(let i=n-1;i>=0;i--){let s=t[i][n];for(let j=i+1;j<n;j++)s-=t[i][j]*x[j];x[i]=s/t[i][i];}return x;}
function solve(doc,{apply=true,maxIterations=limits.iterations,timeLimit=1500}={}){
 const s=state(doc);if(!s||!s.enabled||!s.constraints.some(c=>!c.suppressed))return {converged:true,status:'inactive',variables:0,equations:0,rank:0,degreesOfFreedom:0,redundantEquations:0,conflicts:[]};
 number(maxIterations,'iteration limit',1000);number(timeLimit,'time limit',30000);if(maxIterations<0||timeLimit<=0)throw Error('Invalid spatial solver budget.');
 const sys=compile(doc),start=Date.now(),norm=r=>r.reduce((s,v)=>s+v*v,0),max=r=>Math.max(0,...r.map(Math.abs));let x=sys.x.slice(),r=sys.residual(x),error=norm(r),lambda=1e-3,iterations=0,seeds=0;
 for(;iterations<maxIterations&&max(r)>1e-9&&x.length&&Date.now()-start<timeLimit;iterations++){
  const j=jacobian(sys.linearized(x),x,r),n=x.length,a=Array.from({length:n},()=>Array(n).fill(0)),b=Array(n).fill(0);
  for(let k=0;k<r.length;k++)for(let u=0;u<n;u++){b[u]-=j[k][u]*r[k];for(let v=0;v<=u;v++)a[u][v]+=j[k][u]*j[k][v];}
  if((max(b)<1e-12||j.some((row,i)=>Math.abs(r[i])>1e-6&&max(row)<1e-12))&&error>1e-18&&seeds<3){seeds++;x=x.map((v,i)=>v+1e-4*seeds*Math.sin((i+1)*2.399963));r=sys.residual(x);error=norm(r);continue;}
  for(let u=0;u<n;u++){for(let v=0;v<u;v++)a[v][u]=a[u][v];a[u][u]+=lambda*(a[u][u]+1e-3);}
  const dx=linear(a,b);if(!dx)break;const next=x.map((v,i)=>v+dx[i]);let nr,ne=Infinity;try{if(sys.valid(next)){nr=sys.residual(next);ne=norm(nr);}}catch(_){}
  if(ne<error){x=next;r=nr;error=ne;lambda=Math.max(1e-12,lambda/3);}else lambda=Math.min(1e12,lambda*10);
 }
 const converged=max(r)<=1e-9&&sys.valid(x),equalities=x=>sys.residual(x,false),er=equalities(x),rk=rank(jacobian(equalities,x,er),x.length),ctx=sys.context(x);
 // Rank only bilateral equations. An active stop blocks outward velocity, not
 // inward travel, so it must not falsely report a permanently fixed joint.
 const joints=sys.jointReports(x);
 const report={converged,status:converged?(rk===x.length?'fully-constrained':'under-constrained'):'failed-to-converge',variables:x.length,equations:er.length,inequalities:r.length-er.length,joints,rank:rk,degreesOfFreedom:x.length-rk,redundantEquations:er.length-rk,maxResidual:max(r),lengthScale:sys.scale,iterations,
  conflicts:sys.active.map(c=>({id:c.id,type:c.type,residual:max(sys.residualFor(c,ctx))})).filter(c=>c.residual>1e-9).sort((a,b)=>b.residual-a.residual)};
 if(converged&&apply)sys.apply(x);return report;
}
function enforce(doc){const r=solve(doc);doc.spatialReport=r;if(!r.converged)throw Error('3D constraints did not converge; edit rolled back. Unsatisfied: '+r.conflicts.slice(0,4).map(c=>c.type).join(', ')+'.');}
function add(doc,spec){let c;doc.transaction('Add 3D '+spec.type,()=>{c={...clone(spec),id:spec.id||K.uid('spatial')};const s=state(doc,true);
 if(c.type==='fixed-entity'&&c.target==null){const e=doc.byId.get(c.a.entity);if(!e)throw Error('Select an entity to fix.');c.target=clone(RIGID.has(e.type)?frame(e):entityValues(e));}
 if(c.type==='fixed-point'&&c.target==null){const e=doc.byId.get(c.a.entity);if(!e)throw Error('Select a point to fix.');c.target=clone(anchor(e,c.a));}
 s.constraints.push(c);
 });return c;}
function removeReferences(doc,ids){const s=state(doc);if(s){const deleted=new Set(ids);s.constraints=s.constraints.filter(c=>!refs(c).some(r=>deleted.has(r.entity)));}}
function convertUnits(doc,newUnits,factor,convert){const s=state(doc);if(!s)return;if(!own(P.MM,newUnits)||!Number.isFinite(factor)||factor<=0)throw Error('Invalid spatial unit conversion.');if(!convert){s.units=newUnits;return;}
 for(const c of s.constraints){for(const r of refs(c))if(r.world)r.world=r.world.map(v=>v*factor);if(c.target){const e=doc.byId.get(c.a.entity);c.target=c.type==='fixed-entity'&&RIGID.has(e?.type)?Array.from(M.multiply(M.scale(factor),c.target)):c.target.map(v=>v*factor);}}
}
function copyInto(doc,data,ids,m){const src=data.production?.spatial;if(!src)return[];const warnings=[],complete=src.constraints.filter(c=>refs(c).every(r=>!r.entity||ids.has(r.entity))),relevant=src.constraints.filter(c=>refs(c).some(r=>ids.has(r.entity)));
 if(complete.length!==relevant.length)warnings.push('3D constraints referring to objects outside the copied selection were omitted.');if(!complete.length)return warnings;
 const axes=[[1,0,0],[0,1,0],[0,0,1]].map(a=>M.point(m,a,0)),scale=V.len(axes[0]);
 if(!scale||axes.some(a=>Math.abs(V.len(a)-scale)>scale*1e-7)||Math.abs(V.dot(axes[0],axes[1]))+Math.abs(V.dot(axes[0],axes[2]))+Math.abs(V.dot(axes[1],axes[2]))>scale*scale*1e-7){warnings.push('Nonuniform/skewed clipboard retains geometry without 3D constraints.');return warnings;}
 const st=state(doc,true),names=new Map(),existing=new Set(st.parameters.map(p=>p.name));for(const p of src.parameters){let n=p.name,i=1;while(existing.has(n))n=p.name.slice(0,36)+'_copy'+i++;existing.add(n);names.set(p.name,n);}
 const rewrite=t=>typeof t==='string'?t.replace(/(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?|[A-Za-z][A-Za-z0-9_]*/g,t=>names.get(t)||t):t;
 st.parameters.push(...src.parameters.map(p=>({name:names.get(p.name),expression:rewrite(p.expression)})));
 const factor=scale*P.MM[doc.units]/P.MM[data.units]*P.MM[src.units]/P.MM[st.units],from=new Map(data.entities.map(e=>[e.id,e]));
 for(const orig of complete){const c=clone(orig);c.id=K.uid('spatial');for(const r of refs(c)){if(r.entity){if(FLEX.has(from.get(r.entity)?.type)){if(r.axis)r.axis=V.norm(M.point(m,r.axis,0));if(r.xaxis)r.xaxis=V.norm(M.point(m,r.xaxis,0));}r.entity=ids.get(r.entity);}else{r.world=M.point(m,r.world);r.axis=V.norm(M.point(m,r.axis||[0,0,1],0));r.xaxis=V.norm(M.point(m,r.xaxis||[1,0,0],0));}}
  if(['distance-x','distance-y','distance-z'].includes(c.type))c.measureAxis=V.norm(M.point(m,c.measureAxis||({x:[1,0,0],y:[0,1,0],z:[0,0,1]})[c.type.at(-1)],0));
  if(c.target){const e=from.get(orig.a.entity);if(c.type==='fixed-entity'&&RIGID.has(e.type))c.target=Array.from(M.multiply(m,c.target));else c.target=Array.from({length:c.target.length/3},(_,i)=>M.point(m,c.target.slice(i*3,i*3+3))).flat();}
  if(DIM.has(c.type)){c.value=rewrite(c.value??0);if(!['angle','length-ratio'].includes(c.type)&&Math.abs(factor-1)>1e-12)c.value=`(${c.value}) * ${factor}`;}
  if(JOINT.has(c.type)){
   // Reflections reverse signed hinge twist; slider travel follows its axis.
   const sign=c.type==='hinge'&&V.dot(axes[0],V.cross(axes[1],axes[2]))<0?-1:1;
   const f=c.type==='hinge'?sign:factor;
   const change=v=>{if(v==null)return v;v=rewrite(v);return Math.abs(f-1)<1e-12?v:`(${v}) * ${f}`;};
   if(c.limits){c.limits.min=change(c.limits.min);c.limits.max=change(c.limits.max);if(sign<0)[c.limits.min,c.limits.max]=[c.limits.max,c.limits.min];}
   if(c.drive)c.drive.value=change(c.drive.value);
  }
  st.constraints.push(c);
 }return warnings;
}
const bind=P.bind;P.bind=function(doc){bind(doc);const s=doc.production?.spatial;if(s){const ids=new Set(s.constraints.flatMap(c=>own(TYPES,c.type)?refs(c).map(r=>r?.entity):[]));for(const e of doc.entities)if(RIGID.has(e.type)&&ids.has(e.id)&&!e.constraintFrame)e.constraintFrame=Array.from(M.identity());}};
const transform=K.Geo.transform;K.Geo.transform=function(e,m){const out=transform(e,m);if(e.constraintFrame)out.constraintFrame=Array.from(M.multiply(m,e.constraintFrame));return out;};
const validateEntity=P.validateEntity;P.validateEntity=function(e){validateEntity(e);if(e.constraintFrame){if(!RIGID.has(e.type))throw Error('Only rigid bodies carry spatial frames.');matrix(e.constraintFrame);}};
K.SpatialConstraints={TYPES,JOINT,DIM,RIGID,FLEX,limits,state,validate,solve,enforce,add,anchor,frame,removeReferences,convertUnits,copyInto};
})(globalThis);
