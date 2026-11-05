#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Supplied CLI subprocess contracts and outside-source public SDK contrast."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import uuid

import ape_helpers
from build_ape import BuildError, audit, identity, link_portable, publish_ape, run
from foundation import host_facts, now, sha256
from input_lock import consumption_purpose
from startup import measured_host, select_loader
from workflow_support import guard_evidence_outputs, write_evidence_report


def compile_consumers(args, report, directory, tree, helpers, sdk, work, output):
    (work / "tmp").mkdir(parents=True)
    env = dict(os.environ, TMPDIR=str(work / "tmp"), SOURCE_DATE_EPOCH="0", LC_ALL="C", TZ="UTC",
               BUILDLOG=str(output / "qaplet-cli-host.build-commands.log"))
    report["external_cli_consumer"] = {"source": identity(sdk / "host-cli.c"),
        "include_directories": [str(sdk / "include")], "archive": identity(sdk / "lib/libqaplet.a"), "private_headers": False}
    run(report,directory,"cli-host-compile",[tree / "bin/cosmocc","-Os","-D_GNU_SOURCE","-DSDK_CLI_STANDALONE",
        "-I"+str(sdk / "include"),"-c",sdk / "host-cli.c","-o",work / "host.o"],args.compile_timeout,work,env)
    link_portable(args,report,directory,tree,helpers,"qaplet-cli-host",[work / "host.o",sdk / "lib/libqaplet.a"],
                  work / "qaplet-cli-host.com",work,env,libraries=("-lm","-lpthread"))
    publish_ape(work,output,"qaplet-cli-host",helpers,ape_helpers.source_cache(args.helper_source_cache.resolve(),
                consumption_purpose("build",args.controlled_probe)))
    report["artifacts"].append(audit(args,report,directory,tree,"qaplet-cli-host",output))


def digest(data):
    return {"sha256":hashlib.sha256(data).hexdigest(),"size_bytes":len(data)}


def execute(args, report, directory, name, argv, code, expected, stderr=b"", data=b"", pipes=False, error_contains=False):
    work=directory/name;work.mkdir()
    (work/"stdin.bin").write_bytes(data)
    started=now(); timed_out=False; flags=[]; threads=[]; failure=None
    output=[bytearray(),bytearray()]
    if pipes and os.name!="nt":
        import fcntl
        pairs=[os.pipe() for _ in range(3)]
        owned=[pairs[0][0],pairs[1][1],pairs[2][1]]
        flags=[fcntl.fcntl(fd,fcntl.F_GETFL) for fd in owned]
        def reader(fd,index):
            try:
                while True:
                    chunk=os.read(fd,4096)
                    if not chunk:break
                    output[index].extend(chunk)
            finally:os.close(fd)
        def writer():
            try:
                at=0
                while at<len(data):at+=os.write(pairs[0][1],data[at:])
            except BrokenPipeError:pass
            finally:os.close(pairs[0][1])
        p=subprocess.Popen(list(map(str,argv)),cwd=work,stdin=owned[0],stdout=owned[1],stderr=owned[2],start_new_session=True)
        threads=[threading.Thread(target=reader,args=(pairs[i][0],i-1)) for i in (1,2)]+[threading.Thread(target=writer)]
        for t in threads:t.start()
        try:p.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:timed_out=True;p.kill();p.wait()
        restored=[fcntl.fcntl(fd,fcntl.F_GETFL) for fd in owned]
        for fd in owned:os.close(fd)
        for t in threads:t.join(5)
        stdout,errors=map(bytes,output)
        native={"before_flags":flags,"restored_flags":restored,"full_flags_restored":flags==restored,
                "producer_and_readers_joined":all(not t.is_alive() for t in threads),"read_chunk_bytes":4096}
    elif pipes:
        p=subprocess.Popen(list(map(str,argv)),cwd=work,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:stdout,errors=p.communicate(data,timeout=args.timeout)
        except subprocess.TimeoutExpired:timed_out=True;p.kill();stdout,errors=p.communicate()
        native={"route":"actual synchronous anonymous Windows pipes; SDK attachment remains unsupported"}
    else:
        with (work/"stdin.bin").open("rb") as fi,(work/"stdout.log").open("wb") as fo,(work/"stderr.log").open("wb") as fe:
            p=subprocess.Popen(list(map(str,argv)),cwd=work,stdin=fi,stdout=fo,stderr=fe,start_new_session=os.name!="nt")
            try:p.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:timed_out=True;p.kill();p.wait()
        stdout=(work/"stdout.log").read_bytes();errors=(work/"stderr.log").read_bytes();native={"route":"actual synchronous regular files"}
    (work/"stdout.log").write_bytes(stdout);(work/"stderr.log").write_bytes(errors)
    command={"argv":list(map(str,argv)),"cwd":str(work),"started_at":started,"finished_at":now(),"exit_code":p.returncode,
        "timed_out":timed_out,"timeout_seconds":args.timeout,"stdout_log":str(work/"stdout.log"),"stderr_log":str(work/"stderr.log")}
    passed=not timed_out and p.returncode==code and stdout==expected and (stderr in errors if error_contains else errors==stderr)
    passed=passed and b"native cleanup incomplete" not in errors and b"standard stream restoration incomplete" not in errors
    if flags:passed=passed and native["full_flags_restored"] and native["producer_and_readers_joined"]
    report["commands"].append(command)
    report["cases"].append({"case_id":"qaplet-cli."+name,"required":True,"status":"pass" if passed else "fail",
        "commands":[command],"expected":{"exit_code":code,"stdout":digest(expected),"stderr":digest(stderr),"stderr_contains":error_contains},
        "actual":{"stdout":digest(stdout),"stderr":digest(errors)},"stdin":identity(work/"stdin.bin"),"native":native,
        "native_cleanup_complete":passed,"scope":"Actual supplied public CLI, exact raw bytes/status; timeout never proves cleanup"})


def run_supplied(args, report, directory, artifacts, loader, cli, modules):
    import sdk
    host=artifacts/"qaplet-cli-host.com"; windows=os.name=="nt"
    before=[identity(path) for path in (host,cli)]
    if not windows:
        for path in (host,cli):path.chmod(path.stat().st_mode|0o111)
    expected=next(output for name,_,output in sdk.HOST_CASES if name=="m2-cli-results")
    execute(args,report,directory,"sdk-results",[*loader,host,"--m2-cli-results"],0,expected.encode())
    for name,arguments,code,stdout,stderr in sdk.CLI_CASES:
        execute(args,report,directory,name,[*loader,cli,*[s.replace("{modules}",str(modules)) for s in arguments]],code,
                stdout.encode(),stderr.encode(),error_contains=bool(stderr))
    data=bytes(range(256))*257
    source="import('qaplet:stdio').then(s=>{(async()=>{for(;;){let r=await s.stdin.read(65536).promise;if(r.eof)break;const a=s.stdout.write(r.bytes),b=s.stderr.write(r.bytes);r.bytes.fill(7);await Promise.all([a.promise,b.promise])}})();return 'ENTRY'})"
    execute(args,report,directory,"file-binary-background",[*loader,cli,"eval","--source",source],0,data+b"ENTRY\n",data,data)
    if not windows:execute(args,report,directory,"pipe-binary-background",[*loader,cli,"eval","--source",source],0,data+b"ENTRY\n",data,data,pipes=True)
    else:
        source="import('qaplet:stdio').then(s=>s.stdin===null&&s.stdout===null&&s.stderr===null)"
        execute(args,report,directory,"synchronous-pipes-unsupported",[cli,"eval","--source",source],0,b"true\n",pipes=True)
    execute(args,report,directory,"large-result-restored",[*loader,cli,"eval","--source","'X'.repeat(200000)"],0,b"X"*200000+b"\n",pipes=True)
    childargs=[str(host),"--m2-cli-emit"] if windows else [str(loader[0]),str(host),"--m2-cli-emit"]
    source="Promise.all([import('qaplet:process'),import('qaplet:stdio')]).then(([{spawn},s])=>{const a="+json.dumps(childargs)+";const p=spawn(a[0],{argv:a});p.stdin.finish();async function drain(input,output){for(;;){const r=await input.read(4096).promise;if(r.eof)break;await output.write(r.bytes).promise}}Promise.all([drain(p.stdout,s.stdout),drain(p.stderr,s.stderr),p.exited]).then(v=>{if(v[2].exitCode!==37)throw Error('child exit')});return 'ENTRY'})"
    emit=bytes(range(256))*512
    execute(args,report,directory,"process-dual-channel",[*loader,cli,"eval","--source",source],0,emit+b"ENTRY\n",emit)
    source="import('qaplet:process').then(({spawn})=>{try{spawn('qaplet-missing-explicit-path',{stdio:['ignore','ignore','ignore']})}catch(e){if(e.code===27&&e.errorDomain&&e.error)return 42;throw e}throw Error('missing refusal')})"
    execute(args,report,directory,"ordinary-process-error",[*loader,cli,"eval","--source",source],0,b"42\n")
    source="import('qaplet:timers').then(t=>{t.setImmediate(()=>{try{let a=[];for(;;)a.push(new Uint8Array(1048576))}catch(e){}});return 'ENTRY'})"
    execute(args,report,directory,"background-allocation",[*loader,cli,"eval","--source",source],1,b"",b"qaplet: code=11 MEMORY_LIMIT",error_contains=True)
    for mode in ("normal","error","budget"):
        work=directory/("terminal-"+mode);work.mkdir()
        argv=[*loader,host,"--m2-cli-terminal",cli,host,loader[0] if loader else "-",modules/"cli-terminal.mjs",mode]
        if windows:
            from m2_tty_terminal import conpty
            command,raw=conpty(argv,work,args.timeout)
            passed=command["exit_code"]==0 and not command["timed_out"] and not command["error"] and all(command["cleanup"].values())
        else:
            command,_,_=run(report,directory,"terminal-"+mode,argv,args.timeout,work,expected=None)
            passed=command["exit_code"]==0 and not command["timed_out"]
        record=work/"cli-terminal.ndjson";rows=[json.loads(line) for line in record.read_text().splitlines()] if record.is_file() else []
        groups={stage:[{k:v for k,v in row.items() if k!="stage"} for row in rows if row["stage"]==stage] for stage in ("before","modified","restored")}
        restored=len(rows)==9 and groups["before"]==groups["restored"] and groups["before"]!=groups["modified"]
        marker=work/"cli-terminal-result.txt";status=work/"cli-child-status.json"
        passed=passed and restored and marker.is_file() and status.is_file()
        report["commands"].append(command) if windows else None
        report["cases"].append({"case_id":"qaplet-cli.terminal-"+mode,"required":True,"status":"pass" if passed else "fail",
            "commands":[command],"terminal_readback":identity(record) if record.is_file() else None,"rows":rows,
            "full_modes_and_flags_restored":restored,"child_status":json.loads(status.read_text()) if status.is_file() else None,
            "native_cleanup_complete":bool(passed),"scope":"Initial C prelease; first JS snapshot; released/restored old group; independent raw/VT facades; true CLI child exit; native readback; no signal claim"})
    if before!=[identity(path) for path in (host,cli)]:raise BuildError("Supplied CLI/Host bytes changed")
    report["cli_artifact"]=identity(cli)
    report["actual_parent_imports"]=[{"module":name,**identity(Path(module.__file__).resolve())} for name,module in sorted(sys.modules.items())
        if getattr(module,"__file__",None) and Path(module.__file__).is_file() and Path(module.__file__).resolve().parent==Path(__file__).resolve().parent]
    report["child_dynamic_imports"]="unknown; static recipe is an over-approximation, not a child import trace"


def main():
    import sdk
    p=argparse.ArgumentParser(description=__doc__)
    for key in ("producer","consumer","supplied-host","artifact-dir","report"):p.add_argument("--"+key,type=Path,required=True)
    p.add_argument("--loader-dir",type=Path);p.add_argument("--loader-manifest",type=Path)
    p.add_argument("--expected-os");p.add_argument("--expected-arch");p.add_argument("--runner-label",default="local")
    p.add_argument("--timeout",type=int,default=30);args=p.parse_args()
    directory=args.report.resolve().parent/"evidence"/str(uuid.uuid4())
    guard_evidence_outputs([args.report,directory],[args.producer,args.consumer,args.loader_manifest],[args.artifact_dir,args.supplied_host.parent])
    directory.mkdir(parents=True)
    producer=json.loads(args.producer.read_text(encoding="utf-8"));consumer=json.loads(args.consumer.read_text(encoding="utf-8"))
    report={"schema_version":1,"report_kind":"qaplet-sdk-cli","scope":"run","status":"not_run","started_at":now(),"ci":None,
        "host":host_facts(),"cases":[],"commands":[],"artifacts":[],"sdk":producer["sdk"],"sdk_recipe":producer["sdk_recipe"],
        "producer_report":identity(args.producer),"consumer_report":identity(args.consumer)}
    try:
        if producer["status"]!="pass" or consumer["status"]!="pass":raise BuildError("Successful Ubuntu SDK and external CLI consumer required")
        sdk.check_package(producer,args.supplied_host.parent.resolve())
        for artifact in consumer["artifacts"]:
            path=args.artifact_dir/Path(artifact["path"]).name
            if sha256(path)!=artifact["sha256"] or artifact["audit"]["status"]!="pass":raise BuildError("CLI consumer differs from audited Ubuntu producer")
            report["artifacts"].append(identity(path))
        measured_host(args,report,directory);loader=select_loader(args,report)
        run_supplied(args,report,directory,args.artifact_dir.resolve(),loader,args.supplied_host.parent.resolve()/"qaplet.com",args.supplied_host.parent.resolve()/"sdk/modules")
        report.update(status="pass" if all(c["status"]=="pass" for c in report["cases"]) else "fail",reason="Actual supplied CLI and public SDK contrast; signal/cleanup-budget specialty deferred to ticket 22")
    except (BuildError,OSError,ValueError,KeyError) as error:report.update(status="fail",reason=str(error))
    report["finished_at"]=now();write_evidence_report(args.report,report)
    print(json.dumps({"status":report["status"],"report":str(args.report),"reason":report["reason"]}));return report["status"]!="pass"


if __name__=="__main__":sys.exit(main())
