"""Controlled native-thread retirement failure with real repeated OS interrupts."""
import argparse,json,os,signal,subprocess,time
from pathlib import Path
from m2_cli_signal import capture_inputs, identity, send_signal

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cli',type=Path,required=True);p.add_argument('--loader',type=Path);p.add_argument('--report',type=Path,required=True)
    p.add_argument('--late',action='store_true')
    args=p.parse_args();args.report=args.report.resolve();directory=args.report.parent/(args.report.stem+'-raw');directory.mkdir(parents=True)
    consumer=capture_inputs(directory)
    source="Promise.all([import('qaplet:stdio'),import('qaplet:timers')]).then(async([s,t])=>{await s.stdout.write(new Uint8Array([82,69,65,68,89,10])).promise;await new Promise(r=>t.setTimeout(r,600000));await s.stdout.write(new Uint8Array([65,70,84,69,82])).promise;})"
    (directory/'source.js').write_text(source)
    argv=[*([str(args.loader.resolve())] if args.loader else []),str(args.cli.resolve()),'--timeout-ms','600000','eval','--source',source]
    env=dict(os.environ,**({'QAPLET_SIGNAL_LATE':'1'} if args.late else {'QAPLET_SIGNAL_THREAD_FAULT':'1','QAPLET_SIGNAL_SPLIT_WAIT':'1'}));expired=False;sent=[];ready=False;held=False
    with (directory/'stdout.log').open('wb') as fo,(directory/'stderr.log').open('wb') as fe:
        child=subprocess.Popen(argv,cwd=directory,stdout=fo,stderr=fe,stdin=subprocess.DEVNULL,env=env,start_new_session=True)
        deadline=time.monotonic()+20
        while time.monotonic()<deadline and child.poll() is None:
            if (directory/'stdout.log').read_bytes().startswith(b'READY\n'):ready=True;break
            time.sleep(.001)
        before=time.monotonic()
        if ready:
            if args.late:(directory/'late-arm').write_text('after completed READY write')
            else:sent.append({**send_signal(child.pid,signal.SIGINT),'at_seconds':time.monotonic()-before})
        deadline=time.monotonic()+2
        path=directory/('signal-late.ndjson' if args.late else 'signal-fault.ndjson')
        while time.monotonic()<deadline and child.poll() is None:
            if path.is_file() and ('late-hint-held' if args.late else 'thread-exit-held') in path.read_text():held=True;break
            time.sleep(.001)
        if held:
            for i in range(1 if args.late else 5):
                if child.poll() is not None:break
                signum=signal.SIGTERM if args.late or i%2 else signal.SIGINT
                sent.append({**send_signal(child.pid,signum),'at_seconds':time.monotonic()-before});time.sleep(.1)
        try:child.wait(timeout=10)
        except subprocess.TimeoutExpired:expired=True;child.kill();child.wait()
    elapsed=time.monotonic()-before;errors=(directory/'stderr.log').read_bytes()
    rows=[json.loads(line) for line in path.read_text().splitlines()] if path.is_file() else []
    waits=[row['duration_ns'] for row in rows if row['operation']=='active-cond-wait'];total=sum(waits)/1e9
    passed=ready and held and len(sent)==6 and not expired and child.returncode==1 and b'native cleanup incomplete' in errors and b'AFTER' not in (directory/'stdout.log').read_bytes() and 4.5<=elapsed<6.5 and 4.5<=total<5.5 and sum(n>500000000 for n in waits)>=2
    if args.late:
        late=next((row for row in rows if row['operation']=='post-destroy-late-entry'),{})
        passed=ready and held and len(sent)==1 and not expired and child.returncode==1 and b'EXECUTION_STOPPED' in errors and b'cleanup incomplete' not in errors and late.get('destroy_code')==0 and late.get('original_target_identity_preserved') and not late.get('replacement_identity_reused',True) and late.get('late_entry_completed') and late.get('replacement_objects_untouched') and late.get('future_entry_delivered')
    scope='Controlled product-source CLI/OS pthread retirement fault; true SIGINT then repeats while native exit held; not ordinary product completion'
    if args.late:scope='Controlled product-source CLI/native hint delay after target fd capture; true thread-directed SIGINT, process SIGTERM owner stop, real Runtime destroy, replacement objects and post-destroy actual SIGINT; no pthread-retirement fault'
    passed=passed and all(row['errno']==0 for row in sent)
    report={'status':'pass' if passed else 'fail','scope':scope,'pid':child.pid,'consumer_identity':consumer,
        'cli':identity(args.cli),'script':identity(Path(__file__)),'argv':argv,'ready_accepted':ready,'native_thread_exit_held':held,'signals':sent,'exit_code':child.returncode,'supervisor_timeout':expired,
        'elapsed_seconds':elapsed,'active_condition_waits_seconds':[n/1e9 for n in waits],'cumulative_observed_active_wait_seconds':total,'raw_records':rows,'native_cleanup_complete':bool(passed) if args.late else False,'late_entry_fixture':args.late,
        'stdout':identity(directory/'stdout.log'),'stderr':identity(directory/'stderr.log'),'host':{'uname':list(os.uname()),'page_size':os.sysconf('SC_PAGESIZE')},'ci':None}
    args.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'status':report['status'],'report':str(args.report)}));return report['status']!='pass'
if __name__=='__main__':raise SystemExit(main())
