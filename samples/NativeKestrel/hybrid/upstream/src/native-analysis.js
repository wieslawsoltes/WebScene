/* Read-only native material analysis and explicit, transactional result retention. */
(function(root) {
    'use strict';
    const K=root.Kestrel, N=K.Kernel;
    function pairs(count, groups) {
        if(!Number.isInteger(count)||count<2||count>32)throw Error('Analysis requires 2–32 native bodies.');
        let plan=[];
        if(groups==null) {
            for(let i=0;i<count;i++)for(let j=i+1;j<count;j++)plan.push([i,j]);
        } else {
            if(typeof groups!=='object'||Array.isArray(groups)||Object.keys(groups).sort().join(',')!=='first,second')throw Error('Invalid analysis groups.');
            for(const g of [groups.first,groups.second])if(!Array.isArray(g)||!g.length||g.some(i=>!Number.isInteger(i)||i<0||i>=count)||new Set(g).size!==g.length)throw Error('Invalid analysis group indices.');
            const second=groups.second.filter(i=>!groups.first.includes(i));
            for(const i of groups.first)for(const j of second)plan.push([i,j]);
        }
        if(!plan.length||plan.length>128)throw Error('Choose 1–128 distinct analysis pairs; reduce or split the sets.');
        return plan;
    }
    const nonnegative=v=>typeof v==='number'&&Number.isFinite(v)&&v>=0;
    function options(params={}) {
        const clearance=params.clearance??0,contactTolerance=params.contactTolerance??1e-6,regions=params.regions??false;
        if(!nonnegative(clearance)||clearance>1e7)throw Error('Invalid minimum clearance.');
        if(!nonnegative(contactTolerance)||contactTolerance<1e-9||contactTolerance>1)throw Error('Invalid contact tolerance.');
        if(typeof regions!=='boolean')throw Error('Create regions must be boolean.');
        return {clearance,contactTolerance,regions,...(params.groups!=null?{groups:K.clone(params.groups)}:{})};
    }
    function validate(result,count,params) {
        const p=options(params),plan=pairs(count,p.groups),statuses=['interference','contact','clearance','separated'];
        const bad=()=>{throw Error('Invalid native analysis response.');};
        if(!result||result.provider!=='OCCT'||result.analysis!=='interference-clearance'||result.schema!==1||result.bodyCount!==count||result.pairCount!==plan.length||result.clearance!==p.clearance||result.contactTolerance!==p.contactTolerance||!Array.isArray(result.pairs)||result.pairs.length!==plan.length||!Array.isArray(result.bodies))bad();
        const overlaps=new Map();
        result.pairs.forEach((row,index)=>{
            const [i,j]=plan[index];
            if(!row||row.first!==i||row.second!==j||!nonnegative(row.volume)||!nonnegative(row.distance)||!statuses.includes(row.status)||typeof row.innerSolution!=='boolean')bad();
            const status=row.volume>0?'interference':row.distance<=p.contactTolerance?'contact':row.distance<p.clearance?'clearance':'separated';
            if(row.status!==status||!Array.isArray(row.witnesses)||!row.witnesses.length||row.witnesses.length>8||!Number.isInteger(row.witnessCount)||row.witnessCount<row.witnesses.length)bad();
            for(const pair of row.witnesses) {
                if(!Array.isArray(pair)||pair.length!==2||pair.some(pt=>!Array.isArray(pt)||pt.length!==3||!pt.every(Number.isFinite)))bad();
                // A contained material witness can lie inside a solid, not on its boundary.
                if(Math.abs(K.Math.V.dist(pair[0],pair[1])-row.distance)>Math.max(1e-5,row.distance*1e-7))bad();
            }
            if(row.volume>0)overlaps.set(i+':'+j,row);
        });
        if(!result.counts||statuses.some(s=>result.counts[s]!==result.pairs.filter(r=>r.status===s).length))bad();
        if(result.bodies.length!==(p.regions?overlaps.size:0)||result.bodies.length>64)bad();
        const seen=new Set();
        const prepared=result.bodies.map(b=>{
            const key=b.first+':'+b.second,row=overlaps.get(key);
            if(!row||seen.has(key)||!nonnegative(b.volume)||Math.abs(b.volume-row.volume)>Math.max(1e-8,row.volume*1e-7))bad();
            seen.add(key);return N.body(b);
        });
        return prepared;
    }
    function prepare(doc,entities,params={}) {
        if(!Array.isArray(entities))throw Error('Select native solid entities.');
        const p=options(params);pairs(entities.length,p.groups);
        if(new Set(entities.map(e=>e.id)).size!==entities.length||entities.some(e=>doc.byId.get(e.id)!==e||!doc.visible(e)))throw Error('Select distinct, current visible native bodies.');
        const inputs=entities.map(e=>K.clone(N.input(e))), revision=doc.revision;
        const ids=entities.map(e=>e.id);
        const guard=()=>{if(doc.revision!==revision||ids.some(id=>!doc.byId.has(id)))throw Error('Drawing changed; rerun the native analysis.');};
        return {doc,revision,ids,inputs,params:p,guard};
    }
    async function run(session) {
        session.guard();
        const response=await N.request('interference',session.inputs,session.params);
        session.guard();validate(response,session.inputs.length,session.params);
        return response;
    }
    function writable(doc) {
        const layer=doc.layerMap.get(doc.currentLayer);
        if(!layer||!layer.visible||layer.locked)throw Error('Choose an unlocked visible current layer for analysis geometry.');
    }
    function retain(session,result) {
        session.guard();writable(session.doc);
        const prepared=validate(result,session.inputs.length,session.params);
        if(!prepared.length)throw Error('No overlap solids to retain. Enable region generation and run Analyze.');
        let added=[];
        session.doc.transaction('Retain interference solids',()=>{
            added=prepared.map(e=>session.doc.add({...e,color:'#e25b47'}));
            session.doc.selection=new Set(added.map(e=>e.id));
        });return added;
    }
    function gapLine(session,result,index) {
        session.guard();writable(session.doc);validate(result,session.inputs.length,session.params);
        const row=result.pairs[index];
        if(!row||row.status==='interference'||row.distance<=session.params.contactTolerance)throw Error('Choose a separated pair with a positive measurable gap.');
        let added;
        session.doc.transaction('Retain minimum gap line',()=>{
            added=session.doc.add('LINE',{points:K.clone(row.witnesses[0])});session.doc.selection=new Set([added.id]);
        });return added;
    }
    K.NativeAnalysis={pairs,options,validate,prepare,run,retain,gapLine};
})(typeof window!=='undefined'?window:globalThis);
