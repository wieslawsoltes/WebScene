/* Kestrel CAD — native associative annotation fields. Original MIT implementation.
 * Declarative references only. Never execute imported DXF/DWG field expressions.
 * Cached text remains readable in applications without the native field evaluator.
 */
(function(root) {
'use strict';
const K=root.Kestrel,P=K.Production,{V,M}=K.Math,own=(o,k)=>Object.prototype.hasOwnProperty.call(o,k);
const LIMITS=Object.freeze({bindings:4096,perEntity:2048,sources:32,depth:32,output:2000000,properties:128,tableRows:250});
const HOSTS=new Set(['TEXT','MTEXT','LEADER','TABLE','INSERT']);
const PROPERTIES=Object.freeze(['id','type','layer','block','length','area','volume','surfaceArea','radius','diameter','x','y','z','text','attribute','cell']);
const DRAWING=Object.freeze(['name','units','entityCount']);
const TOKENS=/\{\{([A-Za-z][A-Za-z0-9_]*)\}\}/g;
const validId=v=>typeof v==='string'&&/^[-\w.:]{1,128}$/.test(v);
const plainObject=v=>v&&typeof v==='object'&&!Array.isArray(v)&&[Object.prototype,null].includes(Object.getPrototypeOf(v));
const finite=(v,label,min=-1e12,max=1e12)=>P.num(v,label,min,max);
const string=(v,label,max=10000)=>{if(typeof v!=='string'||v.length>max||v.includes('\0'))throw Error('Invalid '+label+'.');return v;};
function scalar(v,label='field value'){if(typeof v==='number')return finite(v,label);if(typeof v==='boolean')return v;return string(v,label);}
function source(s) {
    if(!plainObject(s)||!K.Expressions.identifier(s.name)||s.name==='Result')throw Error('Invalid field variable name.');
    if(s.kind==='object'||s.kind==='field') {
        if(!validId(s.entity))throw Error('Invalid field source identity.');
        if(s.kind==='field'){string(s.target,'source field target',300);if(!/^(?:text|cell:\d+:\d+|attribute:.+)$/.test(s.target))throw Error('Invalid source field target.');}
        else {
            if(!PROPERTIES.includes(s.property))throw Error('Unsupported object field property.');
            if(s.property==='attribute')P.name(s.tag);
            if(s.property==='cell')for(const key of ['row','column']){finite(s[key],'table index',0,key==='row'?999:99);if(!Number.isInteger(s[key]))throw Error('Table indices must be integers.');}
        }
    } else if(s.kind==='drawing') {if(!DRAWING.includes(s.property))throw Error('Unsupported drawing field property.');}
    else if(s.kind==='property')P.name(s.key);
    else if(s.kind==='literal')scalar(s.value);
    else if(s.kind==='parameter') {if(!['sketch','spatial'].includes(s.domain)||!K.Expressions.identifier(s.parameter))throw Error('Invalid named parameter field.');}
    else if(s.kind==='aggregate') {
        if(!['count','sum','min','max','average'].includes(s.method)||!['length','area','volume','surfaceArea'].includes(s.property))throw Error('Invalid aggregate field.');
        if(!Array.isArray(s.entities)||!s.entities.length||s.entities.length>1000||new Set(s.entities).size!==s.entities.length||s.entities.some(id=>!validId(id)))throw Error('Aggregate requires distinct source identities (maximum 1000).');
    } else throw Error('Unsupported native field source.');
    if(s.units!=null&&(s.kind==='aggregate'&&s.method==='count'||!own(P.MM,s.units)||s.units==='unitless'||!['object','aggregate'].includes(s.kind)||!['length','area','volume','surfaceArea','radius','diameter','x','y','z'].includes(s.property)))throw Error('Explicit output units require a dimensional object property.');
}
function slot(e,target) {
    if(!HOSTS.has(e.type))throw Error('Fields require text, leader, table or block attribute hosts.');
    if(target==='text'&&['TEXT','MTEXT','LEADER'].includes(e.type))return {get:()=>e.text,set:v=>{e.text=v;},limit:e.type==='MTEXT'?50000:e.type==='TEXT'?100000:10000};
    const match=typeof target==='string'&&/^cell:(\d+):(\d+)$/.exec(target);
    if(match&&e.type==='TABLE') {
        const r=Number(match[1]),c=Number(match[2]);
        if(r>=e.cells?.length||c>=e.cells?.[r]?.length||target!==`cell:${r}:${c}`)throw Error('Field target table cell is missing.');
        return {get:()=>e.cells[r][c],set:v=>{e.cells[r][c]=v;},limit:10000};
    }
    if(e.type==='INSERT'&&typeof target==='string'&&target.startsWith('attribute:')) {
        const tag=P.name(target.slice(10));
        return {get:()=>e.attributes?.[tag]??'',set:v=>{e.attributes=e.attributes||{};e.attributes[tag]=v;},limit:10000};
    }
    throw Error('Invalid field target for '+e.type+'.');
}
function validateBinding(e,b) {
    if(!plainObject(b)||b.version!==1)throw Error('Unsupported annotation field schema.');
    const t=slot(e,b.target);string(b.template,'field template',Math.min(t.limit,50000));
    if(!Array.isArray(b.sources)||!b.sources.length||b.sources.length>LIMITS.sources)throw Error('Fields require 1–32 source variables.');
    const names=new Set();for(const s of b.sources){source(s);if(names.has(s.name))throw Error('Duplicate field variable.');names.add(s.name);}
    if(b.expression!=null){string(b.expression,'field formula',512);names.add('Result');}
    let count=0;for(const match of b.template.matchAll(TOKENS)){if(!names.has(match[1]))throw Error('Unknown field template variable: '+match[1]);if(++count>1024)throw Error('Field template token limit exceeded.');}
    finite(b.precision??2,'field precision',0,12);if(!Number.isInteger(b.precision??2))throw Error('Field precision must be integral.');
    if(b.trimZeros!=null&&typeof b.trimZeros!=='boolean')throw Error('Invalid field zero suppression.');
}
function propertyRows(doc){return doc.production?.drawingProperties||[];}
function validateProperties(rows) {
    if(!Array.isArray(rows)||rows.length>LIMITS.properties)throw Error('Drawing property limit is 128.');
    const seen=new Set();for(const p of rows){if(!plainObject(p))throw Error('Invalid drawing property.');const n=P.name(p.name);if(n!==p.name||seen.has(n.toLowerCase()))throw Error('Duplicate or untrimmed drawing property name.');seen.add(n.toLowerCase());scalar(p.value,'drawing property value');}
}
function dependency(s) {
    if(s.kind==='field')return [s.entity,s.target];
    if(s.kind==='object'&&['text','cell','attribute'].includes(s.property))return [s.entity,s.property==='text'?'text':s.property==='cell'?`cell:${s.row}:${s.column}`:'attribute:'+s.tag];
    return null;
}
function graph(doc) {
    const nodes=new Map();let total=0,characters=0;
    for(const e of doc.entities||[])if(e.fieldBindings!=null) {
        if(!Array.isArray(e.fieldBindings)||e.fieldBindings.length>LIMITS.perEntity)throw Error('Invalid field binding list.');
        const targets=new Set();for(const b of e.fieldBindings){validateBinding(e,b);characters+=b.template.length;if(characters>LIMITS.output)throw Error('Drawing field template budget exceeded.');if(e.type==='INSERT'&&!doc.production?.blocks?.find(block=>block.id===e.block)?.attributes?.some(a=>'attribute:'+a.tag===b.target))throw Error('Field target attribute definition is missing.');if(targets.has(b.target))throw Error('Duplicate field target.');targets.add(b.target);nodes.set(JSON.stringify([e.id,b.target]),{e,b});if(++total>LIMITS.bindings)throw Error('Drawing field limit is '+LIMITS.bindings+'.');}
    }
    const depths=new Map(),visiting=new Set();
    function depth(key){if(visiting.has(key))throw Error('Cyclic annotation field dependency.');if(depths.has(key))return depths.get(key);if(!nodes.has(key))return 0;if(visiting.size>=LIMITS.depth)throw Error('Field dependency depth exceeds 32.');visiting.add(key);let n=1;for(const s of nodes.get(key).b.sources){const d=dependency(s);if(d)n=Math.max(n,1+depth(JSON.stringify(d)));}visiting.delete(key);if(n>LIMITS.depth)throw Error('Field dependency depth exceeds 32.');depths.set(key,n);return n;}
    for(const key of nodes.keys())depth(key);
    return nodes;
}
function validate(doc) {
    if(doc.production?.drawingProperties!=null)validateProperties(doc.production.drawingProperties);
    for(const b of doc.production?.blocks||[])if(b.entities.some(e=>e.fieldBindings?.length))throw Error('Freeze annotation fields before placing their hosts in a block definition.');
    graph(doc);
}
function targetOf(s){return s.property==='cell'?`cell:${s.row}:${s.column}`:s.property==='attribute'?'attribute:'+s.tag:'text';}
// Native mass caches originate in the local kernel. Placement changes must not
// substitute display-triangle measurements for the authoritative B-rep quantities.
function nativeQuantity(e, property) {
    if (!e?.solid || !K.Kernel) throw Error('Quantity requires an authoritative native B-rep body.');
    K.Kernel.validate(e);
    const solid = e.solid, matrix = solid.transform;
    if ([3, 7, 11].some(i => Math.abs(matrix[i]) > 1e-12) || Math.abs(matrix[15] - 1) > 1e-12)
        throw Error('Native quantities require an affine placement.');
    const axes = [matrix.slice(0, 3), matrix.slice(4, 7), matrix.slice(8, 11)];
    if (property === 'volume') {
        if (!Number.isInteger(solid.solidCount) || solid.solidCount < 1)
            throw Error('Volume requires a closed native solid, not an open sheet or curve.');
        return finite(solid.volume * Math.abs(V.dot(axes[0], V.cross(axes[1], axes[2]))), 'native volume', 0);
    }
    if (property !== 'surfaceArea') throw Error('Unsupported native quantity.');
    if (!solid.faces.length) throw Error('Surface area requires native faces.');
    const lengths = axes.map(V.len), scale = lengths[0];
    if (lengths.some(n => Math.abs(n - scale) > scale * 1e-9) ||
        [[0,1],[0,2],[1,2]].some(([a,b]) => Math.abs(V.dot(axes[a], axes[b])) > scale * scale * 1e-9))
        throw Error('Surface area after nonuniform scale or shear requires native recomputation.');
    return finite(solid.area * scale * scale, 'native surface area', 0);
}
function objectValue(doc,e,s,read) {
    if(!e)throw Error('Source object is missing: '+s.entity);
    let value,power=0;
    switch(s.property) {
    case 'id':value=e.id;break;
    case 'type':value=e.type;break;
    case 'layer':value=doc.layer(e).name;break;
    case 'block':{const b=doc.production.blocks.find(b=>b.id===e.block);if(!b)throw Error('Source is not a block reference.');value=b.name;break;}
    case 'length':value=K.Productivity.curve(e).length;power=1;break;
    case 'area':{
        if(['CIRCLE','ELLIPSE'].includes(e.type)) {if(e.type==='ELLIPSE'&&e.endAngle!=null&&Math.abs(K.Math.sweep(e.startAngle||0,e.endAngle)-2*Math.PI)>1e-9)throw Error('Open elliptic arcs have no enclosed area.');const axes=K.Geo.conicAxes(e);value=Math.PI*V.len(V.cross(axes.x,axes.y));}
        else value=K.Productivity.area(e);
        if(value==null)throw Error('This source has no supported analytic enclosed area.');power=2;break;
    }
    case 'volume':case 'surfaceArea':value=nativeQuantity(e,s.property);power=s.property==='volume'?3:2;break;
    case 'radius':case 'diameter':{if(!['CIRCLE','ARC'].includes(e.type))throw Error('Radius requires a circle or circular arc.');K.Productivity.curve(e);value=V.len(K.Geo.conicAxes(e).x)*(s.property==='diameter'?2:1);power=1;break;}
    case 'x':case 'y':case 'z':{const pos=e.position||e.center||(e.type==='INSERT'?M.point(e.matrix,[0,0,0]):e.points?.[0]);if(!pos)throw Error('Source has no insertion point, center or start point.');value=pos[['x','y','z'].indexOf(s.property)];power=1;break;}
    case 'text':case 'cell':case 'attribute':{
        const target=targetOf(s),result=read(e.id,target,false);
        if(result){if(!result.ok)throw Error('Dependent field is unresolved.');value=result.text;break;}
        if(s.property==='attribute') {
            if(e.type!=='INSERT')throw Error('Attribute field requires a block reference.');
            const b=doc.production.blocks.find(b=>b.id===e.block),a=b?.attributes?.find(a=>a.tag===s.tag);
            if(!a&&!own(e.attributes||{},s.tag))throw Error('Source attribute is missing.');value=own(e.attributes||{},s.tag)?e.attributes[s.tag]:a.value;
            if(b?.dynamic){const vals=K.DynamicBlocks.resolve(b,e.parameters||{}).values;value=value.replace(/\$\{([A-Za-z][A-Za-z0-9_]*)\}/g,(_,n)=>{if(!own(vals,n))throw Error('Unknown dynamic attribute parameter.');return String(vals[n]);});}
        } else value=slot(e,target).get();break;
    }
    default:throw Error('Unsupported object property.');
    }
    if(s.units!=null){if(doc.units==='unitless')throw Error('Unitless drawings cannot be converted to physical field units.');value*=Math.pow(P.MM[doc.units]/P.MM[s.units],power);}
    return scalar(value);
}
function evaluate(doc) {
    const nodes=graph(doc),byId=doc.byId||new Map(doc.entities.map(e=>[e.id,e])),results=new Map(),params=new Map();let output=0,steps=0;
    const objectCache=new Map();
    const numberString=(value,b)=>{const n=Object.is(value,-0)?0:value,formatted=n.toFixed(b.precision??2);return b.trimZeros?String(Number(formatted)):formatted.replace(/^-0(?=\.0+$|$)/,'0');};
    // Flush percent escapes individually so %%d from a property stays literal.
    const richLiteral=v=>String(v).replace(/[\\{}%]/g,c=>c==='%'?'\\U+0025{}': '\\'+c);
    function object(s) {if(++steps>100000)throw Error('Field evaluation budget exceeded.');const key=JSON.stringify([s.entity,s.property,s.units,s.tag,s.row,s.column]);if(!objectCache.has(key))objectCache.set(key,objectValue(doc,byId.get(s.entity),s,read));return objectCache.get(key);}
    function value(s) {
        if(++steps>100000)throw Error('Field evaluation budget exceeded.');
        if(s.kind==='literal')return s.value;
        if(s.kind==='object')return object(s);
        if(s.kind==='field'){const r=read(s.entity,s.target);if(!r?.ok)throw Error('Source field is missing or unresolved.');return r.value;}
        if(s.kind==='drawing')return s.property==='entityCount'?doc.entities.length:doc[s.property];
        if(s.kind==='property'){const p=propertyRows(doc).find(p=>p.name===s.key);if(!p)throw Error('Drawing property is missing: '+s.key);return p.value;}
        if(s.kind==='parameter') {if(!params.has(s.domain)){const st=doc.production?.[s.domain==='sketch'?'parametric':'spatial'];params.set(s.domain,K.Expressions.parameters(st?.parameters||[]));}const p=params.get(s.domain);if(!p.has(s.parameter))throw Error('Named parameter is missing: '+s.parameter);return p.get(s.parameter);}
        if(s.kind==='aggregate') {const values=s.entities.map(id=>{if(!byId.has(id))throw Error('Aggregate source is missing: '+id);return s.method==='count'?1:object({...s,kind:'object',entity:id});});return finite(s.method==='count'?values.length:s.method==='min'?Math.min(...values):s.method==='max'?Math.max(...values):values.reduce((a,b)=>a+b,0)/(s.method==='average'?values.length:1),'aggregate result');}
        throw Error('Unsupported field source.');
    }
    function read(id,target,required=true) {
        const key=JSON.stringify([id,target]);if(results.has(key))return results.get(key);
        const node=nodes.get(key);if(!node){if(required)throw Error('Source field is missing.');return null;}
        const {e,b}=node;let result;
        try {
            const values=new Map(b.sources.map(s=>[s.name,value(s)]));
            if(b.expression!=null)values.set('Result',K.Expressions.evaluate(b.expression,n=>{const v=values.get(n);if(typeof v!=='number')throw Error('Formula variable must be numeric: '+n);return v;}));
            const rendered=new Map([...values].map(([n,v])=>[n,typeof v==='number'?numberString(v,b):String(v)]));
            const text=b.template.replace(TOKENS,(_,name)=>e.type==='MTEXT'?richLiteral(rendered.get(name)):rendered.get(name));
            if(text.length>slot(e,b.target).limit)throw Error('Evaluated field text exceeds its host limit.');
            result={ok:true,text,value:values.has('Result')?values.get('Result'):values.size===1?values.values().next().value:text};
        } catch(error) {result={ok:false,text:'####',value:null,error:error.message};}
        output+=result.text.length;if(output>LIMITS.output)throw Error('Drawing field output budget exceeded.');results.set(key,result);return result;
    }
    for(const {e,b} of nodes.values())read(e.id,b.target);
    return {nodes,results};
}
function refresh(doc,before=null) {
    if(!doc.entities.some(e=>e.fieldBindings?.length)){doc.fieldReport=[];return [];}
    validate(doc);
    // Refuse a normal text edit that would be immediately overwritten by its field.
    // Changing/finally detaching the binding is an explicit authoring operation.
    if(before){const previous=new Map(JSON.parse(before).entities.map(e=>[e.id,e]));for(const e of doc.entities){const old=previous.get(e.id);for(const b of e.fieldBindings||[]){const prior=old?.fieldBindings?.find(p=>p.target===b.target);if(prior&&JSON.stringify(prior)===JSON.stringify(b)&&slot(old,b.target).get()!==slot(e,b.target).get())throw Error('Text is controlled by a field. Use FIELD to edit it or FIELDREMOVE to freeze it first.');}}}
    const {nodes,results}=evaluate(doc),report=[];
    for(const [key,{e,b}]of nodes){const result=results.get(key);slot(e,b.target).set(result.text);report.push({entity:e.id,target:b.target,...result});}
    doc.fieldReport=report;return report;
}
function editable(doc,id) {const e=doc.byId.get(id);if(!e||!doc.editable(e))throw Error('Select an editable annotation host.');return e;}
function setBinding(doc,id,binding) {
    const e=editable(doc,id);validateBinding(e,binding);
    doc.transaction('Set associative annotation field',()=>{
        e.fieldBindings=(e.fieldBindings||[]).filter(b=>b.target!==binding.target).concat(K.clone(binding));validate(doc);
        const {results}=evaluate(doc),r=results.get(JSON.stringify([id,binding.target]));if(!r?.ok)throw Error(r?.error||'Field evaluation failed.');
    });return doc.byId.get(id);
}
function freeze(doc,ids,targets=null) {
    const entities=ids.map(id=>editable(doc,id));
    doc.transaction('Freeze annotation fields',()=>{for(const e of entities){e.fieldBindings=(e.fieldBindings||[]).filter(b=>targets&&!targets.includes(b.target));if(!e.fieldBindings.length)delete e.fieldBindings;}});
}
function setProperties(doc,rows,name=doc.name) {
    validateProperties(rows);string(name,'drawing name',512);if(!name.trim())throw Error('Drawing name is required.');
    doc.transaction('Drawing properties',()=>{P.ensure(doc).drawingProperties=K.clone(rows);doc.name=name;});
}
function remap(e,ids,retainExternal=false) {
    if(!e.fieldBindings?.length)return e;
    const out=K.clone(e);out.fieldBindings=out.fieldBindings.filter(b=>retainExternal||b.sources.every(s=>s.kind==='literal'||s.kind==='object'&&ids.has(s.entity)||s.kind==='field'&&ids.has(s.entity)||s.kind==='aggregate'&&s.entities.every(id=>ids.has(id))));
    for(const b of out.fieldBindings)for(const s of b.sources){if(s.entity)s.entity=ids.get(s.entity)||s.entity;if(s.entities)s.entities=s.entities.map(id=>ids.get(id)||id);}
    if(!out.fieldBindings.length)delete out.fieldBindings;
    return out;
}
function finishCopy(doc,ids) {
    // A copied numeric field cannot keep a link to another copied field which was
    // frozen because its own sources were not copied. Propagate that freeze.
    let changed=true,removed=0;
    while(changed){changed=false;for(const id of ids.values()){const e=doc.byId.get(id);if(!e?.fieldBindings)continue;const before=e.fieldBindings.length;e.fieldBindings=e.fieldBindings.filter(b=>b.sources.every(s=>s.kind!=='field'||doc.byId.get(s.entity)?.fieldBindings?.some(f=>f.target===s.target)));if(e.fieldBindings.length!==before){removed+=before-e.fieldBindings.length;changed=true;}if(!e.fieldBindings.length)delete e.fieldBindings;}}
    return removed;
}

function exportData(data) {
    const serialized=data instanceof K.Drawing?data.serialize({includeSource:false}):K.clone(data),doc=K.Drawing.from(serialized);
    refresh(doc);const out=doc.serialize({includeSource:false});for(const e of out.entities)delete e.fieldBindings;return out;
}
function linkedTable(doc,ids,position=[0,0,0],options={}) {
    P.point(position);if(!Array.isArray(ids)||!ids.length||ids.length>LIMITS.tableRows||new Set(ids).size!==ids.length||ids.some(id=>!doc.byId.has(id)))throw Error('Linked tables require 1–250 distinct source objects.');
    const columns=options.columns||[{label:'Object',property:'type'},{label:'Layer',property:'layer'},{label:'Length',property:'length'}];
    if(!Array.isArray(columns)||!columns.length||columns.length>8)throw Error('Choose 1–8 linked table columns.');
    const precision=options.precision??2;finite(precision,'table precision',0,12);if(!Number.isInteger(precision))throw Error('Precision must be integral.');
    const fields=[],cells=[columns.map(c=>string(c.label,'column label',255))];
    for(let i=0;i<ids.length;i++){cells.push(columns.map(()=>''));for(let j=0;j<columns.length;j++){const {label,...desc}=columns[j];const b={version:1,target:`cell:${i+1}:${j}`,template:'{{Value}}',sources:[{...desc,name:'Value',kind:'object',entity:ids[i]}],precision,trimZeros:true};fields.push(b);}}
    let e;doc.transaction('Create linked data table',()=>{e=doc.add('TABLE',{position:K.clone(position),cells,rowHeight:8,columnWidth:45,textHeight:2.5,fieldBindings:fields});doc.selection=new Set([e.id]);});return e;
}
// Native persistence and static interoperability share one evaluator, also in the worker.
const originalValidate=P.validate;P.validate=function(data){originalValidate(data);validate(data);};
const originalWrite=K.Exchange.writeDXF;K.Exchange.writeDXF=function(data){return originalWrite((data.entities||[]).some(e=>e.fieldBindings?.length)?exportData(data):data);};
K.Fields={LIMITS,HOSTS,PROPERTIES,DRAWING,slot,source,nativeQuantity,validateBinding,validateProperties,validate,propertyRows,evaluate,refresh,setBinding,freeze,setProperties,remap,finishCopy,exportData,linkedTable};
})(typeof window!=='undefined'?window:globalThis);
