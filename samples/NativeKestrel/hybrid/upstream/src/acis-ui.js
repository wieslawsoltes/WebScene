/* Geometry-only native ACIS exchange. The codec executes solely in the local worker. */
(function(root) {
    'use strict';
    const K = root.Kestrel, U = K.UI, N = K.Kernel, P = K.Production, esc = U.escape;
    const commands = [
        ['acis-import', 'Import SAT / SAB bodies', 'ACISIN', 'import'],
        ['acis-export', 'Export SAT / SAB bodies', 'ACISOUT', 'export'],
        ['acis-dxf', 'Export native solid DXF', 'SOLIDDXF', 'export']
    ];
    for (const [id, label, alias, icon] of commands) U.commands.push({id, label, alias, icon, description: label + ' — planar faces, straight edges, local codec'});
    U.groups.Exchange.push({name: 'Native planar B-reps', large: ['acis-import', 'acis-export'], columns: [['acis-dxf']]});
    const note = '<div class="dialog-note warning"><strong>Planar B-rep geometry translation.</strong> SAT 7.0 and SAB 21800 with straight edges are supported, including holes and void shells. Curved or unsupported geometry rejects; it is never replaced by triangles. ACIS application attributes/history and original input bytes are not embedded. Use native projects for subsequent edits. Requires the local server and requirements-kernel.txt.</div>';
    function menu(name, label, entries) {
        return `<div class="form-field"><label>${esc(label)}</label><select name="${name}">${entries.map(([v,t]) => `<option value="${esc(v)}">${esc(t)}</option>`).join('')}</select></div>`;
    }
    K.installAcisUI = function(App) {
        const run = App.prototype.run;
        App.prototype.run = async function(id, ...args) {
            if (!commands.some(c => c[0] === id)) return run.call(this, id, ...args);
            this.cancel(false);
            const app = this, doc = this.doc, revision = doc.revision;
            const current = () => { if (app.doc !== doc || doc.revision !== revision) throw Error('Drawing changed; reopen the ACIS dialog.'); };
            if (id === 'acis-import') {
                this.dialog({title: 'Import planar ACIS geometry', wide: true, html: note +
                    '<div class="form-field"><label>SAT / SAB file (16 MiB maximum)</label><input id="acis-file" name="file" type="file" accept=".sat,.sab" required></div>' +
                    menu('sourceUnit', 'Source units', [['auto','Read the file header (unitless files reject)'], ['mm','Override: millimetres'], ['cm','Override: centimetres'], ['m','Override: metres'], ['in','Override: inches'], ['ft','Override: feet']]) +
                    '<label class="form-check"><input type="checkbox" name="acknowledge" required>Import translated geometry only; keep the original file separately.</label>',
                    submit: 'Import native bodies', onSubmit: async f => {
                        current();
                        if (!f.acknowledge) throw Error('Acknowledge geometry-only translation.');
                        const file = document.getElementById('acis-file').files[0];
                        if (!file || !file.size || file.size > 16*1024*1024) throw Error('Choose a nonempty SAT/SAB file up to 16 MiB.');
                        const format = file.name.split('.').at(-1).toLowerCase();
                        if (!['sat','sab'].includes(format)) throw Error('Expected a .sat or .sab file.');
                        if (f.sourceUnit !== 'auto' && !Object.hasOwn(P.MM, f.sourceUnit)) throw Error('Invalid source units.');
                        const data = N.encode(new Uint8Array(await file.arrayBuffer())); current();
                        const result = await N.request('acis-import', [], {format, data, unitMM: P.MM[doc.units], ...(f.sourceUnit !== 'auto' ? {sourceUnitMM: P.MM[f.sourceUnit]} : {})});
                        current();
                        if (!Array.isArray(result.bodies) || !result.bodies.length || result.bodies.length > 32) throw Error('Invalid ACIS body response.');
                        const prepared = result.bodies.map(N.body);
                        doc.transaction('Import ACIS B-rep geometry', () => {
                            const added = prepared.map(e => doc.add(e)); doc.selection = new Set(added.map(e => e.id));
                        });
                        app.setView('iso'); app.setStyle('shaded-edges'); app.fit(false); app.selectionChanged();
                        app.log('ACIS', `${prepared.length} native bodies imported; source SHA-256 ${result.exchange?.sha256 || 'not reported'}.`);
                        for (const warning of result.exchange?.warnings || []) app.log('ACIS', warning);
                        app.toast('Planar native bodies imported. Original file remains separate.');
                    }});
                return;
            }
            const selected = doc.selected().filter(e => e.solid);
            if (!selected.length || selected.length !== doc.selected().length || selected.length > 32) throw Error('Select 1–32 native B-rep bodies. Ordinary meshes are not silently promoted to solids.');
            const inputs = selected.map(e => K.clone(N.input(e))), dxf = id === 'acis-dxf';
            this.dialog({title: dxf ? 'Export planar bodies as DXF solids' : 'Export planar SAT / SAB geometry', wide: true,
                html: note + '<p>Only the selected native bodies are exported. Layers, annotations, original file records and drawing metadata are not included. DXF output contains real 3DSOLID/BODY payloads, not 3DFACE triangles.</p>' +
                    menu(dxf ? 'version' : 'format', 'Output format', dxf ? [['R2018','R2018 DXF (SAB bodies)'],['R2013','R2013 DXF (SAB bodies)'],['R2010','R2010 DXF (SAT bodies)'],['R2000','R2000 DXF (SAT bodies)']] : [['sat','SAT 7.0 — text'],['sab','SAB 21800 — binary']]) +
                    '<label class="form-check"><input type="checkbox" name="acknowledge" required>Export selected body geometry only.</label>',
                submit: 'Export native geometry', onSubmit: async f => {
                    current(); if (!f.acknowledge) throw Error('Acknowledge selected-geometry export.');
                    const result = await N.request(id, inputs, {format:f.format, version:f.version, units:doc.units, unitMM:P.MM[doc.units]}); current();
                    if (!['sat','sab','dxf'].includes(result.format) || typeof result.data !== 'string' || result.data.length > 24*1024*1024) throw Error('Invalid ACIS export response.');
                    app.download(N.decode(result.data), app.filename(result.format), result.format === 'sat' ? 'text/plain' : result.format === 'dxf' ? 'application/dxf' : 'application/octet-stream');
                    app.log('ACIS', `${result.bodies} native bodies exported as ${result.format.toUpperCase()}; planar B-rep geometry only.`);
                }});
        };
    };
})(window);
