"""Owned real Windows byte-pipe stdio; explicit input and paused-reader gates."""
import ctypes,json,msvcrt,os,pathlib,subprocess,threading,time,uuid
from ctypes import wintypes as W
K=ctypes.WinDLL('kernel32',use_last_error=True);H=W.HANDLE;D=W.DWORD
class Overlapped(ctypes.Structure):
    _fields_=[('Internal',ctypes.c_size_t),('InternalHigh',ctypes.c_size_t),('Offset',D),('OffsetHigh',D),('hEvent',H)]
K.CreateNamedPipeW.argtypes=[W.LPCWSTR,D,D,D,D,D,D,ctypes.c_void_p];K.CreateNamedPipeW.restype=H
K.CreateFileW.argtypes=[W.LPCWSTR,D,D,ctypes.c_void_p,D,D,H];K.CreateFileW.restype=H
K.CreateEventW.argtypes=[ctypes.c_void_p,W.BOOL,W.BOOL,W.LPCWSTR];K.CreateEventW.restype=H
K.ConnectNamedPipe.argtypes=[H,ctypes.POINTER(Overlapped)];K.GetOverlappedResult.argtypes=[H,ctypes.POINTER(Overlapped),ctypes.POINTER(D),W.BOOL]
K.ReadFile.argtypes=[H,ctypes.c_void_p,D,ctypes.POINTER(D),ctypes.c_void_p]
K.WriteFile.argtypes=[H,ctypes.c_void_p,D,ctypes.POINTER(D),ctypes.c_void_p]
K.PeekNamedPipe.argtypes=[H,ctypes.c_void_p,D,ctypes.c_void_p,ctypes.POINTER(D),ctypes.c_void_p]
K.GetNamedPipeInfo.argtypes=[H,ctypes.POINTER(D),ctypes.POINTER(D),ctypes.POINTER(D),ctypes.POINTER(D)]
K.CloseHandle.argtypes=[H];K.OpenThread.argtypes=[D,W.BOOL,D];K.OpenThread.restype=H;K.CancelSynchronousIo.argtypes=[H]
BAD=ctypes.c_void_p(-1).value
def check(value,operation):
    if not value:raise OSError(ctypes.get_last_error(),operation)
def pipe():
    name='\\\\.\\pipe\\qaplet-cli-overlapped-'+str(os.getpid())+'-'+str(uuid.uuid4())
    server=K.CreateNamedPipeW(name,3|0x40000000,8,1,4096,4096,0,None)
    check(server not in (None,BAD),'CreateNamedPipeW')
    client=K.CreateFileW(name,0xc0000000,0,None,3,0,None);check(client not in (None,BAD),'CreateFileW(client)')
    event=K.CreateEventW(None,True,False,None);check(event,'CreateEventW(connect)');op=Overlapped();op.hEvent=event;n=D()
    ok=K.ConnectNamedPipe(server,ctypes.byref(op));error=ctypes.get_last_error()
    if not ok and error==997:check(K.GetOverlappedResult(server,ctypes.byref(op),ctypes.byref(n),True),'Connect completion')
    elif not ok and error!=535:raise OSError(error,'ConnectNamedPipe')
    check(K.CloseHandle(event),'Close connect event')
    flags,quota,readquota,instances=D(),D(),D(),D();check(K.GetNamedPipeInfo(server,ctypes.byref(flags),ctypes.byref(quota),ctypes.byref(readquota),ctypes.byref(instances)),'GetNamedPipeInfo')
    fd=msvcrt.open_osfhandle(int(server),os.O_RDWR|os.O_BINARY)
    return fd,client,{'name':name,'flags':flags.value,'write_quota':quota.value,'read_quota':readquota.value,'overlapped':True,'byte_mode':not(flags.value&4)}
def run(cli,directory,mode='value',timeout=30,controlled=False,fault=False):
    directory=pathlib.Path(directory);directory.mkdir(parents=True)
    prefix=threading.Event();drain=threading.Event();data=[bytearray(),bytearray()];errors=[];clients=[];fds=[];threads=[];p=None
    started=time.time();timed=False;gate={};pipes=[];stderr_file=None
    try:
        incoming,writer,info=pipe();fds.append(incoming);clients.append(writer);pipes.append(info)
        outgoing,reader,info=pipe();fds.append(outgoing);clients.append(reader);pipes.append(info)
        errfd=None
        if mode=='diagnostic':errfd,errreader,info=pipe();fds.append(errfd);clients.append(errreader);pipes.append(info)
        else:stderr_file=(directory/'stderr.log').open('wb');errreader=None
        def read_one(handle,index,first=False):
            buf=ctypes.create_string_buffer(4096);n=D()
            try:
                if first:
                    while len(data[index])<3:
                        ok=K.ReadFile(handle,buf,3-len(data[index]),ctypes.byref(n),None)
                        if not ok or not n.value:return
                        data[index].extend(buf.raw[:n.value])
                    prefix.set()
                drain.wait(timeout)
                while K.ReadFile(handle,buf,len(buf),ctypes.byref(n),None) and n.value:data[index].extend(buf.raw[:n.value])
            except Exception as error:errors.append(str(error))
        source="import('qaplet:stdio').then(async s=>{if(s.stdin.kind!=='pipe'||s.stdout.kind!=='pipe')throw Error('not attached real overlapped pipes');await s.stdout.write(new Uint8Array([66,0,90])).promise;if((await s.stdin.read(1).promise).bytes[0]!==71)throw Error('gate');return 'X'.repeat(200000)})"
        if mode=='diagnostic':source="Promise.all([import('qaplet:stdio'),import('qaplet:timers')]).then(async([s,t])=>{if(s.stdin.kind!=='pipe'||s.stdout.kind!=='pipe'||s.stderr.kind!=='pipe')throw Error('not attached');await s.stderr.write(new Uint8Array(3584).fill(69)).promise;await s.stdout.write(new Uint8Array([66,0,90])).promise;await s.stdin.read(1).promise;t.setImmediate(()=>{throw Error('D'.repeat(900))});return 'ENTRY'})"
        argv=[str(cli),'eval','--source',source];env=dict(os.environ)
        if fault:env['QAPLET_CLI_OUTPUT_OBSERVER_FAULT']='1'
        p=subprocess.Popen(argv,cwd=directory,stdin=incoming,stdout=outgoing,stderr=errfd if errfd is not None else stderr_file,env=env,creationflags=0x08000000)
        threads=[threading.Thread(target=read_one,args=(reader,0,True),daemon=True)]
        if errreader:threads.append(threading.Thread(target=read_one,args=(errreader,1),daemon=True))
        for thread in threads:thread.start()
        deadline=time.monotonic()+timeout
        while not prefix.wait(.01):
            if p.poll() is not None or time.monotonic()>=deadline:break
        gate['managed_prefix']=bytes(data[0]).hex();gate['prefix_accepted']=prefix.is_set()
        writes=[]
        if prefix.is_set():
            block=ctypes.create_string_buffer(b'G');n=D();check(K.WriteFile(writer,block,1,ctypes.byref(n),None) and n.value==1,'Publish input gate')
            target=errreader if mode=='diagnostic' else reader;available=D();events=[]
            while time.monotonic()<deadline:
                check(K.PeekNamedPipe(target,None,0,None,ctypes.byref(available),None),'Peek paused reader')
                path=directory/'host-output-native.ndjson'
                events=[json.loads(line) for line in path.read_text(encoding='utf-8').splitlines()] if path.is_file() else []
                writes=[e for e in events if e.get('operation')=='WriteFile' and e.get('kind')==mode]
                if writes or available.value>=4096 or p.poll() is not None:break
                threading.Event().wait(.001)
            gate.update(paused_reader_available=available.value,process_alive_before_release=p.poll() is None,observed_native_writes=writes if controlled else [],reader_released=True)
        drain.set()
        try:p.wait(timeout=max(1,deadline-time.monotonic()))
        except subprocess.TimeoutExpired:timed=True;p.kill();p.wait()
    finally:
        drain.set()
        for fd in fds:
            try:os.close(fd)
            except OSError:pass
        if stderr_file:stderr_file.close()
        for thread in threads:
            thread.join(3)
            if thread.is_alive():
                handle=K.OpenThread(1,False,thread.native_id)
                if handle:K.CancelSynchronousIo(handle);K.CloseHandle(handle)
                thread.join(2)
        for handle in clients:K.CloseHandle(handle)
    stdout=bytes(data[0]);stderr=bytes(data[1]) if mode=='diagnostic' else (directory/'stderr.log').read_bytes()
    (directory/'stdout.log').write_bytes(stdout);(directory/'stderr.log').write_bytes(stderr)
    path=directory/'host-output-native.ndjson';events=[json.loads(line) for line in path.read_text(encoding='utf-8').splitlines()] if path.is_file() else []
    expected_stdout=b'B\0Z'+b'X'*200000+b'\n' if mode=='value' else b'B\0Z'
    correct=stdout==expected_stdout and (not stderr if mode=='value' else stderr.startswith(b'E'*3584+b'qaplet: code=7 ') and b'D'*900 in stderr)
    record={'argv':argv,'cwd':str(directory),'exit_code':p.returncode if p else None,'timed_out':timed,'started_unix':started,'finished_unix':time.time(),'mode':mode,'controlled_system_fixture':controlled,'fault_injected':fault,
        'pipe_facts':pipes,'gate':gate,'events':events,'exact_data':correct,'errors':errors,'reader_threads_finished':all(not t.is_alive() for t in threads),'stdout_log':str(directory/'stdout.log'),'stderr_log':str(directory/'stderr.log'),
        'status':'pass' if correct and p.returncode==(0 if mode=='value' else 1) and not timed and not errors and all(not t.is_alive() for t in threads) else 'fail'}
    (directory/'result.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8');return record

def append_cases(args,report,directory,cli,observer):
    from build_ape import identity
    from foundation import now
    for controlled in (False,True):
        for mode in ('value','diagnostic'):
            work=directory/('overlapped-'+('observer-' if controlled else 'normal-')+mode)
            result=run(observer if controlled else cli,work,mode,args.timeout,controlled)
            writes=[e for e in result['events'] if e.get('operation')=='WriteFile' and e.get('asynchronous_pipe')]
            gets=[e for e in result['events'] if e.get('operation')=='GetOverlappedResult']
            exits=[e for e in result['events'] if e.get('operation')=='ExitProcess']
            protocol=None if not controlled else bool(writes and gets and exits) and all(e['overlapped_nonnull'] and e['event_nonnull'] and e['buffer_allocation_known'] for e in writes) and any(e['pending'] for e in writes) and all(e['physical_completion_confirmed'] for e in gets) and all(not e['buffer_freed_while_pending'] and not e['event_closed_while_pending'] and not e['identity_reused_while_pending'] and not e['unconfirmed_operations'] for e in exits)
            passed=result['status']=='pass' and (not controlled or protocol)
            report['cases'].append({'case_id':'qaplet-cli.overlapped-'+('observer-' if controlled else 'normal-')+mode,
                'required':True,'status':'pass' if passed else 'fail','controlled_system_fixture':controlled,
                'artifact':identity(observer if controlled else cli),'native_record':identity(work/'result.json'),'actual':result,
                'protocol_observation':'pass' if protocol else 'fail' if controlled else 'not_observed',
                'native_cleanup_complete':passed,'scope':'Real overlapped byte-pipe standard routes; unwrapped proves public data only; controlled observer relates real Win32 parameters/results and allocator ranges without private SDK layout'})
    work=directory/'overlapped-unknown-completion';result=run(observer,work,'value',args.timeout,True,True)
    exits=[e for e in result['events'] if e.get('operation')=='ExitProcess'];errors=pathlib.Path(result['stderr_log']).read_bytes()
    passed=result['exit_code']==1 and not result['timed_out'] and result['reader_threads_finished'] and bool(exits) and exits[-1]['unconfirmed_operations']>=1 and not exits[-1]['buffer_freed_while_pending'] and not exits[-1]['event_closed_while_pending'] and not exits[-1]['identity_reused_while_pending'] and exits[-1]['fault_injected'] and b'Host output native cleanup incomplete' in errors
    report['cases'].append({'case_id':'qaplet-cli.overlapped-unknown-completion','required':True,'status':'pass' if passed else 'fail',
        'controlled_system_fixture':True,'artifact':identity(observer),'native_record':identity(work/'result.json'),'actual':result,
        'native_cleanup_complete':False,'protocol_observation':'completion_unknown_retained_until_process_exit',
        'scope':'Deliberate GetOverlappedResult refusal while real request remains unconfirmed; no successful Host native cleanup or complete output claimed'})
if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser();parser.add_argument('--cli',type=pathlib.Path,required=True);parser.add_argument('--output',type=pathlib.Path,required=True);args=parser.parse_args()
    rows=[run(args.cli.resolve(),args.output/name,name) for name in ('value','diagnostic')]
    print(json.dumps([{'mode':r['mode'],'status':r['status'],'exit_code':r['exit_code'],'gate':r['gate']} for r in rows]))
