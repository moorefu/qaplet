import { onEvent } from 'qaplet:events';
globalThis.entryEvents = [];
globalThis.entryEventResult = await new Promise(resolve => {
  onEvent(event => {
    entryEvents.push(event);
    if (event.type === 42) resolve(42);
    return new Promise(() => {});
  });
});
