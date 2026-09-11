#!/usr/bin/env python3
"""Configure real installed-package consumers; no replacement SDK declarations."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--package', choices=['WebScene', 'AppScene'], default='WebScene')
    args = parser.parse_args()
    package = args.package
    supported = 'NativeWeb' if package == 'WebScene' else 'Native'
    scenarios = {
        'repeat-required-missing': f'''
find_package({package} REQUIRED CONFIG COMPONENTS {supported})
find_package({package} QUIET CONFIG COMPONENTS Runtime)
if({package}_FOUND OR {package}_Runtime_FOUND)
  message(FATAL_ERROR "A repeated lookup falsely accepted Runtime")
endif()
find_package({package} REQUIRED CONFIG COMPONENTS {supported})
if(NOT {package}_FOUND OR NOT {package}_{supported}_FOUND)
  message(FATAL_ERROR "A rejected lookup poisoned a later supported lookup")
endif()
''',
        'optional-first': f'''
find_package({package} REQUIRED CONFIG OPTIONAL_COMPONENTS Runtime)
if(NOT {package}_FOUND OR {package}_Runtime_FOUND)
  message(FATAL_ERROR "Optional unavailable Runtime rejected the native SDK")
endif()
find_package({package} REQUIRED CONFIG COMPONENTS {supported})
if(NOT {package}_{supported}_FOUND)
  message(FATAL_ERROR "Supported component not reported after optional lookup")
endif()
''',
        'repeat-optional-missing': f'''
find_package({package} REQUIRED CONFIG COMPONENTS {supported})
find_package({package} REQUIRED CONFIG OPTIONAL_COMPONENTS Runtime)
if(NOT {package}_FOUND OR {package}_Runtime_FOUND)
  message(FATAL_ERROR "Repeated optional component query is incorrect")
endif()
''',
        'required-missing-fails': f'''
find_package({package} REQUIRED CONFIG COMPONENTS {supported})
find_package({package} REQUIRED CONFIG COMPONENTS Runtime)
''',
    }
    with tempfile.TemporaryDirectory(prefix='sdk-component-contract-') as temporary:
        root = Path(temporary)
        for name, body in scenarios.items():
            source = root / name
            source.mkdir()
            (source / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.28)\n'
                'project(InstalledPackageComponents LANGUAGES CXX)\n' + body)
            command = ['cmake', '-G', 'Ninja', '-S', str(source), '-B', str(source / 'build'),
                       '-DCMAKE_CXX_COMPILER=' + args.compiler,
                       '-DCMAKE_PREFIX_PATH=' + str(args.sdk.resolve())]
            result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            if name == 'required-missing-fails':
                if result.returncode == 0 or 'Runtime' not in result.stdout or 'not' not in result.stdout:
                    raise RuntimeError('Required missing component did not fail as expected:\n' + result.stdout)
            elif result.returncode:
                raise RuntimeError(name + '\n' + result.stdout)
            print(package + ': ' + name + ' passed')


if __name__ == '__main__':
    main()
