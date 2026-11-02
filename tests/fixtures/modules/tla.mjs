import { answer } from './dependency.js';
const dynamic = await import('./sub/dynamic.mjs');
await Promise.resolve();
globalThis.tlaAnswer = answer + dynamic.increment;
