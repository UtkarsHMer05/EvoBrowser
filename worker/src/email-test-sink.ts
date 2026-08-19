// Phase 2 — safe test sink for side-effecting email execution (Milestone 24).
//
// Automated distributed tests must not send real email. This module provides a
// deterministic in-memory NodeExecutor that stands in for "send-email" via the
// adapter's executorOverrides. It records every call so tests can assert on
// recipients/subject/body, and returns the same output shape the real
// sendEmail executor produces ({ id }) so downstream interpolation behaves
// identically.
//
// This is a TEST-ONLY sink. It is never registered in the product node
// registry and never used outside tests.

import type { NodeExecutor } from "@/features/workflows/nodes/node-executors";

export interface RecordedEmail {
  to: string;
  subject: string;
  body: string;
}

export interface EmailTestSink {
  executor: NodeExecutor;
  sent: RecordedEmail[];
  reset: () => void;
}

export function createEmailTestSink(): EmailTestSink {
  const sent: RecordedEmail[] = [];

  const executor: NodeExecutor = async ({ values }) => {
    sent.push({
      to: values.to,
      subject: values.subject,
      body: values.body,
    });
    // Match the real sendEmail output shape so interpolation parity holds.
    return { id: `test-email-${sent.length}` };
  };

  return {
    executor,
    sent,
    reset: () => {
      sent.length = 0;
    },
  };
}
