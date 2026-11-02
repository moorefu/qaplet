import { readA } from './cycle-a.mjs';
export function readB() { return readA(); }
