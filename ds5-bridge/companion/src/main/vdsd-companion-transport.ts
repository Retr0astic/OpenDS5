import { EventEmitter } from 'node:events';
import { createConnection } from 'node:net';

type OpenOptions = {
  retryTimeoutMs?: number;
  retryDelayMs?: number;
  socketPath?: string;
};

type VdsdReply = {
  OK: boolean;
  error?: string;
  report?: number[];
  controlProtocol?: number;
  hapticsStreamProtocol?: number;
  features?: unknown;
};

export type VdsdCapabilities = {
  controlProtocol: number;
  hapticsStreamProtocol: number;
  features: string[];
};

const REQUEST_TIMEOUT_MS = 1500;
const DEFAULT_OPEN_RETRY_DELAY_MS = 50;
const REPORT_LENGTH = 64;
const DEFAULT_SOCKET_PATH = '/run/vdsd.sock';

export function defaultVdsdSocketPath(): string {
  return process.env.VDSD_SOCKET ?? DEFAULT_SOCKET_PATH;
}

/**
 * Companion transport that speaks to the vdsd control socket instead of the
 * Pico's WinUSB vendor interface. vdsd emulates the DS5 Bridge companion
 * report protocol behind a JSONL request/reply exchange; each request uses a
 * short-lived connection, mirroring how vdsctl talks to the daemon.
 */
export class VdsdCompanionTransport extends EventEmitter {
  private closed = false;

  private constructor(readonly path: string, readonly capabilities: VdsdCapabilities | null) {
    super();
  }

  static async open(options: OpenOptions = {}): Promise<VdsdCompanionTransport> {
    const socketPath = options.socketPath ?? defaultVdsdSocketPath();
    const retryTimeoutMs = Math.max(0, options.retryTimeoutMs ?? 0);
    const retryDelayMs = Math.max(1, options.retryDelayMs ?? DEFAULT_OPEN_RETRY_DELAY_MS);
    const startedAt = Date.now();

    while (true) {
      try {
        const transport = new VdsdCompanionTransport(socketPath, null);
        let capabilities: VdsdCapabilities | null;
        try {
          capabilities = parseCapabilities(await transport.request({ command: 'capabilities' }));
        } catch (error) {
          // Older daemons have no capability command. Keep legacy companion
          // reports usable, but never advertise the new source-aware path.
          const message = error instanceof Error ? error.message : String(error);
          if (!message.includes('unknown command: capabilities')) {
            throw incompatibleDaemonError(message);
          }
          capabilities = null;
        }
        await transport.request({ command: 'companion', op: 'get', report_id: [1] });
        return new VdsdCompanionTransport(socketPath, capabilities);
      } catch (error) {
        const lastError = error instanceof Error ? error : new Error(String(error));
        const elapsedMs = Date.now() - startedAt;
        if (retryTimeoutMs <= 0 || elapsedMs >= retryTimeoutMs) {
          throw lastError;
        }
        await delay(Math.min(retryDelayMs, retryTimeoutMs - elapsedMs));
      }
    }
  }

  supportsFeature(feature: string): boolean {
    return this.capabilities?.features.includes(feature) ?? false;
  }

  async getFeatureReport(reportId: number, _length = REPORT_LENGTH): Promise<number[]> {
    const reply = await this.request({
      command: 'companion',
      op: 'get',
      report_id: [reportId & 0xff]
    });
    if (!Array.isArray(reply.report)) {
      throw new Error('vdsd did not return a companion report.');
    }
    return reply.report;
  }

  async sendFeatureReport(report: ArrayLike<number>): Promise<void> {
    await this.request({ command: 'companion', op: 'set', report: normalizeReport(report) });
  }

  async write(report: ArrayLike<number>): Promise<void> {
    await this.request({ command: 'companion', op: 'write', report: normalizeReport(report) });
  }

  close(): void {
    if (this.closed) {
      return;
    }
    this.closed = true;
    this.emit('close');
  }

  private request(payload: Record<string, unknown>): Promise<VdsdReply> {
    if (this.closed) {
      return Promise.reject(new Error('vdsd companion transport is closed.'));
    }
    return new Promise<VdsdReply>((resolve, reject) => {
      const socket = createConnection(this.path);
      let response = '';
      let settled = false;
      const finish = (error: Error | null, reply?: VdsdReply) => {
        if (settled) {
          return;
        }
        settled = true;
        clearTimeout(timeout);
        socket.destroy();
        if (error) {
          reject(error);
        } else {
          resolve(reply!);
        }
      };
      const timeout = setTimeout(() => {
        finish(new Error('vdsd control request timed out.'));
      }, REQUEST_TIMEOUT_MS);
      socket.on('error', (error) => finish(error));
      socket.on('connect', () => {
        socket.end(`${JSON.stringify(payload)}\n`);
      });
      socket.on('data', (chunk) => {
        response += chunk.toString('utf8');
      });
      socket.on('close', () => {
        if (settled) {
          return;
        }
        const line = response.trim();
        if (!line) {
          finish(new Error('vdsd closed the control connection without a reply.'));
          return;
        }
        try {
          const reply = JSON.parse(line) as VdsdReply;
          if (reply.OK !== true) {
            finish(new Error(reply.error || 'vdsd rejected the companion request.'));
            return;
          }
          finish(null, reply);
        } catch (error) {
          finish(error instanceof Error ? error : new Error(String(error)));
        }
      });
    });
  }
}

function parseCapabilities(reply: VdsdReply): VdsdCapabilities {
  if (reply.OK !== true || reply.controlProtocol !== 4 ||
      reply.hapticsStreamProtocol !== 1 || !Array.isArray(reply.features) ||
      !reply.features.every((feature): feature is string => typeof feature === 'string')) {
    throw new Error('daemon capability response is missing required version or feature fields');
  }
  return {
    controlProtocol: reply.controlProtocol,
    hapticsStreamProtocol: reply.hapticsStreamProtocol,
    features: reply.features
  };
}

function incompatibleDaemonError(detail: string): Error {
  return new Error(
    `Incompatible vdsd daemon: ${detail}. Restart vdsd or install the matching OpenDS5 vds package.`
  );
}

function normalizeReport(report: ArrayLike<number>): number[] {
  if (report.length !== REPORT_LENGTH) {
    throw new Error(`Expected ${REPORT_LENGTH} report bytes, received ${report.length}.`);
  }
  return Array.from({ length: REPORT_LENGTH }, (_, index) => report[index] & 0xff);
}

function delay(milliseconds: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}
