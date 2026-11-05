"""Guest CLI observations require their original system boundary records."""
import base64,json
from pathlib import Path
import sys,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"scripts"))
import m2_guest_cli_matrix


class GuestCliEvidenceTests(unittest.TestCase):
    def test_successful_controller_cannot_replace_the_real_signal_trace(self):
        row=next(row for row in m2_guest_cli_matrix.catalog() if row["case_id"]=="m2-cli.signal.SIGINT.queue")
        observed={"case_id":"cli.signal.SIGINT.queue","status":"pass","actual":{"ready":True,"signal":2,"signal_return":0,"signal_errno":0,"exit_code":1,"supervisor_timeout":False,"owner_pid":123,"signals_attempted":1}}
        sidecars={"cli.stdout":b"READY\n","cli.stderr":b"public C Host: EXECUTION_STOPPED\n","queue-result.json":json.dumps({"queue_full":True,"owner_stopped":True,"sdk_destroyed":True,"sdk_unix_dispositions_preserved":True,"user_marks":0,"events_dropped":2}).encode()}
        raw=b"M2G 1\nCWD /tmp/command-m2-cli.signal.SIGINT.queue\nEXIT 0\nSTDOUT "+base64.b64encode(json.dumps(observed).encode())+b"\nSTDERR \n"
        raw+=b"".join(b"SIDE "+name.encode()+b" "+base64.b64encode(data)+b"\n" for name,data in sidecars.items())+b"END\n"
        with self.assertRaisesRegex(ValueError,"signal"):
            m2_guest_cli_matrix.inspect_cli_capture(row,raw)


if __name__=="__main__":unittest.main()
