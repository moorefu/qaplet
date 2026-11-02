import { argv, cwd } from 'qaplet:runtime';
if (argv.length !== 3 || argv[0] !== 'first' || argv[1] !== '参数 空格' || argv[2] !== '--literal')
  throw new Error('application argv differs');
if (!Object.isFrozen(argv) || typeof cwd !== 'string' || cwd[0] !== '/') throw new Error('snapshot differs');
try { argv[0] = 'changed'; } catch (_) {}
try { argv.push('changed'); } catch (_) {}
if (argv[0] !== 'first' || argv.length !== 3) throw new Error('argv changed');
if (!import.meta.main || !import.meta.url.endsWith('/cli-runtime.mjs')) throw new Error('entry identity differs');
