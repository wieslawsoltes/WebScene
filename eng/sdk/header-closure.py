#!/usr/bin/env python3
"""Generate install rules for actual local includes of public SDK headers."""
from pathlib import Path
import re
import sys

TOKENS = re.compile(
    r'(?P<include>^[ \t]*\#[ \t]*include[ \t]*[<"](?P<path>[^">\r\n]+)[">])'
    r'|(?:u8|u|U|L)?R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delimiter)"'
    r'|/\*.*?\*/|//[^\r\n]*'
    r'|"(?:\\.|[^"\\\r\n])*"'
    r'|(?<![\w])\x27(?:\\.|[^\x27\\\r\n])*\x27',
    re.MULTILINE | re.DOTALL,
)

def includes(text):
    return [m.group('path') for m in TOKENS.finditer(text) if m.group('include')]

def generate(root):
    root=Path(root).resolve()
    native=root/'experiments/WebScene.NativeEngine.Probe/native'
    authoring=root/'src/WebScene.NativeWeb/include'
    roots=[authoring,native,native/'graphics']
    pending=[(path,[]) for path in sorted(authoring.rglob('*.hpp'))+[native/'webscene/compiled_document.hpp',native/'graphics/native_webgpu_surface.h']]
    seen=set()
    while pending:
        item,chain=pending.pop();item=item.resolve()
        if item in seen:continue
        if not any(item.is_relative_to(base) for base in (authoring,native)):
            raise RuntimeError('Public header includes a producer-only source: '+' -> '.join(str(path.relative_to(root)) for path in chain+[item]))
        seen.add(item)
        for included in includes(item.read_text()):
            candidates=[item.parent/included]+[base/included for base in roots]
            match=next((candidate for candidate in candidates if candidate.is_file()),None)
            if match:pending.append((match,chain+[item]))
            elif included.startswith(('webscene_','webscene/')):
                raise RuntimeError(f'Unresolved SDK header dependency {included} in {item}')
    for item in sorted(seen):
        relative=item.relative_to(authoring if item.is_relative_to(authoring) else native)
        print(f'install(FILES "{item}" DESTINATION "include/{relative.parent.as_posix()}")')
        print(f'set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "{item}")')
    json_root=root/'samples/NativeKestrel/third_party/nlohmann'
    print('if(CMAKE_SYSTEM_NAME STREQUAL "Linux")')
    print(f'install(FILES "{json_root}/json.hpp" DESTINATION include/third_party/nlohmann)')
    print(f'install(FILES "{json_root}/LICENSE.MIT" DESTINATION share/licenses/WebScene RENAME nlohmann-LICENSE)')
    print('endif()')
    print(f'WebScene SDK header closure: {len(seen)} files',file=sys.stderr)

if __name__=='__main__':
    generate(sys.argv[1])
