import { answer } from './dependency.js';
globalThis.moduleAnswer = answer;
globalThis.entryRuns = (globalThis.entryRuns || 0) + 1;
globalThis.entryUrl = import.meta.url;
globalThis.entryMain = import.meta.main;
