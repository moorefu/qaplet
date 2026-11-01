#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Bounded worker for one predeclared, uniquely named SCM fixture.

The supervising svc_scm command owns the deadline and invokes cleanup again if
this worker times out. This worker never builds an artifact or changes policies.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

from build_ape import BuildError, identity
from evidence import read_json
from foundation import now, sha256
from nightly_toolchain import write_json


class ServiceStatus(C.Structure):
    _fields_ = [(name, W.DWORD) for name in
                ("type", "state", "accepted", "winerror", "specific", "checkpoint", "wait_hint")]


class ServiceStatusProcess(C.Structure):
    _fields_ = ServiceStatus._fields_ + [("pid", W.DWORD), ("flags", W.DWORD)]


class ServiceConfig(C.Structure):
    _fields_ = [("type", W.DWORD), ("start", W.DWORD), ("error", W.DWORD),
                ("binary", W.LPWSTR), ("group", W.LPWSTR), ("tag", W.DWORD),
                ("dependencies", W.LPWSTR), ("account", W.LPWSTR), ("display", W.LPWSTR)]


class SCM:
    def __init__(self, report, path):
        self.report, self.path = report, path
        self.owned_process = None
        self.advapi = C.WinDLL("advapi32", use_last_error=True)
        self.kernel = C.WinDLL("kernel32", use_last_error=True)
        signatures = {
            "OpenSCManagerW": (W.HANDLE, [W.LPCWSTR, W.LPCWSTR, W.DWORD]),
            "OpenServiceW": (W.HANDLE, [W.HANDLE, W.LPCWSTR, W.DWORD]),
            "CreateServiceW": (W.HANDLE, [W.HANDLE, W.LPCWSTR, W.LPCWSTR, W.DWORD, W.DWORD, W.DWORD,
                W.DWORD, W.LPCWSTR, W.LPCWSTR, C.POINTER(W.DWORD), W.LPCWSTR, W.LPCWSTR, W.LPCWSTR]),
            "StartServiceW": (W.BOOL, [W.HANDLE, W.DWORD, C.POINTER(W.LPCWSTR)]),
            "QueryServiceStatusEx": (W.BOOL, [W.HANDLE, C.c_int, C.c_void_p, W.DWORD, C.POINTER(W.DWORD)]),
            "QueryServiceConfigW": (W.BOOL, [W.HANDLE, C.c_void_p, W.DWORD, C.POINTER(W.DWORD)]),
            "ControlService": (W.BOOL, [W.HANDLE, W.DWORD, C.POINTER(ServiceStatus)]),
            "DeleteService": (W.BOOL, [W.HANDLE]),
            "CloseServiceHandle": (W.BOOL, [W.HANDLE]),
            "OpenProcessToken": (W.BOOL, [W.HANDLE, W.DWORD, C.POINTER(W.HANDLE)]),
            "GetTokenInformation": (W.BOOL, [W.HANDLE, C.c_int, C.c_void_p, W.DWORD, C.POINTER(W.DWORD)]),
        }
        for name, (restype, argtypes) in signatures.items():
            function = getattr(self.advapi, name)
            function.restype, function.argtypes = restype, argtypes
        for name, restype, argtypes in (
            ("GetCurrentProcess", W.HANDLE, []),
            ("OpenProcess", W.HANDLE, [W.DWORD, W.BOOL, W.DWORD]),
            ("QueryFullProcessImageNameW", W.BOOL, [W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD)]),
            ("GetProcessTimes", W.BOOL, [W.HANDLE, C.POINTER(W.FILETIME), C.POINTER(W.FILETIME), C.POINTER(W.FILETIME), C.POINTER(W.FILETIME)]),
            ("WaitForSingleObject", W.DWORD, [W.HANDLE, W.DWORD]),
            ("TerminateProcess", W.BOOL, [W.HANDLE, W.UINT]),
            ("CloseHandle", W.BOOL, [W.HANDLE]),
        ):
            function = getattr(self.kernel, name)
            function.restype, function.argtypes = restype, argtypes

    def save(self):
        self.report["updated_at"] = now()
        write_json(self.path, self.report)

    def call(self, name, *args, allow_error=False, details=None):
        started = now()
        C.set_last_error(0)
        result = getattr(self.advapi, name)(*args)
        error = C.get_last_error() if not result else 0
        self.report["api"].append({"api": name, "started_at": started, "finished_at": now(),
            "return": int(result or 0), "winerror": error, "details": details})
        self.save()
        if not result and not allow_error:
            raise BuildError(name + " failed with Win32 error " + str(error), "blocked" if error == 5 else "fail")
        return result, error

    def query(self, service):
        status, needed = ServiceStatusProcess(), W.DWORD()
        self.call("QueryServiceStatusEx", service, 0, C.byref(status), C.sizeof(status), C.byref(needed))
        result = {name: int(getattr(status, name)) for name, _ in status._fields_}
        self.report["queries"].append({"at": now(), **result})
        self.save()
        return result

    def wait_state(self, service, wanted, seconds):
        deadline = time.monotonic() + seconds
        while True:
            status = self.query(service)
            if status["state"] == wanted:
                return status
            if wanted == 4 and status["state"] == 1:
                raise BuildError("SCM reported STOPPED before RUNNING: " + json.dumps(status))
            if time.monotonic() >= deadline:
                raise BuildError("SCM state transition timeout; wanted " + str(wanted))
            time.sleep(0.1)

    def config(self, service):
        needed = W.DWORD()
        self.call("QueryServiceConfigW", service, None, 0, C.byref(needed), allow_error=True)
        if not 1 <= needed.value <= 65536:
            raise BuildError("Invalid SCM configuration buffer size")
        buffer = C.create_string_buffer(needed.value)
        self.call("QueryServiceConfigW", service, buffer, needed.value, C.byref(needed))
        config = C.cast(buffer, C.POINTER(ServiceConfig)).contents
        result = {"type": config.type, "start": config.start, "error": config.error,
                  "binary": config.binary, "account": config.account}
        self.report["configuration"] = result
        self.save()
        return result

    def token(self):
        token, elevated, size = W.HANDLE(), W.DWORD(), W.DWORD()
        self.call("OpenProcessToken", self.kernel.GetCurrentProcess(), 8, C.byref(token))
        try:
            self.call("GetTokenInformation", token, 20, C.byref(elevated), C.sizeof(elevated), C.byref(size))
            self.report["token"] = {"elevated": bool(elevated.value), "pid": os.getpid()}
        finally:
            self.kernel.CloseHandle(token)
        self.save()

    def bind_process(self, pid, artifact, expected=None):
        if not pid:
            return False
        process = self.kernel.OpenProcess(0x100000 | 0x1000 | 1, False, pid)
        if not process:
            error = C.get_last_error()
            self.report["process_cleanup"].append({"pid": pid, "open_error": error})
            if error == 87:
                return False  # The original PID has already exited.
            raise BuildError("Cannot open the controlled fixture process: " + str(error))
        try:
            creation, exit_time, kernel_time, user_time = W.FILETIME(), W.FILETIME(), W.FILETIME(), W.FILETIME()
            if not self.kernel.GetProcessTimes(process, C.byref(creation), C.byref(exit_time), C.byref(kernel_time), C.byref(user_time)):
                raise BuildError("Cannot measure fixture process creation time")
            created = (creation.dwHighDateTime << 32) | creation.dwLowDateTime
            if expected and (pid != expected["pid"] or created != expected["creation_filetime"]):
                self.report["process_cleanup"].append({"pid": pid, "creation_filetime": created,
                    "expected_creation_filetime": expected["creation_filetime"], "original_exited": True,
                    "termination_attempted": False, "reason": "PID reused; original process has exited"})
                return False
            image, length = C.create_unicode_buffer(32768), W.DWORD(32768)
            if not self.kernel.QueryFullProcessImageNameW(process, 0, image, C.byref(length)):
                raise BuildError("Cannot verify fixture process image before cleanup")
            if os.path.normcase(os.path.abspath(image.value)) != os.path.normcase(str(artifact.resolve())):
                raise BuildError("Fixture PID image differs; refusing process termination")
            self.owned_process = process
            self.report["fixture_process"] = {"pid": pid, "image": image.value, "creation_filetime": created}
            process = None  # Retain the exact kernel process object through STOPPED.
            self.save()
            return True
        finally:
            if process:
                self.kernel.CloseHandle(process)

    def process_exit(self, force):
        process = self.owned_process
        if not process:
            return True
        try:
            waited = self.kernel.WaitForSingleObject(process, 5000)
            killed = False
            if waited == 258 and force:
                killed = bool(self.kernel.TerminateProcess(process, 124))
                waited = self.kernel.WaitForSingleObject(process, 5000)
            self.report["process_cleanup"].append({**self.report["fixture_process"], "forced": killed,
                                                   "wait_result": waited, "binding": "retained process handle + creation FILETIME"})
            self.save()
            return waited == 0
        finally:
            self.kernel.CloseHandle(process)
            self.owned_process = None


def clean(scm, manager, service, plan):
    cleanup = scm.report["cleanup"]
    binding_path = Path(plan["callback_log"]).with_name("service-process.json")
    previous = read_json(binding_path) if binding_path.exists() else None
    if previous and (previous.get("name") != plan["name"] or previous.get("artifact_sha256") != plan["artifact"]["sha256"]):
        raise BuildError("Recovery process identity differs from the exact fixture plan")
    # A recovery worker can open only this UUID-named service and must verify its
    # complete command line before sending a control or deleting it.
    if not service:
        service, error = scm.call("OpenServiceW", manager, plan["name"], 0x10000 | 4 | 32 | 1,
                                  allow_error=True, details={"name": plan["name"], "purpose": "cleanup"})
        if not service:
            exited = error == 1060
            if exited and previous:
                scm.bind_process(previous["pid"], Path(plan["artifact"]["path"]), previous)
                exited = scm.process_exit(force=True)
            cleanup.update(service_removed=error == 1060, process_exited=exited)
            scm.save()
            return
    pid = 0
    try:
        config = scm.config(service)
        if config["binary"] != plan["binary"] or config["type"] != 16 or config["start"] != 3:
            raise BuildError("Service configuration differs from this fixture; refusing cleanup")
        status = scm.query(service)
        pid = status["pid"] or (previous["pid"] if previous else 0)
        if previous and status["pid"] and status["pid"] != previous["pid"]:
            raise BuildError("SCM fixture PID changed after the recorded RUNNING observation")
        if not scm.owned_process and pid:
            scm.bind_process(pid, Path(plan["artifact"]["path"]), previous)
        if status["state"] != 1:
            temporary = ServiceStatus()
            scm.call("ControlService", service, 1, C.byref(temporary), allow_error=True, details={"purpose": "cleanup"})
            try:
                scm.wait_state(service, 1, 5)
            except BuildError:
                pass
        cleanup["process_exited"] = bool(pid) and scm.process_exit(force=True)
        scm.call("DeleteService", service)
        cleanup["delete_requested"] = True
    finally:
        scm.call("CloseServiceHandle", service)
        cleanup["service_handle_closed"] = True
    deadline = time.monotonic() + 5
    while True:
        reopened, error = scm.call("OpenServiceW", manager, plan["name"], 4, allow_error=True,
                                  details={"name": plan["name"], "purpose": "post-delete query"})
        if reopened:
            scm.call("CloseServiceHandle", reopened)
        if not reopened and error == 1060:
            cleanup["service_removed"] = True
            break
        if time.monotonic() >= deadline:
            cleanup["service_removed"] = False
            break
        time.sleep(0.1)
    scm.save()


def execute(plan, report, output, cleanup_only):
    scm = SCM(report, output)
    manager = service = 0
    created = False
    try:
        scm.token()
        connect, _ = scm.call("OpenSCManagerW", None, None, 1, details={"access": "SC_MANAGER_CONNECT"})
        scm.call("CloseServiceHandle", connect)
        manager, _ = scm.call("OpenSCManagerW", None, None, 1 if cleanup_only else 3,
                              details={"access": "SC_MANAGER_CONNECT" if cleanup_only else "SC_MANAGER_CONNECT|CREATE_SERVICE"})
        if cleanup_only:
            clean(scm, manager, 0, plan)
        else:
            service, _ = scm.call("CreateServiceW", manager, plan["name"], plan["name"], 0x10000 | 4 | 16 | 32 | 1,
                16, 3, 1, plan["binary"], None, None, None, None, None,
                details={"name": plan["name"], "binary": plan["binary"], "start": "demand", "account": "LocalSystem"})
            created = True
            report["created"] = True
            initial = scm.query(service)
            if initial["state"] != 1:
                raise BuildError("New demand-start fixture was not STOPPED")
            scm.call("StartServiceW", service, 0, None)
            running = scm.wait_state(service, 4, 8)
            if not running["pid"] or running["winerror"]:
                raise BuildError("SCM RUNNING query did not identify a healthy process")
            report["fixture_pid"] = running["pid"]
            if not scm.bind_process(running["pid"], Path(plan["artifact"]["path"])):
                raise BuildError("Fixture process exited before its RUNNING identity could be bound")
            write_json(Path(plan["callback_log"]).with_name("service-process.json"),
                {**report["fixture_process"], "name": plan["name"], "artifact_sha256": plan["artifact"]["sha256"]})
            scm.save()
            temporary = ServiceStatus()
            scm.call("ControlService", service, 1, C.byref(temporary))
            stopped = scm.wait_state(service, 1, 8)
            if stopped["winerror"] or stopped["specific"]:
                raise BuildError("SCM STOPPED query contains a service error")
            result, error = scm.call("ControlService", service, 1, C.byref(temporary), allow_error=True,
                                     details={"purpose": "repeat stop after SCM STOPPED"})
            if result or error != 1062:
                raise BuildError("Repeated stop did not return ERROR_SERVICE_NOT_ACTIVE (1062)")
            report["repeated_stop"] = {"return": int(result), "winerror": error}
        report.update(status="pass", reason="Actual SCM lifecycle" if not cleanup_only else "Recovery cleanup completed")
    except BuildError as error:
        report.update(status=error.status, reason=str(error))
    except (OSError, ValueError, KeyError, TypeError) as error:
        report.update(status="fail", reason=str(error))
    finally:
        if created:
            try:
                clean(scm, manager, service, plan)
            except (BuildError, OSError, ValueError) as error:
                report.update(status="fail", reason="Cleanup failed: " + str(error))
                report["cleanup"]["error"] = str(error)
        elif not cleanup_only:
            report["cleanup"].update(service_created=False, service_removed=True, process_exited=True,
                                      reason="No service was created")
        if manager:
            try:
                scm.call("CloseServiceHandle", manager)
                report["cleanup"]["manager_handle_closed"] = True
            except BuildError as error:
                report.update(status="fail", reason=str(error))
        if scm.owned_process:
            scm.kernel.CloseHandle(scm.owned_process)
            scm.owned_process = None
        if report["status"] == "pass" and not all(report["cleanup"].get(key) for key in ("service_removed", "process_exited")):
            report.update(status="fail", reason="SCM service/process cleanup was not proved")
        report["finished_at"] = now()
        scm.save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("run", "cleanup"))
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    report = {"schema_version": 1, "report_kind": "svc-scm-worker", "started_at": now(), "status": "not_run",
              "api": [], "queries": [], "process_cleanup": [], "cleanup": {}, "created": False}
    try:
        if sys.platform != "win32":
            raise BuildError("SCM worker requires Windows", "blocked")
        plan = read_json(args.plan)
        if plan.get("schema_version") != 1 or not re.fullmatch(r"qaplet-m0-scm-[0-9a-f]{32}", plan["name"]):
            raise BuildError("Invalid unique SCM fixture plan")
        artifact = Path(plan["artifact"]["path"])
        if sha256(artifact) != plan["artifact"]["sha256"]:
            raise BuildError("SCM fixture artifact changed before worker launch")
        expected = subprocess.list2cmdline([str(artifact.resolve()), "--service", plan["name"], plan["callback_log"]])
        if plan["binary"] != expected:
            raise BuildError("SCM command line differs from the declared fixture")
        execute(plan, report, args.report, args.operation == "cleanup")
    except (BuildError, OSError, ValueError, KeyError, TypeError) as error:
        report.update(status=getattr(error, "status", "fail"), reason=str(error), finished_at=now())
        write_json(args.report, report)
    print(json.dumps({"status": report["status"], "reason": report["reason"], "report": str(args.report)}))
    return {"pass": 0, "fail": 1, "blocked": 2}[report["status"]]


if __name__ == "__main__":
    sys.exit(main())
