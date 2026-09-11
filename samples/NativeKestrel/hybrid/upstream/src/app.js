/* Kestrel CAD — command-driven editing application. Plain JavaScript, no framework. */
(function (root) {
    'use strict';
    const K = root.Kestrel, { V, M, EPS, TAU, angle, sweep, lineIntersection, segmentDistance, inside, polygonArea, faceNormal } = K.Math, G = K.Geo, U = K.UI, $ = id => document.getElementById(id), esc = U.escape;
    const UNIT_LABELS = { mm: 'Millimeters', cm: 'Centimeters', m: 'Meters', in: 'Inches', ft: 'Feet', unitless: 'Unitless' }, UNIT_MM = { mm: 1, cm: 10, m: 1000, in: 25.4, ft: 304.8, unitless: 1 };
    const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
    function finite(value, name = 'Value', min = -1e12, max = 1e12) { const n = Number(value); if (!Number.isFinite(n) || n < min || n > max)
        throw Error(`${name} must be between ${min} and ${max}.`); return n; }
    function positive(value, name = 'Value') { return finite(value, name, 1e-7, 1e9); }
    function centerOf(entities) { let min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity]; for (const e of entities) {
        const p = e.type === 'MESH' ? e.vertices : G.geometry(e, 2).points;
        for (const q of p)
            for (let j = 0; j < 3; j++) {
                min[j] = Math.min(min[j], q[j] || 0);
                max[j] = Math.max(max[j], q[j] || 0);
            }
    } if (!Number.isFinite(min[0]))
        return { center: [0, 0, 0], min: [0, 0, 0], max: [0, 0, 0], size: [0, 0, 0] }; return { center: V.lerp(min, max, .5), min, max, size: V.sub(max, min) }; }
    class App {
        constructor() {
            this.docs = [];
            this.activeId = null;
            this.camera = new K.Camera();
            this.renderer = new K.Renderer($('scene'), $('overlay'), this.camera);
            this.index = new K.SpatialIndex();
            this.settings = { grid: true, snap: false, osnap: true, ortho: false, polar: false, lineweights: false, snapSpacing: 100, polarAngle: 15 };
            this.defaults = { color: 'bylayer', customColor: '#5ac6d2', lineweight: 0, textHeight: 160, dimensionHeight: 145, precision: 2 };
            this.theme = 'dark';
            this.workspace = '2d';
            this.ribbonTab = 'Home';
            this.explorerTab = 'layers';
            this.tool = null;
            this.lastTool = 'line';
            this.lastPoint = [0, 0, 0];
            this.pointer = { x: 0, y: 0, world: [0, 0, 0], inside: false };
            this.drag = null;
            this.navMode = null;
            this.hoverId = null;
            this.snapTarget = null;
            this.sheet = false;
            this.modelCamera = null;
            this.clipboard = [];
            this.queued = false;
            this.logEntries = [];
            this.commandHistory = [];
            this.historyPosition = 0;
            this.suggestionIndex = 0;
            this.paletteIndex = 0;
            this.pendingIO = new Map();
            this.ioSequence = 0;
            this.importMode = 'open';
            this.shift = false;
            this.space = false;
            this.suppressClick = false;
            this.autoSaveTimer = null;
            this.backendReady = false;
            this.capabilities = null;
            this.aliases = new Map();
            for (const c of U.commands) {
                this.aliases.set(c.id.toUpperCase(), c.id);
                for (const alias of c.alias.split(/\s*[·]\s*/))
                    this.aliases.set(alias.trim().toUpperCase(), c.id);
            }
            for (const [a, id] of Object.entries({ RECT: 'rectangle', RECTANGLE: 'rectangle', PL: 'polyline', DLI: 'dimension', DIM: 'dimension', DIMLINEAR: 'dimension', DELETE: 'erase', DEL: 'erase', SAVEAS: 'save', QSAVE: 'save', '3DO': 'orbit', SHADE: 'shaded-edges', SEISO: 'view-iso', ZOOMEXTENTS: 'fit', CLOSE: 'close-tool' }))
                this.aliases.set(a, id);
        }
        get doc() { return this.docs.find(d => d.id === this.activeId); }
        async init() {
            U.installIcons();
            $('quick-access').innerHTML = ['new', 'open', 'save', 'undo', 'redo'].map(U.toolButton).join('');
            $('ribbon-tab-list').innerHTML = Object.keys(U.groups).map(t => `<button data-ribbon="${t}" role="tab" aria-selected="${t === 'Home'}" class="${t === 'Home' ? 'active' : ''}">${t}</button>`).join('');
            $('viewport-tools').innerHTML = ['clear-selection', 'line', 'polyline', 'circle', 'rectangle', 'dimension'].map(U.toolButton).join('') + '<div class="toolbar-divider"></div>' + ['move', 'hatch', 'extrude'].map(U.toolButton).join('');
            $('navigation-tools').innerHTML = ['fit', 'zoomin', 'zoomout'].map(U.toolButton).join('') + '<div class="toolbar-divider"></div>' + ['pan', 'orbit'].map(U.toolButton).join('');
            $('status-toggles').innerHTML = [['grid', 'GRID'], ['snap', 'SNAP'], ['ortho', 'ORTHO'], ['polar', 'POLAR'], ['osnap', 'OSNAP'], ['lineweights', 'LWT']].map(([id, label]) => `<button data-action="${id}" title="${esc(U.get(id).description + ' (' + U.get(id).alias + ')')}">${U.icon(U.get(id).icon)}<span class="toggle-label">${label}</span></button>`).join('');
            this.makeFileMenu();
            this.bind();
            try {
                const preferences = JSON.parse(localStorage.getItem('kestrel.preferences') || 'null');
                if (preferences) {
                    this.theme = preferences.theme === 'light' ? 'light' : 'dark';
                    Object.assign(this.settings, preferences.settings || {});
                    Object.assign(this.defaults, preferences.defaults || {});
                }
            }
            catch { }
            this.applyTheme();
            this.setRibbon('Home');
            const params = new URLSearchParams(location.search);
            let loaded = false;
            if (!params.has('demo'))
                try {
                    const saved = JSON.parse(localStorage.getItem('kestrel.workspace.v1') || 'null');
                    if (saved?.documents?.length) {
                        for (const data of saved.documents.slice(0, 8)) {
                            const d = K.Drawing.from(data);
                            d.dirty = true;
                            this.addDocument(d, false);
                        }
                        this.switchDocument(this.docs[clamp(saved.active || 0, 0, this.docs.length - 1)].id);
                        loaded = true;
                        this.log('Recovered', 'Restored the locally autosaved workspace.');
                    }
                }
                catch (error) {
                    this.log('Recovery', error.message, 'error');
                }
            if (!loaded) {
                const example = params.get('demo') === '3d' ? K.Examples.fixture() : params.get('demo') === 'blank' ? new K.Drawing() : K.Examples.courtyard();
                this.addDocument(example);
                this.workspace = params.get('demo') === '3d' ? '3d' : '2d';
                this.camera.setView(this.workspace === '3d' ? 'iso' : 'top');
                this.renderer.style = this.workspace === '3d' ? 'shaded-edges' : 'wireframe';
            }
            this.renderer.onBackend = backend => { this.backendReady = true; $('engine-label').textContent = backend === 'WebGPU' ? 'WebGPU · GPU pipeline' : 'Canvas 2D · Compatibility'; $('engine-badge').classList.toggle('fallback', backend !== 'WebGPU'); this.log('Renderer', backend === 'WebGPU' ? 'WebGPU active · 4× MSAA · instanced lines · depth-tested meshes' : `Canvas 2D fallback: ${this.renderer.fallbackReason}`); this.invalidate(); };
            this.renderer.onError = message => { this.log('Renderer', message, 'error'); this.toast(message, true); };
            this.renderer.drawOverlay = ctx => this.drawOverlay(ctx);
            new ResizeObserver(() => { const r = $('viewport').getBoundingClientRect(); this.renderer.resize(r.width, r.height); this.invalidate(); }).observe($('viewport'));
            const rect = $('viewport').getBoundingClientRect();
            this.renderer.resize(rect.width, rect.height);
            await this.renderer.init(params.get('renderer') === 'canvas');
            this.renderer.resize(rect.width, rect.height, true);
            if (!loaded || !this.doc.camera)
                this.fit(false);
            else
                this.camera.restore(this.doc.camera);
            this.refresh();
            this.invalidate();
            this.setupWorker();
            this.log('Ready', 'L line · C circle · REC rectangle · M move · Ctrl+K commands.');
            root.kestrel = this;
            document.documentElement.dataset.ready = 'true';
        }
        bind() {
            document.addEventListener('click', e => {
                const action = e.target.closest('[data-action]');
                if (action && !action.disabled) {
                    e.preventDefault();
                    this.run(action.dataset.action).catch(error => this.fail(error));
                    return;
                }
                const tab = e.target.closest('[data-ribbon]');
                if (tab) {
                    this.setRibbon(tab.dataset.ribbon);
                    return;
                }
                const view = e.target.closest('[data-view]');
                if (view) {
                    this.setView(view.dataset.view);
                    return;
                }
                const layout = e.target.closest('[data-layout]');
                if (layout) {
                    this.setLayout(layout.dataset.layout);
                    return;
                }
                const exp = e.target.closest('[data-explorer]');
                if (exp) {
                    this.explorerTab = exp.dataset.explorer;
                    $('explorer-search').value = '';
                    this.refreshExplorer();
                    return;
                }
                const doc = e.target.closest('[data-doc]');
                if (doc) {
                    if (e.target.closest('.tab-close'))
                        this.closeDocument(doc.dataset.doc);
                    else
                        this.switchDocument(doc.dataset.doc);
                    return;
                }
                if (!e.target.closest('#file-menu,#app-menu-button'))
                    $('file-menu').hidden = true;
                if (!e.target.closest('#context-menu'))
                    $('context-menu').hidden = true;
            });
            $('app-menu-button').onclick = e => { e.stopPropagation(); $('file-menu').hidden = !$('file-menu').hidden; };
            $('workspace-select').onchange = e => this.setWorkspace(e.target.value);
            $('view-select').onchange = e => this.setView(e.target.value);
            $('style-select').onchange = e => this.setStyle(e.target.value);
            $('explorer-search').oninput = () => this.refreshExplorer();
            $('explorer-list').addEventListener('click', e => this.explorerClick(e));
            $('ribbon').addEventListener('change', e => { const target = e.target; if (target.id === 'ribbon-layer') {
                this.doc.currentLayer = target.value;
                this.refresh();
                this.saveSoon();
            }
            else if (target.id === 'draw-color') {
                this.defaults.customColor = target.value;
                this.defaults.color = target.value;
                $('draw-color-mode').value = 'custom';
                this.savePreferences();
            }
            else if (target.id === 'draw-color-mode') {
                this.defaults.color = target.value === 'bylayer' ? 'bylayer' : this.defaults.customColor;
                this.savePreferences();
            }
            else if (target.id === 'draw-lineweight') {
                this.defaults.lineweight = Number(target.value);
                this.savePreferences();
            } });
            $('inspector').addEventListener('change', e => { if (e.target.dataset.prop)
                this.changeProperty(e.target).catch(error => this.fail(error)); });
            const viewport = $('viewport');
            viewport.addEventListener('pointerdown', e => this.pointerDown(e));
            viewport.addEventListener('pointermove', e => this.pointerMove(e));
            viewport.addEventListener('pointerup', e => this.pointerUp(e));
            viewport.addEventListener('pointercancel', () => this.endDrag());
            viewport.addEventListener('pointerleave', () => { if (!this.drag) {
                this.pointer.inside = false;
                this.hoverId = null;
                this.snapTarget = null;
                this.invalidate();
            } });
            viewport.addEventListener('contextmenu', e => e.preventDefault());
            viewport.addEventListener('wheel', e => { if (e.target.closest('select'))
                return; e.preventDefault(); const p = this.eventPoint(e); this.camera.zoomAt(Math.exp(clamp(-e.deltaY * .0014, -.6, .6)), p[0], p[1]); this.invalidate(); }, { passive: false });
            viewport.addEventListener('dblclick', e => { if (e.target.closest('button,select'))
                return; if (this.tool && ['polyline', 'spline'].includes(this.tool.id)) {
                this.finishTool();
                return;
            } const p = this.eventPoint(e), hit = this.hit(p); if (['TEXT','MTEXT'].includes(hit?.e.type)) {
                this.doc.selection = new Set([hit.e.id]);
                this.refresh();
                this.textDialog(hit.e);
            }
            else if (!hit)
                this.fit(); });
            const cmd = $('command-input');
            cmd.addEventListener('input', () => this.commandSuggest());
            cmd.addEventListener('keydown', e => this.commandKey(e));
            cmd.addEventListener('focus', () => this.commandSuggest());
            cmd.addEventListener('blur', () => setTimeout(() => { $('command-suggestions').hidden = true; }, 150));
            $('command-suggestions').addEventListener('mousedown', e => { const row = e.target.closest('[data-command]'); if (row) {
                e.preventDefault();
                cmd.value = '';
                $('command-suggestions').hidden = true;
                this.run(row.dataset.command).catch(x => this.fail(x));
            } });
            document.addEventListener('keydown', e => this.keyDown(e));
            document.addEventListener('keyup', e => { if (e.key === 'Shift')
                this.shift = false; if (e.code === 'Space')
                this.space = false; });
            window.addEventListener('blur', () => { this.shift = false; this.space = false; this.endDrag(); });
            $('palette-search').addEventListener('input', () => { this.paletteIndex = 0; this.renderPalette(); });
            $('palette-search').addEventListener('keydown', e => { if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
                e.preventDefault();
                this.paletteIndex = clamp(this.paletteIndex + (e.key === 'ArrowDown' ? 1 : -1), 0, this.paletteMatches.length - 1);
                this.renderPalette();
            }
            else if (e.key === 'Enter') {
                e.preventDefault();
                const c = this.paletteMatches[this.paletteIndex];
                if (c) {
                    $('command-palette').close();
                    this.run(c.id).catch(x => this.fail(x));
                }
            } });
            $('palette-results').onclick = e => { const item = e.target.closest('[data-palette]'); if (item) {
                $('command-palette').close();
                this.run(item.dataset.palette).catch(x => this.fail(x));
            } };
            $('modal-close').onclick = () => this.closeDialog();
            $('modal-cancel').onclick = () => this.closeDialog();
            $('modal').addEventListener('cancel', () => { this.dialogResolve?.(false); this.dialogResolve = null; });
            $('file-input').onchange = () => { const f = $('file-input').files[0]; if (f)
                this.openFile(f, this.importMode).catch(x => this.fail(x)); $('file-input').value = ''; };
            document.addEventListener('dragover', e => { if (e.dataTransfer?.types.includes('Files')) {
                e.preventDefault();
                $('drop-zone').hidden = false;
            } });
            document.addEventListener('dragleave', e => { if (!e.relatedTarget)
                $('drop-zone').hidden = true; });
            document.addEventListener('drop', e => { e.preventDefault(); $('drop-zone').hidden = true; const f = e.dataTransfer?.files[0]; if (f)
                this.openFile(f, 'open').catch(x => this.fail(x)); });
            for (const el of document.querySelectorAll('.panel-resizer,#command-resizer')) {
                el.addEventListener('pointerdown', e => { e.preventDefault(); const isCommand = el.id === 'command-resizer', isLeft = el.classList.contains('left-resizer'), x = e.clientX, y = e.clientY, initial = isCommand ? $('command-dock').offsetHeight : isLeft ? $('explorer').offsetWidth : $('properties').offsetWidth; el.setPointerCapture(e.pointerId); const move = ev => { const v = isCommand ? clamp(initial + y - ev.clientY, 60, window.innerHeight * .4) : clamp(initial + (isLeft ? 1 : -1) * (ev.clientX - x), 170, 390); document.documentElement.style.setProperty(isCommand ? '--command-height' : isLeft ? '--left-width' : '--right-width', v + 'px'); }; const up = () => { el.removeEventListener('pointermove', move); el.removeEventListener('pointerup', up); }; el.addEventListener('pointermove', move); el.addEventListener('pointerup', up); });
            }
            window.addEventListener('beforeunload', () => this.autosave());
        }
        addDocument(doc, activate = true) { if (this.docs.length >= 12)
            throw Error('Close a drawing before opening more (12 tabs maximum).'); doc.onChange = label => { if (doc.id === this.activeId) {
            this.refresh();
            this.invalidate();
        } this.saveSoon(); }; this.docs.push(doc); if (activate)
            this.switchDocument(doc.id); return doc; }
        switchDocument(id) { if (this.activeId && this.doc)
            this.doc.camera = this.camera.serialize(); this.cancel(false); this.activeId = id; const doc = this.doc; if (!doc)
            return; this.sheet = false; this.modelCamera = null; if (doc.camera) {
            this.camera.restore(doc.camera);
            this.workspace = Math.abs(this.camera.pitch - Math.PI / 2) < .01 ? '2d' : '3d';
            this.renderer.style = this.workspace === '3d' ? 'shaded-edges' : 'wireframe';
        }
        else {
            const hasMeshes = doc.entities.filter(e => e.type === 'MESH').length > doc.entities.length * .25;
            this.workspace = hasMeshes ? '3d' : '2d';
            this.camera.setView(hasMeshes ? 'iso' : 'top');
            this.renderer.style = hasMeshes ? 'shaded-edges' : 'wireframe';
            this.fit(false);
        } this.refresh(); this.invalidate(); this.saveSoon(); }
        closeDocument(id) { const d = this.docs.find(d => d.id === id); if (!d)
            return; const close = () => { const index = this.docs.indexOf(d); this.docs.splice(index, 1); if (!this.docs.length)
            this.addDocument(new K.Drawing());
        else if (this.activeId === id)
            this.switchDocument(this.docs[Math.min(index, this.docs.length - 1)].id); this.refresh(); this.saveSoon(); }; if (d.dirty)
            this.dialog({ title: 'Close drawing?', html: `<p><strong>${esc(d.name)}</strong> has edits that may not be in an exported project file. Closing removes this tab from workspace recovery.</p><p>Use Save project first to keep a portable copy.</p>`, submit: 'Close drawing', onSubmit: close });
        else
            close(); }
        refresh() { if (!this.doc)
            return; this.refreshTabs(); this.refreshExplorer(); this.refreshInspector(); this.refreshRibbon(); this.refreshStatus(); $('workspace-select').value = this.workspace; $('style-select').value = this.renderer.style; $('view-select').value = Math.abs(this.camera.pitch - Math.PI / 2) < .001 ? 'top' : Math.abs(this.camera.pitch) < .001 ? (Math.abs(this.camera.yaw + Math.PI / 2) < .001 ? 'front' : Math.abs(this.camera.yaw) < .001 ? 'right' : Math.abs(this.camera.yaw - Math.PI / 2) < .001 ? 'back' : 'left') : 'iso'; $('title-name').textContent = this.doc.name; $('document-units').textContent = UNIT_LABELS[this.doc.units]; $('drawing-kind').textContent = this.workspace === '3d' ? '3D MODEL SPACE' : 'MODEL SPACE'; this.updateToolPrompt(); }
        refreshTabs() { $('document-tabs').innerHTML = this.docs.map(d => `<button class="document-tab ${d.id === this.activeId ? 'active' : ''}" data-doc="${d.id}" role="tab" aria-selected="${d.id === this.activeId}">${U.icon('drawing')}<span>${esc(d.name)}</span>${d.dirty ? '<span class="dirty-dot">·</span>' : ''}<span class="tab-close" role="button" aria-label="Close ${esc(d.name)}" title="Close drawing">×</span></button>`).join(''); }
        refreshExplorer() {
            if (!this.doc)
                return;
            const doc = this.doc, query = $('explorer-search').value.toLowerCase(), counts = {};
            for (const e of doc.entities)
                counts[e.layer] = (counts[e.layer] || 0) + 1;
            $('layer-count').textContent = doc.layers.length;
            $('object-count').textContent = doc.entities.length.toLocaleString();
            $('layers-tab').classList.toggle('active', this.explorerTab === 'layers');
            $('objects-tab').classList.toggle('active', this.explorerTab === 'objects');
            $('explorer-search').placeholder = this.explorerTab === 'layers' ? 'Filter layers…' : 'Filter objects…';
            $('explorer-subtitle').textContent = this.explorerTab === 'layers' ? 'LAYER NAME' : 'DRAWING OBJECTS';
            if (this.explorerTab === 'layers') {
                $('explorer-list').innerHTML = doc.layers.filter(l => l.name.toLowerCase().includes(query)).map(l => `<div class="layer-row ${l.id === doc.currentLayer ? 'current' : ''} ${l.visible ? '' : 'off'}" data-layer="${l.id}" title="Click to make current · Shift+click to select layer objects"><span class="color-dot" style="background:${l.color}"></span><span class="layer-name">${esc(l.name)}</span><span class="layer-count">${counts[l.id] || '—'}</span><button class="icon-button" data-layer-toggle="visible" title="${l.visible ? 'Hide' : 'Show'} layer" aria-label="${l.visible ? 'Hide' : 'Show'} ${esc(l.name)}">${U.icon(l.visible ? 'eye' : 'eyeoff')}</button><button class="icon-button ${l.locked ? 'locked' : ''}" data-layer-toggle="locked" title="${l.locked ? 'Unlock' : 'Lock'} layer" aria-label="${l.locked ? 'Unlock' : 'Lock'} ${esc(l.name)}">${U.icon(l.locked ? 'lock' : 'unlock')}</button></div>`).join('') || '<div class="empty-explorer">No layers match this filter.</div>';
            }
            else {
                const matches = doc.entities.filter(e => [e.type, e.name || '', doc.layer(e).name, e.id].join(' ').toLowerCase().includes(query));
                $('explorer-list').innerHTML = matches.slice(0, 500).map(e => `<div class="object-row ${doc.selection.has(e.id) ? 'active' : ''}" data-object="${e.id}">${U.icon(this.entityIcon(e))}<div><strong>${esc(e.name || this.entityLabel(e))}</strong><small>${esc(doc.layer(e).name)}</small></div><span class="object-id">${esc(e.id.split('_').at(-1))}</span></div>`).join('') + (matches.length > 500 ? `<div class="empty-explorer">Showing the first 500 of ${matches.length.toLocaleString()} objects. Refine the filter.</div>` : '');
            }
            $('summary-name').textContent = doc.name;
            $('summary-entities').textContent = doc.entities.length.toLocaleString();
            $('summary-selected').textContent = doc.selection.size;
            $('summary-units').textContent = doc.units;
        }
        explorerClick(event) { const row = event.target.closest('[data-layer]'); if (row) {
            const id = row.dataset.layer, l = this.doc.layer(id), toggle = event.target.closest('[data-layer-toggle]');
            if (toggle) {
                this.doc.transaction((toggle.dataset.layerToggle === 'visible' ? 'Toggle visibility' : 'Toggle lock') + ' ' + l.name, () => { l[toggle.dataset.layerToggle] = !l[toggle.dataset.layerToggle]; });
            }
            else if (event.shiftKey) {
                this.doc.selection = new Set(this.doc.entities.filter(e => e.layer === id && this.doc.visible(e)).map(e => e.id));
                this.selectionChanged();
            }
            else {
                this.doc.currentLayer = id;
                this.refreshRibbon();
                this.refreshExplorer();
                this.refreshInspector();
                this.saveSoon();
            }
            return;
        } const obj = event.target.closest('[data-object]'); if (obj) {
            const id = obj.dataset.object;
            if (event.shiftKey) {
                if (this.doc.selection.has(id))
                    this.doc.selection.delete(id);
                else
                    this.doc.selection.add(id);
            }
            else
                this.doc.selection = new Set([id]);
            this.selectionChanged();
        } }
        entityLabel(e) { return e.type === 'MESH' ? (e.primitive || 'Mesh solid') : ({ LINE: 'Line', POLYLINE: 'Polyline', CIRCLE: 'Circle', ARC: 'Arc', ELLIPSE: 'Ellipse', SPLINE: 'Spline', TEXT: 'Text', DIMENSION: 'Aligned dimension', HATCH: 'Hatch', POINT: 'Point' }[e.type] || e.type); }
        entityIcon(e) { return e.type === 'MESH' ? 'box' : ({ LINE: 'line', POLYLINE: 'polyline', CIRCLE: 'circle', ARC: 'arc', ELLIPSE: 'ellipse', SPLINE: 'spline', TEXT: 'text', DIMENSION: 'dimension', HATCH: 'hatch', POINT: 'point' }[e.type] || 'drawing'); }
        refreshRibbon() { if (!this.doc)
            return; const layer = $('ribbon-layer'); if (layer) {
            layer.innerHTML = this.doc.layers.map(l => `<option value="${l.id}">${esc(l.name)}</option>`).join('');
            layer.value = this.doc.currentLayer;
            $('current-layer-color').style.background = this.doc.layer(this.doc.currentLayer).color;
        } if ($('draw-color')) {
            $('draw-color').value = this.defaults.color === 'bylayer' ? this.doc.layer(this.doc.currentLayer).color : this.defaults.color;
            $('draw-color-mode').value = this.defaults.color === 'bylayer' ? 'bylayer' : 'custom';
            $('draw-lineweight').value = this.defaults.lineweight;
        } document.querySelectorAll('[data-action]').forEach(b => { const id = b.dataset.action; const active = (this.tool?.id === id) || (this.navMode === id) || (Object.hasOwn(this.settings, id) && this.settings[id] === true) || id === this.renderer.style || id === 'perspective' && this.camera.perspective; b.classList.toggle('active', !!active); if (id === 'undo')
            b.disabled = this.doc.undoStack.length === 0; if (id === 'redo')
            b.disabled = this.doc.redoStack.length === 0; }); }
        refreshStatus() { if (!this.doc)
            return; $('units-status').textContent = this.doc.units; $('selection-status').textContent = this.doc.selection.size ? this.doc.selection.size + ' selected' : 'No selection'; for (const b of $('layout-tabs').children)
            b.classList.toggle('active', (b.dataset.layout === 'sheet') === this.sheet); $('sheet-label').hidden = !this.sheet; this.refreshRibbon(); }
        setRibbon(tab) { this.ribbonTab = tab; $('ribbon').innerHTML = U.ribbon(tab); document.querySelectorAll('[data-ribbon]').forEach(b => { b.classList.toggle('active', b.dataset.ribbon === tab); b.setAttribute('aria-selected', b.dataset.ribbon === tab); }); this.refreshRibbon(); }
        applyTheme() { document.documentElement.dataset.theme = this.theme; this.renderer.theme = this.sheet ? 'light' : this.theme; document.querySelectorAll('[data-action="theme"]').forEach(b => { if (b.classList.contains('icon-button'))
            b.innerHTML = U.icon(this.theme === 'dark' ? 'sun' : 'moon'); }); this.invalidate(); }
        setWorkspace(workspace) { this.workspace = workspace; if (workspace === '3d') {
            this.camera.setView('iso');
            this.renderer.style = 'shaded-edges';
            this.defaults.textHeight = 5;
            this.defaults.dimensionHeight = 5;
            this.settings.snapSpacing = Math.max(.1, this.renderer.gridSpacing());
            this.setRibbon('Model');
        }
        else {
            this.camera.setView('top');
            this.camera.perspective = false;
            this.camera.update();
            this.renderer.style = 'wireframe';
            this.setRibbon('Home');
        } this.fit(false); this.refresh(); this.invalidate(); }
        setView(name) { this.camera.setView(name); $('view-select').value = name; if (name !== 'top' && name !== 'bottom') {
            this.workspace = '3d';
            if (this.renderer.style === 'wireframe' && this.doc.entities.some(e => e.type === 'MESH'))
                this.renderer.style = 'shaded-edges';
        }
        else if (name === 'top')
            this.workspace = '2d'; this.refresh(); this.invalidate(); }
        setStyle(style) { this.renderer.style = style; this.refreshStatus(); $('style-select').value = style; this.invalidate(); }
        setLayout(layout) { const sheet = layout === 'sheet'; if (sheet === this.sheet)
            return; if (sheet) {
            this.modelCamera = this.camera.serialize();
            this.sheet = true;
            this.camera.perspective = false;
            this.camera.setView('top');
            this.fit(false, 1.55);
        }
        else {
            this.sheet = false;
            if (this.modelCamera)
                this.camera.restore(this.modelCamera);
        } this.renderer.theme = this.sheet ? 'light' : this.theme; this.refreshStatus(); this.invalidate(); }
        fit(log = true, padding = 1.24) { if (!this.doc)
            return; const points = []; for (const e of this.doc.entities)
            if (this.doc.visible(e)) {
                const p = this.doc.geometry(e).points;
                for (const v of p)
                    points.push(v);
            } this.camera.fit(points, padding); if (log)
            this.log('Zoom', 'Visible drawing extents.'); this.invalidate(); }
        invalidate() { if (this.queued)
            return; this.queued = true; requestAnimationFrame(() => { this.queued = false; if (!this.doc || !this.backendReady)
            return; try {
            this.renderer.theme = this.sheet ? 'light' : this.theme;
            this.renderer.grid = this.settings.grid && !this.sheet;
            this.renderer.lineweights = this.settings.lineweights;
            this.renderer.render(this.doc);
            const s = this.renderer.stats;
            $('frame-status').textContent = s.cpuMs.toFixed(1) + ' ms';
            $('frame-status').title = 'CPU frame submission time, not GPU execution time';
        }
        catch (error) {
            this.log('Render', error.message, 'error');
            console.error(error);
        } }); }
        selectionChanged() { this.hoverId = null; this.refreshExplorer(); this.refreshInspector(); this.refreshStatus(); this.invalidate(); }
        log(label, message, type = '') { const row = document.createElement('div'); if (type)
            row.className = 'history-' + type; const name = document.createElement('span'); name.className = 'history-label'; name.textContent = label; const text = document.createElement('span'); text.textContent = message; row.append(name, text); $('command-history').append(row); while ($('command-history').children.length > 200)
            $('command-history').firstElementChild.remove(); $('command-history').scrollTop = $('command-history').scrollHeight; this.logEntries.push({ label, message, type, time: new Date().toISOString() }); if (this.logEntries.length > 500)
            this.logEntries.shift(); }
        toast(message, error = false) { const t = document.createElement('div'); t.className = 'toast' + (error ? ' error' : ''); t.innerHTML = U.icon(error ? 'warning' : 'check') + `<span>${esc(message)}</span>`; $('toast-stack').append(t); while ($('toast-stack').children.length > 3)
            $('toast-stack').firstChild.remove(); setTimeout(() => t.remove(), error ? 6500 : 3500); }
        fail(error) { const message = error?.message || String(error); this.log('Error', message, 'error'); this.toast(message, true); }
        savePreferences() { try {
            localStorage.setItem('kestrel.preferences', JSON.stringify({ theme: this.theme, settings: this.settings, defaults: this.defaults }));
        }
        catch { } }
        saveSoon() { clearTimeout(this.autoSaveTimer); $('save-state').textContent = 'Saving locally…'; this.autoSaveTimer = setTimeout(() => this.autosave(), 800); }
        autosave() { if (!this.doc)
            return; try {
            this.doc.camera = this.camera.serialize();
            const content = JSON.stringify({ documents: this.docs.map(d => d.serialize()), active: this.docs.findIndex(d => d.id === this.activeId), saved: new Date().toISOString() });
            localStorage.setItem('kestrel.workspace.v1', content);
            $('save-state').textContent = 'Autosaved locally';
            this.autosaveFailed = false;
        }
        catch (error) {
            $('save-state').textContent = 'Save project to keep edits';
            if (!this.autosaveFailed) {
                this.log('Autosave', 'Browser storage is unavailable or full. Save a .kcad project to preserve these edits.', 'error');
                this.autosaveFailed = true;
            }
        } this.savePreferences(); }
        makeFileMenu() { const items = ['new', 'open', 'save', 'insert', '-', 'export-dxf', 'export-dwg', 'export-svg', 'export-png', 'print', '-', 'demo-2d', 'demo-3d', '-', 'help']; $('file-menu').innerHTML = '<div class="popover-title">DRAWING & EXCHANGE</div>' + items.map(id => id === '-' ? '<div class="menu-separator"></div>' : `<button class="menu-item" data-action="${id}">${U.icon(U.get(id).icon)}<span>${esc(U.get(id).label)}</span><span class="menu-shortcut">${id === 'new' ? 'Ctrl+N' : id === 'open' ? 'Ctrl+O' : id === 'save' ? 'Ctrl+S' : ''}</span></button>`).join(''); }
        dialog({ title, html, submit = 'Apply', wide = false, onSubmit = () => { }, onOpen, closeOnly = false }) { this.closeDialog(); $('modal-title').textContent = title; $('modal-body').innerHTML = html; $('modal-submit').textContent = submit; $('modal-cancel').hidden = closeOnly; $('modal-error').hidden = true; $('modal-submit').disabled = false; $('modal').classList.toggle('wide', wide); $('modal-form').onsubmit = async (event) => { event.preventDefault(); $('modal-submit').disabled = true; $('modal-error').hidden = true; try {
            const result = await onSubmit(Object.fromEntries(new FormData($('modal-form'))));
            if (result !== false) {
                this.dialogResolve?.(true);
                this.dialogResolve = null;
                $('modal').close();
            }
        }
        catch (error) {
            $('modal-error').textContent = error.message;
            $('modal-error').hidden = false;
        }
        finally {
            $('modal-submit').disabled = false;
        } }; U.installIcons($('modal')); $('modal').showModal(); onOpen?.(); return new Promise(resolve => { this.dialogResolve = resolve; }); }
        closeDialog() { if ($('modal').open)
            $('modal').close(); this.dialogResolve?.(false); this.dialogResolve = null; }
        field(name, label, value = '', type = 'number', extra = '') { return `<div class="form-field"><label for="field-${name}">${esc(label)}</label><input id="field-${name}" name="${name}" type="${type}" value="${esc(value)}" ${type === 'number' ? 'step="any"' : ''} ${extra}></div>`; }
        refreshInspector() {
            if (!this.doc)
                return;
            const doc = this.doc, selected = doc.selected(), one = selected.length === 1 ? selected[0] : null, layerOptions = doc.layers.map(l => `<option value="${l.id}" ${one?.layer === l.id || !selected.length && doc.currentLayer === l.id ? 'selected' : ''}>${esc(l.name)}</option>`).join('');
            const row = (label, html) => `<div class="property-row"><label>${esc(label)}</label>${html}</div>`, read = (label, value) => row(label, `<span title="${esc(value)}">${esc(value)}</span>`), input = (label, key, value, type = 'number', attrs = '') => row(label, `<input aria-label="${esc(label)}" data-prop="${key}" type="${type}" value="${esc(type === 'number' ? Number(Number(value || 0).toFixed(4)) : value || '')}" ${type === 'number' ? 'step="any"' : ''} ${attrs}>`), section = title => `<div class="property-section"><div class="property-section-title">${title}</div>`;
            let html = `<div class="inspector-type">${U.icon(one ? this.entityIcon(one) : selected.length ? 'selectall' : 'select')}<strong>${one ? esc(this.entityLabel(one)) : selected.length ? selected.length + ' objects' : 'No selection'}</strong><span>${one?.sourceHandle ? '#' + esc(one.sourceHandle) : '⌄'}</span></div>`;
            html += section('GENERAL');
            html += row(selected.length ? 'Layer' : 'Current layer', `<select data-prop="layer" aria-label="${selected.length ? 'Object layer' : 'Current layer'}">${selected.length > 1 ? '<option value="">Multiple / choose…</option>' : ''}${layerOptions}</select>`);
            const color = one?.color && one.color !== 'bylayer' ? one.color : this.defaults.color === 'bylayer' ? doc.layer(one || doc.currentLayer).color : this.defaults.color;
            html += row('Color', `<div class="color-value"><input type="color" data-prop="color" aria-label="Object color" value="${color}"><button data-action="color-bylayer">ByLayer</button></div>`);
            if (selected.length) {
                html += row('Linetype', `<select data-prop="linetype" aria-label="Linetype">${['ByLayer', 'Continuous', 'Dashed', 'Center'].map(v => `<option ${(one?.linetype || 'ByLayer') === v ? 'selected' : ''}>${v}</option>`).join('')}</select>`);
                html += input('Lineweight', 'lineweight', one?.lineweight || 0, 'number', 'min="0" max="5"');
                if (one) {
                    html += input('Name', 'name', one.name || '', 'text', 'maxlength="80"');
                    html += read('Layer state', doc.layer(one).locked ? 'Locked — read only' : doc.layer(one).visible ? 'Visible / editable' : 'Hidden');
                    if (one.group)
                        html += read('Group', one.groupName || one.group);
                }
            }
            else {
                html += read('Units', UNIT_LABELS[doc.units]);
                html += read('Visual style', this.renderer.style.replaceAll('-', ' '));
                html += read('Workspace', this.workspace === '3d' ? '3D modeling' : '2D drafting');
            }
            html += '</div>';
            if (one) {
                html += section('GEOMETRY');
                const pos = (label, key, p) => { for (let i = 0; i < 3; i++)
                    html += input(label + ' ' + ['X', 'Y', 'Z'][i], key + '.' + i, p[i] || 0); };
                if (one.type === 'LINE') {
                    pos('Start', 'points.0', one.points[0]);
                    pos('End', 'points.1', one.points[1]);
                    html += read('Length', V.dist(...one.points).toFixed(3) + ' ' + doc.units);
                }
                else if (['CIRCLE', 'ARC', 'ELLIPSE'].includes(one.type)) {
                    pos('Center', 'center', one.center);
                    if (one.type !== 'ELLIPSE')
                        html += input('Radius', 'radius', one.radius || V.len(G.conicAxes(one).x), 'number', 'min="0.000001"');
                    else {
                        html += input('Major radius', 'rx', V.len(G.conicAxes(one).x), 'number', 'min="0.000001"');
                        html += input('Minor radius', 'ry', V.len(G.conicAxes(one).y), 'number', 'min="0.000001"');
                    }
                    if (one.type === 'ARC') {
                        html += input('Start angle', 'startAngleDeg', (one.startAngle || 0) * 180 / Math.PI);
                        html += input('End angle', 'endAngleDeg', one.endAngle * 180 / Math.PI);
                    }
                    if (one.type === 'CIRCLE')
                        html += read('Area', (Math.PI * (one.radius || 1) ** 2).toFixed(3) + ' ' + doc.units + '²');
                }
                else if (['POLYLINE', 'SPLINE', 'HATCH'].includes(one.type)) {
                    html += read('Vertices', (one.controlPoints || one.points).length);
                    if (one.type === 'POLYLINE')
                        html += row('Closed', `<input data-prop="closed" aria-label="Closed polyline" type="checkbox" ${one.closed ? 'checked' : ''}>`);
                    if (one.type === 'SPLINE')
                        html += input('Degree', 'degree', one.degree || 3, 'number', `min="1" max="${Math.min(10, (one.controlPoints || one.points).length - 1)}" step="1"`);
                    const p = G.path(one), length = p.reduce((v, q, i) => v + (i ? V.dist(p[i - 1], q) : 0), 0) + (G.closed(one) ? V.dist(p[0], p.at(-1)) : 0);
                    html += read('Length', length.toFixed(3) + ' ' + doc.units);
                    if (G.closed(one))
                        html += read('Plan area', Math.abs(polygonArea(p)).toFixed(3) + ' ' + doc.units + '²');
                    if (one.type === 'HATCH') {
                        html += input('Spacing', 'spacing', one.spacing || 10, 'number', 'min="0.001"');
                        html += row('Pattern', `<select data-prop="pattern">${['ANSI31', 'cross', 'solid'].map(v => `<option ${one.pattern === v ? 'selected' : ''}>${v}</option>`).join('')}</select>`);
                    }
                }
                else if (one.type === 'MTEXT') {
                    pos('Position', 'position', one.position);
                    html += input('Text height', 'height', one.height, 'number', 'min="0.0001"');
                    html += input('Paragraph width', 'width', one.width || 0, 'number', 'min="0"');
                    html += read('Paragraphs', K.MText.parse(one.text).paragraphs.length);
                    html += row('Composition', '<button class="button" data-action="mtext-edit">Edit rich text</button>');
                }
                else if (one.type === 'TEXT') {
                    pos('Position', 'position', one.position);
                    html += input('Text height', 'height', one.height || 10, 'number', 'min="0.0001"');
                    html += input('Rotation', 'rotationDeg', (one.rotation || 0) * 180 / Math.PI);
                    html += row('Text', `<textarea data-prop="text" aria-label="Text content" style="width:107px;min-height:55px;font-size:10px;padding:5px">${esc(one.text)}</textarea>`);
                }
                else if (one.type === 'DIMENSION') {
                    html += read('Measurement', V.dist(...one.points).toFixed(3) + ' ' + doc.units);
                    html += input('Offset', 'offset', one.offset || 0);
                    html += input('Text height', 'textHeight', one.textHeight || 10, 'number', 'min="0.0001"');
                    html += input('Text override', 'text', one.text || '', 'text');
                    html += input('Precision', 'precision', one.precision ?? this.defaults.precision, 'number', 'min="0" max="6" step="1"');
                }
                else if (one.type === 'MESH') {
                    const b = centerOf([one]);
                    pos('Center', 'meshCenter', b.center);
                    html += read('Width X', b.size[0].toFixed(3) + ' ' + doc.units);
                    html += read('Depth Y', b.size[1].toFixed(3) + ' ' + doc.units);
                    html += read('Height Z', b.size[2].toFixed(3) + ' ' + doc.units);
                    html += read('Vertices', one.vertices.length.toLocaleString());
                    html += read('Faces', one.faces.length.toLocaleString());
                    html += read('Signed volume', G.volume(one).toFixed(2) + ' ' + doc.units + '³');
                }
                else if (one.type === 'POINT')
                    pos('Position', 'position', one.position);
                html += '</div>';
            }
            if (!selected.length) {
                html += section('DRAFTING SETTINGS') + input('Snap spacing', 'default-snapSpacing', this.settings.snapSpacing, 'number', 'min="0.0001"') + input('Text height', 'default-textHeight', this.defaults.textHeight, 'number', 'min="0.0001"') + input('Dim. height', 'default-dimensionHeight', this.defaults.dimensionHeight, 'number', 'min="0.0001"') + read('Grid display', 'Adaptive') + '</div>';
                html += `<div class="property-hint"><div class="hint-icon">${U.icon('select')}</div><strong>Select an object to inspect it</strong>Click geometry, or drag a selection window. Hold <kbd>Shift</kbd> to add or remove objects.<br><br>Blue grips edit points directly.</div>`;
            }
            else
                html += `<div class="property-actions">${['move', 'copy', 'rotate', 'erase'].map(id => `<button data-action="${id}">${U.get(id).label}</button>`).join('')}</div><div class="property-hint"><strong>${doc.selected(true).length === 0 ? 'Selection is locked' : 'Precision editing'}</strong>${doc.selected(true).length === 0 ? 'Unlock the layer before changing geometry.' : 'Edit values above, drag blue grips, or type a command. Every completed edit can be undone.'}</div>`;
            $('inspector').innerHTML = html;
        }
        async changeProperty(input) { const key = input.dataset.prop, value = input.type === 'checkbox' ? input.checked : input.value, selected = this.doc.selected(true); if (key.startsWith('default-')) {
            const k = key.slice(8), n = positive(value);
            if (k === 'snapSpacing')
                this.settings[k] = n;
            else
                this.defaults[k] = n;
            this.savePreferences();
            return;
        } if (!this.doc.selection.size) {
            if (key === 'layer') {
                this.doc.currentLayer = value;
                this.refresh();
                this.saveSoon();
            }
            else if (key === 'color') {
                this.defaults.color = value;
                this.defaults.customColor = value;
                this.refreshRibbon();
                this.savePreferences();
            }
            return;
        } if (!selected.length)
            throw Error('The selected layers are locked.'); this.doc.transaction('Edit ' + key, () => { for (const original of selected) {
            let e = K.clone(original);
            if (key === 'meshCenter.0' || key === 'meshCenter.1' || key === 'meshCenter.2') {
                const axis = Number(key.at(-1)), center = centerOf([e]).center, delta = [0, 0, 0];
                delta[axis] = finite(value) - center[axis];
                e = G.transform(e, M.translation(...delta));
            }
            else if (key === 'radius') {
                const radius = positive(value, 'Radius'), old = V.len(G.conicAxes(e).x), f = radius / old;
                e.radius = radius;
                if (e.axisX)
                    e.axisX = V.mul(e.axisX, f);
                if (e.axisY)
                    e.axisY = V.mul(e.axisY, f);
            }
            else if (key === 'rx' || key === 'ry') {
                const ax = G.conicAxes(e), len = positive(value, 'Radius');
                e.axisX = key === 'rx' ? V.mul(V.norm(ax.x), len) : ax.x;
                e.axisY = key === 'ry' ? V.mul(V.norm(ax.y), len) : ax.y;
                e[key] = len;
            }
            else if (key.endsWith('Deg')) {
                e[key.slice(0, -3)] = finite(value) * Math.PI / 180;
                if (key === 'rotationDeg' && e.type === 'TEXT')
                    delete e.direction;
            }
            else if (key.includes('.')) {
                const parts = key.split('.');
                let target = e;
                for (const p of parts.slice(0, -1))
                    target = target[p];
                target[parts.at(-1)] = finite(value);
            }
            else if (['layer', 'color', 'linetype', 'name', 'text', 'pattern'].includes(key)) {
                if (key === 'layer' && !value)
                    continue;
                e[key] = value;
            }
            else if (key === 'closed')
                e.closed = !!value;
            else {
                const n = finite(value);
                if (['height', 'textHeight', 'spacing'].includes(key) && n <= 0)
                    throw Error('This value must be positive.');
                if (key === 'degree') {
                    e.degree = clamp(Math.round(n), 1, Math.min(10, (e.controlPoints || e.points).length - 1));
                    e.knots = G.uniformKnots((e.controlPoints || e.points).length, e.degree);
                }
                else
                    e[key] = key === 'precision' ? clamp(Math.round(n), 0, 6) : n;
            }
            this.doc.replace(e.id, e);
        } }); }
        async run(id) {
            $('file-menu').hidden = true;
            $('context-menu').hidden = true;
            $('command-suggestions').hidden = true;
            if (!this.doc)
                return;
            if (['line', 'polyline', 'rectangle', 'circle', 'arc', 'ellipse', 'spline', 'point', 'dimension', 'measure', 'move', 'copy', 'rotate', 'scale', 'mirror', 'trim', 'extend', 'match'].includes(id)) {
                this.startTool(id);
                return;
            }
            if (['box', 'cylinder', 'sphere', 'cone', 'torus'].includes(id)) {
                this.primitiveDialog(id);
                return;
            }
            if (id.startsWith('view-')) {
                this.setView(id.slice(5));
                return;
            }
            if (['wireframe', 'shaded-edges', 'shaded', 'xray'].includes(id)) {
                this.setStyle(id);
                return;
            }
            if (['grid', 'snap', 'osnap', 'ortho', 'polar', 'lineweights'].includes(id)) {
                this.settings[id] = !this.settings[id];
                if (id === 'ortho' && this.settings.ortho)
                    this.settings.polar = false;
                if (id === 'polar' && this.settings.polar)
                    this.settings.ortho = false;
                this.log(id.toUpperCase(), this.settings[id] ? 'On' : 'Off');
                this.savePreferences();
                this.refreshStatus();
                this.invalidate();
                return;
            }
            switch (id) {
                case 'new': {
                    const d = new K.Drawing('Drawing ' + (this.docs.length + 1));
                    d.units = this.doc.units;
                    this.addDocument(d);
                    this.camera.setView('top');
                    this.fit(false);
                    this.log('New', 'Created a blank drawing.');
                    break;
                }
                case 'open':
                case 'insert':
                    this.importMode = id;
                    $('file-input').click();
                    break;
                case 'save':
                    this.saveNative();
                    break;
                case 'export-dxf':
                    await this.exportDXF();
                    break;
                case 'export-dwg':
                    await this.exportDWG();
                    break;
                case 'export-svg':
                    this.svgDialog();
                    break;
                case 'export-png':
                    this.exportPNG();
                    break;
                case 'print':
                    this.printDialog();
                    break;
                case 'undo': {
                    this.cancel(false);
                    const label = this.doc.undo();
                    this.log('Undo', label || 'Nothing to undo.');
                    break;
                }
                case 'redo': {
                    this.cancel(false);
                    const label = this.doc.redo();
                    this.log('Redo', label || 'Nothing to redo.');
                    break;
                }
                case 'fit':
                    this.fit();
                    break;
                case 'zoomin':
                    this.camera.zoomAt(1.35);
                    this.invalidate();
                    break;
                case 'zoomout':
                    this.camera.zoomAt(1 / 1.35);
                    this.invalidate();
                    break;
                case 'pan':
                case 'orbit': {
                    const previous = this.navMode;
                    this.cancel(false);
                    this.navMode = previous === id ? null : id;
                    if (id === 'orbit' && this.workspace === '2d' && this.navMode) {
                        this.workspace = '3d';
                        this.renderer.style = this.doc.entities.some(e => e.type === 'MESH') ? 'shaded-edges' : 'wireframe';
                    }
                    this.log('Navigate', this.navMode ? (id === 'pan' ? 'Drag to pan. Escape returns to selection.' : 'Drag to orbit. Escape returns to selection.') : 'Selection mode.');
                    this.refreshRibbon();
                    break;
                }
                case 'perspective':
                    this.camera.perspective = !this.camera.perspective;
                    this.camera.update();
                    this.refreshStatus();
                    this.invalidate();
                    break;
                case 'theme':
                    this.theme = this.theme === 'dark' ? 'light' : 'dark';
                    this.applyTheme();
                    this.savePreferences();
                    break;
                case 'toggle-explorer':
                    $('workbench').classList.toggle('hide-explorer');
                    break;
                case 'toggle-properties':
                    $('workbench').classList.toggle('hide-properties');
                    break;
                case 'command-history': {
                    const h = $('command-dock').offsetHeight;
                    document.documentElement.style.setProperty('--command-height', h > 150 ? '103px' : '240px');
                    break;
                }
                case 'selectall':
                    this.doc.selection = new Set(this.doc.entities.filter(e => this.doc.editable(e)).map(e => e.id));
                    this.selectionChanged();
                    break;
                case 'clear-selection':
                    this.doc.selection.clear();
                    this.cancel(false);
                    this.selectionChanged();
                    break;
                case 'erase':
                    this.erase();
                    break;
                case 'color-bylayer': {
                    const selected = this.doc.selected(true);
                    if (selected.length)
                        this.doc.transaction('Color by layer', () => selected.forEach(e => this.doc.replace(e.id, { ...e, color: 'bylayer' })));
                    else {
                        this.defaults.color = 'bylayer';
                        this.refreshInspector();
                        this.refreshRibbon();
                        this.savePreferences();
                    }
                    break;
                }
                case 'polygon':
                    this.dialog({ title: 'Regular polygon', html: `<p>Choose the number of sides, then pick a center and radius in the viewport.</p><div class="form-grid">${this.field('sides', 'Number of sides', 6, 'number', 'min="3" max="128" step="1"')}</div>`, submit: 'Place polygon', onSubmit: f => { this.startTool('polygon', { sides: Math.round(finite(f.sides, 'Sides', 3, 128)) }); } });
                    break;
                case 'text':
                    this.textDialog();
                    break;
                case 'hatch': {
                    const selected = this.doc.selected(true).filter(G.closed);
                    if (selected.length)
                        this.hatchDialog(selected);
                    else
                        this.startTool('hatch');
                    break;
                }
                case 'offset':
                    this.offsetDialog();
                    break;
                case 'fillet':
                case 'chamfer':
                    this.cornerDialog(id);
                    break;
                case 'array':
                    this.arrayDialog();
                    break;
                case 'join':
                    this.join();
                    break;
                case 'explode':
                    this.explode();
                    break;
                case 'group':
                    this.groupDialog();
                    break;
                case 'ungroup': {
                    const selected = this.doc.selected(true);
                    if (!selected.length)
                        throw Error('Select grouped objects first.');
                    this.doc.transaction('Ungroup', () => { const groups = new Set(selected.map(e => e.group)); for (const e of this.doc.entities)
                        if (e.group && groups.has(e.group) && this.doc.editable(e)) {
                            const out = { ...e };
                            delete out.group;
                            delete out.groupName;
                            this.doc.replace(e.id, out);
                        } });
                    break;
                }
                case 'area':
                    this.measureArea();
                    break;
                case 'extrude': {
                    const selected = this.doc.selected(true).filter(G.closed);
                    if (selected.length)
                        this.extrudeDialog(selected);
                    else
                        this.startTool('extrude');
                    break;
                }
                case 'revolve':
                    this.revolveDialog();
                    break;
                case 'union':
                case 'subtract':
                case 'intersect':
                    await this.booleanOperation(id);
                    break;
                case 'section':
                    this.sectionDialog();
                    break;
                case 'transform3d':
                    this.transform3dDialog();
                    break;
                case 'layers':
                    this.layerDialog();
                    break;
                case 'new-layer':
                    this.newLayerDialog();
                    break;
                case 'all-layers-on':
                    this.doc.transaction('Show all layers', () => this.doc.layers.forEach(l => l.visible = true));
                    break;
                case 'units':
                    this.unitsDialog();
                    break;
                case 'filter':
                    this.filterDialog();
                    break;
                case 'audit':
                    this.auditDialog();
                    break;
                case 'purge': {
                    const used = new Set(this.doc.entities.map(e => e.layer));
                    used.add('0');
                    used.add(this.doc.currentLayer);
                    const count = this.doc.layers.filter(l => !used.has(l.id)).length;
                    if (count) {
                        this.doc.transaction('Purge layers', () => this.doc.layers = this.doc.layers.filter(l => used.has(l.id)));
                        this.toast(`Removed ${count} unused layers.`);
                    }
                    else
                        this.toast('No unused layers to purge.');
                    break;
                }
                case 'demo-2d':
                    this.addDocument(K.Examples.courtyard());
                    this.workspace = '2d';
                    this.camera.setView('top');
                    this.renderer.style = 'wireframe';
                    this.defaults.textHeight = 160;
                    this.defaults.dimensionHeight = 145;
                    this.setRibbon('Home');
                    this.fit(false);
                    this.refresh();
                    break;
                case 'demo-3d':
                    this.addDocument(K.Examples.fixture());
                    this.setWorkspace('3d');
                    this.defaults.textHeight = 5;
                    this.defaults.dimensionHeight = 5;
                    break;
                case 'benchmark':
                    this.benchmarkDialog();
                    break;
                case 'diagnostics':
                    this.diagnosticsDialog();
                    break;
                case 'download-diagnostics':
                    this.download(JSON.stringify(this.diagnosticData(), null, 2), 'kestrel-diagnostics.json', 'application/json');
                    break;
                case 'dwg-setup':
                    await this.dwgDialog();
                    break;
                case 'help':
                    this.helpDialog();
                    break;
                case 'palette':
                    this.openPalette();
                    break;
                case 'close-tool':
                    this.closePolyline();
                    break;
                case 'finish-tool':
                    this.finishTool();
                    break;
                case 'cancel-tool':
                    this.cancel();
                    break;
                case 'copy-clipboard':
                    this.copyClipboard();
                    break;
                case 'paste':
                    this.pasteClipboard();
                    break;
                default: throw Error('Unknown command: ' + id);
            }
        }
        ensureSelection(min = 1, types = null) { const selected = this.doc.selected(true).filter(e => !types || types.includes(e.type)); if (selected.length < min)
            throw Error(`Select ${min === 1 ? 'an editable object' : `at least ${min} editable objects`}${types ? ' of type ' + types.join(' / ') : ''} first.`); return selected; }
        addGeometry(e, select = false) { const layer = this.doc.layer(this.doc.currentLayer); if (layer.locked)
            throw Error('The current layer is locked. Choose an unlocked layer.'); const out = this.doc.add({ ...e, id: K.uid(), layer: e.layer || this.doc.currentLayer, color: e.color || this.defaults.color, lineweight: e.lineweight || this.defaults.lineweight || undefined }); if (select)
            this.doc.selection = new Set([out.id]); return out; }
        create(e, label) { let added; this.doc.transaction(label || this.entityLabel(e), () => { added = this.addGeometry(e); }); this.lastPoint = (e.points?.at(-1) || e.center || e.position || this.lastPoint).slice(); this.log(label || e.type, 'Created ' + this.entityLabel(added) + '.'); return added; }
        erase() { const selected = this.doc.selected(true); if (!selected.length) {
            this.startTool('erase');
            return;
        } this.doc.transaction('Erase ' + selected.length + ' objects', () => this.doc.remove(selected.map(e => e.id))); this.log('Erase', selected.length + ' object(s) removed.'); this.cancel(false); }
        textDialog(entity = null) { this.dialog({ title: entity ? 'Edit text' : 'Create text', html: `<p>${entity ? 'Change the selected text entity.' : 'Set the text, then click its insertion point in the drawing.'}</p><div class="form-grid"><div class="form-field full"><label for="field-text">Text content</label><textarea id="field-text" name="text" required maxlength="10000">${esc(entity?.text || 'New annotation')}</textarea></div>${this.field('height', 'Text height (' + this.doc.units + ')', entity?.height || this.defaults.textHeight, 'number', 'min="0.000001"')}${this.field('rotation', 'Rotation (degrees)', (entity?.rotation || 0) * 180 / Math.PI)}<div class="form-field"><label>Alignment</label><select name="align">${['left', 'center', 'right'].map(v => `<option ${entity?.align === v ? 'selected' : ''}>${v}</option>`).join('')}</select></div></div>`, submit: entity ? 'Apply text' : 'Place text', onSubmit: f => { const props = { text: f.text, height: positive(f.height, 'Height'), rotation: finite(f.rotation) * Math.PI / 180, align: f.align }; if (entity)
                this.doc.transaction('Edit text', () => { const out = { ...entity, ...props }; delete out.direction; this.doc.replace(entity.id, out); });
            else {
                this.defaults.textHeight = props.height;
                this.startTool('text', props);
            } } }); }
        hatchDialog(entities) { this.cancel(false); const size = centerOf(entities).size, defaultSpacing = Math.max(.1, Math.max(size[0], size[1]) / 35); this.dialog({ title: 'Hatch selected boundaries', html: `<p>${entities.length} closed ${entities.length === 1 ? 'boundary' : 'boundaries'} selected. Patterns are clipped to each simple polygon.</p><div class="form-grid"><div class="form-field"><label>Pattern</label><select name="pattern"><option value="ANSI31">ANSI31 — diagonal</option><option value="cross">Crosshatch</option><option value="solid">Solid fill</option></select></div>${this.field('spacing', 'Spacing (' + this.doc.units + ')', Number(defaultSpacing.toPrecision(3)), 'number', 'min="0.001"')}${this.field('angle', 'Angle (degrees)', 45)}</div><div class="dialog-note">The hatch is an independent entity. Islands and associative boundary updates are not part of this implementation.</div>`, submit: 'Create hatch', onSubmit: f => { const spacing = positive(f.spacing), a = finite(f.angle) * Math.PI / 180, ids = []; this.doc.transaction('Hatch boundaries', () => { for (const e of entities) {
                const p = G.path(e, .5);
                const h = this.addGeometry({ type: 'HATCH', points: p, pattern: f.pattern, spacing, angle: a, normal: faceNormal(p), layer: this.doc.layerMap.has('hatch') ? 'hatch' : this.doc.currentLayer });
                ids.push(h.id);
            } this.doc.selection = new Set(ids); }); } }); }
        offsetDialog() { const selected = this.doc.selected(true).filter(e => ['LINE', 'POLYLINE', 'CIRCLE', 'ARC'].includes(e.type)); this.dialog({ title: 'Offset geometry', html: `<p>Choose an offset distance. Then pick the object and the side on which to place the offset.</p><div class="form-grid">${this.field('distance', 'Distance (' + this.doc.units + ')', Math.max(.1, this.settings.snapSpacing), 'number', 'min="0.000001"')}</div><div class="dialog-note">Lines, planar polylines, circles and arcs are supported. Polyline offsets use mitered corners; self-intersection cleanup is not automatic.</div>`, submit: 'Choose offset side', onSubmit: f => { this.startTool('offset', { distance: positive(f.distance) }); if (selected.length === 1)
                this.tool.source = selected[0]; this.updateToolPrompt(); } }); }
        cornerDialog(id) { const selected = this.doc.selected(true).filter(e => e.type === 'LINE'); this.dialog({ title: id === 'fillet' ? 'Fillet two lines' : 'Chamfer two lines', html: `<p>${selected.length === 2 ? 'The two selected lines will be trimmed to the new corner.' : 'After confirming, click the two line segments near the ends to retain.'}</p><div class="form-grid">${this.field('distance', id === 'fillet' ? 'Radius (' + this.doc.units + ')' : 'Setback distance (' + this.doc.units + ')', Math.max(.1, this.settings.snapSpacing), 'number', 'min="0.000001"')}</div>`, submit: selected.length === 2 ? 'Create corner' : 'Pick two lines', onSubmit: f => { const value = positive(f.distance); if (selected.length === 2)
                this.cornerOperation(id, selected[0], selected[1], value);
            else
                this.startTool(id, { distance: value }); } }); }
        cornerOperation(id, e1, e2, distance, pick1 = null, pick2 = null) { const hit = lineIntersection(...e1.points, ...e2.points); if (!hit)
            throw Error('Parallel lines do not define a corner.'); const p = hit.point, far = (e, pick) => pick ? e.points.reduce((a, b) => V.dist(a, pick) < V.dist(b, pick) ? a : b) : e.points.reduce((a, b) => V.dist(a, p) > V.dist(b, p) ? a : b), a = far(e1, pick1), b = far(e2, pick2), u = V.norm(V.sub(a, p)), v = V.norm(V.sub(b, p)), theta = Math.acos(clamp(V.dot(u, v), -1, 1)); if (theta < .001 || Math.abs(theta - Math.PI) < .001)
            throw Error('The corner angle is degenerate.'); const setback = id === 'fillet' ? distance / Math.tan(theta / 2) : distance; if (setback > V.dist(a, p) + EPS || setback > V.dist(b, p) + EPS)
            throw Error('The radius or setback is too large for the selected segments.'); const t1 = V.add(p, V.mul(u, setback)), t2 = V.add(p, V.mul(v, setback)); this.doc.transaction(id === 'fillet' ? 'Fillet lines' : 'Chamfer lines', () => { this.doc.replace(e1.id, { ...e1, points: [a, t1] }); this.doc.replace(e2.id, { ...e2, points: [b, t2] }); if (id === 'chamfer')
            this.addGeometry({ type: 'LINE', points: [t1, t2], layer: e1.layer });
        else {
            const c = V.add(p, V.mul(V.norm(V.add(u, v)), distance / Math.sin(theta / 2))), aa = Math.atan2(t1[1] - c[1], t1[0] - c[0]), bb = Math.atan2(t2[1] - c[1], t2[0] - c[0]);
            this.addGeometry({ type: 'ARC', center: c, radius: distance, startAngle: angle(bb - aa) < Math.PI ? aa : bb, endAngle: angle(bb - aa) < Math.PI ? bb : aa, layer: e1.layer });
        } }); this.cancel(false); this.log(id === 'fillet' ? 'Fillet' : 'Chamfer', 'Created a trimmed corner.'); }
        arrayDialog() { const selected = this.ensureSelection(), b = centerOf(selected); this.dialog({ title: 'Create an array', html: `<p>${selected.length} object(s) will be copied. The original selection is retained.</p><div class="form-grid"><div class="form-field full"><label>Array type</label><select name="kind"><option value="rectangular">Rectangular array</option><option value="polar">Polar array</option></select></div>${this.field('columns', 'Columns (rectangular)', 3, 'number', 'min="1" max="200" step="1"')}${this.field('rows', 'Rows (rectangular)', 2, 'number', 'min="1" max="200" step="1"')}${this.field('dx', 'Column spacing', Math.max(b.size[0] * 1.3, 10))}${this.field('dy', 'Row spacing', Math.max(b.size[1] * 1.3, 10))}<div class="form-divider"></div>${this.field('count', 'Count including original (polar)', 6, 'number', 'min="2" max="1000" step="1"')}${this.field('angle', 'Total angle (polar, degrees)', 360)}${this.field('cx', 'Polar center X', b.center[0])}${this.field('cy', 'Polar center Y', b.center[1])}</div>`, submit: 'Create array', onSubmit: f => { const matrices = []; if (f.kind === 'polar') {
                const count = Math.round(finite(f.count, 'Count', 2, 1000)), angle = finite(f.angle) * Math.PI / 180, c = [finite(f.cx), finite(f.cy), 0], div = Math.abs(Math.abs(angle) - TAU) < 1e-6 ? count : count - 1;
                for (let i = 1; i < count; i++)
                    matrices.push(M.around(c, M.rotation(angle * i / div)));
            }
            else {
                const columns = Math.round(finite(f.columns, 'Columns', 1, 200)), rows = Math.round(finite(f.rows, 'Rows', 1, 200)), dx = finite(f.dx), dy = finite(f.dy);
                for (let r = 0; r < rows; r++)
                    for (let c = 0; c < columns; c++)
                        if (r || c)
                            matrices.push(M.translation(c * dx, r * dy));
            } if (matrices.length * selected.length > 100000)
                throw Error('Array limit: 100,000 new objects.'); const ids = selected.map(e => e.id); this.doc.transaction('Create ' + f.kind + ' array', () => { for (const mat of matrices)
                for (const e of selected) {
                    const out = G.transform(e, mat);
                    delete out.id;
                    delete out.group;
                    const n = this.doc.add(out);
                    ids.push(n.id);
                } this.doc.selection = new Set(ids); }); } }); }
        join() { const selected = this.ensureSelection(2, ['LINE', 'POLYLINE']); let points = G.path(selected[0]).map(p => p.slice()), remaining = selected.slice(1), tolerance = Math.max(1e-6, Math.max(...centerOf(selected).size) * 1e-7); while (remaining.length) {
            let found = false;
            for (let i = 0; i < remaining.length; i++) {
                let p = G.path(remaining[i]);
                if (V.same(points.at(-1), p[0], tolerance))
                    points.push(...p.slice(1));
                else if (V.same(points.at(-1), p.at(-1), tolerance))
                    points.push(...p.slice(0, -1).reverse());
                else if (V.same(points[0], p.at(-1), tolerance))
                    points.unshift(...p.slice(0, -1));
                else if (V.same(points[0], p[0], tolerance))
                    points.unshift(...p.slice(1).reverse());
                else
                    continue;
                remaining.splice(i, 1);
                found = true;
                break;
            }
            if (!found)
                throw Error('The selected paths must form one connected, unbranched chain.');
        } const closed = V.same(points[0], points.at(-1), tolerance); if (closed)
            points.pop(); this.doc.transaction('Join paths', () => { this.doc.remove(selected.map(e => e.id)); this.addGeometry({ type: 'POLYLINE', points, closed, layer: selected[0].layer }, true); }); this.log('Join', 'Created one ' + (closed ? 'closed' : 'open') + ' polyline.'); }
        explode() { const selected = this.ensureSelection(), newEntities = []; for (const e of selected) {
            if (e.type === 'POLYLINE' || e.type === 'DIMENSION' || e.type === 'HATCH') {
                const g = this.doc.geometry(e);
                for (const s of g.segments)
                    newEntities.push({ ...e, type: 'LINE', points: s, closed: undefined });
                for (const t of g.texts)
                    newEntities.push({ ...e, ...t, type: 'TEXT' });
            }
            else if (e.type === 'MESH') {
                for (const t of this.doc.geometry(e).triangles)
                    newEntities.push({ ...e, vertices: t.points, faces: [[0, 1, 2]], primitive: '3D face' });
            }
            else
                continue;
        } if (!newEntities.length)
            throw Error('Select a polyline, dimension, hatch or mesh to explode.'); if (newEntities.length > 200000)
            throw Error('Explode would exceed the 200,000-entity limit.'); this.doc.transaction('Explode objects', () => { this.doc.remove(selected.filter(e => ['POLYLINE', 'DIMENSION', 'HATCH', 'MESH'].includes(e.type)).map(e => e.id)); const ids = []; for (const e of newEntities) {
            delete e.id;
            delete e.group;
            const n = this.doc.add(e);
            ids.push(n.id);
        } this.doc.selection = new Set(ids); }); }
        groupDialog() { const selected = this.ensureSelection(2); this.dialog({ title: 'Group selected objects', html: `<p>${selected.length} entities will share a selection group. Geometry remains directly editable.</p><div class="form-grid">${this.field('name', 'Group name', 'Group ' + this.doc.entities.filter(e => e.group).length, 'text', 'maxlength="80" required')}</div>`, submit: 'Create group', onSubmit: f => { const id = K.uid('group'); this.doc.transaction('Create group', () => selected.forEach(e => this.doc.replace(e.id, { ...e, group: id, groupName: f.name.trim() || 'Group' }))); } }); }
        measureArea() { const selected = this.ensureSelection().filter(e => G.closed(e) || e.type === 'MESH'); if (!selected.length)
            throw Error('Select a closed planar boundary or mesh.'); let area = 0, volume = 0; for (const e of selected) {
            if (e.type === 'MESH') {
                volume += Math.abs(G.volume(e));
                for (const t of this.doc.geometry(e).triangles)
                    area += V.len(V.cross(V.sub(t.points[1], t.points[0]), V.sub(t.points[2], t.points[0]))) / 2;
            }
            else if (e.type === 'CIRCLE')
                area += Math.PI * e.radius ** 2;
            else {
                const p = G.path(e), n = faceNormal(p);
                let a = [0, 0, 0];
                for (let i = 0; i < p.length; i++)
                    a = V.add(a, V.cross(p[i], p[(i + 1) % p.length]));
                area += Math.abs(V.dot(a, n)) / 2;
            }
        } this.dialog({ title: 'Measurement results', html: `<div class="report-stat-grid" style="grid-template-columns:1fr 1fr"><div class="report-stat"><strong>${area.toLocaleString(undefined, { maximumFractionDigits: 3 })}</strong><span>${selected.some(e => e.type === 'MESH') ? 'Surface / boundary area' : 'Boundary area'} · ${this.doc.units}²</span></div><div class="report-stat"><strong>${volume.toLocaleString(undefined, { maximumFractionDigits: 3 })}</strong><span>Mesh volume · ${this.doc.units}³</span></div></div><p>Curved boundary area and mesh surface area are evaluated from tessellated geometry, except circles.</p>`, submit: 'Close', closeOnly: true }); this.log('Measure', `Area ${area.toFixed(3)} ${this.doc.units}² · volume ${volume.toFixed(3)} ${this.doc.units}³.`); }
        primitiveDialog(kind) { this.cancel(false); const c = this.camera.target, defaultSize = Math.max(10, Math.round(this.camera.height / this.camera.zoom / 12 / 10) * 10); const labels = { box: 'Box', cylinder: 'Cylinder', sphere: 'Sphere', cone: 'Cone / frustum', torus: 'Torus' }; let fields = this.field('x', kind === 'box' ? 'Minimum corner X' : 'Center X', Number(c[0].toFixed(2))) + this.field('y', kind === 'box' ? 'Minimum corner Y' : 'Center Y', Number(c[1].toFixed(2))) + this.field('z', 'Base Z', 0); if (kind === 'box')
            fields += this.field('width', 'Width X', defaultSize) + this.field('depth', 'Depth Y', defaultSize * .7) + this.field('height', 'Height Z', defaultSize * .5);
        else if (kind === 'torus')
            fields += this.field('radius', 'Major radius', defaultSize * .5) + this.field('minor', 'Tube radius', defaultSize * .13);
        else {
            fields += this.field('radius', 'Radius', defaultSize * .4);
            if (kind !== 'sphere')
                fields += this.field('height', 'Height', defaultSize * .7);
            if (kind === 'cone')
                fields += this.field('topRadius', 'Top radius (0 for cone)', 0, 'number', 'min="0"');
        } this.dialog({ title: 'Create ' + labels[kind].toLowerCase(), html: `<p>Coordinates and dimensions are in <strong>${esc(this.doc.units)}</strong>. The result is an editable polygonal mesh.</p><div class="form-grid three">${fields}</div>`, submit: 'Create ' + kind, onSubmit: f => { const p = [finite(f.x), finite(f.y), finite(f.z)]; let e; if (kind === 'box')
                e = G.box(p, positive(f.width), positive(f.depth), positive(f.height)); if (kind === 'cylinder')
                e = G.cylinder(p, positive(f.radius), positive(f.height)); if (kind === 'cone')
                e = G.cylinder(p, positive(f.radius), positive(f.height), 64, finite(f.topRadius, 'Top radius', 0, 1e9)); if (kind === 'sphere')
                e = G.sphere(p, positive(f.radius)); if (kind === 'torus') {
                const radius = positive(f.radius), minor = positive(f.minor);
                if (minor >= radius)
                    throw Error('Tube radius must be smaller than major radius.');
                e = G.torus(p, radius, minor);
            } this.doc.transaction('Create ' + kind, () => this.addGeometry(e, true)); if (this.workspace === '2d') {
                this.workspace = '3d';
                this.camera.setView('iso');
            } this.renderer.style = 'shaded-edges'; this.refresh(); this.fit(false); this.setRibbon('Model'); } }); }
        extrudeDialog(selected) { this.cancel(false); this.dialog({ title: 'Extrude closed profiles', html: `<p>${selected.length} closed profile(s) selected. Extrusion produces mesh solids with triangulated caps.</p><div class="form-grid">${this.field('height', 'Extrusion height (' + this.doc.units + ')', Math.max(this.settings.snapSpacing, 1))}<div class="form-field"><label>&nbsp;</label><label class="form-check"><input type="checkbox" name="keep" checked>Keep source profiles</label></div></div>`, submit: 'Extrude', onSubmit: f => { const height = finite(f.height); if (Math.abs(height) < EPS)
                throw Error('Extrusion height cannot be zero.'); const meshes = selected.map(e => { const p = G.path(e, .25), normal = faceNormal(p); if (V.len(normal) < EPS)
                throw Error('A profile is degenerate.'); const extent = Math.max(...centerOf([e]).size, 1); if (p.some(q => Math.abs(V.dot(V.sub(q, p[0]), normal)) > extent * 1e-6))
                throw Error('Extrusion profiles must be planar.'); const n = Math.abs(normal[2]) > .999 ? [0, 0, 1] : normal; return { ...G.extrude(p, height, n), layer: e.layer, color: e.color }; }); this.doc.transaction('Extrude profiles', () => { const ids = []; for (const e of meshes)
                ids.push(this.addGeometry(e).id); if (!f.keep)
                this.doc.remove(selected.map(e => e.id)); this.doc.selection = new Set(ids); }); this.workspace = '3d'; this.camera.setView('iso'); this.renderer.style = 'shaded-edges'; this.setRibbon('Model'); this.fit(false); this.refresh(); } }); }
        revolveDialog() { const selected = this.ensureSelection().filter(G.closed); if (!selected.length)
            throw Error('Select a closed profile that does not cross the revolution axis.'); const b = centerOf(selected); this.dialog({ title: 'Revolve closed profiles', html: `<p>The profile revolves around a world axis through the specified origin. Profiles must not cross the axis.</p><div class="form-grid"><div class="form-field"><label>Axis</label><select name="axis"><option value="y">World Y</option><option value="x">World X</option><option value="z">World Z</option></select></div>${this.field('angle', 'Sweep (degrees)', 360, 'number', 'min="1" max="360"')}${this.field('x', 'Axis origin X', 0)}${this.field('y', 'Axis origin Y', 0)}${this.field('z', 'Axis origin Z', 0)}${this.field('segments', 'Angular segments', 64, 'number', 'min="8" max="128" step="1"')}<label class="form-check"><input type="checkbox" name="keep" checked>Keep source profiles</label></div>`, submit: 'Revolve', onSubmit: f => { const axis = { x: [1, 0, 0], y: [0, 1, 0], z: [0, 0, 1] }[f.axis], origin = [finite(f.x), finite(f.y), finite(f.z)], degrees = finite(f.angle, 'Sweep', 1, 360), segments = Math.round(finite(f.segments, 'Segments', 8, 128)); const meshes = selected.map(e => ({ ...G.revolve(G.path(e, 1), origin, axis, degrees, segments), layer: e.layer, color: e.color })); this.doc.transaction('Revolve profiles', () => { const ids = meshes.map(e => this.addGeometry(e).id); if (!f.keep)
                this.doc.remove(selected.map(e => e.id)); this.doc.selection = new Set(ids); }); this.workspace = '3d'; this.camera.setView('iso'); this.renderer.style = 'shaded-edges'; this.setRibbon('Model'); this.fit(false); this.refresh(); } }); }
        async booleanOperation(op) { const selected = this.ensureSelection(2, ['MESH']); if (selected.some(e => Math.abs(G.volume(e)) < 1e-8))
            throw Error('Boolean operands must have non-zero enclosed volume.'); this.log(op, 'Computing mesh Boolean…'); await new Promise(r => requestAnimationFrame(r)); const start = performance.now(); let result = K.clone(selected[0]); for (const e of selected.slice(1))
            result = K.CSG.boolean(result, e, op); if (!result.faces.length && op === 'intersect')
            this.log('Intersect', 'Operands have no common volume.'); this.doc.transaction('Mesh ' + op, () => { this.doc.remove(selected.map(e => e.id)); if (result.faces.length)
            this.addGeometry(result, true); }); this.renderer.style = 'shaded-edges'; this.refresh(); this.log(op, `${result.faces.length.toLocaleString()} result faces · ${(performance.now() - start).toFixed(0)} ms CPU.`); }
        transform3dDialog() { const selected = this.ensureSelection(), center = centerOf(selected).center; this.dialog({ title: 'Transform in 3D', html: `<p>Rotation is applied around the selection center, followed by translation.</p><div class="form-grid three">${this.field('x', 'Translate X', 0)}${this.field('y', 'Translate Y', 0)}${this.field('z', 'Translate Z', 0)}${this.field('rx', 'Rotate X (degrees)', 0)}${this.field('ry', 'Rotate Y (degrees)', 0)}${this.field('rz', 'Rotate Z (degrees)', 0)}${this.field('scale', 'Uniform scale', 1, 'number', 'min="0.000001"')}<label class="form-check"><input type="checkbox" name="copy">Create a copy</label></div>`, submit: 'Transform', onSubmit: f => { const rot = M.multiply(M.rotation(finite(f.rz) * Math.PI / 180, [0, 0, 1]), M.multiply(M.rotation(finite(f.ry) * Math.PI / 180, [0, 1, 0]), M.rotation(finite(f.rx) * Math.PI / 180, [1, 0, 0]))), mat = M.multiply(M.translation(finite(f.x), finite(f.y), finite(f.z)), M.around(center, M.multiply(rot, M.scale(positive(f.scale))))); this.doc.transaction('3D transform', () => this.doc.transform(selected.map(e => e.id), mat, !!f.copy)); } }); }
        sectionDialog() { const selected = this.ensureSelection(1, ['MESH']), b = centerOf(selected); this.dialog({ title: 'Create a horizontal section', html: `<p>Intersect the selected meshes with a plane at world Z. The result is independent line geometry.</p><div class="form-grid">${this.field('z', 'Section elevation Z', Number(b.center[2].toFixed(3)))}</div>`, submit: 'Create section', onSubmit: f => { const z = finite(f.z), segments = []; for (const e of selected)
                for (const t of this.doc.geometry(e).triangles) {
                    const hits = [];
                    for (let i = 0; i < 3; i++) {
                        const a = t.points[i], b = t.points[(i + 1) % 3];
                        if ((a[2] < z && b[2] > z) || (a[2] > z && b[2] < z))
                            hits.push(V.lerp(a, b, (z - a[2]) / (b[2] - a[2])));
                        else if (Math.abs(a[2] - z) < 1e-7)
                            hits.push(a);
                    }
                    const unique = hits.filter((p, i) => !hits.slice(0, i).some(q => V.same(p, q, 1e-6)));
                    if (unique.length === 2 && V.dist(...unique) > 1e-6)
                        segments.push(unique);
                } if (!segments.length)
                throw Error('The section plane does not cross the selected meshes.'); this.doc.transaction('Section meshes', () => { let layer = this.doc.layers.find(l => l.name === 'M-SECTION'); if (!layer)
                layer = this.doc.addLayer('M-SECTION', '#db8a9e'); const ids = segments.map(points => this.doc.add('LINE', { points, layer: layer.id }).id); this.doc.selection = new Set(ids); }); this.log('Section', segments.length + ' section segments created.'); } }); }
        startTool(id, params = {}) {
            const selected = this.doc.selected(true);
            this.cancel(false);
            if (this.sheet)
                this.setLayout('model');
            this.navMode = null;
            this.tool = { id, params, points: [], selectedIds: selected.map(e => e.id), stage: 'points' };
            if (['move', 'copy', 'rotate', 'scale', 'mirror', 'erase'].includes(id) && !selected.length)
                this.tool.stage = 'selection';
            if (['trim', 'extend', 'match', 'hatch', 'extrude', 'fillet', 'chamfer', 'offset'].includes(id))
                this.tool.stage = 'pick';
            if (id === 'match' && selected.length === 1)
                this.tool.source = selected[0];
            if (id === 'scale')
                this.tool.reference = Math.max(...centerOf(selected).size, 1) / 2;
            if (id === 'paste')
                this.tool.stage = 'points';
            if (Math.abs(this.camera.direction[2]) < .015 && ['line', 'polyline', 'rectangle', 'circle', 'arc', 'ellipse', 'spline', 'polygon', 'dimension', 'hatch', 'text', 'point'].includes(id)) {
                this.camera.setView('top');
                this.log('WCS', 'Switched to Top view for drawing on the world XY plane.');
            }
            this.lastTool = id;
            this.measureResult = null;
            this.updateToolPrompt();
            this.refreshRibbon();
            this.invalidate();
            $('viewport').focus({ preventScroll: true });
            this.log(id.toUpperCase(), this.prompt());
        }
        prompt() { const t = this.tool; if (!t)
            return ''; const n = t.points.length; if (t.stage === 'selection')
            return 'Select objects, then press Enter'; switch (t.id) {
            case 'line': return n ? 'Specify next point or [Undo]' : 'Specify first point';
            case 'polyline': return n ? 'Next vertex or [Close / Undo / Enter]' : 'Specify first vertex';
            case 'spline': return n ? 'Next control point or Enter to finish' : 'Specify first control point';
            case 'rectangle': return n ? 'Specify opposite corner' : 'Specify first corner';
            case 'circle': return n ? 'Specify radius or a point on the circle' : 'Specify center point';
            case 'arc': return ['Specify start point', 'Specify point on arc', 'Specify end point'][n] || 'Specify end point';
            case 'ellipse': return ['Specify center', 'Specify major-axis endpoint or radius', 'Specify minor radius'][n] || '';
            case 'polygon': return n ? 'Specify circumradius' : 'Specify polygon center';
            case 'point': return 'Specify insertion point';
            case 'text': return 'Specify text insertion point';
            case 'dimension': return ['Specify first extension point', 'Specify second extension point', 'Specify dimension line position or offset'][n] || '';
            case 'measure': return n ? 'Specify second measurement point' : 'Specify first measurement point';
            case 'move':
            case 'copy': return n ? 'Specify destination or @dx,dy,dz' : 'Specify base point';
            case 'rotate': return n ? 'Specify rotation angle (degrees) or point' : 'Specify rotation center';
            case 'scale': return n ? 'Specify scale factor or target point' : 'Specify scale base point';
            case 'mirror': return n ? 'Specify second mirror-axis point' : 'Specify first mirror-axis point';
            case 'offset': return t.source ? 'Click the side to place the offset' : 'Select an object to offset';
            case 'fillet':
            case 'chamfer': return t.source ? 'Select second line, near the end to retain' : 'Select first line, near the end to retain';
            case 'trim': return 'Click the line interval to trim';
            case 'extend': return 'Click the line near the endpoint to extend';
            case 'hatch': return 'Select a closed boundary to hatch';
            case 'extrude': return 'Select a closed planar profile';
            case 'match': return t.source ? 'Select target object (Escape to finish)' : 'Select source object';
            case 'paste': return 'Specify clipboard insertion point';
            case 'erase': return 'Select objects, then Enter to erase';
            default: return 'Specify point';
        } }
        updateToolPrompt() { const t = this.tool; $('tool-banner').hidden = !t; $('tool-banner-name').textContent = t ? t.id.toUpperCase() : ''; $('tool-banner-text').textContent = this.prompt(); $('command-prefix').textContent = t ? t.id.toUpperCase() + ': ' + this.prompt() : 'Command:'; $('command-input').placeholder = t ? 'Enter coordinates, a value, or an option…' : 'Type a command or search…'; if (!t)
            $('dynamic-input').hidden = true; $('view-hint').textContent = this.workspace === '3d' ? 'Wheel to zoom · Middle drag to pan · Shift+middle to orbit' : 'Wheel to zoom · Middle drag to pan · Shift to add selection'; }
        cancel(log = true) { if (this.tool && log)
            this.log(this.tool.id.toUpperCase(), 'Cancelled.'); this.tool = null; this.navMode = null; this.snapTarget = null; this.measureResult = null; this.previewEntities = []; $('dynamic-input').hidden = true; $('tool-banner').hidden = true; $('command-input').value = ''; $('command-suggestions').hidden = true; if (this.doc) {
            this.updateToolPrompt();
            this.refreshRibbon();
        } this.invalidate(); }
        finishTool() {
            const t = this.tool;
            if (!t) {
                this.run(this.lastTool).catch(e => this.fail(e));
                return;
            }
            if (t.stage === 'selection') {
                const selected = this.doc.selected(true);
                if (!selected.length) {
                    this.log(t.id.toUpperCase(), 'Select at least one unlocked object.');
                    return;
                }
                if (t.id === 'erase') {
                    this.erase();
                    return;
                }
                t.selectedIds = selected.map(e => e.id);
                t.stage = 'points';
                if (t.id === 'scale')
                    t.reference = Math.max(...centerOf(selected).size, 1) / 2;
                this.updateToolPrompt();
                this.invalidate();
                return;
            }
            if (t.id === 'polyline') {
                if (t.points.length >= 2)
                    this.create({ type: 'POLYLINE', points: t.points.map(p => p.slice()), closed: false }, 'Polyline');
                this.cancel(false);
            }
            else if (t.id === 'spline') {
                if (t.points.length >= 2) {
                    const degree = Math.min(3, t.points.length - 1);
                    this.create({ type: 'SPLINE', controlPoints: t.points.map(p => p.slice()), degree, knots: G.uniformKnots(t.points.length, degree) }, 'Spline');
                }
                this.cancel(false);
            }
            else {
                this.cancel(false);
            }
            this.invalidate();
        }
        closePolyline() { if (this.tool?.id !== 'polyline')
            throw Error('Close is available while drawing a polyline.'); if (this.tool.points.length < 3)
            throw Error('A closed polyline needs at least three vertices.'); this.create({ type: 'POLYLINE', points: this.tool.points.map(p => p.slice()), closed: true }, 'Closed polyline'); this.cancel(false); }
        eventPoint(e) { const r = $('viewport').getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; }
        ensureIndex() { this.index.build(this.doc, this.camera, this.renderer.style); }
        hit(p, filter = null) {
            this.ensureIndex();
            let best = null;
            for (const item of this.index.near(p[0], p[1], 9)) {
                if (filter && !filter(item.e))
                    continue;
                let d = Infinity, depth = 1;
                for (const s of item.segments) {
                    const dist = segmentDistance(p, s[0], s[1]);
                    if (dist.d < d) {
                        d = dist.d;
                        depth = s[0][2] * (1 - dist.t) + s[1][2] * dist.t;
                    }
                }
                if (item.g.texts.length) {
                    for (const text of item.g.texts) {
                        if(text.composition){const quads=text.composition.quads.map(q=>q.map(v=>this.camera.project(v)));if(quads.some(q=>inside(p,q))){d=2;depth=this.camera.project(text.position)[2];}continue;}
                        const axes = G.textAxes(text), height = text.height || 10, lines = (text.text || '').split('\n'), w = Math.max(height, lines.reduce((n, l) => Math.max(n, l.length), 0) * height * .66), left = text.align === 'center' ? -w / 2 : text.align === 'right' ? -w : 0, bottom = -height * .25 - (lines.length - 1) * height * 1.35, top = height * .9, quad = [[left, bottom], [left + w, bottom], [left + w, top], [left, top]].map(([x, y]) => this.camera.project(V.add(text.position, V.add(V.mul(axes.x, x), V.mul(axes.y, y)))));
                        if (inside(p, quad) || quad.some((q, i) => segmentDistance(p, q, quad[(i + 1) % 4]).d < 4)) {
                            d = 2;
                            depth = this.camera.project(text.position)[2];
                        }
                    }
                }
                if (d > 8 && item.e.type === 'MESH' && this.renderer.style !== 'wireframe') {
                    let faceDepth = Infinity;
                    for (const face of item.faces) {
                        if (!inside(p, face))
                            continue;
                        const [a, b, c] = face, den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1]);
                        if (Math.abs(den) < 1e-10)
                            continue;
                        const u = ((b[1] - c[1]) * (p[0] - c[0]) + (c[0] - b[0]) * (p[1] - c[1])) / den, v = ((c[1] - a[1]) * (p[0] - c[0]) + (a[0] - c[0]) * (p[1] - c[1])) / den, z = u * a[2] + v * b[2] + (1 - u - v) * c[2];
                        if (z >= 0 && z <= 1)
                            faceDepth = Math.min(faceDepth, z);
                    }
                    if (Number.isFinite(faceDepth)) {
                        d = 7.5;
                        depth = faceDepth;
                    }
                }
                if (d <= 9 && (!best || d < best.distance - .3 || Math.abs(d - best.distance) < .3 && depth < best.depth))
                    best = { ...item, distance: d, depth };
            }
            return best;
        }
        selectAt(p, add = false) { const hit = this.hit(p); if (!hit) {
            if (!add)
                this.doc.selection.clear();
            this.selectionChanged();
            return null;
        } let ids = [hit.e.id]; if (hit.e.group && !this.ctrl)
            ids = this.doc.entities.filter(e => e.group === hit.e.group && this.doc.visible(e)).map(e => e.id); if (add) {
            const remove = this.doc.selection.has(hit.e.id);
            for (const id of ids)
                remove ? this.doc.selection.delete(id) : this.doc.selection.add(id);
        }
        else
            this.doc.selection = new Set(ids); this.selectionChanged(); return hit; }
        snapPoint(p, z = 0, excludeId = null) {
            let world = this.camera.unproject(p[0], p[1], z) || this.camera.pointOnView(p[0], p[1]);
            this.snapTarget = null;
            if (this.settings.osnap) {
                this.ensureIndex();
                let best = null;
                const nearby = this.index.near(p[0], p[1], 13).filter(i => i.e.id !== excludeId);
                for (const item of nearby)
                    for (const snap of item.g.snaps) {
                        const q = this.camera.project(snap.point), distance = Math.hypot(q[0] - p[0], q[1] - p[1]);
                        if (distance < 11 && (!best || distance < best.distance))
                            best = { ...snap, distance, screen: q };
                    }
                const cutting = [];
                for (const item of nearby) {
                    if (!['LINE', 'POLYLINE'].includes(item.e.type))
                        continue;
                    for (const s of item.g.segments) {
                        const a = this.camera.project(s[0]), b = this.camera.project(s[1]);
                        if (segmentDistance(p, a, b).d < 22 && Math.abs(s[0][2] - s[1][2]) < 1e-7)
                            cutting.push(s);
                        if (cutting.length > 40)
                            break;
                    }
                    if (cutting.length > 40)
                        break;
                }
                for (let i = 0; i < cutting.length; i++)
                    for (let j = i + 1; j < cutting.length; j++) {
                        if (Math.abs(cutting[i][0][2] - cutting[j][0][2]) > 1e-6)
                            continue;
                        const hit = lineIntersection(...cutting[i], ...cutting[j]);
                        if (!hit || hit.t < -EPS || hit.t > 1 + EPS || hit.u < -EPS || hit.u > 1 + EPS)
                            continue;
                        const q = this.camera.project(hit.point), distance = Math.hypot(q[0] - p[0], q[1] - p[1]);
                        if (distance < 11 && (!best || distance < best.distance))
                            best = { point: hit.point, type: 'intersection', distance, screen: q };
                    }
                if (best) {
                    this.snapTarget = best;
                    return best.point.slice();
                }
            }
            if (this.settings.snap) {
                const s = this.settings.snapSpacing;
                world = [Math.round(world[0] / s) * s, Math.round(world[1] / s) * s, z];
            }
            const base = this.tool?.points.at(-1);
            if (base && ['line', 'polyline', 'move', 'copy', 'mirror'].includes(this.tool.id)) {
                const dx = world[0] - base[0], dy = world[1] - base[1];
                if (this.settings.ortho || this.shift) {
                    if (Math.abs(dx) >= Math.abs(dy))
                        world[1] = base[1];
                    else
                        world[0] = base[0];
                }
                else if (this.settings.polar) {
                    const step = this.settings.polarAngle * Math.PI / 180, a = Math.round(Math.atan2(dy, dx) / step) * step, r = Math.hypot(dx, dy);
                    world[0] = base[0] + Math.cos(a) * r;
                    world[1] = base[1] + Math.sin(a) * r;
                }
            }
            return world;
        }
        pointerDown(e) {
            if (e.target.closest('button,select,input,#viewcube'))
                return;
            if (e.button > 2)
                return;
            e.preventDefault();
            $('context-menu').hidden = true;
            $('viewport').focus({ preventScroll: true });
            this.ctrl = e.ctrlKey || e.metaKey;
            const p = this.eventPoint(e);
            this.pointer = { ...this.pointer, x: p[0], y: p[1], inside: true, world: this.snapPoint(p) };
            let mode = e.button === 1 ? (e.shiftKey ? 'orbit' : 'pan') : this.navMode || 'pending';
            if (e.button === 2)
                mode = 'right-pending';
            const grip = e.button === 0 && !this.tool && !this.navMode ? this.gripAt(p) : null;
            if (grip)
                mode = 'grip';
            this.drag = { mode, start: p, previous: p, button: e.button, shift: e.shiftKey, ctrl: e.ctrlKey || e.metaKey, moved: false, grip, original: grip ? K.clone(grip.e) : null };
            $('viewport').setPointerCapture(e.pointerId);
        }
        pointerMove(e) {
            if (e.target.closest('button,select,input,#viewcube') && !this.drag) {
                this.pointer.inside = false;
                this.invalidate();
                return;
            }
            const p = this.eventPoint(e);
            this.shift = e.shiftKey;
            this.ctrl = e.ctrlKey || e.metaKey;
            this.pointer.x = p[0];
            this.pointer.y = p[1];
            this.pointer.inside = true;
            this.pointer.world = this.snapPoint(p, this.drag?.grip?.point?.[2] || 0, this.drag?.grip?.e?.id || null);
            $('coordinates').textContent = this.pointer.world.map(v => v.toFixed(3)).join(', ');
            if (this.drag) {
                const d = this.drag, dx = p[0] - d.previous[0], dy = p[1] - d.previous[1];
                if (Math.hypot(p[0] - d.start[0], p[1] - d.start[1]) > 4)
                    d.moved = true;
                if (d.mode === 'right-pending' && d.moved)
                    d.mode = this.workspace === '3d' ? 'orbit' : 'pan';
                if (d.mode === 'pan') {
                    this.camera.pan(dx, dy);
                    $('viewport').classList.add('panning');
                }
                else if (d.mode === 'orbit') {
                    this.camera.yaw -= dx * .007;
                    this.camera.pitch = clamp(this.camera.pitch + dy * .007, -Math.PI / 2 + .002, Math.PI / 2 - .002);
                    this.camera.update();
                    this.workspace = '3d';
                    $('viewport').classList.add('orbiting');
                    $('view-select').value = 'iso';
                }
                else if (d.mode === 'grip' && d.moved) {
                    d.preview = this.moveGrip(d.original, d.grip, this.pointer.world);
                }
                else if (d.mode === 'pending' && d.moved && (!this.tool || this.tool.stage === 'selection')) {
                    const box = $('selection-window');
                    box.hidden = false;
                    box.classList.toggle('crossing', p[0] < d.start[0]);
                    box.style.left = Math.min(p[0], d.start[0]) + 'px';
                    box.style.top = Math.min(p[1], d.start[1]) + 'px';
                    box.style.width = Math.abs(p[0] - d.start[0]) + 'px';
                    box.style.height = Math.abs(p[1] - d.start[1]) + 'px';
                }
                d.previous = p;
            }
            else {
                this.hoverId = (!this.tool || this.tool.stage === 'pick' || this.tool.stage === 'selection') ? this.hit(p)?.e.id || null : null;
            }
            this.updateDynamic();
            this.invalidate();
        }
        pointerUp(e) {
            if (!this.drag)
                return;
            const p = this.eventPoint(e), d = this.drag;
            try {
                if (d.mode === 'grip' && d.moved && d.preview) {
                    this.doc.transaction('Edit grip', () => this.doc.replace(d.original.id, d.preview));
                }
                else if (d.mode === 'right-pending' && !d.moved) {
                    this.showContextMenu(e.clientX, e.clientY);
                }
                else if (d.mode === 'pending') {
                    if (d.moved && (!this.tool || this.tool.stage === 'selection'))
                        this.selectWindow(d.start, p, d.shift || d.ctrl);
                    else if (!d.moved) {
                        if (this.tool?.stage === 'selection')
                            this.selectAt(p, d.shift || d.ctrl);
                        else if (this.tool)
                            this.acceptPoint(this.snapPoint(p), p);
                        else
                            this.selectAt(p, d.shift || d.ctrl);
                    }
                }
                else if (d.mode === 'grip' && !d.moved) { /* Grips are edited by dragging, not a click-only state. */ }
            }
            catch (error) {
                this.fail(error);
            }
            this.endDrag();
            this.updateToolPrompt();
            this.invalidate();
        }
        endDrag() { this.drag = null; $('selection-window').hidden = true; $('viewport').classList.remove('panning', 'orbiting'); }
        selectWindow(a, b, add) {
            this.ensureIndex();
            const rect = [Math.min(a[0], b[0]), Math.min(a[1], b[1]), Math.max(a[0], b[0]), Math.max(a[1], b[1])], crossing = b[0] < a[0], within = p => p[0] >= rect[0] && p[0] <= rect[2] && p[1] >= rect[1] && p[1] <= rect[3], edges = [[[rect[0], rect[1]], [rect[2], rect[1]]], [[rect[2], rect[1]], [rect[2], rect[3]]], [[rect[2], rect[3]], [rect[0], rect[3]]], [[rect[0], rect[3]], [rect[0], rect[1]]]], ids = [];
            for (const { e, bounds: q, segments, faces, g } of this.index.items.values()) {
                if (!this.doc.editable(e))
                    continue;
                let hit = false;
                if (!crossing)
                    hit = q[0] >= rect[0] && q[1] >= rect[1] && q[2] <= rect[2] && q[3] <= rect[3];
                else if (!(q[2] < rect[0] || q[0] > rect[2] || q[3] < rect[1] || q[1] > rect[3])) {
                    hit = segments.some(s => within(s[0]) || within(s[1]) || edges.some(edge => { const h = lineIntersection(...s, ...edge); return h && h.t >= 0 && h.t <= 1 && h.u >= 0 && h.u <= 1; })) || g.texts.length > 0 || faces.some(f => inside([rect[0], rect[1]], f));
                }
                if (hit) {
                    ids.push(e.id);
                    if (e.group)
                        for (const item of this.doc.entities)
                            if (item.group === e.group && this.doc.editable(item))
                                ids.push(item.id);
                }
            }
            if (!add)
                this.doc.selection.clear();
            for (const id of ids)
                this.doc.selection.add(id);
            this.selectionChanged();
            this.log('Select', this.doc.selection.size + ' object(s) selected.');
        }
        acceptPoint(p, screen = null) {
            const t = this.tool;
            if (!t)
                return;
            const n = t.points.length, id = t.id;
            if (t.stage === 'pick') {
                const hit = screen ? this.hit(screen, e => this.doc.editable(e) && (!['fillet', 'chamfer'].includes(id) || e.type === 'LINE') && (!t.source || !['fillet', 'chamfer'].includes(id) || e.id !== t.source.id)) : null;
                if (id === 'offset' && t.source) {
                    const sign = this.offsetSide(t.source, p), e = G.offset(t.source, t.params.distance * sign);
                    delete e.id;
                    this.create(e, 'Offset');
                    this.cancel(false);
                    return;
                }
                if (!hit)
                    throw Error('No eligible editable object at that point.');
                if (id === 'hatch' || id === 'extrude') {
                    if (!G.closed(hit.e))
                        throw Error('Choose a closed polyline, circle, ellipse or hatch.');
                    this.doc.selection = new Set([hit.e.id]);
                    this.selectionChanged();
                    if (id === 'hatch')
                        this.hatchDialog([hit.e]);
                    else
                        this.extrudeDialog([hit.e]);
                    return;
                }
                if (id === 'offset') {
                    if (!['LINE', 'POLYLINE', 'CIRCLE', 'ARC'].includes(hit.e.type))
                        throw Error('Offset supports lines, polylines, circles and arcs.');
                    t.source = hit.e;
                    this.doc.selection = new Set([hit.e.id]);
                    this.selectionChanged();
                    this.updateToolPrompt();
                    return;
                }
                if (id === 'fillet' || id === 'chamfer') {
                    if (!t.source) {
                        t.source = hit.e;
                        t.pick = p;
                        this.doc.selection = new Set([hit.e.id]);
                        this.selectionChanged();
                        this.updateToolPrompt();
                    }
                    else
                        this.cornerOperation(id, t.source, hit.e, t.params.distance, t.pick, p);
                    return;
                }
                if (id === 'match') {
                    if (!t.source) {
                        t.source = hit.e;
                        this.doc.selection = new Set([hit.e.id]);
                        this.selectionChanged();
                        this.updateToolPrompt();
                    }
                    else {
                        this.doc.transaction('Match properties', () => this.doc.replace(hit.e.id, { ...hit.e, layer: t.source.layer, color: t.source.color, linetype: t.source.linetype, lineweight: t.source.lineweight }));
                    }
                    return;
                }
                if (id === 'trim' || id === 'extend') {
                    this.trimExtend(hit.e, p, id === 'extend', t.selectedIds);
                    return;
                }
            }
            if (id === 'point') {
                this.create({ type: 'POINT', position: p, size: Math.max(.01, this.renderer.gridSpacing() * .08) }, 'Point');
                return;
            }
            if (id === 'text') {
                this.create({ type: 'TEXT', position: p, ...t.params }, 'Text');
                this.cancel(false);
                return;
            }
            if (id === 'paste') {
                const matrix = M.translation(...V.sub(p, t.params.origin));
                this.doc.transaction('Paste objects', () => { const ids = [], mapping = new Map(); for (const l of t.params.layers || []) {
                    let dest = this.doc.layers.find(x => x.name === l.name);
                    if (!dest) {
                        dest = { ...l, id: K.uid('layer') };
                        this.doc.layers.push(dest);
                    }
                    mapping.set(l.id, dest.id);
                } for (const original of t.params.entities) {
                    const e = G.transform(original, matrix);
                    delete e.id;
                    delete e.group;
                    e.layer = mapping.get(e.layer) || this.doc.currentLayer;
                    ids.push(this.addGeometry(e).id);
                } this.doc.selection = new Set(ids); });
                this.cancel(false);
                return;
            }
            if (id === 'line') {
                if (n && V.same(t.points.at(-1), p))
                    return;
                if (n)
                    this.create({ type: 'LINE', points: [t.points.at(-1).slice(), p.slice()] }, 'Line');
                t.points.push(p.slice());
            }
            else if (id === 'polyline' || id === 'spline') {
                if (!n || !V.same(t.points.at(-1), p))
                    t.points.push(p.slice());
            }
            else if (id === 'rectangle') {
                if (!n)
                    t.points.push(p.slice());
                else {
                    const a = t.points[0];
                    if (Math.abs(p[0] - a[0]) < EPS || Math.abs(p[1] - a[1]) < EPS)
                        throw Error('A rectangle needs a non-zero width and height.');
                    this.create({ type: 'POLYLINE', closed: true, points: [a, [p[0], a[1], a[2]], [p[0], p[1], a[2]], [a[0], p[1], a[2]]] }, 'Rectangle');
                    this.cancel(false);
                }
            }
            else if (id === 'circle') {
                if (!n)
                    t.points.push(p.slice());
                else {
                    const r = Math.hypot(p[0] - t.points[0][0], p[1] - t.points[0][1]);
                    if (r < EPS)
                        throw Error('Radius must be positive.');
                    this.create({ type: 'CIRCLE', center: t.points[0], radius: r }, 'Circle');
                    this.cancel(false);
                }
            }
            else if (id === 'arc') {
                if (n < 2)
                    t.points.push(p.slice());
                else {
                    this.create(G.arcThrough(t.points[0], t.points[1], p), 'Arc');
                    this.cancel(false);
                }
            }
            else if (id === 'ellipse') {
                if (n < 2) {
                    if (n === 1 && V.dist(t.points[0], p) < EPS)
                        throw Error('Major radius must be positive.');
                    t.points.push(p.slice());
                }
                else {
                    const ax = V.sub(t.points[1], t.points[0]), normal = V.norm([-ax[1], ax[0], 0]), r = Math.abs(V.dot(V.sub(p, t.points[0]), normal));
                    if (r < EPS)
                        throw Error('Minor radius must be positive.');
                    this.create({ type: 'ELLIPSE', center: t.points[0], axisX: ax, axisY: V.mul(normal, r), rx: V.len(ax), ry: r }, 'Ellipse');
                    this.cancel(false);
                }
            }
            else if (id === 'polygon') {
                if (!n)
                    t.points.push(p.slice());
                else {
                    const center = t.points[0], r = V.dist(center, p), a = Math.atan2(p[1] - center[1], p[0] - center[0]);
                    if (r < EPS)
                        throw Error('Radius must be positive.');
                    this.create({ type: 'POLYLINE', closed: true, points: Array.from({ length: t.params.sides }, (_, i) => V.add(center, [Math.cos(a + i / t.params.sides * TAU) * r, Math.sin(a + i / t.params.sides * TAU) * r, 0])) }, 'Polygon');
                    this.cancel(false);
                }
            }
            else if (id === 'dimension') {
                if (n < 2) {
                    if (n && V.dist(t.points[0], p) < EPS)
                        throw Error('Dimension endpoints must be distinct.');
                    t.points.push(p.slice());
                }
                else {
                    const normal = V.norm(V.cross([0, 0, 1], V.sub(t.points[1], t.points[0]))), offset = V.dot(V.sub(p, t.points[0]), normal);
                    this.create({ type: 'DIMENSION', points: t.points, offset, textHeight: this.defaults.dimensionHeight, precision: this.defaults.precision, layer: this.doc.layerMap.has('dimensions') ? 'dimensions' : this.doc.currentLayer }, 'Dimension');
                    this.cancel(false);
                }
            }
            else if (id === 'measure') {
                if (!n)
                    t.points.push(p.slice());
                else {
                    const a = t.points[0], delta = V.sub(p, a), distance = V.len(delta);
                    this.log('Distance', `${distance.toFixed(6)} ${this.doc.units} | ΔX ${delta[0].toFixed(4)}, ΔY ${delta[1].toFixed(4)}, ΔZ ${delta[2].toFixed(4)}`);
                    this.toast(`${distance.toFixed(3)} ${this.doc.units}`);
                    this.cancel(false);
                    this.measureResult = { a, b: p, distance };
                }
            }
            else if (['move', 'copy', 'rotate', 'scale', 'mirror'].includes(id)) {
                if (!n)
                    t.points.push(p.slice());
                else {
                    const mat = this.transformPreview(p);
                    if (!mat)
                        throw Error('The transformation is degenerate.');
                    this.doc.transaction(id[0].toUpperCase() + id.slice(1), () => this.doc.transform(t.selectedIds, mat, id === 'copy'));
                    this.log(id.toUpperCase(), t.selectedIds.length + ' object(s) transformed.');
                    this.cancel(false);
                }
            }
            this.lastPoint = p.slice();
            this.updateToolPrompt();
            this.updateDynamic();
            this.invalidate();
        }
        offsetSide(e, p) { if (e.center)
            return V.dist(e.center, p) > V.len(G.conicAxes(e).x) ? 1 : -1; if (e.type === 'POLYLINE' && e.closed)
            return inside(p, G.path(e)) ? -1 : 1; let nearest = null; for (const s of G.geometry(e).segments) {
            const hit = segmentDistance(p, s[0], s[1]);
            if (!nearest || hit.d < nearest.d)
                nearest = { ...hit, s };
        } if (!nearest)
            return 1; const d = V.sub(nearest.s[1], nearest.s[0]), v = V.sub(p, nearest.s[0]); return d[0] * v[1] - d[1] * v[0] >= 0 ? 1 : -1; }
        transformPreview(p) { const t = this.tool; if (!t?.points.length)
            return null; const base = t.points[0]; if (t.id === 'move' || t.id === 'copy')
            return M.translation(...V.sub(p, base)); if (t.id === 'rotate')
            return M.around(base, M.rotation(Math.atan2(p[1] - base[1], p[0] - base[0]))); if (t.id === 'scale') {
            const scale = V.dist(base, p) / (t.reference || 1);
            return scale > 1e-8 ? M.around(base, M.scale(scale)) : null;
        } if (t.id === 'mirror') {
            if (V.dist(base, p) < EPS)
                return null;
            const a = Math.atan2(p[1] - base[1], p[0] - base[0]);
            return M.around(base, M.multiply(M.rotation(a), M.multiply(M.scale(1, -1, 1), M.rotation(-a))));
        } return null; }
        trimExtend(entity, p, extend, cutters = []) {
            if (entity.type !== 'LINE')
                throw Error('This command edits LINE entities. Explode a polyline first.');
            const a = entity.points[0], b = entity.points[1], cuts = [];
            for (const e of this.doc.entities) {
                if (e.id === entity.id || !this.doc.visible(e) || cutters.length && !cutters.includes(e.id))
                    continue;
                for (const s of this.doc.geometry(e).segments) {
                    if (Math.abs(s[0][2] - a[2]) > 1e-5 || Math.abs(s[1][2] - a[2]) > 1e-5)
                        continue;
                    const hit = lineIntersection(a, b, ...s);
                    if (hit && hit.u >= -EPS && hit.u <= 1 + EPS)
                        cuts.push(hit.t);
                }
            }
            if (extend) {
                const first = V.dist(p, a) < V.dist(p, b), candidates = cuts.filter(t => first ? t < -EPS : t > 1 + EPS);
                if (!candidates.length)
                    throw Error('No boundary beyond that endpoint.');
                const t = first ? Math.max(...candidates) : Math.min(...candidates), point = V.lerp(a, b, t);
                this.doc.transaction('Extend line', () => this.doc.replace(entity.id, { ...entity, points: first ? [point, b] : [a, point] }));
            }
            else {
                const insideCuts = [...new Set(cuts.filter(t => t > EPS && t < 1 - EPS).map(t => Number(t.toFixed(10))))].sort((a, b) => a - b);
                if (!insideCuts.length)
                    throw Error('No cutting intersections on this line.');
                const t = segmentDistance(p, a, b).t, values = [0, ...insideCuts, 1];
                let low = 0, high = 1;
                for (let i = 0; i < values.length - 1; i++)
                    if (t >= values[i] - EPS && t <= values[i + 1] + EPS) {
                        low = values[i];
                        high = values[i + 1];
                        break;
                    }
                const pieces = [];
                if (low > EPS)
                    pieces.push([a, V.lerp(a, b, low)]);
                if (high < 1 - EPS)
                    pieces.push([V.lerp(a, b, high), b]);
                this.doc.transaction('Trim line', () => { this.doc.remove([entity.id]); for (const points of pieces) {
                    const e = { ...entity, points };
                    delete e.id;
                    this.doc.add(e);
                } });
            }
        }
        entityGrips(e) {
            const result = [];
            if (e.points && ['LINE', 'POLYLINE', 'HATCH', 'DIMENSION'].includes(e.type)) {
                e.points.forEach((p, i) => result.push({ kind: 'point', index: i, point: p }));
                if (e.type === 'LINE')
                    result.push({ kind: 'translate', point: V.lerp(...e.points, .5) });
                if (e.type === 'DIMENSION') {
                    const n = V.norm(V.cross([0, 0, 1], V.sub(e.points[1], e.points[0])));
                    result.push({ kind: 'dim-offset', point: V.add(V.lerp(...e.points, .5), V.mul(n, e.offset || 0)) });
                }
            }
            else if (e.type === 'SPLINE')
                (e.controlPoints || e.points).forEach((p, i) => result.push({ kind: 'control', index: i, point: p }));
            else if (e.center) {
                result.push({ kind: 'translate', point: e.center });
                if (e.type === 'ELLIPSE') {
                    const a = G.conicAxes(e);
                    result.push({ kind: 'axisX', point: V.add(e.center, a.x) }, { kind: 'axisY', point: V.add(e.center, a.y) });
                }
                else {
                    for (let i = 0; i < 4; i++)
                        result.push({ kind: 'radius', point: G.conicPoint(e, i * Math.PI / 2) });
                    if (e.type === 'ARC')
                        result.push({ kind: 'startAngle', point: G.conicPoint(e, e.startAngle) }, { kind: 'endAngle', point: G.conicPoint(e, e.endAngle) });
                }
            }
            else if (e.position)
                result.push({ kind: 'translate', point: e.position });
            else if (e.type === 'MESH')
                result.push({ kind: 'translate', point: centerOf([e]).center });
            return result.map(g => ({ ...g, e }));
        }
        gripAt(p) { if (this.doc.selection.size > 60)
            return null; for (const e of this.doc.selected(true))
            for (const grip of this.entityGrips(e)) {
                const q = this.camera.project(grip.point);
                if (Math.hypot(q[0] - p[0], q[1] - p[1]) <= 7)
                    return grip;
            } return null; }
        moveGrip(original, grip, p) { const e = K.clone(original); if (grip.kind === 'translate')
            return G.transform(e, M.translation(...V.sub(p, grip.point))); if (grip.kind === 'point')
            e.points[grip.index] = p.slice();
        else if (grip.kind === 'control')
            (e.controlPoints || e.points)[grip.index] = p.slice();
        else if (grip.kind === 'radius') {
            const r = Math.max(1e-7, V.dist(e.center, p)), ax = G.conicAxes(e);
            e.radius = r;
            e.axisX = V.mul(V.norm(ax.x), r);
            e.axisY = V.mul(V.norm(ax.y), r);
        }
        else if (grip.kind === 'axisX' || grip.kind === 'axisY') {
            const a = G.conicAxes(e), v = V.sub(p, e.center);
            if (V.len(v) < EPS)
                return e;
            if (grip.kind === 'axisX') {
                e.axisX = v;
                e.axisY = V.mul(V.norm(V.cross([0, 0, 1], v)), V.len(a.y));
            }
            else {
                e.axisY = v;
                e.axisX = V.mul(V.norm(V.cross(v, [0, 0, 1])), V.len(a.x));
            }
            e.rx = V.len(e.axisX);
            e.ry = V.len(e.axisY);
        }
        else if (grip.kind === 'startAngle' || grip.kind === 'endAngle') {
            const v = V.sub(p, e.center), a = G.conicAxes(e);
            e[grip.kind] = Math.atan2(V.dot(v, V.norm(a.y)), V.dot(v, V.norm(a.x)));
        }
        else if (grip.kind === 'dim-offset') {
            const n = V.norm(V.cross([0, 0, 1], V.sub(e.points[1], e.points[0])));
            e.offset = V.dot(V.sub(p, V.lerp(...e.points, .5)), n);
        } return e; }
        previewGeometry() {
            const t = this.tool, p = this.pointer.world;
            if (this.drag?.preview)
                return [this.drag.preview];
            if (!t)
                return [];
            const n = t.points.length;
            try {
                if (t.id === 'paste') {
                    const mat = M.translation(...V.sub(p, t.params.origin));
                    return t.params.entities.slice(0, 200).map(e => G.transform(e, mat));
                }
                if (['move', 'copy', 'rotate', 'scale', 'mirror'].includes(t.id) && n) {
                    const mat = this.transformPreview(p);
                    return mat ? t.selectedIds.slice(0, 300).map(id => this.doc.byId.get(id)).filter(Boolean).map(e => G.transform(e, mat)) : [];
                }
                if (t.id === 'offset' && t.source)
                    return [G.offset(t.source, t.params.distance * this.offsetSide(t.source, p))];
                if (!n)
                    return [];
                const a = t.points[0], last = t.points.at(-1);
                if (t.id === 'line' || t.id === 'measure')
                    return [{ type: 'LINE', points: [last, p] }];
                if (t.id === 'polyline')
                    return [{ type: 'POLYLINE', points: [...t.points, p] }];
                if (t.id === 'spline') {
                    const pts = [...t.points, p], degree = Math.min(3, pts.length - 1);
                    return [{ type: 'SPLINE', controlPoints: pts, degree, knots: G.uniformKnots(pts.length, degree) }, { type: 'POLYLINE', points: pts }];
                }
                if (t.id === 'rectangle')
                    return [{ type: 'POLYLINE', closed: true, points: [a, [p[0], a[1], a[2]], [p[0], p[1], a[2]], [a[0], p[1], a[2]]] }];
                if (t.id === 'circle')
                    return [{ type: 'CIRCLE', center: a, radius: Math.max(1e-6, V.dist(a, p)) }, { type: 'LINE', points: [a, p] }];
                if (t.id === 'polygon') {
                    const r = V.dist(a, p), theta = Math.atan2(p[1] - a[1], p[0] - a[0]);
                    return [{ type: 'POLYLINE', closed: true, points: Array.from({ length: t.params.sides }, (_, i) => V.add(a, [Math.cos(theta + i / t.params.sides * TAU) * r, Math.sin(theta + i / t.params.sides * TAU) * r, 0])) }];
                }
                if (t.id === 'arc')
                    return n < 2 ? [{ type: 'LINE', points: [a, p] }] : [G.arcThrough(a, t.points[1], p)];
                if (t.id === 'ellipse') {
                    if (n < 2)
                        return [{ type: 'LINE', points: [a, p] }];
                    const x = V.sub(t.points[1], a), normal = V.norm([-x[1], x[0], 0]), r = Math.max(.0001, Math.abs(V.dot(V.sub(p, a), normal)));
                    return [{ type: 'ELLIPSE', center: a, axisX: x, axisY: V.mul(normal, r) }];
                }
                if (t.id === 'dimension') {
                    if (n < 2)
                        return [{ type: 'LINE', points: [a, p] }];
                    const normal = V.norm(V.cross([0, 0, 1], V.sub(t.points[1], a)));
                    return [{ type: 'DIMENSION', points: t.points, offset: V.dot(V.sub(p, a), normal), textHeight: this.defaults.dimensionHeight, precision: this.defaults.precision }];
                }
            }
            catch { }
            return [];
        }
        updateDynamic() { const t = this.tool; if (!t || !this.pointer.inside || (!t.points.length && t.id !== 'paste')) {
            $('dynamic-input').hidden = true;
            return;
        } const base = t.points.at(-1) || t.params.origin, d = V.dist(base, this.pointer.world); let label = 'Distance', value = d.toFixed(3) + ' ' + this.doc.units; if (t.id === 'rotate') {
            label = 'Angle';
            value = (Math.atan2(this.pointer.world[1] - base[1], this.pointer.world[0] - base[0]) * 180 / Math.PI).toFixed(2) + '°';
        } if (t.id === 'scale') {
            label = 'Scale';
            value = (d / (t.reference || 1)).toFixed(3) + ' ×';
        } if (t.id === 'circle' || t.id === 'polygon')
            label = 'Radius'; $('dynamic-label').textContent = label; $('dynamic-value').textContent = value; $('dynamic-input').style.left = clamp(this.pointer.x + 19, 10, this.renderer.width - 165) + 'px'; $('dynamic-input').style.top = clamp(this.pointer.y + 19, 35, this.renderer.height - 80) + 'px'; $('dynamic-input').hidden = false; }
        drawOverlay(c) {
            const cam = this.camera, theme = this.sheet ? 'light' : this.theme, project = p => cam.project(p);
            c.save();
            c.setLineDash([]);
            if (this.sheet) {
                const w = this.renderer.width, h = this.renderer.height, pw = Math.min(w - 40, (h - 40) * Math.SQRT2), ph = pw / Math.SQRT2, x = (w - pw) / 2, y = (h - ph) / 2;
                c.strokeStyle = '#778d9e';
                c.lineWidth = 1;
                c.strokeRect(x, y, pw, ph);
                c.strokeRect(x + 8, y + 8, pw - 16, ph - 16);
                const tw = Math.min(260, pw * .4);
                c.strokeRect(x + pw - tw - 8, y + ph - 58, tw, 50);
                c.fillStyle = '#edf2f6';
                c.fillRect(x + pw - tw - 7, y + ph - 57, tw - 2, 48);
                c.fillStyle = '#304457';
                c.font = '11px "Segoe UI",sans-serif';
                c.fillText(this.doc.name, x + pw - tw + 1, y + ph - 37);
                c.font = '8px "Segoe UI",sans-serif';
                c.fillText('KESTREL CAD · A3 · FIT TO SHEET', x + pw - tw + 1, y + ph - 21);
            }
            const drawWire = (e, color, dashed = false, alpha = 1) => { const g = G.geometry(e, .5); c.strokeStyle = color; c.globalAlpha = alpha; c.lineWidth = 1.45; c.setLineDash(dashed ? [5, 4] : []); c.beginPath(); for (const s of g.segments.slice(0, 30000)) {
                const a = project(s[0]), b = project(s[1]);
                c.moveTo(a[0], a[1]);
                c.lineTo(b[0], b[1]);
            } c.stroke(); c.setLineDash([]); for (const text of g.texts) {
                if(text.composition){K.MText.draw(c,text,project,color,alpha,theme,true);continue;}
                const p = project(text.position), size = clamp((text.height || 10) * cam.zoom, 7, 200);
                c.font = `${size}px "Segoe UI",sans-serif`;
                c.fillStyle = color;
                c.textAlign = text.align || 'left';
                c.fillText(text.text || '', p[0], p[1]);
            } c.globalAlpha = 1; };
            if (this.hoverId && !this.doc.selection.has(this.hoverId)) {
                const e = this.doc.byId.get(this.hoverId);
                if (e)
                    drawWire(e, theme === 'light' ? '#328da8' : '#9fcedc', false, .65);
            }
            for (const e of this.previewGeometry())
                drawWire(e, theme === 'light' ? '#16869c' : '#8ce0e5', true);
            if (!this.tool && this.doc.selection.size <= 60) {
                for (const e of this.doc.selected(true))
                    for (const grip of this.entityGrips(e)) {
                        const p = project(grip.point);
                        if (p[2] < 0 || p[2] > 1)
                            continue;
                        c.fillStyle = theme === 'light' ? '#318da8' : '#172a3a';
                        c.strokeStyle = '#68d0e4';
                        c.lineWidth = 1.15;
                        c.fillRect(p[0] - 3, p[1] - 3, 6, 6);
                        c.strokeRect(p[0] - 3, p[1] - 3, 6, 6);
                    }
            }
            if (this.measureResult) {
                const { a, b, distance } = this.measureResult, p = project(a), q = project(b);
                c.strokeStyle = '#dfba78';
                c.setLineDash([4, 4]);
                c.beginPath();
                c.moveTo(...p.slice(0, 2));
                c.lineTo(...q.slice(0, 2));
                c.stroke();
                c.setLineDash([]);
                c.font = '11px Consolas,monospace';
                c.fillStyle = '#dfba78';
                c.textAlign = 'center';
                c.fillText(distance.toFixed(3) + ' ' + this.doc.units, (p[0] + q[0]) / 2, (p[1] + q[1]) / 2 - 10);
            }
            if (this.pointer.inside && !this.drag?.mode?.includes('pan') && this.navMode !== 'pan' && this.navMode !== 'orbit') {
                const x = this.pointer.x, y = this.pointer.y;
                c.strokeStyle = theme === 'light' ? '#4f6679' : '#a9b9c9';
                c.globalAlpha = .85;
                c.lineWidth = .7;
                c.beginPath();
                c.moveTo(x - 23, y);
                c.lineTo(x - 5, y);
                c.moveTo(x + 5, y);
                c.lineTo(x + 23, y);
                c.moveTo(x, y - 23);
                c.lineTo(x, y - 5);
                c.moveTo(x, y + 5);
                c.lineTo(x, y + 23);
                c.rect(x - 3, y - 3, 6, 6);
                c.stroke();
                c.globalAlpha = 1;
            }
            if (this.snapTarget && this.pointer.inside) {
                const p = project(this.snapTarget.point), s = 6;
                c.strokeStyle = theme === 'light' ? '#28956c' : '#7ad4a8';
                c.fillStyle = c.strokeStyle;
                c.lineWidth = 1.4;
                c.beginPath();
                if (this.snapTarget.type === 'center' || this.snapTarget.type === 'quadrant')
                    c.arc(p[0], p[1], s, 0, TAU);
                else if (this.snapTarget.type === 'midpoint') {
                    c.moveTo(p[0], p[1] - s);
                    c.lineTo(p[0] + s, p[1] + s);
                    c.lineTo(p[0] - s, p[1] + s);
                    c.closePath();
                }
                else if (this.snapTarget.type === 'intersection') {
                    c.moveTo(p[0] - s, p[1] - s);
                    c.lineTo(p[0] + s, p[1] + s);
                    c.moveTo(p[0] + s, p[1] - s);
                    c.lineTo(p[0] - s, p[1] + s);
                }
                else
                    c.rect(p[0] - s, p[1] - s, s * 2, s * 2);
                c.stroke();
                c.font = '10px "Segoe UI",sans-serif';
                c.textAlign = 'left';
                c.fillText(this.snapTarget.type, p[0] + 11, p[1] - 11);
            }
            c.restore();
        }
        // Command input deliberately parses numbers; no eval or executable drawing content.
        commandMatches(query) { const q = query.trim().toUpperCase(); if (!q)
            return []; return U.commands.filter(c => c.id.toUpperCase().includes(q) || c.label.toUpperCase().includes(q) || c.alias.toUpperCase().includes(q)).sort((a, b) => (this.aliases.get(q) === b.id ? 1 : 0) - (this.aliases.get(q) === a.id ? 1 : 0)).slice(0, 9); }
        commandSuggest() { const value = $('command-input').value.trim(), matches = this.tool || /[\d,@<>;]/.test(value) ? [] : this.commandMatches(value); this.suggestions = matches; this.suggestionIndex = clamp(this.suggestionIndex, 0, Math.max(0, matches.length - 1)); $('command-suggestions').innerHTML = matches.map((c, i) => `<div class="suggestion ${i === this.suggestionIndex ? 'active' : ''}" data-command="${c.id}"><strong>${esc(c.alias.split(' · ')[0])}</strong><span>${esc(c.label)}</span></div>`).join(''); $('command-suggestions').hidden = !matches.length; }
        async commandKey(event) { if (event.key === 'Enter') {
            event.preventDefault();
            event.stopPropagation();
            const value = $('command-input').value;
            $('command-input').value = '';
            $('command-suggestions').hidden = true;
            try {
                await this.executeCommand(value);
            }
            catch (error) {
                this.fail(error);
            }
            return;
        } if (event.key === 'Escape') {
            event.preventDefault();
            event.stopPropagation();
            $('command-input').value = '';
            $('command-suggestions').hidden = true;
            this.cancel();
            $('viewport').focus();
            return;
        } if (event.key === 'Tab' && !$('command-suggestions').hidden) {
            event.preventDefault();
            const c = this.suggestions[this.suggestionIndex];
            if (c) {
                $('command-input').value = c.alias.split(' · ')[0] + ' ';
                $('command-suggestions').hidden = true;
            }
            return;
        } if (event.key === 'ArrowUp' || event.key === 'ArrowDown') {
            event.preventDefault();
            if (!$('command-suggestions').hidden) {
                this.suggestionIndex = clamp(this.suggestionIndex + (event.key === 'ArrowUp' ? -1 : 1), 0, this.suggestions.length - 1);
                this.commandSuggest();
            }
            else {
                this.historyPosition = clamp(this.historyPosition + (event.key === 'ArrowUp' ? -1 : 1), 0, this.commandHistory.length);
                $('command-input').value = this.commandHistory[this.historyPosition] || '';
            }
        } }
        async executeCommand(input) { const line = String(input).trim(); if (!line) {
            this.finishTool();
            return;
        } this.commandHistory.push(line); if (this.commandHistory.length > 100)
            this.commandHistory.shift(); this.historyPosition = this.commandHistory.length; this.log('Command', line, 'input'); for (const part of line.split(';')) {
            const tokens = part.trim().split(/\s+/).filter(Boolean);
            for (const token of tokens) {
                const upper = token.toUpperCase();
                if (upper === 'ESC' || upper === 'CANCEL') {
                    this.cancel();
                    continue;
                }
                if (upper === 'ENTER') {
                    this.finishTool();
                    continue;
                }
                if (this.tool && ['C', 'CLOSE'].includes(upper) && this.tool.id === 'polyline') {
                    this.closePolyline();
                    continue;
                }
                if (this.tool && ['U', 'UNDO'].includes(upper) && this.tool.points.length) {
                    if (this.tool.id === 'line' && this.tool.points.length > 1)
                        this.doc.undo();
                    this.tool.points.pop();
                    this.updateToolPrompt();
                    continue;
                }
                if (this.tool && ['ALL'].includes(upper) && this.tool.stage === 'selection') {
                    await this.run('selectall');
                    continue;
                }
                if ((token.includes(',') || token.includes('<')) && this.tool) {
                    this.acceptPoint(this.parsePoint(token));
                    continue;
                }
                if (/^[+-]?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?$/i.test(token) && this.tool) {
                    this.acceptNumber(finite(token));
                    continue;
                }
                const id = this.aliases.get(upper);
                if (!id)
                    throw Error(`Unknown input “${token}”. Use X,Y,Z or @DX,DY,DZ for coordinates; Ctrl+K lists commands.`);
                await this.run(id);
            }
        } this.updateToolPrompt(); this.invalidate(); }
        parsePoint(input) { let text = input.trim(), relative = text.startsWith('@'); if (relative)
            text = text.slice(1); const base = this.tool?.points.at(-1) || this.lastPoint || [0, 0, 0]; if (text.includes('<')) {
            const parts = text.split('<');
            if (parts.length !== 2)
                throw Error('Polar coordinates use distance<angle or @distance<angle.');
            const d = finite(parts[0], 'Distance'), a = finite(parts[1], 'Angle') * Math.PI / 180, p = [d * Math.cos(a), d * Math.sin(a), 0];
            return relative ? V.add(base, p) : p;
        } const values = text.split(','); if (values.length < 2 || values.length > 3 || values.some(v => !v.trim()))
            throw Error('Coordinates use X,Y or X,Y,Z.'); const p = values.map(v => finite(v, 'Coordinate')); if (p.length === 2)
            p.push(relative ? 0 : base[2] || 0); return relative ? V.add(base, p) : p; }
        acceptNumber(value) { const t = this.tool; if (!t?.points.length)
            throw Error('Specify a base point first with X,Y or a viewport click.'); const a = t.points.at(-1); if (t.id === 'rotate') {
            this.doc.transaction('Rotate', () => this.doc.transform(t.selectedIds, M.around(a, M.rotation(value * Math.PI / 180))));
            this.cancel(false);
            return;
        } if (t.id === 'scale') {
            positive(value, 'Scale factor');
            this.doc.transaction('Scale', () => this.doc.transform(t.selectedIds, M.around(a, M.scale(value))));
            this.cancel(false);
            return;
        } if (t.id === 'dimension' && t.points.length === 2) {
            const normal = V.norm(V.cross([0, 0, 1], V.sub(t.points[1], t.points[0])));
            this.acceptPoint(V.add(t.points[0], V.mul(normal, value)));
            return;
        } if (['circle', 'polygon'].includes(t.id)) {
            positive(value, 'Radius');
            this.acceptPoint(V.add(a, [value, 0, 0]));
            return;
        } if (t.id === 'ellipse' && t.points.length === 2) {
            positive(value, 'Minor radius');
            const x = V.sub(t.points[1], t.points[0]);
            this.acceptPoint(V.add(t.points[0], V.mul(V.norm([-x[1], x[0], 0]), value)));
            return;
        } if (['line', 'polyline', 'move', 'copy', 'ellipse'].includes(t.id)) {
            positive(value, 'Distance');
            let direction = V.sub(this.pointer.world, a);
            if (V.len(direction) < EPS)
                direction = [1, 0, 0];
            let theta = Math.atan2(direction[1], direction[0]);
            if (this.settings.ortho)
                theta = Math.round(theta / (Math.PI / 2)) * Math.PI / 2;
            else if (this.settings.polar)
                theta = Math.round(theta / (this.settings.polarAngle * Math.PI / 180)) * this.settings.polarAngle * Math.PI / 180;
            this.acceptPoint(V.add(a, [value * Math.cos(theta), value * Math.sin(theta), 0]));
            return;
        } throw Error('Use coordinates for this point, rather than a scalar value.'); }
        keyDown(event) { this.shift = event.shiftKey; this.ctrl = event.ctrlKey || event.metaKey; const target = event.target, typing = target.matches('input,textarea,select,[contenteditable=true]'); if ($('modal').open || $('command-palette').open)
            return; if (event.key === 'Escape') {
            event.preventDefault();
            this.cancel();
            this.doc.selection.clear();
            this.selectionChanged();
            $('context-menu').hidden = true;
            return;
        } const invoke = id => { event.preventDefault(); this.run(id).catch(e => this.fail(e)); }; if (this.ctrl) {
            const key = event.key.toLowerCase();
            if (key === 'k') {
                invoke('palette');
                return;
            }
            if (typing)
                return;
            if (key === 's')
                invoke('save');
            else if (key === 'o')
                invoke('open');
            else if (key === 'n')
                invoke('new');
            else if (key === 'p')
                invoke('print');
            else if (key === 'z')
                invoke(event.shiftKey ? 'redo' : 'undo');
            else if (key === 'y')
                invoke('redo');
            else if (key === 'a')
                invoke('selectall');
            else if (key === 'c') {
                event.preventDefault();
                this.copyClipboard();
            }
            else if (key === 'x') {
                event.preventDefault();
                this.copyClipboard(true);
            }
            else if (key === 'v') {
                event.preventDefault();
                this.pasteClipboard();
            }
            return;
        } if (typing)
            return; const functions = { F1: 'help', F2: 'command-history', F3: 'osnap', F7: 'grid', F8: 'ortho', F9: 'snap', F10: 'polar' }; if (functions[event.key]) {
            invoke(functions[event.key]);
            return;
        } if (event.key === 'Delete' || event.key === 'Backspace') {
            invoke('erase');
            return;
        } if (event.key === 'Enter' || event.code === 'Space') {
            event.preventDefault();
            try {
                this.finishTool();
            }
            catch (e) {
                this.fail(e);
            }
            return;
        } if (event.key.length === 1 && !event.altKey) {
            $('command-input').focus();
            $('command-input').value = event.key;
            event.preventDefault();
            this.commandSuggest();
        } }
        openPalette() { this.paletteIndex = 0; $('palette-search').value = ''; this.renderPalette(); $('command-palette').showModal(); $('palette-search').focus(); }
        renderPalette() { const q = $('palette-search').value.toLowerCase(); this.paletteMatches = U.commands.filter(c => [c.label, c.alias, c.description].join(' ').toLowerCase().includes(q)); this.paletteIndex = clamp(this.paletteIndex, 0, Math.max(0, this.paletteMatches.length - 1)); $('palette-results').innerHTML = this.paletteMatches.map((c, i) => `<button type="button" class="palette-item ${i === this.paletteIndex ? 'active' : ''}" data-palette="${c.id}">${U.icon(c.icon)}<div><strong>${esc(c.label)}</strong><small>${esc(c.description)}</small></div><kbd>${esc(c.alias.split(' · ').at(-1))}</kbd></button>`).join('') || '<div class="empty-explorer">No matching commands.</div>'; $('palette-results').querySelector('.active')?.scrollIntoView({ block: 'nearest' }); }
        showContextMenu(x, y) { const ids = this.tool ? ['finish-tool', 'close-tool', 'cancel-tool'] : ['copy-clipboard', 'paste', 'move', 'erase', 'fit', 'palette']; const menu = $('context-menu'); menu.innerHTML = '<div class="popover-title">' + (this.tool ? esc(this.tool.id.toUpperCase()) : 'MODEL SPACE') + '</div>' + ids.map(id => `<button class="menu-item" data-action="${id}">${U.icon(({ 'finish-tool': 'check', 'cancel-tool': 'close', 'close-tool': 'polyline', 'copy-clipboard': 'copy', 'paste': 'insert' })[id] || U.get(id).icon)}${esc(({ 'finish-tool': 'Enter / finish', 'cancel-tool': 'Cancel command', 'close-tool': 'Close polyline', 'copy-clipboard': 'Copy to internal clipboard', 'paste': 'Paste objects' })[id] || U.get(id).label)}</button>`).join(''); menu.hidden = false; menu.style.left = clamp(x, 5, innerWidth - menu.offsetWidth - 5) + 'px'; menu.style.top = clamp(y, 5, innerHeight - menu.offsetHeight - 5) + 'px'; }
        copyClipboard(cut = false) { const selected = this.doc.selected(cut); if (!selected.length) {
            this.toast('Select objects to copy.', true);
            return;
        } this.clipboard = K.clone(selected); this.clipboardLayers = K.clone(this.doc.layers); this.clipboardUnits = this.doc.units; this.clipboardOrigin = centerOf(selected).min; this.toast(`${selected.length} object(s) copied to the in-app clipboard.`); if (cut)
            this.erase(); }
        pasteClipboard() { if (!this.clipboard.length) {
            this.toast('The in-app clipboard is empty. Select objects and press Ctrl+C first.', true);
            return;
        } const scale = UNIT_MM[this.clipboardUnits] / UNIT_MM[this.doc.units], entities = this.clipboard.map(e => G.transform(e, M.scale(scale))), origin = V.mul(this.clipboardOrigin, scale); this.startTool('paste', { entities, origin, layers: this.clipboardLayers }); }
        layerDialog() { const layers = this.doc.layers; this.dialog({ title: 'Layer manager', wide: true, html: `<p>Layer state and appearance apply immediately after saving. Create new layers with <strong>NEWLAYER</strong>.</p><table class="report-table"><thead><tr><th>CURRENT</th><th>LAYER NAME</th><th>COLOR</th><th>VISIBLE</th><th>LOCK</th><th>LINE TYPE</th><th>WIDTH mm</th></tr></thead><tbody>${layers.map((l, i) => `<tr><td><input type="radio" name="current" value="${i}" ${l.id === this.doc.currentLayer ? 'checked' : ''}></td><td><input type="text" name="name${i}" value="${esc(l.name)}" maxlength="255" required aria-label="Layer name ${i + 1}"></td><td><input type="color" name="color${i}" value="${l.color}" aria-label="Layer color ${i + 1}"></td><td><input type="checkbox" name="visible${i}" ${l.visible ? 'checked' : ''} aria-label="Visibility ${i + 1}"></td><td><input type="checkbox" name="locked${i}" ${l.locked ? 'checked' : ''} aria-label="Locked ${i + 1}"></td><td><select name="linetype${i}" aria-label="Line type ${i + 1}">${['Continuous', 'Dashed', 'Center'].map(v => `<option ${l.linetype === v ? 'selected' : ''}>${v}</option>`).join('')}</select></td><td><input type="number" name="weight${i}" value="${l.lineweight || .25}" min=".01" max="5" step=".01" style="width:65px" aria-label="Lineweight ${i + 1}"></td></tr>`).join('')}</tbody></table>`, submit: 'Apply layers', onSubmit: f => { const names = layers.map((l, i) => f['name' + i].trim()); if (names.some(n => !n) || new Set(names.map(n => n.toLowerCase())).size !== names.length)
                throw Error('Layer names must be non-empty and unique.'); this.doc.transaction('Edit layers', () => { this.doc.layers = layers.map((l, i) => ({ ...l, name: names[i], color: f['color' + i], visible: !!f['visible' + i], locked: !!f['locked' + i], linetype: f['linetype' + i], lineweight: finite(f['weight' + i], 'Lineweight', .01, 5) })); this.doc.currentLayer = layers[Number(f.current) || 0].id; }); } }); }
        newLayerDialog() { this.dialog({ title: 'Create layer', html: `<div class="form-grid">${this.field('name', 'Layer name', 'New layer', 'text', 'maxlength="255" required')}${this.field('color', 'Layer color', '#5ac6d2', 'color')}</div>`, submit: 'Create layer', onSubmit: f => { const name = f.name.trim(); if (!name)
                throw Error('A layer name is required.'); if (this.doc.layers.some(l => l.name.toLowerCase() === name.toLowerCase()))
                throw Error('A layer with this name already exists.'); this.doc.transaction('Create layer', () => { const l = this.doc.addLayer(name, f.color); this.doc.currentLayer = l.id; }); } }); }
        unitsDialog() { this.dialog({ title: 'Drawing units & precision', html: `<p>Coordinates are stored in drawing units. Changing metadata does not rescale existing geometry unless conversion is selected.</p><div class="form-grid"><div class="form-field"><label>Drawing units</label><select name="units">${Object.entries(UNIT_LABELS).map(([id, label]) => `<option value="${id}" ${this.doc.units === id ? 'selected' : ''}>${label}</option>`).join('')}</select></div>${this.field('precision', 'Dimension decimal places', this.defaults.precision, 'number', 'min="0" max="6"')}${this.field('spacing', 'Grid snap spacing', this.settings.snapSpacing, 'number', 'min="0.000001"')}${this.field('text', 'Text height', this.defaults.textHeight, 'number', 'min="0.000001"')}${this.field('dim', 'Dimension text height', this.defaults.dimensionHeight, 'number', 'min="0.000001"')}${this.field('polar', 'Polar tracking increment (degrees)', this.settings.polarAngle, 'number', 'min="1" max="90"')}<label class="form-check form-field full"><input type="checkbox" name="convert">Convert all existing coordinates to the new units</label></div>`, submit: 'Apply units', onSubmit: f => { const factor = f.convert ? UNIT_MM[this.doc.units] / UNIT_MM[f.units] : 1; const values = { snapSpacing: positive(f.spacing), textHeight: positive(f.text), dimensionHeight: positive(f.dim), precision: Math.round(finite(f.precision, 'Precision', 0, 6)), polarAngle: finite(f.polar, 'Polar angle', 1, 90) }; this.doc.transaction('Change drawing units', () => { K.Constraints?.convertUnits(this.doc,f.units,factor,!!f.convert); K.SpatialConstraints?.convertUnits(this.doc,f.units,factor,!!f.convert); if (factor !== 1)
                this.doc.entities = this.doc.entities.map(e => G.transform(e, M.scale(factor))); this.doc.units = f.units; }); Object.assign(this.defaults, { textHeight: values.textHeight, dimensionHeight: values.dimensionHeight, precision: values.precision }); Object.assign(this.settings, { snapSpacing: values.snapSpacing, polarAngle: values.polarAngle }); this.savePreferences(); if (factor !== 1)
                this.fit(); this.refresh(); } }); }
        filterDialog() { const types = [...new Set(this.doc.entities.map(e => e.type))].sort(); this.dialog({ title: 'Select by filter', html: `<div class="form-grid"><div class="form-field"><label>Entity type</label><select name="type"><option value="">All types</option>${types.map(t => `<option>${t}</option>`).join('')}</select></div><div class="form-field"><label>Layer</label><select name="layer"><option value="">All layers</option>${this.doc.layers.map(l => `<option value="${l.id}">${esc(l.name)}</option>`).join('')}</select></div><label class="form-check form-field full"><input type="checkbox" name="add">Add to the existing selection</label></div><div class="dialog-note">Hidden and locked entities are excluded.</div>`, submit: 'Select matching objects', onSubmit: f => { if (!f.add)
                this.doc.selection.clear(); for (const e of this.doc.entities)
                if (this.doc.editable(e) && (!f.type || f.type === e.type) && (!f.layer || f.layer === e.layer))
                    this.doc.selection.add(e.id); this.selectionChanged(); this.toast(this.doc.selection.size + ' object(s) selected.'); } }); }
        auditDialog() { let error = '', degenerate = 0; try {
            K.validateProject(K.clone(this.doc.serialize()));
        }
        catch (e) {
            error = e.message;
        } for (const e of this.doc.entities) {
            if (e.type === 'LINE' && V.same(...e.points))
                degenerate++;
            if (e.type === 'MESH' && Math.abs(G.volume(e)) < 1e-9)
                degenerate++;
        } this.dialog({ title: 'Drawing audit', html: `<div class="report-stat-grid"><div class="report-stat"><strong>${this.doc.entities.length.toLocaleString()}</strong><span>Entities</span></div><div class="report-stat"><strong>${error ? 1 : 0}</strong><span>Validation errors</span></div><div class="report-stat"><strong>${degenerate}</strong><span>Degenerate objects</span></div></div><div class="dialog-note ${error ? 'warning' : 'success'}"><strong>${error ? 'Validation failed' : 'Native structure is valid'}</strong>${esc(error || 'Identifiers, layer references, coordinates, supported entity fields and mesh face indices passed validation.')}</div><p style="margin-top:14px">This is a data-structure audit, not certification of mesh manifoldness, manufacturing tolerances, or CAD interoperability.</p>`, submit: 'Close', closeOnly: true }); }
        benchmarkDialog() { this.dialog({ title: 'Rendering stress scene', html: `<p>Generate a new drawing containing individually editable line entities. The diagnostics panel reports the actual active backend and CPU submission time, not an estimated frame rate.</p><div class="form-grid">${this.field('count', 'Line entity count', 10000, 'number', 'min="100" max="100000" step="100"')}</div><div class="dialog-note">Very large scenes also exercise JavaScript geometry creation, selection indexing and undo snapshots. They can be slower than GPU camera navigation.</div>`, submit: 'Generate scene', onSubmit: async (f) => { const count = Math.round(finite(f.count, 'Entity count', 100, 100000)); await new Promise(requestAnimationFrame); const start = performance.now(); this.addDocument(K.Examples.benchmark(count)); this.camera.setView('top'); this.renderer.style = 'wireframe'; this.settings.osnap = false; this.workspace = '2d'; this.fit(); this.log('Stress scene', count.toLocaleString() + ' entities created in ' + (performance.now() - start).toFixed(1) + ' ms; see diagnostics for rendering counters.'); } }); }
        diagnosticData() { const r = this.renderer; return { application: 'Kestrel CAD', version: '1.0.0', timestamp: new Date().toISOString(), backend: r.backend, adapter: r.adapterInfo || null, fallbackReason: r.fallbackReason || null, secureContext: window.isSecureContext, webgpuExposed: !!navigator.gpu, viewport: { width: r.width, height: r.height, devicePixelRatio: devicePixelRatio }, drawing: { name: this.doc.name, entities: this.doc.entities.length, layers: this.doc.layers.length, selected: this.doc.selection.size, units: this.doc.units }, render: { ...r.stats, style: r.style, antialiasing: r.device ? '4× MSAA + analytic line AA' : 'Canvas 2D', retainedGeometry: true, cpuTimeDescription: 'Scene processing and command submission on the CPU; not GPU execution time.' }, worker: !!this.worker, gpuErrors: r.gpuErrors.slice(-10), codec: this.capabilities || { status: 'Not checked' }, limits: { entities: 200000, historyEntries: 80 }, userAgent: navigator.userAgent }; }
        diagnosticsDialog() { const d = this.diagnosticData(), rows = [['Backend', d.backend], ['Adapter', JSON.stringify(d.adapter || 'Not exposed')], ['Viewport', d.viewport.width + ' × ' + d.viewport.height], ['Visible line segments', d.render.segments.toLocaleString()], ['Rendered triangles', d.render.triangles.toLocaleString()], ['GPU draw calls / frame', d.render.drawCalls], ['Last CPU processing / submission', d.render.cpuMs.toFixed(2) + ' ms'], ['DXF worker', d.worker ? 'Active' : 'Synchronous fallback'], ['GPU validation errors', d.gpuErrors.length], ['Secure context', d.secureContext ? 'Yes' : 'No']]; this.dialog({ title: 'Renderer diagnostics', html: `<span class="capability-badge">${esc(d.backend)}</span><table class="report-table" style="margin-top:15px">${rows.map(([a, b]) => `<tr><td>${esc(a)}</td><td>${esc(b)}</td></tr>`).join('')}</table><div class="dialog-note">Camera movement reuses retained geometry buffers. Line instances and triangles are batched by pipeline. CPU time is measured with performance.now(); it is not GPU time or an FPS benchmark.</div>${d.fallbackReason ? `<div class="dialog-note warning">${esc(d.fallbackReason)}</div>` : ''}<button type="button" class="button secondary" data-action="download-diagnostics" style="margin-top:15px">${U.icon('save')}Save diagnostics JSON</button>`, submit: 'Close', closeOnly: true }); }
        helpDialog() { const shortcuts = [['Line / Polyline / Circle', 'L / PL / C'], ['Rectangle / Arc / Spline', 'REC / A / SPL'], ['Move / Copy / Rotate', 'M / CO / RO'], ['Offset / Trim / Extend', 'O / TR / EX'], ['Dimension / Distance', 'DAL / DI'], ['Finish / Cancel', 'Enter / Esc'], ['Open / Save project', 'Ctrl+O / Ctrl+S'], ['Undo / Redo', 'Ctrl+Z / Ctrl+Y'], ['Select all / Delete', 'Ctrl+A / Delete'], ['Copy / Paste objects', 'Ctrl+C / Ctrl+V'], ['Command palette', 'Ctrl+K'], ['Grid / Grid snap', 'F7 / F9'], ['Ortho / Object snap', 'F8 / F3'], ['Pan / Orbit', 'Middle-drag / Shift+middle'], ['Zoom', 'Mouse wheel'], ['Window / Crossing', 'Drag left→right / right→left']]; this.dialog({ title: 'Welcome to Kestrel CAD', wide: true, html: `<p>An independent, local-first 2D drafting and mesh-modeling workbench, built with plain JavaScript and a custom WebGPU renderer.</p><div class="shortcut-grid">${shortcuts.map(([a, b]) => `<div class="shortcut-row"><span>${a}</span><kbd>${b}</kbd></div>`).join('')}</div><h3>Precision command input</h3><p>Type coordinates into the command line. <code>100,50</code> is absolute; <code>@25,0</code> is relative to the last point; <code>@100&lt;45</code> is a relative polar point. Enter completes a path, and <code>C</code> closes a polyline. Circle accepts a numeric radius after its center. Rotate and scale accept a numeric angle or factor after the base point.</p><div class="dialog-note"><code>LINE 0,0 100,0 @0,80 ENTER</code><br><code>REC 150,0 250,80</code><br><code>CIRCLE 200,40 20</code></div><h3>What is stored</h3><p><strong>.kcad</strong> preserves the editable native drawing, layers and view. Workspace recovery uses this browser’s local storage and can run out of space; exported project files are the portable backup. Native project files do not preserve undo history.</p><p><strong>DXF:</strong> native ASCII reader and writer for supported analytical entities, dimensions, simple hatches, expanded blocks and faceted meshes. Import reports skipped or approximated content. Meshes export as 3DFACE entities, not ACIS solids.</p><p><strong>DWG:</strong> optional local conversion bridge only. A separate installed codec is required; DWG is never faked by renaming a DXF file.</p><div class="dialog-note warning"><strong>Scope & limitations</strong>This is a substantial editable CAD implementation, not a complete replacement for a mature commercial CAD system. The Solids ribbon provides an optional local OpenCascade B-rep kernel; the Model ribbon remains mesh based. Persistent blocks, linked annotations, local references, UCS and scaled SVG layouts are in the Drafting ribbon. Planar geometric and dimensional constraints are available in the Parametric ribbon. ACIS SAT/SAB, 3D assembly constraints, complete SHX fidelity and arbitrary lossless DWG editing are not claimed. 3D Boolean operations use polygon meshes and can fail on degenerate inputs. UCS-aware drafting is available through the Drafting ribbon. A3 Sheet is a fitted print preview; LAYOUT and PLOT provide separate scaled SVG output.</div>`, submit: 'Start drawing', closeOnly: true }); }
        // File processing runs in a worker where available; standalone builds embed it.
        setupWorker() { try {
            let url = 'src/io-worker.js';
            if (root.KESTREL_WORKER_SOURCE)
                url = URL.createObjectURL(new Blob([root.KESTREL_WORKER_SOURCE], { type: 'text/javascript' }));
            else if (location.protocol === 'file:')
                return;
            this.worker = new Worker(url);
            this.worker.onmessage = event => { const { id, result, error } = event.data, pending = this.pendingIO.get(id); if (!pending)
                return; clearTimeout(pending.timer); this.pendingIO.delete(id); error ? pending.reject(Error(error)) : pending.resolve(result); };
            this.worker.onerror = () => { for (const p of this.pendingIO.values()) {
                clearTimeout(p.timer);
                p.reject(Error('The file worker could not finish. Retry the operation to use the synchronous fallback.'));
            } this.pendingIO.clear(); this.worker?.terminate(); this.worker = null; };
        }
        catch {
            this.worker = null;
        } }
        io(action, payload, name = 'Drawing') { if (!this.worker)
            return Promise.resolve().then(() => action === 'parse-dxf' ? K.Exchange.parseDXF(payload, name) : action === 'preserve-dxf' ? K.SourceDocument.exportPreserved(payload) : action === 'binary-dxf' ? K.SourceDocument.toBinary(K.Exchange.writeDXF(payload)) : K.Exchange.writeDXF(payload)); return new Promise((resolve, reject) => { const id = ++this.ioSequence, timer = setTimeout(() => { this.pendingIO.delete(id); reject(Error('File processing exceeded its 90-second safety limit.')); }, 90000); this.pendingIO.set(id, { resolve, reject, timer }); this.worker.postMessage({ id, action, payload, name }, payload instanceof ArrayBuffer ? [payload] : []); }); }
        filename(extension) { return (this.doc.name || 'Drawing').replace(/[<>:"/\\|?*\x00-\x1f]/g, '_').slice(0, 120) + '.' + extension; }
        download(content, name, type = 'application/octet-stream') { const blob = content instanceof Blob ? content : new Blob([content], { type }), url = URL.createObjectURL(blob), a = document.createElement('a'); a.href = url; a.download = name; document.body.append(a); a.click(); a.remove(); setTimeout(() => URL.revokeObjectURL(url), 30000); }
        saveNative() { this.doc.camera = this.camera.serialize(); this.download(JSON.stringify(this.doc.serialize(), null, 2), this.filename('kcad'), 'application/json'); this.doc.dirty = false; this.refreshTabs(); this.autosave(); this.toast('Editable project downloaded.'); }
        async exportDXF() { this.log('DXF', 'Writing supported entities to ASCII DXF…'); const text = await this.io('write-dxf', this.doc.serialize()); this.download(text, this.filename('dxf'), 'application/dxf'); this.log('DXF', 'Exported ' + this.doc.entities.length + ' native entities; meshes are faceted 3DFACEs.'); this.toast('DXF exported. Meshes are faceted; unsupported application metadata is not included.'); }
        async openFile(file, mode = 'open') { if (file.size > (/\.(kcad|json)$/i.test(file.name) ? 256 : 64) * 1024 * 1024)
            throw Error('File size exceeds the import limit (64 MiB exchange, 256 MiB archived native project).'); this.cancel(false); const ext = file.name.split('.').at(-1).toLowerCase(), name = file.name.replace(/\.[^.]+$/, ''), target = this.doc; this.log('Open', file.name + ' (' + (file.size / 1024).toFixed(1) + ' KiB)'); let data, report = null; if (ext === 'kcad' || ext === 'json') {
            data = JSON.parse(await file.text());
            K.validateProject(data);
        }
        else if (ext === 'dxf') {
            ({ data, report } = await this.io('parse-dxf', await file.arrayBuffer(), name));
        }
        else if (ext === 'dwg') {
            const caps = await this.getCapabilities();
            if (!caps.dwgRead) {
                await this.dwgDialog(caps);
                return;
            }
            const originalDWG = await file.arrayBuffer(), response = await this.convert('dwg-to-dxf', originalDWG);
            ({ data, report } = await this.io('parse-dxf', await response.arrayBuffer(), name));
            K.SourceDocument?.withOriginalDWG(data, originalDWG, file.name);
            report.warnings.unshift('DWG was converted to DXF by the local codec. The original DWG and converted DXF are retained separately; this does not establish lossless DWG editing.');
        }
        else
            throw Error('Open a .kcad project, ASCII/binary .dxf file, or .dwg file with the optional local codec.'); if (mode === 'insert') {
            if (data.sourceDocument && report) { report.sourceRetained = false; report.warnings.push('Insert imports editable content only. Open the source as its own drawing to retain its original bytes.'); }
            if (this.doc !== target)
                this.switchDocument(target.id);
            this.insertData(data);
        }
        else
            this.addDocument(K.Drawing.from(data)); this.fit(); this.saveSoon(); if (report) {
            this.lastImportReport = report;
            this.showImportReport(report);
        }
        else
            this.toast('Opened ' + file.name); return { data, report }; }
        insertData(data) {
            // Normalize and refresh in the source context before freezing any links.
            // The shared clipboard importer builds a complete ID map and transfers
            // definitions/styles before remapping fields, anchors and constraints.
            const source = K.Drawing.from(data), factor = UNIT_MM[source.units] / UNIT_MM[this.doc.units];
            const sourceData = source.serialize({ includeSource: false });
            let ids = [];
            this.doc.transaction('Insert drawing', () => {
                ids = K.Production.copyInto(this.doc, sourceData, sourceData.entities, M.scale(factor));
                this.doc.selection = new Set(ids);
            });
            this.log('Insert', ids.length + ' entities inserted at their original coordinates; unit conversion ×' + factor + '. Use Move to place them.');
            if (this.doc.operationWarnings?.length) {
                this.log('Insert', this.doc.operationWarnings.join(' '));
                this.toast(this.doc.operationWarnings.join(' '));
            }
            return ids;
        }
        showImportReport(report) { const skipped = Object.values(report.skipped || {}).reduce((a, b) => a + b, 0), warnings = report.warnings || []; this.log('Import', `${report.created} created · ${skipped} skipped · ${warnings.length} warning(s)`); this.dialog({ title: 'DXF import report', html: `<div class="report-stat-grid"><div class="report-stat"><strong>${report.read.toLocaleString()}</strong><span>Records read</span></div><div class="report-stat"><strong>${report.created.toLocaleString()}</strong><span>Entities created</span></div><div class="report-stat"><strong>${skipped.toLocaleString()}</strong><span>Skipped</span></div></div>${skipped ? `<table class="report-table">${Object.entries(report.skipped).map(([name, count]) => `<tr><td>${esc(name)}</td><td>${count}</td></tr>`).join('')}</table>` : ''}${warnings.length ? warnings.slice(0, 30).map(w => `<div class="dialog-note warning">${esc(w)}</div>`).join('') : '<div class="dialog-note success"><strong>Supported entity data imported</strong>No parser warnings were generated. Always compare critical drawings against the originating CAD application.</div>'}`, submit: 'View drawing', closeOnly: true }); }
        svgDialog() { this.dialog({ title: 'Export vector SVG', html: `<p>Export the visible drawing using a fitted top view or the current 3D projection.</p><div class="form-grid"><div class="form-field"><label>Projection</label><select name="projection"><option value="top">Top / world XY</option><option value="current">Current view</option></select></div>${this.field('width', 'Output width (pixels)', 1600, 'number', 'min="200" max="20000"')}${this.field('height', 'Output height (pixels)', 1131, 'number', 'min="200" max="20000"')}<label class="form-check"><input type="checkbox" name="monochrome" checked>Monochrome</label><label class="form-check"><input type="checkbox" name="sheet" checked>Include sheet border</label></div>`, submit: 'Export SVG', onSubmit: f => { const svg = K.Exchange.writeSVG(this.doc, { width: finite(f.width, 'Width', 200, 20000), height: finite(f.height, 'Height', 200, 20000), camera: f.projection === 'current' ? this.camera : null, monochrome: !!f.monochrome, sheet: !!f.sheet }); this.download(svg, this.filename('svg'), 'image/svg+xml'); this.toast('Vector drawing exported.'); } }); }
        exportPNG() { this.pointer.inside = false; requestAnimationFrame(() => { this.renderer.render(this.doc); const canvas = this.renderer.screenshot(); canvas.toBlob(blob => { if (!blob) {
            this.fail(Error('Image export failed.'));
            return;
        } this.download(blob, this.filename('png'), 'image/png'); this.toast('Viewport PNG exported.'); }, 'image/png'); }); }
        printDialog() { this.dialog({ title: 'Print / plot', html: `<p>Create a fitted A3 landscape sheet. The browser print dialog can print it or save it as a PDF.</p><div class="form-grid"><div class="form-field"><label>Projection</label><select name="projection"><option value="top">Top / world XY</option><option value="current">Current view</option></select></div><label class="form-check"><input name="monochrome" type="checkbox" checked>Monochrome</label></div><div class="dialog-note">This is fit-to-page output, not an engineering-scale plot. Use DXF for dimensionally accurate downstream CAD plotting.</div>`, submit: 'Open print sheet', onSubmit: f => { const popup = window.open('', '_blank'); if (!popup)
                throw Error('Allow popups for this local application to open its print sheet.'); const svg = K.Exchange.writeSVG(this.doc, { width: 1680, height: 1188, camera: f.projection === 'current' ? this.camera : null, monochrome: !!f.monochrome, sheet: true }); popup.document.write('<!doctype html><html><head><title>' + esc(this.doc.name) + ' — Kestrel CAD</title><style>@page{size:A3 landscape;margin:0}body{margin:0;background:#ddd}svg{display:block;width:100vw;height:100vh;background:white}button{position:fixed;top:12px;right:12px;padding:10px 20px;cursor:pointer}@media print{button{display:none}body{background:white}svg{width:420mm;height:297mm}}</style></head><body>' + svg + '<button onclick="window.print()">Print / Save PDF</button></body></html>'); popup.document.close(); } }); }
        async getCapabilities() { if (!['http:', 'https:'].includes(location.protocol) || !['localhost', '127.0.0.1', '[::1]'].includes(location.hostname))
            return this.capabilities = { dwgRead: false, dwgWrite: false, bridge: false, reason: 'Run the included Python server on localhost to use the optional DWG bridge.' }; try {
            const response = await fetch('/api/capabilities', { signal: AbortSignal.timeout(4000) });
            if (!response.ok)
                throw Error('Bridge not found');
            const data = await response.json();
            if (data.application !== 'Kestrel CAD bridge')
                throw Error('Not the Kestrel server');
            return this.capabilities = { ...data, bridge: true };
        }
        catch {
            return this.capabilities = { dwgRead: false, dwgWrite: false, bridge: false, reason: 'The Kestrel conversion bridge is not running. Start tools/serve.py instead of a generic static server.' };
        } }
        async convert(direction, bytes) { const response = await fetch('/api/convert/' + direction, { method: 'POST', headers: { 'Content-Type': 'application/octet-stream', 'X-Kestrel-Client': '1' }, body: bytes, signal: AbortSignal.timeout(70000) }); if (!response.ok) {
            let reason = 'Conversion failed (HTTP ' + response.status + ').';
            try {
                reason = (await response.json()).error || reason;
            }
            catch { }
            throw Error(reason);
        } return response; }
        async exportDWG() { const caps = await this.getCapabilities(); if (!caps.dwgWrite) {
            await this.dwgDialog(caps);
            return;
        } this.dialog({ title: 'Export DWG through local codec', html: `<p>The installed <strong>dxf2dwg</strong> converter will encode an ASCII DXF export as DWG. Meshes remain faceted entities; they do not become ACIS solids.</p><div class="dialog-note warning">Codec writing is experimental in some distributions. This is not a lossless DWG round trip. Verify the result in the destination application.</div>`, submit: 'Convert and export DWG', onSubmit: async () => { const text = await this.io('write-dxf', this.doc.serialize()), response = await this.convert('dxf-to-dwg', text); const blob = await response.blob(); this.download(blob, this.filename('dwg')); this.toast('DWG conversion completed by the local codec.'); } }); }
        async dwgDialog(existing = null) { const caps = existing || await this.getCapabilities(); this.dialog({ title: 'DWG codec status', html: `<table class="report-table"><tr><td>Local bridge</td><td>${caps.bridge ? 'Connected' : 'Not connected'}</td></tr><tr><td>DWG → DXF</td><td>${caps.dwgRead ? 'Converter detected' : 'Not installed'}</td></tr><tr><td>DXF → DWG</td><td>${caps.dwgWrite ? 'Converter detected' : 'Not installed'}</td></tr></table><div class="dialog-note ${caps.dwgRead ? 'success' : 'warning'}"><strong>${caps.dwgRead ? 'DWG import converter is available' : 'Native DWG codec is not bundled'}</strong>${esc(caps.reason || 'The bridge invokes separately installed GNU LibreDWG command-line converters. Detection does not certify every DWG version or entity type.')}</div><h3>Local setup</h3><p>From the application folder run <code>python3 tools/serve.py</code>, then open <code>http://localhost:8000</code>. Install GNU LibreDWG separately and ensure <code>dwg2dxf</code> and, for export, <code>dxf2dwg</code> are on PATH. Read the included README before enabling conversion.</p><p>All conversion is local. No drawing is uploaded to a remote conversion service. Without a codec, use DXF exchange or the native .kcad format.</p><div class="dialog-note">DWG version and object support depend on the installed codec and this application's DXF subset. Proprietary extension objects, ACIS solids and application metadata are not preserved.</div>`, submit: 'Close', closeOnly: true }); }
    }
    K.App = App;
    K.installProductionUI?.(App);
    K.installAdvancedUI?.(App);
    K.installKernelUI?.(App); K.installConstraintsUI?.(App); K.installDynamicUI?.(App);
    K.installFontsUI?.(App); K.installSourceUI?.(App); K.installProductivityUI?.(App); K.installMTextUI?.(App); K.installSpatialUI?.(App); K.installAcisUI?.(App); K.installNativeAnalysisUI?.(App);
    K.installNativeCurvesUI?.(App); K.installFieldsUI?.(App); K.installTextSearchUI?.(App);
    const app = new App();
    app.init().catch(error => { console.error(error); document.documentElement.dataset.ready = 'error'; const log = $('command-history'); if (log) {
        const row = document.createElement('div');
        row.className = 'history-error';
        row.textContent = 'Startup failed: ' + error.message;
        log.append(row);
    } const badge = $('engine-label'); if (badge)
        badge.textContent = 'Startup error — see command history'; });
})(window);
