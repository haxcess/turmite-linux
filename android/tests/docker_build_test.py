#!/usr/bin/env python3
"""Docker command construction / entrypoint checks; no Docker or SDK required."""
from pathlib import Path
import json, os, shutil, subprocess, tempfile, unittest

ANDROID = Path(__file__).resolve().parents[1]

class ContainerBuildTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='turmite docker test ')
        self.root = Path(self.temp.name)
        self.project = self.root/'project with spaces'
        (self.project/'android').mkdir(parents=True)
        self.launcher = self.project/'android/container-build.sh'
        shutil.copyfile(ANDROID/'container-build.sh', self.launcher)
        self.bin = self.root/'bin'; self.bin.mkdir()
        self.log = self.root/'commands.jsonl'
        self.env = dict(os.environ, PATH=str(self.bin)+os.pathsep+os.environ['PATH'], LOG=str(self.log))
        self.env.pop('TURMITE_DOCKER_IMAGE', None)
        self.fake('docker', """import json,os,sys
with open(os.environ['LOG'],'a') as f: f.write(json.dumps(sys.argv[1:])+'\\n')
if sys.argv[1]=='info':
 print(os.environ.get('SECURITY','[]')); sys.exit(int(os.environ.get('INFO_STATUS','0')))
sys.exit(int(os.environ.get(sys.argv[1].upper()+'_STATUS','0')))
""")

    def tearDown(self): self.temp.cleanup()
    def fake(self, name, body):
        p=self.bin/name
        p.write_text('#!/usr/bin/env python3\n'+body)
        p.chmod(0o755)
    def launch(self, *args):
        return subprocess.run(['sh',str(self.launcher),*args],env=self.env,capture_output=True,text=True)
    def commands(self):
        return [json.loads(x) for x in self.log.read_text().splitlines()]

    def test_default_build_and_mounts(self):
        self.assertEqual(self.launch().returncode,0)
        info,build,run=self.commands()
        self.assertEqual(build[0],'build')
        self.assertIn('linux/amd64',build)
        self.assertEqual(run[-1],'assembleDebug')
        self.assertIn(str(self.project)+':/source:ro,z',run)
        self.assertEqual(run[run.index('--user')+1],f'{os.getuid()}:{os.getgid()}')
        self.assertNotIn('--privileged',run)

    def test_rootless_and_argument_boundaries(self):
        self.env['SECURITY']='["name=rootless"]'
        self.assertEqual(self.launch('assembleDebug','-Pmessage=two words').returncode,0)
        run=self.commands()[-1]
        self.assertEqual(run[run.index('--user')+1],'0:0')
        self.assertEqual(run[-2:],['assembleDebug','-Pmessage=two words'])

    def test_image_only(self):
        self.assertEqual(self.launch('--image-only').returncode,0)
        self.assertEqual([x[0] for x in self.commands()],['info','build'])

    def test_image_failure_stops_run(self):
        self.env['BUILD_STATUS']='7'
        self.assertEqual(self.launch().returncode,7)
        self.assertEqual([x[0] for x in self.commands()],['info','build'])

    def test_daemon_failure(self):
        self.env['INFO_STATUS']='1'
        result=self.launch()
        self.assertEqual(result.returncode,1)
        self.assertIn('Cannot reach Docker',result.stderr)

    def test_gradle_failure_propagates(self):
        self.env['RUN_STATUS']='9'
        self.assertEqual(self.launch().returncode,9)

    def test_entrypoint_exports_failure_reports(self):
        source=self.root/'source'; cache=self.root/'cache'; output=self.root/'output'
        (source/'android').mkdir(parents=True);(source/'src').mkdir();output.mkdir()
        (source/'android/build.sh').write_text("""#!/bin/sh
mkdir -p "$(dirname "$0")/app/build/reports"
printf report > "$(dirname "$0")/app/build/reports/lint.txt"
exit 9
""")
        # Substitute container mount points; real sh/flock execute on the host.
        body=(ANDROID/'container-entrypoint.sh').read_text()
        for old,new in [('/source',source),('/cache',cache),('/output',output)]:
            # Test paths contain spaces, so quote every substituted absolute prefix.
            body=body.replace(old,"'"+str(new)+"'")
        entry=self.root/'entry.sh';entry.write_text(body)
        self.fake('rsync',"""import pathlib,shutil,sys
src,dst=map(pathlib.Path,sys.argv[-2:])
shutil.copytree(src,dst,dirs_exist_ok=True)
""")
        result=subprocess.run(['sh',str(entry)],env=self.env,capture_output=True,text=True)
        self.assertEqual(result.returncode,9,result.stderr)
        self.assertEqual((output/'reports/lint.txt').read_text(),'report')

if __name__=='__main__': unittest.main()
