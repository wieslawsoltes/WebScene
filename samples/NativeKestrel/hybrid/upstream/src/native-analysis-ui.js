/* Pairwise material checks. A report never edits source bodies. */
(function(root) {
    'use strict';
    const K=root.Kestrel,U=K.UI,A=K.NativeAnalysis,esc=U.escape;
    const commands=[['native-interference','Interference / clearance','INTERFERE'],['native-clearance','Minimum clearance','CLEARANCE']];
    for(const [id,label,alias] of commands)U.commands.push({id,label,alias,icon:'intersect',description:'Actual native solid overlap volumes and minimum gaps'});
    U.groups.Solids.push({name:'Material checks',columns:[commands.map(c=>c[0])]});
    K.installNativeAnalysisUI=function(App) {
        const run=App.prototype.run;
        App.prototype.run=async function(id,...args) {
            const alias=commands.find(c=>c[2]===String(id).toUpperCase());if(alias)id=alias[0];
            if(!commands.some(c=>c[0]===id))return run.call(this,id,...args);
            this.cancel(false);
            const app=this,doc=this.doc,revision=doc.revision;
            const entities=doc.selection.size?doc.selected():doc.entities.filter(e=>e.solid&&doc.visible(e));
            if(entities.length<2||entities.length>32||entities.some(e=>!e.solid||!doc.visible(e)))throw Error('Select 2–32 visible top-level native solids, or clear selection to analyze all visible native bodies.');
            let session=null,result=null,controls=null,busy=false;
            const current=()=>{if(app.doc!==doc||doc.revision!==revision)throw Error('Drawing changed; reopen the analysis dialog.');};
            const active=()=>document.getElementById('analysis-controls')===controls&&document.getElementById('modal').open;
            const error=e=>{if(active()){const box=document.getElementById('modal-error');box.textContent=e.message;box.hidden=false;}};
            const invalidate=()=>{result=null;session=null;document.getElementById('analysis-results').textContent='Run Analyze after changing settings.';document.getElementById('modal-submit').disabled=true;};
            const selectedRow=()=>Number(document.querySelector('#analysis-results [name="analysis-pair"]:checked')?.value??0);
            const label=(e,i)=>`${i+1}. ${e.name||e.id} — ${doc.layer(e).name}`;
            const number=n=>Number(n).toPrecision(8);
            this.dialog({title:'Native interference and clearance',wide:true,submit:'Retain overlap solids',html:
                '<div class="dialog-note">Uses transformed native B-reps, not display meshes. Sources remain unchanged. Contact includes gaps within tolerance; this is not motion/contact simulation. Up to 128 pairs per query.</div>'+ 
                '<div id="analysis-controls"><div class="form-grid">'+
                '<div class="form-field"><label for="analysis-mode">Pair selection</label><select id="analysis-mode" name="mode"><option value="all">Every pair in this list</option><option value="sets">First set versus second set</option></select></div>'+
                this.field('clearance','Required clearance ('+doc.units+')',id==='native-clearance'?1:0,'number','min="0" max="10000000"')+
                this.field('contactTolerance','Contact tolerance ('+doc.units+')',.000001,'number','min="0.000000001" max="1"')+
                '<label class="form-check"><input name="regions" type="checkbox" checked>Generate overlap solids for optional retention</label></div>'+
                '<details><summary>Compare sets / input bodies ('+entities.length+')</summary><p>A body included in both sets belongs only to the first set. Locked sources can be analyzed without editing them.</p><table class="report-table"><thead><tr><th>Body</th><th>First</th><th>Second</th></tr></thead><tbody>'+
                entities.map((e,i)=>`<tr><td>${esc(label(e,i))}</td><td><input type="checkbox" name="first${i}" aria-label="First set body ${i+1}" ${i===0?'checked':''}></td><td><input type="checkbox" name="second${i}" aria-label="Second set body ${i+1}" ${i>0?'checked':''}></td></tr>`).join('')+
                '</tbody></table></details><button type="button" class="button primary" id="analysis-run">Analyze</button></div>'+ 
                '<div id="analysis-results" role="status">No analysis has been run.</div>',
                onOpen:()=>{
                    controls=document.getElementById('analysis-controls');document.getElementById('modal-submit').disabled=true;
                    controls.addEventListener('input',invalidate);
                    document.getElementById('analysis-mode').onchange=()=>{controls.querySelector('details').open=document.getElementById('analysis-mode').value==='sets';};
                    document.getElementById('analysis-run').onclick=async()=>{
                        if(busy)return;busy=true;const button=document.getElementById('analysis-run');button.disabled=true;invalidate();
                        document.getElementById('modal-error').hidden=true;
                        try {
                            current();const form=new FormData(document.getElementById('modal-form'));
                            const params={clearance:Number(form.get('clearance')),contactTolerance:Number(form.get('contactTolerance')),regions:form.has('regions')};
                            if(form.get('mode')==='sets')params.groups={first:entities.flatMap((_,i)=>form.has('first'+i)?[i]:[]),second:entities.flatMap((_,i)=>form.has('second'+i)?[i]:[])};
                            const captured=A.prepare(doc,entities,params);
                            // Lock controls while a captured set is being checked.
                            for(const input of controls.querySelectorAll('input,select'))input.disabled=true;
                            document.getElementById('analysis-results').textContent='Analyzing native material…';
                            const response=await A.run(captured);current();if(!active())return;
                            session=captured;result=response;
                            document.getElementById('analysis-results').innerHTML=`<h3>${result.pairCount} pairs · ${result.counts.interference} overlaps · ${result.counts.contact} contact/within tolerance · ${result.counts.clearance} clearance violations</h3>`+
                                '<table class="report-table"><thead><tr><th>Pair</th><th>Status</th><th>Gap ('+esc(doc.units)+')</th><th>Overlap ('+esc(doc.units)+'³)</th></tr></thead><tbody>'+
                                result.pairs.map((r,i)=>`<tr><td><label><input type="radio" name="analysis-pair" value="${i}" ${i===0?'checked':''}>${r.first+1} / ${r.second+1}</label></td><td>${esc(r.status==='contact'?'contact / within tolerance':r.status)}</td><td>${number(r.distance)}</td><td>${number(r.volume)}</td></tr>`).join('')+
                                '</tbody></table><p>Pairwise overlaps may count shared material more than once. Gap is not penetration depth.</p><button type="button" class="button secondary" id="analysis-focus">Focus pair</button> <button type="button" class="button secondary" id="analysis-gap">Retain gap line</button> <button type="button" class="button secondary" id="analysis-download">Download report</button>';
                            document.getElementById('modal-submit').disabled=!result.bodies.length;
                            document.getElementById('analysis-focus').onclick=()=>{try{current();session.guard();const r=result.pairs[selectedRow()],chosen=[entities[r.first],entities[r.second]];doc.selection=new Set(chosen.map(e=>e.id));app.selectionChanged();app.camera.fit(chosen.flatMap(e=>doc.geometry(e).points),1.3);app.invalidate();}catch(e){error(e);}};
                            document.getElementById('analysis-gap').onclick=()=>{try{current();A.gapLine(session,result,selectedRow());app.closeDialog();app.selectionChanged();app.invalidate();}catch(e){error(e);}};
                            document.getElementById('analysis-download').onclick=()=>{try{current();session.guard();const {bodies,...report}=result;app.download(JSON.stringify({drawing:doc.name,units:doc.units,sourceRevision:revision,entities:entities.map((e,i)=>({id:e.id,label:label(e,i)})),...report},null,2),app.filename('interference.json'),'application/json');}catch(e){error(e);}};
                        } catch(e){error(e);} finally {
                            busy=false;button.disabled=false;
                            for(const input of controls.querySelectorAll('input,select'))input.disabled=false;
                        }
                    };
                },onSubmit:()=>{current();if(!session||!result)throw Error('Run Analyze before retaining results.');A.retain(session,result);app.selectionChanged();app.invalidate();}
            });
        };
    };
})(window);
