import { readB } from './cycle-b.mjs';
export function readA() { return 20; }
globalThis.cycleAnswer = readB() + 22;
