import { readFile } from 'node:fs/promises';

export const profileVersion = '1.0';

export const knownCapabilities = Object.freeze([
  'dom', 'css.layout', 'canvas.2d', 'svg', 'input.pointer', 'input.keyboard',
  'input.focus', 'clipboard', 'host.commands', 'host.settings',
  'host.notifications', 'host.network', 'host.clipboard', 'host.files'
]);

const rules = [
  unsupported(/\bnavigator\s*\.\s*serviceWorker\b/g, 'WEBSCENE1001', 'Service workers are not supported.'),
  unsupported(/\b(?:localStorage|sessionStorage|indexedDB)\b/g, 'WEBSCENE1002', 'Browser storage is not supported; request host.settings instead.'),
  unsupported(/\b(?:Worker|SharedWorker|Worklet)\s*\(/g, 'WEBSCENE1003', 'Web workers and worklets are not supported.'),
  unsupported(/\b(?:RTCPeerConnection|MediaRecorder|AudioContext|webkitAudioContext)\b/g, 'WEBSCENE1004', 'WebRTC, recording, and Web Audio are not supported.'),
  unsupported(/\bnavigator\s*\.\s*(?:mediaDevices|geolocation)\b/g, 'WEBSCENE1005', 'Media devices and geolocation are not supported.'),
  unsupported(/\bwindow\s*\.\s*open\s*\(/g, 'WEBSCENE1006', 'Arbitrary browser windows and navigation are not supported.'),
  requires(/\bnavigator\s*\.\s*clipboard\b/g, 'WEBSCENE2001', 'clipboard', 'Clipboard access must be declared.'),
  requires(/\bwebscene\s*\.\s*host\s*\.\s*commands\b/g, 'WEBSCENE2002', 'host.commands', 'Host commands must be declared.'),
  requires(/\bwebscene\s*\.\s*host\s*\.\s*settings\b/g, 'WEBSCENE2003', 'host.settings', 'Host settings must be declared.'),
  requires(/\bwebscene\s*\.\s*host\s*\.\s*notifications\b/g, 'WEBSCENE2004', 'host.notifications', 'Host notifications must be declared.'),
  requires(/\bwebscene\s*\.\s*host\s*\.\s*network\b/g, 'WEBSCENE2005', 'host.network', 'Host networking must be declared.'),
  requires(/\bwebscene\s*\.\s*host\s*\.\s*clipboard\b/g, 'WEBSCENE2006', 'host.clipboard', 'Host clipboard access must be declared.'),
  requires(/\bwebscene\s*\.\s*host\s*\.\s*files\b/g, 'WEBSCENE2007', 'host.files', 'Host file selection must be declared.'),
  { pattern: /\b(?:fetch|WebSocket|XMLHttpRequest)\b/g, code: 'WEBSCENE3001', severity: 'warning', message: 'Direct networking bypasses host policy; prefer webscene.host.network.' }
];

function unsupported(pattern, code, message) {
  return { pattern, code, severity: 'error', message };
}

function requires(pattern, code, capability, message) {
  return { pattern, code, severity: 'error', capability, message };
}

export function validateManifest(manifest) {
  const errors = [];
  if (!manifest || typeof manifest !== 'object') return ['Manifest is required.'];
  if (manifest.schemaVersion !== '1.0') errors.push("schemaVersion must be '1.0'.");
  if (manifest.profileVersion !== profileVersion) errors.push(`profileVersion must be '${profileVersion}'.`);
  if (!/^[a-z][a-z0-9]*(?:[.-][a-z0-9]+)+$/.test(manifest.id ?? '')) errors.push('id must be a lowercase reverse-domain identifier.');
  if (!/^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$/.test(manifest.version ?? '')) errors.push('version must be a semantic version.');
  if (!manifest.displayName?.trim()) errors.push('displayName is required.');
  validatePath(manifest.entryPoint, 'entryPoint', errors);
  validateList(manifest.assets, 'assets', value => validatePath(value, 'assets', errors), errors);
  if (Array.isArray(manifest.assets) && !manifest.assets.includes(manifest.entryPoint)) errors.push('assets must include entryPoint.');
  validateList(manifest.capabilities, 'capabilities', value => {
    if (!knownCapabilities.includes(value)) errors.push(`Unknown capability '${value}'.`);
  }, errors);
  return errors;
}

export function checkSource(source, manifest, sourceName = '<source>') {
  const manifestErrors = validateManifest(manifest);
  if (manifestErrors.length) throw new Error(`Invalid WebScene component manifest: ${manifestErrors.join('; ')}`);
  const searchable = maskCommentsAndStrings(source);
  const diagnostics = [];
  for (const rule of rules) {
    rule.pattern.lastIndex = 0;
    for (const match of searchable.matchAll(rule.pattern)) {
      if (rule.capability && manifest.capabilities.includes(rule.capability)) continue;
      const { line, column } = location(source, match.index);
      diagnostics.push({
        code: rule.code,
        severity: rule.severity,
        message: rule.capability ? `${rule.message} Missing capability '${rule.capability}'.` : rule.message,
        source: sourceName,
        line,
        column,
        requiredCapability: rule.capability
      });
    }
  }
  return diagnostics;
}

export async function readManifest(path) {
  return JSON.parse(await readFile(path, 'utf8'));
}

function validateList(values, property, validate, errors) {
  if (!Array.isArray(values) || values.length === 0) {
    errors.push(`${property} must contain at least one value.`);
    return;
  }
  const seen = new Set();
  for (const value of values) {
    if (typeof value !== 'string' || !value.trim()) errors.push(`${property} values must not be empty.`);
    else if (seen.has(value)) errors.push(`${property} contains duplicate '${value}'.`);
    else { seen.add(value); validate(value); }
  }
}

function validatePath(value, property, errors) {
  if (typeof value !== 'string' || !value || value.startsWith('/') || value.includes('\\') || value.split('/').some(part => !part || part === '.' || part === '..')) {
    errors.push(`${property} must be a normalized relative package path.`);
  }
}

function maskCommentsAndStrings(source) {
  return source.replace(/\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|'(?:\\.|[^'\\])*'|"(?:\\.|[^"\\])*"|`(?:\\.|[^`\\])*`/g,
    value => value.replace(/[^\r\n]/g, ' '));
}

function location(source, index) {
  const prefix = source.slice(0, index);
  const lines = prefix.split('\n');
  return { line: lines.length, column: lines.at(-1).length + 1 };
}
