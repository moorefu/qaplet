#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""M2 private ConPTY driver, derived from native_io_terminal's owned lifecycle.

The M0 recipe is unchanged. This private extension adds deterministic input and
resize gates and returns raw bytes, never replacement decoding or CSI stripping.
"""
import ctypes
from ctypes import wintypes as W
import subprocess
import threading
import time

from foundation import now


def conpty(argv, directory, timeout, *, resize_order_fixture=False):
    """Bounded explicit CreateProcessW argv; close all handles and reap only our child."""
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    HANDLE = W.HANDLE
    SIZE = ctypes.c_size_t

    class Coord(ctypes.Structure):
        _fields_ = [("x", ctypes.c_short), ("y", ctypes.c_short)]

    class Startup(ctypes.Structure):
        _fields_ = [("cb", W.DWORD), ("reserved", W.LPWSTR), ("desktop", W.LPWSTR), ("title", W.LPWSTR),
                    ("x", W.DWORD), ("y", W.DWORD), ("xs", W.DWORD), ("ys", W.DWORD),
                    ("xc", W.DWORD), ("yc", W.DWORD), ("fill", W.DWORD), ("flags", W.DWORD),
                    ("show", W.WORD), ("reserved2", W.WORD), ("reserved_ptr", ctypes.c_void_p),
                    ("stdin", HANDLE), ("stdout", HANDLE), ("stderr", HANDLE)]

    class StartupEx(ctypes.Structure):
        _fields_ = [("startup", Startup), ("attributes", ctypes.c_void_p)]

    class Process(ctypes.Structure):
        _fields_ = [("process", HANDLE), ("thread", HANDLE), ("pid", W.DWORD), ("tid", W.DWORD)]

    kernel.CreatePipe.argtypes = [ctypes.POINTER(HANDLE), ctypes.POINTER(HANDLE), ctypes.c_void_p, W.DWORD]
    kernel.CreatePseudoConsole.argtypes = [Coord, HANDLE, HANDLE, W.DWORD, ctypes.POINTER(HANDLE)]
    kernel.CreatePseudoConsole.restype = ctypes.c_long
    kernel.ClosePseudoConsole.argtypes = [HANDLE]
    kernel.ResizePseudoConsole.argtypes = [HANDLE, Coord]
    kernel.ResizePseudoConsole.restype = ctypes.c_long
    kernel.WriteFile.argtypes = [HANDLE, ctypes.c_void_p, W.DWORD, ctypes.POINTER(W.DWORD), ctypes.c_void_p]
    kernel.CloseHandle.argtypes = [HANDLE]
    kernel.InitializeProcThreadAttributeList.argtypes = [ctypes.c_void_p, W.DWORD, W.DWORD, ctypes.POINTER(SIZE)]
    kernel.UpdateProcThreadAttribute.argtypes = [ctypes.c_void_p, W.DWORD, SIZE, ctypes.c_void_p, SIZE,
                                                ctypes.c_void_p, ctypes.c_void_p]
    kernel.DeleteProcThreadAttributeList.argtypes = [ctypes.c_void_p]
    kernel.CreateProcessW.argtypes = [W.LPCWSTR, W.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, W.BOOL,
                                      W.DWORD, ctypes.c_void_p, W.LPCWSTR, ctypes.POINTER(StartupEx), ctypes.POINTER(Process)]
    kernel.ReadFile.argtypes = [HANDLE, ctypes.c_void_p, W.DWORD, ctypes.POINTER(W.DWORD), ctypes.c_void_p]
    kernel.WaitForSingleObject.argtypes = [HANDLE, W.DWORD]
    kernel.GetExitCodeProcess.argtypes = [HANDLE, ctypes.POINTER(W.DWORD)]
    kernel.TerminateProcess.argtypes = [HANDLE, W.UINT]
    kernel.CancelSynchronousIo.argtypes = [HANDLE]
    kernel.OpenThread.argtypes = [W.DWORD, W.BOOL, W.DWORD]
    kernel.OpenThread.restype = HANDLE
    started, output, handles, closed, actions = now(), [], [], [], []
    pc, process = HANDLE(), Process()
    attrs = None
    initialized = False
    reader = None
    exit_code, timed_out, failure = None, False, None
    cleanup = {"process_reaped": False, "pseudoconsole_closed": False, "handles_closed": False, "reader_finished": False}

    def check(ok, operation):
        if not ok:
            raise OSError(ctypes.get_last_error(), operation)

    def release(handle):
        if handle:
            closed.append(bool(kernel.CloseHandle(handle)))

    try:
        pin_read, pin_write, pout_read, pout_write = HANDLE(), HANDLE(), HANDLE(), HANDLE()
        check(kernel.CreatePipe(ctypes.byref(pin_read), ctypes.byref(pin_write), None, 0), "CreatePipe(input)")
        handles.extend((pin_read, pin_write))
        check(kernel.CreatePipe(ctypes.byref(pout_read), ctypes.byref(pout_write), None, 0), "CreatePipe(output)")
        handles.extend((pout_read, pout_write))
        hr = kernel.CreatePseudoConsole(Coord(80, 24), pin_read, pout_write, 0, ctypes.byref(pc))
        if hr != 0:
            raise OSError(hr, "CreatePseudoConsole HRESULT")
        size = SIZE()
        kernel.InitializeProcThreadAttributeList(None, 1, 0, ctypes.byref(size))
        attrs = ctypes.create_string_buffer(size.value)
        check(kernel.InitializeProcThreadAttributeList(attrs, 1, 0, ctypes.byref(size)), "InitializeProcThreadAttributeList")
        initialized = True
        check(kernel.UpdateProcThreadAttribute(attrs, 0, 0x20016, pc, ctypes.sizeof(pc), None, None), "UpdateProcThreadAttribute(ConPTY)")
        startup = StartupEx()
        startup.startup.cb = ctypes.sizeof(startup)
        startup.attributes = ctypes.cast(attrs, ctypes.c_void_p)
        # list2cmdline is the documented Windows argv quoting algorithm, never a shell.
        commandline = ctypes.create_unicode_buffer(subprocess.list2cmdline(list(map(str, argv))))

        def drain():
            data, count = ctypes.create_string_buffer(4096), W.DWORD()
            while kernel.ReadFile(pout_read, data, len(data), ctypes.byref(count), None) and count.value:
                output.append(data.raw[:count.value])

        reader = threading.Thread(target=drain, daemon=True)
        reader.start()
        check(kernel.CreateProcessW(str(argv[0]), commandline, None, None, False, 0x80000, None, str(directory),
                                    ctypes.byref(startup), ctypes.byref(process)), "CreateProcessW(ConPTY fixture)")
        deadline = time.monotonic() + timeout
        while kernel.WaitForSingleObject(process.process, 10) == 258 and time.monotonic() < deadline:
            if (directory / "resize.ready").is_file() and not (directory / "resize.done").exists():
                for cols, rows in ((90, 31), (100, 33)):
                    hr = kernel.ResizePseudoConsole(pc, Coord(cols, rows))
                    actions.append({"operation": "ResizePseudoConsole", "cols": cols, "rows": rows, "hresult": hr})
                    if hr != 0:
                        raise OSError(hr, "ResizePseudoConsole HRESULT")
                    if resize_order_fixture and cols == 90:
                        # Controlled system-boundary fixture: the child records
                        # the real intermediate size before the final resize.
                        (directory / "resize.done").write_bytes(b"done")
                        while not (directory / "resize-query.ready").is_file():
                            if kernel.WaitForSingleObject(process.process, 1) != 258 or time.monotonic() >= deadline:
                                raise OSError("Controlled resize query handshake did not complete")
                    if resize_order_fixture and cols == 100:
                        (directory / "resize-query.done").write_bytes(b"done")
                (directory / "resize.done").write_bytes(b"done")
            if (directory / "text.ready").is_file() and not (directory / "text.done").exists():
                for byte in bytes.fromhex("41e4b8ad5a"):
                    chunk, count = ctypes.create_string_buffer(bytes([byte])), W.DWORD()
                    check(kernel.WriteFile(pin_write, chunk, 1, ctypes.byref(count), None), "WriteFile(ConPTY input)")
                    actions.append({"operation": "WriteFile", "byte": byte, "written": count.value})
                    if count.value != 1:
                        raise OSError("Short ConPTY input write")
                (directory / "text.done").write_bytes(b"done")
            for name, payload in (("invalid", "c0afeda08051"), ("supplementary", "f09f998251")):
                if (directory / (name + ".ready")).is_file() and not (directory / (name + ".done")).exists():
                    for byte in bytes.fromhex(payload):
                        chunk, count = ctypes.create_string_buffer(bytes([byte])), W.DWORD()
                        check(kernel.WriteFile(pin_write, chunk, 1, ctypes.byref(count), None), "WriteFile(ConPTY boundary input)")
                        actions.append({"operation": "WriteFile", "phase": name, "byte": byte, "written": count.value})
                        if count.value != 1:
                            raise OSError("Short ConPTY boundary input write")
                    (directory / (name + ".done")).write_bytes(b"done")
        if kernel.WaitForSingleObject(process.process, 0) == 258:
            timed_out = True
            check(kernel.TerminateProcess(process.process, 124), "TerminateProcess(controlled fixture)")
        cleanup["process_reaped"] = kernel.WaitForSingleObject(process.process, 5000) == 0
        code = W.DWORD()
        check(kernel.GetExitCodeProcess(process.process, ctypes.byref(code)), "GetExitCodeProcess")
        exit_code = code.value
    except (OSError, AttributeError) as error:
        failure = str(error)
    finally:
        if process.process and not cleanup["process_reaped"]:
            kernel.TerminateProcess(process.process, 124)
            cleanup["process_reaped"] = kernel.WaitForSingleObject(process.process, 5000) == 0
        if pc:
            def close_console():
                kernel.ClosePseudoConsole(pc)
                cleanup["pseudoconsole_closed"] = True
            closer = threading.Thread(target=close_console, daemon=True)
            closer.start()
            closer.join(5)
            if closer.is_alive():
                failure = failure or "ClosePseudoConsole exceeded cleanup deadline"
        if initialized:
            kernel.DeleteProcThreadAttributeList(attrs)
        # The writer must close before the drain thread can observe EOF.
        for handle in handles[0:2] + handles[3:4]:
            release(handle)
        if reader:
            reader.join(5)
            if reader.is_alive():
                thread_handle = kernel.OpenThread(0x1, False, reader.native_id)
                if thread_handle:
                    kernel.CancelSynchronousIo(thread_handle)
                    release(thread_handle)
                reader.join(2)
            cleanup["reader_finished"] = not reader.is_alive()
        if len(handles) > 2:
            release(handles[2])
        release(process.thread)
        release(process.process)
        cleanup["process_reaped"] = cleanup["process_reaped"] or not process.process
        cleanup["pseudoconsole_closed"] = cleanup["pseudoconsole_closed"] or not pc
        cleanup["reader_finished"] = cleanup["reader_finished"] or reader is None
        cleanup["handles_closed"] = all(closed)
    stdout = b"".join(output)
    (directory / "conpty.stdout.log").write_bytes(stdout)
    (directory / "conpty.stderr.log").write_bytes((failure or "").encode())
    record = {"argv": list(map(str, argv)), "cwd": str(directory), "started_at": started, "finished_at": now(),
              "exit_code": exit_code, "timed_out": timed_out, "timeout_seconds": timeout,
              "stdout_log": str(directory / "conpty.stdout.log"), "stderr_log": str(directory / "conpty.stderr.log"),
              "cleanup": cleanup, "actions": actions, "resize_order_fixture": resize_order_fixture,
              "api": "CreatePseudoConsole + CreateProcessW + ResizePseudoConsole (private fixture, no shell)", "error": failure}
    return record, stdout
