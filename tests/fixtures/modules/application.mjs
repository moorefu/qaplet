import {answer, record} from 'host:application';
import {onEvent} from 'qaplet:events';
import {argv} from 'qaplet:runtime';
import {increment} from './application-dependency.mjs';

let deliver;
const completion = new Promise(resolve => { deliver = resolve; });
onEvent(event => {
  if (event.type !== 7 || event.token !== 18446744073709551615n ||
      event.payload.join(',') !== '0,255,17') throw new Error('event copy mismatch');
  if (record(event.payload) !== 'copied') throw new Error('Host result mismatch');
  deliver();
});
await completion;
await Promise.resolve();
if (argv[0] === 'reject') throw new Error('reproducible application rejection');
globalThis.applicationResult = increment(await import('./application-dependency.mjs').then(m => m.increment(answer)));
