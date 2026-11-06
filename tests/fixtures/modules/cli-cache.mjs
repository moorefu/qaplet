import { answer } from './cli-cache-dependency.mjs';
const repeated = await import('./cli-cache-dependency.mjs');
if (answer !== 42 || repeated.answer !== 42 || globalThis.cliDependencyRuns !== 1)
  throw new Error('module cache differs');
export function then() { throw new Error('namespace conversion ran'); }
