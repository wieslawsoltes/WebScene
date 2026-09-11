/* Authoritative OCCT B-rep bodies with retained display meshes. No remote service. */
(function(root){
    'use strict';
    const K=root.Kestrel, G=K.Geo, {M,V}=K.Math;
    const MAX_NATIVE=24*1024*1024;
    function hashMesh(e){
        // Integrity guard for accidental mesh-only edits, not a cryptographic signature.
        const s=JSON.stringify([e.vertices,e.faces]);let h=2166136261;
        for(let i=0;i<s.length;i++)h=Math.imul(h^s.charCodeAt(i),16777619);
        return (h>>>0).toString(16);
    }
    function validate(e){
        if(!e.solid)return;
        const s=e.solid;
        if(e.type!=='MESH'||s.provider!=='OCCT'||typeof s.brep!=='string'||!s.brep.length||s.brep.length>MAX_NATIVE||!/^[A-Za-z0-9+/]*={0,2}$/.test(s.brep))throw Error('Invalid native B-rep body.');
        if(!Array.isArray(s.transform)||s.transform.length!==16||!s.transform.every(Number.isFinite)||!M.inverse(s.transform))throw Error('Invalid native body transform.');
        if(s.meshHash!==hashMesh(e))throw Error('Native solid display mesh was edited independently. Use SOLIDDETACH to deliberately convert it to an editable mesh first.');
        if(!Array.isArray(s.edgePolylines)||s.edgePolylines.length>20000||s.edgePolylines.some(line=>!Array.isArray(line)||line.length>512||line.some(p=>!Array.isArray(p)||p.length!==3||!p.every(v=>Number.isFinite(v)&&Math.abs(v)<=1e12))))throw Error('Invalid native edge display data.');
        if(!Array.isArray(s.edges)||!Array.isArray(s.faces)||s.edges.length>20000||s.faces.length>20000)throw Error('Invalid native topology table.');
        if(!Number.isFinite(s.volume)||s.volume<0||!Number.isFinite(s.area)||s.area<0)throw Error('Invalid native mass properties.');
    }
    function body(result){
        if(result.provider!=='OCCT'||!result.valid)throw Error('The bridge did not return a validated native body.');
        const e={...result.mesh,solid:{provider:'OCCT',brep:result.brep,transform:Array.from(M.identity()),version:result.version,
            volume:result.volume,area:result.area,solidCount:result.solidCount,tolerance:result.tolerance,
            edges:result.edges,faces:result.faces,edgePolylines:result.edgePolylines}};
        e.solid.meshHash=hashMesh(e);validate(e);return e;
    }
    const geometry=G.geometry, transform=G.transform;
    G.geometry=function(e,tolerance){
        const g=geometry(e,tolerance);
        if(e.solid?.edgePolylines){
            const segments=[];
            for(const line of e.solid.edgePolylines)for(let i=1;i<line.length;i++)segments.push([M.point(e.solid.transform,line[i-1]),M.point(e.solid.transform,line[i])]);
            g.segments=segments;g.wireSegments=segments;
            // Section results can have analytic edges but no faces/triangulation.
            if(!g.points.length)g.points=segments.flat();
        }
        return g;
    };
    G.transform=function(e,m){
        if(e.solid)validate(e);
        const out=transform(e,m);
        if(e.solid){out.solid=K.clone(e.solid);out.solid.transform=Array.from(M.multiply(m,e.solid.transform));out.solid.meshHash=hashMesh(out);}
        return out;
    };
    const entityValidator=K.Production.validateEntity;
    K.Production.validateEntity=function(e){entityValidator(e);validate(e);};
    function input(e){validate(e);if(!e.solid)throw Error('Select a native OCCT solid. Meshes are not silently converted to exact solids.');return e.solid;}
    function profile(e){
        if(e.type==='CIRCLE'||e.type==='ELLIPSE'){
            const axes=G.conicAxes(e),normal=V.norm(V.cross(axes.x,axes.y));
            return {type:e.type==='CIRCLE'?'circle':'ellipse',center:e.center.slice(),radius:e.radius,rx:V.len(axes.x),ry:V.len(axes.y),normal,xdir:V.norm(axes.x)};
        }
        if(e.type==='POLYLINE'||e.type==='LINE'){
            return {type:'polyline',points:K.clone(e.points),bulges:K.clone(e.bulges||[]),normal:e.normal||[0,0,1],closed:!!e.closed};
        }
        throw Error('Select a circle, ellipse, or polyline profile. Splines and arbitrary meshes require a separate conversion workflow.');
    }
    async function request(op,inputs=[],params={},tolerance=.1){
        if(typeof location==='undefined'||!['127.0.0.1','localhost'].includes(location.hostname))throw Error('Native B-rep operations require the local server: install requirements-kernel.txt, then run python3 tools/serve.py. Stored bodies remain viewable on GitHub Pages.');
        const response=await fetch('/api/kernel',{method:'POST',headers:{'Content-Type':'application/json','X-Kestrel-Client':'1'},body:JSON.stringify({op,inputs,params,tolerance}),signal:AbortSignal.timeout(65000)});
        let data;try{data=await response.json();}catch{throw Error('Local B-rep bridge not found. Use tools/serve.py, not a generic static server.');}
        if(!response.ok||data.error)throw Error(data.error||'Native operation failed (HTTP '+response.status+').');
        if(!data.result)throw Error('Invalid native bridge response.');return data.result;
    }
    function decode(s){const text=atob(s),out=new Uint8Array(text.length);for(let i=0;i<out.length;i++)out[i]=text.charCodeAt(i);return out;}
    function encode(bytes){let s='';for(let i=0;i<bytes.length;i+=32768)s+=String.fromCharCode(...bytes.subarray(i,i+32768));return btoa(s);}
    function appearance(e){
        const properties={};
        for(const key of ['layer','color','linetype','lineweight','opacity','group'])
            if(e && e[key]!==undefined)properties[key]=K.clone(e[key]);
        return properties;
    }
    async function operate(doc,op,entities,params={},replace=true){
        const revision=doc.revision,ids=entities.map(e=>e.id);
        if(!entities.length || entities.some(e=>!doc.byId.has(e.id)||!doc.editable(e)))throw Error('Select current editable native bodies.');
        const result=await request(op,entities.map(input),params);
        if(doc.revision!==revision)throw Error('Drawing changed while the kernel was working. No stale result was applied; retry the operation.');
        // Validate every output before deleting any input. A failed result never partially slices a drawing.
        const outputs=result.bodies || [result];
        if(!Array.isArray(outputs)||!outputs.length||outputs.length>64)throw Error('Invalid multi-body kernel response.');
        const prepared=outputs.map(value=>{
            const index=value.sourceIndex??0;
            if(!Number.isInteger(index)||index<0||index>=entities.length)throw Error('Invalid kernel source index.');
            return {...body(value),...appearance(entities[index])};
        });
        let added=[];doc.transaction('Native '+op,()=>{
            if(replace)doc.remove(ids);
            added=prepared.map(e=>doc.add(e));doc.selection=new Set(added.map(e=>e.id));
        });
        return result.bodies?added:added[0];
    }
    K.Kernel={hashMesh,validate,body,input,profile,request,encode,decode,operate,appearance};
})(typeof window!=='undefined'?window:globalThis);
