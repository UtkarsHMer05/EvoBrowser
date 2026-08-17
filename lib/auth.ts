import * as Sentry from "@sentry/nextjs";
import { auth, clerkClient } from "@clerk/nextjs/server";

// Resolves the organization the current request should operate on.
//
// `auth().orgId` reflects the *active organization* claim carried by the
// session token. That claim is set client-side when the user selects an org,
// and it can be intermittently absent from a token snapshot (token rotation /
// session refresh) even though the user is signed in and has an active
// organization — which made org-scoped server actions fail with "No active
// organization" while other requests moments earlier succeeded.
//
// When the claim is missing, fall back to the user's verified membership list
// from Clerk's Backend API. That source is authoritative and does not depend
// on the session token claim. Throws when the user is not signed in or has no
// organization membership at all.
export async function resolveActiveOrgId(): Promise<string> {
  const { userId, orgId } = await auth();

  if (!userId) {
    throw new Error("Not authenticated");
  }

  if (orgId) {
    return orgId;
  }

  const client = await clerkClient();
  const { data: memberships } =
    await client.users.getOrganizationMembershipList({
      userId,
      limit: 10,
    });

  const fallbackOrgId = memberships[0]?.organization?.id;

  if (!fallbackOrgId) {
    throw new Error("No active organization");
  }

  Sentry.logger.warn(
    "Active org claim missing from session token — resolved org from membership",
    { userId, orgId: fallbackOrgId },
  );

  return fallbackOrgId;
}
