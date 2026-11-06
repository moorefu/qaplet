"""Actual Ctrl+C in a hidden, privately owned native Console."""
import argparse,json,os,subprocess,time
from pathlib import Path
from m2_cli_signal import capture_inputs, identity

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cli',type=Path,required=True);p.add_argument('--host',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
    p.add_argument('--mode',choices=('all','wait','execute','async-output','sync-output','queue','mixed'),default='all')
    p.add_argument('--thread-fault',action='store_true')
    p.add_argument('--late',action='store_true')
    args=p.parse_args();args.report=args.report.resolve();cases=[];args.report.parent.mkdir(parents=True,exist_ok=True)
    consumer=capture_inputs(args.report.parent/(args.report.stem+'-raw'))
    for mode in (('wait','execute','async-output','sync-output') if args.mode=='all' else (args.mode,)):
        directory=args.report.parent/(args.report.stem+'-raw')/mode;directory.mkdir(parents=True)
        source="Promise.all([import('qaplet:stdio'),import('qaplet:timers')]).then(async ([s,t])=>{await s.stdout.write(new Uint8Array([82,69,65,68,89,10])).promise;"
        source+='for(;;){}' if mode=='execute' else 'await new Promise(r=>t.setTimeout(r,600000));await s.stdout.write(new Uint8Array([65,70,84,69,82])).promise;'
        source+='})'
        if mode.endswith('output'):source="'X'.repeat(200000)"
        if mode=='mixed':
            from m2_cli_signal_mixed import source as mixed_source
            source=mixed_source([str(args.host.resolve()),'--held-child'])
        path=directory/'source.js';path.write_text(source)
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
        argv=[str(args.host.resolve()),'--console',str(args.cli.resolve()),str(path.resolve()),*([mode] if mode.endswith('output') or mode in ('queue','mixed') else [])];before=time.monotonic();expired=False
        with (directory/'host.stdout').open('wb') as fo,(directory/'host.stderr').open('wb') as fe:
            env=dict(os.environ)
            if args.thread_fault:env.update(QAPLET_SIGNAL_THREAD_FAULT='1',QAPLET_SIGNAL_SPLIT_WAIT='1')
            if args.late:env['QAPLET_SIGNAL_LATE']='1'
            child=subprocess.Popen(argv,cwd=directory,stdout=fo,stderr=fe,stdin=subprocess.DEVNULL,creationflags=subprocess.CREATE_NEW_CONSOLE,startupinfo=startup,env=env)
            try:child.wait(timeout=40)
            except subprocess.TimeoutExpired:expired=True;child.kill();child.wait()
        result=directory/'ctrl-result.json';native=json.loads(result.read_text()) if result.is_file() else None
        errors=(directory/'cli.stderr').read_bytes() if (directory/'cli.stderr').is_file() else b''
        passed=not expired and child.returncode==0 and native and native['sender_received'] and native['exit_code']==1 and not native['supervisor_timeout'] and b'EXECUTION_STOPPED' in errors and b'cleanup incomplete' not in errors
        queue=None;modes=None;direct=None
        fault_path=directory/'signal-fault.ndjson';rows=[json.loads(line) for line in fault_path.read_text().splitlines()] if fault_path.is_file() else []
        waits=[row['duration_ns']/1e9 for row in rows if row['operation']=='active-cond-wait']
        if args.thread_fault:passed=not expired and child.returncode==0 and native and native['sender_received']>=6 and native['repeated_after_held']==5 and native['native_thread_exit_held'] and native['exit_code']==1 and not native['supervisor_timeout'] and b'native cleanup incomplete' in errors and 4.5<=sum(waits)<5.5 and len([n for n in waits if n>.5])>=2 and 4.5<=native['elapsed_seconds']<6.5
        if args.late:
            late_path=directory/'signal-late.ndjson';rows=[json.loads(line) for line in late_path.read_text().splitlines()] if late_path.is_file() else []
            late=next((row for row in rows if row['operation']=='post-destroy-late-entry'),{})
            passed=passed and native['sender_received']>=3 and late.get('destroy_code')==0 and late.get('original_target_identity_preserved') and not late.get('replacement_identity_reused',True) and late.get('late_entry_completed') and late.get('replacement_objects_untouched') and late.get('future_entry_delivered')
        if mode=='queue':
            queue_path=directory/'queue-result.json';queue=json.loads(queue_path.read_text()) if queue_path.is_file() else None
            passed=passed and queue and queue['queue_full'] and queue['owner_stopped'] and queue['user_marks']==0 and queue['events_dropped']==2 and queue['sdk_destroyed']
        if mode=='mixed':
            from m2_cli_signal_mixed import facts
            proof,rows,direct=facts(directory);modes_path=directory/'mixed-modes.json';modes=json.loads(modes_path.read_text()) if modes_path.is_file() else None
            passed=not expired and child.returncode==0 and native and native['sender_received'] and native['exit_code']==1 and not native['supervisor_timeout'] and proof and modes and modes['full_modes_restored'] and modes['actual_entry_baseline']==modes['restored'] and modes['modified']!=modes['restored']
        scope='Actual supplied CLI, private native Console, real CTRL_C_EVENT group0; no raw 0x03/Break substitution'
        if mode=='queue':scope='Separate public C Host OS-relay/SDK queue-saturation boundary; private Console real CTRL_C_EVENT group0; not CLI end-to-end'
        if mode=='mixed':scope='Actual CLI source with no-fault public SDK/native-reap observer; private Console actual entry/raw/restored modes and direct child; pre-CRT restoration not claimed'
        if args.late:scope='Controlled CLI native hint delay after callback captures HANDLE; actual Runtime destroy, replacement events, delayed target access and post-destroy Ctrl+C; asynchronous targets remain process lifetime, OS callback-thread join not asserted'
        if args.thread_fault:scope='Controlled CLI OS-pthread retirement failure; actual repeated Ctrl+C during native exit hold; cumulative SDK+relay active wait, incomplete and exit1; no complete native cleanup claim'
        cases.append({'case':'windows-Ctrl+C-'+mode,'status':'pass' if passed else 'fail','argv':argv,'launcher_exit':child.returncode,'native':native,'queue_result':queue,'terminal_modes':modes,'direct_child':direct,'supervisor_timeout':expired,'elapsed_seconds':time.monotonic()-before,'controlled_system_fixture':args.thread_fault or args.late,'controlled_native_observer':mode=='mixed','active_condition_waits_seconds':waits,'cumulative_observed_active_wait_seconds':sum(waits),'raw_records':rows,'native_cleanup_complete':bool(passed) and not args.thread_fault,'scope':scope})
    report={'status':'pass' if all(c['status']=='pass' for c in cases) else 'fail','cli':identity(args.cli),'host_artifact':identity(args.host),'script':identity(Path(__file__)),'cases':cases,'host':{'platform':__import__('platform').platform(),'architecture':__import__('platform').machine(),'page_size':__import__('mmap').PAGESIZE},'ci':None}
    report.update(consumer_identity=consumer,execution_gate='execute awaits stdout READY; continuation may not have entered for(;;); use m2_cli_signal_strong.py for the unsuspended gate')
    args.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'status':report['status'],'report':str(args.report)}));return report['status']!='pass'
if __name__=='__main__':raise SystemExit(main())
