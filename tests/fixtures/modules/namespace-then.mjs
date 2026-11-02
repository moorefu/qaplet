globalThis.namespaceRuns = (globalThis.namespaceRuns || 0) + 1;
export function then() { globalThis.namespaceThenCalled = true; throw new Error('namespace assimilation'); }
