/* Native solid tooling. Numerical fields operate on actual OCCT topology. */
(function(root){
    'use strict';
    const K=root.Kestrel,U=K.UI,N=K.Kernel,P=K.Production,{M,V}=K.Math,esc=U.escape;
    const commands=[
        ['solid-box','Native box','SOLIDBOX','box'],['solid-cylinder','Native cylinder','SOLIDCYLINDER','cylinder'],['solid-cone','Native cone','SOLIDCONE','cone'],['solid-sphere','Native sphere','SOLIDSPHERE','sphere'],['solid-torus','Native torus','SOLIDTORUS','torus'],
        ['solid-extrude','Native extrude','SOLIDEXTRUDE','extrude'],['solid-revolve','Native revolve','SOLIDREVOLVE','revolve'],['solid-loft','Native loft','SOLIDLOFT','extrude'],['solid-sweep','Native sweep','SOLIDSWEEP','extrude'],
        ['solid-union','Native union','SOLIDUNION','union'],['solid-subtract','Native subtract','SOLIDSUBTRACT','subtract'],['solid-intersect','Native intersect','SOLIDINTERSECT','intersect'],
        ['solid-fillet','Solid-edge fillet','SOLIDFILLET','fillet'],['solid-chamfer','Solid-edge chamfer','SOLIDCHAMFER','chamfer'],['solid-shell','Shell body','SOLIDSHELL','box'],['solid-section','Native section','SOLIDSECTION','section'],
        ['solid-slice','Slice native bodies','SLICE','section'],['solid-plane-surface','Planar surface / region','PLANESURF','hatch'],['solid-extract-faces','Extract native faces','SOLIDEXTRACT','copy'],['solid-thicken','Thicken surface','THICKEN','extrude'],['solid-separate','Separate composite solid','SOLIDSEPARATE','explode'],['solid-massprops','Mass properties','MASSPROP','properties'],
        ['solid-import','Import STEP / IGES','STEPIMPORT','import'],['solid-export','Export native body','STEPEXPORT','export'],['solid-inspect','Native topology','SOLIDINFO','properties'],['solid-detach','Convert to mesh','SOLIDDETACH','explode']
    ];
    for(const[id,label,alias,icon]of commands)U.commands.push({id,label,alias,icon,description:label+' — local OpenCascade B-rep'});
    U.groups.Solids=[{name:'Native primitives',columns:[['solid-box','solid-cylinder','solid-cone'],['solid-sphere','solid-torus','solid-inspect']]},
        {name:'Profiles',large:['solid-extrude','solid-revolve'],columns:[['solid-loft','solid-sweep','solid-shell']]},
        {name:'Boolean',large:['solid-union','solid-subtract','solid-intersect']},{name:'Edges and sections',columns:[['solid-fillet','solid-chamfer','solid-section']]},
        {name:'Surfaces and analysis',columns:[['solid-plane-surface','solid-extract-faces','solid-thicken'],['solid-slice','solid-separate','solid-massprops']]},
        {name:'Exchange',large:['solid-import','solid-export'],columns:[['solid-detach']]}];
    const xyz=s=>P.point(String(s).split(',').map(Number));
    const menu=(key,label,choices)=>`<div class="form-field"><label>${esc(label)}</label><select name="${key}">${choices.map(([v,l])=>`<option value="${esc(v)}">${esc(l)}</option>`).join('')}</select></div>`;
    const note='<div class="dialog-note">Real curves, surfaces and topology are retained in native projects. The viewport uses a display mesh. Operations need the optional local OpenCascade engine; use the dedicated ACIS commands for planar SAT/SAB translation.</div>';
    K.installKernelUI=function(App){
        const run=App.prototype.run;
        App.prototype.run=async function(id){
            if(!commands.some(c=>c[0]===id))return run.call(this,id);
            this.cancel(false);const app=this,doc=this.doc,selected=doc.selected(true),field=(...a)=>this.field(...a),op=id.slice(6);
            const view=()=>{if(app.doc===doc){app.setView('iso');app.setStyle('shaded-edges');app.fit(false);}};
            const add=async(params,operation=op)=>{const rev=doc.revision;const result=await N.request(operation,[],params);if(rev!==doc.revision)throw Error('Drawing changed; retry the native operation.');doc.transaction('Native '+operation,()=>{const e=doc.add(N.body(result));doc.selection=new Set([e.id]);});view();};
            const native=()=>{if(!selected.length||selected.some(e=>!e.solid))throw Error('Select native B-rep bodies. Use the Solids ribbon to create them.');return selected;};
            const single=()=>{native();if(selected.length!==1)throw Error('Select exactly one native body.');return selected[0];};
            if(op==='slice'){
                native();const opened=doc.revision;
                this.dialog({title:'Slice native bodies',html:note+'<p>Plane coordinates use the current UCS. Positive means the direction of the plane normal. Each input produces up to two independent bodies, with its appearance preserved.</p>'+field('origin','Plane origin XYZ','0,0,5','text')+field('normal','Plane normal XYZ','0,0,1','text')+menu('keep','Keep',[['both','Both sides'],['positive','Positive side'],['negative','Negative side']]),onSubmit:async f=>{
                    if(doc.revision!==opened)throw Error('Drawing changed while the dialog was open; reopen Slice.');
                    const frame=P.frame(P.ensure(doc).ucs),o=M.point(frame,xyz(f.origin)),n=V.sub(M.point(frame,xyz(f.normal)),M.point(frame,[0,0,0]));
                    await N.operate(doc,'slice',selected,{origin:o,normal:n,keep:f.keep});view();
                }});return;
            }
            if(op==='plane-surface'){
                if(!selected.length)throw Error('Select an outer closed profile followed by optional holes.');
                const opened=doc.revision,profiles=selected.map(N.profile);
                this.dialog({title:'Create native planar surface',html:note+'<p>The first profile is the outer boundary. Later profiles are holes. Profiles must be closed and coplanar; source curves are retained.</p>',onSubmit:async()=>{
                    if(doc.revision!==opened)throw Error('Drawing changed; reselect the surface profiles.');
                    await add({profile:profiles[0],holes:profiles.slice(1)},'plane-surface');
                }});return;
            }
            if(op==='thicken'){
                const e=single(),opened=doc.revision;
                this.dialog({title:'Thicken native surface',html:note+field('thickness','Signed thickness',2)+'<label><input type="checkbox" name="retain">Retain source surface</label><p>A single planar or curved native face is supported. Invalid/self-intersecting offsets fail without changing the drawing.</p>',onSubmit:async f=>{
                    if(doc.revision!==opened)throw Error('Drawing changed; reopen Thicken.');
                    await N.operate(doc,'thicken',[e],{thickness:Number(f.thickness)},!f.retain);view();
                }});return;
            }
            if(op==='separate'){const e=single();await N.operate(doc,'separate',[e]);view();return;}
            if(op==='extract-faces'){
                const e=single(),opened=doc.revision,s=await N.request('inspect',[N.input(e)]);
                if(doc.revision!==opened)throw Error('Drawing changed while inspecting topology.');
                this.dialog({title:'Extract native faces',wide:true,html:note+`<table class="report-table">${s.faces.map(x=>`<tr><td>${x.index}</td><td>${esc(x.type)}</td><td>${x.area.toFixed(4)}</td></tr>`).join('')}</table>`+field('indices','Face indices','0','text'),onSubmit:async f=>{
                    if(doc.revision!==opened)throw Error('Topology changed; reopen the face selector.');
                    await N.operate(doc,'extract-faces',[e],{faces:f.indices.split(',').map(v=>Number(v.trim()))},false);view();
                }});return;
            }
            if(op==='massprops'){
                native();const inputs=selected.map(e=>K.clone(N.input(e)));
                this.dialog({title:'Native mass properties',html:note+field('density','Uniform density (mass / drawing-unit³)',1)+'<p>World-space centroid and centroidal inertia are integrated from the transformed B-rep. Overlapping volumes are added; union them first to remove overlap.</p>',onSubmit:async f=>{
                    const result=await N.request('massprops',inputs,{density:Number(f.density)});
                    app.download(JSON.stringify({drawing:doc.name,units:doc.units,...result},null,2),app.filename('massprops.json'));
                    app.toast('Mass '+result.mass.toPrecision(8)+'; volume '+result.volume.toPrecision(8)+'; centroid '+result.centroid.map(v=>v.toPrecision(6)).join(', '));
                }});return;
            }
            if(['box','cylinder','cone','sphere','torus'].includes(op)){
                const fields={box:[['width','Width',100],['depth','Depth',60],['height','Height',20]],cylinder:[['radius','Radius',20],['height','Height',50]],cone:[['radius1','Bottom radius',20],['radius2','Top radius',0],['height','Height',50]],sphere:[['radius','Radius',25]],torus:[['major','Major radius',30],['minor','Tube radius',8]]}[op];
                this.dialog({title:U.get(id).label,html:note+`<div class="form-grid">${fields.map(f=>field(...f)).join('')}${field('origin','UCS position XYZ','0,0,0','text')}</div>`,onSubmit:async f=>{const params=Object.fromEntries(fields.map(([k])=>[k,Number(f[k])]));params.matrix=Array.from(M.multiply(P.frame(P.ensure(doc).ucs),M.translation(...xyz(f.origin))));await add(params);}});return;
            }
            if(['union','subtract','intersect'].includes(op)){native();await N.operate(doc,op,selected);view();return;}
            if(['extrude','revolve','loft','sweep'].includes(op)){
                if(!selected.length)throw Error('Select closed profiles; for sweep select the profile first and path last.');
                const profiles=selected.map(N.profile);
                const html=op==='extrude'?field('vector','Extrusion vector XYZ','0,0,20','text')+field('taper','Taper degrees',0):op==='revolve'?field('axisStart','Axis start XYZ','0,0,0','text')+field('axisEnd','Axis end XYZ','0,0,1','text')+field('angle','Angle degrees',360):op==='loft'?'<label><input type="checkbox" name="ruled">Ruled loft</label>':'<label><input type="checkbox" name="frenet">Frenet frame along path</label>';
                this.dialog({title:U.get(id).label,html:note+`<p>Profile order follows selection order. Source profiles are retained.</p><div class="form-grid">${html}</div>`,onSubmit:async f=>{
                    let params;if(op==='extrude')params={profile:profiles[0],holes:profiles.slice(1),vector:xyz(f.vector),taper:Number(f.taper)};
                    else if(op==='revolve')params={profile:profiles[0],holes:profiles.slice(1),axisStart:xyz(f.axisStart),axisEnd:xyz(f.axisEnd),angle:Number(f.angle)};
                    else if(op==='loft')params={profiles,ruled:!!f.ruled};
                    else{if(profiles.length!==2)throw Error('Sweep requires one profile followed by one path.');params={profile:profiles[0],path:profiles[1],frenet:!!f.frenet};}
                    await add(params);
                }});return;
            }
            if(['fillet','chamfer','shell','section'].includes(op)){
                const e=single(),opened=doc.revision,s=await N.request('inspect',[N.input(e)]),rows=op==='shell'?s.faces:s.edges;
                if(doc.revision!==opened)throw Error('Drawing changed while inspecting topology.');
                const topology=op==='section'?'':`<details><summary>Current ${op==='shell'?'face':'edge'} indices</summary><table class="report-table">${rows.map(x=>`<tr><td>${x.index}</td><td>${esc(x.type)}</td><td>${(x.length??x.area).toFixed(4)}</td><td>${x.center.map(n=>n.toFixed(2)).join(', ')}</td></tr>`).join('')}</table></details>`;
                const html=op==='section'?field('origin','Plane origin XYZ','0,0,10','text')+field('normal','Plane normal XYZ','0,0,1','text'):field('indices',op==='shell'?'Face indices to remove':'Edge indices','0','text')+field('size',op==='shell'?'Thickness (negative = inward)':op==='fillet'?'Radius':'Chamfer distance',op==='shell'?-2:2);
                this.dialog({title:U.get(id).label,wide:true,html:note+topology+`<div class="form-grid">${html}</div>`,onSubmit:async f=>{
                    if(doc.revision!==opened)throw Error('Topology changed; reopen the operation.');
                    const params=op==='section'?{origin:xyz(f.origin),normal:xyz(f.normal)}:{[op==='shell'?'faces':'edges']:f.indices.split(',').map(v=>Number(v.trim())),[op==='shell'?'thickness':op==='fillet'?'radius':'distance']:Number(f.size)};
                    await N.operate(doc,op,[e],params,op!=='section');view();
                }});return;
            }
            if(op==='import'){
                const fileInput=document.createElement('input');fileInput.type='file';fileInput.accept='.step,.stp,.iges,.igs,.brep';
                fileInput.onchange=async()=>{try{const file=fileInput.files[0];if(!file)return;if(file.size>16*1024*1024)throw Error('Native input limit is 16 MiB.');let format=file.name.split('.').at(-1).toLowerCase();format=({stp:'step',igs:'iges'})[format]||format;await add({format,data:N.encode(new Uint8Array(await file.arrayBuffer())),scale:format==='brep'?1:1/P.MM[doc.units]},'import');}catch(e){app.fail(e);}};fileInput.click();return;
            }
            if(op==='export'){
                const e=single();this.dialog({title:'Export native B-rep body',html:note+menu('format','Format',[['step','STEP — B-rep solids (mm)'],['iges','IGES — surfaces/topology (mm)'],['brep','OCCT BREP — drawing units'],['stl','STL — triangulated, not exact']]),onSubmit:async f=>{const result=await N.request('export',[N.input(e)],{format:f.format,scale:['step','iges'].includes(f.format)?P.MM[doc.units]:1});app.download(N.decode(result.data),app.filename(f.format));}});return;
            }
            if(op==='inspect'){
                const e=single(),s=e.solid;const result=await N.request('inspect',[N.input(e)]);
                this.dialog({title:'Validated native topology',html:`<p>Provider: OpenCascade ${esc(s.version)}</p><table class="report-table"><tr><td>Solids</td><td>${result.solidCount}</td></tr><tr><td>Volume (${esc(doc.units)}³)</td><td>${result.volume}</td></tr><tr><td>Area (${esc(doc.units)}²)</td><td>${result.area}</td></tr><tr><td>Faces / edges</td><td>${result.faces.length} / ${result.edges.length}</td></tr></table>`+note,closeOnly:true,submit:'Close'});return;
            }
            if(op==='detach'){
                native();this.dialog({title:'Convert native solid to mesh?',html:'<p>This intentionally removes the authoritative B-rep data from the selected objects. The display triangulation becomes the editable geometry. Undo restores the native body.</p>',submit:'Convert to mesh',onSubmit:()=>doc.transaction('Convert B-rep to mesh',()=>{for(const e of selected){const copy=K.clone(e);delete copy.solid;doc.replace(e.id,copy);}})});return;
            }
        };
    };
})(window);
