import { useSyncExternalStore } from "react";

const MOBILE_BREAKPOINT = 768;

// Subscribes to the viewport-width media query without an effect: the store
// snapshot is read synchronously, so there's no setState-in-effect cascade.
function subscribeToMobileQuery(callback: () => void) {
  const mql = window.matchMedia(`(max-width: ${MOBILE_BREAKPOINT - 1}px)`);
  mql.addEventListener("change", callback);
  return () => mql.removeEventListener("change", callback);
}

export function useIsMobile() {
  return useSyncExternalStore(
    subscribeToMobileQuery,
    () => window.innerWidth < MOBILE_BREAKPOINT,
    () => false,
  );
}
