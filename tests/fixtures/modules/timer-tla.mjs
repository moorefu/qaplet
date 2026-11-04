import {setTimeout} from 'qaplet:timers';
await new Promise(resolve => setTimeout(() => { globalThis.timerTla = 42; resolve(); }, 5));
