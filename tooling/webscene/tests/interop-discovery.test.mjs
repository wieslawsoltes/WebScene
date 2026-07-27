import test from 'node:test';
import assert from 'node:assert/strict';
import { execFile } from 'node:child_process';
import { mkdtemp, readFile, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';
import { promisify } from 'node:util';
import {
  configureInteropPolicy,
  createInteropCoverageReport,
  discoverInteropSurface,
  scaffoldInteropPolicy
} from '../interop-discovery.mjs';

const execFileAsync = promisify(execFile);

test('discovers a TradingView-shaped public API from TypeScript declarations', async () => {
  const root = await mkdtemp(join(tmpdir(), 'webscene-interop-'));
  const declarations = join(root, 'charting_library.d.ts');
  await writeFile(declarations, `
    declare namespace Charting_Library {
      type ResolutionString = string;
      interface IChartingLibraryWidget {
        activeChart(): IChartWidgetApi;
        getLanguage(): string;
      }
      interface IChartWidgetApi {
        symbol(): string;
        resolution(): ResolutionString;
        setSymbol(symbol: string): Promise<boolean>;
        setZoomEnabled(enabled: boolean): void;
      }
    }
  `);

  const result = await discoverInteropSurface(
    [declarations],
    ['Charting_Library.IChartingLibraryWidget', 'Charting_Library.IChartWidgetApi']);

  assert.equal(result.schemaVersion, '1.0');
  assert.equal(result.apiFingerprint.length, 64);
  assert.equal(result.declarations[0].sha256.length, 64);
  const widget = result.roots[0];
  assert.equal(widget.qualifiedName, 'Charting_Library.IChartingLibraryWidget');
  assert.deepEqual(widget.methods.map(method => method.name), ['activeChart', 'getLanguage']);
  const chart = result.roots[1];
  assert.equal(chart.methods.find(method => method.name === 'setSymbol').returns.kind, 'promise');
  assert.equal(
    chart.methods.find(method => method.name === 'setZoomEnabled').parameters[0].type.kind,
    'boolean');

  const policy = scaffoldInteropPolicy(result, 'Demo.TradingView');
  assert.equal(policy.namespace, 'Demo.TradingView');
  assert.equal(policy.apiFingerprint, result.apiFingerprint);
  assert.equal(policy.bindings[0].name, 'ChartingLibraryWidget');
  assert.equal(policy.bindings[0].methods[0].include, false);
  assert.equal(policy.bindings[1].methods.find(
    method => method.source === 'setSymbol').name, 'SetSymbolAsync');
});

test('committed TradingView proof manifest matches its declaration fixture', async () => {
  const sample = new URL('../../../samples/TradingViewInterop.Generated/', import.meta.url);
  const declarations = new URL('TradingViewApi.fixture.d.ts', sample);
  const expected = JSON.parse(await readFile(
    new URL('TradingViewApi.webscene-interop-api.json', sample),
    'utf8'));
  const actual = await discoverInteropSurface(
    [fileURLToPath(declarations)],
    [
      'Charting_Library.widget',
      'Charting_Library.IChartWidgetApi',
      'Charting_Library.IOrderLineAdapter',
      'Charting_Library.IPositionLineAdapter',
      'Charting_Library.IExecutionLineAdapter',
      'Charting_Library.IWatchedValue',
      'Broker.IBrokerConnectionAdapterHost'
    ]);

  assert.deepEqual(actual, expected);
});

test('discovers the complete TradingView-shaped type graph without hand-picked roots', async () => {
  const declarations = new URL(
    '../../../samples/TradingViewInterop.Generated/TradingViewApi.fixture.d.ts',
    import.meta.url);
  const result = await discoverInteropSurface([fileURLToPath(declarations)]);

  assert.ok(result.types.length >= 30);
  assert.equal(result.roots.length, result.types.length);
  const datafeed = result.types.find(
    type => type.qualifiedName === 'Datafeed.IDatafeedChartApi');
  assert.deepEqual(
    datafeed.methods.map(method => method.name),
    [
      'getBars',
      'getQuotes',
      'onReady',
      'resolveSymbol',
      'searchSymbols',
      'subscribeBars',
      'subscribeQuotes',
      'unsubscribeBars',
      'unsubscribeQuotes'
    ]);
  const getBars = datafeed.methods.find(method => method.name === 'getBars');
  assert.equal(getBars.parameters[3].type.kind, 'callback');
  assert.equal(
    getBars.parameters[3].type.signatures[0].parameters[0].type.kind,
    'array');

  const broker = result.types.find(
    type => type.qualifiedName === 'Broker.IBrokerTerminal');
  assert.equal(
    broker.methods.find(method => method.name === 'orders').returns.kind,
    'promise');
  assert.equal(
    broker.methods.find(method => method.name === 'accountManagerInfo').returns.kind,
    'reference');
  const widgetConstructor = result.types.find(
    type => type.qualifiedName === 'Charting_Library.widget').constructors[0];
  assert.equal(
    widgetConstructor.parameters[0].type.qualifiedName,
    'Charting_Library.ChartingLibraryWidgetOptions');
  const watchedValue = result.types.find(
    type => type.qualifiedName === 'Charting_Library.IWatchedValue');
  assert.deepEqual(watchedValue.typeParameters, ['T']);

  const policy = scaffoldInteropPolicy(result, 'Demo.ComprehensiveTradingView');
  assert.ok(policy.models.length > 0);
  assert.ok(policy.adapters.some(
    adapter => adapter.source === 'Datafeed.IDatafeedChartApi'));
  configureInteropPolicy(policy, {
    includeAllModels: true,
    includeAllProxies: true,
    proxyRoots: ['IChartWidgetApi'],
    adapterRoots: ['IDatafeedChartApi']
  });
  assert.ok(policy.models.every(model => model.include));
  assert.ok(policy.bindings.every(binding => binding.include));
  assert.ok(policy.bindings.find(
    binding => binding.source === 'Charting_Library.IChartWidgetApi').include);
  assert.ok(policy.adapters.find(
    adapter => adapter.source === 'Datafeed.IDatafeedChartApi').methods.every(
      method => method.include));

  const coverage = createInteropCoverageReport(result);
  assert.equal(coverage.namedTypes, result.types.length);
  assert.ok(coverage.methods >= 30);
  assert.ok(coverage.callbacks >= 10);
  assert.ok(coverage.promises >= 5);
  assert.deepEqual(coverage.fallbacks, []);
});

test('follows imported declaration files and hashes the complete public graph', async () => {
  const root = await mkdtemp(join(tmpdir(), 'webscene-interop-imports-'));
  const entry = join(root, 'index.d.ts');
  await writeFile(entry, `import './widget.d.ts';`);
  await writeFile(join(root, 'widget.d.ts'), `
    declare namespace ImportedLibrary {
      interface WidgetApi { value(): number; }
    }
  `);

  const result = await discoverInteropSurface(
    [entry],
    ['ImportedLibrary.WidgetApi']);

  assert.equal(result.roots[0].qualifiedName, 'ImportedLibrary.WidgetApi');
  assert.deepEqual(
    result.declarations.map(item => item.fileName),
    ['index.d.ts', 'widget.d.ts']);
});

test('separates exported roots from private dependency declarations', async () => {
  const root = await mkdtemp(join(tmpdir(), 'webscene-interop-exports-'));
  const entry = join(root, 'index.d.ts');
  await writeFile(entry, `
    export { PublicApi, createApi } from './library.d.ts';
  `);
  await writeFile(join(root, 'library.d.ts'), `
    declare interface PrivateOptions { token: string; }
    export interface PublicApi {
      configure(options: PrivateOptions): void;
    }
    export function createApi(options: PrivateOptions): PublicApi;
    declare function privateHelper(): void;
  `);

  const result = await discoverInteropSurface([entry]);

  assert.ok(result.types.some(
    type => type.qualifiedName === 'PrivateOptions'));
  assert.deepEqual(
    result.roots.map(type => type.qualifiedName),
    ['PublicApi']);
  assert.deepEqual(
    result.functions.map(method => method.qualifiedName),
    ['createApi']);
});

test('strict discovery fails when a declaration requires an untyped fallback', async () => {
  const root = await mkdtemp(join(tmpdir(), 'webscene-interop-strict-'));
  const declarations = join(root, 'unsupported.d.ts');
  const report = join(root, 'coverage.json');
  await writeFile(declarations, `
    declare namespace StrictLibrary {
      type EmptyObject = {};
    }
  `);

  await assert.rejects(
    execFileAsync(process.execPath, [
      fileURLToPath(new URL('../interop-discover.mjs', import.meta.url)),
      '--declarations',
      declarations,
      '--report-output',
      report,
      '--fail-on-fallbacks'
    ]),
    error => {
      assert.match(error.stderr, /1 unsupported type shape/);
      assert.match(error.stderr, /StrictLibrary\.EmptyObject\.aliasTarget/);
      return true;
    });

  const coverage = JSON.parse(await readFile(report, 'utf8'));
  assert.equal(coverage.fallbacks.length, 1);
});

test('expands standard mapped utility types into serializable object shapes', async () => {
  const root = await mkdtemp(join(tmpdir(), 'webscene-interop-mapped-'));
  const declarations = join(root, 'mapped.d.ts');
  await writeFile(declarations, `
    declare namespace MappedLibrary {
      interface Quotes {
        ask?: number;
        bid?: number;
        last: number;
      }
      type AskBid = Required<Pick<Quotes, "ask" | "bid">>;
      type MaybeQuotes = Partial<Quotes>;
      type QuotesWithoutLast = Omit<Quotes, "last">;
    }
  `);

  const result = await discoverInteropSurface([declarations]);
  const askBid = result.types.find(
    type => type.qualifiedName === 'MappedLibrary.AskBid').aliasTarget;
  assert.equal(askBid.kind, 'inlineObject');
  assert.deepEqual(
    askBid.properties.map(property => [property.name, property.optional]),
    [['ask', false], ['bid', false]]);

  const maybeQuotes = result.types.find(
    type => type.qualifiedName === 'MappedLibrary.MaybeQuotes').aliasTarget;
  assert.equal(maybeQuotes.kind, 'inlineObject');
  assert.ok(maybeQuotes.properties.every(property => property.optional));

  const withoutLast = result.types.find(
    type => type.qualifiedName === 'MappedLibrary.QuotesWithoutLast').aliasTarget;
  assert.equal(withoutLast.kind, 'inlineObject');
  assert.deepEqual(
    withoutLast.properties.map(property => property.name),
    ['ask', 'bid']);
  assert.deepEqual(createInteropCoverageReport(result).fallbacks, []);
});

test('scaffolds collision-free names and one canonical inbound overload', async () => {
  const declarations = new URL(
    '../../../tests/WebScene.JavaScript.Interop.Generator.Compile/GeneratorCapabilities.fixture.d.ts',
    import.meta.url);
  const result = await discoverInteropSurface([fileURLToPath(declarations)]);
  const policy = configureInteropPolicy(
    scaffoldInteropPolicy(result, 'GeneratorCapabilities.Generated'),
    {
      includeAllModels: true,
      includeAllProxies: true,
      includeAllAdapters: true,
      includeAllFunctions: true,
      includeAllGlobals: true
    });

  const generatedNames = [
    ...policy.models.map(model => model.name),
    ...policy.bindings.map(binding => binding.name),
    ...policy.adapters.map(adapter => adapter.name)
  ];
  assert.equal(new Set(generatedNames).size, generatedNames.length);

  const binding = policy.bindings.find(
    item => item.source === 'GeneratorCapabilities.Widget');
  assert.deepEqual(
    binding.methods
      .filter(method => method.source === 'update')
      .map(method => method.name),
    ['UpdateAsync', 'UpdateOverload2Async']);
  assert.equal(
    binding.methods.find(method => method.source === 'maybeAsync').name,
    'MaybeAsync');
  assert.equal(
    binding.methods.find(method => method.source === 'configure')
      .omitOptionalParameters,
    false);
  assert.equal(
    new Set(binding.methods.map(method => method.name)).size,
    binding.methods.length);

  const controller = policy.bindings.find(
    item => item.source === 'GeneratorCapabilities.Controller');
  assert.deepEqual(
    controller.constructors.map(constructor => [
      constructor.name,
      constructor.overload,
      constructor.globalName
    ]),
    [
      ['CreateAsync', 0, 'GeneratorCapabilities.Controller'],
      ['CreateOverload2Async', 1, 'GeneratorCapabilities.Controller']
    ]);

  const options = result.types.find(
    item => item.qualifiedName === 'GeneratorCapabilities.WidgetOptions');
  assert.ok(options.properties.some(
    property => property.name === 'formatter'
      && property.type.kind === 'callback'));
  assert.ok(options.properties.some(
    property => property.name === 'namedFormatter'
      && property.type.kind === 'callback'
      && property.type.qualifiedName === 'GeneratorCapabilities.Formatter'));
  assert.ok(!options.methods.some(
    method => method.name === 'formatter'
      || method.name === 'namedFormatter'));
  assert.ok(!policy.models.some(
    model => model.source === 'GeneratorCapabilities.Formatter'));
  assert.ok(!policy.models.some(
    model => model.source === 'GeneratorCapabilities.Disposer'
      || model.source === 'GeneratorCapabilities.MaybeDisposer'
      || model.source === 'GeneratorCapabilities.WidgetHandle'));
  assert.deepEqual(
    result.functions.map(method => method.qualifiedName),
    [
      'GeneratorCapabilities.Controller.fromId',
      'GeneratorCapabilities.createDisposer',
      'GeneratorCapabilities.createWidget',
      'GeneratorCapabilities.listWidgets',
      'GeneratorCapabilities.loadDisposer',
      'GeneratorCapabilities.loadWidget',
      'GeneratorCapabilities.loadWidgets',
      'GeneratorCapabilities.maybeDisposer',
      'GeneratorCapabilities.normalizeLabel',
      'GeneratorCapabilities.widgetOrLabel'
    ]);
  assert.ok(policy.functions.every(method => method.include));
  assert.deepEqual(
    result.globals.map(property => property.qualifiedName),
    [
      'GeneratorCapabilities.Controller.current',
      'GeneratorCapabilities.Controller.ready',
      'GeneratorCapabilities.Controller.version',
      'GeneratorCapabilities.currentController',
      'GeneratorCapabilities.libraryVersion',
      'GeneratorCapabilities.readyController',
      'GeneratorCapabilities.widgets'
    ]);
  assert.ok(policy.globalProperties.every(property => property.include));
  assert.equal(
    policy.functions.find(
      method => method.source === 'GeneratorCapabilities.loadWidget').name,
    'LoadWidgetAsync');

  const adapter = policy.adapters.find(
    item => item.source === 'GeneratorCapabilities.Widget');
  assert.deepEqual(
    adapter.methods
      .filter(method => method.source === 'update')
      .map(method => method.include),
    [true, false]);
});
