import { getResendClient } from "@/lib/resend";

export async function sendEmail({
  to,
  subject,
  body,
  idempotencyKey,
}: {
  to: string;
  subject: string;
  body: string;
  /**
   * Milestone 33: provider-side idempotency. Resend's send-email endpoint
   * accepts an `Idempotency-Key` header (SDK 6.x `idempotencyKey` option); a
   * retry/duplicate delivery with the same key returns the original email
   * instead of sending a second one. The distributed worker derives a
   * deterministic key from the attempt identity. Undefined on the legacy path
   * (no key => Resend's default non-idempotent behavior, unchanged).
   */
  idempotencyKey?: string;
}) {
  const { data, error } = await getResendClient().emails.send(
    {
      from: "onboarding@resend.dev",
      to,
      subject,
      html: body,
    },
    idempotencyKey ? { idempotencyKey } : undefined,
  );

  // The Resend SDK returns { data, error } and does not throw on API errors.
  // Throw so the run marks this step failed instead of looking successful.
  if (error || !data) {
    throw new Error(error?.message ?? "Resend returned no email id");
  }

  return { id: data.id };
}
