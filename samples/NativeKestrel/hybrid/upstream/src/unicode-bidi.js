/* MIT: bidi-shaper contributors. See third_party/bidi-shaper/LICENSE and manifest.json. */
(function(root){"use strict";const modules={
"api/render":function(exports,require){
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.render = render;
exports.analyze = analyze;
exports.shape = shape;
exports.reorder = reorder;
exports.getEmbeddingLevels = getEmbeddingLevels;
exports.detectDirection = detectDirection;
const algorithm_1 = require("../bidi/algorithm");
const shape_1 = require("../shape/shape");
function isPlainLtr(cps, direction) {
    if (direction === 'rtl')
        return false;
    for (let i = 0; i < cps.length; i++) {
        if (cps[i] >= 0x0590)
            return false;
    }
    return true;
}
function render(text, options) {
    const direction = options?.direction ?? 'auto';
    const cps = (0, algorithm_1.toCodePoints)(text);
    if (isPlainLtr(cps, direction))
        return text;
    const logical = options?.shape !== false ? (0, shape_1.shapeCodePoints)(cps, options).codePoints : cps;
    const { codePoints } = (0, algorithm_1.runBidi)(logical, direction, {
        mirror: options?.mirror !== false,
        singleParagraph: options?.paragraphs === 'single',
    });
    let out = '';
    for (const cp of codePoints)
        out += String.fromCodePoint(cp);
    return out;
}
function analyze(text, options) {
    const direction = options?.direction ?? 'auto';
    const cps = (0, algorithm_1.toCodePoints)(text);
    if (isPlainLtr(cps, direction)) {
        const n = cps.length;
        const visualToLogical = new Array(n);
        const logicalToVisual = new Int32Array(n);
        for (let i = 0; i < n; i++) {
            visualToLogical[i] = i;
            logicalToVisual[i] = i;
        }
        return { text, direction: 'ltr', levels: new Uint8Array(n), visualToLogical, logicalToVisual };
    }
    const shaped = options?.shape !== false
        ? (0, shape_1.shapeCodePoints)(cps, options)
        : { codePoints: cps, src: cps.map((_, i) => i) };
    const bidi = (0, algorithm_1.runBidi)(shaped.codePoints, direction, {
        mirror: options?.mirror !== false,
        singleParagraph: options?.paragraphs === 'single',
    });
    let out = '';
    for (const cp of bidi.codePoints)
        out += String.fromCodePoint(cp);
    const levels = new Uint8Array(cps.length);
    const written = new Uint8Array(cps.length);
    for (let j = 0; j < shaped.codePoints.length; j++) {
        levels[shaped.src[j]] = bidi.levels[j];
        written[shaped.src[j]] = 1;
    }
    let lastLevel = 0;
    for (let i = 0; i < cps.length; i++) {
        if (written[i])
            lastLevel = levels[i];
        else
            levels[i] = lastLevel;
    }
    const visualToLogical = bidi.map.map((j) => shaped.src[j]);
    const logicalToVisual = new Int32Array(cps.length).fill(-1);
    for (let v = 0; v < visualToLogical.length; v++)
        logicalToVisual[visualToLogical[v]] = v;
    return { text: out, direction: bidi.direction, levels, visualToLogical, logicalToVisual };
}
function shape(text, options) {
    return (0, shape_1.shapeString)(text, options);
}
function reorder(text, options) {
    return render(text, { ...options, shape: false });
}
function getEmbeddingLevels(text, options) {
    const cps = (0, algorithm_1.toCodePoints)(text);
    if (isPlainLtr(cps, options?.direction ?? 'auto'))
        return new Uint8Array(cps.length);
    return (0, algorithm_1.runBidi)(cps, options?.direction ?? 'auto', {
        singleParagraph: options?.paragraphs === 'single',
    }).levels;
}
function detectDirection(text) {
    return (0, algorithm_1.detectBaseDirection)((0, algorithm_1.toCodePoints)(text));
}
},
"bidi/algorithm":function(exports,require){
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.classify = classify;
exports.toCodePoints = toCodePoints;
exports.runBidi = runBidi;
exports.detectBaseDirection = detectBaseDirection;
exports.resolveByTypes = resolveByTypes;
exports.baseLevelForTypes = baseLevelForTypes;
const types_1 = require("./types");
const levels_1 = require("./levels");
const reorder_1 = require("./reorder");
const lookup_1 = require("../data/lookup");
function classify(codePoints) {
    const types = new Uint8Array(codePoints.length);
    for (let i = 0; i < codePoints.length; i++)
        types[i] = (0, lookup_1.getBidiClass)(codePoints[i]);
    return types;
}
function toCodePoints(text) {
    const out = [];
    for (const ch of text)
        out.push(ch.codePointAt(0));
    return out;
}
function splitParagraphs(types) {
    const segs = [];
    let start = 0;
    for (let i = 0; i < types.length; i++) {
        if (types[i] === types_1.BC.B) {
            segs.push([start, i + 1]);
            start = i + 1;
        }
    }
    if (start < types.length || segs.length === 0)
        segs.push([start, types.length]);
    return segs;
}
function paragraphLevelFor(types, start, end, base) {
    if (base === 'ltr')
        return 0;
    if (base === 'rtl')
        return 1;
    return (0, levels_1.computeBaseLevel)(types, start, end);
}
function runBidi(codePoints, base, options) {
    const mirror = options?.mirror !== false;
    const types = classify(codePoints);
    const all = Int32Array.from(codePoints);
    const n = all.length;
    const segments = options?.singleParagraph ? [[0, n]] : splitParagraphs(types);
    const visual = [];
    const map = [];
    const allLevels = new Uint8Array(n);
    let direction = null;
    for (const [s, e] of segments) {
        if (s === e)
            continue;
        const hasSep = !options?.singleParagraph && types[e - 1] === types_1.BC.B;
        const contentEnd = hasSep ? e - 1 : e;
        const paraLevel = paragraphLevelFor(types, s, e, base);
        if (direction === null)
            direction = paraLevel === 1 ? 'rtl' : 'ltr';
        if (s < contentEnd) {
            const segTypes = types.subarray(s, contentEnd);
            const segCps = all.subarray(s, contentEnd);
            const { levels, removed } = (0, levels_1.resolveParagraph)(segTypes, segCps, paraLevel);
            (0, reorder_1.applyL1)(segTypes, levels, removed, paraLevel);
            allLevels.set(levels, s);
            for (const idx of (0, reorder_1.reorderIndices)(levels, removed)) {
                let cp = segCps[idx];
                if (mirror && (levels[idx] & 1) === 1) {
                    const m = (0, lookup_1.getMirror)(cp);
                    if (m !== 0)
                        cp = m;
                }
                visual.push(cp);
                map.push(s + idx);
            }
        }
        if (hasSep) {
            allLevels[e - 1] = paraLevel;
            visual.push(all[e - 1]);
            map.push(e - 1);
        }
    }
    return {
        codePoints: visual,
        map,
        levels: allLevels,
        direction: direction ?? (base === 'rtl' ? 'rtl' : 'ltr'),
    };
}
function detectBaseDirection(codePoints) {
    const types = classify(codePoints);
    let isolateDepth = 0;
    for (let i = 0; i < types.length; i++) {
        const t = types[i];
        if (t === types_1.BC.LRI || t === types_1.BC.RLI || t === types_1.BC.FSI) {
            isolateDepth++;
        }
        else if (t === types_1.BC.PDI) {
            if (isolateDepth > 0)
                isolateDepth--;
        }
        else if (isolateDepth === 0) {
            if (t === types_1.BC.L)
                return 'ltr';
            if (t === types_1.BC.R || t === types_1.BC.AL)
                return 'rtl';
        }
    }
    return 'neutral';
}
function resolveByTypes(types, paraLevel) {
    const { levels, removed } = (0, levels_1.resolveParagraph)(types, null, paraLevel);
    (0, reorder_1.applyL1)(types, levels, removed, paraLevel);
    const order = (0, reorder_1.reorderIndices)(levels, removed);
    return { levels, removed, order };
}
function baseLevelForTypes(types) {
    return (0, levels_1.computeBaseLevel)(types, 0, types.length);
}
},
"bidi/levels":function(exports,require){
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.computeBaseLevel = computeBaseLevel;
exports.resolveParagraph = resolveParagraph;
const types_1 = require("./types");
const lookup_1 = require("../data/lookup");
function leastGreaterOdd(level) {
    return (level & 1) === 0 ? level + 1 : level + 2;
}
function leastGreaterEven(level) {
    return (level & 1) === 0 ? level + 2 : level + 1;
}
function computeBaseLevel(types, start, end) {
    let isolateDepth = 0;
    for (let i = start; i < end; i++) {
        const t = types[i];
        if (t === types_1.BC.LRI || t === types_1.BC.RLI || t === types_1.BC.FSI) {
            isolateDepth++;
        }
        else if (t === types_1.BC.PDI) {
            if (isolateDepth > 0)
                isolateDepth--;
        }
        else if (isolateDepth === 0) {
            if (t === types_1.BC.L)
                return 0;
            if (t === types_1.BC.R || t === types_1.BC.AL)
                return 1;
        }
    }
    return 0;
}
function computeMatching(types) {
    const n = types.length;
    const matchPDI = new Int32Array(n).fill(-1);
    const matchInit = new Int32Array(n).fill(-1);
    const stack = [];
    for (let i = 0; i < n; i++) {
        const t = types[i];
        if (t === types_1.BC.LRI || t === types_1.BC.RLI || t === types_1.BC.FSI) {
            stack.push(i);
        }
        else if (t === types_1.BC.PDI && stack.length > 0) {
            const open = stack.pop();
            matchPDI[open] = i;
            matchInit[i] = open;
        }
    }
    return { matchPDI, matchInit };
}
const NEUTRAL = -1;
const NO_DIR = -1;
function dirOf(t) {
    if (t === types_1.BC.L)
        return types_1.BC.L;
    if (t === types_1.BC.R || t === types_1.BC.EN || t === types_1.BC.AN)
        return types_1.BC.R;
    return NO_DIR;
}
function isNI(t) {
    return (t === types_1.BC.B ||
        t === types_1.BC.S ||
        t === types_1.BC.WS ||
        t === types_1.BC.ON ||
        t === types_1.BC.FSI ||
        t === types_1.BC.LRI ||
        t === types_1.BC.RLI ||
        t === types_1.BC.PDI);
}
function resolveParagraph(origTypes, codePoints, paraLevel) {
    const n = origTypes.length;
    const levels = new Uint8Array(n);
    const types = Uint8Array.from(origTypes);
    const removed = new Uint8Array(n);
    if (n === 0)
        return { levels, removed };
    const { matchPDI } = computeMatching(origTypes);
    const stack = [{ level: paraLevel, override: NEUTRAL, isolate: false }];
    let overflowIsolate = 0;
    let overflowEmbedding = 0;
    let validIsolate = 0;
    const top = () => stack[stack.length - 1];
    for (let i = 0; i < n; i++) {
        const t = origTypes[i];
        switch (t) {
            case types_1.BC.RLE:
            case types_1.BC.LRE:
            case types_1.BC.RLO:
            case types_1.BC.LRO: {
                levels[i] = top().level;
                removed[i] = 1;
                const isRTL = t === types_1.BC.RLE || t === types_1.BC.RLO;
                const newLevel = isRTL ? leastGreaterOdd(top().level) : leastGreaterEven(top().level);
                const override = t === types_1.BC.RLO ? types_1.BC.R : t === types_1.BC.LRO ? types_1.BC.L : NEUTRAL;
                if (newLevel <= types_1.MAX_DEPTH && overflowIsolate === 0 && overflowEmbedding === 0) {
                    stack.push({ level: newLevel, override, isolate: false });
                }
                else if (overflowIsolate === 0) {
                    overflowEmbedding++;
                }
                break;
            }
            case types_1.BC.RLI:
            case types_1.BC.LRI:
            case types_1.BC.FSI: {
                levels[i] = top().level;
                if (top().override !== NEUTRAL)
                    types[i] = top().override;
                let isRTL;
                if (t === types_1.BC.FSI) {
                    const innerEnd = matchPDI[i] === -1 ? n : matchPDI[i];
                    isRTL = computeBaseLevel(origTypes, i + 1, innerEnd) === 1;
                }
                else {
                    isRTL = t === types_1.BC.RLI;
                }
                const newLevel = isRTL ? leastGreaterOdd(top().level) : leastGreaterEven(top().level);
                if (newLevel <= types_1.MAX_DEPTH && overflowIsolate === 0 && overflowEmbedding === 0) {
                    validIsolate++;
                    stack.push({ level: newLevel, override: NEUTRAL, isolate: true });
                }
                else {
                    overflowIsolate++;
                }
                break;
            }
            case types_1.BC.PDI: {
                if (overflowIsolate > 0) {
                    overflowIsolate--;
                }
                else if (validIsolate > 0) {
                    overflowEmbedding = 0;
                    while (!top().isolate)
                        stack.pop();
                    stack.pop();
                    validIsolate--;
                }
                levels[i] = top().level;
                if (top().override !== NEUTRAL)
                    types[i] = top().override;
                break;
            }
            case types_1.BC.PDF: {
                levels[i] = top().level;
                removed[i] = 1;
                if (overflowIsolate > 0) {
                }
                else if (overflowEmbedding > 0) {
                    overflowEmbedding--;
                }
                else if (!top().isolate && stack.length >= 2) {
                    stack.pop();
                }
                levels[i] = top().level;
                break;
            }
            case types_1.BC.B: {
                levels[i] = paraLevel;
                break;
            }
            case types_1.BC.BN: {
                levels[i] = top().level;
                removed[i] = 1;
                break;
            }
            default: {
                levels[i] = top().level;
                if (top().override !== NEUTRAL)
                    types[i] = top().override;
                break;
            }
        }
    }
    const sequences = buildIsolatingRunSequences(origTypes, levels, removed, matchPDI);
    const embLevels = Uint8Array.from(levels);
    for (const seq of sequences) {
        resolveSequence(seq, origTypes, types, levels, embLevels, removed, paraLevel, matchPDI, codePoints);
    }
    return { levels, removed };
}
function buildIsolatingRunSequences(origTypes, levels, removed, matchPDI) {
    const n = origTypes.length;
    const runs = [];
    let cur = [];
    for (let i = 0; i < n; i++) {
        if (removed[i])
            continue;
        if (cur.length === 0 || levels[i] === levels[cur[cur.length - 1]]) {
            cur.push(i);
        }
        else {
            runs.push(cur);
            cur = [i];
        }
    }
    if (cur.length)
        runs.push(cur);
    const runStartingAt = new Map();
    runs.forEach((r, ri) => runStartingAt.set(r[0], ri));
    const sequences = [];
    for (let ri = 0; ri < runs.length; ri++) {
        const first = runs[ri][0];
        if (origTypes[first] === types_1.BC.PDI && matchedInitiatorExists(origTypes, levels, removed, first)) {
            continue;
        }
        let seq = [];
        let curRi = ri;
        while (curRi !== undefined) {
            const run = runs[curRi];
            seq = seq.concat(run);
            const last = run[run.length - 1];
            const lt = origTypes[last];
            if ((lt === types_1.BC.LRI || lt === types_1.BC.RLI || lt === types_1.BC.FSI) && matchPDI[last] >= 0) {
                curRi = runStartingAt.get(matchPDI[last]);
            }
            else {
                curRi = undefined;
            }
        }
        sequences.push(seq);
    }
    return sequences;
}
function matchedInitiatorExists(types, _levels, _removed, pos) {
    let depth = 0;
    let lastInitiator = -1;
    for (let i = 0; i < pos; i++) {
        const t = types[i];
        if (t === types_1.BC.LRI || t === types_1.BC.RLI || t === types_1.BC.FSI) {
            depth++;
            lastInitiator = i;
        }
        else if (t === types_1.BC.PDI && depth > 0) {
            depth--;
        }
    }
    return depth > 0 && lastInitiator >= 0;
}
function resolveSequence(seq, origTypes, types, levels, embLevels, removed, paraLevel, matchPDI, codePoints) {
    const len = seq.length;
    if (len === 0)
        return;
    const n = origTypes.length;
    const seqLevel = embLevels[seq[0]];
    const e = (seqLevel & 1) === 1 ? types_1.BC.R : types_1.BC.L;
    const o = e === types_1.BC.L ? types_1.BC.R : types_1.BC.L;
    let prevLevel = paraLevel;
    for (let i = seq[0] - 1; i >= 0; i--) {
        if (!removed[i]) {
            prevLevel = embLevels[i];
            break;
        }
    }
    const sos = (Math.max(seqLevel, prevLevel) & 1) === 1 ? types_1.BC.R : types_1.BC.L;
    const lastIdx = seq[len - 1];
    const lastType = origTypes[lastIdx];
    let eos;
    if ((lastType === types_1.BC.LRI || lastType === types_1.BC.RLI || lastType === types_1.BC.FSI) && matchPDI[lastIdx] === -1) {
        eos = (Math.max(seqLevel, paraLevel) & 1) === 1 ? types_1.BC.R : types_1.BC.L;
    }
    else {
        let nextLevel = paraLevel;
        for (let i = lastIdx + 1; i < n; i++) {
            if (!removed[i]) {
                nextLevel = embLevels[i];
                break;
            }
        }
        eos = (Math.max(seqLevel, nextLevel) & 1) === 1 ? types_1.BC.R : types_1.BC.L;
    }
    const at = (k) => types[seq[k]];
    const set = (k, v) => {
        types[seq[k]] = v;
    };
    let prevType = sos;
    for (let k = 0; k < len; k++) {
        const t = at(k);
        if (t === types_1.BC.NSM) {
            const resolved = prevType === types_1.BC.LRI || prevType === types_1.BC.RLI || prevType === types_1.BC.FSI || prevType === types_1.BC.PDI
                ? types_1.BC.ON
                : prevType;
            set(k, resolved);
            prevType = resolved;
        }
        else {
            prevType = t;
        }
    }
    let strong = sos;
    for (let k = 0; k < len; k++) {
        const t = at(k);
        if (t === types_1.BC.EN && strong === types_1.BC.AL)
            set(k, types_1.BC.AN);
        if (t === types_1.BC.L || t === types_1.BC.R || t === types_1.BC.AL)
            strong = t;
    }
    for (let k = 0; k < len; k++) {
        if (at(k) === types_1.BC.AL)
            set(k, types_1.BC.R);
    }
    const snapshot = new Int32Array(len);
    for (let k = 0; k < len; k++)
        snapshot[k] = at(k);
    for (let k = 1; k < len - 1; k++) {
        const t = snapshot[k];
        const prev = snapshot[k - 1];
        const next = snapshot[k + 1];
        if (t === types_1.BC.ES && prev === types_1.BC.EN && next === types_1.BC.EN)
            set(k, types_1.BC.EN);
        else if (t === types_1.BC.CS && prev === types_1.BC.EN && next === types_1.BC.EN)
            set(k, types_1.BC.EN);
        else if (t === types_1.BC.CS && prev === types_1.BC.AN && next === types_1.BC.AN)
            set(k, types_1.BC.AN);
    }
    for (let k = 0; k < len;) {
        if (at(k) === types_1.BC.ET) {
            let j = k;
            while (j < len && at(j) === types_1.BC.ET)
                j++;
            const before = k > 0 ? at(k - 1) : sos;
            const after = j < len ? at(j) : eos;
            if (before === types_1.BC.EN || after === types_1.BC.EN) {
                for (let m = k; m < j; m++)
                    set(m, types_1.BC.EN);
            }
            k = j;
        }
        else {
            k++;
        }
    }
    for (let k = 0; k < len; k++) {
        const t = at(k);
        if (t === types_1.BC.ES || t === types_1.BC.ET || t === types_1.BC.CS)
            set(k, types_1.BC.ON);
    }
    strong = sos;
    for (let k = 0; k < len; k++) {
        const t = at(k);
        if (t === types_1.BC.EN && strong === types_1.BC.L)
            set(k, types_1.BC.L);
        if (t === types_1.BC.L || t === types_1.BC.R)
            strong = t;
    }
    if (codePoints) {
        resolveBrackets(seq, origTypes, types, codePoints, e, o, sos);
    }
    for (let k = 0; k < len;) {
        if (isNI(at(k))) {
            let j = k;
            while (j < len && isNI(at(j)))
                j++;
            const before = k > 0 ? dirOf(at(k - 1)) : sos;
            const after = j < len ? dirOf(at(j)) : eos;
            if (before === after && (before === types_1.BC.L || before === types_1.BC.R)) {
                for (let m = k; m < j; m++)
                    set(m, before);
            }
            k = j;
        }
        else {
            k++;
        }
    }
    for (let k = 0; k < len; k++) {
        if (isNI(at(k)))
            set(k, e);
    }
    for (let k = 0; k < len; k++) {
        const idx = seq[k];
        const lvl = embLevels[idx];
        const t = types[idx];
        if ((lvl & 1) === 0) {
            if (t === types_1.BC.R)
                levels[idx] = lvl + 1;
            else if (t === types_1.BC.AN || t === types_1.BC.EN)
                levels[idx] = lvl + 2;
        }
        else if (t === types_1.BC.L || t === types_1.BC.EN || t === types_1.BC.AN) {
            levels[idx] = lvl + 1;
        }
    }
}
function resolveBrackets(seq, origTypes, types, codePoints, e, o, sos) {
    const len = seq.length;
    const at = (k) => types[seq[k]];
    const openStack = [];
    const pairs = [];
    for (let k = 0; k < len; k++) {
        const idx = seq[k];
        if (types[idx] !== types_1.BC.ON)
            continue;
        const cp = codePoints[idx];
        const bt = (0, lookup_1.getBracketType)(cp);
        if (bt === 0) {
            if (openStack.length === 63)
                break;
            openStack.push({ canon: (0, lookup_1.canonicalBracket)((0, lookup_1.getBracketPair)(cp)), k });
        }
        else if (bt === 1) {
            const cc = (0, lookup_1.canonicalBracket)(cp);
            for (let s = openStack.length - 1; s >= 0; s--) {
                if (openStack[s].canon === cc) {
                    pairs.push([openStack[s].k, k]);
                    openStack.length = s;
                    break;
                }
            }
        }
    }
    pairs.sort((a, b) => a[0] - b[0]);
    const setBracketNSM = (k, dir) => {
        for (let m = k + 1; m < len && origTypes[seq[m]] === types_1.BC.NSM; m++) {
            types[seq[m]] = dir;
        }
    };
    for (const [openK, closeK] of pairs) {
        let foundE = false;
        let foundO = false;
        for (let m = openK + 1; m < closeK; m++) {
            const d = dirOf(at(m));
            if (d === e) {
                foundE = true;
                break;
            }
            if (d === o)
                foundO = true;
        }
        let dir = NO_DIR;
        if (foundE) {
            dir = e;
        }
        else if (foundO) {
            let ctx = sos;
            for (let m = openK - 1; m >= 0; m--) {
                const d = dirOf(at(m));
                if (d !== NO_DIR) {
                    ctx = d;
                    break;
                }
            }
            dir = ctx === o ? o : e;
        }
        if (dir !== NO_DIR) {
            types[seq[openK]] = dir;
            types[seq[closeK]] = dir;
            setBracketNSM(openK, dir);
            setBracketNSM(closeK, dir);
        }
    }
}
},
"bidi/reorder":function(exports,require){
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.applyL1 = applyL1;
exports.reorderIndices = reorderIndices;
const types_1 = require("./types");
function isResettable(origTypes, removed, i) {
    if (removed[i])
        return true;
    const t = origTypes[i];
    return t === types_1.BC.WS || t === types_1.BC.LRI || t === types_1.BC.RLI || t === types_1.BC.FSI || t === types_1.BC.PDI;
}
function applyL1(origTypes, levels, removed, paraLevel) {
    const n = origTypes.length;
    let i = n - 1;
    while (i >= 0 && isResettable(origTypes, removed, i)) {
        levels[i] = paraLevel;
        i--;
    }
    for (let j = 0; j < n; j++) {
        const t = origTypes[j];
        if (t === types_1.BC.B || t === types_1.BC.S) {
            levels[j] = paraLevel;
            let k = j - 1;
            while (k >= 0 && isResettable(origTypes, removed, k)) {
                levels[k] = paraLevel;
                k--;
            }
        }
    }
}
function reverseRange(arr, from, to) {
    while (from < to) {
        const tmp = arr[from];
        arr[from] = arr[to];
        arr[to] = tmp;
        from++;
        to--;
    }
}
function reorderIndices(levels, removed) {
    const order = [];
    for (let i = 0; i < levels.length; i++) {
        if (!removed[i])
            order.push(i);
    }
    if (order.length === 0)
        return order;
    let maxLevel = 0;
    let minOdd = Number.MAX_SAFE_INTEGER;
    for (const i of order) {
        const l = levels[i];
        if (l > maxLevel)
            maxLevel = l;
        if ((l & 1) === 1 && l < minOdd)
            minOdd = l;
    }
    if (minOdd === Number.MAX_SAFE_INTEGER)
        return order;
    for (let level = maxLevel; level >= minOdd; level--) {
        let s = 0;
        while (s < order.length) {
            if (levels[order[s]] >= level) {
                let eIdx = s;
                while (eIdx < order.length && levels[order[eIdx]] >= level)
                    eIdx++;
                reverseRange(order, s, eIdx - 1);
                s = eIdx;
            }
            else {
                s++;
            }
        }
    }
    return order;
}
},
"bidi/types":function(exports,require){
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.MAX_DEPTH = exports.JT = exports.BC = void 0;
exports.BC = {
    L: 0,
    R: 1,
    AL: 2,
    EN: 3,
    ES: 4,
    ET: 5,
    AN: 6,
    CS: 7,
    NSM: 8,
    BN: 9,
    B: 10,
    S: 11,
    WS: 12,
    ON: 13,
    LRE: 14,
    LRO: 15,
    RLE: 16,
    RLO: 17,
    PDF: 18,
    LRI: 19,
    RLI: 20,
    FSI: 21,
    PDI: 22,
};
exports.JT = {
    U: 0,
    C: 1,
    D: 2,
    L: 3,
    R: 4,
    T: 5,
};
exports.MAX_DEPTH = 125;
},
"data/generated/bidi-classes":function(exports,require){
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.BIDI_CLASS_RANGES = exports.BIDI_CLASS_NAMES = void 0;
exports.BIDI_CLASS_NAMES = ['L', 'R', 'AL', 'EN', 'ES', 'ET', 'AN', 'CS', 'NSM', 'BN', 'B', 'S', 'WS', 'ON', 'LRE', 'LRO', 'RLE', 'RLO', 'PDF', 'LRI', 'RLI', 'FSI', 'PDI'];
exports.BIDI_CLASS_RANGES = [0, 8, 9, 9, 9, 11, 10, 10, 10, 11, 11, 11, 12, 12, 12, 13, 13, 10, 14, 27, 9, 28, 30, 10, 31, 31, 11, 32, 32, 12, 33, 34, 13, 35, 37, 5, 38, 42, 13, 43, 43, 4, 44, 44, 7, 45, 45, 4, 46, 47, 7, 48, 57, 3, 58, 58, 7, 59, 64, 13, 91, 96, 13, 123, 126, 13, 127, 132, 9, 133, 133, 10, 134, 159, 9, 160, 160, 7, 161, 161, 13, 162, 165, 5, 166, 169, 13, 171, 172, 13, 173, 173, 9, 174, 175, 13, 176, 177, 5, 178, 179, 3, 180, 180, 13, 182, 184, 13, 185, 185, 3, 187, 191, 13, 215, 215, 13, 247, 247, 13, 697, 698, 13, 706, 719, 13, 722, 735, 13, 741, 749, 13, 751, 767, 13, 768, 879, 8, 884, 885, 13, 894, 894, 13, 900, 901, 13, 903, 903, 13, 1014, 1014, 13, 1155, 1161, 8, 1418, 1418, 13, 1421, 1422, 13, 1423, 1423, 5, 1424, 1424, 1, 1425, 1469, 8, 1470, 1470, 1, 1471, 1471, 8, 1472, 1472, 1, 1473, 1474, 8, 1475, 1475, 1, 1476, 1477, 8, 1478, 1478, 1, 1479, 1479, 8, 1480, 1535, 1, 1536, 1541, 6, 1542, 1543, 13, 1544, 1544, 2, 1545, 1546, 5, 1547, 1547, 2, 1548, 1548, 7, 1549, 1549, 2, 1550, 1551, 13, 1552, 1562, 8, 1563, 1610, 2, 1611, 1631, 8, 1632, 1641, 6, 1642, 1642, 5, 1643, 1644, 6, 1645, 1647, 2, 1648, 1648, 8, 1649, 1749, 2, 1750, 1756, 8, 1757, 1757, 6, 1758, 1758, 13, 1759, 1764, 8, 1765, 1766, 2, 1767, 1768, 8, 1769, 1769, 13, 1770, 1773, 8, 1774, 1775, 2, 1776, 1785, 3, 1786, 1808, 2, 1809, 1809, 8, 1810, 1839, 2, 1840, 1866, 8, 1867, 1957, 2, 1958, 1968, 8, 1969, 1983, 2, 1984, 2026, 1, 2027, 2035, 8, 2036, 2037, 1, 2038, 2041, 13, 2042, 2044, 1, 2045, 2045, 8, 2046, 2069, 1, 2070, 2073, 8, 2074, 2074, 1, 2075, 2083, 8, 2084, 2084, 1, 2085, 2087, 8, 2088, 2088, 1, 2089, 2093, 8, 2094, 2136, 1, 2137, 2139, 8, 2140, 2143, 1, 2144, 2191, 2, 2192, 2193, 6, 2194, 2198, 2, 2199, 2207, 8, 2208, 2249, 2, 2250, 2273, 8, 2274, 2274, 6, 2275, 2306, 8, 2362, 2362, 8, 2364, 2364, 8, 2369, 2376, 8, 2381, 2381, 8, 2385, 2391, 8, 2402, 2403, 8, 2433, 2433, 8, 2492, 2492, 8, 2497, 2500, 8, 2509, 2509, 8, 2530, 2531, 8, 2546, 2547, 5, 2555, 2555, 5, 2558, 2558, 8, 2561, 2562, 8, 2620, 2620, 8, 2625, 2626, 8, 2631, 2632, 8, 2635, 2637, 8, 2641, 2641, 8, 2672, 2673, 8, 2677, 2677, 8, 2689, 2690, 8, 2748, 2748, 8, 2753, 2757, 8, 2759, 2760, 8, 2765, 2765, 8, 2786, 2787, 8, 2801, 2801, 5, 2810, 2815, 8, 2817, 2817, 8, 2876, 2876, 8, 2879, 2879, 8, 2881, 2884, 8, 2893, 2893, 8, 2901, 2902, 8, 2914, 2915, 8, 2946, 2946, 8, 3008, 3008, 8, 3021, 3021, 8, 3059, 3064, 13, 3065, 3065, 5, 3066, 3066, 13, 3072, 3072, 8, 3076, 3076, 8, 3132, 3132, 8, 3134, 3136, 8, 3142, 3144, 8, 3146, 3149, 8, 3157, 3158, 8, 3170, 3171, 8, 3192, 3198, 13, 3201, 3201, 8, 3260, 3260, 8, 3276, 3277, 8, 3298, 3299, 8, 3328, 3329, 8, 3387, 3388, 8, 3393, 3396, 8, 3405, 3405, 8, 3426, 3427, 8, 3457, 3457, 8, 3530, 3530, 8, 3538, 3540, 8, 3542, 3542, 8, 3633, 3633, 8, 3636, 3642, 8, 3647, 3647, 5, 3655, 3662, 8, 3761, 3761, 8, 3764, 3772, 8, 3784, 3790, 8, 3864, 3865, 8, 3893, 3893, 8, 3895, 3895, 8, 3897, 3897, 8, 3898, 3901, 13, 3953, 3966, 8, 3968, 3972, 8, 3974, 3975, 8, 3981, 3991, 8, 3993, 4028, 8, 4038, 4038, 8, 4141, 4144, 8, 4146, 4151, 8, 4153, 4154, 8, 4157, 4158, 8, 4184, 4185, 8, 4190, 4192, 8, 4209, 4212, 8, 4226, 4226, 8, 4229, 4230, 8, 4237, 4237, 8, 4253, 4253, 8, 4957, 4959, 8, 5008, 5017, 13, 5120, 5120, 13, 5760, 5760, 12, 5787, 5788, 13, 5906, 5908, 8, 5938, 5939, 8, 5970, 5971, 8, 6002, 6003, 8, 6068, 6069, 8, 6071, 6077, 8, 6086, 6086, 8, 6089, 6099, 8, 6107, 6107, 5, 6109, 6109, 8, 6128, 6137, 13, 6144, 6154, 13, 6155, 6157, 8, 6158, 6158, 9, 6159, 6159, 8, 6277, 6278, 8, 6313, 6313, 8, 6432, 6434, 8, 6439, 6440, 8, 6450, 6450, 8, 6457, 6459, 8, 6464, 6464, 13, 6468, 6469, 13, 6622, 6655, 13, 6679, 6680, 8, 6683, 6683, 8, 6742, 6742, 8, 6744, 6750, 8, 6752, 6752, 8, 6754, 6754, 8, 6757, 6764, 8, 6771, 6780, 8, 6783, 6783, 8, 6832, 6877, 8, 6880, 6891, 8, 6912, 6915, 8, 6964, 6964, 8, 6966, 6970, 8, 6972, 6972, 8, 6978, 6978, 8, 7019, 7027, 8, 7040, 7041, 8, 7074, 7077, 8, 7080, 7081, 8, 7083, 7085, 8, 7142, 7142, 8, 7144, 7145, 8, 7149, 7149, 8, 7151, 7153, 8, 7212, 7219, 8, 7222, 7223, 8, 7376, 7378, 8, 7380, 7392, 8, 7394, 7400, 8, 7405, 7405, 8, 7412, 7412, 8, 7416, 7417, 8, 7616, 7679, 8, 8125, 8125, 13, 8127, 8129, 13, 8141, 8143, 13, 8157, 8159, 13, 8173, 8175, 13, 8189, 8190, 13, 8192, 8202, 12, 8203, 8205, 9, 8207, 8207, 1, 8208, 8231, 13, 8232, 8232, 12, 8233, 8233, 10, 8234, 8234, 14, 8235, 8235, 16, 8236, 8236, 18, 8237, 8237, 15, 8238, 8238, 17, 8239, 8239, 7, 8240, 8244, 5, 8245, 8259, 13, 8260, 8260, 7, 8261, 8286, 13, 8287, 8287, 12, 8288, 8293, 9, 8294, 8294, 19, 8295, 8295, 20, 8296, 8296, 21, 8297, 8297, 22, 8298, 8303, 9, 8304, 8304, 3, 8308, 8313, 3, 8314, 8315, 4, 8316, 8318, 13, 8320, 8329, 3, 8330, 8331, 4, 8332, 8334, 13, 8352, 8399, 5, 8400, 8432, 8, 8448, 8449, 13, 8451, 8454, 13, 8456, 8457, 13, 8468, 8468, 13, 8470, 8472, 13, 8478, 8483, 13, 8485, 8485, 13, 8487, 8487, 13, 8489, 8489, 13, 8494, 8494, 5, 8506, 8507, 13, 8512, 8516, 13, 8522, 8525, 13, 8528, 8543, 13, 8585, 8587, 13, 8592, 8721, 13, 8722, 8722, 4, 8723, 8723, 5, 8724, 9013, 13, 9083, 9108, 13, 9110, 9257, 13, 9280, 9290, 13, 9312, 9351, 13, 9352, 9371, 3, 9450, 9899, 13, 9901, 10239, 13, 10496, 11123, 13, 11126, 11263, 13, 11493, 11498, 13, 11503, 11505, 8, 11513, 11519, 13, 11647, 11647, 8, 11744, 11775, 8, 11776, 11869, 13, 11904, 11929, 13, 11931, 12019, 13, 12032, 12245, 13, 12272, 12287, 13, 12288, 12288, 12, 12289, 12292, 13, 12296, 12320, 13, 12330, 12333, 8, 12336, 12336, 13, 12342, 12343, 13, 12349, 12351, 13, 12441, 12442, 8, 12443, 12444, 13, 12448, 12448, 13, 12539, 12539, 13, 12736, 12773, 13, 12783, 12783, 13, 12829, 12830, 13, 12880, 12895, 13, 12924, 12926, 13, 12977, 12991, 13, 13004, 13007, 13, 13175, 13178, 13, 13278, 13279, 13, 13311, 13311, 13, 19904, 19967, 13, 42128, 42182, 13, 42509, 42511, 13, 42607, 42610, 8, 42611, 42611, 13, 42612, 42621, 8, 42622, 42623, 13, 42654, 42655, 8, 42736, 42737, 8, 42752, 42785, 13, 42888, 42888, 13, 43010, 43010, 8, 43014, 43014, 8, 43019, 43019, 8, 43045, 43046, 8, 43048, 43051, 13, 43052, 43052, 8, 43064, 43065, 5, 43124, 43127, 13, 43204, 43205, 8, 43232, 43249, 8, 43263, 43263, 8, 43302, 43309, 8, 43335, 43345, 8, 43392, 43394, 8, 43443, 43443, 8, 43446, 43449, 8, 43452, 43453, 8, 43493, 43493, 8, 43561, 43566, 8, 43569, 43570, 8, 43573, 43574, 8, 43587, 43587, 8, 43596, 43596, 8, 43644, 43644, 8, 43696, 43696, 8, 43698, 43700, 8, 43703, 43704, 8, 43710, 43711, 8, 43713, 43713, 8, 43756, 43757, 8, 43766, 43766, 8, 43882, 43883, 13, 44005, 44005, 8, 44008, 44008, 8, 44013, 44013, 8, 64285, 64285, 1, 64286, 64286, 8, 64287, 64296, 1, 64297, 64297, 4, 64298, 64335, 1, 64336, 64450, 2, 64451, 64466, 13, 64467, 64829, 2, 64830, 64847, 13, 64848, 64911, 2, 64912, 64913, 13, 64914, 64967, 2, 64968, 64975, 13, 64976, 65007, 9, 65008, 65020, 2, 65021, 65023, 13, 65024, 65039, 8, 65040, 65049, 13, 65056, 65071, 8, 65072, 65103, 13, 65104, 65104, 7, 65105, 65105, 13, 65106, 65106, 7, 65108, 65108, 13, 65109, 65109, 7, 65110, 65118, 13, 65119, 65119, 5, 65120, 65121, 13, 65122, 65123, 4, 65124, 65126, 13, 65128, 65128, 13, 65129, 65130, 5, 65131, 65131, 13, 65136, 65278, 2, 65279, 65279, 9, 65281, 65282, 13, 65283, 65285, 5, 65286, 65290, 13, 65291, 65291, 4, 65292, 65292, 7, 65293, 65293, 4, 65294, 65295, 7, 65296, 65305, 3, 65306, 65306, 7, 65307, 65312, 13, 65339, 65344, 13, 65371, 65381, 13, 65504, 65505, 5, 65506, 65508, 13, 65509, 65510, 5, 65512, 65518, 13, 65520, 65528, 9, 65529, 65533, 13, 65534, 65535, 9, 65793, 65793, 13, 65856, 65932, 13, 65936, 65948, 13, 65952, 65952, 13, 66045, 66045, 8, 66272, 66272, 8, 66273, 66299, 3, 66422, 66426, 8, 67584, 67870, 1, 67871, 67871, 13, 67872, 68096, 1, 68097, 68099, 8, 68100, 68100, 1, 68101, 68102, 8, 68103, 68107, 1, 68108, 68111, 8, 68112, 68151, 1, 68152, 68154, 8, 68155, 68158, 1, 68159, 68159, 8, 68160, 68324, 1, 68325, 68326, 8, 68327, 68408, 1, 68409, 68415, 13, 68416, 68863, 1, 68864, 68899, 2, 68900, 68903, 8, 68904, 68911, 2, 68912, 68921, 6, 68922, 68927, 2, 68928, 68937, 6, 68938, 68968, 1, 68969, 68973, 8, 68974, 68974, 13, 68975, 69215, 1, 69216, 69246, 6, 69247, 69290, 1, 69291, 69292, 8, 69293, 69311, 1, 69312, 69327, 2, 69328, 69336, 13, 69337, 69369, 2, 69370, 69375, 8, 69376, 69423, 1, 69424, 69445, 2, 69446, 69456, 8, 69457, 69487, 2, 69488, 69505, 1, 69506, 69509, 8, 69510, 69631, 1, 69633, 69633, 8, 69688, 69702, 8, 69714, 69733, 13, 69744, 69744, 8, 69747, 69748, 8, 69759, 69761, 8, 69811, 69814, 8, 69817, 69818, 8, 69826, 69826, 8, 69888, 69890, 8, 69927, 69931, 8, 69933, 69940, 8, 70003, 70003, 8, 70016, 70017, 8, 70070, 70078, 8, 70089, 70092, 8, 70095, 70095, 8, 70191, 70193, 8, 70196, 70196, 8, 70198, 70199, 8, 70206, 70206, 8, 70209, 70209, 8, 70367, 70367, 8, 70371, 70378, 8, 70400, 70401, 8, 70459, 70460, 8, 70464, 70464, 8, 70502, 70508, 8, 70512, 70516, 8, 70587, 70592, 8, 70606, 70606, 8, 70608, 70608, 8, 70610, 70610, 8, 70625, 70626, 8, 70712, 70719, 8, 70722, 70724, 8, 70726, 70726, 8, 70750, 70750, 8, 70835, 70840, 8, 70842, 70842, 8, 70847, 70848, 8, 70850, 70851, 8, 71090, 71093, 8, 71100, 71101, 8, 71103, 71104, 8, 71132, 71133, 8, 71219, 71226, 8, 71229, 71229, 8, 71231, 71232, 8, 71264, 71276, 13, 71339, 71339, 8, 71341, 71341, 8, 71344, 71349, 8, 71351, 71351, 8, 71453, 71453, 8, 71455, 71455, 8, 71458, 71461, 8, 71463, 71467, 8, 71727, 71735, 8, 71737, 71738, 8, 71995, 71996, 8, 71998, 71998, 8, 72003, 72003, 8, 72148, 72151, 8, 72154, 72155, 8, 72160, 72160, 8, 72193, 72198, 8, 72201, 72202, 8, 72243, 72248, 8, 72251, 72254, 8, 72263, 72263, 8, 72273, 72278, 8, 72281, 72283, 8, 72330, 72342, 8, 72344, 72345, 8, 72544, 72544, 8, 72546, 72548, 8, 72550, 72550, 8, 72752, 72758, 8, 72760, 72765, 8, 72850, 72871, 8, 72874, 72880, 8, 72882, 72883, 8, 72885, 72886, 8, 73009, 73014, 8, 73018, 73018, 8, 73020, 73021, 8, 73023, 73029, 8, 73031, 73031, 8, 73104, 73105, 8, 73109, 73109, 8, 73111, 73111, 8, 73459, 73460, 8, 73472, 73473, 8, 73526, 73530, 8, 73536, 73536, 8, 73538, 73538, 8, 73562, 73562, 8, 73685, 73692, 13, 73693, 73696, 5, 73697, 73713, 13, 78912, 78912, 8, 78919, 78933, 8, 90398, 90409, 8, 90413, 90415, 8, 92912, 92916, 8, 92976, 92982, 8, 94031, 94031, 8, 94095, 94098, 8, 94178, 94178, 13, 94180, 94180, 8, 113821, 113822, 8, 113824, 113827, 9, 117760, 117973, 13, 118000, 118009, 3, 118010, 118012, 13, 118016, 118451, 13, 118458, 118480, 13, 118496, 118512, 13, 118528, 118573, 8, 118576, 118598, 8, 119143, 119145, 8, 119155, 119162, 9, 119163, 119170, 8, 119173, 119179, 8, 119210, 119213, 8, 119273, 119274, 13, 119296, 119361, 13, 119362, 119364, 8, 119365, 119365, 13, 119552, 119638, 13, 120513, 120513, 13, 120539, 120539, 13, 120571, 120571, 13, 120597, 120597, 13, 120629, 120629, 13, 120655, 120655, 13, 120687, 120687, 13, 120713, 120713, 13, 120745, 120745, 13, 120771, 120771, 13, 120782, 120831, 3, 121344, 121398, 8, 121403, 121452, 8, 121461, 121461, 8, 121476, 121476, 8, 121499, 121503, 8, 121505, 121519, 8, 122880, 122886, 8, 122888, 122904, 8, 122907, 122913, 8, 122915, 122916, 8, 122918, 122922, 8, 123023, 123023, 8, 123184, 123190, 8, 123566, 123566, 8, 123628, 123631, 8, 123647, 123647, 5, 124140, 124143, 8, 124398, 124399, 8, 124643, 124643, 8, 124646, 124646, 8, 124654, 124655, 8, 124661, 124661, 8, 124928, 125135, 1, 125136, 125142, 8, 125143, 125251, 1, 125252, 125258, 8, 125259, 126063, 1, 126064, 126143, 2, 126144, 126207, 1, 126208, 126287, 2, 126288, 126463, 1, 126464, 126703, 2, 126704, 126705, 13, 126706, 126719, 2, 126720, 126975, 1, 126976, 127019, 13, 127024, 127123, 13, 127136, 127150, 13, 127153, 127167, 13, 127169, 127183, 13, 127185, 127221, 13, 127232, 127242, 3, 127243, 127247, 13, 127279, 127279, 13, 127338, 127343, 13, 127405, 127405, 13, 127584, 127589, 13, 127744, 128728, 13, 128732, 128748, 13, 128752, 128764, 13, 128768, 128985, 13, 128992, 129003, 13, 129008, 129008, 13, 129024, 129035, 13, 129040, 129095, 13, 129104, 129113, 13, 129120, 129159, 13, 129168, 129197, 13, 129200, 129211, 13, 129216, 129217, 13, 129232, 129240, 13, 129280, 129623, 13, 129632, 129645, 13, 129648, 129660, 13, 129664, 129674, 13, 129678, 129734, 13, 129736, 129736, 13, 129741, 129756, 13, 129759, 129770, 13, 129775, 129784, 13, 129792, 129938, 13, 129940, 130031, 13, 130032, 130041, 3, 130042, 130042, 13, 131070, 131071, 9, 196606, 196607, 9, 262142, 262143, 9, 327678, 327679, 9, 393214, 393215, 9, 458750, 458751, 9, 524286, 524287, 9, 589822, 589823, 9, 655358, 655359, 9, 720894, 720895, 9, 786430, 786431, 9, 851966, 851967, 9, 917502, 917759, 9, 917760, 917999, 8, 918000, 921599, 9, 983038, 983039, 9, 1048574, 1048575, 9, 1114110, 1114111, 9];
},
"data/generated/brackets":function(exports,require){
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.BRACKET_CANONICAL = exports.BRACKET_TYPES = exports.BRACKET_PAIRS = exports.BRACKET_CPS = void 0;
exports.BRACKET_CPS = [40, 41, 91, 93, 123, 125, 3898, 3899, 3900, 3901, 5787, 5788, 8261, 8262, 8317, 8318, 8333, 8334, 8968, 8969, 8970, 8971, 9001, 9002, 10088, 10089, 10090, 10091, 10092, 10093, 10094, 10095, 10096, 10097, 10098, 10099, 10100, 10101, 10181, 10182, 10214, 10215, 10216, 10217, 10218, 10219, 10220, 10221, 10222, 10223, 10627, 10628, 10629, 10630, 10631, 10632, 10633, 10634, 10635, 10636, 10637, 10638, 10639, 10640, 10641, 10642, 10643, 10644, 10645, 10646, 10647, 10648, 10712, 10713, 10714, 10715, 10748, 10749, 11810, 11811, 11812, 11813, 11814, 11815, 11816, 11817, 11861, 11862, 11863, 11864, 11865, 11866, 11867, 11868, 12296, 12297, 12298, 12299, 12300, 12301, 12302, 12303, 12304, 12305, 12308, 12309, 12310, 12311, 12312, 12313, 12314, 12315, 65113, 65114, 65115, 65116, 65117, 65118, 65288, 65289, 65339, 65341, 65371, 65373, 65375, 65376, 65378, 65379];
exports.BRACKET_PAIRS = [41, 40, 93, 91, 125, 123, 3899, 3898, 3901, 3900, 5788, 5787, 8262, 8261, 8318, 8317, 8334, 8333, 8969, 8968, 8971, 8970, 9002, 9001, 10089, 10088, 10091, 10090, 10093, 10092, 10095, 10094, 10097, 10096, 10099, 10098, 10101, 10100, 10182, 10181, 10215, 10214, 10217, 10216, 10219, 10218, 10221, 10220, 10223, 10222, 10628, 10627, 10630, 10629, 10632, 10631, 10634, 10633, 10636, 10635, 10640, 10639, 10638, 10637, 10642, 10641, 10644, 10643, 10646, 10645, 10648, 10647, 10713, 10712, 10715, 10714, 10749, 10748, 11811, 11810, 11813, 11812, 11815, 11814, 11817, 11816, 11862, 11861, 11864, 11863, 11866, 11865, 11868, 11867, 12297, 12296, 12299, 12298, 12301, 12300, 12303, 12302, 12305, 12304, 12309, 12308, 12311, 12310, 12313, 12312, 12315, 12314, 65114, 65113, 65116, 65115, 65118, 65117, 65289, 65288, 65341, 65339, 65373, 65371, 65376, 65375, 65379, 65378];
exports.BRACKET_TYPES = [0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1];
exports.BRACKET_CANONICAL = [9001, 12296, 9002, 12297];
},
"data/generated/joining-types":function(exports,require){
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.JOINING_GROUP_RANGES = exports.JOINING_TYPE_RANGES = exports.JOINING_GROUP_NAMES = exports.JOINING_TYPE_NAMES = void 0;
exports.JOINING_TYPE_NAMES = ['U', 'C', 'D', 'L', 'R', 'T'];
exports.JOINING_GROUP_NAMES = ['No_Joining_Group', 'KASHMIRI YEH', 'ALEF', 'WAW', 'YEH', 'BEH', 'TEH MARBUTA', 'HAH', 'DAL', 'REH', 'SEEN', 'SAD', 'TAH', 'AIN', 'GAF', 'FARSI YEH', 'FEH', 'QAF', 'KAF', 'LAM', 'MEEM', 'NOON', 'HEH', 'SWASH KAF', 'NYA', 'KNOTTED HEH', 'HEH GOAL', 'TEH MARBUTA GOAL', 'YEH WITH TAIL', 'YEH BARREE', 'ALAPH', 'BETH', 'GAMAL', 'DALATH RISH', 'HE', 'SYRIAC WAW', 'ZAIN', 'HETH', 'TETH', 'YUDH', 'YUDH HE', 'KAPH', 'LAMADH', 'MIM', 'NUN', 'SEMKATH', 'FINAL SEMKATH', 'E', 'PE', 'REVERSED PE', 'SADHE', 'QAPH', 'SHIN', 'TAW', 'ZHAIN', 'KHAPH', 'FE', 'BURUSHASKI YEH BARREE', 'MALAYALAM NGA', 'MALAYALAM JA', 'MALAYALAM NYA', 'MALAYALAM TTA', 'MALAYALAM NNA', 'MALAYALAM NNNA', 'MALAYALAM BHA', 'MALAYALAM RA', 'MALAYALAM LLA', 'MALAYALAM LLLA', 'MALAYALAM SSA', 'THIN YEH', 'VERTICAL TAIL', 'ROHINGYA YEH', 'STRAIGHT WAW', 'AFRICAN FEH', 'AFRICAN QAF', 'AFRICAN NOON', 'MANICHAEAN ALEPH', 'MANICHAEAN BETH', 'MANICHAEAN GIMEL', 'MANICHAEAN DALETH', 'MANICHAEAN WAW', 'MANICHAEAN ZAYIN', 'MANICHAEAN HETH', 'MANICHAEAN TETH', 'MANICHAEAN YODH', 'MANICHAEAN KAPH', 'MANICHAEAN LAMEDH', 'MANICHAEAN DHAMEDH', 'MANICHAEAN THAMEDH', 'MANICHAEAN MEM', 'MANICHAEAN NUN', 'MANICHAEAN SAMEKH', 'MANICHAEAN AYIN', 'MANICHAEAN PE', 'MANICHAEAN SADHE', 'MANICHAEAN QOPH', 'MANICHAEAN RESH', 'MANICHAEAN TAW', 'MANICHAEAN ONE', 'MANICHAEAN FIVE', 'MANICHAEAN TEN', 'MANICHAEAN TWENTY', 'MANICHAEAN HUNDRED', 'HANIFI ROHINGYA PA', 'HANIFI ROHINGYA KINNA YA', 'THIN NOON'];
exports.JOINING_TYPE_RANGES = [173, 173, 5, 768, 879, 5, 1155, 1161, 5, 1425, 1469, 5, 1471, 1471, 5, 1473, 1474, 5, 1476, 1477, 5, 1479, 1479, 5, 1552, 1562, 5, 1564, 1564, 5, 1568, 1568, 2, 1570, 1573, 4, 1574, 1574, 2, 1575, 1575, 4, 1576, 1576, 2, 1577, 1577, 4, 1578, 1582, 2, 1583, 1586, 4, 1587, 1599, 2, 1600, 1600, 1, 1601, 1607, 2, 1608, 1608, 4, 1609, 1610, 2, 1611, 1631, 5, 1646, 1647, 2, 1648, 1648, 5, 1649, 1651, 4, 1653, 1655, 4, 1656, 1671, 2, 1672, 1689, 4, 1690, 1727, 2, 1728, 1728, 4, 1729, 1730, 2, 1731, 1739, 4, 1740, 1740, 2, 1741, 1741, 4, 1742, 1742, 2, 1743, 1743, 4, 1744, 1745, 2, 1746, 1747, 4, 1749, 1749, 4, 1750, 1756, 5, 1759, 1764, 5, 1767, 1768, 5, 1770, 1773, 5, 1774, 1775, 4, 1786, 1788, 2, 1791, 1791, 2, 1807, 1807, 5, 1808, 1808, 4, 1809, 1809, 5, 1810, 1812, 2, 1813, 1817, 4, 1818, 1821, 2, 1822, 1822, 4, 1823, 1831, 2, 1832, 1832, 4, 1833, 1833, 2, 1834, 1834, 4, 1835, 1835, 2, 1836, 1836, 4, 1837, 1838, 2, 1839, 1839, 4, 1840, 1866, 5, 1869, 1869, 4, 1870, 1880, 2, 1881, 1883, 4, 1884, 1898, 2, 1899, 1900, 4, 1901, 1904, 2, 1905, 1905, 4, 1906, 1906, 2, 1907, 1908, 4, 1909, 1911, 2, 1912, 1913, 4, 1914, 1919, 2, 1958, 1968, 5, 1994, 2026, 2, 2027, 2035, 5, 2042, 2042, 1, 2045, 2045, 5, 2070, 2073, 5, 2075, 2083, 5, 2085, 2087, 5, 2089, 2093, 5, 2112, 2112, 4, 2113, 2117, 2, 2118, 2119, 4, 2120, 2120, 2, 2121, 2121, 4, 2122, 2131, 2, 2132, 2132, 4, 2133, 2133, 2, 2134, 2136, 4, 2137, 2139, 5, 2144, 2144, 2, 2146, 2149, 2, 2151, 2151, 4, 2152, 2152, 2, 2153, 2154, 4, 2160, 2178, 4, 2179, 2181, 1, 2182, 2182, 2, 2185, 2189, 2, 2190, 2190, 4, 2191, 2191, 2, 2199, 2207, 5, 2208, 2217, 2, 2218, 2220, 4, 2222, 2222, 4, 2223, 2224, 2, 2225, 2226, 4, 2227, 2232, 2, 2233, 2233, 4, 2234, 2248, 2, 2250, 2273, 5, 2275, 2306, 5, 2362, 2362, 5, 2364, 2364, 5, 2369, 2376, 5, 2381, 2381, 5, 2385, 2391, 5, 2402, 2403, 5, 2433, 2433, 5, 2492, 2492, 5, 2497, 2500, 5, 2509, 2509, 5, 2530, 2531, 5, 2558, 2558, 5, 2561, 2562, 5, 2620, 2620, 5, 2625, 2626, 5, 2631, 2632, 5, 2635, 2637, 5, 2641, 2641, 5, 2672, 2673, 5, 2677, 2677, 5, 2689, 2690, 5, 2748, 2748, 5, 2753, 2757, 5, 2759, 2760, 5, 2765, 2765, 5, 2786, 2787, 5, 2810, 2815, 5, 2817, 2817, 5, 2876, 2876, 5, 2879, 2879, 5, 2881, 2884, 5, 2893, 2893, 5, 2901, 2902, 5, 2914, 2915, 5, 2946, 2946, 5, 3008, 3008, 5, 3021, 3021, 5, 3072, 3072, 5, 3076, 3076, 5, 3132, 3132, 5, 3134, 3136, 5, 3142, 3144, 5, 3146, 3149, 5, 3157, 3158, 5, 3170, 3171, 5, 3201, 3201, 5, 3260, 3260, 5, 3263, 3263, 5, 3270, 3270, 5, 3276, 3277, 5, 3298, 3299, 5, 3328, 3329, 5, 3387, 3388, 5, 3393, 3396, 5, 3405, 3405, 5, 3426, 3427, 5, 3457, 3457, 5, 3530, 3530, 5, 3538, 3540, 5, 3542, 3542, 5, 3633, 3633, 5, 3636, 3642, 5, 3655, 3662, 5, 3761, 3761, 5, 3764, 3772, 5, 3784, 3790, 5, 3864, 3865, 5, 3893, 3893, 5, 3895, 3895, 5, 3897, 3897, 5, 3953, 3966, 5, 3968, 3972, 5, 3974, 3975, 5, 3981, 3991, 5, 3993, 4028, 5, 4038, 4038, 5, 4141, 4144, 5, 4146, 4151, 5, 4153, 4154, 5, 4157, 4158, 5, 4184, 4185, 5, 4190, 4192, 5, 4209, 4212, 5, 4226, 4226, 5, 4229, 4230, 5, 4237, 4237, 5, 4253, 4253, 5, 4957, 4959, 5, 5906, 5908, 5, 5938, 5939, 5, 5970, 5971, 5, 6002, 6003, 5, 6068, 6069, 5, 6071, 6077, 5, 6086, 6086, 5, 6089, 6099, 5, 6109, 6109, 5, 6151, 6151, 2, 6154, 6154, 1, 6155, 6157, 5, 6159, 6159, 5, 6176, 6264, 2, 6277, 6278, 5, 6279, 6312, 2, 6313, 6313, 5, 6314, 6314, 2, 6432, 6434, 5, 6439, 6440, 5, 6450, 6450, 5, 6457, 6459, 5, 6679, 6680, 5, 6683, 6683, 5, 6742, 6742, 5, 6744, 6750, 5, 6752, 6752, 5, 6754, 6754, 5, 6757, 6764, 5, 6771, 6780, 5, 6783, 6783, 5, 6832, 6877, 5, 6880, 6891, 5, 6912, 6915, 5, 6964, 6964, 5, 6966, 6970, 5, 6972, 6972, 5, 6978, 6978, 5, 7019, 7027, 5, 7040, 7041, 5, 7074, 7077, 5, 7080, 7081, 5, 7083, 7085, 5, 7142, 7142, 5, 7144, 7145, 5, 7149, 7149, 5, 7151, 7153, 5, 7212, 7219, 5, 7222, 7223, 5, 7376, 7378, 5, 7380, 7392, 5, 7394, 7400, 5, 7405, 7405, 5, 7412, 7412, 5, 7416, 7417, 5, 7616, 7679, 5, 8203, 8203, 5, 8205, 8205, 1, 8206, 8207, 5, 8234, 8238, 5, 8288, 8292, 5, 8298, 8303, 5, 8400, 8432, 5, 11503, 11505, 5, 11647, 11647, 5, 11744, 11775, 5, 12330, 12333, 5, 12441, 12442, 5, 42607, 42610, 5, 42612, 42621, 5, 42654, 42655, 5, 42736, 42737, 5, 43010, 43010, 5, 43014, 43014, 5, 43019, 43019, 5, 43045, 43046, 5, 43052, 43052, 5, 43072, 43121, 2, 43122, 43122, 3, 43204, 43205, 5, 43232, 43249, 5, 43263, 43263, 5, 43302, 43309, 5, 43335, 43345, 5, 43392, 43394, 5, 43443, 43443, 5, 43446, 43449, 5, 43452, 43453, 5, 43493, 43493, 5, 43561, 43566, 5, 43569, 43570, 5, 43573, 43574, 5, 43587, 43587, 5, 43596, 43596, 5, 43644, 43644, 5, 43696, 43696, 5, 43698, 43700, 5, 43703, 43704, 5, 43710, 43711, 5, 43713, 43713, 5, 43756, 43757, 5, 43766, 43766, 5, 44005, 44005, 5, 44008, 44008, 5, 44013, 44013, 5, 64286, 64286, 5, 65024, 65039, 5, 65056, 65071, 5, 65279, 65279, 5, 65529, 65531, 5, 66045, 66045, 5, 66272, 66272, 5, 66422, 66426, 5, 68097, 68099, 5, 68101, 68102, 5, 68108, 68111, 5, 68152, 68154, 5, 68159, 68159, 5, 68288, 68292, 2, 68293, 68293, 4, 68295, 68295, 4, 68297, 68298, 4, 68301, 68301, 3, 68302, 68306, 4, 68307, 68310, 2, 68311, 68311, 3, 68312, 68316, 2, 68317, 68317, 4, 68318, 68320, 2, 68321, 68321, 4, 68324, 68324, 4, 68325, 68326, 5, 68331, 68334, 2, 68335, 68335, 4, 68480, 68480, 2, 68481, 68481, 4, 68482, 68482, 2, 68483, 68485, 4, 68486, 68488, 2, 68489, 68489, 4, 68490, 68491, 2, 68492, 68492, 4, 68493, 68493, 2, 68494, 68495, 4, 68496, 68496, 2, 68497, 68497, 4, 68521, 68524, 4, 68525, 68526, 2, 68864, 68864, 3, 68865, 68897, 2, 68898, 68898, 4, 68899, 68899, 2, 68900, 68903, 5, 68969, 68973, 5, 69291, 69292, 5, 69314, 69314, 4, 69315, 69316, 2, 69318, 69319, 2, 69370, 69375, 5, 69424, 69426, 2, 69427, 69427, 4, 69428, 69444, 2, 69446, 69456, 5, 69457, 69459, 2, 69460, 69460, 4, 69488, 69491, 2, 69492, 69493, 4, 69494, 69505, 2, 69506, 69509, 5, 69552, 69552, 2, 69554, 69555, 2, 69556, 69558, 4, 69560, 69560, 2, 69561, 69562, 4, 69563, 69564, 2, 69565, 69565, 4, 69566, 69567, 2, 69569, 69569, 2, 69570, 69571, 4, 69572, 69572, 2, 69577, 69577, 4, 69578, 69578, 2, 69579, 69579, 3, 69633, 69633, 5, 69688, 69702, 5, 69744, 69744, 5, 69747, 69748, 5, 69759, 69761, 5, 69811, 69814, 5, 69817, 69818, 5, 69826, 69826, 5, 69888, 69890, 5, 69927, 69931, 5, 69933, 69940, 5, 70003, 70003, 5, 70016, 70017, 5, 70070, 70078, 5, 70089, 70092, 5, 70095, 70095, 5, 70191, 70193, 5, 70196, 70196, 5, 70198, 70199, 5, 70206, 70206, 5, 70209, 70209, 5, 70367, 70367, 5, 70371, 70378, 5, 70400, 70401, 5, 70459, 70460, 5, 70464, 70464, 5, 70502, 70508, 5, 70512, 70516, 5, 70587, 70592, 5, 70606, 70606, 5, 70608, 70608, 5, 70610, 70610, 5, 70625, 70626, 5, 70712, 70719, 5, 70722, 70724, 5, 70726, 70726, 5, 70750, 70750, 5, 70835, 70840, 5, 70842, 70842, 5, 70847, 70848, 5, 70850, 70851, 5, 71090, 71093, 5, 71100, 71101, 5, 71103, 71104, 5, 71132, 71133, 5, 71219, 71226, 5, 71229, 71229, 5, 71231, 71232, 5, 71339, 71339, 5, 71341, 71341, 5, 71344, 71349, 5, 71351, 71351, 5, 71453, 71453, 5, 71455, 71455, 5, 71458, 71461, 5, 71463, 71467, 5, 71727, 71735, 5, 71737, 71738, 5, 71995, 71996, 5, 71998, 71998, 5, 72003, 72003, 5, 72148, 72151, 5, 72154, 72155, 5, 72160, 72160, 5, 72193, 72202, 5, 72243, 72248, 5, 72251, 72254, 5, 72263, 72263, 5, 72273, 72278, 5, 72281, 72283, 5, 72330, 72342, 5, 72344, 72345, 5, 72544, 72544, 5, 72546, 72548, 5, 72550, 72550, 5, 72752, 72758, 5, 72760, 72765, 5, 72767, 72767, 5, 72850, 72871, 5, 72874, 72880, 5, 72882, 72883, 5, 72885, 72886, 5, 73009, 73014, 5, 73018, 73018, 5, 73020, 73021, 5, 73023, 73029, 5, 73031, 73031, 5, 73104, 73105, 5, 73109, 73109, 5, 73111, 73111, 5, 73459, 73460, 5, 73472, 73473, 5, 73526, 73530, 5, 73536, 73536, 5, 73538, 73538, 5, 73562, 73562, 5, 78896, 78912, 5, 78919, 78933, 5, 90398, 90409, 5, 90413, 90415, 5, 92912, 92916, 5, 92976, 92982, 5, 94031, 94031, 5, 94095, 94098, 5, 94180, 94180, 5, 113821, 113822, 5, 113824, 113827, 5, 118528, 118573, 5, 118576, 118598, 5, 119143, 119145, 5, 119155, 119170, 5, 119173, 119179, 5, 119210, 119213, 5, 119362, 119364, 5, 121344, 121398, 5, 121403, 121452, 5, 121461, 121461, 5, 121476, 121476, 5, 121499, 121503, 5, 121505, 121519, 5, 122880, 122886, 5, 122888, 122904, 5, 122907, 122913, 5, 122915, 122916, 5, 122918, 122922, 5, 123023, 123023, 5, 123184, 123190, 5, 123566, 123566, 5, 123628, 123631, 5, 124140, 124143, 5, 124398, 124399, 5, 124643, 124643, 5, 124646, 124646, 5, 124654, 124655, 5, 124661, 124661, 5, 125136, 125142, 5, 125184, 125251, 2, 125252, 125259, 5, 917505, 917505, 5, 917536, 917631, 5, 917760, 917999, 5];
exports.JOINING_GROUP_RANGES = [1568, 1568, 1, 1570, 1571, 2, 1572, 1572, 3, 1573, 1573, 2, 1574, 1574, 4, 1575, 1575, 2, 1576, 1576, 5, 1577, 1577, 6, 1578, 1579, 5, 1580, 1582, 7, 1583, 1584, 8, 1585, 1586, 9, 1587, 1588, 10, 1589, 1590, 11, 1591, 1592, 12, 1593, 1594, 13, 1595, 1596, 14, 1597, 1599, 15, 1601, 1601, 16, 1602, 1602, 17, 1603, 1603, 18, 1604, 1604, 19, 1605, 1605, 20, 1606, 1606, 21, 1607, 1607, 22, 1608, 1608, 3, 1609, 1610, 4, 1646, 1646, 5, 1647, 1647, 17, 1649, 1651, 2, 1653, 1653, 2, 1654, 1655, 3, 1656, 1656, 4, 1657, 1664, 5, 1665, 1671, 7, 1672, 1680, 8, 1681, 1689, 9, 1690, 1692, 10, 1693, 1694, 11, 1695, 1695, 12, 1696, 1696, 13, 1697, 1702, 16, 1703, 1704, 17, 1705, 1705, 14, 1706, 1706, 23, 1707, 1707, 14, 1708, 1710, 18, 1711, 1716, 14, 1717, 1720, 19, 1721, 1724, 21, 1725, 1725, 24, 1726, 1726, 25, 1727, 1727, 7, 1728, 1728, 6, 1729, 1730, 26, 1731, 1731, 27, 1732, 1739, 3, 1740, 1740, 15, 1741, 1741, 28, 1742, 1742, 15, 1743, 1743, 3, 1744, 1745, 4, 1746, 1747, 29, 1749, 1749, 6, 1774, 1774, 8, 1775, 1775, 9, 1786, 1786, 10, 1787, 1787, 11, 1788, 1788, 13, 1791, 1791, 25, 1808, 1808, 30, 1810, 1810, 31, 1811, 1812, 32, 1813, 1814, 33, 1815, 1815, 34, 1816, 1816, 35, 1817, 1817, 36, 1818, 1818, 37, 1819, 1820, 38, 1821, 1821, 39, 1822, 1822, 40, 1823, 1823, 41, 1824, 1824, 42, 1825, 1825, 43, 1826, 1826, 44, 1827, 1827, 45, 1828, 1828, 46, 1829, 1829, 47, 1830, 1830, 48, 1831, 1831, 49, 1832, 1832, 50, 1833, 1833, 51, 1834, 1834, 33, 1835, 1835, 52, 1836, 1836, 53, 1837, 1837, 31, 1838, 1838, 32, 1839, 1839, 33, 1869, 1869, 54, 1870, 1870, 55, 1871, 1871, 56, 1872, 1878, 5, 1879, 1880, 7, 1881, 1882, 8, 1883, 1883, 9, 1884, 1884, 10, 1885, 1887, 13, 1888, 1889, 16, 1890, 1892, 14, 1893, 1894, 20, 1895, 1897, 21, 1898, 1898, 19, 1899, 1900, 9, 1901, 1901, 10, 1902, 1903, 7, 1904, 1904, 10, 1905, 1905, 9, 1906, 1906, 7, 1907, 1908, 2, 1909, 1910, 15, 1911, 1911, 4, 1912, 1913, 3, 1914, 1915, 57, 1916, 1916, 7, 1917, 1918, 10, 1919, 1919, 18, 2144, 2144, 58, 2145, 2145, 59, 2146, 2146, 60, 2147, 2147, 61, 2148, 2148, 62, 2149, 2149, 63, 2150, 2150, 64, 2151, 2151, 65, 2152, 2152, 66, 2153, 2153, 67, 2154, 2154, 68, 2160, 2178, 2, 2182, 2182, 69, 2185, 2185, 21, 2186, 2186, 7, 2187, 2188, 12, 2189, 2189, 14, 2190, 2190, 70, 2191, 2191, 21, 2208, 2209, 5, 2210, 2210, 7, 2211, 2211, 12, 2212, 2212, 16, 2213, 2213, 17, 2214, 2214, 19, 2215, 2215, 20, 2216, 2217, 4, 2218, 2218, 9, 2219, 2219, 3, 2220, 2220, 71, 2222, 2222, 8, 2223, 2223, 11, 2224, 2224, 14, 2225, 2225, 72, 2226, 2226, 9, 2227, 2227, 13, 2228, 2228, 18, 2229, 2229, 17, 2230, 2232, 5, 2233, 2233, 9, 2234, 2234, 4, 2235, 2235, 73, 2236, 2236, 74, 2237, 2237, 75, 2238, 2240, 5, 2241, 2241, 7, 2242, 2242, 14, 2243, 2243, 13, 2244, 2244, 74, 2245, 2246, 7, 2247, 2247, 19, 2248, 2248, 14, 68288, 68288, 76, 68289, 68290, 77, 68291, 68292, 78, 68293, 68293, 79, 68295, 68295, 80, 68297, 68298, 81, 68301, 68301, 82, 68302, 68302, 83, 68303, 68303, 84, 68304, 68306, 85, 68307, 68307, 86, 68308, 68308, 87, 68309, 68309, 88, 68310, 68310, 89, 68311, 68311, 90, 68312, 68312, 91, 68313, 68314, 92, 68315, 68316, 93, 68317, 68317, 94, 68318, 68320, 95, 68321, 68321, 96, 68324, 68324, 97, 68331, 68331, 98, 68332, 68332, 99, 68333, 68333, 100, 68334, 68334, 101, 68335, 68335, 102, 68866, 68866, 103, 68873, 68873, 103, 68889, 68889, 104, 68892, 68892, 103, 68894, 68894, 104, 68896, 68896, 104, 68899, 68899, 104, 69314, 69314, 8, 69315, 69315, 12, 69316, 69316, 18, 69318, 69318, 105, 69319, 69319, 4];
},
"data/generated/mirroring":function(exports,require){
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.MIRROR_PAIRS = void 0;
exports.MIRROR_PAIRS = [40, 41, 41, 40, 60, 62, 62, 60, 91, 93, 93, 91, 123, 125, 125, 123, 171, 187, 187, 171, 3898, 3899, 3899, 3898, 3900, 3901, 3901, 3900, 5787, 5788, 5788, 5787, 8249, 8250, 8250, 8249, 8261, 8262, 8262, 8261, 8317, 8318, 8318, 8317, 8333, 8334, 8334, 8333, 8712, 8715, 8713, 8716, 8714, 8717, 8715, 8712, 8716, 8713, 8717, 8714, 8725, 10741, 8735, 11262, 8736, 10659, 8737, 10651, 8738, 10656, 8740, 10990, 8764, 8765, 8765, 8764, 8771, 8909, 8773, 8780, 8780, 8773, 8786, 8787, 8787, 8786, 8788, 8789, 8789, 8788, 8804, 8805, 8805, 8804, 8806, 8807, 8807, 8806, 8808, 8809, 8809, 8808, 8810, 8811, 8811, 8810, 8814, 8815, 8815, 8814, 8816, 8817, 8817, 8816, 8818, 8819, 8819, 8818, 8820, 8821, 8821, 8820, 8822, 8823, 8823, 8822, 8824, 8825, 8825, 8824, 8826, 8827, 8827, 8826, 8828, 8829, 8829, 8828, 8830, 8831, 8831, 8830, 8832, 8833, 8833, 8832, 8834, 8835, 8835, 8834, 8836, 8837, 8837, 8836, 8838, 8839, 8839, 8838, 8840, 8841, 8841, 8840, 8842, 8843, 8843, 8842, 8847, 8848, 8848, 8847, 8849, 8850, 8850, 8849, 8856, 10680, 8866, 8867, 8867, 8866, 8870, 10974, 8872, 10980, 8873, 10979, 8875, 10981, 8880, 8881, 8881, 8880, 8882, 8883, 8883, 8882, 8884, 8885, 8885, 8884, 8886, 8887, 8887, 8886, 8888, 10204, 8905, 8906, 8906, 8905, 8907, 8908, 8908, 8907, 8909, 8771, 8912, 8913, 8913, 8912, 8918, 8919, 8919, 8918, 8920, 8921, 8921, 8920, 8922, 8923, 8923, 8922, 8924, 8925, 8925, 8924, 8926, 8927, 8927, 8926, 8928, 8929, 8929, 8928, 8930, 8931, 8931, 8930, 8932, 8933, 8933, 8932, 8934, 8935, 8935, 8934, 8936, 8937, 8937, 8936, 8938, 8939, 8939, 8938, 8940, 8941, 8941, 8940, 8944, 8945, 8945, 8944, 8946, 8954, 8947, 8955, 8948, 8956, 8950, 8957, 8951, 8958, 8954, 8946, 8955, 8947, 8956, 8948, 8957, 8950, 8958, 8951, 8968, 8969, 8969, 8968, 8970, 8971, 8971, 8970, 9001, 9002, 9002, 9001, 10088, 10089, 10089, 10088, 10090, 10091, 10091, 10090, 10092, 10093, 10093, 10092, 10094, 10095, 10095, 10094, 10096, 10097, 10097, 10096, 10098, 10099, 10099, 10098, 10100, 10101, 10101, 10100, 10179, 10180, 10180, 10179, 10181, 10182, 10182, 10181, 10184, 10185, 10185, 10184, 10187, 10189, 10189, 10187, 10197, 10198, 10198, 10197, 10204, 8888, 10205, 10206, 10206, 10205, 10210, 10211, 10211, 10210, 10212, 10213, 10213, 10212, 10214, 10215, 10215, 10214, 10216, 10217, 10217, 10216, 10218, 10219, 10219, 10218, 10220, 10221, 10221, 10220, 10222, 10223, 10223, 10222, 10627, 10628, 10628, 10627, 10629, 10630, 10630, 10629, 10631, 10632, 10632, 10631, 10633, 10634, 10634, 10633, 10635, 10636, 10636, 10635, 10637, 10640, 10638, 10639, 10639, 10638, 10640, 10637, 10641, 10642, 10642, 10641, 10643, 10644, 10644, 10643, 10645, 10646, 10646, 10645, 10647, 10648, 10648, 10647, 10651, 8737, 10656, 8738, 10659, 8736, 10660, 10661, 10661, 10660, 10664, 10665, 10665, 10664, 10666, 10667, 10667, 10666, 10668, 10669, 10669, 10668, 10670, 10671, 10671, 10670, 10680, 8856, 10688, 10689, 10689, 10688, 10692, 10693, 10693, 10692, 10703, 10704, 10704, 10703, 10705, 10706, 10706, 10705, 10708, 10709, 10709, 10708, 10712, 10713, 10713, 10712, 10714, 10715, 10715, 10714, 10728, 10729, 10729, 10728, 10741, 8725, 10744, 10745, 10745, 10744, 10748, 10749, 10749, 10748, 10795, 10796, 10796, 10795, 10797, 10798, 10798, 10797, 10804, 10805, 10805, 10804, 10812, 10813, 10813, 10812, 10852, 10853, 10853, 10852, 10873, 10874, 10874, 10873, 10875, 10876, 10876, 10875, 10877, 10878, 10878, 10877, 10879, 10880, 10880, 10879, 10881, 10882, 10882, 10881, 10883, 10884, 10884, 10883, 10885, 10886, 10886, 10885, 10887, 10888, 10888, 10887, 10889, 10890, 10890, 10889, 10891, 10892, 10892, 10891, 10893, 10894, 10894, 10893, 10895, 10896, 10896, 10895, 10897, 10898, 10898, 10897, 10899, 10900, 10900, 10899, 10901, 10902, 10902, 10901, 10903, 10904, 10904, 10903, 10905, 10906, 10906, 10905, 10907, 10908, 10908, 10907, 10909, 10910, 10910, 10909, 10911, 10912, 10912, 10911, 10913, 10914, 10914, 10913, 10918, 10919, 10919, 10918, 10920, 10921, 10921, 10920, 10922, 10923, 10923, 10922, 10924, 10925, 10925, 10924, 10927, 10928, 10928, 10927, 10929, 10930, 10930, 10929, 10931, 10932, 10932, 10931, 10933, 10934, 10934, 10933, 10935, 10936, 10936, 10935, 10937, 10938, 10938, 10937, 10939, 10940, 10940, 10939, 10941, 10942, 10942, 10941, 10943, 10944, 10944, 10943, 10945, 10946, 10946, 10945, 10947, 10948, 10948, 10947, 10949, 10950, 10950, 10949, 10951, 10952, 10952, 10951, 10953, 10954, 10954, 10953, 10955, 10956, 10956, 10955, 10957, 10958, 10958, 10957, 10959, 10960, 10960, 10959, 10961, 10962, 10962, 10961, 10963, 10964, 10964, 10963, 10965, 10966, 10966, 10965, 10974, 8870, 10979, 8873, 10980, 8872, 10981, 8875, 10988, 10989, 10989, 10988, 10990, 8740, 10999, 11000, 11000, 10999, 11001, 11002, 11002, 11001, 11262, 8735, 11778, 11779, 11779, 11778, 11780, 11781, 11781, 11780, 11785, 11786, 11786, 11785, 11788, 11789, 11789, 11788, 11804, 11805, 11805, 11804, 11808, 11809, 11809, 11808, 11810, 11811, 11811, 11810, 11812, 11813, 11813, 11812, 11814, 11815, 11815, 11814, 11816, 11817, 11817, 11816, 11861, 11862, 11862, 11861, 11863, 11864, 11864, 11863, 11865, 11866, 11866, 11865, 11867, 11868, 11868, 11867, 12296, 12297, 12297, 12296, 12298, 12299, 12299, 12298, 12300, 12301, 12301, 12300, 12302, 12303, 12303, 12302, 12304, 12305, 12305, 12304, 12308, 12309, 12309, 12308, 12310, 12311, 12311, 12310, 12312, 12313, 12313, 12312, 12314, 12315, 12315, 12314, 65113, 65114, 65114, 65113, 65115, 65116, 65116, 65115, 65117, 65118, 65118, 65117, 65124, 65125, 65125, 65124, 65288, 65289, 65289, 65288, 65308, 65310, 65310, 65308, 65339, 65341, 65341, 65339, 65371, 65373, 65373, 65371, 65375, 65376, 65376, 65375, 65378, 65379, 65379, 65378];
},
"data/generated/shaping-forms":function(exports,require){
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.LAM_ALEF = exports.SHAPING_FORMS = void 0;
exports.SHAPING_FORMS = {
    1569: [65152, 0, 0, 0],
    1570: [65153, 0, 0, 65154],
    1571: [65155, 0, 0, 65156],
    1572: [65157, 0, 0, 65158],
    1573: [65159, 0, 0, 65160],
    1574: [65161, 65163, 65164, 65162],
    1575: [65165, 0, 0, 65166],
    1576: [65167, 65169, 65170, 65168],
    1577: [65171, 0, 0, 65172],
    1578: [65173, 65175, 65176, 65174],
    1579: [65177, 65179, 65180, 65178],
    1580: [65181, 65183, 65184, 65182],
    1581: [65185, 65187, 65188, 65186],
    1582: [65189, 65191, 65192, 65190],
    1583: [65193, 0, 0, 65194],
    1584: [65195, 0, 0, 65196],
    1585: [65197, 0, 0, 65198],
    1586: [65199, 0, 0, 65200],
    1587: [65201, 65203, 65204, 65202],
    1588: [65205, 65207, 65208, 65206],
    1589: [65209, 65211, 65212, 65210],
    1590: [65213, 65215, 65216, 65214],
    1591: [65217, 65219, 65220, 65218],
    1592: [65221, 65223, 65224, 65222],
    1593: [65225, 65227, 65228, 65226],
    1594: [65229, 65231, 65232, 65230],
    1601: [65233, 65235, 65236, 65234],
    1602: [65237, 65239, 65240, 65238],
    1603: [65241, 65243, 65244, 65242],
    1604: [65245, 65247, 65248, 65246],
    1605: [65249, 65251, 65252, 65250],
    1606: [65253, 65255, 65256, 65254],
    1607: [65257, 65259, 65260, 65258],
    1608: [65261, 0, 0, 65262],
    1609: [65263, 64488, 64489, 65264],
    1610: [65265, 65267, 65268, 65266],
    1649: [64336, 0, 0, 64337],
    1655: [64477, 0, 0, 0],
    1657: [64358, 64360, 64361, 64359],
    1658: [64350, 64352, 64353, 64351],
    1659: [64338, 64340, 64341, 64339],
    1662: [64342, 64344, 64345, 64343],
    1663: [64354, 64356, 64357, 64355],
    1664: [64346, 64348, 64349, 64347],
    1667: [64374, 64376, 64377, 64375],
    1668: [64370, 64372, 64373, 64371],
    1670: [64378, 64380, 64381, 64379],
    1671: [64382, 64384, 64385, 64383],
    1672: [64392, 0, 0, 64393],
    1676: [64388, 0, 0, 64389],
    1677: [64386, 0, 0, 64387],
    1678: [64390, 0, 0, 64391],
    1681: [64396, 0, 0, 64397],
    1688: [64394, 0, 0, 64395],
    1700: [64362, 64364, 64365, 64363],
    1702: [64366, 64368, 64369, 64367],
    1705: [64398, 64400, 64401, 64399],
    1709: [64467, 64469, 64470, 64468],
    1711: [64402, 64404, 64405, 64403],
    1713: [64410, 64412, 64413, 64411],
    1715: [64406, 64408, 64409, 64407],
    1722: [64414, 0, 0, 64415],
    1723: [64416, 64418, 64419, 64417],
    1726: [64426, 64428, 64429, 64427],
    1728: [64420, 0, 0, 64421],
    1729: [64422, 64424, 64425, 64423],
    1733: [64480, 0, 0, 64481],
    1734: [64473, 0, 0, 64474],
    1735: [64471, 0, 0, 64472],
    1736: [64475, 0, 0, 64476],
    1737: [64482, 0, 0, 64483],
    1739: [64478, 0, 0, 64479],
    1740: [64508, 64510, 64511, 64509],
    1744: [64484, 64486, 64487, 64485],
    1746: [64430, 0, 0, 64431],
    1747: [64432, 0, 0, 64433],
};
exports.LAM_ALEF = {
    1570: [65269, 65270],
    1571: [65271, 65272],
    1573: [65273, 65274],
    1575: [65275, 65276],
};
},
"data/generated/version":function(exports,require){
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.UNICODE_VERSION = void 0;
exports.UNICODE_VERSION = '17.0.0';
},
"data/lookup":function(exports,require){
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.getBidiClass = getBidiClass;
exports.getJoiningType = getJoiningType;
exports.getJoiningGroup = getJoiningGroup;
exports.getMirror = getMirror;
exports.getBracketType = getBracketType;
exports.getBracketPair = getBracketPair;
exports.canonicalBracket = canonicalBracket;
exports.getShapingForms = getShapingForms;
exports.getLamAlef = getLamAlef;
const bidi_classes_1 = require("./generated/bidi-classes");
const mirroring_1 = require("./generated/mirroring");
const brackets_1 = require("./generated/brackets");
const joining_types_1 = require("./generated/joining-types");
const shaping_forms_1 = require("./generated/shaping-forms");
function searchTriples(ranges, cp, def) {
    let lo = 0;
    let hi = ranges.length / 3 - 1;
    while (lo <= hi) {
        const mid = (lo + hi) >> 1;
        const i = mid * 3;
        if (cp < ranges[i])
            hi = mid - 1;
        else if (cp > ranges[i + 1])
            lo = mid + 1;
        else
            return ranges[i + 2];
    }
    return def;
}
function searchPairs(pairs, cp) {
    let lo = 0;
    let hi = pairs.length / 2 - 1;
    while (lo <= hi) {
        const mid = (lo + hi) >> 1;
        const key = pairs[mid * 2];
        if (cp < key)
            hi = mid - 1;
        else if (cp > key)
            lo = mid + 1;
        else
            return pairs[mid * 2 + 1];
    }
    return 0;
}
function getBidiClass(cp) {
    return searchTriples(bidi_classes_1.BIDI_CLASS_RANGES, cp, 0);
}
function getJoiningType(cp) {
    return searchTriples(joining_types_1.JOINING_TYPE_RANGES, cp, 0);
}
function getJoiningGroup(cp) {
    return searchTriples(joining_types_1.JOINING_GROUP_RANGES, cp, 0);
}
function getMirror(cp) {
    return searchPairs(mirroring_1.MIRROR_PAIRS, cp);
}
function bracketIndex(cp) {
    let lo = 0;
    let hi = brackets_1.BRACKET_CPS.length - 1;
    while (lo <= hi) {
        const mid = (lo + hi) >> 1;
        const v = brackets_1.BRACKET_CPS[mid];
        if (cp < v)
            hi = mid - 1;
        else if (cp > v)
            lo = mid + 1;
        else
            return mid;
    }
    return -1;
}
function getBracketType(cp) {
    const i = bracketIndex(cp);
    return i < 0 ? -1 : brackets_1.BRACKET_TYPES[i];
}
function getBracketPair(cp) {
    const i = bracketIndex(cp);
    return i < 0 ? 0 : brackets_1.BRACKET_PAIRS[i];
}
function canonicalBracket(cp) {
    for (let i = 0; i < brackets_1.BRACKET_CANONICAL.length; i += 2) {
        if (brackets_1.BRACKET_CANONICAL[i] === cp)
            return brackets_1.BRACKET_CANONICAL[i + 1];
    }
    return cp;
}
function getShapingForms(cp) {
    return shaping_forms_1.SHAPING_FORMS[cp];
}
function getLamAlef(cp) {
    return shaping_forms_1.LAM_ALEF[cp];
}
},
"index":function(exports,require){
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.UNICODE_VERSION = exports.detectDirection = exports.getEmbeddingLevels = exports.reorder = exports.shape = exports.analyze = exports.render = void 0;
var render_1 = require("./api/render");
Object.defineProperty(exports, "render", { enumerable: true, get: function () { return render_1.render; } });
Object.defineProperty(exports, "analyze", { enumerable: true, get: function () { return render_1.analyze; } });
Object.defineProperty(exports, "shape", { enumerable: true, get: function () { return render_1.shape; } });
Object.defineProperty(exports, "reorder", { enumerable: true, get: function () { return render_1.reorder; } });
Object.defineProperty(exports, "getEmbeddingLevels", { enumerable: true, get: function () { return render_1.getEmbeddingLevels; } });
Object.defineProperty(exports, "detectDirection", { enumerable: true, get: function () { return render_1.detectDirection; } });
var version_1 = require("./data/generated/version");
Object.defineProperty(exports, "UNICODE_VERSION", { enumerable: true, get: function () { return version_1.UNICODE_VERSION; } });
},
"shape/shape":function(exports,require){
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.isTashkeel = isTashkeel;
exports.shapeCodePoints = shapeCodePoints;
exports.shapeString = shapeString;
const types_1 = require("../bidi/types");
const lookup_1 = require("../data/lookup");
const LAM = 0x0644;
function isTashkeel(cp) {
    return ((cp >= 0x0610 && cp <= 0x061a) ||
        (cp >= 0x064b && cp <= 0x065f) ||
        cp === 0x0670 ||
        (cp >= 0x06d6 && cp <= 0x06dc) ||
        (cp >= 0x06df && cp <= 0x06e4) ||
        cp === 0x06e7 ||
        cp === 0x06e8 ||
        (cp >= 0x06ea && cp <= 0x06ed));
}
function joinsAhead(jt) {
    return jt === types_1.JT.D || jt === types_1.JT.L || jt === types_1.JT.C;
}
function joinsBack(jt) {
    return jt === types_1.JT.D || jt === types_1.JT.R || jt === types_1.JT.C;
}
const ISOLATED = 0;
const INITIAL = 1;
const MEDIAL = 2;
const FINAL = 3;
function pickForm(forms, form) {
    if (forms[form] !== 0)
        return forms[form];
    if (form === MEDIAL && forms[FINAL] !== 0)
        return forms[FINAL];
    if (form === INITIAL || form === MEDIAL || form === FINAL) {
        if (forms[ISOLATED] !== 0)
            return forms[ISOLATED];
    }
    return 0;
}
function shapeCodePoints(input, options) {
    const ligatures = options?.ligatures !== false;
    const strip = options?.tashkeel === 'strip';
    const cps = [];
    const src = [];
    for (let i = 0; i < input.length; i++) {
        const cp = input[i];
        if (strip && isTashkeel(cp))
            continue;
        cps.push(cp);
        src.push(i);
    }
    const n = cps.length;
    const jts = new Uint8Array(n);
    let hasArabic = false;
    for (let i = 0; i < n; i++) {
        jts[i] = (0, lookup_1.getJoiningType)(cps[i]);
        if (!hasArabic && (0, lookup_1.getShapingForms)(cps[i]) !== undefined)
            hasArabic = true;
    }
    if (!hasArabic)
        return { codePoints: cps, src };
    const out = [];
    const outSrc = [];
    let prevJt = types_1.JT.U;
    for (let i = 0; i < n; i++) {
        const cp = cps[i];
        const jt = jts[i];
        if (jt === types_1.JT.T) {
            out.push(cp);
            outSrc.push(src[i]);
            continue;
        }
        let next = i + 1;
        while (next < n && jts[next] === types_1.JT.T)
            next++;
        const nextJt = next < n ? jts[next] : types_1.JT.U;
        const linksBack = joinsAhead(prevJt) && (jt === types_1.JT.D || jt === types_1.JT.R);
        const linksAhead = joinsBack(nextJt) && (jt === types_1.JT.D || jt === types_1.JT.L);
        if (ligatures && cp === LAM && next < n) {
            const lig = (0, lookup_1.getLamAlef)(cps[next]);
            if (lig !== undefined) {
                out.push(linksBack ? lig[1] : lig[0]);
                outSrc.push(src[i]);
                for (let m = i + 1; m < next; m++) {
                    out.push(cps[m]);
                    outSrc.push(src[m]);
                }
                i = next;
                prevJt = types_1.JT.R;
                continue;
            }
        }
        const forms = (0, lookup_1.getShapingForms)(cp);
        if (forms !== undefined) {
            const form = linksBack ? (linksAhead ? MEDIAL : FINAL) : linksAhead ? INITIAL : ISOLATED;
            const sub = pickForm(forms, form);
            out.push(sub !== 0 ? sub : cp);
        }
        else {
            out.push(cp);
        }
        outSrc.push(src[i]);
        prevJt = jt;
    }
    return { codePoints: out, src: outSrc };
}
function shapeString(text, options) {
    const cps = [];
    for (const ch of text)
        cps.push(ch.codePointAt(0));
    const { codePoints } = shapeCodePoints(cps, options);
    let outStr = '';
    for (const cp of codePoints)
        outStr += String.fromCodePoint(cp);
    return outStr;
}
},
};const cache=Object.create(null);function load(id){if(cache[id])return cache[id];if(!Object.prototype.hasOwnProperty.call(modules,id))throw Error("Unknown Unicode module "+id);const e=cache[id]={};modules[id](e,rel=>{const p=id.split("/");p.pop();for(const s of rel.split("/")){if(s==="..")p.pop();else if(s!==".")p.push(s);}return load(p.join("/"));});return e;}root.Kestrel.Unicode={...load("index"),...load("bidi/algorithm"),...load("bidi/reorder"),...load("bidi/levels"),BIDI_CLASS_NAMES:load("data/generated/bidi-classes").BIDI_CLASS_NAMES};})(globalThis);
