#!/usr/bin/env python3
"""Regression tests for GNU avr-size output consumed by the build hook."""
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path
import runpy
import subprocess
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

CHECKER = Path(__file__).with_name('check-avr-size.py')


class SizeCheckTests(unittest.TestCase):
    def run_checker(self, filename, rows):
        output = f'{filename}  :\nsection  size  addr\n{rows}\nTotal  38696\n'
        captured = StringIO()
        with patch.object(sys, 'argv', [str(CHECKER), filename, 'avr-size']), \
                patch('subprocess.run', return_value=SimpleNamespace(stdout=output)) as tool, \
                redirect_stdout(captured):
            runpy.run_path(str(CHECKER), run_name='__main__')
        self.assertEqual(tool.call_args.args[0], ['avr-size', '-A', '-d', filename])
        self.assertTrue(tool.call_args.kwargs['check'])
        return captured.getvalue()

    def test_platformio_relative_filename_is_not_a_section(self):
        result = self.run_checker('.pio/build/rf2000_grbl/firmware.elf',
                                  '.data 50 8389120\n.text 35176 0\n.bss 3470 8389170')
        self.assertIn('Flash 35226/253952 bytes; static SRAM 3520/8192 bytes', result)

    def test_paths_with_spaces_and_windows_separators(self):
        for filename in ('/Users/example/My Project/firmware.elf',
                         '.pio/build/my project/firmware.elf',
                         r'.pio\build\rf2000_grbl\firmware.elf'):
            with self.subTest(filename=filename):
                self.assertIn('Flash 10/', self.run_checker(filename, '.text 10 0\n.data 0 100'))

    def test_noinit_counts_toward_sram(self):
        result = self.run_checker('firmware.elf', '.text 20 0\n.data 2 100\n.bss 3 102\n.noinit 4 105')
        self.assertIn('static SRAM 9/', result)

    def test_exact_capacity_is_accepted(self):
        result = self.run_checker('firmware.elf', '.text 253950 0\n.data 2 100\n.bss 8190 102')
        self.assertIn('Flash 253952/253952 bytes; static SRAM 8192/8192 bytes', result)

    def test_flash_overflow_is_rejected(self):
        with self.assertRaisesRegex(AssertionError, 'bootloader'):
            self.run_checker('firmware.elf', '.text 253953 0\n.data 0 100')

    def test_initialized_data_can_overflow_sram(self):
        with self.assertRaisesRegex(AssertionError, 'SRAM'):
            self.run_checker('firmware.elf', '.text 20 0\n.data 1 100\n.bss 8192 101')

    def test_missing_text_is_rejected(self):
        with self.assertRaisesRegex(AssertionError, 'missing'):
            self.run_checker('.pio/build/firmware.elf', '.debug_info 200 0')

    def test_tool_error_is_preserved(self):
        with patch.object(sys, 'argv', [str(CHECKER), 'firmware.elf', 'avr-size']), \
                patch('subprocess.run', side_effect=subprocess.CalledProcessError(1, 'avr-size')):
            with self.assertRaises(subprocess.CalledProcessError):
                runpy.run_path(str(CHECKER), run_name='__main__')


if __name__ == '__main__':
    unittest.main()
