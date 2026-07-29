import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { afterEach, describe, expect, it } from 'vitest';
import { resolveAudioHelperPath } from './audio-helper';

const APP_ROOT_ENV = 'OPENDS5_APP_ROOT';
const HELPER_RELATIVE_PATH = process.platform === 'win32'
  ? path.join('native', 'AudioHelper', 'AudioHelper.exe')
  : path.join('native', 'audio-helper-linux.mjs');

const originalAppRoot = process.env[APP_ROOT_ENV];
const originalResourcesPath = Object.getOwnPropertyDescriptor(process, 'resourcesPath');
const temporaryRoots: string[] = [];

function createHelperRoot(prefix: string): string {
  const root = mkdtempSync(path.join(tmpdir(), prefix));
  const helperPath = path.join(root, HELPER_RELATIVE_PATH);
  mkdirSync(path.dirname(helperPath), { recursive: true });
  writeFileSync(helperPath, '#!/usr/bin/env node\n');
  temporaryRoots.push(root);
  return root;
}

function setResourcesPath(value: string | undefined): void {
  if (value === undefined) {
    delete (process as NodeJS.Process & { resourcesPath?: string }).resourcesPath;
    return;
  }
  Object.defineProperty(process, 'resourcesPath', {
    configurable: true,
    enumerable: false,
    value
  });
}

afterEach(() => {
  if (originalAppRoot === undefined) {
    delete process.env[APP_ROOT_ENV];
  } else {
    process.env[APP_ROOT_ENV] = originalAppRoot;
  }
  if (originalResourcesPath) {
    Object.defineProperty(process, 'resourcesPath', originalResourcesPath);
  } else {
    delete (process as NodeJS.Process & { resourcesPath?: string }).resourcesPath;
  }
  for (const root of temporaryRoots.splice(0)) {
    rmSync(root, { recursive: true, force: true });
  }
});

describe('resolveAudioHelperPath', () => {
  it('prefers the explicit packaged app root over Electron resourcesPath', () => {
    const appRoot = createHelperRoot('opends5-app-root-');
    const resourcesRoot = createHelperRoot('opends5-resources-root-');
    process.env[APP_ROOT_ENV] = appRoot;
    setResourcesPath(resourcesRoot);

    expect(resolveAudioHelperPath()).toBe(path.join(appRoot, HELPER_RELATIVE_PATH));
  });

  it('falls back to Electron resourcesPath when no app root is configured', () => {
    const resourcesRoot = createHelperRoot('opends5-resources-root-');
    delete process.env[APP_ROOT_ENV];
    setResourcesPath(resourcesRoot);

    expect(resolveAudioHelperPath()).toBe(path.join(resourcesRoot, HELPER_RELATIVE_PATH));
  });
});
