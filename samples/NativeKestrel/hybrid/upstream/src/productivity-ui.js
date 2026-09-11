/* Kestrel CAD productivity ribbon and command integration. */
(function (root) {
    'use strict';
    const K = root.Kestrel, D = K.Productivity, U = K.UI;
    if (!D || !U) throw Error('Productivity modules must load after the CAD model and UI registry.');
    const escape = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;', '<':'&lt;', '>':'&gt;', '"':'&quot;', "'":'&#39;'}[c]));
    const field = (name, label, value = '', type = 'text') => `<div class="form-field"><label>${escape(label)}</label><input name="${name}" type="${type}" value="${escape(value)}"${type === 'number' ? ' step="any"' : ''}></div>`;
    const menu = (name, label, choices) => `<div class="form-field"><label>${escape(label)}</label><select name="${name}">${choices.map(([value, text]) => `<option value="${escape(value)}">${escape(text)}</option>`).join('')}</select></div>`;
    const commands = [
        ['productivity-select', 'Quick select', 'QSELECT', 'properties'],
        ['productivity-count', 'Count and quantities', 'COUNT', 'properties'],
        ['productivity-divide', 'Divide curve', 'DIVIDE', 'point'],
        ['productivity-measure', 'Measure curve stations', 'MEASURE', 'dimension'],
        ['productivity-lengthen', 'Lengthen curve', 'LENGTHEN', 'line'],
        ['productivity-reverse', 'Reverse curve direction', 'REVERSE', 'mirror'],
        ['productivity-extract', 'Extract drawing data', 'DATAEXTRACTION', 'export'],
        ['productivity-layer-save', 'Save layer state', 'LAYERSTATESAVE', 'layers'],
        ['productivity-layer-restore', 'Restore layer state', 'LAYERSTATERESTORE', 'layers']
    ];
    for (const [id, label, alias, icon] of commands) if (!U.commands.some(c => c.id === id))
        U.commands.push({id, label, alias, icon, description: label + ' — editable drawing geometry'});
    U.groups.Productivity = [
        {name:'Selection and quantities', large:['productivity-select','productivity-count'], columns:[['productivity-extract']]},
        {name:'Curve stations', large:['productivity-divide','productivity-measure']},
        {name:'Curve editing', columns:[['productivity-lengthen','productivity-reverse']]},
        {name:'Layer states', columns:[['productivity-layer-save','productivity-layer-restore']]}
    ];
    const aliases = new Map(commands.map(([id, , alias]) => [alias, id]));
    const ids = new Set(commands.map(c => c[0]));
    const xyz = text => {
        const point = String(text).split(',').map(Number);
        if (point.length === 2) point.push(0);
        return K.Production.point(point);
    };
    function install(app) {
        if (!app || typeof app.run !== 'function' || app.__productivityInstalled) return !!app?.__productivityInstalled;
        const previous = app.run;
        app.run = async function (id, ...args) {
            id = aliases.get(String(id).toUpperCase()) || id;
            if (!ids.has(id)) return previous.call(this, id, ...args);
            try {return execute(this, id);} catch (error) {this.toast(error.message); return false;}
        };
        app.__productivityInstalled = true;
        D.installed = true;
        return true;
    }
    function execute(app, id) {
        const doc = app.doc, selected = doc.selected(true), selection = selected.map(e => e.id);
        const chooseOne = () => {if (selected.length !== 1) throw Error('Select exactly one editable curve.'); return selected[0];};
        const changed = () => app.selectionChanged();
        if (id === 'productivity-select') {
            const types = [...new Set(doc.entities.map(e => e.type))].sort();
            app.dialog({title:'Quick select', html:menu('type','Object type',[['*','All types'], ...types.map(t => [t,t])]) + menu('layer','Layer',[['*','All layers'], ...doc.layers.map(l => [l.id,l.name])]) + field('text','Text contains (literal match)') + menu('mode','Selection operation',[['replace','Replace'],['add','Add'],['remove','Remove'],['intersect','Intersect']]) + '<p>Hidden and locked objects are not selected. Text matching also searches table cells and block attributes.</p>', onSubmit: f => {
                const found = D.select(doc,{type:f.type,layer:f.layer,text:f.text},f.mode); changed(); app.toast(found.length + ' objects selected.');
            }}); return;
        }
        if (id === 'productivity-count') {
            const rows = D.count(doc, selection.length ? selection : null);
            const value = n => Number(n.toPrecision(10)).toString();
            app.dialog({title:'Count and quantities', wide:true, html:'<p>' + (selection.length ? 'Selected objects' : 'Visible model-space objects') + '; block references count as instances, not exploded members. Areas are algebraic boundary areas, not a Boolean union.</p><table class="report-table"><thead><tr><th>Type</th><th>Layer</th><th>Block</th><th>Count</th><th>Length ('+escape(doc.units)+')</th><th>Area ('+escape(doc.units)+'²)</th></tr></thead><tbody>' + rows.map(r => '<tr>' + [r.type,r.layer,r.block,r.count,value(r.totalLength),value(r.totalArea)].map(v => '<td>'+escape(v)+'</td>').join('') + '</tr>').join('') + '</tbody></table>', onSubmit:()=>{}}); return;
        }
        if (id === 'productivity-divide' || id === 'productivity-measure') {
            const e = chooseOne(), mode = id.endsWith('divide') ? 'divide' : 'measure', opened = doc.revision;
            const c = D.curve(e), blocks = K.Production.ensure(doc).blocks;
            app.dialog({title:mode === 'divide' ? 'Divide curve' : 'Measure curve stations', html:'<p>Analytic curve length: '+escape(c.length)+' '+escape(doc.units)+'. Source geometry is retained.</p>' + field('value',mode === 'divide' ? 'Equal divisions' : 'Station spacing',mode === 'divide' ? 5 : Math.max(c.length/5, .001),'number') + menu('block','Marker',[['','Point nodes'],...blocks.map(b => [b.id,b.name])]) + field('scale','Block scale',1,'number') + menu('align','Block orientation',[['yes','Align X axis with curve tangent'],['no','World X axis']]), onSubmit:f=>{
                if (doc.revision !== opened) throw Error('Drawing changed; reopen the station dialog.');
                const added = D.mark(doc,e.id,mode,Number(f.value),{block:f.block,scale:Number(f.scale),align:f.align === 'yes'}); changed(); app.toast(added.length + ' markers created in one undoable operation.');
            }}); return;
        }
        if (id === 'productivity-lengthen') {
            const e = chooseOne(), opened = doc.revision, length = D.curve(e).length;
            app.dialog({title:'Lengthen curve', html:'<p>Current length: '+escape(length)+' '+escape(doc.units)+'. LINE and circular ARC are supported. Driving constraints must be edited through the Parametric tools.</p>' + menu('mode','Method',[['total','Total length'],['delta','Length increment'],['percent','Percentage of current length']]) + field('value','Value',length,'number') + menu('end','Endpoint to change',[['end','End'],['start','Start']]), onSubmit:f=>{
                if (doc.revision !== opened) throw Error('Drawing changed; reopen Lengthen.');
                D.lengthen(doc,e.id,f.mode,Number(f.value),f.end); changed();
            }}); return;
        }
        if (id === 'productivity-reverse') {D.reverse(doc,selection); changed(); app.toast(selection.length+' curves reversed.'); return;}
        if (id === 'productivity-extract') {
            const records = D.rows(doc,selection.length ? selection : null), opened = doc.revision;
            app.dialog({title:'Extract drawing data', html:'<p>'+records.length+' objects. CSV uses spreadsheet-safe text cells. JSON retains exact scalar values. Ordinary tables are editable snapshots. Linked tables keep a fixed set of source objects and update their native field cells; neither option is an external data link.</p>' + menu('format','Output',[['csv','CSV file'],['json','JSON file'],['table','Editable snapshot table'],['linked','Linked field table']]) + field('position','Table insertion XYZ (current UCS)','0,0,0'), onSubmit:f=>{
                if (f.format === 'csv') app.download(D.csv(records),app.filename('csv'));
                else if (f.format === 'json') app.download(JSON.stringify({drawing:doc.name,units:doc.units,records},null,2),app.filename('quantities.json'));
                else {
                    if (doc.revision !== opened) throw Error('Drawing changed; reopen Data extraction.');
                    if(f.format==='linked'){if(!K.Fields)throw Error('Annotation fields are not loaded.');K.Fields.linkedTable(doc,records.map(r=>r.id),K.Production.toWorld(doc,xyz(f.position)),{columns:[{label:'Type',property:'type'},{label:'Layer',property:'layer'},{label:'Length',property:'length'},{label:'Area',property:'area'}]});}else D.extractionTable(doc,selection.length ? selection : null,K.Production.toWorld(doc,xyz(f.position))); changed();
                }
            }}); return;
        }
        if (id === 'productivity-layer-save') {
            app.dialog({title:'Save layer state', html:field('name','State name','Working layers') + menu('replace','Existing name',[['no','Do not overwrite'],['yes','Overwrite same name']]) + '<p>Stores visibility, locks, color, linetype, lineweight and the current layer. Layer identities and geometry are not changed.</p>', onSubmit:f=>{D.saveLayers(doc,f.name,f.replace === 'yes'); changed();}}); return;
        }
        if (id === 'productivity-layer-restore') {
            const states = D.layerStates(doc);
            if (!states.length) throw Error('Save a layer state first.');
            app.dialog({title:'Restore layer state', html:menu('name','Saved state',states.map(s => [s.name,s.name])) + menu('operation','Operation',[['restore','Restore'],['delete','Delete saved state']]) + '<p>Restoring is undoable. New layers are retained; deleted layers are not recreated.</p>', onSubmit:f=>{if(f.operation === 'delete') D.deleteLayers(doc,f.name); else D.restoreLayers(doc,f.name); changed();}});
        }
    }
    D.installUI = install;
    K.installProductivityUI = App => install(App.prototype);
})(typeof window !== 'undefined' ? window : globalThis);
