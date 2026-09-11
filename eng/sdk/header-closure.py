#!/usr/bin/env python3
"""Generate installation rules for public SDK headers and their local includes."""
from pathlib import Path
import re
import sys
root=Path(sys.argv[1]).resolve()
native=root/'experiments/WebScene.NativeEngine.Probe/native'
authoring=root/'src/WebScene.NativeWeb/include'
roots=[authoring,native,native/'graphics']
pending=list(authoring.rglob('*.hpp'))+[native/'webscene/compiled_document.hpp',native/'graphics/native_webgpu_surface.h']
seen=set()
while pending:
    item=pending.pop().resolve()
    if item in seen:continue
    seen.add(item)
    for included in re.findall(r'^\s*#\s*include\s*[<"]([^">]+)[">]',item.read_text(),re.M):
        candidates=[item.parent/included]+[base/included for base in roots]
        match=next((candidate for candidate in candidates if candidate.is_file()),None)
        if match:pending.append(match)
        elif included.startswith(('webscene_','webscene/')):
            raise RuntimeError(f'Unresolved SDK header dependency {included} in {item}')
for item in sorted(seen):
    relative=item.relative_to(authoring if item.is_relative_to(authoring) else native)
    print(f'install(FILES "{item}" DESTINATION "include/{relative.parent.as_posix()}")')
    print(f'set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "{item}")')
