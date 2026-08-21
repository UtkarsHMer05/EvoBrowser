// Phase 2 — safe test sink for side-effecting email execution (Milestone 24).
//
// Automated distributed tests must not send real email. This module provides a
// deterministic in-memory NodeExecutor that stands in for "send-email" via the
// adapter's executorOverrides. It records every call so tests can assert on
// recipients/subject/body, and returns the same output shape the real
// sendEmail executor produces ({ id }) so downstream interpolation behaves
// identically.
//
// Milestone 33 — provider-side idempotency simulation. The real sendEmail
// forwards an `Idempotency-Key` to Resend, which returns the ORIGINAL email
// (no second send) when the same key is retried. This sink mirrors that: when
// an `idempotencyKey` is supplied and was already used, it returns the cached
// result WITHOUT recording a new send. Tests can therefore count ACTUAL side
// effects (`sent.length`) and prove a duplicate delivery / crash-after-result
// redelivery produces exactly one send.
//
// This is a TEST-ONLY sink. It is never registered in the product node
// registry and never used outside tests.

import type { NodeExecutor } from "@/features/workflows/nodes/node-executors";

export interface RecordedEmail {
  to: string;
  subject: string;
  body: string;
  /** The idempotency key this send was recorded under (undefined => none). */
  idempotencyKey?: string;
}

export interface EmailTestSink {
  executor: NodeExecutor;
  sent: RecordedEmail[];
  reset: () => void;
}

export function createEmailTestSink(): EmailTestSink {
  const sent: RecordedEmail[] = [];
  // M33: idempotency key -> cached result (mirrors Resend's server-side cache).
  const idempotentCache = new Map<string, { id: string }>();

  const executor: NodeExecutor = async ({ values, idempotencyKey }) => {
    // Provider-side idempotency: a repeated key returns the original result
    // without a new side effect (exactly what Resend does with the header).
    if (idempotencyKey) {
      const cached = idempotentCache.get(idempotencyKey);
      if (cached) return cached;
    }

    sent.push({
      to: values.to,
      subject: values.subject,
      body: values.body,
      idempotencyKey,
    });
    // Match the real sendEmail output shape so interpolation parity holds.
    const result = { id: `test-email-${sent.length}` };
    if (idempotencyKey) idempotentCache.set(idempotencyKey, result);
    return result;
  };

  return {
    executor,
    sent,
    reset: () => {
      sent.length = 0;
      idempotentCache.clear();
    },
  };
}
