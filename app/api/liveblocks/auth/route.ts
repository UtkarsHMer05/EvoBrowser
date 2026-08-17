import * as Sentry from "@sentry/nextjs";
import { auth, currentUser } from "@clerk/nextjs/server";

import { getLiveblocksClient } from "@/lib/liveblocks";
import { resolveActiveOrgId } from "@/lib/auth";

export async function POST() {
  const { userId } = await auth();

  if (!userId) {
    return new Response("Unauthorized", { status: 401 });
  }

  let orgId: string;
  try {
    orgId = await resolveActiveOrgId();
  } catch {
    return new Response("Unauthorized", { status: 401 });
  }

  const user = await currentUser();

  if (!user) {
    return new Response("Unauthorized", { status: 401 });
  }

  Sentry.getIsolationScope().setAttributes({
    route: "POST /api/liveblocks/auth",
    userId,
    orgId,
  });

  // Identify the user with an ID token. Permissions are resolved per-room
  // from the user's groups — scope access to their Clerk organization.
  const { status, body } = await getLiveblocksClient().identifyUser(
    {
      userId,
      groupIds: [orgId],
      organizationId: orgId,
    },
    {
      userInfo: {
        name:
          user.fullName ??
          user.username ??
          user.primaryEmailAddress?.emailAddress ??
          "Anonymous",
        avatar: user.imageUrl,
      },
    },
  );

  if (status >= 400) {
    Sentry.logger.error("Liveblocks user identification failed", {
      userId,
      orgId,
      status,
    });
  } else {
    Sentry.logger.info("Liveblocks user identified", { userId, orgId, status });
  }

  return new Response(body, { status });
}
