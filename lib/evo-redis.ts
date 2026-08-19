// Phase 2 — server-side Redis client for Evo run events (Milestone 28).
//
// The C++ distributed run loop publishes normalized run events to a Redis
// Stream (`<envPrefix>:events`). This module gives the Next.js server a
// read-only handle to that stream so the authorized SSE route can deliver
// live events to the frontend.
//
// SECURITY: this runs server-side only. The Redis host/port/password come from
// server env vars and are NEVER sent to the browser — the frontend talks to
// the authorized SSE route, not to Redis directly (M28 no-go: do not expose
// Redis credentials to the browser).
//
// The client is lazily created and cached per process. It connects to the
// LOCAL Phase-2 Redis by default (scripts/phase2), matching the worker and the
// C++ scheduler.

import { Redis } from "ioredis";

let cached: Redis | null = null;

export interface EvoRedisConfig {
  host: string;
  port: number;
  password?: string;
}

export function getEvoRedisConfig(): EvoRedisConfig {
  return {
    host: process.env.EVO_PHASE2_REDIS_HOST ?? "127.0.0.1",
    port: Number(process.env.EVO_PHASE2_REDIS_PORT ?? 6390),
    password: process.env.EVO_PHASE2_REDIS_PASSWORD || undefined,
  };
}

/** The env prefix that namespaces the Evo streams (matches the worker). */
export function getEvoEnvPrefix(): string {
  return process.env.EVO_WORKER_ENV_PREFIX ?? "evo:dev";
}

/** Event stream key for a given env prefix (mirrors the C++ helper). */
export function eventStreamKey(envPrefix: string): string {
  return `${envPrefix}:events`;
}

/** Lazily create + cache the server-side Redis client. */
export function getEvoRedis(): Redis {
  if (!cached) {
    const cfg = getEvoRedisConfig();
    cached = new Redis({
      host: cfg.host,
      port: cfg.port,
      password: cfg.password,
      connectTimeout: 2000,
      maxRetriesPerRequest: 1,
      lazyConnect: true,
      enableOfflineQueue: false,
    });
  }
  return cached;
}

/** Close the cached client (tests / graceful shutdown). */
export async function closeEvoRedis(): Promise<void> {
  if (cached) {
    await cached.quit().catch(() => cached?.disconnect());
    cached = null;
  }
}
