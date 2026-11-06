#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Bounded advisory host/QEMU observations, independent of product verdicts."""
import json
import os
from pathlib import Path
import re
import threading
import time

from workflow_support import identity, now


class ResourceSampler:
    def __init__(self, directory, executable, timeout, *, proc=Path('/proc'), cgroup=Path('/sys/fs/cgroup')):
        if type(timeout) is not int or not 1 <= timeout <= 900:
            raise ValueError('Resource observation requires the existing 1..900s guest budget')
        self.path = Path(directory) / 'host-resources.ndjson'
        self.executable, self.proc, self.cgroup = Path(executable).name, proc, cgroup
        self.limit, self.count, self.limitations = timeout + 2, 0, []
        self.stop = threading.Event()
        self.thread = None
        self.joined = True

    def read(self, path, maximum=4096):
        try:
            with path.open('rb') as stream:
                return stream.read(maximum).decode('utf-8', errors='replace').strip()
        except OSError as error:
            message = str(path) + ': ' + str(error)[:160]
            if message not in self.limitations and len(self.limitations) < 8:
                self.limitations.append(message)
            return None

    def sample(self):
        children = self.read(self.proc / str(os.getpid()) / 'task' / str(os.getpid()) / 'children')
        processes = []
        for number in (children or '').split():
            if not number.isdecimal():
                continue
            base = self.proc / number
            command = self.read(base / 'cmdline')
            if not command or Path(command.split('\0')[0]).name != self.executable:
                continue
            raw = self.read(base / 'stat')
            fields = raw.rsplit(')', 1)[-1].split() if raw else []
            if len(fields) < 22:
                continue
            try:
                if int(fields[1]) != os.getpid():
                    continue
                processes.append({'pid': int(number), 'parent_pid': int(fields[1]), 'state': fields[0],
                    'user_ticks': int(fields[11]), 'system_ticks': int(fields[12]), 'threads': int(fields[17]),
                    'start_ticks': int(fields[19]), 'rss_pages': int(fields[21])})
            except ValueError:
                if 'Malformed proc process sample' not in self.limitations:
                    self.limitations.append('Malformed proc process sample')
        return {'observed_at': now(), 'monotonic_ns': time.monotonic_ns(),
                'loadavg': self.read(self.proc / 'loadavg', 256), 'meminfo': self.read(self.proc / 'meminfo', 16384),
                'cgroup_cpu_stat': self.read(self.cgroup / 'cpu.stat'),
                'cgroup_memory_current': self.read(self.cgroup / 'memory.current', 256), 'qemu_processes': processes}

    def observe(self):
        try:
            with self.path.open('x', encoding='utf-8') as stream:
                for _ in range(self.limit):
                    stream.write(json.dumps(self.sample()) + '\n')
                    stream.flush()
                    self.count += 1
                    if self.stop.wait(1):
                        break
        except (OSError, ValueError) as error:
            self.limitations.append(str(error)[:200])

    def __enter__(self):
        try:
            self.thread = threading.Thread(target=self.observe, name='guest-resource-observer', daemon=True)
            self.thread.start()
        except RuntimeError as error:
            self.thread = None
            self.limitations.append(str(error)[:200])
        return self

    def __exit__(self, *unused):
        self.stop.set()
        if self.thread:
            self.thread.join(2)
            self.joined = not self.thread.is_alive()

    def summary(self):
        log = None
        try:
            if self.joined and self.path.is_file():
                log = identity(self.path)
        except OSError as error:
            self.limitations.append(str(error)[:200])
        return {'scope': 'diagnostic-only', 'evidence_verified': False, 'samples': self.count,
                'sample_limit': self.limit, 'interval_seconds': 1, 'observer_joined': self.joined,
                'log': log,
                'limitations': self.limitations[:8], 'attribution': 'Unknown; resource observations do not establish root cause'}


def kernel_observation(paths):
    lockups, last_case = [], None
    for path in paths:
        try:
            with Path(path).open('rb') as stream:
                stream.seek(0, 2)
                stream.seek(max(0, stream.tell() - 1024**2))
                lines = stream.read(1024**2).decode('utf-8', errors='replace').splitlines()
        except OSError:
            continue
        for line in lines:
            if 'soft lockup' in line and len(lockups) < 3:
                lockups.append(line[:300])
            match = re.search(r'QAPLET_[0-9a-f]+ CASE ([A-Za-z0-9._-]+) START ', line)
            if match:
                last_case = match[1]
    return {'scope': 'diagnostic-only', 'soft_lockup_observations': lockups,
            'last_case_started': last_case, 'attribution': 'Unknown', 'evidence_verified': False}
