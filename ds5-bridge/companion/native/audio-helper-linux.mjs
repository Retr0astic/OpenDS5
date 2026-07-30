#!/usr/bin/env node
// Linux audio helper for the DS5 Bridge companion app.
//
// Speaks the same CLI + stdio protocol as the Windows AudioHelper.exe, but
// drives PipeWire: the vds virtual USB DualSense exposes a 4-channel pro-audio
// sink where channels 1-2 are the speaker/headphone path and channels 3-4 are
// the left/right haptic actuators.
import { spawn, execFile } from 'node:child_process';
import { createInterface } from 'node:readline';
import process from 'node:process';
import { pathToFileURL } from 'node:url';

const SAMPLE_RATE = 48000;
// Small seam used by the real-time capture path and behavioral tests. A
// blocked child stdin drops the chunk immediately; later calls resume once
// writableNeedDrain clears, without buffering or diagnostic spam.
export function createHapticsInputWriter(stdin, onDrop = () => {}) {
  let dropped = false;
  return (chunk) => {
    if (!stdin.writable || stdin.writableNeedDrain) {
      ++dropped;
      if (dropped === 1) onDrop(dropped);
      return false;
    }
    if (!stdin.write(chunk)) {
      ++dropped;
      if (dropped === 1) onDrop(dropped);
      return false;
    }
    return true;
  };
}
export const BRIDGE_ENDPOINT_ISSUES = Object.freeze({
  MISSING_CARD: 'missing-card',
  MISSING_SINK: 'missing-sink',
  PARENT_MISMATCH: 'parent-mismatch',
  NOT_PRO_AUDIO: 'not-pro-audio',
  WRONG_CHANNEL_COUNT: 'wrong-channel-count',
  WRONG_CHANNEL_MAP: 'wrong-channel-map',
  AMBIGUOUS: 'ambiguous',
  STALE: 'stale'
});
const REQUIRED_HAPTICS_POSITION = ['FL', 'FR', 'RL', 'RR'];

const BASS_FOCUS_CUTOFF_HZ = { deep: 80, balanced: 160, punchy: 240, wide: 400 }; // matches UI labels
const RESPONSE_GAIN = { subtle: 0.6, balanced: 1.0, strong: 1.5 };
const ATTACK_MS = { soft: 30, balanced: 15, fast: 8, sharp: 3 };
const RELEASE_MS = { tight: 60, balanced: 120, smooth: 250, long: 450 };

function argValue(args, flag) {
  const index = args.indexOf(flag);
  return index >= 0 && index + 1 < args.length ? args[index + 1] : null;
}

function fail(message) {
  process.stderr.write(`${message}\n`);
  process.exit(1);
}

export function pwDump() {
  return new Promise((resolve, reject) => {
    execFile('pw-dump', [], {
      maxBuffer: 64 * 1024 * 1024,
      timeout: 2000,
      killSignal: 'SIGKILL'
    }, (error, stdout) => {
      if (error) {
        if (error.killed || error.code === 'ETIMEDOUT') error.code = 'PW_DUMP_TIMEOUT';
        reject(error);
        return;
      }
      try {
        resolve(JSON.parse(stdout));
      } catch (parseError) {
        reject(parseError);
      }
    });
  });
}

function nodeProps(object) {
  return object?.info?.props ?? {};
}

const STEREO_LAYOUT = { channels: 2, position: ['FL', 'FR'] };

// Negotiated audio format of a pw-dump node; stereo fallback when absent
// or inconsistent (spec: unknown layouts read as first-two-channel stereo).
export function nodeChannelLayout(node) {
  const format = node?.info?.params?.Format?.[0];
  const channels = Number(format?.channels ?? 0);
  const position = Array.isArray(format?.position) ? format.position : null;
  if (!position || channels < 1 || position.length !== channels) {
    return { ...STEREO_LAYOUT, position: [...STEREO_LAYOUT.position] };
  }
  return { channels, position: [...position] };
}

export function channelIndices(position) {
  const stride = position.length;
  let fl = position.indexOf('FL');
  let fr = position.indexOf('FR');
  if (fl < 0 || fr < 0) {
    fl = 0;
    fr = Math.min(1, stride - 1);
  }
  return { stride, fl, fr, fc: position.indexOf('FC'), lfe: position.indexOf('LFE') };
}

function isAudioSink(object) {
  return object.type === 'PipeWire:Interface:Node'
    && nodeProps(object)['media.class'] === 'Audio/Sink';
}

function isTaggedVdsCard(object) {
  const props = nodeProps(object);
  return object?.type === 'PipeWire:Interface:Device'
    && (props['opends5.vds'] === true || props['opends5.vds'] === 'true')
    && props['opends5.haptics.version'] === '1';
}

function isTaggedBridgeSink(object, cardId) {
  const props = nodeProps(object);
  return isAudioSink(object)
    && (props['opends5.vds'] === true || props['opends5.vds'] === 'true')
    && props['opends5.haptics.version'] === '1'
    && String(props['device.id'] ?? '') === String(cardId);
}

const LEGACY_VDS_CARD = /^alsa_card\.usb-OpenDS5_vDS_[A-Za-z0-9_.-]+$/;
const LEGACY_VDS_SINK = /^alsa_output\.usb-OpenDS5_vDS_[A-Za-z0-9_.-]+$/;
// Some PipeWire/WirePlumber setups expose the physical DualSense ALSA card
// without OpenDS5 metadata. Keep this compatibility path deliberately exact:
// it must not broaden endpoint selection to arbitrary DualSense devices.
const SONY_DUALSENSE_CARD = 'alsa_card.usb-Sony_Interactive_Entertainment_DualSense_Wireless_Controller-00';
const SONY_DUALSENSE_SINK = 'alsa_output.usb-Sony_Interactive_Entertainment_DualSense_Wireless_Controller-00.pro-output-0';
function normalizeAudioPosition(value) {
  if (Array.isArray(value)) return value;
  if (typeof value !== 'string') return null;
  try {
    const parsed = JSON.parse(value);
    return Array.isArray(parsed) ? parsed : null;
  } catch {
    return null;
  }
}

function isRequiredHapticsPosition(value) {
  const position = normalizeAudioPosition(value);
  return position?.length === REQUIRED_HAPTICS_POSITION.length
    && position.every((channel, index) => channel === REQUIRED_HAPTICS_POSITION[index]);
}

function isLegacyVdsCard(object) {
  return object?.type === 'PipeWire:Interface:Device'
    && LEGACY_VDS_CARD.test(String(nodeProps(object)['device.name'] ?? ''));
}
function isLegacyVdsSink(object) {
  return isAudioSink(object) && LEGACY_VDS_SINK.test(String(nodeProps(object)['node.name'] ?? ''));
}
function isSonyDualsenseCard(object) {
  return object?.type === 'PipeWire:Interface:Device'
    && nodeProps(object)['device.name'] === SONY_DUALSENSE_CARD
    && hasVdsHcdAncestry(nodeProps(object));
}
function isSonyDualsenseSink(object) {
  return isAudioSink(object) && nodeProps(object)['node.name'] === SONY_DUALSENSE_SINK;
}
function hasVdsHcdAncestry(props) {
  return ['device.bus-path', 'device.sysfs.path'].some((key) => {
    const value = props?.[key];
    if (typeof value !== 'string') return false;
    return value.split('/').some((component) => /^vds_hcd(?:\.\d+)?$/.test(component));
  });
}
function endpointMetadataIssue(card, sink) {
  const props = nodeProps(sink);
  if (Number(props['audio.channels']) !== 4) return BRIDGE_ENDPOINT_ISSUES.WRONG_CHANNEL_COUNT;
  const format = sink?.info?.params?.Format?.[0];
  if (format && (Number(format.channels) !== 4
    || !Array.isArray(format.position)
    || format.position.length !== 4
    || format.position.some((value, index) => value !== REQUIRED_HAPTICS_POSITION[index]))) {
    return BRIDGE_ENDPOINT_ISSUES.WRONG_CHANNEL_MAP;
  }
  if (props['audio.position'] !== undefined && !isRequiredHapticsPosition(props['audio.position'])) {
    return BRIDGE_ENDPOINT_ISSUES.WRONG_CHANNEL_MAP;
  }
  return null;
}

function inspectBridgeEndpoint(objects) {
  const cards = objects.filter(isTaggedVdsCard);
  if (cards.length === 0) return { endpoint: null, issue: BRIDGE_ENDPOINT_ISSUES.MISSING_CARD };
  if (cards.length > 1) return { endpoint: null, issue: BRIDGE_ENDPOINT_ISSUES.AMBIGUOUS };
  const card = cards[0];
  const taggedSinks = objects.filter((object) => isAudioSink(object)
    && (nodeProps(object)['opends5.vds'] === true || nodeProps(object)['opends5.vds'] === 'true')
    && nodeProps(object)['opends5.haptics.version'] === '1');
  const sinks = taggedSinks.filter((object) => isTaggedBridgeSink(object, card.id));
  if (sinks.length === 0) {
    return { endpoint: null, issue: taggedSinks.length > 0
      ? BRIDGE_ENDPOINT_ISSUES.PARENT_MISMATCH : BRIDGE_ENDPOINT_ISSUES.MISSING_SINK };
  }
  if (sinks.length > 1) return { endpoint: null, issue: BRIDGE_ENDPOINT_ISSUES.AMBIGUOUS };
  const sink = sinks[0];
  const profile = nodeProps(card)['device.profile'] ?? nodeProps(card)['device.profile.name'];
  if (profile !== 'pro-audio') return { endpoint: null, issue: BRIDGE_ENDPOINT_ISSUES.NOT_PRO_AUDIO };
  const props = nodeProps(sink);
  if (Number(props['audio.channels']) !== 4) return { endpoint: null, issue: BRIDGE_ENDPOINT_ISSUES.WRONG_CHANNEL_COUNT };
  const metadataIssue = endpointMetadataIssue(card, sink);
  if (metadataIssue) return { endpoint: null, issue: metadataIssue };
  return { endpoint: { card, sink }, issue: null };
}

function inspectLegacyBridgeEndpoint(objects) {
  const cards = objects.filter(isLegacyVdsCard);
  if (cards.length === 0) return { endpoint: null, issue: BRIDGE_ENDPOINT_ISSUES.MISSING_CARD };
  if (cards.length > 1) return { endpoint: null, issue: BRIDGE_ENDPOINT_ISSUES.AMBIGUOUS };
  const card = cards[0];
  if ((nodeProps(card)['device.profile'] ?? nodeProps(card)['device.profile.name']) !== 'pro-audio') {
    return { endpoint: null, issue: BRIDGE_ENDPOINT_ISSUES.NOT_PRO_AUDIO };
  }
  const sinks = objects.filter(isLegacyVdsSink).filter((sink) => {
    const props = nodeProps(sink);
    return String(props['device.id'] ?? '') === String(card.id);
  });
  if (sinks.length === 0) {
    return { endpoint: null, issue: objects.some(isLegacyVdsSink)
      ? BRIDGE_ENDPOINT_ISSUES.PARENT_MISMATCH : BRIDGE_ENDPOINT_ISSUES.MISSING_SINK };
  }
  if (sinks.length > 1) return { endpoint: null, issue: BRIDGE_ENDPOINT_ISSUES.AMBIGUOUS };
  const metadataIssue = endpointMetadataIssue(card, sinks[0]);
  if (metadataIssue) return { endpoint: null, issue: metadataIssue };
  return { endpoint: { card, sink: sinks[0] }, issue: null };
}

function inspectSonyDualsenseEndpoint(objects) {
  const cards = objects.filter(isSonyDualsenseCard);
  if (cards.length === 0) return { endpoint: null, issue: BRIDGE_ENDPOINT_ISSUES.MISSING_CARD };
  if (cards.length > 1) return { endpoint: null, issue: BRIDGE_ENDPOINT_ISSUES.AMBIGUOUS };
  const card = cards[0];
  const profile = nodeProps(card)['device.profile'] ?? nodeProps(card)['device.profile.name'];
  if (profile !== 'pro-audio') return { endpoint: null, issue: BRIDGE_ENDPOINT_ISSUES.NOT_PRO_AUDIO };
  const namedSinks = objects.filter(isSonyDualsenseSink);
  const sinks = namedSinks.filter((sink) => String(nodeProps(sink)['device.id'] ?? '') === String(card.id));
  if (sinks.length === 0) {
    return { endpoint: null, issue: namedSinks.length > 0
      ? BRIDGE_ENDPOINT_ISSUES.PARENT_MISMATCH : BRIDGE_ENDPOINT_ISSUES.MISSING_SINK };
  }
  if (sinks.length > 1) return { endpoint: null, issue: BRIDGE_ENDPOINT_ISSUES.AMBIGUOUS };
  const sink = sinks[0];
  const metadataIssue = endpointMetadataIssue(card, sink);
  if (metadataIssue) return { endpoint: null, issue: metadataIssue };
  return { endpoint: { card, sink }, issue: null };
}

export function resolveBridgeEndpoint(objects) {
  const list = Array.isArray(objects) ? objects : [];
  const tagged = inspectBridgeEndpoint(list);
  // Once a tagged vDS card is present, its result is authoritative—even when
  // its sink is not ready yet. Falling through to a physical Sony endpoint in
  // that state could route haptics to a different controller.
  if (tagged.issue !== BRIDGE_ENDPOINT_ISSUES.MISSING_CARD) return tagged;
  const legacy = inspectLegacyBridgeEndpoint(list);
  if (legacy.endpoint || legacy.issue !== BRIDGE_ENDPOINT_ISSUES.MISSING_CARD) return legacy;
  return inspectSonyDualsenseEndpoint(list);
}

async function findBridgeSink() {
  return resolveBridgeEndpoint(await pwDump()).endpoint?.sink ?? null;
}

async function requireBridgeSink() {
  const result = resolveBridgeEndpoint(await pwDump());
  if (!result.endpoint) {
    fail(`status: capture-unavailable endpoint-${result.issue}. OpenDS5 vDS audio endpoint is not ready.`);
  }
  return result.endpoint.sink;
}

function biquadLowpass(cutoffHz) {
  const w0 = 2 * Math.PI * cutoffHz / SAMPLE_RATE;
  const alpha = Math.sin(w0) / (2 * Math.SQRT1_2);
  const cosW0 = Math.cos(w0);
  const a0 = 1 + alpha;
  return {
    b0: ((1 - cosW0) / 2) / a0,
    b1: (1 - cosW0) / a0,
    b2: ((1 - cosW0) / 2) / a0,
    a1: (-2 * cosW0) / a0,
    a2: (1 - alpha) / a0,
    x1: 0, x2: 0, y1: 0, y2: 0
  };
}

function biquadStep(f, x) {
  const y = f.b0 * x + f.b1 * f.x1 + f.b2 * f.x2 - f.a1 * f.y1 - f.a2 * f.y2;
  f.x2 = f.x1; f.x1 = x;
  f.y2 = f.y1; f.y1 = y;
  return y;
}

function envelopeCoefficient(milliseconds) {
  return Math.exp(-1 / (SAMPLE_RATE * (milliseconds / 1000)));
}

// The guard pins the haptic channels while the ear channels track the
// knob, so compensation must be computed from the sink's real per-channel
// volumes: desired scaling (ears when syncing, unity when not) over what
// the sink actually applies to the haptic pair. Correct in all four
// toggle states, guard running or not.
export function channelCompensation(earsLinear, hapticLinear, volumeSync) {
  const desired = volumeSync
    ? (Number.isFinite(earsLinear) && earsLinear > 0 ? earsLinear : 1)
    : 1;
  const actual = Number.isFinite(hapticLinear) && hapticLinear > 0 ? hapticLinear : 1;
  return Math.min(32, Math.max(1 / 32, desired / actual));
}

export class HapticsProcessor {
  constructor(config) {
    this.envelope = 0;
    this.layout = { stride: 4, fl: 0, fr: 1, fc: -1, lfe: -1 };
    this.outputCompensation = 1;
    // Volume Sync (default ON): haptics follow the listening volume. This is
    // an input to the per-channel compensation math (see channelCompensation),
    // which the poll reads via processor.volumeSync; process() always applies
    // the resulting outputCompensation.
    this.volumeSync = true;
    this.setConfig(config);
  }

  setOutputCompensation(compensation) {
    this.outputCompensation = compensation;
  }

  setVolumeSync(enabled) {
    this.volumeSync = enabled;
  }

  setConfig({ gainPercent, bassFocus, response, attack, release }) {
    this.gain = Math.max(0, Math.min(400, gainPercent)) / 100;
    this.responseGain = RESPONSE_GAIN[response] ?? 1.0;
    this.attackCoeff = envelopeCoefficient(ATTACK_MS[attack] ?? 15);
    this.releaseCoeff = envelopeCoefficient(RELEASE_MS[release] ?? 120);
    const cutoff = BASS_FOCUS_CUTOFF_HZ[bassFocus] ?? 90;
    this.lowpassLeft = biquadLowpass(cutoff);
    this.lowpassRight = biquadLowpass(cutoff);
  }

  setInputLayout(layout) {
    this.layout = layout;
  }

  // Layout-aware input (see setInputLayout) -> 4ch f32 out
  // (speaker channels silent, haptics on 3-4). Blend per spec:
  // left/right = FL/FR + 0.5*FC + 1.0*LFE; rears and sides ignored.
  process(input) {
    const { stride, fl, fr, fc, lfe } = this.layout;
    const frames = Math.floor(input.length / stride);
    const output = new Float32Array(frames * 4);
    for (let frame = 0; frame < frames; frame += 1) {
      const base = frame * stride;
      const center = fc >= 0 ? input[base + fc] * 0.5 : 0;
      const bass = lfe >= 0 ? input[base + lfe] : 0;
      const left = biquadStep(this.lowpassLeft, input[base + fl] + center + bass);
      const right = biquadStep(this.lowpassRight, input[base + fr] + center + bass);
      const peak = Math.max(Math.abs(left), Math.abs(right));
      const coeff = peak > this.envelope ? this.attackCoeff : this.releaseCoeff;
      this.envelope = coeff * this.envelope + (1 - coeff) * peak;
      // Music bass peaks well below full scale and the daemon quantizes
      // haptics to 8 bits, so drive hard into the tanh limiter; gate the
      // noise floor so silence does not buzz the actuators.
      const gate = this.envelope < 0.003 ? 0 : 1;
      const drive = this.gain * this.responseGain * 4 * gate;
      // Compensation is always applied: the poll computes ears/haptic from
      // the sink's real per-channel volumes (unity when no guard runs and
      // sync ON, the ear volume when the guard pins the haptic pair).
      const comp = this.outputCompensation;
      output[frame * 4 + 2] = Math.max(-1, Math.min(1, Math.tanh(left * drive) * comp));
      output[frame * 4 + 3] = Math.max(-1, Math.min(1, Math.tanh(right * drive) * comp));
    }
    return output;
  }
}

export function readHapticsConfig(args) {
  return {
    gainPercent: Number(argValue(args, '--haptics-gain') ?? 100),
    bassFocus: argValue(args, '--haptics-bass-focus') ?? 'balanced',
    response: argValue(args, '--haptics-response') ?? 'balanced',
    attack: argValue(args, '--haptics-attack') ?? 'balanced',
    release: argValue(args, '--haptics-release') ?? 'balanced',
    // Volume Sync defaults ON: only an explicit "0" disables it.
    volumeSync: argValue(args, '--haptics-volume-sync') !== '0'
  };
}

async function runRenderLoopbackHaptics(args) {
  const sink = await requireBridgeSink();
  const config = readHapticsConfig(args);
  const processor = new HapticsProcessor(config);
  processor.setVolumeSync(config.volumeSync);

  const appSource = {
    processId: Number(argValue(args, '--haptics-app-process-id') ?? 0),
    processPath: argValue(args, '--haptics-app-process-path'),
    executableName: argValue(args, '--haptics-app-executable'),
    sessionIdentifier: argValue(args, '--haptics-app-session-id')
  };
  const hasAppSource = appSource.processId > 0 || Boolean(appSource.processPath)
    || Boolean(appSource.executableName) || Boolean(appSource.sessionIdentifier);

  // Optional capture pin: monitor a specific output device instead of
  // following the system default sink.
  const captureDevice = argValue(args, '--haptics-output-device');

  const hapticsClient = process.env.OPENDS5_HAPTICS_CLIENT || 'vds-haptics-client';
  const play = spawn(hapticsClient, hapticsClientArgs(args), { stdio: ['pipe', 'ignore', 'pipe'] });
  play.stderr.on('data', (chunk) => process.stderr.write(chunk));

  let record = null;
  let attachTimer = null;
  let volumeTimer = null;
  let stopping = false;
  let announcedRecording = false;
  let droppedInputChunks = 0;
  let dropDiagnosticEmitted = false;
  const writeHaptics = createHapticsInputWriter(play.stdin, () => {
    droppedInputChunks += 1;
    if (!dropDiagnosticEmitted) {
      dropDiagnosticEmitted = true;
      process.stderr.write('status: haptics-input-drop\n');
    }
  });

  // Playback always goes to the bridge sink. Its per-channel volumes
  // (ears = channel 0, haptic = channel 2) drive the compensation: the
  // volume guard may pin the haptic pair at unity while the ear channels
  // track the knob, so a single scalar is not enough. Poll pw-dump, cache
  // the channel volumes, and recompute so a live sync toggle applies at
  // once instead of waiting for the next poll.
  let lastChannelVolumes = null;
  let lastCompError = null;
  let volumeCompensationInFlight = false;
  const applyCompensation = () => {
    if (lastChannelVolumes) {
      processor.setOutputCompensation(
        channelCompensation(lastChannelVolumes[0], lastChannelVolumes[2], processor.volumeSync)
      );
    }
  };
  const refreshVolumeCompensation = async () => {
    if (volumeCompensationInFlight) return;
    volumeCompensationInFlight = true;
    try {
      const objects = await pwDump();
      const found = objects.find((object) => object.id === sink.id)
        ?? resolveBridgeEndpoint(objects).endpoint?.sink
        ?? null;
      const cv = found?.info?.params?.Props?.[0]?.channelVolumes;
      if (Array.isArray(cv) && cv.length >= 3) {
        lastChannelVolumes = cv;
        applyCompensation();
      }
      // channelVolumes unavailable: keep the last compensation.
    } catch (error) {
      if (error.message !== lastCompError) {
        lastCompError = error.message;
        process.stderr.write(`volume compensation poll failed: ${error.message}\n`);
      }
    } finally {
      volumeCompensationInFlight = false;
    }
  };
  refreshVolumeCompensation();
  volumeTimer = setInterval(refreshVolumeCompensation, 2000);

  const shutdown = (code, detail) => {
    stopping = true;
    if (detail) {
      process.stderr.write(`${detail}\n`);
    }
    clearTimeout(attachTimer);
    clearInterval(volumeTimer);
    record?.kill();
    for (const stream of appStreams.values()) {
      stream.proc.kill();
    }
    play.kill();
    process.exit(code);
  };
  play.on('error', (error) => shutdown(1, `haptics IPC client failed: ${error.message}`));
  play.on('exit', (code) => shutdown(code ?? 1, 'haptics IPC stream ended'));

  const pipeRecordToProcessor = (proc, stride) => {
    let carry = Buffer.alloc(0);
    proc.stdout.on('data', (chunk) => {
      let data = carry.length ? Buffer.concat([carry, chunk]) : chunk;
      const frameBytes = stride * 4; // stride channels x f32
      const usable = data.length - (data.length % frameBytes);
      carry = data.subarray(usable);
      if (usable === 0) {
        return;
      }
      const input = new Float32Array(data.buffer, data.byteOffset, usable / 4);
      const output = processor.process(input);
      writeHaptics(Buffer.from(output.buffer, 0, output.byteLength));
    });
    proc.stderr.on('data', (chunk) => process.stderr.write(chunk));
    proc.on('spawn', () => {
      if (!announcedRecording) {
        announcedRecording = true;
        process.stderr.write('status: recording-started\n');
      }
    });
  };

  const startSinkMonitorCapture = () => {
    record = spawn('pw-record', [
      '--raw',
      '-P', '{ stream.capture.sink = true }',
      ...(captureDevice ? ['--target', captureDevice] : []),
      // Capture four discrete channels and let the processor use the fronts
      // only. When the headset is plugged in, the default sink can be the
      // bridge's own 4-channel device; any narrower capture goes through
      // PipeWire's channel mixer, which folds the rear haptics channels we
      // play back into the fronts (constant buzz / self-oscillation). With a
      // matching 4ch format no mixing happens; stereo sinks upmix with
      // silent rears, leaving the fronts intact either way.
      '--format', 'f32', '--rate', `${SAMPLE_RATE}`, '--channels', '4',
      '--channel-map', 'FL,FR,RL,RR',
      '--latency', '256',
      '-'
    ], { stdio: ['ignore', 'pipe', 'pipe'] });
    pipeRecordToProcessor(record, 4);
    record.on('exit', (code) => {
      if (!stopping) {
        shutdown(code ?? 1, 'capture stream ended');
      }
    });
  };

  // Pre-fold app capture: target the app's own output streams so discrete
  // surround channels (especially LFE) survive even when the listening
  // device is stereo. Unreal titles publish several streams at once and only
  // one of them carries the mix, so capture every stream the app owns and sum
  // them. Streams come and go with the game, so re-check on a poll.
  const APP_POLL_MS = 2000;
  const MIX_BLOCK_FRAMES = 256;
  // Roughly 85ms of slack: enough to ride out jitter between the captures,
  // little enough that a stalled stream cannot build up audible latency.
  const MAX_LAG_FRAMES = MIX_BLOCK_FRAMES * 16;
  const appStreams = new Map();
  // How long a new stream is metered before we decide whether it carries the
  // mix. Long enough to survive a gap between game sound effects, short
  // enough that haptics start promptly.
  const PROBE_WINDOW_MS = 750;
  // A rejected stream is retried on a cooldown, because a game can start using
  // a stream it opened silently. Retry fast while nothing is live so haptics
  // pick up quickly, slowly once we already have audio.
  const DORMANT_RETRY_MS = 15000;
  const DORMANT_RETRY_IDLE_MS = 3000;
  // A live capture delivering no bytes at all is broken, not quiet. Drop it so
  // it cannot hold up the lockstep mix.
  const LIVE_STALL_MS = 10000;
  const dormantUntil = new Map();

  const mixReadyBlocks = () => {
    for (;;) {
      const ready = readyLiveStreams([...appStreams.values()], MIX_BLOCK_FRAMES);
      if (ready.length === 0) {
        return;
      }
      const blocks = ready.map((stream) => stream.take(MIX_BLOCK_FRAMES));
      const output = processor.process(sumBusFrames(blocks));
      writeHaptics(Buffer.from(output.buffer, 0, output.byteLength));
    }
  };

  const anyLive = () => [...appStreams.values()].some((stream) => stream.role === 'live');

  const startAppStream = (key, node) => {
    const layout = nodeChannelLayout(node);
    const indices = channelIndices(layout.position);
    const proc = spawn('pw-record', appCaptureRecordArgs(node, layout), { stdio: ['ignore', 'pipe', 'pipe'] });
    let pending = new Float32Array(0);
    let carry = Buffer.alloc(0);
    const stream = {
      proc,
      role: 'probing',
      probePeak: 0,
      lastDataAt: Date.now(),
      probeTimer: null,
      get frames() {
        return pending.length / BUS_CHANNELS;
      },
      take(count) {
        const wanted = count * BUS_CHANNELS;
        const block = pending.subarray(0, wanted);
        pending = pending.slice(wanted);
        return block;
      }
    };
    appStreams.set(key, stream);

    stream.probeTimer = setTimeout(() => {
      stream.probeTimer = null;
      if (stream.role !== 'probing') {
        return;
      }
      if (hasSignal(stream.probePeak)) {
        stream.role = 'live';
        process.stderr.write(`status: app-stream-live ${key}\n`);
        return;
      }
      process.stderr.write(`status: app-stream-silent ${key}\n`);
      dormantUntil.set(key, Date.now() + (anyLive() ? DORMANT_RETRY_MS : DORMANT_RETRY_IDLE_MS));
      stream.proc.kill();
    }, PROBE_WINDOW_MS);
    proc.stdout.on('data', (chunk) => {
      const data = carry.length ? Buffer.concat([carry, chunk]) : chunk;
      const frameBytes = layout.channels * 4; // channels x f32
      const usable = data.length - (data.length % frameBytes);
      carry = Buffer.from(data.subarray(usable));
      if (usable === 0) {
        return;
      }
      const input = new Float32Array(data.buffer, data.byteOffset, usable / 4);
      const bus = busFrames(input, indices);
      stream.lastDataAt = Date.now();
      if (stream.role === 'probing') {
        const peak = peakAmplitude(bus);
        if (peak > stream.probePeak) {
          stream.probePeak = peak;
        }
        return;
      }
      const merged = new Float32Array(pending.length + bus.length);
      merged.set(pending);
      merged.set(bus, pending.length);
      // Drop the oldest audio rather than let one lagging capture stall the
      // mix and grow the queue without bound.
      pending = merged.length > MAX_LAG_FRAMES * BUS_CHANNELS
        ? merged.slice(merged.length - MAX_LAG_FRAMES * BUS_CHANNELS)
        : merged;
      mixReadyBlocks();
    });
    proc.stderr.on('data', (data) => process.stderr.write(data));
    proc.on('spawn', () => {
      if (!announcedRecording) {
        announcedRecording = true;
        process.stderr.write('status: recording-started\n');
      }
    });
    proc.on('exit', () => {
      if (stream.probeTimer) {
        clearTimeout(stream.probeTimer);
        stream.probeTimer = null;
      }
      appStreams.delete(key);
      if (!stopping && appStreams.size === 0) {
        process.stderr.write('status: waiting-for-app\n');
      }
    });
  };

  const syncAppStreams = async () => {
    if (stopping) {
      return;
    }
    let nodes = [];
    try {
      nodes = matchAppStreamNodes(await pwDump(), appSource);
    } catch (error) {
      process.stderr.write(`app node poll failed: ${error.message}\n`);
    }
    const now = Date.now();
    const wanted = new Map(nodes.map((node) => [`${nodeProps(node)['object.serial'] ?? node.id}`, node]));

    for (const [key, stream] of appStreams) {
      if (!wanted.has(key)) {
        stream.proc.kill();
        continue;
      }
      // A live capture that has gone completely quiet on stdout is wedged, not
      // silent: silence still arrives as zero-filled buffers.
      if (stream.role === 'live' && now - stream.lastDataAt > LIVE_STALL_MS) {
        process.stderr.write(`status: app-stream-stalled ${key}\n`);
        dormantUntil.set(key, now + DORMANT_RETRY_IDLE_MS);
        stream.proc.kill();
      }
    }

    // Forget cooldowns for streams the app has closed, so a restarted game is
    // probed immediately rather than serving out a stale timer.
    for (const key of [...dormantUntil.keys()]) {
      if (!wanted.has(key)) {
        dormantUntil.delete(key);
      }
    }

    for (const [key, node] of wanted) {
      if (appStreams.has(key)) {
        continue;
      }
      const retryAt = dormantUntil.get(key);
      if (retryAt !== undefined && now < retryAt) {
        continue;
      }
      dormantUntil.delete(key);
      startAppStream(key, node);
    }
    attachTimer = setTimeout(syncAppStreams, APP_POLL_MS);
  };

  if (hasAppSource) {
    processor.setInputLayout(BUS_LAYOUT);
    process.stderr.write('status: waiting-for-app\n');
    await syncAppStreams();
  } else {
    startSinkMonitorCapture();
  }

  const control = createInterface({ input: process.stdin });
  control.on('line', (line) => {
    const parts = line.trim().split(/\s+/);
    if (parts[0] === 'haptics-config' && parts.length >= 6) {
      processor.setConfig({
        gainPercent: Number(parts[1]),
        bassFocus: parts[2],
        response: parts[3],
        attack: parts[4],
        release: parts[5]
      });
      // 7th field is optional so 6-field lines keep working: absent → sync ON.
      processor.setVolumeSync(parts[6] === undefined ? true : parts[6] === '1');
      // Recompute from the cached channel volumes so the toggle applies now.
      applyCompensation();
    } else if (parts[0] === 'stop') {
      shutdown(0);
    }
  });
  process.on('SIGTERM', () => shutdown(0));
  process.on('SIGINT', () => shutdown(0));
}

async function runPlayTestTone(args) {
  const sink = await requireBridgeSink();
  const target = nodeProps(sink)['node.name'];
  const audioPath = argValue(args, '--test-audio-path');
  if (!audioPath) {
    fail('missing --test-audio-path');
  }
  const volume = Math.max(0, Math.min(100, Number(argValue(args, '--speaker-volume') ?? 100))) / 100;
  await new Promise((resolve, reject) => {
    const play = spawn('pw-play', ['--target', target, '--volume', `${volume}`, audioPath], {
      stdio: ['ignore', 'ignore', 'inherit']
    });
    play.on('error', reject);
    play.on('exit', (code) => (code === 0 ? resolve() : reject(new Error(`pw-play exited ${code}`))));
  });
}

function buildHapticsTestWave(gainPercent) {
  const gain = Math.max(0, Math.min(400, gainPercent)) / 100;
  const seconds = 1.6;
  const frames = Math.round(seconds * SAMPLE_RATE);
  const wave = new Float32Array(frames * 4);
  for (let frame = 0; frame < frames; frame += 1) {
    const t = frame / SAMPLE_RATE;
    // Two rumble bursts: low 40 Hz swell, then a punchier 80 Hz pulse train.
    let amp = 0;
    if (t < 0.7) {
      amp = Math.sin(Math.PI * (t / 0.7)) * Math.sin(2 * Math.PI * 40 * t);
    } else if (t >= 0.8) {
      const u = t - 0.8;
      const pulse = Math.exp(-6 * (u % 0.25));
      amp = pulse * Math.sin(2 * Math.PI * 80 * t);
    }
    const sample = Math.tanh(amp * gain);
    wave[frame * 4 + 2] = sample;
    wave[frame * 4 + 3] = sample;
  }
  return wave;
}

async function runPlayTestHaptics(args) {
  const sink = await requireBridgeSink();
  const target = nodeProps(sink)['node.name'];
  const gain = Number(argValue(args, '--haptics-gain') ?? 100);
  const wave = buildHapticsTestWave(gain);
  await new Promise((resolve, reject) => {
    const play = spawn('pw-play', hapticsPlaybackArgs(target), { stdio: ['pipe', 'ignore', 'inherit'] });
    play.on('error', reject);
    play.on('exit', (code) => (code === 0 ? resolve() : reject(new Error(`pw-play exited ${code}`))));
    play.stdin.end(Buffer.from(wave.buffer));
  });
}

async function defaultSinkName() {
  return new Promise((resolve) => {
    execFile('wpctl', ['inspect', '@DEFAULT_AUDIO_SINK@'], (error, stdout) => {
      if (error) {
        resolve(null);
        return;
      }
      const description = stdout.match(/node\.description = "([^"]*)"/);
      const name = stdout.match(/node\.name = "([^"]*)"/);
      resolve({
        description: description?.[1] ?? '',
        name: name?.[1] ?? ''
      });
    });
  });
}

// Prints the audio output sinks as a JSON array for the Audio Haptics
// capture-device picker. The bridge's own sink is excluded: capturing it
// would feed the haptics we play back into the processor.
async function runListOutputSinks() {
  const objects = await pwDump();
  const current = await defaultSinkName();
  const bridgeSink = resolveBridgeEndpoint(objects).endpoint?.sink;
  const devices = objects
    .filter((object) => isAudioSink(object) && object.id !== bridgeSink?.id)
    .map((object) => {
      const props = nodeProps(object);
      const nodeName = props['node.name'] ?? '';
      return {
        nodeName,
        displayName: props['node.description'] || nodeName,
        isDefault: nodeName === (current?.name ?? '')
      };
    })
    .filter((device) => device.nodeName);
  process.stdout.write(`${JSON.stringify(devices)}\n`);
}

async function runEndpointStatus() {
  try {
    const result = resolveBridgeEndpoint(await pwDump());
    process.stdout.write(`${JSON.stringify(result.endpoint
      ? { status: 'ready', nodeName: nodeProps(result.endpoint.sink)['node.name'] }
      : { status: result.issue })}\n`);
  } catch (error) {
    process.stdout.write(`${JSON.stringify({
      status: 'stale',
      detail: error?.code === 'PW_DUMP_TIMEOUT' ? 'pw-dump-timeout' : 'pw-dump-unavailable'
    })}\n`);
  }
}

async function runDefaultRenderStatus() {
  const current = await defaultSinkName();
  const deviceName = current?.description || current?.name || '';
  let isBridgeEndpoint = false;
  try {
    const endpoint = resolveBridgeEndpoint(await pwDump()).endpoint;
    isBridgeEndpoint = Boolean(endpoint && nodeProps(endpoint.sink)['node.name'] === current?.name);
  } catch { /* report the default endpoint without guessing from its name */ }
  process.stdout.write(`${JSON.stringify({ deviceName, isBridgeEndpoint })}\n`);
}

async function runSetDefaultRenderBridge() {
  const sink = await requireBridgeSink();
  await new Promise((resolve, reject) => {
    execFile('wpctl', ['set-default', `${sink.id}`], (error) => (error ? reject(error) : resolve()));
  });
}

// The helper owns its output level (gain, limiter, sink-volume
// compensation), so pin the playback stream at unity and opt out of
// WirePlumber's stream-restore: a remembered mixer tweak on "pw-play"
// must not silently scale the haptics.
export function hapticsPlaybackArgs(target) {
  return [
    '--raw',
    '--target', target,
    '--volume', '1',
    '-P', '{ state.restore-props = false }',
    '--format', 'f32', '--rate', `${SAMPLE_RATE}`, '--channels', '4',
    '--channel-map', 'FL,FR,RL,RR',
    '--latency', '256',
    '-'
  ];
}

export function hapticsClientArgs(args, env = process.env) {
  const socket = argValue(args, '--haptics-socket')
    ?? env.OPENDS5_HAPTICS_SOCKET
    ?? '/run/vdsd.sock.haptics';
  const port = argValue(args, '--haptics-port')
    ?? env.OPENDS5_HAPTICS_PORT
    ?? '0';
  const streamId = Math.max(1, process.pid >>> 0);
  return ['--socket', socket, '--port', `${port}`, '--stream-id', `${streamId}`];
}

export function appCaptureRecordArgs(node, layout) {
  const serial = nodeProps(node)['object.serial'];
  return [
    '--raw',
    // Target the app's own output stream by serial: WirePlumber ignores a
    // plain --target <id> for playback streams and falls back to the
    // default source (the microphone), which is silence for haptics.
    '-P', `{ target.object = ${serial ?? node.id} }`,
    '--format', 'f32', '--rate', `${SAMPLE_RATE}`,
    '--channels', `${layout.channels}`,
    '--channel-map', layout.position.join(','),
    '--latency', '256',
    '-'
  ];
}

// Wine reports its loader as the client binary, so every Proton game looks
// identical here. Such a name identifies the runtime, never the game.
export const GENERIC_WINE_BINARIES = new Set([
  'wine', 'wine64', 'wine-preloader', 'wine64-preloader', 'wineserver'
]);

export function matchAppStreamNodes(objects, { processId, executableName, processPath, sessionIdentifier }) {
  const pathBasename = processPath ? processPath.split('/').pop() : null;
  const identifiesApp = (name) => Boolean(name) && !GENERIC_WINE_BINARIES.has(name);
  return objects.filter((object) => {
    if (object.type !== 'PipeWire:Interface:Node') {
      return false;
    }
    const props = nodeProps(object);
    if (props['media.class'] !== 'Stream/Output/Audio') {
      return false;
    }
    if (processId > 0 && Number(props['application.process.id'] ?? 0) === processId) {
      return true;
    }
    // The stream name outlives the process id across a game restart, and is
    // the only thing distinguishing one wine app from another.
    if (sessionIdentifier
      && (props['node.name'] === sessionIdentifier || props['application.name'] === sessionIdentifier)) {
      return true;
    }
    const binary = props['application.process.binary'] ?? null;
    return identifiesApp(binary)
      && (binary === executableName || binary === pathBasename);
  });
}

export function matchAppStreamNode(objects, appSource) {
  const matches = matchAppStreamNodes(objects, appSource);
  return matches.find((object) => object.info?.state === 'running') ?? matches[0] ?? null;
}

// The processor reads FL, FR, FC and LFE and ignores everything else, so
// every capture is reduced to those four lanes before mixing. Doing it here
// rather than asking PipeWire for a fixed channel map keeps the app's own
// LFE intact instead of letting the channel mixer synthesise or drop it.
export const BUS_CHANNELS = 4;
export const BUS_LAYOUT = { stride: BUS_CHANNELS, fl: 0, fr: 1, fc: 2, lfe: 3 };

export function busFrames(input, layout) {
  const { stride, fl, fr, fc, lfe } = layout;
  const frames = Math.floor(input.length / stride);
  const bus = new Float32Array(frames * BUS_CHANNELS);
  for (let frame = 0; frame < frames; frame += 1) {
    const from = frame * stride;
    const to = frame * BUS_CHANNELS;
    bus[to] = input[from + fl];
    bus[to + 1] = input[from + fr];
    bus[to + 2] = fc >= 0 ? input[from + fc] : 0;
    bus[to + 3] = lfe >= 0 ? input[from + lfe] : 0;
  }
  return bus;
}

// A stream that never rises above this is treated as silence. Games publish
// idle streams that emit exact zeros; real audio, even a quiet ambience bed,
// clears this by orders of magnitude. -80 dBFS.
export const SIGNAL_PEAK_THRESHOLD = 1e-4;

export function peakAmplitude(frames) {
  let peak = 0;
  for (let i = 0; i < frames.length; i += 1) {
    const value = frames[i] < 0 ? -frames[i] : frames[i];
    if (value > peak) {
      peak = value;
    }
  }
  return peak;
}

export function hasSignal(peak) {
  return peak > SIGNAL_PEAK_THRESHOLD;
}

// Probing captures are metered, never mixed: a stream still being evaluated
// must not gate output. Live streams are mixed in lockstep so the summed
// blocks stay sample-aligned.
export function readyLiveStreams(streams, blockFrames) {
  const live = streams.filter((stream) => stream.role === 'live');
  if (live.length === 0) {
    return [];
  }
  return live.every((stream) => stream.frames >= blockFrames) ? live : [];
}

export function sumBusFrames(blocks) {
  if (blocks.length === 1) {
    return blocks[0];
  }
  const summed = new Float32Array(blocks[0].length);
  for (const block of blocks) {
    for (let i = 0; i < summed.length; i += 1) {
      summed[i] += block[i];
    }
  }
  return summed;
}

async function listOutputStreamSessions() {
  const objects = await pwDump();
  const bridge = await findBridgeSink().catch(() => null);
  const bridgeName = bridge ? nodeProps(bridge)['node.name'] : '';
  const sessions = [];
  const seen = new Set();
  for (const object of objects) {
    if (object.type !== 'PipeWire:Interface:Node') {
      continue;
    }
    const props = nodeProps(object);
    if (props['media.class'] !== 'Stream/Output/Audio') {
      continue;
    }
    const processId = Number(props['application.process.id'] ?? 0);
    const displayName = props['application.name'] || props['node.name'] || 'Unknown';
    const key = `${processId}:${displayName}`;
    if (seen.has(key)) {
      continue;
    }
    seen.add(key);
    sessions.push({
      processId,
      displayName,
      executableName: props['application.process.binary'] ?? null,
      processPath: null,
      iconPath: null,
      sessionIdentifier: props['node.name'] ?? null,
      sessionInstanceIdentifier: `${object.id}`,
      state: object.info?.state === 'running' ? 'active' : 'inactive',
      endpointName: props['target.object'] ?? bridgeName ?? '',
      isSelected: false
    });
  }
  return sessions;
}

async function runMonitorAudioSessions() {
  let stopped = false;
  const emit = async () => {
    if (stopped) {
      return;
    }
    try {
      const sessions = await listOutputStreamSessions();
      process.stdout.write(`${JSON.stringify({ type: 'snapshot', sessions })}\n`);
    } catch (error) {
      process.stderr.write(`session snapshot failed: ${error.message}\n`);
    }
  };
  await emit();
  const timer = setInterval(emit, 2000);
  const control = createInterface({ input: process.stdin });
  const stop = () => {
    stopped = true;
    clearInterval(timer);
    process.exit(0);
  };
  control.on('line', (line) => {
    if (line.trim() === 'stop') {
      stop();
    }
  });
  control.on('close', stop);
  process.on('SIGTERM', stop);
}

// Desktop volume controls rewrite all four channels of the bridge sink;
// when HD Volume Sync is off the haptic pair (3-4) must stay at unity so
// game HD haptics don't fade with the listening volume.
export function pinnedChannelVolumes(current) {
  if (!Array.isArray(current) || current.length < 4) {
    return null;
  }
  if (current[2] === 1 && current[3] === 1) {
    return null;
  }
  return [current[0], current[1], 1, 1, ...current.slice(4)];
}

const VOLUME_GUARD_INTERVAL_MS = 2000;

async function runVolumeGuard() {
  let stopping = false;
  // Hard-fail the first tick when the sink is missing (mirrors the other
  // helper modes): the parent only starts the guard once the controller
  // audio path is ready, so a missing sink at boot is a real setup error.
  let sink = await requireBridgeSink();
  let pinInFlight = false;

  const pinTick = async () => {
    if (stopping || pinInFlight) {
      return;
    }
    pinInFlight = true;
    try {
      // Re-find the sink each tick so the guard survives the sink coming
      // and going with the controller. channelVolumes change as the user
      // adjusts volume, so always read them fresh from a live pw-dump.
      const found = await findBridgeSink();
      if (!found) {
        process.stderr.write('volume guard: bridge sink not found, retrying\n');
        return;
      }
      sink = found;
      const vols = sink.info?.params?.Props?.[0]?.channelVolumes;
      const pinned = pinnedChannelVolumes(vols);
      if (!pinned) {
        return;
      }
      await new Promise((resolve) => {
        execFile('pw-cli', [
          'set-param', `${sink.id}`, 'Props',
          `{ channelVolumes: [ ${pinned.join(', ')} ] }`
        ], (error) => {
          if (error) {
            process.stderr.write(`volume guard: set-param failed: ${error.message}\n`);
          }
          resolve();
        });
      });
    } catch (error) {
      process.stderr.write(`volume guard tick failed: ${error.message}\n`);
    } finally {
      pinInFlight = false;
    }
  };

  await pinTick();
  const timer = setInterval(pinTick, VOLUME_GUARD_INTERVAL_MS);
  const control = createInterface({ input: process.stdin });
  const stop = () => {
    stopping = true;
    clearInterval(timer);
    process.exit(0);
  };
  control.on('line', (line) => {
    if (line.trim() === 'stop') {
      stop();
    }
  });
  control.on('close', stop);
  process.on('SIGTERM', stop);
  process.on('SIGINT', stop);
}

function runMicKeepalive() {
  // Microphone input is unsupported over the vds Bluetooth transport; stay
  // alive so the engine's lifecycle management works, but do nothing.
  process.stderr.write('mic keepalive: unsupported on Linux (vds Bluetooth transport)\n');
  setInterval(() => {}, 60_000);
  process.on('SIGTERM', () => process.exit(0));
  process.on('SIGINT', () => process.exit(0));
}

async function main() {
  const args = process.argv.slice(2);
  if (args.includes('--play-test-tone')) {
    await runPlayTestTone(args);
  } else if (args.includes('--play-test-haptics')) {
    await runPlayTestHaptics(args);
  } else if (args.includes('--list-output-sinks')) {
    await runListOutputSinks();
  } else if (args.includes('--endpoint-status')) {
    await runEndpointStatus();
  } else if (args.includes('--default-render-status')) {
    await runDefaultRenderStatus();
  } else if (args.includes('--set-default-render-bridge')) {
    await runSetDefaultRenderBridge();
  } else if (args.includes('--monitor-audio-sessions')) {
    await runMonitorAudioSessions();
  } else if (args.includes('--volume-guard')) {
    await runVolumeGuard();
  } else if (args.includes('--mic-keepalive-only')) {
    runMicKeepalive();
  } else if (argValue(args, '--source') === 'render-loopback') {
    await runRenderLoopbackHaptics(args);
  } else {
    fail(`unsupported arguments: ${args.join(' ')}`);
  }
}

const isCliEntry = process.argv[1]
  && import.meta.url === pathToFileURL(process.argv[1]).href;
if (isCliEntry) {
  main().catch((error) => fail(error.message));
}
