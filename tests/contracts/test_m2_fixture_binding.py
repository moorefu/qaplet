"""A current SDK binding must include the actual transitive public C inputs."""
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"scripts"))
from build_ape import identity
from core_gate import Evidence
import m2_fixture_binding


class FixtureBindingTests(unittest.TestCase):
    def test_link_must_use_only_the_bound_objects_archives_and_exact_flags(self):
        compiler="/locked/bin/cosmocc";objects=["/build/a.o"];archive="/public/sdk/lib/libqaplet.a"
        valid=[compiler,"-Os","-Wl,-z,max-page-size=65536","-Wl,-Map=%.map",*objects,archive,"-lm","-lpthread","-o","/build/fixture.com"]
        for extra in ("/build/unknown.o","/other/unbound.a","-T/other/override.ld","-Wl,--version-script=/other/symbols"):
            with self.subTest(extra=extra),self.assertRaisesRegex(ValueError,"link"):
                m2_fixture_binding.verify_link_inputs(valid[:-2]+[extra]+valid[-2:],compiler,objects,archive,())
        m2_fixture_binding.verify_link_inputs(valid,compiler,objects,archive,())

    def test_same_main_c_cannot_hide_an_old_transitive_tty_fixture(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            (root/"tty-faults.c").write_text('#include "host-tty.c"\nint main(void){return check();}\n', encoding="utf-8")
            (root/"host-tty-old.c").write_text('int check(void){return 0;}\n', encoding="utf-8")
            (root/"host-tty-new.c").write_text('int check(void){return physical_join();}\n', encoding="utf-8")
            actual={"sdk/tty-faults.c":identity(root/"tty-faults.c"),"sdk/host-tty.c":identity(root/"host-tty-old.c")}
            target={"sdk/tty-faults.c":identity(root/"tty-faults.c"),"sdk/host-tty.c":identity(root/"host-tty-new.c")}
            with self.assertRaisesRegex(ValueError,"host-tty.c"):
                m2_fixture_binding.verify_used_sources(Evidence({},root),actual,target,["sdk/tty-faults.c"])


if __name__=="__main__":unittest.main()
