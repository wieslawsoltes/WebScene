#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('closure',Path(__file__).with_name('header-closure.py'))
closure=importlib.util.module_from_spec(spec);spec.loader.exec_module(closure)
class Includes(unittest.TestCase):
    def test_real_directives(self):
        self.assertEqual(closure.includes('#include "a.hpp"\n  # include <b.h> // c\n'),['a.hpp','b.h'])
    def test_raw_generated_source(self):
        text='auto x=R"cpp(\n#include "../../../tooling/webscene-uic/main.cpp"\n#include "unterminated\nmore text)cpp";\n#include "real.h"\n'
        self.assertEqual(closure.includes(text),['real.h'])
    def test_comments(self):
        self.assertEqual(closure.includes('/*\n#include "a.h"\n*/\n// #include "b.h"\n#include "real.h"\n'),['real.h'])
    def test_no_multiline_paths(self):
        self.assertEqual(closure.includes('#include "not-a-path\nmore text"\n'),[])
if __name__=='__main__':unittest.main()
