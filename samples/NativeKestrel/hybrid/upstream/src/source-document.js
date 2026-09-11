/* Kestrel CAD — original-source retention, binary DXF and guarded record editing.
 * Raw DXF records are never regenerated unless they are explicitly edited.
 * Uninterpreted application data is retained, not evaluated or certified.
 */
(function (root) {
    'use strict';
    const K = root.Kestrel, X = K.Exchange;
    const baseParse = X.parseDXF, baseWrite = X.writeDXF;
    const MAX_BYTES = 64 * 1024 * 1024, MAX_PAIRS = 2000000;
    const encoder = new TextEncoder(), latin = new TextDecoder('windows-1252');
    const sentinel = encoder.encode('AutoCAD Binary DXF\r\n\x1a\0');
    const validated = new WeakSet(), parsedCache = new WeakMap();
    const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);
    const own = (o, k) => Object.prototype.hasOwnProperty.call(o, k);
    const fail = message => { throw Error(message + ' Use an explicit compatibility export to regenerate supported content.'); };
    function bytes(input) {
        if (typeof input === 'string') return encoder.encode(input);
        if (input instanceof ArrayBuffer) return new Uint8Array(input);
        if (ArrayBuffer.isView(input)) return new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
        throw Error('Drawing input must be text or bytes.');
    }
    function encode64(input) {
        const data = bytes(input); let s = '';
        for (let i = 0; i < data.length; i += 32768) s += String.fromCharCode(...data.subarray(i, i + 32768));
        return btoa(s);
    }
    function decode64(text) {
        if (typeof text !== 'string' || text.length > Math.ceil(MAX_BYTES / 3) * 4 ||
            text.length % 4 || !/^[A-Za-z0-9+/]*={0,2}$/.test(text))
            throw Error('Invalid or oversized original drawing archive.');
        const s = atob(text), out = new Uint8Array(s.length);
        if (s.length > MAX_BYTES) throw Error('Original drawing exceeds 64 MiB.');
        for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i);
        return out;
    }
    function concat(parts) {
        let size = 0; for (const p of parts) size += p.length;
        if (size > MAX_BYTES * 2) throw Error('DXF output exceeds 128 MiB.');
        const out = new Uint8Array(size); let at = 0;
        for (const p of parts) { out.set(p, at); at += p.length; } return out;
    }
    function valueType(code) {
        if (code === 1004 || code >= 310 && code <= 319) return 'binary';
        if (code >= 160 && code <= 169) return 'int64';
        if (code >= 290 && code <= 299) return 'bool';
        if (code >= 10 && code <= 59 || code >= 110 && code <= 149 || code >= 210 && code <= 239 ||
            code >= 460 && code <= 469 || code >= 1010 && code <= 1059) return 'double';
        if (code >= 60 && code <= 79 || code >= 170 && code <= 179 || code >= 270 && code <= 289 ||
            code >= 370 && code <= 389 || code >= 400 && code <= 409 || code >= 1060 && code <= 1070) return 'int16';
        if (code >= 90 && code <= 99 || code >= 420 && code <= 429 || code >= 440 && code <= 459 || code === 1071) return 'int32';
        if (code >= 0 && code <= 9 || [100, 101, 102, 105].includes(code) || code >= 300 && code <= 309 ||
            code >= 320 && code <= 369 || code >= 390 && code <= 399 || code >= 410 && code <= 419 ||
            code >= 430 && code <= 439 || code >= 470 && code <= 479 || code === 999 || code >= 1000 && code <= 1009) return 'string';
        throw Error('No defined binary DXF value type for group ' + code + '.');
    }
    function encodingOf(groups, utf8 = false) {
        let version = '', cp = '';
        for (let i = 0; i + 1 < groups.length; i++) if (groups[i].code === 9) {
            if (groups[i].value.trim() === '$ACADVER') version = groups[i + 1].value.trim();
            if (groups[i].value.trim() === '$DWGCODEPAGE') cp = groups[i + 1].value.trim();
        }
        const id = cp.match(/(?:ANSI_|DOS|CP)(\d+)/i)?.[1];
        const map = { 932: 'shift_jis', 936: 'gbk', 949: 'euc-kr', 950: 'big5', 874: 'windows-874', 65001: 'utf-8', 866: 'ibm866' };
        const encoding = utf8 || Number(version.slice(2)) >= 1021 || /utf.?8/i.test(cp) ? 'utf-8' : map[id] || (id ? 'windows-' + id : 'windows-1252');
        try { return { encoding, version, decoder: new TextDecoder(encoding, { fatal: true }) }; }
        catch (_) { throw Error('Unsupported source character encoding: ' + encoding); }
    }
    function parseRaw(input, suppliedText = false) {
        const raw = bytes(input);
        if (!raw.length || raw.length > MAX_BYTES) throw Error('DXF source must be 1 byte–64 MiB.');
        const binary = sentinel.every((v, i) => raw[i] === v), legacy = binary && raw[23] !== 0;
        const groups = []; let pos = binary ? sentinel.length : 0, newline = '\n';
        const bom = !binary && raw[0] === 239 && raw[1] === 187 && raw[2] === 191;
        if (bom) pos = 3;
        const view = new DataView(raw.buffer, raw.byteOffset, raw.byteLength);
        const need = n => { if (pos + n > raw.length) throw Error('Truncated binary DXF near byte ' + pos + '.'); };
        function line() {
            const start = pos; while (pos < raw.length && raw[pos] !== 10 && raw[pos] !== 13) pos++;
            const end = pos; if (raw[pos] === 13) { pos++; if (raw[pos] === 10) { pos++; newline = '\r\n'; } }
            else if (raw[pos] === 10) pos++;
            return { start, end, next: pos };
        }
        while (pos < raw.length) {
            const start = pos; let code, value, valueStart, valueEnd;
            if (binary) {
                need(legacy ? 1 : 2); code = legacy ? raw[pos++] : view.getUint16(pos, true);
                if (!legacy) pos += 2; else if (code === 255) { need(2); code = view.getUint16(pos, true); pos += 2; }
                const kind = valueType(code); valueStart = pos;
                if (kind === 'string') {
                    while (pos < raw.length && raw[pos] !== 0) pos++;
                    if (pos === raw.length) throw Error('Unterminated binary DXF string at byte ' + valueStart + '.');
                    valueEnd = pos; value = latin.decode(raw.subarray(valueStart, pos++));
                } else if (kind === 'binary') {
                    need(1); const size = raw[pos++]; need(size);
                    value = ''; for (let i = 0; i < size; i++) value += raw[pos++].toString(16).padStart(2, '0').toUpperCase();
                } else if (kind === 'bool') { need(1); value = String(raw[pos++]); if (Number(value) > 1) throw Error('Invalid DXF boolean.'); }
                else if (kind === 'int16') { need(2); value = String(view.getInt16(pos, true)); pos += 2; }
                else if (kind === 'int32') { need(4); value = String(view.getInt32(pos, true)); pos += 4; }
                else if (kind === 'int64') { need(8); value = view.getBigInt64(pos, true).toString(); pos += 8; }
                else { need(8); const v = view.getFloat64(pos, true); if (!Number.isFinite(v)) throw Error('Nonfinite binary DXF number.'); value = String(v); pos += 8; }
            } else {
                const c = line(), text = latin.decode(raw.subarray(c.start, c.end)).trim();
                if (!text && pos === raw.length) break;
                if (!/^\d{1,4}$/.test(text) || Number(text) > 1071) throw Error('Invalid DXF group code near byte ' + start + '.');
                code = Number(text); if (pos === raw.length) throw Error('DXF group is missing its value.');
                const v = line(); valueStart = v.start; valueEnd = v.end; value = latin.decode(raw.subarray(v.start, v.end));
            }
            groups.push({ code, value, start, end: pos, valueStart, valueEnd });
            if (groups.length > MAX_PAIRS) throw Error('DXF group-pair limit exceeded.');
            if (code === 0 && value.trim().toUpperCase() === 'EOF') break;
        }
        const enc = encodingOf(groups, bom || suppliedText);
        for (const g of groups) if (!binary || valueType(g.code) === 'string')
            g.value = enc.decoder.decode(raw.subarray(g.valueStart, g.valueEnd));
        const records = []; let record, section = '', table = '', block = '';
        for (const g of groups) {
            if (g.code === 0) {
                if (record) record.end = g.start;
                record = { type: g.value.trim().toUpperCase(), start: g.start, end: g.end, groups: [], section, table, block };
                records.push(record);
            }
            if (!record) throw Error('DXF must begin with a record.');
            record.groups.push(g); record.end = g.end;
            if (record.type === 'SECTION' && g.code === 2) { section = g.value.trim(); record.section = section; }
            if (record.type === 'TABLE' && g.code === 2) { table = g.value.trim(); record.table = table; }
            if (record.type === 'BLOCK' && g.code === 2) { block = g.value.trim(); record.block = block; }
            if (record.type === 'ENDTAB') table = '';
            if (record.type === 'ENDBLK') block = '';
            if (record.type === 'ENDSEC') { section = ''; table = ''; block = ''; }
        }
        if (!records.some(r => r.type === 'SECTION') || records.at(-1)?.type !== 'EOF') throw Error('DXF needs SECTION and EOF records.');
        for (const r of records) {
            let subclass = '', depth = 0, xdata = false;
            for (const g of r.groups) {
                if (g.code === 1001) xdata = true;
                if (g.code === 102) { g.protected = true; if (g.value.startsWith('{')) depth++; else if (g.value.trim() === '}') depth = Math.max(0, depth - 1); }
                else g.protected = depth > 0 || xdata;
                if (g.code === 100 && !g.protected) subclass = g.value.trim();
                g.subclass = subclass;
            }
            r.handle = r.groups.find(g => !g.protected && (g.code === 5 || g.code === 105))?.value.trim().toUpperCase();
        }
        return { raw, groups, records, binary, legacy, newline, bom, encoding: enc.encoding, outputEncoding: encodingOf(groups, bom).encoding, version: enc.version };
    }
    function escapeUnicode(value) { return String(value).replace(/[^\x20-\x7e\t]/g, c => '\\U+' + c.charCodeAt(0).toString(16).toUpperCase().padStart(4, '0')); }
    function encodePair(code, value, format) {
        value = String(value); if (/[\r\n\0]/.test(value)) throw Error('DXF group values cannot contain raw newlines or NUL.');
        if (!format.binary) return encoder.encode(code + format.newline + ((format.outputEncoding || format.encoding) === 'utf-8' ? value : escapeUnicode(value)) + format.newline);
        const prefix = format.legacy ? code < 255 ? Uint8Array.of(code) : Uint8Array.of(255, code & 255, code >> 8) : Uint8Array.of(code & 255, code >> 8);
        const kind = valueType(code); let data;
        if (kind === 'string') data = concat([encoder.encode((format.outputEncoding || format.encoding) === 'utf-8' ? value : escapeUnicode(value)), Uint8Array.of(0)]);
        else if (kind === 'binary') {
            if (!/^(?:[0-9a-fA-F]{2}){0,255}$/.test(value)) throw Error('Invalid DXF binary chunk.');
            data = new Uint8Array(value.length / 2 + 1); data[0] = value.length / 2;
            for (let i = 0; i < value.length; i += 2) data[i / 2 + 1] = parseInt(value.slice(i, i + 2), 16);
        } else {
            const size = { bool: 1, int16: 2, int32: 4, int64: 8, double: 8 }[kind]; data = new Uint8Array(size); const v = new DataView(data.buffer);
            if (kind === 'int64') { const n = BigInt(value); if (n < -(1n << 63n) || n >= 1n << 63n) throw Error('DXF int64 out of range.'); v.setBigInt64(0, n, true); }
            else { const n = Number(value); if (!Number.isFinite(n)) throw Error('Invalid DXF number.');
                if (kind !== 'double' && (!Number.isInteger(n) || n < (kind === 'bool' ? 0 : -(2 ** (size * 8 - 1))) || n > (kind === 'bool' ? 1 : 2 ** (size * 8 - 1) - 1))) throw Error('DXF integer out of range.');
                if (kind === 'bool') v.setUint8(0, n); else if (kind === 'int16') v.setInt16(0, n, true); else if (kind === 'int32') v.setInt32(0, n, true); else v.setFloat64(0, n, true);
            }
        }
        return concat([prefix, data]);
    }
    const canonical = raw => raw.groups.map(g => g.code + '\n' + g.value).join('\n') + '\n';
    function toBinary(input) {
        const source = parseRaw(input, typeof input === 'string');
        const format = { ...source, binary: true, legacy: Number(source.version.slice(2)) < 1012, encoding: Number(source.version.slice(2)) >= 1021 ? 'utf-8' : 'windows-1252' };
        return concat([sentinel, ...source.groups.filter(g => g.code !== 999).map(g => encodePair(g.code, g.value, format))]);
    }
    function semantic(data) { return { format: 'kestrel-cad', version: 2, units: data.units, layers: data.layers, entities: data.entities, production: data.production }; }
    function validateArchive(source) {
        if (!source) return;
        if (validated.has(source)) return;
        if (source.schema !== 1 || !['dxf', 'dwg'].includes(source.format) || typeof source.name !== 'string' || source.name.length > 512 ||
            typeof source.binary !== 'boolean' || typeof source.suppliedAsText !== 'boolean' || typeof source.baseline !== 'string' || source.baseline.length > 128 * 1024 * 1024 || typeof source.encoding !== 'string') throw Error('Invalid original drawing archive.');
        decode64(source.bytes); if (source.convertedDXF) decode64(source.convertedDXF);
        if (source.format === 'dwg' && !source.convertedDXF) throw Error('Missing converted DXF archive.');
        const data = JSON.parse(source.baseline);
        if (data.sourceDocument) throw Error('Recursive source archive.'); K.validateProject(data);
        Object.freeze(source); validated.add(source);
    }
    function parse(input, name = 'Imported drawing') {
        const raw = parseRaw(input, typeof input === 'string');
        // Keep extension/control data in the archive, out of the simple-entity interpreter.
        const result = baseParse(canonical({ groups: raw.groups.filter(g => !g.protected) }), name);
        const normalized = K.Drawing.from(result.data).serialize({ includeSource: false });
        const archive = { schema: 1, format: 'dxf', name: String(name).slice(0, 500) + '.dxf', encoding: raw.encoding,
            binary: raw.binary, suppliedAsText: typeof input === 'string', bytes: encode64(raw.raw), baseline: JSON.stringify(semantic(normalized)) };
        validateArchive(archive); parsedCache.set(archive, raw); normalized.sourceDocument = archive;
        result.data = normalized; result.report.encoding = raw.encoding; result.report.binary = raw.binary;
        result.report.sourceRetained = true;
        result.report.warnings.push('Original bytes and uninterpreted records are retained in the native project. Source-preserving DXF export supports guarded edits; compatibility export regenerates only supported content.');
        return result;
    }
    function withOriginalDWG(data, original, name) {
        if (!data.sourceDocument) throw Error('Converted drawing lacks a source archive.');
        const raw = bytes(original); if (!/^AC\d{4}/.test(latin.decode(raw.subarray(0, 6)))) throw Error('Invalid DWG signature.');
        const source = { ...data.sourceDocument, format: 'dwg', name: String(name).slice(0, 512), convertedDXF: data.sourceDocument.bytes, bytes: encode64(raw) };
        validateArchive(source); data.sourceDocument = source; return data;
    }
    const get = (r, code, fallback = '') => r.groups.find(g => g.code === code && !g.protected)?.value.trim() ?? fallback;
    const decodedName = s => String(s).replace(/\\U\+([0-9a-f]{4})/gi, (_, n) => String.fromCharCode(parseInt(n, 16)));
    function slots(record) {
        const count = new Map(); let slot = '';
        for (const g of record.groups) {
            if (g.code === 100 && !g.protected) { const key = g.value.trim(), n = count.get(key) || 0; count.set(key, n + 1); slot = key + '#' + n; }
            g.slot = slot;
        } return record;
    }
    function patchRecord(record, changes, format) {
        // A change contains a subclass-local mask and replacement pairs. Application
        // control groups and XDATA are deliberately outside all writable masks.
        slots(record); const hasClasses = record.groups.some(g => g.code === 100 && !g.protected);
        if (!hasClasses) {
            const codes = new Set(), pairs = [];
            for (const c of changes.values()) { for (const code of c.codes) codes.add(code); pairs.push(...c.pairs); }
            changes = new Map([['', { codes, pairs }]]);
        }
        for (const slot of changes.keys()) if (slot && !record.groups.some(g => g.slot === slot))
            fail('The source ' + record.type + ' subclass layout is not supported for editing.');
        const result = [], done = new Set();
        const flush = slot => {
            if (done.has(slot) || !changes.has(slot)) return;
            done.add(slot); for (const [code, value] of changes.get(slot).pairs) result.push(encodePair(code, value, format));
        };
        let previous = '';
        for (const g of record.groups) {
            if (g.code === 100 && !g.protected) flush(previous);
            const c = changes.get(g.slot);
            if (g.protected) { flush(g.slot); result.push(format.raw.subarray(g.start, g.end)); }
            else if (c?.codes.has(g.code)) flush(g.slot);
            else result.push(format.raw.subarray(g.start, g.end));
            previous = g.slot;
        }
        for (const slot of changes.keys()) flush(slot);
        return concat(result);
    }
    function change(changes, slot, codes, pairs) {
        if (!changes.has(slot)) changes.set(slot, { codes: new Set(), pairs: [] });
        const c = changes.get(slot); for (const code of codes) c.codes.add(code); c.pairs.push(...pairs);
    }
    const geometryCodes = {
        LINE: [10, 20, 30, 11, 21, 31], POINT: [10, 20, 30],
        CIRCLE: [10, 20, 30, 40, 210, 220, 230], ARC: [10, 20, 30, 40, 50, 51, 210, 220, 230],
        ELLIPSE: [10, 20, 30, 11, 21, 31, 40, 41, 42, 210, 220, 230],
        LWPOLYLINE: [90, 70, 38, 10, 20, 42, 210, 220, 230],
        SPLINE: [70, 71, 72, 73, 74, 40, 41, 10, 20, 30],
        MTEXT: [10,20,30,11,21,31,40,41,50,71,72,73,44,1,3,7,210,220,230,90,45,63,420,421],
        TEXT: [10, 20, 30, 11, 21, 31, 40, 1, 50, 41, 51, 7, 71, 72, 73, 210, 220, 230]
    };
    const nativeFields = new Set(['points', 'position', 'center', 'normal', 'direction', 'axisX', 'axisY', 'radius', 'rx', 'ry',
        'startAngle', 'endAngle', 'closed', 'bulges', 'controlPoints', 'degree', 'knots', 'weights', 'text', 'height', 'rotation',
        'align', 'textStyle', 'widthFactor', 'oblique', 'backwards', 'upsideDown', 'font', 'lineSpacing', 'vertical','width','attachment','spacingFactor','spacingStyle','background','textAxisX','textAxisY','textDirection','drawingDirection','columns']);
    const commonFields = new Set(['layer', 'color', 'linetype', 'lineweight', 'hidden']);
    function generated(data, entities) {
        const d = new K.Drawing('Source edit'); d.units = data.units; d.layers = K.clone(data.layers); d.entities = K.clone(entities);
        d.production = K.Production.defaults();
        for (const key of ['textstyles', 'dimstyles']) if (data.production?.[key]) d.production[key] = K.clone(data.production[key]);
        d.reindex(); return parseRaw(baseWrite(d), true);
    }
    function modifyEntity(record, before, after, data, source) {
        if (!geometryCodes[record.type] || before.type !== after.type) fail('Source-preserving editing does not support this entity/type change (' + record.type + ').');
        for (const key of new Set([...Object.keys(before), ...Object.keys(after)]))
            if (!eq(before[key], after[key]) && !nativeFields.has(key) && !commonFields.has(key)) fail('The changed property “' + key + '” cannot be represented by a source-preserving edit.');
        const geometryChanged = [...nativeFields].some(key => !eq(before[key], after[key]));
        const changes = new Map(), entitySlot = record.groups.some(g => g.code === 100 && g.value.trim() === 'AcDbEntity') ? 'AcDbEntity#0' : '';
        if (before.layer !== after.layer) change(changes, entitySlot, [8], [[8, data.layers.find(l => l.id === after.layer).name]]);
        if (before.color !== after.color) {
            if (after.color && after.color !== 'bylayer' && !/^#[0-9a-f]{6}$/i.test(after.color)) fail('Unsupported source color.');
            if (Number(source.version.slice(2)) < 1018 && after.color && after.color !== 'bylayer') fail('True color needs a DXF R2004-or-later source.');
            change(changes, entitySlot, [62, 420, 430], after.color && after.color !== 'bylayer' ? [[420, parseInt(after.color.slice(1), 16)]] : []);
        }
        if (before.linetype !== after.linetype) {
            const type = after.linetype || 'ByLayer';
            if (!/^(bylayer|byblock|continuous)$/i.test(type) && !source.records.some(r => r.type === 'LTYPE' && decodedName(get(r, 2)).toLowerCase() === type.toLowerCase())) fail('The selected linetype does not exist in the source drawing.');
            change(changes, entitySlot, [6], /^bylayer$/i.test(type) ? [] : [[6, type]]);
        }
        if (before.lineweight !== after.lineweight) {
            if (after.lineweight != null && Number(source.version.slice(2)) < 1015) fail('Lineweight needs a DXF R2000-or-later source.');
            change(changes, entitySlot, [370], after.lineweight == null ? [] : [[370, Math.round(after.lineweight * 100)]]);
        }
        if (!!before.hidden !== !!after.hidden) change(changes, entitySlot, [60], after.hidden ? [[60, 1]] : []);
        if (geometryChanged) {
            if(record.type==='MTEXT' && (record.groups.some(g=>[75,76,78,79,48,49,101].includes(g.code)&&!g.protected) || after.text.includes('%<') || before.text.includes('%<')))fail('Linked columns, embedded MTEXT objects and fields cannot be source-patched safely.');
            if (Number(get(record, 39, '0')) !== 0) fail('Editing extruded 2D entities with source thickness is not supported.');
            if (record.type === 'LWPOLYLINE' && record.groups.some(g => !g.protected && [40, 41, 43, 91].includes(g.code) && Number(g.value) !== 0))
                fail('Per-vertex widths/identifiers or constant-width polylines require a dedicated width-aware editor.');
            if (record.type === 'SPLINE' && record.groups.some(g => !g.protected && [11, 12, 13].includes(g.code))) fail('Fit-point and tangent-constrained source splines cannot be regenerated safely.');
            if (after.type === 'TEXT' && (after.text.includes('\n') || after.vertical || after.lineSpacing && after.lineSpacing !== 1.35)) fail('A source TEXT record cannot become multiline or vertical text.');
            const produced = generated(data, [after]).records.filter(r => r.section === 'ENTITIES' && !['SECTION', 'ENDSEC'].includes(r.type));
            if (produced.length !== 1 || produced[0].type !== record.type) fail('This change would alter the original DXF entity structure.');
            const target = slots(produced[0]), codes = new Set(geometryCodes[record.type]);
            // Preserve the polyline-generation bit and unrelated SPLINE flags.
            if (record.type === 'LWPOLYLINE') for (const g of target.groups) if (g.code === 70) g.value = String((Number(get(record, 70, '0')) & ~1) | (Number(g.value) & 1));
            if (record.type === 'TEXT') for (const g of target.groups) if (g.code === 71) g.value = String((Number(get(record, 71, '0')) & ~6) | (Number(g.value) & 6));
            if (record.type === 'SPLINE') for (const g of target.groups) if (g.code === 70) g.value = String((Number(get(record, 70, '0')) & ~5) | (Number(g.value) & 5));
            const originalSlots = slots(record).groups.filter(g => codes.has(g.code) && !g.protected && (record.type !== 'MTEXT' || g.slot !== entitySlot));
            for (const g of originalSlots) change(changes, g.slot, [g.code], []);
            for (const g of target.groups) if (codes.has(g.code) && !g.protected && (record.type !== 'MTEXT' || g.slot !== entitySlot)) change(changes, g.slot, [g.code], [[g.code, (source.outputEncoding || source.encoding) === 'utf-8' && valueType(g.code) === 'string' ? decodedName(g.value) : g.value]]);
        }
        return changes.size ? patchRecord(record, changes, source) : source.raw.subarray(record.start, record.end);
    }
    function exportPreserved(data) {
        const doc = data instanceof K.Drawing ? data : K.Drawing.from(data), current = doc.serialize({ includeSource: false }), archive = doc.sourceDocument;
        if (!archive) throw Error('This drawing has no original source. Use the normal DXF exporter.');
        validateArchive(archive); const baseline = JSON.parse(archive.baseline);
        let source = parsedCache.get(archive);
        if (!source) { source = parseRaw(decode64(archive.convertedDXF || archive.bytes), archive.suppliedAsText); parsedCache.set(archive, source); }
        const report = { format: source.binary ? 'binary DXF' : 'ASCII DXF', changed: 0, added: 0, deleted: 0, unchanged: true,
            warnings: ['Uninterpreted metadata is retained but not recomputed; this is not a claim of arbitrary lossless CAD editing.'] };
        if (eq(semantic(current), baseline)) return { bytes: source.raw.slice(), report };
        for (const key of new Set([...Object.keys(baseline.production || {}), ...Object.keys(current.production || {})])) {
            if (['ucs', 'currentTextStyle'].includes(key)) continue; // Native working-view preferences, not part of a model-space save.
            if (!eq(baseline.production?.[key], current.production?.[key])) fail('Changed ' + key + ' definitions are not supported by source-preserving export.');
        }
        const replacements = new Map(), insertions = new Map(), entities = new Map(current.entities.map(e => [e.id, e]));
        const handleRecords = new Map(); let maxHandle = 0n;
        for (const r of source.records) if (r.handle && r.section !== 'HEADER') {
            if (handleRecords.has(r.handle)) fail('Duplicate source handles prevent unambiguous editing.');
            handleRecords.set(r.handle, r);
            if (/^[0-9A-F]{1,16}$/.test(r.handle)) maxHandle = maxHandle > BigInt('0x' + r.handle) ? maxHandle : BigInt('0x' + r.handle);
        }
        const nextHandle = () => { maxHandle++; if (maxHandle >= 1n << 64n) fail('Source handle space is exhausted.'); return maxHandle.toString(16).toUpperCase(); };
        const baselineByHandle = new Map();
        for (const e of baseline.entities) if (e.sourceHandle) {
            const h = e.sourceHandle.toUpperCase(); if (!baselineByHandle.has(h)) baselineByHandle.set(h, []); baselineByHandle.get(h).push(e);
        }
        const deleted = new Set();
        for (const before of baseline.entities) {
            const after = entities.get(before.id); entities.delete(before.id);
            if (after && eq(before, after)) continue;
            const h = before.sourceHandle?.toUpperCase(), r = handleRecords.get(h);
            if (!r || r.section !== 'ENTITIES' || baselineByHandle.get(h)?.length !== 1) fail('This source entity has no unambiguous editable model-space record.');
            if (!geometryCodes[r.type]) fail('Source-preserving edits do not support ' + r.type + '.');
            if (!after) { deleted.add(h); replacements.set(r, new Uint8Array()); report.deleted++; }
            else { replacements.set(r, modifyEntity(r, before, after, current, source)); report.changed++; }
        }
        for (const r of source.records) if (!deleted.has(r.handle)) for (const g of r.groups)
            if ((g.code >= 320 && g.code <= 369 || g.code >= 390 && g.code <= 399 || g.code === 1005) && deleted.has(g.value.trim().toUpperCase()))
                fail('A deleted entity is referenced by retained source data; deletion would leave a dangling handle.');
        const layers = new Map(current.layers.map(l => [l.id, l]));
        for (const before of baseline.layers) {
            const after = layers.get(before.id); layers.delete(before.id);
            if (!after || before.name !== after.name) fail('Source layer deletion/renaming is not supported.');
            if (eq(before, after)) continue;
            const r = source.records.find(r => r.type === 'LAYER' && decodedName(get(r, 2)) === before.name);
            if (!r) fail('The changed layer has no retained source table record.');
            for (const key of new Set([...Object.keys(before), ...Object.keys(after)])) if (!eq(before[key], after[key]) && !['visible', 'locked', 'color', 'linetype', 'lineweight'].includes(key)) fail('Unsupported source layer property: ' + key);
            const c = new Map(), slot = r.groups.some(g => g.code === 100) ? 'AcDbLayerTableRecord#0' : '';
            let flags = Number(get(r, 70, '0')), aci = Number(get(r, 62, '7'));
            if (before.visible !== after.visible) { if (after.visible) flags &= ~1; aci = Math.max(1, Math.abs(aci)) * (after.visible ? 1 : -1); }
            if (before.locked !== after.locked) flags = after.locked ? flags | 4 : flags & ~4;
            if (before.locked !== after.locked || before.visible !== after.visible) change(c, slot, [70, 62], [[70, flags], [62, aci]]);
            if (before.color !== after.color) { if (Number(source.version.slice(2)) < 1018) fail('This source DXF version has no true-color layer support.'); change(c, slot, [420], [[420, parseInt(after.color.slice(1), 16)]]); }
            if (before.lineweight !== after.lineweight) {
                if (Number(source.version.slice(2)) < 1015) fail('Layer lineweight needs a DXF R2000-or-later source.');
                change(c, slot, [370], [[370, Math.round(after.lineweight * 100)]]);
            }
            if (before.linetype !== after.linetype) { if (!source.records.some(r => r.type === 'LTYPE' && decodedName(get(r, 2)).toLowerCase() === after.linetype.toLowerCase())) fail('Linetype is not defined in source.'); change(c, slot, [6], [[6, after.linetype]]); }
            replacements.set(r, patchRecord(r, c, source)); report.changed++;
        }
        if (layers.size) fail('Adding layers requires a compatibility export in this release.');
        if (entities.size) {
            const end = source.records.find(r => r.type === 'ENDSEC' && r.section === 'ENTITIES');
            if (!end) fail('The source has no model-space ENTITIES section.');
            const model = source.records.find(r => r.type === 'BLOCK_RECORD' && /^\*model_space$/i.test(get(r, 2)));
            const owner = model?.handle;
            const extras = [];
            for (const e of entities.values()) {
                if (!['LINE', 'POINT', 'CIRCLE', 'ARC', 'ELLIPSE', 'POLYLINE', 'SPLINE', 'TEXT'].includes(e.type) || e.solid) fail('Adding ' + e.type + ' is not supported by the guarded source exporter.');
                for (const key of Object.keys(e)) if (!['id', 'type', 'sourceHandle'].includes(key) && !nativeFields.has(key) && !commonFields.has(key)) fail('The added property “' + key + '” cannot be represented by the source exporter.');
                if (e.type === 'TEXT' && (e.text.includes('\n') || e.vertical)) fail('New source text must be single-line.');
                const gen = generated(current, [e]).records.filter(r => r.section === 'ENTITIES' && !['SECTION', 'ENDSEC'].includes(r.type));
                if (gen.length !== 1 || !geometryCodes[gen[0].type]) fail('The added object needs compound DXF records.');
                if (Number(source.version.slice(2)) < 1012 && ['ELLIPSE', 'SPLINE', 'LWPOLYLINE'].includes(gen[0].type)) fail('The original DXF version cannot contain this modern entity type.');
                for (const g of gen[0].groups) {
                    if (g.code === 100 && !owner) continue;
                    if (g.code === 330) { if (owner) extras.push(encodePair(330, owner, source)); continue; }
                    if (g.code === 5) { extras.push(encodePair(5, nextHandle(), source)); continue; }
                    if ([420, 370].includes(g.code) && Number(source.version.slice(2)) < (g.code === 420 ? 1018 : 1015)) fail('The original DXF version cannot store this added appearance.');
                    extras.push(encodePair(g.code, (source.outputEncoding || source.encoding) === 'utf-8' && valueType(g.code) === 'string' ? decodedName(g.value) : g.value, source));
                } report.added++;
            }
            insertions.set(end.start, concat(extras));
        }
        const pairChanges = new Map();
        const headerValue = name => { const i = source.groups.findIndex(g => g.code === 9 && g.value.trim() === name); return i < 0 ? null : source.groups[i + 1]; };
        if (report.added) { const seed = headerValue('$HANDSEED'); if (seed) pairChanges.set(seed, encodePair(seed.code, (maxHandle + 1n).toString(16).toUpperCase(), source)); }
        if (current.units !== baseline.units) { const unit = headerValue('$INSUNITS'); if (!unit) fail('The source has no unit header to update.'); pairChanges.set(unit, encodePair(unit.code, X.unitCodes[current.units], source)); }
        // Record and pair replacements share one ordered byte-splice pass. Everything
        // else (including unknown sections, comments, encoding and trailing data) is copied.
        const edits = [...replacements].map(([r, value]) => ({ start: r.start, end: r.end, value }));
        for (const [g, value] of pairChanges) edits.push({ start: g.start, end: g.end, value });
        for (const [start, value] of insertions) edits.push({ start, end: start, value });
        edits.sort((a, b) => a.start - b.start || a.end - b.end);
        const parts = []; let position = 0;
        for (const edit of edits) { if (edit.start < position) throw Error('Overlapping source edits.'); parts.push(source.raw.subarray(position, edit.start), edit.value); position = edit.end; }
        parts.push(source.raw.subarray(position)); report.unchanged = !edits.length;
        if (archive.format === 'dwg') report.warnings.push('This DXF retains the converter output; the original DWG is available separately. It is not a modified DWG round trip.');
        return { bytes: concat(parts), report };
    }
    function sourceInfo(doc) {
        const source = doc.sourceDocument; if (!source) return null; validateArchive(source);
        const original = decode64(source.bytes), dxf = parsedCache.get(source) || parseRaw(decode64(source.convertedDXF || source.bytes), source.suppliedAsText);
        parsedCache.set(source, dxf);
        const counts = {}; for (const r of dxf.records) if (r.section === 'ENTITIES' && !['SECTION', 'ENDSEC'].includes(r.type)) counts[r.type] = (counts[r.type] || 0) + 1;
        return { format: source.format, name: source.name, originalBytes: original.length, encoding: dxf.encoding, binary: dxf.binary, version: dxf.version, records: dxf.records.length, entityRecords: counts,
            unchanged: eq(semantic(doc.serialize({ includeSource: false })), JSON.parse(source.baseline)) };
    }
    K.SourceDocument = { MAX_BYTES, bytes, encode64, decode64, parseRaw, canonical, valueType, encodePair, toBinary,
        validate: validateArchive, parse, withOriginalDWG, exportPreserved, info: sourceInfo, original: doc => { if (!doc.sourceDocument) throw Error('No original source archive.'); validateArchive(doc.sourceDocument); return decode64(doc.sourceDocument.bytes); } };
    X.parseDXF = parse;
})(typeof window !== 'undefined' ? window : globalThis);
