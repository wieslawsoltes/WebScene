/* Kestrel CAD — local ASCII DXF and SVG exchange.
 * Unsupported entities are reported, never silently presented as supported. */
(function (root) {
    'use strict';
    const K = root.Kestrel, { V, M, basis, TAU, EPS, angle, faceNormal } = K.Math, G = K.Geo;
    const ACI = ['#000000', '#ef6565', '#e3c55e', '#74c58b', '#5acbd4', '#698ee6', '#c383db', '#dce4ed', '#727d8a', '#a9b3bd'];
    function aciColor(index) { const i = Math.abs(index || 7); if (ACI[i])
        return ACI[i]; const hue = ((i - 10) / 10 % 24) * 15, sat = (i % 2 === 0 ? 1 : .5), value = [1, .8, .6, .5, .3][Math.floor((i % 10) / 2)] || .8; const c = value * sat, x = c * (1 - Math.abs(hue / 60 % 2 - 1)), m = value - c; const rgb = hue < 60 ? [c, x, 0] : hue < 120 ? [x, c, 0] : hue < 180 ? [0, c, x] : hue < 240 ? [0, x, c] : hue < 300 ? [x, 0, c] : [c, 0, x]; return '#' + rgb.map(v => Math.round((v + m) * 255).toString(16).padStart(2, '0')).join(''); }
    const hexColor = n => '#' + (Math.max(0, Number(n)) & 0xffffff).toString(16).padStart(6, '0');
    const decode = s => String(s || '').replace(/\\U\+((?:000[1-9a-f]|0010)[0-9a-f]{4}|[0-9a-f]{4})/gi, (_, n) => String.fromCodePoint(parseInt(n, 16))).replace(/%%d/gi, '°').replace(/%%p/gi, '±').replace(/%%c/gi, '⌀');
    const stripMText = s => decode(s).replace(/\\P/gi, '\n').replace(/\\~/g, ' ').replace(/\\S([^;]+);/g, (_, v) => v.replace(/[\^#]/g, '/')).replace(/\\[AaCcFfHhQqTtWw][^;]*;/g, '').replace(/\\[LlOoKk]/g, '').replace(/[{}]/g, '');
    const unitNames = { 0: 'unitless', 1: 'in', 2: 'ft', 4: 'mm', 5: 'cm', 6: 'm' }, unitCodes = { unitless: 0, in: 1, ft: 2, mm: 4, cm: 5, m: 6 };
    function decodeDXF(data) {
        const bytes = data instanceof ArrayBuffer ? new Uint8Array(data) : ArrayBuffer.isView(data) ? new Uint8Array(data.buffer, data.byteOffset, data.byteLength) : null;
        if (!bytes)
            throw Error('DXF input must be decoded text, an ArrayBuffer, or a byte array.');
        const header = new TextDecoder('windows-1252').decode(bytes.subarray(0, 32768)), version = header.match(/\$ACADVER\s*\r?\n\s*1\s*\r?\n\s*AC(\d+)/), codepage = header.match(/\$DWGCODEPAGE\s*\r?\n\s*3\s*\r?\n\s*([^\r\n]+)/)?.[1]?.trim();
        let encoding = 'windows-1252';
        if (bytes[0] === 239 && bytes[1] === 187 && bytes[2] === 191 || version && Number(version[1]) >= 1021 || /utf.?8/i.test(codepage || ''))
            encoding = 'utf-8';
        else if (codepage) {
            const id = codepage.match(/(?:ANSI_|DOS|CP)(\d+)/i)?.[1], map = { '932': 'shift_jis', '936': 'gbk', '949': 'euc-kr', '950': 'big5', '874': 'windows-874', '65001': 'utf-8', '866': 'ibm866' };
            encoding = map[id] || (id ? 'windows-' + id : codepage.toLowerCase());
        }
        try {
            return { text: new TextDecoder(encoding).decode(bytes), encoding };
        }
        catch {
            throw Error('Unsupported DXF character encoding ' + encoding + '. Resave as UTF-8 DXF R2007 or later.');
        }
    }
    function parseDXF(text, name = 'Imported drawing') {
        let encoding = 'decoded text';
        if (typeof text !== 'string') {
            const decoded = decodeDXF(text);
            text = decoded.text;
            encoding = decoded.encoding;
        }
        if (text.startsWith('AutoCAD Binary DXF'))
            throw Error('Binary DXF is not supported. Export an ASCII DXF instead.');
        if (text.length > 64 * 1024 * 1024)
            throw Error('DXF limit is 64 MiB.');
        const lines = text.replace(/^\uFEFF/, '').split(/\r?\n/), pairs = [];
        for (let i = 0; i + 1 < lines.length; i += 2) {
            const c = Number(lines[i].trim());
            if (!Number.isInteger(c) || c < 0 || c > 1071)
                throw Error('Invalid DXF group code near line ' + (i + 1) + '.');
            // Text, attribute and application values may have significant trailing spaces.
            pairs.push([c, lines[i + 1]]);
        }
        if (!pairs.some(p => p[0] === 0 && p[1].trim() === 'SECTION'))
            throw Error('No DXF SECTION record found.');
        const recs = [];
        let rec = null, section = '';
        for (const pair of pairs) {
            if (pair[0] === 0) {
                rec = { type: pair[1].trim().toUpperCase(), pairs: [], section };
                recs.push(rec);
            }
            else if (rec) {
                rec.pairs.push(pair);
                if (rec.type === 'SECTION' && pair[0] === 2) {
                    section = pair[1].trim();
                    rec.section = section;
                }
            }
            if (pair[0] === 0 && pair[1].trim() === 'ENDSEC')
                section = '';
        }
        const values = (r, c) => r.pairs.filter(p => p[0] === c).map(p => p[1]), str = (r, c, f = '') => { const p = r.pairs.find(p => p[0] === c); return p ? p[1].trim() : f; }, num = (r, c, f = 0) => { const s = str(r, c); const v = s === '' ? f : Number(s); return Number.isFinite(v) ? v : f; }, pt = (r, c = 10) => [num(r, c), num(r, c + 10), num(r, c + 20)], has = (r, c) => r.pairs.some(p => p[0] === c);
        const normal = r => has(r, 210) ? pt(r, 210) : [0, 0, 1], ocs = (p, n) => { const b = basis(n); return V.add(V.add(V.mul(b.x, p[0]), V.mul(b.y, p[1])), V.mul(b.n, p[2] || 0)); };
        function repeatedPoints(r, c = 10) { const out = []; let p; for (const [code, value] of r.pairs) {
            if (code === c) {
                p = [Number(value) || 0, 0, 0];
                out.push(p);
            }
            else if (p && code === c + 10)
                p[1] = Number(value) || 0;
            else if (p && code === c + 20)
                p[2] = Number(value) || 0;
        } return out; }
        const doc = new K.Drawing(name);
        doc.layers = [];
        doc.entities = [];
        const layerNames = new Map(), report = { read: 0, created: 0, skipped: {}, warnings: [], source: 'DXF', encoding };
        let inUnits = 0;
        for (let i = 0; i < pairs.length - 1; i++)
            if (pairs[i][0] === 9 && pairs[i][1].trim() === '$INSUNITS')
                inUnits = Number(pairs[i + 1][1]) || 0;
        doc.units = unitNames[inUnits] || 'unitless';
        function ensureLayer(name, record) { name = decode(name || '0'); if (layerNames.has(name))
            return layerNames.get(name); const id = name === '0' ? '0' : K.uid('layer'), l = { id, name, color: record && has(record, 420) ? hexColor(num(record, 420)) : aciColor(record ? num(record, 62, 7) : 7), visible: record ? num(record, 62, 7) >= 0 && !(num(record, 70) & 1) : true, locked: record ? !!(num(record, 70) & 4) : false, linetype: record ? str(record, 6, 'Continuous') : 'Continuous', lineweight: record && num(record, 370) > 0 ? num(record, 370) / 100 : .25 }; doc.layers.push(l); layerNames.set(name, id); return id; }
        for (const r of recs)
            if (r.section === 'TABLES' && r.type === 'LAYER')
                ensureLayer(str(r, 2, '0'), r);
        ensureLayer('0');
        doc.reindex();
        doc.currentLayer = '0';
        const blocks = new Map();
        let block = null;
        for (const r of recs.filter(r => r.section === 'BLOCKS')) {
            if (r.type === 'BLOCK') {
                block = { id: K.uid('block'), name: str(r, 2), base: pt(r), records: [] };
                blocks.set(block.name, block);
            }
            else if (r.type === 'ENDBLK')
                block = null;
            else if (block)
                block.records.push(r);
        }
        function skip(type, detail) { report.skipped[type] = (report.skipped[type] || 0) + 1; if (detail && !report.warnings.includes(detail))
            report.warnings.push(detail); }
        function common(r, inheritedLayer) { const lname = str(r, 8, '0'), c = num(r, 62, 256); return { layer: lname === '0' && inheritedLayer ? inheritedLayer : ensureLayer(lname), color: has(r, 420) ? hexColor(num(r, 420)) : c === 0 ? 'byblock' : c !== 256 ? aciColor(c) : 'bylayer', linetype: str(r, 6, 'ByLayer'), lineweight: num(r, 370) > 0 ? num(r, 370) / 100 : undefined, sourceHandle: str(r, 5) || undefined, hidden: num(r, 60) ? true : undefined }; }
        function emit(e, r, m, inherit) { if (!e)
            return; let out = { ...common(r, inherit), ...e }; if (m)
            out = G.transform(out, m); doc.add(out); report.created++; if (report.created > 200000)
            throw Error('Import entity limit exceeded (200,000).'); }
        function readRecords(list, m = null, inherit = null, stack = []) {
            for (let i = 0; i < list.length; i++) {
                const r = list[i], t = r.type;
                if (['ENDSEC', 'SECTION', 'EOF', 'SEQEND', 'VERTEX'].includes(t))
                    continue;
                report.read++;
                try {
                    if (num(r, 67) === 1) {
                        skip(t + ' (paper space)', 'Paper-space entities are not imported into model space.');
                        continue;
                    }
                    if (t === 'LINE')
                        emit({ type: 'LINE', points: [pt(r), pt(r, 11)] }, r, m, inherit);
                    else if (t === 'POINT')
                        emit({ type: 'POINT', position: pt(r) }, r, m, inherit);
                    else if (t === 'LWPOLYLINE') {
                        const points = [], bulges = [];
                        let p = null, b = 0;
                        for (const [code, value] of r.pairs) {
                            if (code === 10) {
                                if (p) {
                                    points.push(ocs(p, normal(r)));
                                    bulges.push(b);
                                }
                                p = [Number(value) || 0, 0, num(r, 38)];
                                b = 0;
                            }
                            else if (p && code === 20)
                                p[1] = Number(value) || 0;
                            else if (p && code === 42)
                                b = Number(value) || 0;
                        }
                        if (p) {
                            points.push(ocs(p, normal(r)));
                            bulges.push(b);
                        }
                        if (points.length > 1)
                            emit({ type: 'POLYLINE', points, bulges, closed: !!(num(r, 70) & 1), normal: normal(r) }, r, m, inherit);
                    }
                    else if (t === 'POLYLINE') {
                        const vr = [];
                        while (i + 1 < list.length && list[i + 1].type === 'VERTEX')
                            vr.push(list[++i]);
                        if (i + 1 < list.length && list[i + 1].type === 'SEQEND')
                            i++;
                        const flags = num(r, 70);
                        if (flags & 64) {
                            const vertices = vr.filter(v => num(v, 70) & 64).map(v => pt(v)), faces = vr.filter(v => !(num(v, 70) & 64) && has(v, 71)).map(v => [71, 72, 73, 74].filter(c => num(v, c) !== 0).map(c => Math.abs(num(v, c)) - 1));
                            if (vertices.length && faces.length)
                                emit({ type: 'MESH', vertices, faces, primitive: 'Polyface mesh' }, r, m, inherit);
                            else
                                skip(t, 'A malformed polyface mesh was skipped.');
                        }
                        else if (flags & 16) {
                            const vertices = vr.map(v => pt(v)), rows = num(r, 71), cols = num(r, 72), faces = [];
                            for (let row = 0; row < rows - 1; row++)
                                for (let col = 0; col < cols - 1; col++) {
                                    const a = row * cols + col;
                                    faces.push([a, a + 1, a + 1 + cols, a + cols]);
                                }
                            if (faces.length)
                                emit({ type: 'MESH', vertices, faces, primitive: 'Polygon mesh' }, r, m, inherit);
                        }
                        else {
                            const points = vr.map(v => flags & 8 ? pt(v) : ocs([num(v, 10), num(v, 20), num(r, 30)], normal(r))), bulges = vr.map(v => num(v, 42));
                            if (points.length > 1)
                                emit({ type: 'POLYLINE', points, closed: !!(flags & 1), bulges, normal: normal(r) }, r, m, inherit);
                        }
                    }
                    else if (t === 'CIRCLE' || t === 'ARC') {
                        const e = { type: t, center: ocs(pt(r), normal(r)), radius: Math.max(EPS, num(r, 40, 1)), normal: normal(r) };
                        if (t === 'ARC') {
                            e.startAngle = num(r, 50) * Math.PI / 180;
                            e.endAngle = num(r, 51) * Math.PI / 180;
                        }
                        emit(e, r, m, inherit);
                    }
                    else if (t === 'ELLIPSE') {
                        const axisX = pt(r, 11), axisY = V.mul(V.norm(V.cross(normal(r), axisX)), V.len(axisX) * num(r, 40, 1));
                        emit({ type: 'ELLIPSE', center: pt(r), axisX, axisY, rx: V.len(axisX), ry: V.len(axisY), startAngle: num(r, 41), endAngle: num(r, 42, TAU) }, r, m, inherit);
                    }
                    else if (t === 'SPLINE') {
                        let controlPoints = repeatedPoints(r), degree = Math.max(1, Math.min(10, num(r, 71, 3)));
                        if (controlPoints.length < 2) {
                            controlPoints = repeatedPoints(r, 11);
                            degree = Math.min(3, controlPoints.length - 1);
                            report.warnings.push('Fit-point-only SPLINE was approximated as a B-spline.');
                        }
                        if (controlPoints.length >= 2)
                            emit({ type: 'SPLINE', controlPoints, degree: Math.min(degree, controlPoints.length - 1), knots: values(r, 40).map(Number), weights: has(r, 41) ? values(r, 41).map(Number) : undefined, closed: !!(num(r, 70) & 1) }, r, m, inherit);
                        else
                            skip(t);
                    }
                    else if (t === 'ATTRIB' && K.Production && doc.entities.at(-1)?.type === 'INSERT') {
                        doc.entities.at(-1).attributes[decode(str(r, 2))] = decode(values(r, 1)[0] || '');
                    }
                    else if(t==='MTEXT' && K.MText){
                        const e=K.MText.fromDXF(r,{num,str,values,pt,normal});K.MText.validate(e);emit(e,r,m,inherit);for(const w of K.MText.parse(e.text,{height:e.height}).warnings)report.warnings.push(w);
                    }
                    else if (t === 'TEXT' || t === 'MTEXT' || t === 'ATTRIB' || t === 'ATTDEF') {
                        if ((t === 'ATTRIB' || t === 'ATTDEF') && (num(r, 70) & 1))
                            continue;
                        const raw = t === 'MTEXT' ? values(r, 3).join('') + (values(r, 1)[0] || '') : (values(r, 1)[0] || '');
                        const align = t === 'MTEXT' ? [2, 5, 8].includes(num(r, 71)) ? 'center' : [3, 6, 9].includes(num(r, 71)) ? 'right' : 'left' : num(r, 72) === 1 ? 'center' : num(r, 72) === 2 ? 'right' : 'left';
                        emit({ type: 'TEXT', position: t === 'MTEXT' ? pt(r) : ocs(align !== 'left' && has(r, 11) ? pt(r, 11) : pt(r), normal(r)), text: t === 'MTEXT' ? stripMText(raw) : decode(raw), attributeTag: t === 'ATTDEF' ? decode(str(r, 2)) : undefined, textStyle:decode(str(r,7,'STANDARD')),widthFactor:t==='MTEXT'?1:num(r,41,1),oblique:t==='MTEXT'?0:num(r,51),backwards:t==='MTEXT'?false:!!(num(r,71)&2),upsideDown:t==='MTEXT'?false:!!(num(r,71)&4),lineSpacing:t==='MTEXT'?1.35*num(r,44,1):1.35, height: Math.max(EPS, num(r, 40, 10)), rotation: t === 'MTEXT' && has(r, 11) ? Math.atan2(num(r, 21), num(r, 11)) : num(r, 50) * Math.PI / 180, align, direction: t === 'MTEXT' && has(r, 11) ? pt(r, 11) : undefined, normal: normal(r) }, r, m, inherit);
                        if (t === 'MTEXT' && !report.warnings.includes('MTEXT inline formatting is approximated; named font styles are retained.'))
                            report.warnings.push('MTEXT inline formatting is approximated; named font styles are retained.');
                    }
                    else if (t === '3DFACE' || t === 'SOLID' || t === 'TRACE') {
                        let points = [pt(r), pt(r, 11), pt(r, 12), pt(r, 13)];
                        if (t !== '3DFACE') {
                            points = [points[0], points[1], points[3], points[2]].map(p => ocs(p, normal(r)));
                        }
                        if (V.same(points[2], points[3]))
                            points.pop();
                        emit({ type: 'MESH', vertices: points, faces: [points.map((_, i) => i)], primitive: t === '3DFACE' ? '3D face' : 'Planar face' }, r, m, inherit);
                    }
                    else if (t === 'INSERT') {
                        const name = str(r, 2), block = blocks.get(name);
                        if (!block) {
                            skip(t, 'Missing block definition: ' + name);
                            continue;
                        }
                        if (stack.includes(name) || stack.length > 12) {
                            skip(t, 'Cyclic or over-deep block reference: ' + name);
                            continue;
                        }
                        const c = common(r, inherit), columns = Math.max(1, num(r, 70, 1)), rows = Math.max(1, num(r, 71, 1));
                        if (columns * rows > 10000)
                            throw Error('Block array instance limit exceeded.');
                        const ax = basis(normal(r)), orient = M.identity();
                        orient.set(ax.x, 0);
                        orient.set(ax.y, 4);
                        orient.set(ax.n, 8);
                        const base = M.multiply(M.translation(...ocs(pt(r), normal(r))), M.multiply(orient, M.rotation(num(r, 50) * Math.PI / 180))), scale = M.scale(num(r, 41, 1), num(r, 42, 1), num(r, 43, 1)), locations = new Set();
                        for (let row = 0; row < rows; row++)
                            for (let col = 0; col < columns; col++) {
                                const dx = col * num(r, 44), dy = row * num(r, 45), key = dx + ',' + dy;
                                if (locations.has(key))
                                    continue;
                                locations.add(key);
                                const matrix = M.multiply(base, M.multiply(M.translation(dx, dy, 0), M.multiply(scale, M.translation(...V.mul(block.base, -1)))));
                                if (K.Production) emit({ type: 'INSERT', block: block.id, matrix: Array.from(matrix), attributes: {} }, r, m, inherit);
                                else readRecords(block.records, m ? M.multiply(m, matrix) : matrix, c.layer, [...stack, name]);
                            }
                    }
                    else if (t === 'DIMENSION') {
                        const kind = num(r, 70) & 7;
                        if ((kind === 1 || kind === 0 && K.Production) && has(r, 13) && has(r, 14)) {
                            const a = pt(r, 13), b = pt(r, 14), axis = [Math.cos(num(r,50)*Math.PI/180), Math.sin(num(r,50)*Math.PI/180),0], n = V.norm(V.cross(normal(r), kind === 0 ? axis : V.sub(b, a))), off = V.dot(V.sub(pt(r), a), n), text = str(r, 1);
                            emit({ type: 'DIMENSION', kind: kind === 0 ? 'linear' : undefined, measureAxis: kind === 0 ? axis : undefined, dimstyle: K.Production ? decode(str(r,3,'STANDARD')) : undefined, points: [a, b], offset: off, normal: normal(r), text: text && text !== '<>' ? decode(text) : undefined, textHeight: Math.max(V.dist(a, b) * .025, .1) }, r, m, inherit);
                        }
                        else if (blocks.has(str(r, 2))) {
                            readRecords(blocks.get(str(r, 2)).records, m, common(r, inherit).layer, [...stack, str(r, 2)]);
                            report.warnings.push('A non-aligned dimension was imported as display geometry.');
                        }
                        else
                            skip(t, 'Unsupported dimension subtype without an anonymous display block.');
                    }
                    else if (t === 'HATCH' && K.Production && num(r, 91, 1) > 1) {
                        const loops = []; let begin = r.pairs.findIndex(p => p[0] === 92);
                        for (let loopIndex = 0; loopIndex < num(r, 91); loopIndex++) {
                            if (begin < 0 || !(Number(r.pairs[begin][1]) & 2)) throw Error('Only polyline hatch edge loops are supported.');
                            let j = begin + 1; while (j < r.pairs.length && r.pairs[j][0] !== 93) j++;
                            const count = Number(r.pairs[j++]?.[1]), points = [], bulges = [];
                            if (!Number.isInteger(count) || count < 3 || count > 10000) throw Error('Invalid hatch loop.');
                            for (let k = 0; k < count; k++) {
                                if (r.pairs[j]?.[0] !== 10 || r.pairs[j+1]?.[0] !== 20) throw Error('Invalid hatch vertex.');
                                points.push(ocs([Number(r.pairs[j][1]), Number(r.pairs[j+1][1]), num(r, 30)], normal(r))); j += 2;
                                bulges.push(r.pairs[j]?.[0] === 42 ? Number(r.pairs[j++][1]) : 0);
                            }
                            loops.push(bulges.some(Boolean) ? G.path({ type:'POLYLINE', points, bulges, closed:true, normal:normal(r) }, .1) : points);
                            begin = r.pairs.findIndex((p, i) => i >= j && p[0] === 92);
                        }
                        emit({ type:'HATCH', points:loops[0], loops, normal:normal(r), pattern:num(r,70)?'solid':num(r,78)>1?'cross':'ANSI31', angle:num(r,52,45)*Math.PI/180, spacing:Math.max(.1,Math.hypot(num(r,45),num(r,46))||3.175) }, r,m,inherit);
                    }
                    else if (t === 'HATCH') {
                        if (num(r, 91, 1) !== 1) {
                            skip(t, 'Multi-boundary hatches (holes) are not imported.');
                            continue;
                        }
                        const start = r.pairs.findIndex(p => p[0] === 92);
                        if (start < 0 || !(Number(r.pairs[start][1]) & 2)) {
                            skip(t, 'Only polyline-boundary hatches are supported.');
                            continue;
                        }
                        const end = r.pairs.findIndex((p, j) => j > start && p[0] === 97), slice = { pairs: r.pairs.slice(start, end < 0 ? undefined : end) }, points = repeatedPoints(slice).map(p => ocs([p[0], p[1], num(r, 30)], normal(r)));
                        if (points.length >= 3) {
                            const bulges = [];
                            let vertex = -1;
                            for (const [code, value] of slice.pairs) {
                                if (code === 10) {
                                    vertex++;
                                    bulges[vertex] = 0;
                                }
                                else if (code === 42 && vertex >= 0)
                                    bulges[vertex] = Number(value) || 0;
                            }
                            const boundary = bulges.some(Boolean) ? G.path({ type: 'POLYLINE', points, bulges, closed: true, normal: normal(r) }, .25) : points;
                            emit({ type: 'HATCH', points: boundary, normal: normal(r), pattern: num(r, 70) ? 'solid' : num(r, 78) > 1 ? 'cross' : 'ANSI31', angle: num(r, 52, 45) * Math.PI / 180, spacing: Math.max(.1, Math.hypot(num(r, 45), num(r, 46)) || num(r, 41, 1) * 3.175) }, r, m, inherit);
                        }
                        else
                            skip(t);
                    }
                    else
                        skip(t);
                }
                catch (error) {
                    if (/limit exceeded/i.test(error.message))
                        throw error;
                    skip(t, 'Skipped malformed ' + t + ': ' + error.message);
                }
            }
        }
        if (K.Production) {
            doc.production.textstyles = recs.filter(r=>r.section==='TABLES'&&r.type==='STYLE').filter(r=>!(num(r,70)&1)).map(r=>({name:decode(str(r,2,'STANDARD')),font:decode(str(r,3,'')),bigFont:decode(str(r,4,'')),height:Math.max(0,num(r,40)),width:Math.max(.001,num(r,41,1)),oblique:num(r,50),backwards:!!(num(r,71)&2),upsideDown:!!(num(r,71)&4),vertical:!!(num(r,70)&4)}));
            if(!doc.production.textstyles.some(s=>s.name.toLowerCase()==='standard'))doc.production.textstyles.push({name:'STANDARD',font:'',height:0,width:1,oblique:0});
            doc.production.dimstyles = recs.filter(r => r.section === 'TABLES' && r.type === 'DIMSTYLE').map(r => ({ name: decode(str(r, 2, 'STANDARD')), textHeight: Math.max(1e-7, num(r, 140, 2.5)), precision: Math.max(0, Math.min(8, Math.round(num(r, 271, 2)))), scale: Math.max(1e-7, num(r, 144, 1)), prefix: '', suffix: '' }));
            if (!doc.production.dimstyles.length) doc.production.dimstyles = K.Production.defaults().dimstyles;
            const usedBlockNames = new Set();
            for (const block of blocks.values()) {
                // Model/paper-space containers are not user definitions. Importing them
                // repeatedly otherwise manufactures colliding _Model_Space blocks.
                if (/^\*(?:Model_Space|Paper_Space\d*)$/i.test(block.name)) continue;
                const start = doc.entities.length;
                readRecords(block.records, null, null, [block.name]);
                const entities = doc.entities.splice(start), attributes = entities.filter(e => e.attributeTag).map(e => ({ tag:e.attributeTag, value:e.text, position:e.position, height:e.height, rotation:e.rotation||0, textStyle:e.textStyle, widthFactor:e.widthFactor, oblique:e.oblique, backwards:e.backwards, upsideDown:e.upsideDown, vertical:e.vertical }));
                let label = decode(block.name).replace(/[\x00-\x1f<>/\\":;?*|=]/g, '_').trim() || block.id;
                if (['__proto__', 'constructor', 'prototype'].includes(label)) label = '_' + label;
                label = label.slice(0, 240); const stem = label; let suffix = 1;
                while (usedBlockNames.has(label.toLowerCase())) label = stem + '_' + suffix++;
                usedBlockNames.add(label.toLowerCase());
                doc.production.blocks.push({ id: block.id, name: label, entities: entities.filter(e => !e.attributeTag), attributes });
            }
        }
        readRecords(recs.filter(r => r.section === 'ENTITIES' && !['SECTION', 'ENDSEC'].includes(r.type)));
        doc.reindex();
        doc.dirty = false;
        doc.undoStack = [];
        if (!doc.entities.length && Object.keys(report.skipped).length)
            report.warnings.push('No supported model-space entities were found.');
        return { data: doc.serialize(), report };
    }
    function writeDXF(data) {
        const doc = data instanceof K.Drawing ? data : K.Drawing.from(data), out = [];
        let handle = 0x100;
        const h = () => (++handle).toString(16).toUpperCase(), put = (...pairs) => { for (let i = 0; i < pairs.length; i += 2)
            out.push(String(pairs[i]), String(pairs[i + 1])); }, point = (code, p) => put(code, p[0], code + 10, p[1], code + 20, p[2] || 0), esc = s => String(s || '').replace(/[\r\n]/g, ' ').replace(/[^\x20-\x7E]/g, c => { const n = c.codePointAt(0); return n <= 0xffff ? '\\U+' + n.toString(16).toUpperCase().padStart(4, '0') : [...c].map(() => c).join(''); });
        const userBlocks = (doc.production?.blocks || []).map(b => ({ ...b, name: esc(b.name), handle: h() })), userBlockMap = new Map(userBlocks.map(b => [b.id, b]));
        const layerName = id => esc(doc.layer(id).name), modelHandle = h(), paperHandle = h(), dimensionBlocks = doc.entities.filter(e => e.type === 'DIMENSION').map((e, i) => ({ e, name: '*D' + (i + 1), handle: h() })), dimMap = new Map(dimensionBlocks.map(v => [v.e.id, v]));
        put(0, 'SECTION', 2, 'HEADER', 9, '$ACADVER', 1, 'AC1015', 9, '$ACADMAINTVER', 70, 6, 9, '$DWGCODEPAGE', 3, 'ANSI_1252', 9, '$INSUNITS', 70, unitCodes[doc.units] ?? 4, 9, '$MEASUREMENT', 70, ['in', 'ft'].includes(doc.units) ? 0 : 1, 9, '$LUNITS', 70, 2, 9, '$LUPREC', 70, 4, 9, '$HANDSEED', 5, 'FFFFFFF', 9, '$INSBASE');
        point(10, [0, 0, 0]);
        put(0, 'ENDSEC');
        put(0, 'SECTION', 2, 'TABLES');
        put(0, 'TABLE', 2, 'LTYPE', 5, h(), 100, 'AcDbSymbolTable', 70, 3);
        for (const [name, desc, dashes] of [['Continuous', 'Solid line', []], ['Dashed', 'Dashed __ __', [12, -6]], ['Center', 'Center ____ _', [24, -6, 3, -6]]]) {
            put(0, 'LTYPE', 5, h(), 100, 'AcDbSymbolTableRecord', 100, 'AcDbLinetypeTableRecord', 2, name, 70, 0, 3, desc, 72, 65, 73, dashes.length, 40, dashes.reduce((a, b) => a + Math.abs(b), 0));
            for (const d of dashes)
                put(49, d, 74, 0);
        }
        put(0, 'ENDTAB');
        put(0, 'TABLE', 2, 'LAYER', 5, h(), 100, 'AcDbSymbolTable', 70, doc.layers.length);
        for (const l of doc.layers) {
            put(0, 'LAYER', 5, h(), 100, 'AcDbSymbolTableRecord', 100, 'AcDbLayerTableRecord', 2, esc(l.name), 70, l.locked ? 4 : 0, 62, l.visible ? 7 : -7, 420, parseInt(l.color.slice(1), 16), 6, /center/i.test(l.linetype) ? 'Center' : /dash/i.test(l.linetype) ? 'Dashed' : 'Continuous', 370, Math.round((l.lineweight || .25) * 100));
        }
        put(0, 'ENDTAB');
        const fontStyles=K.Fonts?K.Fonts.styles(doc):doc.production?.textstyles||[{name:'STANDARD',font:'',height:0,width:1,oblique:0}];
        put(0,'TABLE',2,'STYLE',5,h(),100,'AcDbSymbolTable',70,fontStyles.length);
        for(const s of fontStyles)put(0,'STYLE',5,h(),100,'AcDbSymbolTableRecord',100,'AcDbTextStyleTableRecord',2,esc(s.name),70,s.vertical?4:0,40,s.height||0,41,s.width||1,50,s.oblique||0,71,(s.backwards?2:0)|(s.upsideDown?4:0),42,2.5,3,esc(s.font||''),4,esc(s.bigFont||''));
        put(0,'ENDTAB');
        put(0, 'TABLE', 2, 'DIMSTYLE', 5, h(), 100, 'AcDbSymbolTable', 100, 'AcDbDimStyleTable', 70, 1, 0, 'DIMSTYLE', 105, h(), 100, 'AcDbSymbolTableRecord', 100, 'AcDbDimStyleTableRecord', 2, 'STANDARD', 70, 0, 40, 1, 41, doc.entities.find(e => e.type === 'DIMENSION')?.textHeight || 2.5, 140, doc.entities.find(e => e.type === 'DIMENSION')?.textHeight || 2.5, 147, 1, 271, 2, 0, 'ENDTAB');
        put(0, 'TABLE', 2, 'APPID', 5, h(), 100, 'AcDbSymbolTable', 70, 1, 0, 'APPID', 5, h(), 100, 'AcDbSymbolTableRecord', 100, 'AcDbRegAppTableRecord', 2, 'ACAD', 70, 0, 0, 'ENDTAB');
        put(0, 'TABLE', 2, 'BLOCK_RECORD', 5, h(), 100, 'AcDbSymbolTable', 70, 2 + dimensionBlocks.length + userBlocks.length);
        for (const b of [{ name: '*Model_Space', handle: modelHandle }, { name: '*Paper_Space', handle: paperHandle }, ...dimensionBlocks, ...userBlocks])
            put(0, 'BLOCK_RECORD', 5, b.handle, 100, 'AcDbSymbolTableRecord', 100, 'AcDbBlockTableRecord', 2, b.name, 70, 0, 280, 1, 281, 0);
        put(0, 'ENDTAB', 0, 'ENDSEC');
        function base(type, e, owner = modelHandle) { put(0, type, 5, h(), 330, owner, 100, 'AcDbEntity', 8, layerName(e.layer)); if (e.color === 'byblock') put(62, 0); else if (e.color && e.color !== 'bylayer')
            put(420, parseInt(e.color.slice(1), 16)); if (e.hidden) put(60, 1); if (e.lineweight)
            put(370, Math.round(e.lineweight * 100)); if (e.linetype && e.linetype !== 'ByLayer')
            put(6, /center/i.test(e.linetype) ? 'Center' : /dash/i.test(e.linetype) ? 'Dashed' : 'Continuous'); }
        function writeEntity(e, owner = modelHandle) {
            if (e.type === 'INSERT') {
                const b = userBlockMap.get(e.block); if (!b) throw Error('Missing exported block.');
                const m = e.matrix, x = [m[0],m[1],m[2]], y = [m[4],m[5],m[6]], z = [m[8],m[9],m[10]], n = V.norm(V.cross(x,y)), ax = basis(n), sx = V.len(x), sy = V.len(y), sz = V.dot(z,n);
                if (Math.abs(V.dot(x,y)) > 1e-7*sx*sy || Math.abs(V.dot(x,z))+Math.abs(V.dot(y,z)) > 1e-7*sx*Math.max(V.len(z),1)) {
                    for (const child of K.Production.expand(doc,e)) writeEntity(child,owner); return;
                }
                const pos = [m[12],m[13],m[14]], attrs = b.attributes || [];
                base('INSERT',e,owner); put(100,'AcDbBlockReference',2,b.name,66,attrs.length?1:0);
                point(10,[V.dot(pos,ax.x),V.dot(pos,ax.y),V.dot(pos,ax.n)]); put(41,sx,42,sy,43,sz,50,Math.atan2(V.dot(x,ax.y),V.dot(x,ax.x))*180/Math.PI); point(210,n);
                for (const a of attrs) {
                    const t = G.transform({ ...(K.Fonts?K.Fonts.properties(a,doc):a), type:'TEXT', position:a.position, text:e.attributes?.[a.tag]??a.value, height:a.height, rotation:a.rotation||0, layer:e.layer, color:e.color },m), axes=G.textAxes(t), ta=basis(axes.n), p=t.position;
                    base('ATTRIB',e,owner); put(100,'AcDbText'); point(10,[V.dot(p,ta.x),V.dot(p,ta.y),V.dot(p,ta.n)]); put(40,t.height,1,esc(t.text),50,Math.atan2(V.dot(axes.x,ta.y),V.dot(axes.x,ta.x))*180/Math.PI,7,esc(t.textStyle||'STANDARD'),41,t.widthFactor||1,51,t.oblique||0,71,(t.backwards?2:0)|(t.upsideDown?4:0)); point(210,axes.n); put(100,'AcDbAttribute',280,0,2,esc(a.tag),70,a.hidden?1:0,73,0,74,0,280,0);
                }
                if(attrs.length){base('SEQEND',e,owner);}
                return;
            }
            if (e.type === 'TABLE' || e.type === 'LEADER') { for(const c of K.Production.flattenGeometry(e,doc))writeEntity(c,owner); return; }

            if (e.type === 'LINE') {
                base('LINE', e, owner);
                put(100, 'AcDbLine');
                point(10, e.points[0]);
                point(11, e.points[1]);
            }
            else if (e.type === 'POINT') {
                base('POINT', e, owner);
                put(100, 'AcDbPoint');
                point(10, e.position);
            }
            else if (e.type === 'POLYLINE') {
                const n = e.normal || [0, 0, 1], ax = basis(n), q = e.points.map(p => [V.dot(p, ax.x), V.dot(p, ax.y), V.dot(p, ax.n)]), planar = q.every(p => Math.abs(p[2] - q[0][2]) < EPS);
                if (planar) {
                    base('LWPOLYLINE', e, owner);
                    put(100, 'AcDbPolyline', 90, q.length, 70, e.closed ? 1 : 0, 38, q[0][2]);
                    point(210, n);
                    q.forEach((p, i) => { put(10, p[0], 20, p[1]); if (e.bulges?.[i])
                        put(42, e.bulges[i]); });
                }
                else {
                    base('POLYLINE', e, owner);
                    put(100, 'AcDb3dPolyline', 66, 1);
                    point(10, [0, 0, 0]);
                    put(70, (e.closed ? 1 : 0) | 8);
                    for (const p of e.bulges?.some(Boolean) ? G.path(e, .25) : e.points) {
                        base('VERTEX', e, owner);
                        put(100, 'AcDbVertex', 100, 'AcDb3dPolylineVertex');
                        point(10, p);
                        put(70, 32);
                    }
                    base('SEQEND', e, owner);
                }
            }
            else if (['CIRCLE', 'ARC', 'ELLIPSE'].includes(e.type)) {
                let { x, y } = G.conicAxes(e), a = e.startAngle || 0, b = e.endAngle ?? TAU;
                const phase = .5 * Math.atan2(2 * V.dot(x, y), V.dot(x, x) - V.dot(y, y));
                const xx = V.add(V.mul(x, Math.cos(phase)), V.mul(y, Math.sin(phase))), yy = V.add(V.mul(x, -Math.sin(phase)), V.mul(y, Math.cos(phase)));
                x = xx;
                y = yy;
                a -= phase;
                b -= phase;
                let n = V.norm(V.cross(x, y));
                if (V.len(n) < EPS)
                    n = [0, 0, 1];
                if (e.type === 'ELLIPSE' && Math.abs(V.len(x) - V.len(y)) > 1e-8) {
                    if (V.len(y) > V.len(x)) {
                        [x, y] = [y, V.mul(x, -1)];
                        a -= Math.PI / 2;
                        b -= Math.PI / 2;
                    }
                    base('ELLIPSE', e, owner);
                    put(100, 'AcDbEllipse');
                    point(10, e.center);
                    point(11, x);
                    point(210, n);
                    put(40, V.len(y) / V.len(x), 41, angle(a), 42, Math.abs(b - a) >= TAU - 1e-8 ? angle(a) + TAU : angle(b));
                }
                else {
                    const isArc = e.type === 'ARC' || e.type === 'ELLIPSE' && e.endAngle != null && K.Math.sweep(a, b) < TAU - EPS, ax = basis(n), offset = Math.atan2(V.dot(x, ax.y), V.dot(x, ax.x)), p = [V.dot(e.center, ax.x), V.dot(e.center, ax.y), V.dot(e.center, ax.n)];
                    base(isArc ? 'ARC' : 'CIRCLE', e, owner);
                    put(100, 'AcDbCircle');
                    point(10, p);
                    put(40, V.len(x));
                    point(210, n);
                    if (isArc)
                        put(100, 'AcDbArc', 50, angle(a + offset) * 180 / Math.PI, 51, angle(b + offset) * 180 / Math.PI);
                }
            }
            else if (e.type === 'SPLINE') {
                const p = e.controlPoints || e.points, degree = Math.min(e.degree || 3, p.length - 1), knots = e.knots?.length === p.length + degree + 1 ? e.knots : G.uniformKnots(p.length, degree);
                base('SPLINE', e, owner);
                // Some readers round knots using group 42. Keep that tolerance well
                // below every distinct span; a fixed 1e-7 collapses small spans.
                let knotTolerance = 1e-14;
                for (let i = 1; i < knots.length; i++)
                    if (knots[i] > knots[i-1]) knotTolerance = Math.min(knotTolerance, (knots[i]-knots[i-1])*1e-6);
                knotTolerance = Math.max(Number.MIN_VALUE, knotTolerance);
                put(100, 'AcDbSpline', 70, (e.closed ? 1 : 0) | (e.weights?.length ? 4 : 0), 71, degree, 72, knots.length, 73, p.length, 74, 0, 42, knotTolerance, 43, 1e-7, 44, 1e-10);
                for (const k of knots)
                    put(40, k);
                for (const w of e.weights || [])
                    put(41, w);
                for (const v of p)
                    point(10, v);
            }
            else if(e.type==='MTEXT'){if(!K.MText)throw Error('MTEXT support is not loaded.');base('MTEXT',e,owner);for(const [c,v]of K.MText.dxfPairs(e))put(c,v);}
            else if (e.type === 'TEXT') {
                if(K.Fonts)e=K.Fonts.properties(e,doc);
                const lines = (e.text || '').split('\n'), axes = G.textAxes(e), normal = axes.n, ax = basis(normal), rotation = Math.atan2(V.dot(axes.x, ax.y), V.dot(axes.x, ax.x));
                for (let i = 0; i < lines.length; i++) {
                    const pos = V.add(e.position, V.mul(axes.y, -i * (e.height || 10) * (e.lineSpacing||1.35))), ocs = [V.dot(pos, ax.x), V.dot(pos, ax.y), V.dot(pos, ax.n)];
                    base('TEXT', e, owner);
                    put(100, 'AcDbText');
                    point(10, ocs);
                    put(40, e.height || 10, 1, esc(lines[i]), 50, rotation * 180 / Math.PI, 41, e.widthFactor||1, 51, e.oblique||0, 71, (e.backwards?2:0)|(e.upsideDown?4:0), 7, esc(e.textStyle||'STANDARD'), 72, e.align === 'center' ? 1 : e.align === 'right' ? 2 : 0);
                    if (e.align && e.align !== 'left')
                        point(11, ocs);
                    point(210, normal);
                    put(100, 'AcDbText', 73, 0);
                }
            }
            else if (e.type === 'DIMENSION') {
                const d = G.dimension(e), block = dimMap.get(e.id);
                if (!block || e.kind && !['aligned','linear'].includes(e.kind)) {
                    for (const s of d.segments)
                        writeEntity({ ...e, type: 'LINE', points: s }, owner);
                    writeEntity({ ...e, ...d.text, type: 'TEXT' }, owner);
                    return;
                }
                base('DIMENSION', e, owner);
                put(100, 'AcDbDimension', 2, block.name);
                const n = V.norm(V.cross(e.normal || [0, 0, 1], e.kind === 'linear' ? e.measureAxis || [1,0,0] : V.sub(e.points[1], e.points[0])));
                point(10, V.add(e.points[1], V.mul(n, e.offset || 0)));
                point(11, d.text.position);
                put(70, e.kind === 'linear' ? 32 : 33, 1, e.text ? esc(e.text) : '<>', 3, 'STANDARD', 100, 'AcDbAlignedDimension');
                point(13, e.points[0]);
                point(14, e.points[1]);
                if(e.kind==='linear')put(100,'AcDbRotatedDimension',50,Math.atan2((e.measureAxis||[1,0,0])[1],(e.measureAxis||[1,0,0])[0])*180/Math.PI);
            }
            else if (e.type === 'HATCH') {
                const normal = e.normal || faceNormal(e.points), ax = basis(normal), p = e.points.map(v => [V.dot(v, ax.x), V.dot(v, ax.y), V.dot(v, ax.n)]), solid = e.pattern === 'solid';
                base('HATCH', e, owner);
                put(100, 'AcDbHatch');
                point(10, [0, 0, p[0][2]]);
                point(210, normal);
                const loops = e.loops || [e.points];
                put(2, solid ? 'SOLID' : 'ANSI31', 70, solid ? 1 : 0, 71, 0, 91, loops.length);
                for(const loop of loops){put(92,2,72,0,73,1,93,loop.length);for(const v of loop)put(10,V.dot(v,ax.x),20,V.dot(v,ax.y));put(97,0);}
                put(75, 0, 76, 1);
                if (!solid) {
                    const angles = e.pattern === 'cross' ? [e.angle || 0, (e.angle || 0) + Math.PI / 2] : [e.angle ?? Math.PI / 4];
                    put(52, (e.angle ?? Math.PI / 4) * 180 / Math.PI, 41, 1, 77, 0, 78, angles.length);
                    for (const a of angles)
                        put(53, a * 180 / Math.PI, 43, 0, 44, 0, 45, -Math.sin(a) * (e.spacing || 10), 46, Math.cos(a) * (e.spacing || 10), 79, 0);
                }
                put(98, 0);
            }
            else if (e.type === 'MESH') {
                for (const t of G.geometry(e).triangles) {
                    base('3DFACE', e, owner);
                    put(100, 'AcDbFace');
                    point(10, t.points[0]);
                    point(11, t.points[1]);
                    point(12, t.points[2]);
                    point(13, t.points[2]);
                    put(70, 0);
                }
            }
        }
        put(0, 'SECTION', 2, 'BLOCKS');
        for (const b of [{ name: '*Model_Space', handle: modelHandle }, { name: '*Paper_Space', handle: paperHandle }, ...dimensionBlocks, ...userBlocks]) {
            put(0, 'BLOCK', 5, h(), 330, b.handle, 100, 'AcDbEntity', 8, '0', 100, 'AcDbBlockBegin', 2, b.name, 70, b.e ? 1 : 0);
            point(10, [0, 0, 0]);
            put(3, b.name, 1, '');
            if (b.entities) {
                for (const e of b.entities) writeEntity(e,b.handle);
                for (const a of b.attributes || []) {
                    base('ATTDEF',{layer:'0'},b.handle);put(100,'AcDbText');point(10,a.position);put(40,a.height,1,esc(a.value),50,(a.rotation||0)*180/Math.PI,7,esc(a.textStyle||'STANDARD'),41,a.widthFactor||1,51,a.oblique||0,71,(a.backwards?2:0)|(a.upsideDown?4:0),100,'AcDbAttributeDefinition',280,0,3,esc(a.tag),2,esc(a.tag),70,a.hidden?1:0,73,0,74,0,280,0);
                }
            }
            if (b.e) {
                const d = G.dimension(b.e);
                for (const s of d.segments)
                    writeEntity({ ...b.e, type: 'LINE', points: s }, b.handle);
                writeEntity({ ...b.e, ...d.text, type: 'TEXT' }, b.handle);
            }
            put(0, 'ENDBLK', 5, h(), 330, b.handle, 100, 'AcDbEntity', 8, '0', 100, 'AcDbBlockEnd');
        }
        put(0, 'ENDSEC', 0, 'SECTION', 2, 'ENTITIES');
        for (const e of doc.entities)
            writeEntity(e);
        put(0, 'ENDSEC', 0, 'EOF');
        return out.join('\n') + '\n';
    }
    const xml = s => String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&apos;' }[c]));
    function writeSVG(doc, options = {}) {
        const camera = options.camera, mono = options.monochrome !== false, w = options.width || 1600, h = options.height || 1000, margin = options.margin ?? 30;
        const all = K.Production?.renderEntities ? [...K.Production.renderEntities(doc)] : doc.entities.filter(e=>doc.visible(e)).map(e=>({e,owner:e.id})), geometries = all.map(({e,owner},i) => ({e,svgid:owner===e.id?e.id:owner+'_'+i,g:doc.geometry(e)}));
        let pts = [];
        for (const { g } of geometries)
            for (const p of g.points)
                pts.push(p);
        const projected = pts.map(p => camera ? camera.project(p) : [p[0], -p[1], p[2] || 0]);
        let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
        for (const p of projected) {
            minX = Math.min(minX, p[0]);
            minY = Math.min(minY, p[1]);
            maxX = Math.max(maxX, p[0]);
            maxY = Math.max(maxY, p[1]);
        }
        if (!projected.length) {
            minX = minY = 0;
            maxX = maxY = 100;
        }
        const contentHeight = h - (options.sheet ? 110 : 0);
        const scale = Math.min((w - 2 * margin) / Math.max(maxX - minX, 1), (contentHeight - 2 * margin) / Math.max(maxY - minY, 1)), tx = (w - (maxX - minX) * scale) / 2 - minX * scale, ty = (contentHeight - (maxY - minY) * scale) / 2 - minY * scale;
        const project = p => { const q = camera ? camera.project(p) : [p[0], -p[1]]; return [q[0] * scale + tx, q[1] * scale + ty]; }, fmt = p => p.map(v => Number(v.toFixed(4))).join(',');
        const svg = [`<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="0 0 ${w} ${h}"><title>${xml(doc.name)}</title><rect width="100%" height="100%" fill="${options.background || '#ffffff'}"/>`];
        for (const { e, g, svgid } of geometries) {
            const layer = doc.layer(e), color = mono ? '#16212b' : K.displayColor ? K.displayColor(e.color && !['bylayer','byblock'].includes(e.color) ? e.color : layer.color, 'light') : e.color && !['bylayer','byblock'].includes(e.color) ? e.color : layer.color, width = Math.max(.6, (e.lineweight || layer.lineweight || .25) * 3);
            svg.push(`<g id="${xml(svgid)}" data-layer="${xml(layer.name)}" stroke="${color}" stroke-width="${width}" fill="none" stroke-linejoin="round" stroke-linecap="round">`);
            if (e.type === 'HATCH' && e.pattern === 'solid' || e.type === 'MESH' && options.shaded)
                for (const t of g.triangles)
                    svg.push(`<polygon points="${t.points.map(p => fmt(project(p))).join(' ')}" fill="${color}" fill-opacity="0.18" stroke="none"/>`);
            if (g.segments.length)
                svg.push(`<path d="${g.segments.map(s => 'M' + fmt(project(s[0])) + 'L' + fmt(project(s[1]))).join('')}"${/dash|center/i.test(e.linetype === 'ByLayer' ? layer.linetype : e.linetype || '') ? ' stroke-dasharray="12 5"' : ''}/>`);
            for (const t of g.texts) {
                if(t.composition && K.MText){svg.push(K.MText.svg(t,project,color,xml,mono));continue;}
                if(K.Fonts && t.fontFamily){svg.push(K.Fonts.svgText(t,project,color,xml));continue;}
                const p = project(t.position), hh = camera ? Math.hypot(...V.sub(project(V.add(t.position, [0, t.height || 10, 0])), p).slice(0, 2)) : (t.height || 10) * scale, rot = -(t.rotation || 0) * 180 / Math.PI;
                const lines = (t.text || '').split('\n');
                svg.push(`<text x="${p[0]}" y="${p[1]}" fill="${color}" stroke="none" font-family="Arial,sans-serif" font-size="${Math.max(.5, hh)}" text-anchor="${t.align === 'center' ? 'middle' : t.align === 'right' ? 'end' : 'start'}" transform="rotate(${rot} ${p[0]} ${p[1]})">${lines.map((line, i) => `<tspan x="${p[0]}" dy="${i ? hh * 1.35 : 0}">${xml(line)}</tspan>`).join('')}</text>`);
            }
            svg.push('</g>');
        }
        if (options.sheet)
            svg.push(`<g fill="none" stroke="#26333e" stroke-width="1"><rect x="12" y="12" width="${w - 24}" height="${h - 24}"/><path d="M${w - 480},${h - 112}H${w - 12}M${w - 480},${h - 12}V${h - 112}"/></g><g fill="#26333e" font-family="Arial,sans-serif"><text x="${w - 460}" y="${h - 79}" font-size="20">${xml(doc.name)}</text><text x="${w - 460}" y="${h - 49}" font-size="12">KESTREL CAD · MODEL SPACE · FIT TO SHEET</text><text x="${w - 460}" y="${h - 27}" font-size="11">${xml(doc.units)} · ${new Date().toISOString().slice(0, 10)}</text></g>`);
        svg.push('</svg>');
        return svg.join('');
    }
    K.Exchange = { decodeDXF, parseDXF, writeDXF, writeSVG, aciColor, unitCodes };
})(typeof window !== 'undefined' ? window : globalThis);
