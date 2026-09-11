/* Kestrel CAD — drawing database, validated persistence and atomic history. */
(function (root) {
    'use strict';
    const K = root.Kestrel, { V, M, EPS } = K.Math;
    const clone = v => JSON.parse(JSON.stringify(v));
    let sequence = 0;
    function uid(prefix = 'e') { return prefix + '_' + Date.now().toString(36) + '_' + (++sequence).toString(36); }
    const defaultLayers = () => [
        { id: '0', name: '0', color: '#dce4ed', visible: true, locked: false, linetype: 'Continuous', lineweight: .25 },
        { id: 'architecture', name: 'A-WALL', color: '#dee6ed', visible: true, locked: false, linetype: 'Continuous', lineweight: .35 },
        { id: 'openings', name: 'A-OPENING', color: '#59c8d9', visible: true, locked: false, linetype: 'Continuous', lineweight: .20 },
        { id: 'furniture', name: 'A-FURNITURE', color: '#a4b5c7', visible: true, locked: false, linetype: 'Continuous', lineweight: .18 },
        { id: 'dimensions', name: 'A-DIMENSION', color: '#65c4b6', visible: true, locked: false, linetype: 'Continuous', lineweight: .15 },
        { id: 'annotation', name: 'A-ANNOTATION', color: '#c2cbd5', visible: true, locked: false, linetype: 'Continuous', lineweight: .18 },
        { id: 'hatch', name: 'A-HATCH', color: '#687d91', visible: true, locked: false, linetype: 'Continuous', lineweight: .13 },
        { id: 'construction', name: 'A-CENTER', color: '#c69a66', visible: true, locked: false, linetype: 'Center', lineweight: .13 }
    ];
    const TYPES = new Set(['LINE', 'POLYLINE', 'CIRCLE', 'ARC', 'ELLIPSE', 'SPLINE', 'HATCH', 'POINT', 'TEXT', 'MTEXT', 'DIMENSION', 'MESH', 'INSERT', 'TABLE', 'LEADER']);
    function validate(data, definition = false) {
        if (!data || typeof data !== 'object' || data.format !== 'kestrel-cad' || ![1, 2].includes(data.version))
            throw Error('This is not a supported Kestrel CAD project (version 1 or 2).');
        if (!Array.isArray(data.entities) || data.entities.length > 200000)
            throw Error('Project entity limit exceeded (200,000).');
        if (!Array.isArray(data.layers) || !data.layers.length || data.layers.length > 2048)
            throw Error('Invalid layer table.');
        if (data.name != null && (typeof data.name !== 'string' || data.name.length > 512))
            throw Error('Invalid drawing name.');
        const ids = new Set(), layerIds = new Set();
        for (const l of data.layers) {
            if (typeof l.id !== 'string' || !/^[-a-zA-Z0-9_.:]{1,128}$/.test(l.id) || layerIds.has(l.id))
                throw Error('Invalid or duplicate layer identifier.');
            layerIds.add(l.id);
            if (typeof l.name !== 'string' || l.name.length > 255)
                throw Error('Invalid layer name.');
            if (!/^#[0-9a-f]{6}$/i.test(l.color))
                l.color = '#dce4ed';
            l.visible = l.visible !== false;
            l.locked = !!l.locked;
            l.lineweight = Number.isFinite(l.lineweight) ? Math.max(.01, Math.min(5, l.lineweight)) : .25;
        }
        if (!definition && K.Production) K.Production.validate(data);
        let vertices = 0;
        const point = p => Array.isArray(p) && p.length >= 2 && p.length <= 3 && p.every(x => Number.isFinite(x) && Math.abs(x) <= 1e12);
        for (const e of data.entities) {
            if (K.Production) K.Production.validateEntity(e);
            if (!TYPES.has(e.type))
                throw Error('Unsupported native entity: ' + e.type);
            if (!e.id)
                e.id = uid();
            if (typeof e.id !== 'string' || !/^[-a-zA-Z0-9_.:]{1,128}$/.test(e.id) || ids.has(e.id))
                throw Error('Invalid or duplicate entity identifier.');
            ids.add(e.id);
            if (!layerIds.has(e.layer))
                e.layer = data.layers[0].id;
            for (const key of ['points', 'vertices', 'controlPoints'])
                if (e[key]) {
                    if (!Array.isArray(e[key]) || !e[key].every(point))
                        throw Error('Invalid coordinates in ' + e.type);
                    vertices += e[key].length;
                    for (const p of e[key])
                        if (p.length === 2)
                            p.push(0);
                }
            for (const key of ['position', 'center', 'axisX', 'axisY', 'normal', 'direction'])
                if (e[key]) {
                    if (!point(e[key]))
                        throw Error('Invalid ' + key + ' in ' + e.type);
                    if (e[key].length === 2)
                        e[key].push(0);
                }
            for (const key of ['radius', 'rx', 'ry', 'height', 'textHeight', 'spacing'])
                if (e[key] != null && (!Number.isFinite(e[key]) || e[key] <= 0))
                    throw Error('Invalid ' + key + ' in ' + e.type);
            for (const key of ['rotation', 'startAngle', 'endAngle', 'offset', 'angle'])
                if (e[key] != null && !Number.isFinite(e[key]))
                    throw Error('Invalid ' + key + ' in ' + e.type);
            if (e.type === 'MESH') {
                if (!Array.isArray(e.faces) || !e.vertices || e.faces.length > 2000000)
                    throw Error('Invalid mesh.');
                for (const f of e.faces)
                    if (!Array.isArray(f) || f.length < 3 || f.length > 10000 || !f.every(i => Number.isInteger(i) && i >= 0 && i < e.vertices.length))
                        throw Error('Invalid mesh face.');
            }
            if (['LINE', 'POLYLINE', 'DIMENSION', 'HATCH'].includes(e.type) && (!e.points || e.points.length < (e.type === 'HATCH' ? 3 : 2)))
                throw Error('Too few points in ' + e.type);
            if (['CIRCLE', 'ARC', 'ELLIPSE'].includes(e.type) && !e.center)
                throw Error('Missing curve center.');
            if (e.type === 'SPLINE') {
                const count = (e.controlPoints || e.points)?.length || 0;
                if (count < 2)
                    throw Error('Too few spline control points.');
                if (e.degree != null && (!Number.isInteger(e.degree) || e.degree < 1 || e.degree > Math.min(10, count - 1)))
                    throw Error('Invalid spline degree.');
                if (e.knots && (!Array.isArray(e.knots) || !e.knots.every((v, i) => Number.isFinite(v) && (!i || v >= e.knots[i - 1]))))
                    throw Error('Invalid spline knot sequence.');
                if (e.weights && (!Array.isArray(e.weights) || e.weights.length !== count || !e.weights.every(v => Number.isFinite(v) && v > 0)))
                    throw Error('Invalid spline weights.');
            }
            if (['TEXT', 'MTEXT', 'POINT'].includes(e.type) && !e.position)
                throw Error('Missing insertion point.');
            if (['TEXT','MTEXT'].includes(e.type) && (typeof e.text !== 'string' || e.text.length > 100000))
                throw Error('Invalid text entity.');
            if (e.color && e.color !== 'bylayer' && e.color !== 'byblock' && !/^#[0-9a-f]{6}$/i.test(e.color))
                e.color = 'bylayer';
        }
        if (data.camera) {
            const c = data.camera;
            if (!point(c.target) || !Number.isFinite(c.zoom) || c.zoom <= 0 || !Number.isFinite(c.yaw) || !Number.isFinite(c.pitch))
                throw Error('Invalid saved camera.');
        }
        if (!definition && K.Constraints) K.Constraints.validate(data);
        if (!definition && K.SpatialConstraints) K.SpatialConstraints.validate(data);
        if (!definition && data.sourceDocument) { if (!K.SourceDocument) throw Error('Source archive support is not loaded.'); K.SourceDocument.validate(data.sourceDocument); }
        if (vertices > 3000000)
            throw Error('Project vertex limit exceeded (3 million).');
        if (!['mm', 'cm', 'm', 'in', 'ft', 'unitless'].includes(data.units))
            data.units = 'mm';
        return data;
    }
    class Drawing {
        constructor(name = 'Untitled') { this.id = uid('doc'); this.name = name; this.entities = []; this.layers = defaultLayers(); this.units = 'mm'; this.currentLayer = 'architecture'; this.selection = new Set(); this.revision = 0; this.undoStack = []; this.redoStack = []; this.dirty = false; this.camera = null; this.sourceDocument = null; this.onChange = () => { }; this.cache = new WeakMap(); this.reindex(); }
        reindex() { if (K.Production) K.Production.bind(this); this.byId = new Map(this.entities.map(e => [e.id, e])); this.layerMap = new Map(this.layers.map(l => [l.id, l])); if (!this.layerMap.has(this.currentLayer))
            this.currentLayer = this.layers[0].id; this.selection = new Set([...this.selection].filter(id => this.byId.has(id))); }
        layer(e) { return this.layerMap.get(typeof e === 'string' ? e : e.layer) || this.layers[0]; }
        visible(e) { return this.layer(e).visible && !e.hidden; }
        editable(e) { return this.visible(e) && !this.layer(e).locked; }
        selected(editable = false) { return [...this.selection].map(id => this.byId.get(id)).filter(e => e && (!editable || this.editable(e))); }
        entity(type, props = {}) { return { id: uid(), type, layer: this.currentLayer, color: 'bylayer', linetype: 'ByLayer', ...props }; }
        add(type, props = {}) { const e = typeof type === 'object' ? { id: uid(), layer: this.currentLayer, color: 'bylayer', linetype: 'ByLayer', ...type } : this.entity(type, props); this.entities.push(e); this.byId.set(e.id, e); return e; }
        remove(ids) { const s = new Set(ids); K.Constraints?.removeReferences(this, [...s]); K.SpatialConstraints?.removeReferences(this,[...s]); this.entities = this.entities.filter(e => !s.has(e.id)); for (const id of s)
            this.selection.delete(id); this.reindex(); }
        replace(id, e) { const i = this.entities.findIndex(x => x.id === id); if (i >= 0) {
            this.entities[i] = { ...e, id };
            this.byId.set(id, this.entities[i]);
        } }
        geometry(e) { let g = this.cache.get(e); if (!g) {
            g = K.Geo.geometry(e, .25);
            this.cache.set(e, g);
        } return g; }
        serialize(options = {}) { return { ...(options.includeSource !== false && this.sourceDocument ? { sourceDocument: this.sourceDocument } : {}), format: 'kestrel-cad', version: 2, production: K.clone(this.production || K.Production?.defaults() || null), name: this.name, units: this.units, currentLayer: this.currentLayer, layers: clone(this.layers), entities: clone(this.entities), camera: this.camera }; }
        snapshot() { return JSON.stringify(this.serialize({ includeSource: false })); }
        apply(data, options = {}) { const { sourceDocument, ...drawingData } = data; if (sourceDocument) { if (!K.SourceDocument) throw Error('Source archive support is not loaded.'); K.SourceDocument.validate(sourceDocument); } const d = validate(clone(drawingData)); if (sourceDocument || !options.retainSource) this.sourceDocument = sourceDocument || null; delete this.constraintReport; delete this.spatialReport; this.production = d.production || K.Production?.defaults() || null; this.name = d.name || 'Untitled'; this.units = d.units; this.entities = d.entities; this.layers = d.layers; this.currentLayer = d.currentLayer || this.layers[0].id; this.camera = d.camera; this.cache = new WeakMap(); this.reindex(); K.Fields?.refresh(this); }
        transaction(label, fn) { const before = this.snapshot(); try {
            fn();
            this.reindex();
            if (K.Constraints) K.Constraints.enforce(this);
            if (K.SpatialConstraints) K.SpatialConstraints.enforce(this);
            if (K.Production) { K.Production.associations(this); K.Fields?.refresh(this, before); K.validateProject(this.serialize()); }
            const after = this.snapshot();
            if (before === after)
                return false;
            this.undoStack.push({ label, state: before });
            while (this.undoStack.length > 80 || this.undoStack.reduce((n, x) => n + x.state.length, 0) > 32e6 && this.undoStack.length > 1)
                this.undoStack.shift();
            this.redoStack = [];
            this.changed(label);
            return true;
        }
        catch (error) {
            this.apply(JSON.parse(before), { retainSource: true });
            throw error;
        } }
        changed(label = 'Edit') { this.cache = new WeakMap(); this.revision++; this.dirty = true; this.onChange(label); }
        undo() { const item = this.undoStack.pop(); if (!item)
            return null; this.redoStack.push({ label: item.label, state: this.snapshot() }); this.apply(JSON.parse(item.state), { retainSource: true }); this.changed('Undo ' + item.label); return item.label; }
        redo() { const item = this.redoStack.pop(); if (!item)
            return null; this.undoStack.push({ label: item.label, state: this.snapshot() }); this.apply(JSON.parse(item.state), { retainSource: true }); this.changed('Redo ' + item.label); return item.label; }
        transform(ids, m, copy = false) { const result = [], copied = new Map(); for (const id of ids) {
            const e = this.byId.get(id);
            if (!e || !this.editable(e))
                continue;
            const t = K.Geo.transform(e, m);
            if (copy) {
                t.id = uid();
                this.add(t); copied.set(id, t.id);
            }
            else
                this.replace(id, t);
            result.push(t.id);
        } if (copy && K.Fields) { for (const id of result) this.replace(id, K.Fields.remap(this.byId.get(id), copied, true)); } this.selection = new Set(result); return result; }
        addLayer(name, color = '#70b8d3') { if (!name?.trim())
            throw Error('A layer name is required.'); if (this.layers.some(l => l.name.toLowerCase() === name.trim().toLowerCase()))
            throw Error('Layer names must be unique.'); const l = { id: uid('layer'), name: name.trim(), color, visible: true, locked: false, linetype: 'Continuous', lineweight: .25 }; this.layers.push(l); this.reindex(); return l; }
        static from(data) { const d = new Drawing(); d.apply(data); return d; }
    }
    class SpatialIndex {
        constructor(cell = 80) { this.cell = cell; this.buckets = new Map(); this.large = []; this.items = new Map(); this.key = ''; }
        build(drawing, camera, style = 'wireframe') { const key = drawing.id + ':' + drawing.revision + ':' + camera.revision + ':' + style; if (key === this.key)
            return; this.key = key; this.buckets.clear(); this.large = []; this.items.clear(); for (const e of drawing.entities) {
            if (!drawing.visible(e))
                continue;
            const g = drawing.geometry(e), segments = (e.type === 'MESH' && style === 'wireframe' ? g.wireSegments || g.segments : g.segments).map(s => s.map(p => camera.project(p))), faces = style === 'wireframe' ? [] : g.triangles.map(t => t.points.map(p => camera.project(p))), pts = g.points.map(p => camera.project(p));
            if (!pts.length)
                continue;
            let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
            for (const p of pts) {
                minX = Math.min(minX, p[0]);
                minY = Math.min(minY, p[1]);
                maxX = Math.max(maxX, p[0]);
                maxY = Math.max(maxY, p[1]);
            }
            const item = { e, segments, faces, g, bounds: [minX, minY, maxX, maxY] };
            this.items.set(e.id, item);
            const x0 = Math.floor(minX / this.cell), x1 = Math.floor(maxX / this.cell), y0 = Math.floor(minY / this.cell), y1 = Math.floor(maxY / this.cell);
            if ((x1 - x0 + 1) * (y1 - y0 + 1) > 256) {
                this.large.push(item);
                continue;
            }
            for (let x = x0; x <= x1; x++)
                for (let y = y0; y <= y1; y++) {
                    const k = x + ',' + y;
                    if (!this.buckets.has(k))
                        this.buckets.set(k, []);
                    this.buckets.get(k).push(item);
                }
        } }
        near(x, y, r = 12) { const set = new Set(this.large); for (let i = Math.floor((x - r) / this.cell); i <= Math.floor((x + r) / this.cell); i++)
            for (let j = Math.floor((y - r) / this.cell); j <= Math.floor((y + r) / this.cell); j++)
                for (const item of this.buckets.get(i + ',' + j) || [])
                    set.add(item); return [...set].filter(({ bounds: b }) => x >= b[0] - r && x <= b[2] + r && y >= b[1] - r && y <= b[3] + r); }
    }
    K.uid = uid;
    K.clone = clone;
    K.Drawing = Drawing;
    K.SpatialIndex = SpatialIndex;
    K.validateProject = validate;
})(typeof window !== 'undefined' ? window : globalThis);
