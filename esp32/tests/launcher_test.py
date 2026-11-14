"""Test Docker argument construction without touching Docker or a device."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'container-build.sh'

class LauncherTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='turmite esp32 ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.project = self.root / 'repo with spaces'
        self.esp = self.project / 'esp32'
        self.esp.mkdir(parents=True)
        shutil.copyfile(SCRIPT, self.esp / SCRIPT.name)
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        docker = self.bin / 'docker'
        docker.write_text('''#!/usr/bin/env python3
import json, os, sys
with open(os.environ['LOG'], 'a') as log: log.write(json.dumps(sys.argv[1:])+'\\n')
if sys.argv[1] == 'info': print(os.environ.get('SECURITY', '[]'))
sys.exit(int(os.environ.get('DOCKER_STATUS', '0')))
''')
        docker.chmod(0o755)
        self.log = self.root / 'log'
        self.env = dict(os.environ, PATH=str(self.bin)+':'+os.environ['PATH'], LOG=str(self.log))
        self.env.pop('TURMITE_ESP32_PORT', None)
        self.env.pop('TURMITE_IDF_IMAGE', None)

    def run_script(self, *args):
        return subprocess.run(['sh', str(self.esp / SCRIPT.name), *args],
                              env=self.env, capture_output=True, text=True)

    def last_command(self):
        return json.loads(self.log.read_text().splitlines()[-1])

    def test_build_without_device(self):
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        args = self.last_command()
        self.assertEqual(args[-3:], ['espressif/idf:v5.5.2', 'idf.py', 'build'])
        self.assertIn(str(self.project)+':/project:ro,z', args)
        self.assertNotIn('--device', args)
        self.assertNotIn('--privileged', args)

    def test_flash_requires_port(self):
        self.assertNotEqual(self.run_script('flash').returncode, 0)
        self.assertEqual(self.last_command()[0], 'info')

    def test_flash_device_and_argument_boundaries(self):
        self.env['TURMITE_ESP32_PORT'] = '/dev/null'
        self.assertEqual(self.run_script('-D', 'EXAMPLE=two words', 'flash').returncode, 0)
        args = self.last_command()
        self.assertIn('/dev/null:/dev/turmite', args)
        self.assertEqual(args[-6:], ['idf.py', '-p', '/dev/turmite', '-D', 'EXAMPLE=two words', 'flash'])

    def test_rootless(self):
        self.env['SECURITY'] = '["name=rootless"]'
        self.assertEqual(self.run_script().returncode, 0)
        args = self.last_command()
        self.assertEqual(args[args.index('--user')+1], '0:0')

    def test_backup_reads_full_flash(self):
        self.env['TURMITE_ESP32_PORT'] = '/dev/null'
        self.assertEqual(self.run_script('backup').returncode, 0)
        args = self.last_command()
        self.assertIn('read_flash', args)
        self.assertIn('0x1000000', args)
        self.assertTrue(args[-1].startswith('/project/esp32/backups/flash-'))

    def test_no_erase(self):
        self.assertNotEqual(self.run_script('erase-flash').returncode, 0)

    def test_docker_failure_propagates(self):
        self.env['DOCKER_STATUS'] = '7'
        self.assertEqual(self.run_script().returncode, 7)

if __name__ == '__main__':
    unittest.main()
