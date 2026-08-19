// Phase 2 — TypeScript Redis Streams client for the distributed worker
// (Milestone 23). Mirrors the C++ RedisTransport semantics (M21) so workers
// and the scheduler agree on the wire protocol:
//
//   ensureGroup  -> XGROUP CREATE <stream> <group> $ MKSTREAM (idempotent;
//                   BUSYGROUP treated as success)
//   publish      -> XADD <stream> * payload <bytes>
//   readGroup    -> XREADGROUP GROUP <group> <consumer> COUNT 1 BLOCK <ms>
//                   STREAMS <stream> >   (at-least-once; pending until ack)
//   ack          -> XACK <stream> <group> <id>   (workers only; late/dup
//                   ack harmless)
//   pendingCount -> XPENDING summary count
//   streamLength -> XLEN
//
// Keys are namespaced with an explicit env/project prefix (see streamKey
// helpers) so multiple stacks can share one Redis.

import { Redis } from "ioredis";

export interface StreamMessage {
  id: string;
  payload: Buffer;
}

export interface RedisStreamsConfig {
  host: string;
  port: number;
  /** Optional password; never logged. */
  password?: string;
  connectTimeoutMs?: number;
  maxRetriesPerRequest?: number | null;
}

export function taskStreamKey(envPrefix: string): string {
  return `${envPrefix}:tasks`;
}
export function resultStreamKey(envPrefix: string): string {
  return `${envPrefix}:results`;
}
export function controlStreamKey(envPrefix: string): string {
  return `${envPrefix}:control`;
}
export function eventStreamKey(envPrefix: string): string {
  return `${envPrefix}:events`;
}

export class RedisStreamsClient {
  private redis: Redis;

  constructor(config: RedisStreamsConfig) {
    this.redis = new Redis({
      host: config.host,
      port: config.port,
      password: config.password,
      connectTimeout: config.connectTimeoutMs ?? 2000,
      // null => never give up on in-flight commands; the worker applies its
      // own bounded retry/backoff policy above this client.
      maxRetriesPerRequest: config.maxRetriesPerRequest ?? null,
      lazyConnect: true,
      enableOfflineQueue: false,
    });
  }

  async connect(): Promise<void> {
    await this.redis.connect();
  }

  async ping(): Promise<boolean> {
    try {
      return (await this.redis.ping()) === "PONG";
    } catch {
      return false;
    }
  }

  async disconnect(): Promise<void> {
    await this.redis.quit().catch(() => this.redis.disconnect());
  }

  /**
   * Idempotently create a consumer group. BUSYGROUP => already exists.
   * startId "$" delivers only new messages; "0" replays from the beginning.
   */
  async ensureGroup(
    stream: string,
    group: string,
    startId: "$" | "0" = "$",
  ): Promise<boolean> {
    try {
      await this.redis.xgroup("CREATE", stream, group, startId, "MKSTREAM");
      return true;
    } catch (err) {
      if (String(err).includes("BUSYGROUP")) return true;
      throw err;
    }
  }

  /** Append a payload. Returns the assigned stream message id. */
  async publish(stream: string, payload: Buffer | string): Promise<string> {
    const id = await this.redis.xadd(stream, "*", "payload", payload);
    if (!id) throw new Error(`XADD to ${stream} returned no id`);
    return id;
  }

  /**
   * Blocking read of the next undelivered message for (group, consumer).
   * Returns null on timeout (no message) — the caller loops. At-least-once:
   * the message is pending until ack().
   *
   * Uses the binary-safe `xreadgroupBuffer` variant: envelope payloads are
   * serialized protobuf, and the default string reply path would UTF-8
   * transcode bytes >= 0x80 (e.g. multi-byte varints in wall-clock
   * Timestamps), corrupting the wire format.
   */
  async readGroup(
    stream: string,
    group: string,
    consumer: string,
    blockMs: number,
  ): Promise<StreamMessage | null> {
    const reply = await this.redis.xreadgroupBuffer(
      "GROUP",
      group,
      consumer,
      "COUNT",
      "1",
      "BLOCK",
      blockMs,
      "STREAMS",
      stream,
      ">",
    );
    if (!reply) return null;
    // reply: [ [stream, [ [id, [field, value, ...]] ] ] ] (all Buffers)
    const streams = reply as Array<[Buffer, Array<[Buffer, Buffer[]]>]>;
    const [, messages] = streams[0] ?? [];
    const first = messages?.[0];
    if (!first) return null;
    const [idBuf, fields] = first;
    // fields: [Buffer("payload"), <bytes>]
    const valueIdx = fields.findIndex((f) => f.toString() === "payload");
    const raw = valueIdx >= 0 ? fields[valueIdx + 1] : undefined;
    if (raw === undefined) return null;
    return { id: idBuf.toString(), payload: raw };
  }

  /** Acknowledge a delivered message. Late/duplicate ack is harmless. */
  async ack(stream: string, group: string, messageId: string): Promise<boolean> {
    const n = await this.redis.xack(stream, group, messageId);
    return n >= 0; // 0 (already acked/unknown) or 1 both count as success
  }

  /** Number of pending (delivered, unacked) entries for a group. */
  async pendingCount(stream: string, group: string): Promise<number> {
    const summary = await this.redis.xpending(stream, group);
    if (!summary) return 0;
    return Number(summary[0] ?? 0);
  }

  /** Total messages appended to the stream. */
  async streamLength(stream: string): Promise<number> {
    return Number(await this.redis.xlen(stream));
  }
}
