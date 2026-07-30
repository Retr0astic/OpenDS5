import { lstatSync, mkdirSync, mkdtempSync, readFileSync, statSync, symlinkSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { describe, expect, it } from 'vitest';
import { inspectWirePlumberConfig, repairWirePlumberConfig, restartWirePlumberAndWait } from './wireplumber-config';

const configSource = readFileSync(new URL('../../../../vds/99-vds-dualsense-wireplumber.conf', import.meta.url), 'utf8');
const udevSource = readFileSync(new URL('../../../../vds/99-vds-dualsense-udev.rules', import.meta.url), 'utf8');

describe('WirePlumber managed config', () => {
  it('distinguishes current, legacy, modified and missing files', () => {
    const dir = mkdtempSync(path.join(tmpdir(), 'opends5-wp-'));
    const file = path.join(dir, '99-vds.conf');
    expect(inspectWirePlumberConfig(file, 'current', { managedPaths: [] }).status).toBe('missing');
    writeFileSync(file, 'alsa_card.usb-Sony_Interactive_Entertainment_DualSense.*');
    expect(inspectWirePlumberConfig(file, 'current').status).toBe('known-legacy');
    writeFileSync(file, 'other');
    expect(inspectWirePlumberConfig(file, 'current').status).toBe('modified');
    writeFileSync(file, 'current');
    expect(inspectWirePlumberConfig(file, 'current').status).toBe('current');
  });

  it('requires approval and preserves a backup while replacing', () => {
    const dir = mkdtempSync(path.join(tmpdir(), 'opends5-wp-'));
    const file = path.join(dir, '99-vds.conf');
    writeFileSync(file, 'modified');
    const report = inspectWirePlumberConfig(file, 'current');
    expect(() => repairWirePlumberConfig(file, 'current', report, { approve: false })).toThrow(/approval/);
    const backup = repairWirePlumberConfig(file, 'current', report, { approve: true });
    expect(readFileSync(file, 'utf8')).toBe('current');
    expect(backup).toBeTruthy();
    expect(readFileSync(backup!, 'utf8')).toBe('modified');
    expect(path.dirname(backup!)).toBe(dir);
  });

  it('reports actionable reload status after a user WirePlumber restart', async () => {
    const result = await restartWirePlumberAndWait(async () => true, { restart: async () => undefined });
    expect(result).toBe('ready');
    const failed = await restartWirePlumberAndWait(async () => false, {
      restart: async () => undefined, timeoutMs: 1, pollMs: 1
    });
    expect(failed).toBe('reload-required');
  });

  it('uses distinct valid WirePlumber ERE matchers and tags the card and sink separately', () => {
    expect(configSource).not.toMatch(/"[^"]+": "~\*[^ "]+/);
    expect(configSource).toContain('"device.name": "~^alsa_card');
    expect(configSource).toContain('"node.name": "~^alsa_output');
    const cardRuleEnd = configSource.indexOf('  {', configSource.indexOf('"device.name"') + 1);
    const cardRule = configSource.slice(0, cardRuleEnd);
    const sinkRuleEnd = configSource.indexOf('  {', configSource.indexOf('"node.name"') + 1);
    const sinkRule = configSource.slice(configSource.indexOf('"node.name"'), sinkRuleEnd);
    expect(cardRule).toContain('"opends5.vds": true');
    expect(cardRule).toContain('"opends5.haptics.version": "1"');
    expect(cardRule).toContain('"device.profile": "pro-audio"');
    expect(sinkRule).toContain('"opends5.vds": true');
    expect(sinkRule).toContain('"audio.position": [ "FL", "FR", "RL", "RR" ]');
  });

  it('detects package-managed files and refuses symlink repair', () => {
    const dir = mkdtempSync(path.join(tmpdir(), 'opends5-wp-'));
    const file = path.join(dir, '99-vds.conf');
    const managed = path.join(dir, 'system.conf');
    writeFileSync(managed, configSource);
    expect(inspectWirePlumberConfig(file, configSource, { managedPaths: [managed] }).status).toBe('package-managed');
    const target = path.join(dir, 'target');
    writeFileSync(target, 'legacy');
    symlinkSync(target, file);
    const report = inspectWirePlumberConfig(file, configSource, { managedPaths: [] });
    expect(report.status).toBe('symlink');
    expect(() => repairWirePlumberConfig(file, configSource, report, { approve: true })).toThrow(/symlink/);
  });

  it('creates a missing config hierarchy only below the user XDG root', () => {
    const root = mkdtempSync(path.join(tmpdir(), 'opends5-xdg-'));
    const file = path.join(root, 'wireplumber', 'wireplumber.conf.d', '99-vds.conf');
    const report = inspectWirePlumberConfig(file, configSource, { managedPaths: [] });
    expect(report.status).toBe('missing');
    repairWirePlumberConfig(file, configSource, report, { approve: true, configRoot: root });
    expect(readFileSync(file, 'utf8')).toBe(configSource);
    expect(statSync(path.join(root, 'wireplumber')).mode & 0o777).toBe(0o700);
    expect(statSync(path.dirname(file)).mode & 0o777).toBe(0o700);
  });

  it('rejects a symlinked config parent while creating a missing file', () => {
    const root = mkdtempSync(path.join(tmpdir(), 'opends5-xdg-'));
    const target = mkdtempSync(path.join(tmpdir(), 'opends5-target-'));
    symlinkSync(target, path.join(root, 'wireplumber'));
    const file = path.join(root, 'wireplumber', 'wireplumber.conf.d', '99-vds.conf');
    const report = inspectWirePlumberConfig(file, configSource, { managedPaths: [] });
    expect(() => repairWirePlumberConfig(file, configSource, report, { approve: true, configRoot: root })).toThrow(/symlink/);
  });

  it('detects only active precedence files, not documentation examples', () => {
    const root = mkdtempSync(path.join(tmpdir(), 'opends5-paths-'));
    const active = path.join(root, 'etc', 'xdg');
    mkdirSync(path.join(active, 'wireplumber', 'wireplumber.conf.d'), { recursive: true });
    const activeFile = path.join(active, 'wireplumber', 'wireplumber.conf.d', '99-vds-dualsense.conf');
    writeFileSync(activeFile, configSource);
    const userFile = path.join(root, 'user', 'wireplumber.conf');
    expect(inspectWirePlumberConfig(userFile, configSource, { configDirs: [active], dataDirs: [] }).status).toBe('package-managed');
    const docs = path.join(root, 'docs');
    mkdirSync(path.join(docs, 'vds', 'examples'), { recursive: true });
    writeFileSync(path.join(docs, 'vds', 'examples', '99-vds-dualsense-wireplumber.conf'), configSource);
    expect(inspectWirePlumberConfig(path.join(root, 'other', 'wireplumber.conf'), configSource, { managedPaths: [] }).status).toBe('missing');
  });

  it('matches only the OpenDS5 virtual identity and preserves Proton-facing names', () => {
    const card = /^alsa_card\.usb-OpenDS5_vDS_[A-Za-z0-9_.-]+$/;
    const sink = /^alsa_output\.usb-OpenDS5_vDS_[A-Za-z0-9_.-]+$/;
    expect(card.test('alsa_card.usb-Sony_Interactive_Entertainment_DualSense_Wireless_Controller')).toBe(false);
    expect(sink.test('alsa_output.usb-Sony_Interactive_Entertainment_DualSense_Wireless_Controller')).toBe(false);
    expect(card.test('alsa_card.usb-OpenDS5_vDS_pci-0000_00_14_0-card0')).toBe(true);
    expect(sink.test('alsa_output.usb-OpenDS5_vDS_pci-0000_00_14_0-card0_pcmC0D0p')).toBe(true);
    expect(configSource).not.toContain('Sony_Interactive_Entertainment_DualSense');
    expect(configSource).toContain('"device.description": "DualSense Wireless Controller"');
    expect(configSource).toContain('"node.description": "Wireless Controller"');
    expect(udevSource).toContain('KERNELS=="vds_hcd.*"');
    expect(udevSource).toContain('IMPORT{builtin}="path_id"');
    expect(udevSource).toContain('SUBSYSTEM=="sound", KERNEL=="card*"');
    expect(udevSource).toContain('SUBSYSTEM=="sound", KERNEL=="pcm*"');
    expect(udevSource).toContain('ENV{ID_SERIAL}:="OpenDS5_vDS_%E{ID_PATH_TAG}_%k"');
    expect(udevSource).toContain('ENV{ID_SERIAL}:="OpenDS5_vDS_%k"');
  });

  it('uses an exclusive temporary file and removes it after a collision', () => {
    const dir = mkdtempSync(path.join(tmpdir(), 'opends5-wp-'));
    const file = path.join(dir, '99-vds.conf');
    const collision = path.join(dir, '.99-vds.conf.tmp-collision');
    writeFileSync(file, 'legacy');
    writeFileSync(collision, 'do not replace');
    let calls = 0;
    repairWirePlumberConfig(file, 'current', inspectWirePlumberConfig(file, 'current', { managedPaths: [] }), {
      approve: true,
      backupPathFactory: () => calls++ === 0 ? collision : path.join(dir, '.99-vds.conf.bak-success'),
      tempPathFactory: () => path.join(dir, '.99-vds.conf.tmp-success')
    });
    expect(readFileSync(file, 'utf8')).toBe('current');
    expect(readFileSync(collision, 'utf8')).toBe('do not replace');
    expect(readFileSync(path.join(dir, '.99-vds.conf.bak-success'), 'utf8')).toBe('legacy');
    expect(() => lstatSync(path.join(dir, '.99-vds.conf.tmp-success'))).toThrow();
  });
});
