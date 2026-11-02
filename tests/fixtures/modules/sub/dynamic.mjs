await Promise.resolve();
globalThis.dynamicRuns = (globalThis.dynamicRuns || 0) + 1;
globalThis.dynamicMain = import.meta.main;
export const increment = 2;
