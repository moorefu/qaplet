import * as stdio from 'qaplet:stdio';
import {spawn} from 'qaplet:process';
import {argv} from 'qaplet:runtime';
if (!stdio.stdin || !stdio.stdout || !stdio.stderr) throw Error('missing initial terminal routes');
const initialInput=stdio.stdin;
try { stdio.acquireTerminal(0,{raw:true}); throw Error('missing lease conflict'); }
catch(e) { if(e.code!==4) throw e; }
await stdio.stdin.releaseTerminal();
await Promise.all([stdio.stdin.closed,stdio.stdout.closed,stdio.stderr.closed]);
let input;
try { input=stdio.acquireTerminal(0,{raw:true}); }
catch(e) { throw Error('restored route reacquire code='+e.code); }
const output=input.joinTerminal(1,{vt:true});
const errors=input.joinTerminal(2);
if(input===stdio.stdin || output===stdio.stdout || (await import('qaplet:stdio')).stdin!==initialInput) throw Error('facade snapshot');
await initialInput.closed;
try {initialInput.read(1);throw Error('old facade reopened');}
catch(e) {if(e.code!==5) throw e;}
const args=argv[1]==='-' ? [argv[0],'--m2-cli-mark'] : [argv[1],argv[0],'--m2-cli-mark'];
const child=spawn(args[0],{argv:args,stdio:['inherit','inherit','inherit']});
if((await child.exited).exitCode!==0) throw Error('native raw/VT readback');
let bytes=[];
while(bytes.length<5) bytes.push(...(await input.read(5-bytes.length).promise).bytes);
if(bytes.join(',')!=='65,228,184,173,90') throw Error('terminal input');
await output.write(new Uint8Array([27,91,51,49,109,65,228,184,173,90,27,91,48,109])).promise;
if(argv[2]==='error') throw Error('background terminal failure');
if(argv[2]==='budget') { const {setImmediate}=await import('qaplet:timers');setImmediate(()=>{function again(){Promise.resolve().then(again)}again()}); }
// Runtime close owns the remaining live terminal group; user finally is not cleanup.
