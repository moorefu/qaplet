"""Real CLI mixed TTY/stdio/timer/direct-child interrupt and native reap facts."""
import argparse,json,os,signal,subprocess,threading,time
from pathlib import Path
from m2_cli_signal import identity

def source(childargs):
    return "Promise.all([import('qaplet:process'),import('qaplet:stdio'),import('qaplet:timers')]).then(async([p,s,t])=>{const a="+json.dumps(childargs)+";const child=p.spawn(a[0],{argv:a});const first=await child.stdout.read(1).promise;if(first.bytes[0]!==82)throw Error('child gate');t.setInterval(()=>{},600000);await s.stdin.releaseTerminal();await Promise.all([s.stdin.closed,s.stdout.closed,s.stderr.closed]);const input=s.acquireTerminal(0,{raw:true});input.joinTerminal(1,{vt:true});input.joinTerminal(2);try{await input.read(1).promise}finally{const b=[...a];b[b.length-1]='--forbidden';p.spawn(b[0],{argv:b})}})"

def facts(directory):
    path=directory/'signal-native.ndjson';rows=[json.loads(line) for line in path.read_text().splitlines()] if path.is_file() else []
    child_path=directory/'direct-child.json';child=json.loads(child_path.read_text()) if child_path.is_file() else None
    reads=[r for r in rows if r['operation']=='mixed-tty-read-accepted'];reaps=[r for r in rows if r['operation']=='native-child-reaped']
    closes=[r for r in rows if r['operation']=='owner-close'];quiet=[r for r in rows if r['operation']=='native-quiesce'];destroy=[r for r in rows if r['operation']=='runtime-destroy']
    passed=bool(child and reads and reads[-1]['processes']==1 and reads[-1]['operations']>=1 and reads[-1]['io_endpoints']>=4 and len(reaps)==1 and reaps[0]['pid']==child['pid'] and closes and closes[-1]['termination_reason']==23 and closes[-1]['code']==0 and quiet and quiet[-1]['code']==0 and quiet[-1]['reason']==6 and destroy and destroy[-1]['code']==0 and not (directory/'forbidden-js').exists())
    if os.name!='nt':passed=passed and reaps[0].get('signal')==9 if reaps else False
    else:passed=passed and any(r['operation']=='native-force-accepted' for r in rows)
    return passed,rows,child

def linux(args,directory,signum):
    import fcntl,termios
    master,slave=os.openpty();before=termios.tcgetattr(slave);flags=fcntl.fcntl(slave,fcntl.F_GETFL)
    text=source([str(args.loader.resolve()),str(args.host.resolve()),'--held-child']);(directory/'source.js').write_text(text)
    argv=[str(args.loader.resolve()),str(args.cli.resolve()),'--timeout-ms','600000','eval','--source',text]
    raw=bytearray()
    def reader():
        try:
            while True:
                chunk=os.read(master,65536)
                if not chunk:break
                raw.extend(chunk)
        except OSError:pass
    child=subprocess.Popen(argv,cwd=directory,stdin=slave,stdout=slave,stderr=slave,start_new_session=True);thread=threading.Thread(target=reader);thread.start()
    ready=False;expired=False;deadline=time.monotonic()+20
    while time.monotonic()<deadline and child.poll() is None:
        if (directory/'mixed-ready').is_file():ready=True;break
        time.sleep(.001)
    modified=termios.tcgetattr(slave);modified_flags=fcntl.fcntl(slave,fcntl.F_GETFL);sent=time.monotonic()
    if ready:os.kill(child.pid,signum)
    try:child.wait(timeout=12)
    except subprocess.TimeoutExpired:expired=True;child.kill();child.wait()
    restored=termios.tcgetattr(slave);restored_flags=fcntl.fcntl(slave,fcntl.F_GETFL);os.close(slave);thread.join(3);os.close(master)
    (directory/'terminal.bin').write_bytes(raw)
    proof,rows,direct=facts(directory);modes_restored=before==restored and flags==restored_flags
    passed=ready and not expired and child.returncode==1 and modes_restored and before!=modified and not thread.is_alive() and proof
    def modes(a):return [*a[:6],[c if isinstance(c,int) else c[0] for c in a[6]]]
    return {'status':'pass' if passed else 'fail','case':signal.Signals(signum).name+'-mixed','argv':argv,'pid':child.pid,'real_os_signal':int(signum) if ready else None,'ready_accepted':ready,'exit_code':child.returncode,'supervisor_timeout':expired,'elapsed_seconds':time.monotonic()-sent,'before':modes(before),'modified':modes(modified),'restored':modes(restored),'flags':[flags,modified_flags,restored_flags],'full_modes_and_flags_restored':modes_restored,'raw_records':rows,'direct_child':direct,'native_cleanup_complete':passed,'controlled_native_observer':True,'scope':'Actual CLI source; public SDK state and true native reap observation, real private PTY, direct PID SIGINT/SIGTERM; no native fault'}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cli',type=Path,required=True);p.add_argument('--host',type=Path,required=True);p.add_argument('--loader',type=Path,required=True);p.add_argument('--report',type=Path,required=True);args=p.parse_args()
    rows=[]
    for signum in (signal.SIGINT,signal.SIGTERM):
        directory=args.report.parent/(args.report.stem+'-raw')/str(signum);directory.mkdir(parents=True);rows.append(linux(args,directory,signum))
    report={'status':'pass' if all(r['status']=='pass' for r in rows) else 'fail','cli':identity(args.cli),'host_artifact':identity(args.host),'script':identity(Path(__file__)),'host':{'uname':list(os.uname()),'page_size':os.sysconf('SC_PAGESIZE')},'cases':rows,'ci':None};args.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'status':report['status'],'report':str(args.report)}));return report['status']!='pass'
if __name__=='__main__':raise SystemExit(main())
