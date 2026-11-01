#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Independent Windows safety fence for one supplied application probe.

The fence never assigns this Python runner or opens its existing Job. The new
artifact process starts suspended and is verified in an owned kill-on-close
Job before any fixture instruction executes.
"""
import ctypes
from ctypes import wintypes as W
import msvcrt
import os
from pathlib import Path
import subprocess
import time

from foundation import now


class Basic(ctypes.Structure):
    _fields_ = [("process_time", ctypes.c_int64), ("job_time", ctypes.c_int64), ("flags", W.DWORD),
                ("min_ws", ctypes.c_size_t), ("max_ws", ctypes.c_size_t), ("active", W.DWORD),
                ("affinity", ctypes.c_size_t), ("priority", W.DWORD), ("scheduling", W.DWORD)]


class Extended(ctypes.Structure):
    _fields_ = [("basic", Basic), ("io", ctypes.c_uint64 * 6), ("memory", ctypes.c_size_t * 4)]


class Accounting(ctypes.Structure):
    _fields_ = [("times", ctypes.c_int64 * 4), ("faults", W.DWORD), ("total", W.DWORD),
                ("active", W.DWORD), ("terminated", W.DWORD)]


class Startup(ctypes.Structure):
    _fields_ = [("cb", W.DWORD), ("reserved", W.LPWSTR), ("desktop", W.LPWSTR), ("title", W.LPWSTR),
                ("x", W.DWORD), ("y", W.DWORD), ("width", W.DWORD), ("height", W.DWORD),
                ("chars_x", W.DWORD), ("chars_y", W.DWORD), ("fill", W.DWORD), ("flags", W.DWORD),
                ("show", W.WORD), ("reserved_bytes", W.WORD), ("reserved_ptr", ctypes.c_void_p),
                ("stdin", W.HANDLE), ("stdout", W.HANDLE), ("stderr", W.HANDLE)]


class Process(ctypes.Structure):
    _fields_ = [("process", W.HANDLE), ("thread", W.HANDLE), ("pid", W.DWORD), ("tid", W.DWORD)]


def launch(argv, directory, timeout):
    k = ctypes.WinDLL("kernel32", use_last_error=True)
    signatures = {
        "CreateJobObjectW": (W.HANDLE, [ctypes.c_void_p, W.LPCWSTR]),
        "SetInformationJobObject": (W.BOOL, [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD]),
        "QueryInformationJobObject": (W.BOOL, [W.HANDLE, ctypes.c_int, ctypes.c_void_p, W.DWORD, ctypes.c_void_p]),
        "AssignProcessToJobObject": (W.BOOL, [W.HANDLE, W.HANDLE]),
        "IsProcessInJob": (W.BOOL, [W.HANDLE, W.HANDLE, ctypes.POINTER(W.BOOL)]),
        "CreateProcessW": (W.BOOL, [W.LPCWSTR, W.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, W.BOOL,
                                    W.DWORD, ctypes.c_void_p, W.LPCWSTR, ctypes.POINTER(Startup), ctypes.POINTER(Process)]),
        "ResumeThread": (W.DWORD, [W.HANDLE]), "WaitForSingleObject": (W.DWORD, [W.HANDLE, W.DWORD]),
        "GetExitCodeProcess": (W.BOOL, [W.HANDLE, ctypes.POINTER(W.DWORD)]),
        "TerminateJobObject": (W.BOOL, [W.HANDLE, W.UINT]),
        "TerminateProcess": (W.BOOL, [W.HANDLE, W.UINT]),
        "CloseHandle": (W.BOOL, [W.HANDLE]),
        "GetHandleInformation": (W.BOOL, [W.HANDLE, ctypes.POINTER(W.DWORD)]),
    }
    for name, (restype, argtypes) in signatures.items():
        function = getattr(k, name)
        function.restype, function.argtypes = restype, argtypes
    if ctypes.sizeof(Extended) != 144 or ctypes.sizeof(Startup) != 104 or ctypes.sizeof(Accounting) != 48:
        raise ValueError("Win64 launcher layout mismatch")
    stdout_path, stderr_path = directory / "suite.stdout.log", directory / "suite.stderr.log"
    command = {"argv": list(map(str, argv)), "cwd": str(directory), "started_at": now(),
               "timeout_seconds": timeout, "timed_out": False, "exit_code": None,
               "stdout_log": str(stdout_path), "stderr_log": str(stderr_path)}
    facts = {"runner_in_job": None, "runner_limits": None, "runner_query_error": 0,
             "job_created": False, "kill_on_close_set": False, "inheritable": None,
             "created_suspended": False, "assigned": False, "verified_before_resume": False,
             "resumed": False, "errors": [], "cleanup": {}}
    job, process = None, Process()
    def require(ok, operation):
        if not ok:
            error = ctypes.get_last_error()
            facts["errors"].append({"api": operation, "winerror": error})
            raise OSError(error, operation)
    runner_member = W.BOOL()
    require(k.IsProcessInJob(W.HANDLE(-1), None, ctypes.byref(runner_member)), "IsProcessInJob(runner)")
    facts["runner_in_job"] = bool(runner_member.value)
    if runner_member.value:
        limits = Extended()
        if k.QueryInformationJobObject(None, 9, ctypes.byref(limits), ctypes.sizeof(limits), None):
            facts["runner_limits"] = limits.basic.flags
        else:
            facts["runner_query_error"] = ctypes.get_last_error()
    with stdout_path.open("xb") as out, stderr_path.open("xb") as err, open(os.devnull, "rb") as inp:
        try:
            job = k.CreateJobObjectW(None, None)
            require(job, "CreateJobObjectW(supervisor)"); facts["job_created"] = True
            limits = Extended(); limits.basic.flags = 0x2000
            require(k.SetInformationJobObject(job, 9, ctypes.byref(limits), ctypes.sizeof(limits)), "SetInformationJobObject(supervisor)")
            facts["kill_on_close_set"] = True
            flags = W.DWORD()
            require(k.GetHandleInformation(job, ctypes.byref(flags)), "GetHandleInformation(supervisor)")
            facts["inheritable"] = bool(flags.value & 1)
            if facts["inheritable"]:
                raise ValueError("Supervisor Job must not be inheritable")
            startup = Startup(); startup.cb = ctypes.sizeof(startup); startup.flags = 0x100
            startup.stdin, startup.stdout, startup.stderr = [msvcrt.get_osfhandle(file.fileno()) for file in (inp, out, err)]
            for file in (inp, out, err): os.set_inheritable(file.fileno(), True)
            try:
                require(k.CreateProcessW(str(argv[0]), ctypes.create_unicode_buffer(subprocess.list2cmdline(list(map(str, argv)))),
                        None, None, True, 4, None, str(directory), ctypes.byref(startup), ctypes.byref(process)), "CreateProcessW(suspended probe)")
            finally:
                for file in (inp, out, err): os.set_inheritable(file.fileno(), False)
            facts.update(created_suspended=True, pid=process.pid)
            inherited = W.BOOL()
            require(k.IsProcessInJob(process.process, None, ctypes.byref(inherited)), "IsProcessInJob(suspended artifact before assignment)")
            facts["artifact_in_job_before_assignment"] = bool(inherited.value)
            require(k.AssignProcessToJobObject(job, process.process), "AssignProcessToJobObject(supervisor)")
            facts["assigned"] = True
            member = W.BOOL()
            require(k.IsProcessInJob(process.process, job, ctypes.byref(member)), "IsProcessInJob(supervisor)")
            require(member.value, "Verified supervisor membership")
            facts["verified_before_resume"] = True
            require(k.ResumeThread(process.thread) == 1, "ResumeThread(probe)"); facts["resumed"] = True
            waited = k.WaitForSingleObject(process.process, timeout * 1000)
            command["timed_out"] = waited == 258
            require(waited in (0, 258), "WaitForSingleObject(probe)")
        except (OSError, ValueError) as error:
            facts["error"] = str(error)
        finally:
            # Terminate only the new fence, never the runner's pre-existing Job.
            if job:
                facts["cleanup"]["terminate_owned_job"] = bool(k.TerminateJobObject(job, 98))
            if process.process:
                if k.WaitForSingleObject(process.process, 0) == 258:
                    facts["cleanup"]["direct_fallback"] = bool(k.TerminateProcess(process.process, 97))
                facts["cleanup"]["probe_reaped"] = k.WaitForSingleObject(process.process, 3000) == 0
                code = W.DWORD()
                if k.GetExitCodeProcess(process.process, ctypes.byref(code)):
                    command["exit_code"] = code.value
            if job:
                account = Accounting()
                for _ in range(100):
                    if not k.QueryInformationJobObject(job, 1, ctypes.byref(account), ctypes.sizeof(account), None): break
                    if account.active == 0: break
                    time.sleep(0.01)
                facts["cleanup"].update(accounting_query=bool(k.QueryInformationJobObject(job, 1, ctypes.byref(account), ctypes.sizeof(account), None)),
                                        active_processes=account.active, total_processes=account.total)
            for label, handle in (("thread", process.thread), ("process", process.process), ("job", job)):
                facts["cleanup"][label + "_closed"] = not handle or bool(k.CloseHandle(handle))
    command["finished_at"] = now()
    command["cleanup"] = facts["cleanup"]
    return command, stdout_path.read_text(encoding="utf-8", errors="replace"), stderr_path.read_text(encoding="utf-8", errors="replace"), facts
