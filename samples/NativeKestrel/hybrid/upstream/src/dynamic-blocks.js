/* Kestrel CAD — declarative, per-instance dynamic blocks. No eval or remote code. */
(function (root) {
    'use strict';
    const K = root.Kestrel, P = K.Production, G = K.Geo, {V, M} = K.Math;
    const clone = K.clone, LIMIT = 20000;
    const PARAM_TYPES = ['number', 'length', 'angle', 'integer', 'boolean', 'enum'];
    const ACTION_TYPES = ['move', 'stretch', 'rotate', 'scale', 'flip', 'array', 'polar-array', 'polar-stretch', 'visibility'];
    const own = (o, k) => Object.prototype.hasOwnProperty.call(o, k);
    const object = (v, label) => {
        if (!v || typeof v !== 'object' || Array.isArray(v) ||
            ![Object.prototype, null].includes(Object.getPrototypeOf(v))) throw Error('Invalid ' + label + '.');
        for (const k of Object.keys(v)) if (['__proto__', 'constructor', 'prototype'].includes(k)) throw Error('Unsafe ' + label + ' key.');
        return v;
    };
    const identifier = s => {
        if (typeof s !== 'string' || !/^[A-Za-z][A-Za-z0-9_]{0,47}$/.test(s)) throw Error('Invalid parameter name.');
        // Reuse the same safe expression-name validation as driving sketches.
        K.Expressions.parameters([{name:s, expression:'1'}]); return s;
    };
    const text = s => typeof s === 'string' && s.length <= 512;
    const scalar = (p, v) => {
        if (p.type === 'boolean') { if (typeof v !== 'boolean') throw Error(p.name + ' must be a boolean.'); }
        else if (p.type === 'enum') {
            if (typeof v !== 'string' || !p.values.includes(v)) throw Error('Unknown value for ' + p.name + '.');
        } else {
            P.num(v, p.name, p.type === 'length' ? 1e-7 : -1e9, 1e9);
            if (p.type === 'integer' && !Number.isInteger(v)) throw Error(p.name + ' must be an integer.');
            if (p.min != null && v < p.min || p.max != null && v > p.max) throw Error(p.name + ' is outside its allowed range.');
            if (p.values && !p.values.some(n => Math.abs(n-v) <= Math.max(1,Math.abs(v))*1e-9)) throw Error(p.name + ' is not an allowed value.');
            if (p.step != null) {
                const n = (v - (p.min ?? p.default)) / p.step;
                if (Math.abs(n-Math.round(n)) > Math.max(1,Math.abs(n))*1e-9) throw Error(p.name + ' does not match its increment.');
            }
        }
        return v;
    };
    function schema(block) {
        const d = object(block.dynamic, 'dynamic block');
        if (d.version !== 1 || !Array.isArray(d.parameters) || !d.parameters.length || d.parameters.length > 64 ||
            !Array.isArray(d.actions) || d.actions.length > 128) throw Error('Invalid dynamic block schema or limits.');
        const names = new Map(), driven = new Set();
        for (const p of d.parameters) {
            object(p,'parameter'); identifier(p.name);
            if (names.has(p.name) || !PARAM_TYPES.includes(p.type)) throw Error('Duplicate parameter or unsupported parameter type.');
            if (p.type === 'enum') {
                if (!Array.isArray(p.values) || !p.values.length || p.values.length > 256 ||
                    p.values.some(v => !text(v) || !v.length) || new Set(p.values).size !== p.values.length) throw Error('Invalid enum values.');
            } else if (p.values != null) {
                if (!Array.isArray(p.values) || !p.values.length || p.values.length > 256) throw Error('Invalid parameter value set.');
                p.values.forEach(v => P.num(v));
            }
            if (p.min != null) P.num(p.min); if (p.max != null) P.num(p.max);
            if (p.min != null && p.max != null && p.min > p.max) throw Error('Parameter minimum exceeds maximum.');
            if (p.step != null) P.num(p.step, 'Parameter increment', 1e-9, 1e9);
            if (p.expression != null) {
                if (!text(p.expression) || ['enum','boolean'].includes(p.type)) throw Error('Only numeric parameters may have expressions.');
                driven.add(p.name);
            }
            scalar(p,p.default); names.set(p.name,p);
        }
        const lookups = d.lookups || [];
        if (!Array.isArray(lookups) || lookups.length > 32) throw Error('Invalid lookup tables.');
        const lookupNames = new Set();
        for (const lookup of lookups) {
            object(lookup,'lookup table'); const p=names.get(lookup.parameter);
            if (!p || p.type !== 'enum' || lookupNames.has(p.name) || !Array.isArray(lookup.rows) || lookup.rows.length !== p.values.length) throw Error('Lookup requires one row for each enum value.');
            lookupNames.add(p.name); const rows=new Set(),outputs=new Set();
            for (const row of lookup.rows) {
                if (!p.values.includes(row.value) || rows.has(row.value)) throw Error('Duplicate or unknown lookup row.');
                rows.add(row.value); object(row.set,'lookup outputs');
                for (const [key,value] of Object.entries(row.set)) {
                    const target=names.get(key);
                    if (!target || target.expression != null || target.type==='enum') throw Error('Lookup output must be a non-derived numeric or boolean parameter.');
                    scalar(target,value); outputs.add(key);
                }
            }
            // Every row sets the same outputs; no history-dependent leftovers.
            for (const row of lookup.rows) if (Object.keys(row.set).length !== outputs.size) throw Error('Every lookup row must set the same outputs.');
            for (const key of outputs) { if (driven.has(key)) throw Error('Multiple drivers for parameter '+key+'.'); driven.add(key); }
        }
        const ids = new Set(block.entities.map(e=>e.id));
        for (const a of block.attributes || []) ids.add('attribute:'+a.tag);
        if (ids.size !== block.entities.length+(block.attributes||[]).length) throw Error('Ambiguous dynamic geometry identifiers.');
        function targets(list) {
            if (!Array.isArray(list) || !list.length || list.length > 20000 || new Set(list).size !== list.length || list.some(id=>!ids.has(id))) throw Error('Action targets must reference distinct definition entities or attribute:TAG.');
        }
        const formula = v => { if (typeof v === 'number') P.num(v,'Action expression',-1e9,1e9); else if (!text(v)) throw Error('Invalid action expression.'); };
        for (const a of d.actions) {
            object(a,'dynamic action'); if (!ACTION_TYPES.includes(a.type)) throw Error('Unknown dynamic action: '+a.type);
            if (a.type==='visibility') {
                const p=names.get(a.parameter);object(a.states,'visibility states');
                if (!p || p.type!=='enum' || Object.keys(a.states).length!==p.values.length) throw Error('Visibility requires complete enum states.');
                for (const state of p.values) {
                    if (!own(a.states,state) || !Array.isArray(a.states[state])) throw Error('Missing visibility state.');
                    if (a.states[state].length) targets(a.states[state]);
                }
                if (a.targets) {targets(a.targets);for(const list of Object.values(a.states))if(list.some(id=>!a.targets.includes(id)))throw Error('Visibility state is outside its target scope.');}
                continue;
            }
            targets(a.targets);
            if (a.type==='move'||a.type==='stretch') for (const k of ['x','y','z']) formula(a[k]??0);
            if (a.type==='stretch') {
                P.point(a.min);P.point(a.max);
                if(a.min.some((v,i)=>v>a.max[i]))throw Error('Stretch minimum exceeds maximum.');
            }
            if (a.type==='rotate') {formula(a.angle);P.point(a.center||[0,0,0]);P.point(a.axis||[0,0,1]);if(V.len(a.axis||[0,0,1])<1e-9)throw Error('Zero rotation axis.');}
            if (a.type==='scale') {formula(a.factor);P.point(a.center||[0,0,0]);}
            if (a.type==='flip') {if(names.get(a.parameter)?.type!=='boolean')throw Error('Flip needs a boolean parameter.');P.point(a.origin||[0,0,0]);P.point(a.normal||[1,0,0]);if(V.len(a.normal||[1,0,0])<1e-9)throw Error('Zero reflection normal.');}
            if (a.type==='array') for (const k of ['columns','rows','dx','dy']) formula(a[k]??(k==='rows'?1:0));
            if (a.type==='polar-stretch') {
                formula(a.length);formula(a.angle??0);formula(a.multiplier??1);
                P.num(a.baseLength,'Base polar length',1e-7,1e9);
                P.point(a.center||[0,0,0]);P.point(a.axis||[0,0,1]);P.point(a.direction||[1,0,0]);
                if(V.len(a.axis||[0,0,1])<1e-7 || V.len(a.direction||[1,0,0])<1e-7)
                    throw Error('Polar stretch axis and reference direction must be nonzero.');
                if(Math.abs(V.dot(V.norm(a.axis||[0,0,1]),V.norm(a.direction||[1,0,0])))>1e-9)
                    throw Error('Polar stretch reference direction must be perpendicular to its rotation axis.');
                P.point(a.min);P.point(a.max);
                if(a.min.some((v,i)=>v>a.max[i]))throw Error('Stretch minimum exceeds maximum.');
                for(const key of ['rotateOnly','moveOnly'])if(a[key]!=null){
                    if(!Array.isArray(a[key]))throw Error('Invalid polar stretch member modes.');
                    if(a[key].length)targets(a[key]);
                    if(a[key].some(id=>!a.targets.includes(id)))throw Error('Polar member mode is outside the action target set.');
                }
                if((a.rotateOnly||[]).some(id=>(a.moveOnly||[]).includes(id)))throw Error('A polar member cannot both move whole and rotate only.');
            }
            if (a.type==='polar-array') {
                formula(a.count); formula(a.angle??360);
                P.point(a.center||[0,0,0]); P.point(a.axis||[0,0,1]);
                if (V.len(a.axis||[0,0,1])<1e-9) throw Error('Zero polar array axis.');
                if (a.rotateItems!=null && typeof a.rotateItems!=='boolean') throw Error('Rotate items must be a boolean.');
                if (a.base!=null || a.rotateItems===false) P.point(a.base);
            }
        }
        return {d,names,driven};
    }
    // Classify conics analytically: display samples are not safe stretch boundaries.
    function conicFrame(e, inside, lo, hi) {
        const {x,y}=G.conicAxes(e), start=e.startAngle||0;
        const span=e.type==='CIRCLE'||e.endAngle==null ? K.Math.TAU : K.Math.sweep(start,e.endAngle);
        const times=[0,span], add=t=>{const d=K.Math.angle(t-start);if(d<=span+1e-10)times.push(Math.min(d,span));};
        for(let i=0;i<3;i++) {
            const radius=Math.hypot(x[i],y[i]);
            if(radius<1e-15)continue;
            const phase=Math.atan2(y[i],x[i]);add(phase);add(phase+Math.PI);
            for(const bound of [lo[i],hi[i]]) {
                const q=(bound-e.center[i])/radius;
                if(q>=-1 && q<=1){const a=Math.acos(q);add(phase+a);add(phase-a);}
            }
        }
        times.sort((a,b)=>a-b);
        const points=times.map(t=>G.conicPoint(e,start+t));
        // Midpoints classify spans between all exact frame crossings, including
        // a conic surrounding the frame without actually entering it.
        for(let i=1;i<times.length;i++)points.push(G.conicPoint(e,start+(times[i-1]+times[i])/2));
        return {all:points.every(inside), any:points.some(inside)};
    }
    function polarStretch(e,a,delta,transform) {
        const inside=p=>p.every((v,i)=>v>=a.min[i]-1e-9 && v<=a.max[i]+1e-9);
        const translate=()=>transform(e,M.translation(...delta));
        if(e.type==='LINE'||e.type==='POLYLINE') {
            const flags=e.points.map(inside);
            // A curved segment can move rigidly, but moving only one endpoint
            // would require a different arc definition, not the old bulge.
            for(let i=0;i<e.points.length-(e.closed?0:1);i++)
                if(Math.abs(e.bulges?.[i]||0)>1e-12 && flags[i]!==flags[(i+1)%flags.length])
                    throw Error('Polar stretch cannot partially deform a bulged segment. Choose Move whole or Rotate only.');
            return {...e,points:e.points.map((p,i)=>flags[i]?V.add(p,delta):p)};
        }
        if(['POINT','TEXT','MTEXT','INSERT'].includes(e.type)) {
            const anchor=e.type==='INSERT'?M.point(e.matrix,[0,0,0]):e.position;
            return inside(anchor)?translate():e;
        }
        let classification;
        if(['CIRCLE','ARC','ELLIPSE'].includes(e.type))classification=conicFrame(e,inside,a.min,a.max);
        else if(e.type==='MESH' && !e.solid) {
            // A convex box containing every vertex contains every mesh face.
            // Otherwise refuse deformation when its bounds overlap the frame.
            const all=e.vertices.every(inside);
            const separated=[0,1,2].some(i=>e.vertices.every(p=>p[i]<a.min[i]-1e-9)||e.vertices.every(p=>p[i]>a.max[i]+1e-9));
            classification={all,any:!separated};
        } else throw Error('Polar stretch requires an explicit Move whole or Rotate only mode for '+(e.solid?'native B-rep':e.type)+'.');
        if(classification.all)return translate();
        if(classification.any)throw Error('Polar stretch cannot partially deform '+e.type+'. Choose Move whole or Rotate only.');
        return e;
    }
    function resolve(block, overrides={}) {
        const {d,names,driven}=schema(block);object(overrides,'instance parameter values');
        for (const key of Object.keys(overrides)) if (!names.has(key) || driven.has(key)) throw Error('Unknown or driven instance parameter: '+key);
        const values=Object.create(null);
        for (const p of d.parameters) values[p.name]=scalar(p,own(overrides,p.name)?overrides[p.name]:p.default);
        for (const t of d.lookups||[]) Object.assign(values,t.rows.find(r=>r.value===values[t.parameter]).set);
        const pending=new Set(),done=new Set();
        function get(name) {
            const p=names.get(name);if(!p)throw Error('Unknown block parameter: '+name);
            if(p.expression!=null && !done.has(name)) {
                if(pending.has(name))throw Error('Cyclic block parameter expression.');
                pending.add(name);values[name]=scalar(p,K.Expressions.evaluate(p.expression,get));pending.delete(name);done.add(name);
            }
            const v=values[name];if(typeof v==='string')throw Error('Enum parameter cannot be used as a number: '+name);
            return typeof v==='boolean'?Number(v):v;
        }
        for (const p of d.parameters) if(p.expression!=null)get(p.name);
        return {values,driven,expression:source=>P.num(typeof source==='number'?source:K.Expressions.evaluate(String(source??0),get),'Action result',-1e9,1e9)};
    }
    // Inverse matching never changes a lookup's outputs independently of its selector.
    // Return all matches so callers can report ambiguity instead of picking row order.
    function lookupMatches(block, parameter, criteria) {
        const {d,names}=schema(block),table=(d.lookups||[]).find(t=>t.parameter===parameter);
        if(!table) throw Error('Missing lookup table: '+parameter);
        object(criteria,'lookup criteria');
        const keys=Object.keys(criteria),columns=Object.keys(table.rows[0].set);
        if(!keys.length || keys.some(k=>!columns.includes(k))) throw Error('Supply one or more declared lookup output properties.');
        for(const key of keys) scalar(names.get(key),criteria[key]);
        const equal=(a,b)=>typeof a==='number'&&typeof b==='number'
            ? Math.abs(a-b)<=16*Number.EPSILON*Math.max(1,Math.abs(a),Math.abs(b)) : a===b;
        return table.rows.filter(row=>keys.every(key=>equal(row.set[key],criteria[key]))).map(clone);
    }
    function setLookupValues(doc,id,parameter,criteria) {
        const e=doc.byId.get(id);
        if(!e || e.type!=='INSERT' || !doc.editable(e)) throw Error('Select an editable block instance.');
        const b=P.ensure(doc).blocks.find(b=>b.id===e.block);
        if(!b?.dynamic) throw Error('This block has no dynamic definition.');
        const matches=lookupMatches(b,parameter,criteria);
        if(!matches.length) throw Error('No lookup row matches these properties. No values were changed.');
        if(matches.length!==1) throw Error('Ambiguous lookup: '+matches.length+' rows match. Supply additional properties.');
        setValues(doc,id,{[parameter]:matches[0].value});
        return matches[0].value;
    }
    function interpolate(value, values) {
        return String(value).replace(/\$\{([A-Za-z][A-Za-z0-9_]*)\}/g,(_,name)=>{
            if(!own(values,name))throw Error('Text references unknown block parameter: '+name);
            return String(values[name]);
        });
    }
    function evaluate(block, overrides={}, doc=null, instance=null) {
        const {values,expression:f}=resolve(block,overrides), d=block.dynamic;
        const transform=(e,m)=>{if(doc)P.owners.set(e,doc);const out=G.transform(e,m);if(doc)P.owners.set(out,doc);return out;};
        if(block.entities.length+(instance?(block.attributes||[]).length:0)>LIMIT)throw Error("Dynamic block expansion exceeds 20,000 entities.");
        let rows=block.entities.map(e=>({e:clone(e),source:e.id}));
        if(instance)for(const a of block.attributes||[])if(!a.hidden)rows.push({source:'attribute:'+a.tag,e:{...clone(a),id:'attribute:'+a.tag,type:'TEXT',position:clone(a.position),height:a.height,rotation:a.rotation||0,text:instance.attributes?.[a.tag]??a.value,attributeTag:a.tag,layer:'0',color:'byblock'}});
        // Actions are ordered and always start from the definition, never last frame.
        for (let actionIndex=0;actionIndex<d.actions.length;actionIndex++) {
            const a=d.actions[actionIndex];
            if(a.type==='visibility') {
                const scope=new Set(a.targets||Object.values(a.states).flat()),visible=new Set(a.states[values[a.parameter]]);
                rows=rows.filter(r=>!scope.has(r.source)||visible.has(r.source));continue;
            }
            const selected=new Set(a.targets),delta=['x','y','z'].map(k=>f(a[k]??0));let matrix;
            if(a.type==='move')matrix=M.translation(...delta);
            if(a.type==='rotate')matrix=M.around(a.center||[0,0,0],M.rotation(f(a.angle)*Math.PI/180,a.axis||[0,0,1]));
            if(a.type==='scale') {const factor=f(a.factor);P.num(factor,'Scale factor',1e-7,1e7);matrix=M.around(a.center||[0,0,0],M.scale(factor));}
            if(a.type==='flip') {
                if(!values[a.parameter])continue;
                const n=V.norm(a.normal||[1,0,0]),m=M.identity();
                for(let col=0;col<3;col++)for(let row=0;row<3;row++)m[col*4+row]-=2*n[col]*n[row];
                matrix=M.around(a.origin||[0,0,0],m);
            }
            if(matrix) {rows=rows.map(r=>selected.has(r.source)?{...r,e:transform(r.e,matrix)}:r);continue;}
            if(a.type==='stretch') {
                const inside=p=>p.every((v,i)=>v>=a.min[i]-1e-9&&v<=a.max[i]+1e-9);
                rows=rows.map(r=>{
                    if(!selected.has(r.source))return r;
                    const e=r.e;
                    if(e.type==='LINE'||e.type==='POLYLINE') {
                        if((e.bulges||[]).some(v=>Math.abs(v)>1e-12))throw Error('Partial dynamic stretch requires straight polyline segments.');
                        return {...r,e:{...e,points:e.points.map(p=>inside(p)?V.add(p,delta):p)}};
                    }
                    if(e.type==='POINT'||e.type==='TEXT'||e.type==='INSERT') {
                        const p=e.type==='INSERT'?M.point(e.matrix,[0,0,0]):e.position;
                        return inside(p)?{...r,e:transform(e,M.translation(...delta))}:r;
                    }
                    const points=G.geometry(e).points||[];
                    if(points.length&&points.every(inside))return {...r,e:transform(e,M.translation(...delta))};
                    if(points.some(inside))throw Error('Cannot partially stretch '+e.type+'. Use a move, scale or rotate action.');
                    return r;
                });
            }
            if(a.type==='polar-stretch') {
                const length=P.num(f(a.length),'Polar length',1e-7,1e9);
                const offset=(length-a.baseLength)*f(a.multiplier??1);
                P.num(offset,'Polar displacement',-1e9,1e9);
                const delta=V.mul(V.norm(a.direction||[1,0,0]),offset);
                const rotation=M.around(a.center||[0,0,0],M.rotation(f(a.angle??0)*Math.PI/180,a.axis||[0,0,1]));
                const rotateOnly=new Set(a.rotateOnly||[]),moveOnly=new Set(a.moveOnly||[]);
                rows=rows.map(r=>{
                    if(!selected.has(r.source))return r;
                    // Membership is in the incoming action-stage frame. Apply
                    // translation first, then rotate it into the new ray direction.
                    let e=r.e;
                    if(offset!==0 && !rotateOnly.has(r.source))
                        e=moveOnly.has(r.source)?transform(e,M.translation(...delta)):polarStretch(e,a,delta,transform);
                    return {...r,e:transform(e,rotation)};
                });
            }
            if(a.type==='polar-array') {
                const count=f(a.count),angle=f(a.angle??360),center=a.center||[0,0,0],axis=a.axis||[0,0,1];
                if(!Number.isInteger(count)||count<1||count>LIMIT) throw Error('Polar array count requires a positive integer within the 20,000-item limit.');
                if(Math.abs(angle)<1e-9||Math.abs(angle)>360) throw Error('Polar fill angle must be nonzero and within -360 to 360 degrees.');
                const source=rows.filter(r=>selected.has(r.source));
                if(rows.length+source.length*(count-1)>LIMIT) throw Error('Dynamic block expansion exceeds 20,000 entities.');
                // A full turn omits the duplicate endpoint. A partial sweep includes it.
                const step=angle*Math.PI/180/(Math.abs(angle)===360?count:Math.max(1,count-1)),extra=[];
                for(let i=1;i<count;i++) {
                    const rotation=M.around(center,M.rotation(step*i,axis));
                    const matrix=a.rotateItems===false?M.translation(...V.sub(M.point(rotation,a.base),a.base)):rotation;
                    for(const r of source) {
                        const e=transform(r.e,matrix); e.id='dynp'+actionIndex+'_'+i+'_'+e.id;
                        if(e.id.length>128) throw Error('Chained arrays exceed identifier depth.');
                        extra.push({...r,e});
                    }
                }
                rows.push(...extra);
            }
            if(a.type==='array') {
                const columns=f(a.columns), countRows=f(a.rows??1),dx=f(a.dx??0),dy=f(a.dy??0);
                if(!Number.isInteger(columns)||!Number.isInteger(countRows)||columns<1||countRows<1||columns*countRows>LIMIT)throw Error('Array counts require positive integers within the 20,000-item limit.');
                const source=rows.filter(r=>selected.has(r.source));
                if(rows.length+source.length*(columns*countRows-1)>LIMIT)throw Error('Dynamic block expansion exceeds 20,000 entities.');
                const extra=[];for(let y=0;y<countRows;y++)for(let x=0;x<columns;x++)if(x||y)for(const r of source){const e=transform(r.e,M.translation(x*dx,y*dy,0));e.id='dyn'+actionIndex+'_'+y+'_'+x+'_'+e.id;if(e.id.length>128)throw Error('Chained arrays exceed identifier depth.');extra.push({...r,e});}
                rows.push(...extra);
            }
        }
        if(rows.length>LIMIT)throw Error('Dynamic block expansion exceeds 20,000 entities.');
        if(new Set(rows.map(r=>r.e.id)).size!==rows.length) throw Error('Dynamic array output identifiers collide with existing geometry.');
        return rows.map(r=>{if(r.e.type==='TEXT'||r.e.type==='LEADER')r.e.text=interpolate(r.e.text,values);return r.e;});
    }
    function validate(doc) {
        const blocks=doc.production?.blocks||[];
        for(const b of blocks)if(b.dynamic){resolve(b);K.validateProject({...doc,production:undefined,entities:evaluate(b)},true);}
        for(const e of [...(doc.entities||[]),...blocks.flatMap(b=>b.entities)])if(e.type==='INSERT'){
            const b=blocks.find(b=>b.id===e.block);
            if(b?.dynamic) {
                const geometry=evaluate(b,e.parameters||{},doc,e);
                // Reuse native geometry safety checks for every actually evaluated variant.
                K.validateProject({...doc,production:undefined,entities:geometry},true);
            } else if(e.parameters!=null && Object.keys(object(e.parameters,'instance parameter values')).length)throw Error('Parameters require a dynamic block definition.');
        }
    }
    function setDefinition(doc, id, definition) {
        doc.transaction('Edit dynamic block definition',()=>{
            const b=P.ensure(doc).blocks.find(b=>b.id===id);if(!b)throw Error('Missing block definition.');
            if(definition==null) {
                for(const e of [...doc.entities,...doc.production.blocks.flatMap(b=>b.entities)])if(e.block===id)delete e.parameters;
                delete b.dynamic;
            } else {
                // Validate before JSON cloning can erase Infinity/NaN or unsupported values.
                schema({...b,dynamic:definition}); b.dynamic=clone(definition);
            }
        });
    }
    function setValues(doc,id,values,replace=false) {
        doc.transaction(replace&&!Object.keys(values).length?'Reset dynamic block':'Edit dynamic block parameters',()=>{
            const e=doc.byId.get(id);if(!e||e.type!=='INSERT'||!doc.editable(e))throw Error('Select an editable block instance.');
            const b=P.ensure(doc).blocks.find(b=>b.id===e.block);if(!b?.dynamic)throw Error('This block has no dynamic definition.');
            const parameters=replace?clone(values):{...e.parameters,...clone(values)};resolve(b,parameters);
            doc.replace(id,{...e,parameters});
        });
    }
    function exportData(data) {
        const doc=data instanceof K.Drawing?data:K.Drawing.from(data);
        if(!doc.production.blocks.some(b=>b.dynamic))return data;
        const result=doc.serialize(),original=new Map(doc.production.blocks.map(b=>[b.id,b])),variants=[],names=new Set(result.production.blocks.map(b=>b.name.toLowerCase()));let serial=0;
        function bake(e,depth=0) {
            if(e.type!=='INSERT')return clone(e);const b=original.get(e.block);
            if(!b?.dynamic)return clone(e);if(depth>=16)throw Error('Dynamic export nesting limit.');
            const children=evaluate(b,e.parameters||{},doc,e).map(c=>bake(c,depth+1));let label;
            do{label='KESTREL_EVAL_'+(++serial);}while(names.has(label.toLowerCase()));names.add(label.toLowerCase());
            const id=K.uid('eval');variants.push({id,name:label,entities:children,attributes:[]});
            const out={...clone(e),block:id,attributes:{}};delete out.parameters;return out;
        }
        for(const b of result.production.blocks){if(b.dynamic){b.entities=evaluate(original.get(b.id),{},doc,{attributes:{}});b.attributes=[];}b.entities=b.entities.map(e=>bake(e));delete b.dynamic;}
        result.entities=result.entities.map(e=>bake(e));result.production.blocks.push(...variants);
        return result;
    }
    const oldValidate=P.validate, oldWrite=K.Exchange.writeDXF;
    P.validate=function(data){oldValidate(data);validate(data);};
    K.Exchange.writeDXF=function(data){return oldWrite(exportData(data));};
    K.DynamicBlocks={PARAM_TYPES,ACTION_TYPES,schema,resolve,lookupMatches,setLookupValues,evaluate,validate,setDefinition,setValues,exportData,LIMIT};
})(typeof window!=='undefined'?window:globalThis);
