#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Unsuspended public JS execution gate with independent native child ownership.

The arm is built on Ubuntu by sdk_signal.py; this helper consumes those same
bytes on either host. Child exit does not prove all SDK native quiescence.
"""
import argparse,ctypes,hashlib,json,os,signal,subprocess,sys,time,platform
from pathlib import Path
from m2_cli_signal import capture_inputs, identity, send_signal
def native_windows():
 from ctypes import wintypes as W
 k=ctypes.WinDLL('kernel32',use_last_error=True)
 class Entry(ctypes.Structure):
  _fields_=[('dwSize',W.DWORD),('cntUsage',W.DWORD),('pid',W.DWORD),('heap',ctypes.c_size_t),('module',W.DWORD),('threads',W.DWORD),('ppid',W.DWORD),('priority',W.LONG),('flags',W.DWORD),('name',W.WCHAR*260)]
 k.CreateToolhelp32Snapshot.argtypes=[W.DWORD,W.DWORD];k.CreateToolhelp32Snapshot.restype=W.HANDLE
 k.Process32FirstW.argtypes=[W.HANDLE,ctypes.POINTER(Entry)];k.Process32NextW.argtypes=[W.HANDLE,ctypes.POINTER(Entry)]
 k.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];k.OpenProcess.restype=W.HANDLE
 k.QueryFullProcessImageNameW.argtypes=[W.HANDLE,W.DWORD,W.LPWSTR,ctypes.POINTER(W.DWORD)]
 k.WaitForSingleObject.argtypes=[W.HANDLE,W.DWORD];k.WaitForSingleObject.restype=W.DWORD
 k.GetExitCodeProcess.argtypes=[W.HANDLE,ctypes.POINTER(W.DWORD)];k.TerminateProcess.argtypes=[W.HANDLE,W.DWORD];k.CloseHandle.argtypes=[W.HANDLE]
 def snapshot():
  h=k.CreateToolhelp32Snapshot(2,0)
  if h in (None,ctypes.c_void_p(-1).value):raise OSError(ctypes.get_last_error(),'CreateToolhelp32Snapshot')
  out={};e=Entry();e.dwSize=ctypes.sizeof(e);ok=k.Process32FirstW(h,ctypes.byref(e))
  while ok:out[e.pid]={'pid':e.pid,'ppid':e.ppid,'name':e.name};ok=k.Process32NextW(h,ctypes.byref(e))
  k.CloseHandle(h);return out
 def open_owned(pid,expected):
  h=k.OpenProcess(0x100000|0x1000|1,False,pid)
  if not h:raise OSError(ctypes.get_last_error(),'OpenProcess')
  text=ctypes.create_unicode_buffer(32768);n=W.DWORD(len(text))
  if not k.QueryFullProcessImageNameW(h,0,text,ctypes.byref(n)):k.CloseHandle(h);raise OSError(ctypes.get_last_error(),'QueryFullProcessImageNameW')
  if os.path.normcase(str(Path(text.value).resolve()))!=os.path.normcase(str(expected.resolve())):k.CloseHandle(h);raise ValueError('Owned process image mismatch')
  return h,text.value
 return k,W,snapshot,open_owned
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cli',type=Path,required=True);p.add_argument('--arm',type=Path,required=True);p.add_argument('--host',type=Path);p.add_argument('--loader',type=Path);p.add_argument('--report',type=Path,required=True);a=p.parse_args();cases=[]
 if os.name=='nt' and not a.host:p.error('Windows strong gate requires the supplied native Console Host')
 a.report=a.report.resolve();a.report.parent.mkdir(parents=True,exist_ok=True);consumer=capture_inputs(a.report.parent/(a.report.stem+'-raw'))
 if os.name!='nt':
  libc=ctypes.CDLL(None,use_errno=True);libc.prctl.argtypes=[ctypes.c_int,ctypes.c_ulong,ctypes.c_ulong,ctypes.c_ulong,ctypes.c_ulong];libc.prctl.restype=ctypes.c_int
  if libc.prctl(36,1,0,0,0):raise OSError(ctypes.get_errno(),'PR_SET_CHILD_SUBREAPER')
  subreaper=ctypes.c_int();probe=ctypes.CDLL(None,use_errno=True).prctl
  if probe(37,ctypes.byref(subreaper),0,0,0) or subreaper.value!=1:raise RuntimeError('Subreaper verification failed')
 else:k,W,snapshot,open_owned=native_windows()
 for number in ((None,) if os.name=='nt' else (signal.SIGINT,signal.SIGTERM)):
  d=a.report.parent/(a.report.stem+'-raw')/('CtrlC' if number is None else str(int(number)));d.mkdir(parents=True)
  childargs=([str(a.loader.resolve())] if a.loader else [])+[str(a.arm.resolve())]
  source="import('qaplet:process').then(p=>{const a="+json.dumps(childargs)+";p.spawn(a[0],{argv:a,stdio:['ignore','inherit','ignore']});for(;;){}})"
  sf=d/'source.js';sf.write_text(source);(d/'driver-input.py').write_bytes(Path(__file__).read_bytes())
  ready=False;expired=False;proof=None;child_result=None;child_handle=None;owner_handle=None;native=None;failure=None;forced=False;delivery=None;before=time.monotonic()
  if os.name=='nt':
   startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
   argv=[str(a.host.resolve()),'--console',str(a.cli.resolve()),str(sf.resolve())];env=dict(os.environ)
   for key in ('QAPLET_SIGNAL_LATE','QAPLET_SIGNAL_THREAD_FAULT','QAPLET_SIGNAL_SPLIT_WAIT'):env.pop(key,None)
   fo=(d/'host.stdout').open('wb');fe=(d/'host.stderr').open('wb');proc=subprocess.Popen(argv,cwd=d,stdin=subprocess.DEVNULL,stdout=fo,stderr=fe,env=env,creationflags=subprocess.CREATE_NEW_CONSOLE,startupinfo=startup)
  else:
   argv=[*([str(a.loader.resolve())] if a.loader else []),str(a.cli.resolve()),'--timeout-ms','600000','--max-dispatches','100000000','eval','--source',source]
   fo=(d/'cli.stdout').open('wb');fe=(d/'cli.stderr').open('wb');proc=subprocess.Popen(argv,cwd=d,stdin=subprocess.DEVNULL,stdout=fo,stderr=fe,start_new_session=True)
  (d/'controller-process.json').write_text(json.dumps({'pid':proc.pid,'driver_pid':os.getpid(),'argv':argv})+'\n')
  try:
   deadline=time.monotonic()+20
   while time.monotonic()<deadline and proc.poll() is None:
    pidfile=d/'strong-child.json'
    if proof is None and pidfile.is_file():
     try:child_data=json.loads(pidfile.read_text())
     except (OSError,json.JSONDecodeError):time.sleep(.001);continue
     pid=child_data['native_pid']
     if os.name=='nt':
      table=snapshot();entry=table[pid];owner=table[entry['ppid']]
      if owner['ppid']!=proc.pid:raise ValueError('Native ownership chain mismatch')
      child_handle,child_image=open_owned(pid,a.arm);owner_handle,owner_image=open_owned(owner['pid'],a.cli)
      proof={'child_pid':pid,'child_image':child_image,'owner_pid':owner['pid'],'owner_image':owner_image,'controller_pid':proc.pid,'actual_parent_chain':[entry,owner,table[proc.pid]],'child_handle_held_before_READY':True,'child_alive_before_arm':k.WaitForSingleObject(child_handle,0)==258}
      if not proof['child_alive_before_arm']:raise ValueError('Native arm child already exited')
     else:
      status=Path(f'/proc/{pid}/status').read_text();ppid=int(next(line.split()[1] for line in status.splitlines() if line.startswith('PPid:')))
      if ppid!=proc.pid:raise ValueError('POSIX arm parent mismatch')
      starttime=Path(f'/proc/{pid}/stat').read_text().rsplit(')',1)[1].split()[19]
      proof={'child_pid':pid,'owner_pid':proc.pid,'actual_child_status':status,'child_exe':os.readlink(f'/proc/{pid}/exe'),'child_starttime_ticks':starttime,'subreaper_verified':True,'actual_parent_pid':ppid}
     if os.name!='nt':
      image=os.readlink(f'/proc/{pid}/exe');cmdline=Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0')
      proof['child_cmdline']=[x.decode('utf-8') for x in cmdline if x]
      if Path(image).resolve()!=(a.loader or a.arm).resolve() or str(a.arm.resolve()) not in proof['child_cmdline']:raise ValueError('POSIX native arm image/argv mismatch')
     (d/'strong-child-arm').write_text('Host verified exact native identity/ownership before READY')
    out=d/'cli.stdout'
    if out.is_file() and out.read_bytes()==b'READY\n':ready=True;break
    time.sleep(.001)
   if not ready:raise RuntimeError('Strong native READY gate not accepted')
   if os.name!='nt':
    delivery=send_signal(proc.pid,number)
    if delivery['errno']:raise RuntimeError('Strong gate signal delivery failed')
   try:proc.wait(timeout=15)
   except subprocess.TimeoutExpired:expired=True;raise
   if os.name=='nt':
    native=json.loads((d/'ctrl-result.json').read_text());code=W.DWORD();waited=k.WaitForSingleObject(child_handle,5000);got=k.GetExitCodeProcess(child_handle,ctypes.byref(code))
    child_result={'actual_native_wait_result':waited,'exit_query_ok':bool(got),'exit_code':code.value,'physical_exit_observed':waited==0 and bool(got) and code.value!=259,'SDK_internal_reap_directly_instrumented':False}
    child_complete=child_result['physical_exit_observed'] and code.value==53261
    if not child_complete:raise RuntimeError('Known native arm child did not exit')
    target_exit=native['exit_code'];stop_ok=proc.returncode==0 and native['sender_received'] and native['target_in_same_console'] and native['group']==0 and not native['supervisor_timeout']
   else:
    try:
     adopted,status=os.waitpid(proof['child_pid'],os.WNOHANG);child_result={'subreaper_waitpid_return':adopted,'raw_status':status,'SDK_reaped_before_owner_exit':False,'fixture_adopted_child':True};child_complete=False
    except ChildProcessError:
     absent=not Path(f"/proc/{proof['child_pid']}").exists();child_result={'subreaper_waitpid_errno':'ECHILD','child_proc_absent':absent,'SDK_reaped_before_owner_exit':absent,'proof_scope':'Initial PPid was exact CLI; verified subreaper would adopt an unreaped orphan, so ECHILD+absence demonstrates prior parent reap'};child_complete=absent
    target_exit=proc.returncode;stop_ok=True
    if not child_complete:raise RuntimeError('Child not reaped by SDK before owner exit')
  except Exception as error:failure=str(error);target_exit=None
  finally:
   if failure:
    forced=True
    if os.name=='nt':
     for handle in (child_handle,owner_handle):
      if handle and k.WaitForSingleObject(handle,0)==258:k.TerminateProcess(handle,98);k.WaitForSingleObject(handle,5000)
    elif proof:
     try:
      fields=Path(f"/proc/{proof['child_pid']}/stat").read_text().rsplit(')',1)[1].split()
      if fields[19]==proof['child_starttime_ticks'] and int(fields[1]) in (os.getpid(),proc.pid):os.kill(proof['child_pid'],signal.SIGKILL)
     except (OSError,ProcessLookupError):pass
    if proc.poll() is None:proc.kill()
    proc.wait()
    if os.name!='nt' and proof:
     try:os.waitpid(proof['child_pid'],0)
     except ChildProcessError:pass
   if os.name=='nt':
    for handle in (child_handle,owner_handle):
     if handle:k.CloseHandle(handle)
   fo.close();fe.close()
  errors=(d/'cli.stderr').read_bytes() if (d/'cli.stderr').is_file() else b''
  passed=not failure and ready and not expired and target_exit==1 and bool(stop_ok) and child_complete and b'EXECUTION_STOPPED' in errors and b'cleanup incomplete' not in errors
  cases.append({'case':'strong-owned-child-'+('Ctrl+C' if number is None else signal.Signals(number).name),'status':'pass' if passed else 'fail','controller_argv':argv,'source':identity(sf),'source_text':source,'native_arm_argv':childargs,'native_arm_artifact':identity(a.arm),'public_spawn_not_awaited':True,'same_js_stack_never_suspends':True,'identity_verified_before_READY':proof,'ready_accepted':ready,'actual_native_controller':native,'child_retirement':child_result,'exit_code':target_exit,'failure':failure,'supervisor_timeout':expired,'forced_fixture_cleanup':forced,'elapsed_seconds':time.monotonic()-before,'native_cleanup_complete':None,'observed_child_exit_complete':bool(passed),'scope':'Frozen normal SKU1 CLI; public spawn then same JS stack for(;;), no await. Separately supplied native child publishes real PID, waits Host identity verification, then inherited stdout READY. Windows records independent Host child-HANDLE exit; SDK internal wait is not instrumented here. Linux verified subreaper distinguishes SDK reap from fixture adoption; mixed scope separately observes native SDK reap.'})
  cases[-1].update(pid=proc.pid,signal_delivery=delivery,cwd=str(d))
 report={'status':'pass' if all(x['status']=='pass' for x in cases) else 'fail','cli':identity(a.cli),'native_arm':identity(a.arm),'controller':identity(a.host) if a.host else None,'script':identity(Path(__file__)),'cases':cases,'host':{'platform':platform.platform(),'machine':platform.machine(),'page_size':__import__('mmap').PAGESIZE},'ci':None}
 report['consumer_identity']=consumer
 for case in cases:case['scope']=case['scope'].replace('Frozen normal SKU1 CLI','Explicit supplied CLI')
 a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'status':report['status'],'report':str(a.report)}));return report['status']!='pass'
if __name__=='__main__':raise SystemExit(main())
