export const answer = 42;
globalThis.dependencyRuns = (globalThis.dependencyRuns || 0) + 1;
globalThis.dependencyUrl = import.meta.url;
globalThis.dependencyMain = import.meta.main;
