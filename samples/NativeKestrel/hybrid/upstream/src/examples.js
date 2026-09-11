/* Kestrel CAD — editable, brand-neutral sample drawings. */
(function (root) {
    'use strict';
    const K = root.Kestrel, { V, M, TAU } = K.Math, G = K.Geo;
    function courtyard() {
        const d = new K.Drawing('Courtyard House · Ground floor');
        const add = (type, props, layer = 'architecture') => d.add(type, { ...props, layer });
        const line = (a, b, layer = 'architecture') => add('LINE', { points: [[...a.slice(0, 2), a[2] || 0], [...b.slice(0, 2), b[2] || 0]] }, layer);
        const rect = (x, y, w, h, layer = 'architecture') => add('POLYLINE', { points: [[x, y, 0], [x + w, y, 0], [x + w, y + h, 0], [x, y + h, 0]], closed: true }, layer);
        const circle = (x, y, r, layer = 'furniture') => add('CIRCLE', { center: [x, y, 0], radius: r }, layer);
        const text = (x, y, t, h = 165, layer = 'annotation', align = 'left') => add('TEXT', { position: [x, y, 0], text: t, height: h, align }, layer);
        const dim = (a, b, offset, h = 145) => add('DIMENSION', { points: [[...a, 0], [...b, 0]], offset, textHeight: h }, 'dimensions');
        function wall(x, y, w, h) { rect(x, y, w, h); add('HATCH', { points: [[x, y, 0], [x + w, y, 0], [x + w, y + h, 0], [x, y + h, 0]], spacing: 110, angle: Math.PI / 4, pattern: 'ANSI31' }, 'hatch'); }
        function win(x1, y1, x2, y2) { const a = [x1, y1], b = [x2, y2]; if (y1 === y2) {
            for (const t of [35, 90, 145])
                line([x1, y1 + t], [x2, y2 + t], 'openings');
            line([x1, y1], [x1, y1 + 180], 'openings');
            line([x2, y2], [x2, y2 + 180], 'openings');
        }
        else {
            for (const t of [35, 90, 145])
                line([x1 + t, y1], [x2 + t, y2], 'openings');
            line([x1, y1], [x1 + 180, y1], 'openings');
            line([x2, y2], [x2 + 180, y2], 'openings');
        } }
        function door(x, y, size, rot = 0, swing = 1) { const mat = M.multiply(M.translation(x, y), M.rotation(rot)), a = K.Geo.transform({ type: 'LINE', points: [[0, 0, 0], [0, size * swing, 0]], layer: 'openings' }, mat), b = K.Geo.transform({ type: 'ARC', center: [0, 0, 0], radius: size, startAngle: swing > 0 ? 0 : -Math.PI / 2, endAngle: swing > 0 ? Math.PI / 2 : 0, layer: 'openings' }, mat); d.add(a); d.add(b); }
        // Envelope and wall openings, millimeters.
        for (const [x, w] of [[0, 1900], [2950, 4400], [10100, 1900]])
            wall(x, 0, w, 180);
        for (const [x, w] of [[0, 700], [3520, 900], [7600, 750], [11100, 900]])
            wall(x, 8320, w, 180);
        for (const [y, h] of [[0, 1050], [3450, 1950], [7700, 800]])
            wall(0, y, 180, h);
        for (const [y, h] of [[0, 650], [2850, 2000], [7350, 1150]])
            wall(11820, y, 180, h);
        wall(7420, 180, 180, 3180);
        wall(7420, 4360, 180, 3960);
        wall(7600, 4090, 1770, 180);
        wall(10370, 4090, 1450, 180);
        wall(4420, 4800, 180, 3520);
        wall(4600, 4620, 520, 180);
        wall(6920, 4620, 500, 180);
        wall(10020, 6370, 180, 1950);
        wall(10200, 6190, 740, 180);
        wall(11700, 6190, 120, 180);
        win(700, 8320, 3520, 8320);
        win(8350, 8320, 11100, 8320);
        win(0, 1050, 0, 3450);
        win(0, 5400, 0, 7700);
        win(11820, 650, 11820, 2850);
        win(11820, 4850, 11820, 7350);
        win(7350, 0, 10100, 0);
        door(1900, 90, 1050, 0, 1);
        door(7510, 3360, 1000, Math.PI / 2, -1);
        door(9370, 4180, 1000, 0, 1);
        door(10940, 6280, 760, 0, 1);
        // Courtyard glazing, mullions and sliding leaves.
        for (const y of [4640, 4690, 4760])
            line([5120, y], [6920, y], 'openings');
        line([5120, 4690], [6020, 4690], 'openings');
        line([6020, 4760], [6920, 4760], 'openings');
        rect(4660, 4900, 2620, 3190, 'hatch');
        for (let y = 5080; y < 8100; y += 380)
            line([4680, y], [7260, y], 'hatch');
        const landscape = d.addLayer('A-LANDSCAPE', '#73ae98').id;
        function tree(x, y, r) { circle(x, y, r, landscape); circle(x, y, r * .63, landscape); for (let i = 0; i < 9; i++) {
            const a = i / 9 * TAU;
            line([x + Math.cos(a) * r * .2, y + Math.sin(a) * r * .2], [x + Math.cos(a) * r * .85, y + Math.sin(a) * r * .85], landscape);
        } }
        tree(5900, 7040, 720);
        rect(4720, 4980, 500, 1280, landscape);
        for (let y = 5150; y < 6200; y += 240)
            circle(4970, y, 135, landscape);
        text(5920, 5360, 'COURTYARD', 165, 'annotation', 'center');
        text(5920, 5100, 'Open to sky', 110, 'hatch', 'center');
        // Living room furniture: sectional, low table, media bench and rug.
        rect(700, 720, 3470, 2850, 'hatch');
        rect(820, 2010, 2420, 900, 'furniture');
        rect(820, 1040, 900, 1870, 'furniture');
        rect(930, 2120, 2160, 670, 'furniture');
        for (const x of [1600, 2300])
            line([x, 2130], [x, 2780], 'furniture');
        rect(930, 1160, 660, 830, 'furniture');
        rect(2140, 1140, 1350, 690, 'furniture');
        rect(2240, 1240, 1150, 490, 'furniture');
        circle(3070, 1510, 140);
        rect(4270, 900, 370, 2380, 'furniture');
        rect(4320, 1390, 220, 1330, 'furniture');
        circle(3510, 2470, 345);
        circle(3510, 2470, 235);
        tree(6740, 770, 325);
        text(5450, 2150, 'LIVING / LOUNGE', 220, 'annotation', 'center');
        text(5450, 1810, '38.4 m²', 145, 'hatch', 'center');
        // Kitchen cabinets and dining setting.
        rect(240, 7210, 3990, 940, 'furniture');
        rect(240, 4790, 780, 2420, 'furniture');
        for (let x = 900; x < 4200; x += 680)
            line([x, 7250], [x, 8110], 'furniture');
        rect(1440, 7390, 1270, 560, 'furniture');
        rect(1520, 7470, 505, 400, 'furniture');
        rect(2110, 7470, 505, 400, 'furniture');
        circle(2070, 8030, 65);
        rect(3100, 7370, 920, 620, 'furniture');
        for (const x of [3320, 3760])
            for (const y of [7510, 7850])
                circle(x, y, 130);
        rect(360, 4880, 540, 1230, 'furniture');
        line([360, 5500], [900, 5500], 'furniture');
        line([810, 5310], [810, 5440], 'furniture');
        rect(1980, 5110, 1790, 980, 'furniture');
        rect(2060, 5190, 1630, 820, 'furniture');
        for (const x of [2230, 3050]) {
            rect(x, 4700, 540, 330, 'furniture');
            rect(x, 6150, 540, 330, 'furniture');
        }
        rect(1540, 5350, 360, 520, 'furniture');
        rect(3840, 5350, 360, 520, 'furniture');
        text(2450, 6650, 'KITCHEN / DINING', 180, 'annotation', 'center');
        // Primary bedroom and fitted wardrobe.
        rect(7930, 5430, 1870, 2450, 'furniture');
        rect(8000, 5530, 1730, 2200, 'furniture');
        rect(8020, 7150, 800, 570, 'furniture');
        rect(8900, 7150, 800, 570, 'furniture');
        line([8000, 7010], [9730, 7010], 'furniture');
        rect(7730, 7430, 190, 460, 'furniture');
        rect(9780, 7430, 190, 460, 'furniture');
        rect(7780, 4470, 2060, 520, 'furniture');
        for (let x = 8160; x < 9800; x += 400)
            line([x, 4490], [x, 4960], 'furniture');
        text(8770, 5140, 'BEDROOM', 180, 'annotation', 'center');
        // Bathroom fittings.
        rect(10380, 7260, 1220, 850, 'furniture');
        rect(10470, 7350, 1040, 660, 'furniture');
        add('ELLIPSE', { center: [10700, 6760, 0], rx: 270, ry: 345 }, 'furniture');
        rect(10420, 6810, 560, 340, 'furniture');
        rect(11250, 6650, 450, 500, 'furniture');
        circle(11475, 6900, 155);
        text(11020, 6460, 'BATH', 135, 'annotation', 'center');
        // Studio / second bedroom.
        rect(9260, 880, 2300, 780, 'furniture');
        rect(9370, 960, 1200, 490, 'furniture');
        rect(10770, 1000, 550, 370, 'furniture');
        line([10820, 1180], [11260, 1180], 'furniture');
        rect(8350, 2040, 2470, 1370, 'hatch');
        rect(8480, 2230, 1810, 880, 'furniture');
        rect(8580, 2320, 800, 680, 'furniture');
        rect(9450, 2320, 740, 680, 'furniture');
        circle(10500, 2060, 230);
        rect(7790, 200, 730, 2490, 'furniture');
        text(10000, 3610, 'STUDIO', 205, 'annotation', 'center');
        text(10000, 3310, '16.8 m²', 140, 'hatch', 'center');
        // Entrance steps and the terrace.
        for (let y = -320; y > -1100; y -= 250)
            line([1720, y], [3130, y], 'hatch');
        line([1720, -70], [1720, -900], 'hatch');
        line([3130, -70], [3130, -900], 'hatch');
        rect(7250, -1480, 3050, 1250, 'hatch');
        for (let y = -1310; y < -230; y += 220)
            line([7270, y], [10280, y], 'hatch');
        // Set-out axes, grid bubbles, and chain dimensions.
        for (const [x, label] of [[90, '1'], [4510, '2'], [7510, '3'], [11910, '4']]) {
            line([x, -560], [x, 9940], 'construction');
            circle(x, 10060, 150, 'construction');
            text(x, 10005, label, 150, 'construction', 'center');
        }
        for (const [y, label] of [[90, 'A'], [4180, 'B'], [8410, 'C']]) {
            line([-720, y], [12450, y], 'construction');
            circle(-930, y, 150, 'construction');
            text(-930, y - 55, label, 150, 'construction', 'center');
        }
        dim([0, 8500], [4420, 8500], 550, 140);
        dim([4420, 8500], [7600, 8500], 550, 140);
        dim([7600, 8500], [12000, 8500], 550, 140);
        dim([0, 8500], [12000, 8500], 1110, 160);
        dim([0, 0], [12000, 0], -1190, 165);
        dim([12000, 0], [12000, 8500], -720, 160);
        dim([0, 0], [0, 4180], -620, 140);
        dim([0, 4180], [0, 8500], -620, 140);
        // North arrow and drawing title, all editable entities.
        const nx = 13500, ny = 7000;
        line([nx, ny], [nx, ny + 1180], 'annotation');
        add('POLYLINE', { points: [[nx - 180, ny + 600, 0], [nx, ny + 1180, 0], [nx + 180, ny + 600, 0]], closed: true }, 'annotation');
        text(nx, ny + 1480, 'N', 270, 'annotation', 'center');
        text(0, -2100, 'GROUND FLOOR PLAN', 270, 'annotation');
        text(0, -2410, 'COURTYARD HOUSE  /  DESIGN STUDY', 135, 'hatch');
        text(12000, -2090, 'A–101', 330, 'annotation', 'right');
        text(12000, -2390, 'Dimensions in millimeters', 130, 'hatch', 'right');
        for (let i = 0; i < 4; i++) {
            rect(8300 + i * 650, -2040, 650, 95, 'annotation');
            if (i % 2 === 0)
                add('HATCH', { points: [[8300 + i * 650, -2040, 0], [8950 + i * 650, -2040, 0], [8950 + i * 650, -1945, 0], [8300 + i * 650, -1945, 0]], pattern: 'solid' }, 'hatch');
        }
        text(8300, -2280, '0        1        2 m', 120, 'hatch');
        d.reindex();
        d.dirty = false;
        d.currentLayer = 'architecture';
        return d;
    }
    function fixture() {
        const d = new K.Drawing('Precision Fixture · Assembly');
        const names = [['0', '#dce4ed'], ['M-BASE', '#bdc8d0'], ['M-STRUCTURE', '#509bab'], ['M-HARDWARE', '#d1b579'], ['M-DIMENSION', '#6fc2b4'], ['M-ANNOTATION', '#b5c5d3'], ['M-DETAIL', '#6c8298'], ['M-CENTER', '#9a8164']];
        d.layers.forEach((l, i) => { l.name = names[i][0]; l.color = names[i][1]; });
        d.reindex();
        const mesh = (m, layer = 'architecture', name) => d.add({ ...m, layer, name: name || m.primitive });
        const plateProfile = [[-114, -85, 0], [114, -85, 0], [120, -79, 0], [120, 79, 0], [114, 85, 0], [-114, 85, 0], [-120, 79, 0], [-120, -79, 0]];
        let plate = G.extrude(plateProfile, 12);
        plate.primitive = 'Chamfered base plate';
        // True mesh cuts, not painted circles.
        for (const [x, y] of [[-96, -61], [96, -61], [-96, 61], [96, 61]])
            plate = K.CSG.boolean(plate, G.cylinder([x, y, -2], 5.5, 16, 28), 'subtract');
        mesh(plate, 'architecture', 'Drilled base plate');
        mesh(G.box([-101, -39, 12], 34, 78, 13), 'hatch', 'Left saddle');
        mesh(G.box([67, -39, 12], 34, 78, 13), 'hatch', 'Right saddle');
        const profile = [[-93, -22, 25], [-66, -22, 25], [-27, -22, 63], [-27, -22, 87], [-47, -22, 87], [-93, -22, 41]];
        const brace = G.extrude(profile, 44, [0, 1, 0]);
        mesh(brace, 'openings', 'Left support');
        mesh(G.transform(brace, M.scale(-1, 1, 1)), 'openings', 'Right support');
        mesh(G.cylinder([0, 0, 12], 43, 12, 64), 'hatch', 'Lower flange');
        let hub = G.cylinder([0, 0, 24], 30, 52, 64);
        hub = K.CSG.boolean(hub, G.cylinder([0, 0, 21], 12, 60, 48), 'subtract');
        mesh(hub, 'openings', 'Bored central hub');
        let collar = K.CSG.boolean(G.cylinder([0, 0, 76], 35, 9, 64), G.cylinder([0, 0, 73], 12, 15, 48), 'subtract');
        mesh(collar, 'architecture', 'Top collar');
        for (const [x, y] of [[-96, -61], [96, -61], [-96, 61], [96, 61]]) {
            mesh(G.cylinder([x, y, 12], 10, 2, 40), 'hatch', 'Mounting washer');
            mesh(G.cylinder([x, y, 14], 7.6, 6.5, 6), 'furniture', 'Hex fastener');
            d.add('LINE', { points: [[x - 3.5, y, 20.6], [x + 3.5, y, 20.6]], layer: 'hatch' });
        }
        for (const x of [-81, 81])
            for (const y of [-27, 27]) {
                mesh(G.cylinder([x, y, 25], 5.8, 4, 6), 'furniture', 'Support bolt');
            }
        for (let i = 0; i < 4; i++) {
            const a = i * TAU / 4 + Math.PI / 4, x = Math.cos(a) * 23, y = Math.sin(a) * 23;
            mesh(G.cylinder([x, y, 85], 4.7, 4, 6), 'furniture', 'Collar bolt');
        }
        const screw = G.transform(G.cylinder([0, 0, 0], 6.5, 45, 40), M.multiply(M.translation(0, -24, 48), M.rotation(Math.PI / 2, [1, 0, 0])));
        mesh(screw, 'architecture', 'Adjustment shaft');
        const knob = G.transform(G.cylinder([0, 0, 0], 12, 9, 12), M.multiply(M.translation(0, -69, 48), M.rotation(Math.PI / 2, [1, 0, 0])));
        mesh(knob, 'furniture', 'Adjustment knob');
        d.add('LINE', { points: [[-142, 0, -.1], [142, 0, -.1]], layer: 'construction' });
        d.add('LINE', { points: [[0, -105, -.1], [0, 106, -.1]], layer: 'construction' });
        d.add('DIMENSION', { points: [[-120, -85, 0], [120, -85, 0]], offset: -31, textHeight: 5, layer: 'dimensions' });
        d.add('DIMENSION', { points: [[120, -85, 0], [120, 85, 0]], offset: -25, textHeight: 5, layer: 'dimensions' });
        d.add('TEXT', { position: [-120, -137, 0], height: 6.6, text: 'PRECISION FIXTURE', layer: 'annotation' });
        d.add('TEXT', { position: [-120, -148, 0], height: 3.8, text: 'EDITABLE MESH ASSEMBLY  /  DIMENSIONS IN mm', layer: 'hatch' });
        d.reindex();
        d.currentLayer = 'openings';
        d.dirty = false;
        return d;
    }
    function benchmark(count = 10000) { const d = new K.Drawing('Render stress · ' + count.toLocaleString() + ' lines'); d.layers = d.layers.slice(0, 1); d.currentLayer = '0'; const cols = Math.ceil(Math.sqrt(count)), spacing = 12; for (let i = 0; i < count; i++) {
        const x = i % cols * spacing, y = Math.floor(i / cols) * spacing;
        d.add('LINE', { points: [[x, y, 0], [x + 9, y + 9, Math.sin(i * .03) * 5]], layer: '0', color: i % 11 === 0 ? '#5ac6d2' : 'bylayer' });
    } d.reindex(); d.dirty = false; return d; }
    K.Examples = { courtyard, fixture, benchmark };
})(typeof window !== 'undefined' ? window : globalThis);
