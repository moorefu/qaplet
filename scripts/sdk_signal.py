#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Ubuntu-built public signal Host and independently identified CLI observers."""
import argparse,json,os,re,shutil,sys,uuid
from pathlib import Path
from build_ape import BuildError,audit,identity,link_portable,publish_ape,run
from foundation import ROOT,host_facts,now,sha256
from workflow_support import write_evidence_report
import sdk

MODES={
    'signal-host':('cli-signal.c',()),
    'signal-fault':('cli-signal-faults.c',('pthread_create','pthread_cond_timedwait')),
    'signal-late':('cli-signal-late.c',('write','GetProcAddress','qaplet_destroy')),
    'signal-observer':('cli-signal-observer.c',('CreateProcess','TerminateProcess','GetExitCodeProcess','waitpid','qaplet_io_read','qaplet_close','qaplet_quiesce','qaplet_destroy')),
}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--producer',type=Path,required=True);p.add_argument('--supplied',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
    p.add_argument('--build-dir',type=Path,required=True);p.add_argument('--timeout',type=int,default=30);p.add_argument('--compile-timeout',type=int,default=300);args=p.parse_args()
    output=args.output_dir.resolve();directory=args.report.resolve().parent/('signal-build-'+str(uuid.uuid4()));directory.mkdir(parents=True)
    report={'report_kind':'qaplet-cli-signal-consumers','status':'not_run','started_at':now(),'host':host_facts(),'commands':[],'artifacts':[],'ci':None}
    try:
        if report['host']['os']!='Linux' or __import__('platform').freedesktop_os_release().get('ID')!='ubuntu':raise BuildError('Ubuntu producer required')
        producer=json.loads(args.producer.read_text());supplied=args.supplied.resolve();sdk.check_package(producer,supplied)
        if producer['status']!='pass':raise BuildError('Successful SDK producer required')
        if output.exists() and any(output.iterdir()):raise BuildError('New output directory required')
        output.mkdir(parents=True,exist_ok=True);inputs=output/'inputs';inputs.mkdir()
        # Snapshot every exact recipe input before compiling; verify rather than
        # assuming current checkout still matches the frozen SDK supply.
        for item in producer['build']['recipe_files']:
            source=ROOT/item['path'];target=inputs/item['path'];target.parent.mkdir(parents=True,exist_ok=True)
            if sha256(source)!=item['sha256']:raise BuildError('Current recipe differs from supplied producer: '+item['path'])
            shutil.copyfile(source,target)
        report.update(producer_report=identity(args.producer),sdk=producer['sdk'],sdk_recipe=producer['sdk_recipe'],package=producer['package'],recipe_snapshots=[identity(path) for path in sorted(inputs.rglob('*')) if path.is_file()])
        tree=Path(producer['native_toolchain']['tree']);helpers=producer['portable_helpers'];work=args.build_dir.resolve()/str(uuid.uuid4());work.mkdir(parents=True)
        report['build_directory']=str(work);archive=supplied/'sdk/lib/libqaplet.a';header=supplied/'sdk/include'
        report['public_sdk']={'archive':identity(archive),'header':identity(header/'qaplet.h'),'private_headers':False}
        for mode,(fixture,symbols) in MODES.items():
            build=work/mode;(build/'tmp').mkdir(parents=True)
            env=dict(os.environ,TMPDIR=str(build/'tmp'),LC_ALL='C',TZ='UTC',SOURCE_DATE_EPOCH='0',BUILDLOG=str(output/(mode+'.build-commands.log')))
            objects=[]
            units=[(supplied/'sdk'/fixture,'fixture')]
            if mode!='signal-host':units.insert(0,(inputs/'src/cli.c','cli'))
            for source,name in units:
                run(report,directory,mode+'-'+name+'-compile',[tree/'bin/cosmocc','-Os','-I'+str(header),'-c',source,'-o',build/(name+'.o')],args.compile_timeout,build,env);objects.append(build/(name+'.o'))
            link_portable(args,report,directory,tree,helpers,mode,[*objects,archive],build/(mode+'.com'),build,env,libraries=(*('-Wl,--wrap='+symbol for symbol in symbols),'-lm','-lpthread'))
            helper_source=next(Path(item['path']).parent for item in helpers['source']['files'] if Path(item['path']).name=='LICENSE')
            publish_ape(build,output,mode,helpers,helper_source)
            report['artifacts'].append(audit(args,report,directory,tree,mode,output))
        normal=supplied/'qaplet.com';report['normal_cli']=identity(normal)
        _,body,_=run(report,directory,'normal-console-callback-abi',[tree/'bin/ape-x86_64.elf',tree/'bin/x86_64-linux-cosmo-objdump','-d','--disassemble=windows_interrupt',supplied/'qaplet.com.dbg'],args.timeout)
        report['console_callback_audit']={'status':'pass' if '<windows_interrupt>:' in body and not re.search(r'%[fg]s:|__tls_get_addr|__stack_chk|__atomic_',body) else 'fail','method':'Actual supplied x64 callback disassembly; explicit MS ABI/raw calls; no TLS/stack protector/external atomic helper'}
        if report['console_callback_audit']['status']!='pass':raise BuildError('Console callback native ABI audit failed')
        report.update(status='pass',reason='Public-header signal Host and separate fault/late/native-observer suppliers built on Ubuntu; runtime behavior belongs to consumer reports')
    except (BuildError,OSError,ValueError,KeyError) as error:report.update(status='fail',reason=str(error))
    report['finished_at']=now();write_evidence_report(args.report,report);print(json.dumps({'status':report['status'],'report':str(args.report),'reason':report['reason']}));return report['status']!='pass'
if __name__=='__main__':sys.exit(main())
