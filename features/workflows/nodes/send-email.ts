import { getResendClient } from "@/lib/resend";

export async function sendEmail({
  to,
  subject,
  body,
}: {
  to: string;
  subject: string;
  body: string;
}) {
  const { data, error } = await getResendClient().emails.send({
    from: "onboarding@resend.dev",
    to,
    subject,
    html: body,
  });

  // The Resend SDK returns { data, error } and does not throw on API errors.
  // Throw so the run marks this step failed instead of looking successful.
  if (error || !data) {
    throw new Error(error?.message ?? "Resend returned no email id");
  }

  return { id: data.id };
}
