/* Kestrel CAD — bounded planar nonlinear constraint solving; no eval or remote service. */
(function(root){
'use strict';
const K=root.Kestrel,P=K.Production,{V,M}=K.Math;
const FN=Object.freeze({abs:Math.abs,sqrt:Math.sqrt,sin:Math.sin,cos:Math.cos,tan:Math.tan,asin:Math.asin,acos:Math.acos,atan:Math.atan,atan2:Math.atan2,min:Math.min,max:Math.max,pow:Math.pow,round:Math.round,floor:Math.floor,ceil:Math.ceil,hypot:Math.hypot});
const CONSTANTS=Object.freeze({pi:Math.PI,e:Math.E,deg:Math.PI/180});
const own=(o,k)=>Object.prototype.hasOwnProperty.call(o,k);
const finite=(v,label='value')=>{if(typeof v!=='number'||!Number.isFinite(v)||Math.abs(v)>1e12)throw Error('Invalid '+label);return v;};
const identifier=s=>typeof s==='string'&&/^[A-Za-z][A-Za-z0-9_]{0,47}$/.test(s)&&!own(FN,s)&&!own(CONSTANTS,s)&&!['constructor','prototype','__proto__'].includes(s);
function expression(source,lookup=()=>{throw Error('Unknown parameter');}){
    if(typeof source==='number')return finite(source);
    if(typeof source!=='string'||source.length>512)throw Error('Expression must contain at most 512 characters.');
    const tokens=[];let pos=0;
    while(pos<source.length){const m=/^\s*(?:(\d+(?:\.\d*)?(?:[eE][+-]?\d+)?|\.\d+(?:[eE][+-]?\d+)?)|([A-Za-z][A-Za-z0-9_]*)|([+\-*/^(),]))/.exec(source.slice(pos));if(!m){if(!source.slice(pos).trim())break;throw Error('Invalid expression near '+source.slice(pos,pos+20));}tokens.push(m[1]?{n:Number(m[1])}:m[2]||m[3]);pos+=m[0].length;if(tokens.length>256)throw Error('Expression too complex.');}
    let at=0,depth=0;
    function parse(min=0){if(++depth>48)throw Error('Expression nesting limit.');let token=tokens[at++],v;
        if(token==='+'||token==='-'){v=parse(3);if(token==='-')v=-v;}
        else if(token==='('){v=parse();if(tokens[at++]!==')')throw Error('Missing closing parenthesis.');}
        else if(token&&typeof token==='object')v=token.n;
        else if(typeof token==='string'&&/^[A-Za-z]/.test(token)){
            if(tokens[at]==='('){if(!own(FN,token))throw Error('Unknown function '+token);at++;const a=[];if(tokens[at]!==')'){do{a.push(parse());if(a.length>16)throw Error('Too many function arguments.');}while(tokens[at]===','&&++at);}if(tokens[at++]!==')')throw Error('Missing closing parenthesis.');const arity=['atan2','pow'].includes(token)?2:['min','max','hypot'].includes(token)?null:1;if(arity!=null&&a.length!==arity||a.length===0)throw Error('Wrong argument count for '+token);v=FN[token](...a);}
            else v=own(CONSTANTS,token)?CONSTANTS[token]:lookup(token);
        }else throw Error('Expected number or parameter.');
        while(true){const op=tokens[at],prec=({'+':1,'-':1,'*':2,'/':2,'^':4})[op];if(prec==null||prec<min)break;at++;const rhs=parse(prec+(op==='^'?0:1));v=op==='+'?v+rhs:op==='-'?v-rhs:op==='*'?v*rhs:op==='/'?v/rhs:Math.pow(v,rhs);finite(v,'expression result');}
        depth--;return finite(v,'expression result');
    }
    const result=parse();if(at!==tokens.length)throw Error('Unexpected expression token.');return result;
}
function parameters(rows=[]){
    if(!Array.isArray(rows)||rows.length>128)throw Error('Parameter limit is 128.');const defs=new Map(),values=new Map(),visiting=new Set();
    for(const r of rows){if(!r||!identifier(r.name)||defs.has(r.name))throw Error('Invalid or duplicate parameter name.');defs.set(r.name,r.expression);}
    const get=name=>{if(values.has(name))return values.get(name);if(!defs.has(name))throw Error('Unknown parameter: '+name);if(visiting.has(name))throw Error('Parameter dependency cycle at '+name);if(visiting.size>48)throw Error('Parameter dependency depth exceeded.');visiting.add(name);const v=expression(defs.get(name),get);visiting.delete(name);values.set(name,v);return v;};
    for(const name of defs.keys())get(name);return values;
}
const TYPES=Object.freeze({horizontal:1,vertical:1,coincident:2,distance:2,'distance-x':2,'distance-y':2,fixed:1,'fix-entity':1,parallel:2,perpendicular:2,collinear:2,equal:2,concentric:2,radius:1,diameter:1,angle:2,tangent:2,'point-on-line':2,'point-on-circle':2,midpoint:2,symmetric:3});
const keys=['a','b','c'];
function state(doc,create=false){const p=P.ensure(doc);if(!p.parametric&&create)p.parametric={enabled:true,units:doc.units,plane:K.clone(p.ucs),parameters:[],constraints:[]};return p.parametric;}
function validate(data){
    const s=data.production?.parametric;if(!s)return;
    if(typeof s.enabled!=='boolean'||!Array.isArray(s.constraints)||s.constraints.length>256)throw Error('Invalid sketch constraint settings.');P.frame(s.plane);const pvalues=parameters(s.parameters);if(s.units!=null&&!own(P.MM,s.units))throw Error('Invalid parameter units.');
    const ids=new Set(),ents=new Map(data.entities.map(e=>[e.id,e]));
    for(const c of s.constraints){if(!c||typeof c.id!=='string'||c.id.length>128||ids.has(c.id)||!own(TYPES,c.type))throw Error('Invalid constraint identifier or type.');ids.add(c.id);
        for(let i=0;i<TYPES[c.type];i++){const r=c[keys[i]],e=r&&ents.get(r.entity);if(!e)throw Error('Constraint references a missing entity.');if(!['LINE','POLYLINE','CIRCLE','ARC','POINT'].includes(e.type)||e.solid)throw Error('Constraints support planar lines, straight polylines, points, circles and arcs.');if(e.type==='POLYLINE'&&e.bulges?.some(b=>Math.abs(b)>1e-12))throw Error('Explode bulged polylines before constraining them.');if(r.segment!=null&&(!Number.isInteger(r.segment)||r.segment<0||!e.points||r.segment>=e.points.length-(e.closed?0:1)))throw Error('Invalid constrained segment index.');if(r.point!=null&&!(Number.isInteger(r.point)&&e.points&&r.point>=0&&r.point<e.points.length)&&!['center','position','start','end','mid'].includes(r.point))throw Error('Invalid constrained point.');}
        if(c.value!=null&&!(typeof c.value==='string'&&c.value.length<=512||typeof c.value==='number'&&Number.isFinite(c.value)))throw Error('Invalid dimensional expression.');
        if(c.target!=null&&(!Array.isArray(c.target)||c.target.length>160||!c.target.every(Number.isFinite)))throw Error('Invalid fixed target.');
        if(c.suppressed!=null&&typeof c.suppressed!=='boolean')throw Error('Invalid constraint suppression.');
        if(c.internal!=null&&typeof c.internal!=='boolean')throw Error('Invalid tangency type.');
        const entity=r=>ents.get(r.entity),segment=r=>{if(!entity(r).points)throw Error('Constraint requires a line segment.');},conic=r=>{if(!['CIRCLE','ARC'].includes(entity(r).type))throw Error('Constraint requires a circle or arc.');};
        const anchor=r=>{const e=entity(r),p=r.point;if(e.center){if(p!=null&&p!=='center'&&!(e.type==='ARC'&&['start','end','mid'].includes(p)))throw Error('Invalid circle/arc constraint point.');}else if(e.position){if(p!=null&&p!=='position')throw Error('Invalid point anchor.');}else if(p!=null&&!Number.isInteger(p)&&!['start','end','mid'].includes(p))throw Error('Invalid line point anchor.');};
        if(['horizontal','vertical'].includes(c.type))segment(c.a);
        if(['parallel','perpendicular','collinear','angle'].includes(c.type)){segment(c.a);segment(c.b);}
        if(['radius','diameter'].includes(c.type))conic(c.a);
        if(c.type==='concentric'){conic(c.a);conic(c.b);}
        if(c.type==='equal'){if(entity(c.a).center){conic(c.a);conic(c.b);}else{segment(c.a);segment(c.b);}}
        if(c.type==='tangent'){if(!entity(c.a).center&&!entity(c.b).center)throw Error('Tangency requires at least one circle or arc.');for(const r of [c.a,c.b])entity(r).center?conic(r):segment(r);}
        if(['coincident','distance','distance-x','distance-y','symmetric'].includes(c.type)){anchor(c.a);anchor(c.b);}
        if(['fixed','point-on-line','point-on-circle','midpoint'].includes(c.type))anchor(c.a);
        if(['point-on-line','midpoint'].includes(c.type))segment(c.b);
        if(c.type==='point-on-circle')conic(c.b);if(c.type==='symmetric')segment(c.c);
        if(['distance','distance-x','distance-y','radius','diameter','angle'].includes(c.type)){const v=expression(c.value,n=>{if(!pvalues.has(n))throw Error('Unknown parameter: '+n);return pvalues.get(n);});if(['distance','radius','diameter'].includes(c.type)&&v<=0)throw Error('Distance/radius/diameter must be positive.');}
        if(c.type==='fixed'&&(!c.target||c.target.length!==2))throw Error('Invalid fixed point target.');
        if(c.type==='fix-entity'){const e=entity(c.a),n=e.points?e.points.length*2:e.type==='ARC'?5:e.type==='CIRCLE'?3:2;if(!c.target||c.target.length!==n)throw Error('Invalid fixed entity target.');}

    }
}
const sub=(a,b)=>[a[0]-b[0],a[1]-b[1]],dot=(a,b)=>a[0]*b[0]+a[1]*b[1],cross=(a,b)=>a[0]*b[1]-a[1]*b[0],len=a=>Math.hypot(...a),mid=(a,b)=>[(a[0]+b[0])/2,(a[1]+b[1])/2];
function compile(doc,s){
    validate(doc.serialize());const active=s.constraints.filter(c=>!c.suppressed),used=new Set(active.flatMap(c=>keys.slice(0,TYPES[c.type]).map(k=>c[k].entity)));
    const plane=P.frame(s.plane),inverse=M.inverse(plane),normal=M.point(plane,[0,0,1],0),raw=new Map();
    const local=p=>{const q=M.point(inverse,p);if(Math.abs(q[2])>1e-6)throw Error('Constrained geometry must lie on the saved sketch plane.');return q.slice(0,2);};
    let scalarCount=0;for(const id of used){const e=doc.byId.get(id);if(!e)throw Error('Missing constrained entity.');scalarCount+=e.points?2*e.points.length:e.type==='ARC'?5:e.type==='CIRCLE'?3:2;if(scalarCount>512)throw Error('Referenced sketch geometry exceeds 512 scalars.');if(['CIRCLE','ARC'].includes(e.type)){
            const axes=K.Geo.conicAxes(e),rx=V.len(axes.x),ry=V.len(axes.y);if(Math.abs(rx-ry)>1e-7*Math.max(rx,ry)||Math.abs(V.dot(V.norm(axes.x),V.norm(axes.y)))>1e-7||Math.abs(V.dot(V.norm(V.cross(axes.x,axes.y)),normal))<1-1e-7)throw Error('Conic must be circular and parallel to the sketch plane.');
            raw.set(id,{e,values:[...local(e.center),rx,...(e.type==='ARC'?[e.startAngle||0,e.endAngle??Math.PI]:[])],axes:{x:V.norm(axes.x),y:V.norm(axes.y)}});
        }else raw.set(id,{e,values:(e.points||[e.position]).flatMap(local)});
    }
    const coords=[];for(const o of raw.values()){const n=['CIRCLE','ARC'].includes(o.e.type)?2:o.values.length;for(let i=0;i<n;i+=2)coords.push([o.values[i],o.values[i+1]]);}
    const bounds=coords.length?[Math.min(...coords.map(p=>p[0])),Math.min(...coords.map(p=>p[1])),Math.max(...coords.map(p=>p[0])),Math.max(...coords.map(p=>p[1]))]:[0,0,1,1];
    const origin=mid(bounds.slice(0,2),bounds.slice(2)),scale=Math.max(1,bounds[2]-bounds[0],bounds[3]-bounds[1],...Array.from(raw.values()).filter(o=>o.axes).map(o=>o.values[2]));
    const normalize=(o,v)=>v.map((x,i)=>o.axes?(i<2?(x-origin[i])/scale:i===2?x/scale:x):(x-origin[i%2])/scale);
    const denormalize=(o,v)=>v.map((x,i)=>o.axes?(i<2?x*scale+origin[i]:i===2?x*scale:x):x*scale+origin[i%2]);
    const fixedEntities=new Map();for(const c of active)if(c.type==='fix-entity'&&!fixedEntities.has(c.a.entity))fixedEntities.set(c.a.entity,c);
    const x=[],objects=new Map();for(const[id,o]of raw){
        const fixed=fixedEntities.get(id);if(fixed&&(!fixed.target||fixed.target.length!==o.values.length))throw Error('Fixed entity requires matching saved coordinates.');
        // Eliminate fully fixed entities from unknowns instead of allowing numerical drift.
        o.initial=normalize(o,fixed&&!doc.layer(o.e).locked?fixed.target:o.values);
        o.indices=o.initial.map(v=>doc.layer(o.e).locked||fixed?-1:x.push(v)-1);objects.set(id,o);
    }
    if(x.length>160)throw Error('Sketch limit: 160 scalar variables. Split the sketch into smaller constrained drawings.');
    const vals=(r,x)=>{const o=objects.get(r.entity);return o.indices.map((k,i)=>k<0?o.initial[i]:x[k]);};
    function point(r,x){const o=objects.get(r.entity),v=vals(r,x),p=r.point;if(o.axes){if(p==null||p==='center')return v.slice(0,2);if(o.e.type!=='ARC'||!['start','end','mid'].includes(p))throw Error('Use center for a circle, or start/end/mid for an arc.');let a=p==='start'?v[3]:p==='end'?v[4]:v[3]+K.Math.sweep(v[3],v[4])/2;const ux=M.point(inverse,o.axes.x,0),uy=M.point(inverse,o.axes.y,0);return[v[0]+v[2]*(ux[0]*Math.cos(a)+uy[0]*Math.sin(a)),v[1]+v[2]*(ux[1]*Math.cos(a)+uy[1]*Math.sin(a))];}
        if(o.e.type==='POINT'){if(p!=null&&p!=='position')throw Error('Use position for a point.');return v.slice(0,2);}const n=v.length/2,i=Number.isInteger(p)?p:p==='end'?n-1:0;if(p==='mid'){const j=r.segment||0;return mid(v.slice(j*2,j*2+2),v.slice(((j+1)%n)*2,((j+1)%n)*2+2));}if(p!=null&&!Number.isInteger(p)&&!['start','end','mid'].includes(p))throw Error('Invalid line/polyline point.');return v.slice(i*2,i*2+2);}
    function segment(r,x){const o=objects.get(r.entity);if(!['LINE','POLYLINE'].includes(o.e.type))throw Error('This constraint requires a line segment.');const v=vals(r,x),i=r.segment||0,n=v.length/2,a=v.slice(i*2,i*2+2),b=v.slice(((i+1)%n)*2,((i+1)%n)*2+2),d=sub(b,a),l=len(d);if(l<1e-10)throw Error('Constrained segment is degenerate.');return{a,b,d,l};}
    const circle=(r,x)=>{const o=objects.get(r.entity);if(!o.axes)throw Error('This constraint requires a circle or arc.');const v=vals(r,x);return{p:v.slice(0,2),r:v[2]};};
    const unitScale=P.MM[s.units||doc.units]/P.MM[doc.units];
    const valueMap=parameters(s.parameters),value=c=>expression(c.value,n=>{if(!valueMap.has(n))throw Error('Unknown parameter: '+n);return valueMap.get(n);});
    const targets=new Map();for(const c of active){if(c.type==='fixed'){if(!c.target||c.target.length!==2)throw Error('Fixed point requires a saved XY target.');targets.set(c.id,c.target.map((v,i)=>(v-origin[i])/scale));}if(c.type==='fix-entity'){const o=objects.get(c.a.entity);if(!c.target||c.target.length!==o.values.length)throw Error('Fixed entity requires matching saved coordinates.');targets.set(c.id,normalize(o,c.target));}if(['distance','radius','diameter'].includes(c.type)&&value(c)<=0)throw Error('Distance/radius/diameter must be positive.');}
    const parts=(c,x)=>{
        const a=c.a,b=c.b,p=()=>point(a,x),q=()=>point(b,x),sa=()=>segment(a,x),sb=()=>segment(b,x),ca=()=>circle(a,x),cb=()=>circle(b,x);
        switch(c.type){
        case'horizontal':return[sa().d[1]];case'vertical':return[sa().d[0]];
        case'coincident':return sub(p(),q());case'distance':return[len(sub(p(),q()))-value(c)*unitScale/scale];
        case'distance-x':return[q()[0]-p()[0]-value(c)*unitScale/scale];case'distance-y':return[q()[1]-p()[1]-value(c)*unitScale/scale];
        case'fixed':return sub(p(),targets.get(c.id));case'fix-entity':if(fixedEntities.get(a.entity)===c&&!doc.layer(objects.get(a.entity).e).locked)return[];return vals(a,x).map((v,i)=>v-targets.get(c.id)[i]);
        case'parallel':{const u=sa(),v=sb();return[cross(u.d,v.d)/(u.l*v.l)];}
        case'perpendicular':{const u=sa(),v=sb();return[dot(u.d,v.d)/(u.l*v.l)];}
        case'collinear':{const u=sa(),v=sb();return[cross(u.d,v.d)/(u.l*v.l),cross(sub(v.a,u.a),u.d)/u.l];}
        case'equal':return objects.get(a.entity).axes?[ca().r-cb().r]:[sa().l-sb().l];
        case'concentric':return sub(ca().p,cb().p);
        case'radius':return[ca().r-value(c)*unitScale/scale];case'diameter':return[ca().r*2-value(c)*unitScale/scale];
        case'angle':{const u=sa(),v=sb(),r=Math.atan2(cross(u.d,v.d),dot(u.d,v.d))-value(c)*Math.PI/180;return[Math.atan2(Math.sin(r),Math.cos(r))];}
        case'point-on-line':{const v=sb();return[cross(sub(p(),v.a),v.d)/v.l];}
        case'point-on-circle':{const v=cb();return[len(sub(p(),v.p))-v.r];}
        case'midpoint':{const v=sb();return sub(p(),mid(v.a,v.b));}
        case'symmetric':{const v=segment(c.c,x),u=p(),w=q();return[cross(sub(mid(u,w),v.a),v.d)/v.l,dot(sub(u,w),v.d)/v.l];}
        case'tangent':{if(objects.get(a.entity).axes&&objects.get(b.entity).axes){const u=ca(),v=cb();return[len(sub(u.p,v.p))-(c.internal?Math.abs(u.r-v.r):u.r+v.r)];}const line=objects.get(a.entity).axes?sb():sa(),cc=objects.get(a.entity).axes?ca():cb();return[Math.abs(cross(sub(cc.p,line.a),line.d)/line.l)-cc.r];}
        default:throw Error('Unsupported constraint '+c.type);
        }
    };
    const residual=x=>{const r=active.flatMap(c=>parts(c,x));if(r.length>512||r.some(v=>!Number.isFinite(v)))throw Error('Invalid/oversized constraint system.');return r;};
    const valid=x=>x.every(v=>Number.isFinite(v)&&Math.abs(v)<1e10)&&[...objects.values()].every(o=>!o.axes||vals({entity:o.e.id},x)[2]>1e-10);
    const apply=x=>{for(const o of objects.values()){const v=denormalize(o,vals({entity:o.e.id},x)),e=o.e;if(o.axes){e.center=M.point(plane,[v[0],v[1],0]);e.radius=v[2];e.axisX=V.mul(o.axes.x,v[2]);e.axisY=V.mul(o.axes.y,v[2]);if(e.type==='ARC'){e.startAngle=v[3];e.endAngle=v[4];}}else{const pts=[];for(let i=0;i<v.length;i+=2)pts.push(M.point(plane,[v[i],v[i+1],0]));if(e.type==='POINT')e.position=pts[0];else e.points=pts;}}doc.cache=new WeakMap();};
    return{x,residual,valid,apply,active,parts,raw,point,scale};
}
function jacobian(fn,x,r){const j=r.map(()=>Array(x.length).fill(0));for(let c=0;c<x.length;c++){const h=1e-6*Math.max(1,Math.abs(x[c])),a=x.slice(),b=x.slice();a[c]+=h;b[c]-=h;const ra=fn(a),rb=fn(b);for(let k=0;k<r.length;k++)j[k][c]=(ra[k]-rb[k])/(2*h);}return j;}
function rank(matrix,n){const a=matrix.map(r=>r.slice());let row=0;for(let col=0;col<n&&row<a.length;col++){let pivot=row;for(let k=row+1;k<a.length;k++)if(Math.abs(a[k][col])>Math.abs(a[pivot][col]))pivot=k;if(Math.abs(a[pivot][col])<1e-7)continue;[a[row],a[pivot]]=[a[pivot],a[row]];const d=a[row][col];for(let c=col;c<n;c++)a[row][c]/=d;for(let k=row+1;k<a.length;k++){const f=a[k][col];for(let c=col;c<n;c++)a[k][c]-=f*a[row][c];}row++;}return row;}
function linear(a,b){const n=b.length,a2=a.map((r,i)=>[...r,b[i]]);for(let i=0;i<n;i++){let p=i;for(let k=i+1;k<n;k++)if(Math.abs(a2[k][i])>Math.abs(a2[p][i]))p=k;if(Math.abs(a2[p][i])<1e-20)return null;[a2[i],a2[p]]=[a2[p],a2[i]];for(let k=i+1;k<n;k++){const f=a2[k][i]/a2[i][i];for(let j=i;j<=n;j++)a2[k][j]-=f*a2[i][j];}}const x=Array(n).fill(0);for(let i=n-1;i>=0;i--){let v=a2[i][n];for(let j=i+1;j<n;j++)v-=a2[i][j]*x[j];x[i]=v/a2[i][i];}return x;}
function solve(doc,{apply=true,maxIterations=80,timeLimit=1500}={}){
    const s=state(doc);if(!s||!s.enabled||!s.constraints.some(c=>!c.suppressed))return{converged:true,status:'inactive',variables:0,equations:0,rank:0,degreesOfFreedom:0,redundantEquations:0};
    const t=Date.now(),sys=compile(doc,s),n=sys.x.length;let x=sys.x.slice(),r=sys.residual(x),error=r.reduce((a,v)=>a+v*v,0),lambda=1e-3,iteration=0;
    const max=r=>Math.max(0,...r.map(Math.abs));
    for(;iteration<Math.min(200,maxIterations)&&max(r)>1e-8&&n;iteration++){
        if(Date.now()-t>timeLimit)break;const j=jacobian(sys.residual,x,r),a=Array.from({length:n},()=>Array(n).fill(0)),b=Array(n).fill(0);
        for(let k=0;k<r.length;k++)for(let u=0;u<n;u++){b[u]-=j[k][u]*r[k];for(let v=0;v<=u;v++)a[u][v]+=j[k][u]*j[k][v];}
        if(iteration===0&&Math.max(0,...b.map(Math.abs))<1e-15&&error>1e-16){
            // At coincident points a distance norm has zero central derivative. A small,
            // deterministic seed selects a local branch without changing the source on failure.
            const seeded=x.map((v,i)=>v+1e-4*Math.sin((i+1)*2.399963));
            if(sys.valid(seeded)){x=seeded;r=sys.residual(x);error=r.reduce((a,v)=>a+v*v,0);continue;}
        }
        for(let u=0;u<n;u++){for(let v=0;v<u;v++)a[v][u]=a[u][v];a[u][u]+=lambda*(a[u][u]+1e-3);}
        const dx=linear(a,b);if(!dx)break;const next=x.map((v,i)=>v+dx[i]);let nr=null,ne=Infinity;try{if(sys.valid(next)){nr=sys.residual(next);ne=nr.reduce((a,v)=>a+v*v,0);}}catch(_){}
        if(ne<error){x=next;r=nr;error=ne;lambda=Math.max(1e-12,lambda/3);}else lambda=Math.min(1e12,lambda*10);
    }
    const converged=max(r)<=1e-8&&sys.valid(x),j=jacobian(sys.residual,x,r),rk=rank(j,n);
    const report={converged,status:converged?(n-rk===0?'fully-constrained':'under-constrained'):'failed-to-converge',variables:n,equations:r.length,rank:rk,degreesOfFreedom:n-rk,redundantEquations:r.length-rk,maxResidual:max(r),iterations:iteration,conflicts:sys.active.map(c=>({id:c.id,type:c.type,residual:max(sys.parts(c,x))})).filter(c=>c.residual>1e-8).sort((a,b)=>b.residual-a.residual)};
    if(converged&&apply)sys.apply(x);return report;
}
function enforce(doc){const report=solve(doc);doc.constraintReport=report;if(!report.converged)throw Error('Constraint solve failed; edit rolled back. Check '+report.conflicts.slice(0,4).map(c=>c.id+' ('+c.type+')').join(', ')+'.');}
function add(doc,spec){return doc.transaction('Add '+spec.type+' constraint',()=>{const s=state(doc,true),c={id:K.uid('constraint'),...K.clone(spec)};if(!own(TYPES,c.type))throw Error('Unknown constraint.');if(['fixed','fix-entity'].includes(c.type)&&!c.target){const probe=K.clone(c);probe.type=c.type==='fixed'?'coincident':'horizontal';if(probe.type==='coincident')probe.b=probe.a;const temp={...s,constraints:[probe]};const sys=compile(doc,temp),o=sys.raw.get(c.a.entity);if(c.type==='fix-entity')c.target=o.values.slice();if(c.type==='fixed'){const inv=M.inverse(P.frame(s.plane)),e=doc.byId.get(c.a.entity);let p;if(e.center){p=c.a.point&&c.a.point!=='center'?K.Geo.conicPoint(e,c.a.point==='end'?e.endAngle:c.a.point==='mid'?(e.startAngle||0)+K.Math.sweep(e.startAngle||0,e.endAngle)/2:e.startAngle||0):e.center;}else if(e.position)p=e.position;else p=c.a.point==='mid'?V.lerp(e.points[c.a.segment||0],e.points[((c.a.segment||0)+1)%e.points.length],.5):e.points[c.a.point==='end'?e.points.length-1:Number.isInteger(c.a.point)?c.a.point:0];c.target=M.point(inv,p).slice(0,2);}}
        s.constraints.push(c);
    });}
function convertUnits(doc,newUnits,factor,convert){
    const s=state(doc);if(!s)return;
    if(!own(P.MM,newUnits)||!Number.isFinite(factor)||factor<=0)throw Error('Invalid unit conversion.');
    if(!s.units)s.units=doc.units;
    if(!convert){s.units=newUnits;return;}
    s.plane.origin=s.plane.origin.map(v=>v*factor);
    for(const c of s.constraints){if(!c.target)continue;const e=doc.byId.get(c.a.entity);c.target=c.target.map((v,i)=>c.type==='fix-entity'&&e.type==='ARC'&&i>=3?v:v*factor);}
}

function rewrite(source,names){if(typeof source!=='string')return source;return source.replace(/(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?|[A-Za-z][A-Za-z0-9_]*/g,t=>names.get(t)||t);}
function copyInto(doc,data,ids,matrix){
    const source=data.production?.parametric;if(!source)return[];
    const warnings=[],available=source.constraints.filter(c=>keys.slice(0,TYPES[c.type]).every(k=>ids.has(c[k].entity)));
    const relevant=source.constraints.filter(c=>keys.slice(0,TYPES[c.type]).some(k=>ids.has(c[k].entity)));
    if(available.length<relevant.length)warnings.push('Constraints to objects outside the copied selection were not copied.');
    if(!available.length)return warnings;
    const transformed=M.multiply(matrix,P.frame(source.plane)),ux=M.point(transformed,[1,0,0],0),uy=M.point(transformed,[0,1,0],0),scale=V.len(ux),org=M.point(transformed,[0,0,0]);
    if(!scale||Math.abs(V.len(uy)-scale)>1e-7*scale||Math.abs(V.dot(ux,uy))>1e-7*scale*scale){warnings.push('Nonuniform/skewed copies retain geometry without sketch constraints.');return warnings;}
    let dest=state(doc);const newPlane={origin:org,x:V.norm(ux),y:V.norm(uy)};
    if(!dest){dest={enabled:source.enabled,units:source.units||data.units,plane:newPlane,parameters:[],constraints:[]};P.ensure(doc).parametric=dest;}
    const inv=M.inverse(P.frame(dest.plane)),dx=M.point(inv,V.norm(ux),0),dy=M.point(inv,V.norm(uy),0),qorg=M.point(inv,org);
    if(V.dist(dx,[1,0,0])>1e-7||V.dist(dy,[0,1,0])>1e-7||Math.abs(qorg[2])>1e-6){warnings.push('Copied sketch plane differs from the destination sketch; geometry copied without constraints.');return warnings;}
    const names=new Map(),existing=new Set(dest.parameters.map(r=>r.name));
    for(const r of source.parameters){let name=r.name,i=1;while(existing.has(name))name=r.name.slice(0,36)+'_copy'+i++;existing.add(name);names.set(r.name,name);}
    for(const r of source.parameters)dest.parameters.push({name:names.get(r.name),expression:rewrite(r.expression,names)});
    const factor=scale*P.MM[doc.units]/P.MM[data.units]*P.MM[source.units||data.units]/P.MM[dest.units||doc.units],sourceById=new Map(data.entities.map(e=>[e.id,e]));
    for(const original of available){const c=K.clone(original);c.id=K.uid('constraint');for(const k of keys)if(c[k])c[k].entity=ids.get(c[k].entity);
        if(c.value!=null){c.value=rewrite(c.value,names);if(['distance','distance-x','distance-y','radius','diameter'].includes(c.type)&&Math.abs(factor-1)>1e-12)c.value=`(${c.value}) * ${factor}`;}
        if(c.target){const e=sourceById.get(original.a.entity),v=c.target;const p=(x,y)=>M.point(inv,M.point(transformed,[x,y,0])).slice(0,2);
            if(c.type==='fixed')c.target=p(v[0],v[1]);
            else if(e.center)c.target=[...p(v[0],v[1]),v[2]*scale,...v.slice(3)];
            else {c.target=[];for(let i=0;i<v.length;i+=2)c.target.push(...p(v[i],v[i+1]));}
        }dest.constraints.push(c);
    }
    return warnings;
}

function removeReferences(doc,ids){const s=state(doc);if(s){const removed=new Set(ids);s.constraints=s.constraints.filter(c=>!keys.some(k=>c[k]&&removed.has(c[k].entity)));}}
K.Expressions={evaluate:expression,parameters,identifier};K.Constraints={TYPES,state,validate,solve,enforce,add,convertUnits,copyInto,removeReferences};
})(globalThis);
