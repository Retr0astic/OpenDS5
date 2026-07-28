import { constants as fsConstants } from 'node:fs';
import {
  accessSync,
  copyFileSync,
  fsyncSync,
  lstatSync,
  openSync,
  closeSync,
  readFileSync,
  renameSync,
  statSync,
  unlinkSync,
  writeFileSync,
  mkdirSync
} from 'node:fs';
import { randomBytes } from 'node:crypto';
import path from 'node:path';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';

const execFileAsync = promisify(execFile);
const CONFIG_BASENAME = '99-vds-dualsense.conf';
const DEFAULT_CONFIG_DIRS = ['/etc/xdg'];
const DEFAULT_DATA_DIRS = ['/usr/local/share', '/usr/share'];

export type WirePlumberConfigStatus =
  | 'current' | 'known-legacy' | 'modified' | 'missing' | 'unreadable'
  | 'symlink' | 'non-regular' | 'package-managed' | 'unavailable';

export interface WirePlumberConfigReport {
  status: WirePlumberConfigStatus;
  path: string;
  detail: string;
}

export interface WirePlumberConfigInspectOptions {
  packageManaged?: boolean;
  /** Complete active system precedence paths, injected by tests or callers. */
  managedPaths?: string[];
  configDirs?: string[];
  dataDirs?: string[];
}

function splitDirs(value: string | undefined, fallback: string[]): string[] {
  const dirs = value?.split(':').filter(Boolean);
  return dirs && dirs.length > 0 ? dirs : fallback;
}

export function activeWirePlumberManagedPaths(options: WirePlumberConfigInspectOptions = {}): string[] {
  if (options.managedPaths) return [...options.managedPaths];
  const configDirs = options.configDirs ?? splitDirs(process.env.XDG_CONFIG_DIRS, DEFAULT_CONFIG_DIRS);
  const dataDirs = options.dataDirs ?? splitDirs(process.env.XDG_DATA_DIRS, DEFAULT_DATA_DIRS);
  return [
    '/etc/wireplumber/wireplumber.conf.d/99-vds-dualsense.conf',
    ...configDirs.map((dir) => path.join(dir, 'wireplumber', 'wireplumber.conf.d', CONFIG_BASENAME)),
    ...dataDirs.map((dir) => path.join(dir, 'wireplumber', 'wireplumber.conf.d', CONFIG_BASENAME))
  ];
}

function lstatOrNull(filePath: string): ReturnType<typeof lstatSync> | null {
  try { return lstatSync(filePath); } catch (error: unknown) {
    if ((error as NodeJS.ErrnoException).code === 'ENOENT') return null;
    throw error;
  }
}

export function inspectWirePlumberConfig(
  filePath: string,
  expected: string,
  options: WirePlumberConfigInspectOptions = {}
): WirePlumberConfigReport {
  let userStat: ReturnType<typeof lstatSync> | null;
  try { userStat = lstatOrNull(filePath); } catch {
    return { status: 'unreadable', path: filePath, detail: 'The OpenDS5 WirePlumber config cannot be inspected.' };
  }
  if (userStat?.isSymbolicLink()) {
    return { status: 'symlink', path: filePath, detail: 'The user config is a symlink and will not be replaced automatically.' };
  }
  if (userStat && !userStat.isFile()) {
    return { status: 'non-regular', path: filePath, detail: 'The user config is not a regular file and will not be replaced automatically.' };
  }
  if (userStat) {
    try {
      accessSync(filePath, fsConstants.R_OK);
      const actual = readFileSync(filePath, 'utf8');
      if (actual === expected) return { status: 'current', path: filePath, detail: 'OpenDS5 WirePlumber config is current.' };
      if (actual.includes('alsa_card.usb-Sony_Interactive_Entertainment_DualSense')) {
        return { status: 'known-legacy', path: filePath, detail: 'The config uses the legacy broad DualSense matcher.' };
      }
      return { status: 'modified', path: filePath, detail: 'The user config differs from the OpenDS5 managed content.' };
    } catch {
      return { status: 'unreadable', path: filePath, detail: 'The OpenDS5 WirePlumber config cannot be read.' };
    }
  }
  if (options.packageManaged || activeWirePlumberManagedPaths(options).some((candidate) => {
    try { return statSync(candidate).isFile(); } catch { return false; }
  })) {
    return { status: 'package-managed', path: filePath, detail: 'An active system package or Nix WirePlumber config takes precedence; no user shadow override is needed.' };
  }
  return { status: 'missing', path: filePath, detail: 'OpenDS5 WirePlumber config is not installed.' };
}

type RepairOptions = {
  approve: boolean;
  configRoot?: string;
  backupPathFactory?: (directory: string, basename: string) => string;
  tempPathFactory?: (directory: string, basename: string) => string;
};

function validateExistingHierarchy(directory: string): void {
  let current = path.resolve(directory);
  while (true) {
    const stat = lstatOrNull(current);
    if (stat) {
      if (stat.isSymbolicLink()) throw new Error('WirePlumber config path contains a symlink.');
      if (!stat.isDirectory()) throw new Error('WirePlumber config path contains a non-directory component.');
    }
    const parent = path.dirname(current);
    if (parent === current) return;
    if (!stat) {
      current = parent;
      continue;
    }
    current = parent;
  }
}

function ensureConfigDirectory(directory: string, configRoot: string): void {
  const root = path.resolve(configRoot);
  const target = path.resolve(directory);
  const relative = path.relative(root, target);
  if (relative.startsWith('..' + path.sep) || path.isAbsolute(relative)) {
    throw new Error('WirePlumber config path is outside the user XDG config root.');
  }
  validateExistingHierarchy(path.dirname(root));
  let rootStat = lstatOrNull(root);
  if (rootStat) {
    if (rootStat.isSymbolicLink() || !rootStat.isDirectory()) throw new Error('User XDG config root must be a real directory.');
  } else {
    mkdirSync(root, { recursive: true, mode: 0o700 });
  }
  let current = root;
  for (const component of relative.split(path.sep).filter(Boolean)) {
    current = path.join(current, component);
    const stat = lstatOrNull(current);
    if (!stat) {
      mkdirSync(current, { mode: 0o700 });
      continue;
    }
    if (stat.isSymbolicLink() || !stat.isDirectory()) throw new Error('WirePlumber config path contains a symlink or non-directory component.');
  }
}

function copyBackupExclusively(
  source: string,
  directory: string,
  basename: string,
  factory?: (directory: string, basename: string) => string
): string {
  for (let attempt = 0; attempt < 8; attempt += 1) {
    const candidate = factory?.(directory, basename)
      ?? path.join(directory, `.${basename}.bak-${randomBytes(12).toString('hex')}`);
    try {
      copyFileSync(source, candidate, fsConstants.COPYFILE_EXCL);
      return candidate;
    } catch (error: unknown) {
      if ((error as NodeJS.ErrnoException).code === 'EEXIST') continue;
      throw error;
    }
  }
  throw new Error('Unable to create an exclusive WirePlumber backup.');
}

export function repairWirePlumberConfig(
  filePath: string,
  expected: string,
  report: WirePlumberConfigReport,
  options: RepairOptions
): string | null {
  if (!options.approve) throw new Error('WirePlumber config repair requires explicit approval.');
  if (report.status === 'package-managed') throw new Error('Package-managed/Nix WirePlumber configs are not repaired in the user home.');
  const directory = path.dirname(filePath);
  if (report.status === 'missing') {
    if (!options.configRoot) throw new Error('A user XDG config root is required to create the missing WirePlumber config.');
    ensureConfigDirectory(directory, options.configRoot);
  } else {
    validateExistingHierarchy(directory);
  }
  const targetStat = lstatOrNull(filePath);
  if (targetStat && (!targetStat.isFile() || targetStat.isSymbolicLink())) {
    throw new Error('WirePlumber config target must be a regular file, not a symlink or special file.');
  }
  let backupPath: string | null = null;
  if (report.status === 'modified' || report.status === 'known-legacy' || report.status === 'unreadable') {
    const sourceStat = lstatSync(filePath);
    if (!sourceStat.isFile() || sourceStat.isSymbolicLink()) throw new Error('WirePlumber backup source must be a regular file.');
    backupPath = copyBackupExclusively(filePath, directory, path.basename(filePath), options.backupPathFactory);
  }
  const basename = path.basename(filePath);
  const makeTemp = options.tempPathFactory ?? ((dir, name) => path.join(dir, `.${name}.tmp-${process.pid}-${randomBytes(12).toString('hex')}`));
  let temporary = makeTemp(directory, basename);
  let created = false;
  try {
    for (let attempt = 0; attempt < 5; attempt += 1) {
      try {
        writeFileSync(temporary, expected, { flag: 'wx', mode: 0o644 });
        created = true;
        break;
      } catch (error: unknown) {
        if ((error as NodeJS.ErrnoException).code !== 'EEXIST') throw error;
        temporary = makeTemp(directory, basename);
      }
    }
    if (!created) throw new Error('Unable to create an exclusive WirePlumber temporary file.');
    const fd = openSync(temporary, 'r');
    try { fsyncSync(fd); } finally { closeSync(fd); }
    renameSync(temporary, filePath);
    created = false;
    return backupPath;
  } finally {
    if (created) {
      try { unlinkSync(temporary); } catch { /* best-effort cleanup */ }
    }
  }
}

export async function restartWirePlumberAndWait(
  endpointReady: () => Promise<boolean>,
  options: { restart?: () => Promise<void>; timeoutMs?: number; pollMs?: number } = {}
): Promise<'ready' | 'reload-required' | 'error'> {
  const restart = options.restart ?? (async () => {
    await execFileAsync('systemctl', ['--user', 'restart', 'wireplumber']);
  });
  try { await restart(); } catch { return 'error'; }
  const deadline = Date.now() + (options.timeoutMs ?? 5000);
  while (Date.now() < deadline) {
    try { if (await endpointReady()) return 'ready'; } catch { /* restart may still be settling */ }
    await new Promise((resolve) => setTimeout(resolve, options.pollMs ?? 100));
  }
  return 'reload-required';
}
