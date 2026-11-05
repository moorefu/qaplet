#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Public SDK terminal consumers; Ubuntu packages, other hosts replay bytes."""
import argparse
import json
import os
from pathlib import Path
import sys
import uuid

import ape_helpers
from build_ape import BuildError,audit,identity,link_portable,publish_ape,run
from foundation import host_facts,now,sha256
from input_lock import consume_identity,consumption_purpose
from startup import measured_host,select_loader
from workflow_support import guard_evidence_outputs,write_evidence_report

ROOT=Path(__file__).resolve().parents[1]
NORMAL=(
 ("--m2-tty-join-c","M2 TTY joined C: explicit same-terminal group; cross Runtime/wrong terminal rejected; second independent lease conflicts; closing one direction retains shared state and closed barrier; group release restores once in reverse order: pass\n"),
 ("--m2-tty-join-js","M2 TTY joined JS: one exclusive lease owns explicit input/output; input size uses joined output; real resize latest, BMP input and supplementary output; group release joins all IO then restores full shared modes/flags: pass\n"),
 ("--m2-tty-identities","M2 TTY identity/limits: finite handle/registration and retained operation/data refusal; no mode side effect on rejection; cross Runtime token rejection; actual native number reuse plus new generation rejects stale notification; full cleanup: pass\n"),
 ("--m2-tty-validation","M2 TTY validation: strict version/size/unknown flags/reserved/tail; wrong owner and invalid identity; no accepted resource; old factory remains unsupported: pass\n"),
 ("--m2-tty-lease","M2 TTY lease: explicit terminal acquisition; native alias conflict; full mode and shared flag readback; idempotent release; Host originals survive and native resources retire: pass\n"),
 ("--m2-tty-stop-close","M2 TTY stop/close: pending native input; stop publication leaves modes for owner; owner confirms cancellation; repeated close and native quiesce join before full restoration; Host originals survive and native resources zero: pass\n"),
)
POSIX=(
 ("--m2-tty-bytes","M2 TTY POSIX bytes: exact all256/NUL/invalid UTF8 raw input and output; three-byte native budget; retained results occupy shared count/data; rejection atomic; full mode/flags restored: pass\n"),
 ("--m2-tty-resize-signal","M2 TTY SIGWINCH: actual foreground PTY resize reaches Host signal self-pipe; native producer publishes across threads; owner queries final100x33 while child lives; SDK signal disposition unchanged; joined/reaped/closed: pass\n"),
 ("--m2-tty-resize","M2 TTY resize: real kernel sizes; 10000 Host notifications coalesce to latest query; bounded storage and single revision; unwatch rejects late producer; cleanup: pass\n"),
 ("--m2-tty-js","M2 TTY JS: explicit stdio terminal lease; size query; unref is not cancel; latest resize via explicit pump; callback and closed Promise jobs; full mode restoration: pass\n"),
)
WINDOWS=(
 ("--m2-tty-helper-limit","M2 TTY helper limit: Console join control consumes one existing helper and requested stack; physical IO refuses atomically at quota1; conflicting Runtime starts no control helper; full native cleanup: pass\n"),
 ("--m2-tty-resize","M2 TTY Console resize: real final window event precedes owner size query; coalesced latest100x33 and single revision; full mode restoration and cleanup: pass\n"),
 ("--m2-tty-js","M2 TTY Console JS resize: explicit lease; real final window event precedes owner size query; latest callback and closed Promise jobs; unref explicit pump; full mode restoration and cleanup: pass\n"),
 ("--m2-tty-output","M2 TTY Console output: strict UTF8 rejects overlong/surrogate/range/truncated/continuation before acceptance; one-byte native chunks preserve BMP and supplementary output; SGR31/reset rendered; exact written count and full mode cleanup: pass\n"),
 ("--m2-tty-input","M2 TTY Console input: bytewise ConPTY UTF8 transport to BMP key events to exact one-byte SDK reads; cancellation wake is not EOF; owner restores modes after helper join; native resources zero: pass\n"),
)
FAULT=(
 ("--restore","controlled TTY restore: syscall failure is IO_ERROR; lease and duplicate remain charged and exclude aliases across CLOSED; quiesce pending and destroy BUSY; actual retry restores all state before retirement; original error retained: pass\n"),
)
POSIX_FAULT=(("--io-error","controlled TTY POSIX IO error: real EBADF preserves errno; group cancels unissued output without bytes; full mode/flags restore while Runtime OPEN; retained results released and native cleanup complete: pass\n"),)
WINDOWS_FAULT=(
 ("--control-start","controlled TTY control startup: pthread EAGAIN refuses lease before mode mutation; original errno and OPEN retained; no accepted endpoint/helper; retry succeeds and all native resources retire: pass\n"),
 ("--control-exit","controlled TTY control exit: real control-thread TLS destructor retains one helper/handle/requested stack after JS close; zero quiesce pending and destroy BUSY; actual exit/join retires control resource: pass\n"),
 ("--exit-wake","controlled TTY actual exit wake: real Console worker TLS destructor holds physical join after IO completion; closed keeps ref; actual OS wait then native exit/join reliably wakes owner; no Host wake/polling; restored and cleaned: pass\n"),
 ("--partial","controlled TTY UTF16 partial: real high surrogate acceptance then successful remaining unit; exact complete UTF8 prefix6 and native units4; cleanup: pass\n"),
 ("--partial-error","controlled TTY UTF16 error: real high surrogate acceptance then error5; UTF8 complete prefix1/native units2/partial scalar visible; no retry-offset claim; restored and cleaned: pass\n"),
 ("--partial-cancel","controlled TTY UTF16 cancel: physical BUSY retains count/data/helper; real high surrogate side effect settles as CANCELLED prefix1/native units2/partial scalar; restored and cleaned: pass\n"),
 ("--partial-close","controlled TTY joined cancel-close: pending input plus real output high surrogate side effect held across CLOSED; both buffers/counts/helpers and group lease retained; destroy BUSY; all native exit/join precedes full restoration: pass\n"),
 ("--js-partial","controlled TTY joined JS UTF16 error: real partial scalar reports prefix1/native units2/partialScalar/error5; group error cancels pending input; both closed Promises wait for all helpers and full restoration: pass\n"),
)

def compile_consumers(args,report,directory,tree,helpers,sdk,work,output):
    """Only the extracted public header and supplied archive enter compilation."""
    for fault in (False,True):
        build=work/('fault' if fault else 'normal');(build/'tmp').mkdir(parents=True)
        name='qaplet-tty-faults' if fault else 'qaplet-tty-host'
        source=sdk/('tty-faults.c' if fault else 'host-tty.c')
        symbols=('tcsetattr','SetConsoleMode','WriteConsole','poll','pthread_create','read') if fault else ()
        environment=dict(os.environ,TMPDIR=str(build/'tmp'),SOURCE_DATE_EPOCH='0',LC_ALL='C',TZ='UTC',BUILDLOG=str(output/(name+'.build-commands.log')))
        report.setdefault('external_tty_consumers',[]).append({'source':identity(source),'include_directories':[str(sdk/'include')],'archive':identity(sdk/'lib/libqaplet.a'),'private_headers':False,'wrapped_symbols':list(symbols),'controlled_system_fixture':fault})
        run(report,directory,name+'-compile',[tree/'bin/cosmocc','-Os','-D_GNU_SOURCE','-DQAPLET_TTY_STANDALONE','-I'+str(sdk/'include'),'-ffile-prefix-map='+str(sdk)+'=sdk','-c',source,'-o',build/'host.o'],args.compile_timeout,build,environment)
        link_portable(args,report,directory,tree,helpers,name,[build/'host.o',sdk/'lib/libqaplet.a'],build/(name+'.com'),build,environment,libraries=(*('-Wl,--wrap='+s for s in symbols),'-lm','-lpthread'))
        publish_ape(build,output,name,helpers,ape_helpers.source_cache(args.helper_source_cache.resolve(),consumption_purpose('build',args.controlled_probe)))
        artifact=audit(args,report,directory,tree,name,output);artifact['build_commands']=identity(output/(name+'.build-commands.log'));report['artifacts'].append(artifact)

def mode_readback(directory):
    path=directory/'tty-native.ndjson'
    if not path.exists():return None
    rows=[json.loads(line) for line in path.read_text().splitlines()]
    def fields(row):return {k:v for k,v in row.items() if k!='stage'}
    groups={}
    for row in rows:
        prefix,stage=row['stage'].rsplit('.',1) if '.' in row['stage'] else ('terminal',row['stage'])
        if stage in groups.setdefault(prefix,{}):raise BuildError('Duplicate terminal snapshot stage')
        groups[prefix][stage]=row
    if not all(set(g)=={'before','modified','restored'} for g in groups.values()) or not all(r['ok'] for r in rows):raise BuildError('Complete native terminal snapshots required')
    if any(fields(g['before'])!=fields(g['restored']) for g in groups.values()) or all(fields(g['before'])==fields(g['modified']) for g in groups.values()):raise BuildError('Native terminal full readback differs')
    return {'artifact':identity(path),'complete_original_equals_restored':True,'modified_differs':True,'rows':rows}

def run_supplied(args,report,directory,artifacts,loader):
    windows=report['host']['os']=='Windows'
    for fault,cases in ((False,NORMAL+(WINDOWS if windows else POSIX)),(True,FAULT+(WINDOWS_FAULT if windows else POSIX_FAULT))):
        artifact=artifacts/('qaplet-tty-faults.com' if fault else 'qaplet-tty-host.com')
        before=identity(artifact)
        if not windows:artifact.chmod(artifact.stat().st_mode|0o111)
        for mode,expected in cases:
            name=artifact.stem+'.'+mode.removeprefix('--');work=directory/name;work.mkdir()
            native=None
            if windows:
                native_path=work/'console.json'
                executed,stdout,stderr=run(report,directory,name,[sys.executable,Path(__file__).resolve(),'console-child','--artifact',artifact,'--mode='+mode,'--directory',work,'--timeout',str(args.timeout),'--native-report',native_path],args.timeout+15,expected=None)
                if native_path.exists():native=json.loads(native_path.read_text())
            else:executed,stdout,stderr=run(report,directory,name,[*loader,artifact,mode],args.timeout,work,expected=None)
            raw_stdout=Path(executed['stdout_log']).read_bytes();raw_stderr=Path(executed['stderr_log']).read_bytes()
            cleanup=not windows or native is not None and not native['timed_out'] and native['exit_code']==0 and not native['error'] and all(native['cleanup'].values())
            measured_mode=mode in ('--m2-tty-lease','--m2-tty-join-js')
            snapshots=mode_readback(work) if measured_mode else None
            passed=executed['exit_code']==0 and not executed['timed_out'] and raw_stdout==expected.encode() and not raw_stderr and cleanup and (not measured_mode or snapshots is not None)
            report['cases'].append({'case_id':name,'status':'pass' if passed else 'fail','required':True,'controlled_system_fixture':fault,'command':executed,'expected':{'exit_code':0,'stdout':expected,'stderr':''},'actual':{'exit_code':executed['exit_code'],'stdout':stdout,'stderr':stderr,'timed_out':executed['timed_out']},'native_console':native,'full_mode_readback':snapshots,'artifact':before,'sdk_sha256':report['sdk']['sha256'],'scope':'Current supplied public SDK; private PTY/ConPTY are Host fixtures, not public creation APIs'})
        if identity(artifact)!=before:raise BuildError('TTY consumer artifact changed during execution')

def console_child(args):
    from m2_tty_terminal import conpty
    record,raw=conpty([str(args.artifact.resolve()),args.mode],args.directory.resolve(),args.timeout)
    args.native_report.write_text(json.dumps(record,indent=2))
    return 0 if record['exit_code']==0 and not record['timed_out'] and not record['error'] and all(record['cleanup'].values()) else 1

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('operation',choices=('run','console-child'))
    parser.add_argument('--producer',type=Path);parser.add_argument('--build-report',type=Path);parser.add_argument('--artifact',type=Path);parser.add_argument('--artifact-dir',type=Path);parser.add_argument('--report',type=Path)
    parser.add_argument('--loader-dir',type=Path);parser.add_argument('--loader-manifest',type=Path);parser.add_argument('--expected-os');parser.add_argument('--expected-arch');parser.add_argument('--runner-label',default='local');parser.add_argument('--controlled-probe',action='store_true')
    parser.add_argument('--mode');parser.add_argument('--directory',type=Path);parser.add_argument('--native-report',type=Path);parser.add_argument('--timeout',type=int,default=30)
    args=parser.parse_args()
    if args.operation=='console-child':return console_child(args)
    import sdk
    producer=json.loads(args.producer.read_text());build=json.loads(args.build_report.read_text())
    directory=args.report.resolve().parent/'evidence'/str(uuid.uuid4())
    guard_evidence_outputs([args.report,directory],[args.producer,args.build_report,args.loader_manifest],[args.artifact_dir])
    directory.mkdir(parents=True)
    report={'schema_version':1,'report_kind':'qaplet-sdk-tty','scope':'run','status':'not_run','started_at':now(),'host':host_facts(),'cases':[],'commands':[],'sdk':producer['sdk'],'sdk_recipe':producer['sdk_recipe'],'producer_report':identity(args.producer),'sdk_build_report':identity(args.build_report),'artifacts':[],'ci':None}
    try:
        consume_identity(build,report,consumption_purpose('build',args.controlled_probe))
        if producer['status']!='pass' or producer['scope']!='tty-consume' or build['status']!='pass' or producer['sdk']!=build['sdk'] or producer['sdk_recipe']!=build['sdk_recipe'] or sdk.recipe()!=build['sdk_recipe']['files']:raise BuildError('Successful matching current SDK and TTY producers required')
        if {a['name'] for a in producer['artifacts']}!={'qaplet-tty-host','qaplet-tty-faults'} or len(producer['artifacts'])!=2:raise BuildError('Exactly the normal and controlled public TTY artifacts required')
        for artifact in producer['artifacts']:
            path=args.artifact_dir/Path(artifact['path']).name
            if sha256(path)!=artifact['sha256'] or path.stat().st_size!=artifact['size_bytes'] or artifact['audit']['status']!='pass':raise BuildError('Supplied TTY artifact differs from audited Ubuntu producer')
            report['artifacts'].append(identity(path))
        measured_host(args,report,directory);loader=select_loader(args,report);run_supplied(args,report,directory,args.artifact_dir.resolve(),loader)
        if sdk.recipe()!=build['sdk_recipe']['files']:raise BuildError('SDK recipe changed during TTY replay')
        report.update(status='pass' if all(c['status']=='pass' for c in report['cases']) else 'fail',reason='Same Ubuntu-built external public TTY consumers replayed with actual native terminal cleanup')
    except (OSError,ValueError,KeyError,BuildError) as error:report.update(status='fail',reason=str(error))
    report['finished_at']=now();write_evidence_report(args.report,report);print(json.dumps({'status':report['status'],'reason':report['reason'],'report':str(args.report)}));return 0 if report['status']=='pass' else 1

if __name__=='__main__':sys.exit(main())
