import ts from 'typescript';
import { createHash } from 'node:crypto';
import { readFile } from 'node:fs/promises';
import { basename, resolve } from 'node:path';

export async function discoverInteropSurface(declarationPaths, rootNames = []) {
  if (!Array.isArray(declarationPaths) || declarationPaths.length === 0) {
    throw new Error('At least one TypeScript declaration file is required.');
  }
  if (!Array.isArray(rootNames)) {
    throw new Error('Public API roots must be an array.');
  }

  const paths = declarationPaths.map(path => resolve(path));
  const program = ts.createProgram(paths, {
    noEmit: true,
    skipLibCheck: true,
    strictNullChecks: true,
    target: ts.ScriptTarget.ES2022,
    moduleResolution: ts.ModuleResolutionKind.Bundler
  });
  const diagnostics = ts.getPreEmitDiagnostics(program);
  if (diagnostics.some(item => item.category === ts.DiagnosticCategory.Error)) {
    throw new Error(formatDiagnostics(diagnostics));
  }

  const checker = program.getTypeChecker();
  const declarationFiles = program.getSourceFiles()
    .filter(source =>
      source.isDeclarationFile
      && !program.isSourceFileDefaultLibrary(source))
    .sort((left, right) => left.fileName.localeCompare(right.fileName));
  const declarations = collectNamedDeclarations(declarationFiles, checker);
  const publicDeclarations = collectPublicDeclarations(
    declarationFiles,
    checker,
    declarations,
    new Set(paths));
  const selectedSymbols = rootNames.length === 0
    ? publicDeclarations.symbols
    : rootNames.map(name => {
    const candidates = declarations.get(name) ?? declarations.get(name.split('.').at(-1));
    if (!candidates?.length) {
      throw new Error(`Public API root '${name}' was not found in the declaration files.`);
    }
    if (candidates.length > 1) {
      throw new Error(
        `Public API root '${name}' is ambiguous: ${candidates.map(qualifiedName).join(', ')}.`);
    }
    return candidates[0];
  });
  const roots = selectedSymbols
    .map(symbol => describeDeclaration(symbol, checker))
    .sort((left, right) => left.qualifiedName.localeCompare(right.qualifiedName));
  const types = declarations.symbols
    .map(symbol => describeDeclaration(symbol, checker))
    .sort((left, right) => left.qualifiedName.localeCompare(right.qualifiedName));
  const functions = [
    ...publicDeclarations.functionSymbols
      .flatMap(symbol => describeFunction(symbol, checker)),
    ...publicDeclarations.symbols
      .flatMap(symbol => describeStaticFunctions(symbol, checker))
  ]
    .sort((left, right) =>
      left.qualifiedName.localeCompare(right.qualifiedName)
      || left.overload - right.overload);
  const globals = [
    ...publicDeclarations.valueSymbols
      .map(symbol => describeGlobalValue(symbol, checker)),
    ...publicDeclarations.symbols
      .flatMap(symbol => describeStaticValues(symbol, checker))
  ].sort((left, right) =>
    left.qualifiedName.localeCompare(right.qualifiedName));
  const files = await Promise.all(declarationFiles.map(async sourceFile => {
    const content = (await readFile(sourceFile.fileName, 'utf8'))
      .replace(/\r\n?/g, '\n');
    return {
      fileName: basename(sourceFile.fileName),
      sha256: createHash('sha256').update(content).digest('hex')
    };
  }));
  const apiFingerprint = createHash('sha256')
    .update(JSON.stringify({
      typescriptVersion: ts.version,
      declarations: files.map(file => [file.fileName, file.sha256])
    }))
    .digest('hex');

  return {
    schemaVersion: '1.0',
    generator: 'webscene-interop-discover',
    typescriptVersion: ts.version,
    apiFingerprint,
    declarations: files,
    types,
    roots,
    functions,
    globals
  };
}

export function scaffoldInteropPolicy(api, namespace = 'WebScene.Interop.Generated') {
  if (!api || api.schemaVersion !== '1.0' || !Array.isArray(api.roots)) {
    throw new Error('A schemaVersion 1.0 interop API manifest is required.');
  }
  if (!/^[A-Za-z_][A-Za-z0-9_.]*$/.test(namespace)) {
    throw new Error(`'${namespace}' is not a valid .NET namespace.`);
  }

  const typesByName = new Map();
  for (const type of api.types) {
    typesByName.set(type.name, type);
    typesByName.set(type.qualifiedName, type);
  }

  const policy = {
    schemaVersion: '1.0',
    api: null,
    apiFingerprint: api.apiFingerprint,
    namespace,
    typeMappings: {},
    functionsClassName: 'JavaScriptGlobals',
    functions: (api.functions ?? []).map(method => ({
      source: method.qualifiedName,
      overload: method.overload,
      globalName: method.qualifiedName,
      name: suggestedMethodName(method, true),
      include: false,
      omitOptionalParameters: false
    })),
    globalProperties: (api.globals ?? []).map(property => ({
      source: property.qualifiedName,
      globalName: property.qualifiedName,
      getterName: `Get${pascalCase(property.name)}Async`,
      include: false
    })),
    models: api.types
      .filter(type => type.kind === 'enum'
        || (type.kind === 'typeAlias'
          && isSerializableAliasModel(type, typesByName))
        || (type.kind === 'interface'
          && type.methods.length === 0
          && type.callSignatures.length === 0)
        || (type.properties.length > 0
          && type.methods.length === 0
          && type.callSignatures.length === 0))
      .map(type => ({
        source: type.qualifiedName,
        name: suggestedTypeName(type.name),
        include: false
      })),
    adapters: api.roots
      .filter(root => root.methods.length > 0)
      .map(root => ({
        source: root.qualifiedName,
        name: `${suggestedTypeName(root.name)}Adapter`,
        include: false,
        methods: root.methods.map(method => ({
          source: method.name,
          overload: method.overload,
          name: suggestedMethodName(
            method,
            method.returns.kind === 'void' || containsPromise(method.returns)),
          include: false
        }))
      })),
    bindings: api.roots
      .filter(root => root.kind === 'interface' || root.kind === 'class')
      .filter(root => root.methods.length > 0
        || root.constructors.length > 0)
      .map(root => ({
      source: root.qualifiedName,
      name: suggestedTypeName(root.name),
      include: false,
      constructor: null,
      constructors: root.constructors.map((_, overload) => ({
        globalName: root.qualifiedName,
        name: overload === 0 ? 'CreateAsync' : `CreateOverload${overload + 1}Async`,
        overload
      })),
      properties: root.properties.map(property => ({
        source: property.name,
        include: false,
        getterName: `Get${pascalCase(property.name)}Async`,
        setterName: property.readonly
          ? null
          : root.methods.some(method =>
            `${pascalCase(method.name)}Async` === `Set${pascalCase(property.name)}Async`)
            ? `Set${pascalCase(property.name)}PropertyAsync`
            : `Set${pascalCase(property.name)}Async`
      })),
      methods: root.methods.map(method => ({
        source: method.name,
        overload: method.overload,
        name: suggestedMethodName(method, true),
        include: false,
        omitOptionalParameters: false
      }))
      }))
  };
  assignUniquePolicyNames(policy);
  assignUniqueMemberNames(policy);
  return policy;
}

function isSerializableAliasModel(type, typesByName, seen = new Set()) {
  if (!type.aliasTarget || seen.has(type.qualifiedName)) return false;
  const nextSeen = new Set(seen);
  nextSeen.add(type.qualifiedName);
  return isSerializableAliasTarget(type.aliasTarget, typesByName, nextSeen);
}

function isSerializableAliasTarget(type, typesByName, seen) {
  switch (type.kind) {
    case 'callback':
    case 'promise':
      return false;
    case 'reference': {
      if (liveReferenceNames.has(type.name)) return false;
      const declaration = typesByName.get(type.qualifiedName)
        ?? typesByName.get(type.name);
      if (!declaration) return true;
      if (declaration.kind === 'typeAlias') {
        return isSerializableAliasModel(declaration, typesByName, seen);
      }
      return declaration.callSignatures.length === 0
        && declaration.methods.length === 0
        && declaration.constructors.length === 0;
    }
    case 'union':
    case 'intersection':
      return type.types.every(candidate =>
        isSerializableAliasTarget(candidate, typesByName, seen));
    case 'array':
      return isSerializableAliasTarget(type.element, typesByName, seen);
    case 'tuple':
      return type.elements.every(element =>
        isSerializableAliasTarget(element, typesByName, seen));
    case 'inlineObject':
      return type.properties.every(property =>
        isSerializableAliasTarget(property.type, typesByName, seen));
    default:
      return true;
  }
}

const liveReferenceNames = new Set([
  'Function',
  'Map',
  'ReadonlyMap',
  'Set',
  'ReadonlySet',
  'HTMLElement',
  'Element',
  'EventTarget',
  'Window',
  'Document',
  'Node',
  'Event',
  'ClipboardEvent',
  'KeyboardEvent',
  'MouseEvent',
  'Worker',
  'RegExp',
  'ArrayBuffer',
  'SharedArrayBuffer',
  'DataView',
  'Int8Array',
  'Uint8Array',
  'Uint8ClampedArray',
  'Int16Array',
  'Uint16Array',
  'Int32Array',
  'Uint32Array',
  'Float32Array',
  'Float64Array',
  'BigInt64Array',
  'BigUint64Array'
]);

export function configureInteropPolicy(
  policy,
  {
    includeAllModels = false,
    includeAllProxies = false,
    includeAllAdapters = false,
    includeAllFunctions = false,
    includeAllGlobals = false,
    proxyRoots = [],
    adapterRoots = [],
    functionRoots = [],
    globalRoots = []
  } = {}) {
  const proxySet = new Set(proxyRoots);
  const adapterSet = new Set(adapterRoots);
  const functionSet = new Set(functionRoots);
  const globalSet = new Set(globalRoots);
  if (includeAllModels) {
    for (const model of policy.models ?? []) model.include = true;
  }
  for (const binding of policy.bindings ?? []) {
    if (!includeAllProxies && !matchesSelection(binding.source, proxySet)) continue;
    binding.include = true;
    for (const method of binding.methods ?? []) method.include = true;
    for (const property of binding.properties ?? []) property.include = true;
  }
  for (const adapter of policy.adapters ?? []) {
    if (!includeAllAdapters && !matchesSelection(adapter.source, adapterSet)) continue;
    adapter.include = true;
    // JavaScript has one runtime member for a TypeScript overload set. An
    // inbound adapter therefore exposes one canonical signature per member;
    // a reviewed policy may select a different overload explicitly.
    const selectedMembers = new Set();
    for (const method of adapter.methods ?? []) {
      method.include = !selectedMembers.has(method.source);
      selectedMembers.add(method.source);
    }
  }
  for (const method of policy.functions ?? []) {
    if (includeAllFunctions || matchesSelection(method.source, functionSet)) {
      method.include = true;
    }
  }
  for (const property of policy.globalProperties ?? []) {
    if (includeAllGlobals || matchesSelection(property.source, globalSet)) {
      property.include = true;
    }
  }
  return policy;
}

export function createInteropCoverageReport(api) {
  if (!api || !Array.isArray(api.types)) {
    throw new Error('An interop API manifest with a complete type graph is required.');
  }
  const kinds = new Map();
  const fallbacks = [];
  let methods = 0;
  let properties = 0;
  let constructors = 0;
  let callbacks = 0;
  let promises = 0;
  let functions = 0;
  let globals = 0;
  for (const declaration of api.types) {
    methods += declaration.methods?.length ?? 0;
    properties += declaration.properties?.length ?? 0;
    constructors += declaration.constructors?.length ?? 0;
    visit(declaration, declaration.qualifiedName);
  }
  for (const method of api.functions ?? []) {
    functions++;
    visit(method, method.qualifiedName);
  }
  for (const property of api.globals ?? []) {
    globals++;
    visit(property, property.qualifiedName);
  }
  return {
    schemaVersion: '1.0',
    declarations: api.declarations.length,
    namedTypes: api.types.length,
    methods,
    properties,
    constructors,
    callbacks,
    promises,
    functions,
    globals,
    typeKinds: Object.fromEntries(
      [...kinds].sort(([left], [right]) => left.localeCompare(right))),
    fallbacks
  };

  function visit(value, location) {
    if (!value || typeof value !== 'object') return;
    if (typeof value.kind === 'string') {
      kinds.set(value.kind, (kinds.get(value.kind) ?? 0) + 1);
      if (value.kind === 'callback') callbacks++;
      if (value.kind === 'promise') promises++;
      if (value.kind === 'display') {
        fallbacks.push({ location, text: value.text });
      }
    }
    if (Array.isArray(value)) {
      value.forEach((item, index) => visit(item, `${location}[${index}]`));
      return;
    }
    for (const [key, child] of Object.entries(value)) {
      if (key === 'kind') continue;
      visit(child, `${location}.${key}`);
    }
  }
}

function matchesSelection(source, selections) {
  return selections.has(source)
    || selections.has(source.split('.').at(-1));
}

function containsPromise(type) {
  return type?.kind === 'promise'
    || type?.kind === 'union' && type.types.some(containsPromise);
}

function suggestedMethodName(method, asynchronous) {
  const base = `${pascalCase(method.name)}${
    method.overload > 0 ? `Overload${method.overload + 1}` : ''}`;
  return asynchronous && !base.endsWith('Async') ? `${base}Async` : base;
}

function suggestedTypeName(name) {
  return pascalCase(/^I[A-Z]/.test(name) ? name.slice(1) : name);
}

function assignUniquePolicyNames(policy) {
  const used = new Set();
  assign(policy.models ?? [], '');
  assign(policy.bindings ?? [], 'Proxy');
  assign(policy.adapters ?? [], 'Adapter');
  const generatedNames = new Set([
    ...(policy.models ?? []).map(entry => entry.name),
    ...(policy.bindings ?? []).map(entry => entry.name),
    ...(policy.adapters ?? []).map(entry => entry.name)
  ]);
  let functionsClassName = policy.functionsClassName;
  let suffix = 2;
  while (generatedNames.has(functionsClassName)) {
    functionsClassName = `JavaScriptGlobals${suffix++}`;
  }
  policy.functionsClassName = functionsClassName;

  function assign(entries, roleSuffix) {
    for (const entry of entries) {
      if (!used.has(entry.name)) {
        used.add(entry.name);
        continue;
      }
      const parts = entry.source.split('.');
      const originalName = pascalCase(parts.at(-1));
      const candidates = [
        originalName,
        `${entry.name}${roleSuffix}`
      ];
      for (let depth = 1; depth < parts.length; depth++) {
        candidates.push(
          pascalCase(parts.slice(-depth - 1, -1).join('_')) + originalName);
      }
      let candidate = candidates.find(name => name && !used.has(name));
      if (!candidate) {
        let suffix = 2;
        do {
          candidate = `${originalName}${roleSuffix}${suffix++}`;
        } while (used.has(candidate));
      }
      entry.name = candidate;
      used.add(candidate);
    }
  }
}

function assignUniqueMemberNames(policy) {
  const functionNames = new Set();
  for (const method of policy.functions ?? []) {
    method.name = uniqueMemberName(method.name, 'Function', functionNames);
  }
  for (const property of policy.globalProperties ?? []) {
    property.getterName = uniqueMemberName(
      property.getterName,
      'Property',
      functionNames);
  }
  for (const binding of policy.bindings ?? []) {
    const used = new Set();
    for (const constructor of binding.constructors ?? []) {
      constructor.name = uniqueMemberName(
        constructor.name ?? 'CreateAsync',
        'Constructor',
        used);
    }
    for (const method of binding.methods ?? []) {
      method.name = uniqueMemberName(method.name, 'Method', used);
    }
    for (const property of binding.properties ?? []) {
      property.getterName = uniqueMemberName(
        property.getterName,
        'Property',
        used);
      if (property.setterName) {
        property.setterName = uniqueMemberName(
          property.setterName,
          'Property',
          used);
      }
    }
  }
  for (const adapter of policy.adapters ?? []) {
    const used = new Set();
    for (const method of adapter.methods ?? []) {
      method.name = uniqueMemberName(method.name, 'Method', used);
    }
  }
}

function uniqueMemberName(preferred, suffix, used) {
  let candidate = preferred;
  let index = 2;
  while (used.has(candidate)) {
    candidate = `${preferred}${suffix}${index++}`;
  }
  used.add(candidate);
  return candidate;
}

function pascalCase(name) {
  return name
    .replace(/(^|[_\-\s]+)([A-Za-z0-9])/g, (_, __, character) =>
      character.toUpperCase())
    .replace(/[^A-Za-z0-9_]/g, '');
}

function collectNamedDeclarations(sourceFiles, checker) {
  const declarations = new Map();
  const symbols = [];
  const functionSymbols = [];
  const valueSymbols = [];
  for (const sourceFile of sourceFiles) visit(sourceFile);
  declarations.symbols = symbols.sort((left, right) =>
    qualifiedName(left).localeCompare(qualifiedName(right)));
  declarations.functionSymbols = functionSymbols.sort((left, right) =>
    qualifiedName(left).localeCompare(qualifiedName(right)));
  declarations.valueSymbols = valueSymbols.sort((left, right) =>
    qualifiedName(left).localeCompare(qualifiedName(right)));
  return declarations;

  function visit(node) {
    if ((ts.isInterfaceDeclaration(node)
        || ts.isClassDeclaration(node)
        || ts.isTypeAliasDeclaration(node)
        || ts.isEnumDeclaration(node))
        && node.name) {
      const symbol = checker.getSymbolAtLocation(node.name);
      if (symbol) {
        if (!symbols.includes(symbol)) symbols.push(symbol);
        add(node.name.text, symbol);
        add(qualifiedName(symbol), symbol);
      }
    }
    if ((ts.isFunctionDeclaration(node) && node.name
        || ts.isVariableDeclaration(node) && ts.isIdentifier(node.name))) {
      const symbol = checker.getSymbolAtLocation(node.name);
      const declaration = symbol?.valueDeclaration ?? symbol?.declarations?.[0];
      if (symbol && declaration) {
        const type = checker.getTypeOfSymbolAtLocation(symbol, declaration);
        if (checker.getSignaturesOfType(type, ts.SignatureKind.Call).length
            && !functionSymbols.includes(symbol)) {
          functionSymbols.push(symbol);
        } else if (ts.isVariableDeclaration(node)
                   && !valueSymbols.includes(symbol)) {
          valueSymbols.push(symbol);
        }
      }
    }
    ts.forEachChild(node, visit);
  }

  function add(name, symbol) {
    const symbols = declarations.get(name) ?? [];
    if (!symbols.includes(symbol)) symbols.push(symbol);
    declarations.set(name, symbols);
  }
}

function collectPublicDeclarations(
  sourceFiles,
  checker,
  declarations,
  entryPaths) {
  const knownTypes = new Set(declarations.symbols);
  const knownFunctions = new Set(declarations.functionSymbols);
  const knownValues = new Set(declarations.valueSymbols);
  const symbols = new Set();
  const functionSymbols = new Set();
  const valueSymbols = new Set();
  const visitedModules = new Set();

  for (const sourceFile of sourceFiles) {
    if (ts.isExternalModule(sourceFile)) {
      if (entryPaths.has(resolve(sourceFile.fileName))) {
        const sourceSymbol = checker.getSymbolAtLocation(sourceFile);
        if (sourceSymbol) visitModule(sourceSymbol);
      }
      for (const statement of sourceFile.statements) {
        if (ts.isModuleDeclaration(statement)
            && (statement.flags & ts.NodeFlags.GlobalAugmentation) !== 0) {
          const symbol = checker.getSymbolAtLocation(statement.name);
          if (symbol) visitModule(symbol);
        }
      }
    } else {
      for (const statement of sourceFile.statements) visitGlobalStatement(statement);
    }
  }

  return {
    symbols: [...symbols].sort((left, right) =>
      qualifiedName(left).localeCompare(qualifiedName(right))),
    functionSymbols: [...functionSymbols].sort((left, right) =>
      qualifiedName(left).localeCompare(qualifiedName(right))),
    valueSymbols: [...valueSymbols].sort((left, right) =>
      qualifiedName(left).localeCompare(qualifiedName(right)))
  };

  function visitGlobalStatement(statement) {
    if (ts.isVariableStatement(statement)) {
      for (const declaration of statement.declarationList.declarations) {
        if (!ts.isIdentifier(declaration.name)) continue;
        const symbol = checker.getSymbolAtLocation(declaration.name);
        if (symbol) visitSymbol(symbol);
      }
      return;
    }
    if ('name' in statement && statement.name) {
      const symbol = checker.getSymbolAtLocation(statement.name);
      if (symbol) visitSymbol(symbol);
    }
  }

  function visitModule(symbol) {
    symbol = resolveAlias(symbol);
    if (visitedModules.has(symbol)) return;
    visitedModules.add(symbol);
    visitKnown(symbol);
    for (const exported of checker.getExportsOfModule(symbol)) {
      visitSymbol(exported);
    }
  }

  function visitSymbol(symbol) {
    symbol = resolveAlias(symbol);
    visitKnown(symbol);
    if ((symbol.flags & (ts.SymbolFlags.NamespaceModule
        | ts.SymbolFlags.ValueModule)) !== 0) {
      visitModule(symbol);
    }
  }

  function visitKnown(symbol) {
    if (knownTypes.has(symbol)) symbols.add(symbol);
    if (knownFunctions.has(symbol)) functionSymbols.add(symbol);
    if (knownValues.has(symbol)) valueSymbols.add(symbol);
  }

  function resolveAlias(symbol) {
    return (symbol.flags & ts.SymbolFlags.Alias) !== 0
      ? checker.getAliasedSymbol(symbol)
      : symbol;
  }
}

function describeFunction(symbol, checker) {
  const declaration = symbol.valueDeclaration ?? symbol.declarations?.[0];
  if (!declaration) return [];
  const type = checker.getTypeOfSymbolAtLocation(symbol, declaration);
  return describeSignatures(
    checker.getSignaturesOfType(type, ts.SignatureKind.Call),
    checker,
    declaration)
    .map((signature, overload) => ({
      name: symbol.getName(),
      qualifiedName: qualifiedName(symbol),
      overload,
      ...signature
    }));
}

function describeStaticFunctions(symbol, checker) {
  const declaration = symbol.declarations?.find(ts.isClassDeclaration);
  if (!declaration) return [];
  const valueType = checker.getTypeOfSymbolAtLocation(symbol, declaration);
  return valueType.getProperties()
    .filter(property => property.getName() !== 'prototype')
    .flatMap(property => {
      const memberDeclaration =
        property.valueDeclaration ?? property.declarations?.[0] ?? declaration;
      const type = checker.getTypeOfSymbolAtLocation(
        property,
        memberDeclaration);
      const signatures = checker.getSignaturesOfType(
        type,
        ts.SignatureKind.Call);
      return describeSignatures(signatures, checker, memberDeclaration)
        .map((signature, overload) => ({
          name: property.getName(),
          qualifiedName: `${qualifiedName(symbol)}.${property.getName()}`,
          overload,
          ...signature
        }));
    });
}

function describeGlobalValue(symbol, checker) {
  const declaration = symbol.valueDeclaration ?? symbol.declarations?.[0];
  return {
    name: symbol.getName(),
    qualifiedName: qualifiedName(symbol),
    readonly: isConstSymbol(symbol),
    type: describeType(
      checker.getTypeOfSymbolAtLocation(symbol, declaration),
      checker)
  };
}

function describeStaticValues(symbol, checker) {
  const declaration = symbol.declarations?.find(ts.isClassDeclaration);
  if (!declaration) return [];
  const valueType = checker.getTypeOfSymbolAtLocation(symbol, declaration);
  return valueType.getProperties()
    .filter(property => property.getName() !== 'prototype')
    .flatMap(property => {
      const memberDeclaration =
        property.valueDeclaration ?? property.declarations?.[0] ?? declaration;
      const type = checker.getTypeOfSymbolAtLocation(
        property,
        memberDeclaration);
      if (checker.getSignaturesOfType(type, ts.SignatureKind.Call).length) {
        return [];
      }
      return [{
        name: property.getName(),
        qualifiedName: `${qualifiedName(symbol)}.${property.getName()}`,
        readonly: isReadonly(property),
        type: describeType(type, checker)
      }];
    });
}

function describeDeclaration(symbol, checker) {
  const type = checker.getDeclaredTypeOfSymbol(symbol);
  const declaration = symbol.declarations?.find(node =>
    ts.isInterfaceDeclaration(node)
    || ts.isClassDeclaration(node)
    || ts.isTypeAliasDeclaration(node)
    || ts.isEnumDeclaration(node));
  const isObjectDeclaration = declaration
    && (ts.isInterfaceDeclaration(declaration) || ts.isClassDeclaration(declaration));
  const result = {
    name: symbol.getName(),
    qualifiedName: qualifiedName(symbol),
    kind: declarationKind(symbol),
    typeParameters: declaration?.typeParameters?.map(parameter => parameter.name.text) ?? [],
    constructors: isObjectDeclaration ? describeConstructors(symbol, checker) : [],
    callSignatures: isObjectDeclaration
      ? describeSignatures(
        checker.getSignaturesOfType(type, ts.SignatureKind.Call),
        checker,
        declaration)
      : [],
    methods: (isObjectDeclaration ? type.getProperties() : [])
      .filter(isMethodSymbol)
      .flatMap(property => describeMethods(property, checker))
      .sort((left, right) =>
        left.name.localeCompare(right.name) || left.overload - right.overload),
    properties: (isObjectDeclaration ? type.getProperties() : [])
      .filter(property => !isMethodSymbol(property))
      .map(property => ({
        name: property.getName(),
        optional: (property.flags & ts.SymbolFlags.Optional) !== 0,
        readonly: isReadonly(property),
        type: describeType(
          checker.getTypeOfSymbolAtLocation(
            property,
            property.valueDeclaration ?? symbol.valueDeclaration),
          checker)
      }))
      .sort((left, right) => left.name.localeCompare(right.name)),
    indexSignatures: isObjectDeclaration ? describeIndexSignatures(type, checker) : [],
    baseTypes: (isObjectDeclaration ? type.getBaseTypes?.() ?? [] : [])
      .map(base => describeType(base, checker))
  };
  if (declaration && ts.isTypeAliasDeclaration(declaration)) {
    result.aliasTarget = describeType(
      checker.getTypeFromTypeNode(declaration.type),
      checker,
      new Set([symbol]));
  }
  if (declaration && ts.isEnumDeclaration(declaration)) {
    result.enumMembers = declaration.members.map(member => {
      const memberSymbol = checker.getSymbolAtLocation(member.name);
      const constant = checker.getConstantValue(member);
      return {
        name: memberSymbol?.getName() ?? member.name.getText(),
        value: constant ?? null
      };
    });
  }
  replaceThisTypes(result, symbol);
  return result;
}

function replaceThisTypes(value, owner) {
  if (!value || typeof value !== 'object') return;
  if (value.kind === 'this') {
    value.kind = 'reference';
    value.name = owner.getName();
    value.qualifiedName = qualifiedName(owner);
    value.display = owner.getName();
    value.typeArguments = [];
    return;
  }
  if (Array.isArray(value)) {
    value.forEach(item => replaceThisTypes(item, owner));
    return;
  }
  Object.values(value).forEach(item => replaceThisTypes(item, owner));
}

function describeMethods(property, checker) {
  const declaration = property.valueDeclaration ?? property.declarations?.[0];
  if (!declaration) return [];
  const type = checker.getTypeOfSymbolAtLocation(property, declaration);
  const signatures = checker.getSignaturesOfType(type, ts.SignatureKind.Call);
  return describeSignatures(signatures, checker, declaration)
    .map((signature, overload) => ({
    name: property.getName(),
    overload,
    ...signature
  }));
}

function isMethodSymbol(symbol) {
  return symbol.declarations?.some(declaration =>
    ts.isMethodSignature(declaration)
    || ts.isMethodDeclaration(declaration)) ?? false;
}

function describeConstructors(symbol, checker) {
  const declaration = symbol.valueDeclaration ?? symbol.declarations?.[0];
  if (!declaration) return [];
  const valueType = checker.getTypeOfSymbolAtLocation(symbol, declaration);
  return describeSignatures(
    checker.getSignaturesOfType(valueType, ts.SignatureKind.Construct),
    checker,
    declaration);
}

function describeSignatures(signatures, checker, fallbackDeclaration, seen = new Set()) {
  return signatures.map(signature => ({
    typeParameters: signature.typeParameters?.map(parameter =>
      parameter.symbol?.getName() ?? checker.typeToString(parameter)) ?? [],
    parameters: signature.getParameters().map(parameter => {
      const parameterDeclaration = parameter.valueDeclaration ?? parameter.declarations?.[0];
      return {
        name: parameter.getName(),
        optional: (parameter.flags & ts.SymbolFlags.Optional) !== 0
          || Boolean(parameterDeclaration?.questionToken)
          || Boolean(parameterDeclaration?.initializer),
        rest: Boolean(parameterDeclaration?.dotDotDotToken),
        type: describeType(
          checker.getTypeOfSymbolAtLocation(
            parameter,
            parameterDeclaration ?? fallbackDeclaration),
          checker,
          seen)
      };
    }),
    returns: describeType(checker.getReturnTypeOfSignature(signature), checker, seen)
  }));
}

function describeIndexSignatures(type, checker) {
  const result = [];
  const stringType = type.getStringIndexType?.();
  const numberType = type.getNumberIndexType?.();
  if (stringType) {
    result.push({ key: 'string', value: describeType(stringType, checker) });
  }
  if (numberType) {
    result.push({ key: 'number', value: describeType(numberType, checker) });
  }
  return result;
}

function describeType(type, checker, seen = new Set()) {
  if (type.isThisType) return { kind: 'this' };
  if (type.flags & ts.TypeFlags.String) return { kind: 'string' };
  if (type.flags & ts.TypeFlags.TemplateLiteral) return { kind: 'string' };
  if (type.flags & ts.TypeFlags.Number) return { kind: 'number' };
  if (type.flags & ts.TypeFlags.Boolean) return { kind: 'boolean' };
  if (type.flags & ts.TypeFlags.BigInt) return { kind: 'bigint' };
  if (type.flags & ts.TypeFlags.ESSymbol) return { kind: 'symbol' };
  if (type.flags & ts.TypeFlags.Void) return { kind: 'void' };
  if (type.flags & ts.TypeFlags.Never) return { kind: 'never' };
  if (type.flags & ts.TypeFlags.NonPrimitive) return { kind: 'object' };
  if (type.flags & ts.TypeFlags.Any) return { kind: 'any' };
  if (type.flags & ts.TypeFlags.Unknown) return { kind: 'unknown' };
  if (type.flags & ts.TypeFlags.Null) return { kind: 'null' };
  if (type.flags & ts.TypeFlags.Undefined) return { kind: 'undefined' };
  if (type.isStringLiteral()) return { kind: 'literal', value: type.value };
  if (type.isNumberLiteral()) return { kind: 'literal', value: type.value };
  if (type.flags & ts.TypeFlags.BooleanLiteral) {
    return { kind: 'literal', value: checker.typeToString(type) === 'true' };
  }
  const declaredAlias = type.aliasSymbol;
  const declaredAliasName = declaredAlias?.getName();
  if (declaredAlias
      && declaredAliasName
      && (declaredAliasName === 'Record'
        || type.isIntersection()
        && !type.types.some(item =>
          Boolean(item.flags & (
            ts.TypeFlags.StringLike
            | ts.TypeFlags.NumberLike
            | ts.TypeFlags.BooleanLike
            | ts.TypeFlags.BigIntLike
            | ts.TypeFlags.ESSymbolLike))))
      && !seen.has(declaredAlias)
      && ![
        'Awaited', 'Exclude', 'Extract', 'NonNullable', 'Omit', 'Partial',
        'Pick', 'Readonly', 'Required', 'ReturnType'
      ].includes(declaredAliasName)) {
    const aliasArguments = type.aliasTypeArguments ?? [];
    return {
      kind: 'reference',
      name: declaredAliasName,
      qualifiedName: qualifiedName(declaredAlias),
      display: safeTypeDisplay(type, checker, declaredAliasName),
      typeArguments: aliasArguments.map(item =>
        describeType(item, checker, new Set([...seen, declaredAlias])))
    };
  }
  if (type.isUnion()) {
    const booleanLiterals = type.types.filter(item =>
      (item.flags & ts.TypeFlags.BooleanLiteral) !== 0);
    const nonBooleanLiterals = type.types.filter(item =>
      (item.flags & ts.TypeFlags.BooleanLiteral) === 0);
    if (booleanLiterals.length === 2) {
      const booleanType = { kind: 'boolean' };
      return nonBooleanLiterals.length
        ? {
          kind: 'union',
          types: [
            ...nonBooleanLiterals.map(item => describeType(item, checker, seen)),
            booleanType
          ]
        }
        : booleanType;
    }
    return { kind: 'union', types: type.types.map(item => describeType(item, checker, seen)) };
  }
  if (type.isIntersection()) {
    const hasPrimitiveConstituent = type.types.some(item =>
      Boolean(item.flags & (
        ts.TypeFlags.StringLike
        | ts.TypeFlags.NumberLike
        | ts.TypeFlags.BooleanLike
        | ts.TypeFlags.BigIntLike
        | ts.TypeFlags.ESSymbolLike)));
    if (!hasPrimitiveConstituent
        && (type.getProperties().length
          || type.getStringIndexType?.()
          || type.getNumberIndexType?.())) {
      return describeInlineObject(type, checker, seen);
    }
    return {
      kind: 'intersection',
      types: type.types.map(item => describeType(item, checker, seen))
    };
  }

  if (checker.isTupleType?.(type)) {
    const elements = checker.getTypeArguments(type);
    return {
      kind: 'tuple',
      elements: elements.map(item => describeType(item, checker, seen))
    };
  }

  const targetSymbol = type.getSymbol();
  const symbol = type.aliasSymbol ?? targetSymbol;
  const symbolName = symbol?.getName();
  const targetSymbolName = targetSymbol?.getName() ?? symbolName;
  const typeArguments = checker.getTypeArguments?.(type) ?? type.aliasTypeArguments ?? [];
  if (['Promise', 'PromiseLike', 'Thenable'].includes(targetSymbolName)
      && typeArguments.length === 1) {
    return { kind: 'promise', result: describeType(typeArguments[0], checker, seen) };
  }
  if ((targetSymbolName === 'Array' || targetSymbolName === 'ReadonlyArray')
      && typeArguments.length === 1) {
    return {
      kind: 'array',
      readonly: targetSymbolName === 'ReadonlyArray',
      element: describeType(typeArguments[0], checker, seen)
    };
  }

  if (type.flags & ts.TypeFlags.TypeParameter) {
    return { kind: 'typeParameter', name: symbolName ?? checker.typeToString(type) };
  }

  const isMappedObject = Boolean(
    type.flags & ts.TypeFlags.Object
    && type.objectFlags & ts.ObjectFlags.Mapped);
  const isNamedMappedAlias = Boolean(
    symbol
    && symbolName
    && symbolName !== '__type'
    && !['Omit', 'Partial', 'Pick', 'Readonly', 'Record', 'Required'].includes(symbolName)
    && !seen.has(symbol));
  if (isMappedObject
      && !isNamedMappedAlias
      && (type.getProperties().length
        || type.getStringIndexType?.()
        || type.getNumberIndexType?.())) {
    return describeInlineObject(type, checker, seen);
  }

  const callSignatures = checker.getSignaturesOfType(type, ts.SignatureKind.Call);
  if (callSignatures.length) {
    const identity = typeIdentity(type);
    if (seen.has(identity)) {
      return {
        kind: 'display',
        text: safeTypeDisplay(type, checker, symbolName ?? 'recursive callback')
      };
    }
    return {
      kind: 'callback',
      name: symbolName === '__type' ? null : symbolName ?? null,
      qualifiedName: symbolName === '__type' ? null : symbol ? qualifiedName(symbol) : null,
      signatures: describeSignatures(
        callSignatures,
        checker,
        symbol?.valueDeclaration,
        new Set([...seen, identity]))
    };
  }

  const display = safeTypeDisplay(type, checker, symbolName ?? 'anonymous');
  if (symbolName && symbolName !== '__type' && !seen.has(symbol)) {
    return {
      kind: 'reference',
      name: symbolName,
      qualifiedName: qualifiedName(symbol),
      display,
      typeArguments: typeArguments.map(item =>
        describeType(item, checker, new Set([...seen, symbol])))
    };
  }
  const properties = type.getProperties?.() ?? [];
  if (properties.length || type.getStringIndexType?.() || type.getNumberIndexType?.()) {
    return describeInlineObject(type, checker, seen);
  }
  return { kind: 'display', text: display };
}

function describeInlineObject(type, checker, seen) {
  const identity = typeIdentity(type);
  if (seen.has(identity)) {
    return {
      kind: 'display',
      text: safeTypeDisplay(type, checker, 'recursive object')
    };
  }
  const nestedSeen = new Set([...seen, identity]);
  return {
    kind: 'inlineObject',
    properties: (type.getProperties?.() ?? []).map(property => {
      const declaration = property.valueDeclaration ?? property.declarations?.[0];
      return {
        name: property.getName(),
        optional: (property.flags & ts.SymbolFlags.Optional) !== 0,
        readonly: isReadonly(property),
        type: describeType(
          checker.getTypeOfSymbolAtLocation(property, declaration),
          checker,
          nestedSeen)
      };
    }),
    indexSignatures: describeIndexSignatures(type, checker)
  };
}

function typeIdentity(type) {
  return typeof type.id === 'number' ? `type:${type.id}` : type;
}

function safeTypeDisplay(type, checker, fallback) {
  try {
    return checker.typeToString(
      type,
      undefined,
      ts.TypeFormatFlags.NoTruncation);
  } catch (error) {
    if (!(error instanceof RangeError)) throw error;
    return fallback;
  }
}

function qualifiedName(symbol) {
  const names = [];
  for (let current = symbol; current; current = current.parent) {
    const name = current.getName();
    if (name && name !== '__global' && !name.startsWith('"')) names.unshift(name);
  }
  return names.join('.');
}

function declarationKind(symbol) {
  if (symbol.flags & ts.SymbolFlags.Interface) return 'interface';
  if (symbol.flags & ts.SymbolFlags.Class) return 'class';
  if (symbol.flags & ts.SymbolFlags.TypeAlias) return 'typeAlias';
  if (symbol.flags & ts.SymbolFlags.Enum) return 'enum';
  return 'unknown';
}

function isReadonly(symbol) {
  return symbol.declarations?.some(declaration =>
    ts.canHaveModifiers(declaration)
    && ts.getModifiers(declaration)?.some(
      modifier => modifier.kind === ts.SyntaxKind.ReadonlyKeyword)) ?? false;
}

function isConstSymbol(symbol) {
  return symbol.declarations?.some(declaration =>
    ts.isVariableDeclaration(declaration)
    && ts.isVariableDeclarationList(declaration.parent)
    && (declaration.parent.flags & ts.NodeFlags.Const) !== 0) ?? false;
}

function formatDiagnostics(diagnostics) {
  return ts.formatDiagnosticsWithColorAndContext(diagnostics, {
    getCanonicalFileName: path => path,
    getCurrentDirectory: () => process.cwd(),
    getNewLine: () => '\n'
  });
}
