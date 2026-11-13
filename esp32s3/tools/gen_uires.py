#!/usr/bin/env python3
"""gen_uires.py -- turn SOUI resource directories into a C++ table.

Why: the desktop/Cortex-M3 builds go uires -> windres -> ".section .rdata",
which the ESP-IDF linker scripts do not collect.  For the ESP32-S3 bring-up we
embed the same files as plain string literals instead and serve them through a
MemResProvider (main/mem_res_provider.cpp).

Usage:
    python tools/gen_uires.py --app-uires <dir> --sys-res <dir> --out <file.cpp>

<dir> for --app-uires is a wizard-style uires directory (uires.idx + files);
<dir> for --sys-res is a soui-sys-resource/theme_sys_res directory (the SVG
variant, because render-gdi/cairo renders SVG through nanosvg and no image
decoder is linked).

The generated tables are (type, name, data, len) quadruples, exactly what
IResProvider::GetRawBuffer(type, name) is called with by SOUI:

    _LoadXmlDocment(L"SYS_XML_SKIN", L"XML", ...)   -> ("XML",  "SYS_XML_SKIN")
    AddResProvider(appRes, L"UIDEF:XML_INIT")       -> ("UIDEF","XML_INIT")
    <string src="values:string"/>                   -> ("values","string")
    CreateSvgFromResId(L"SVG:SYS_WND_BKGND")        -> ("SVG",  "SYS_WND_BKGND")
"""

import argparse
import io
import os
import re
import sys
import xml.etree.ElementTree as ET

# theme_sys_res/<file> -> resource id SOUI asks for.  Anything not listed falls
# back to the upper-cased stem (sys_wnd_bkgnd.svg -> SYS_WND_BKGND).
SYS_XML_MAP = {
    "sys_xml_skin.xml": "SYS_XML_SKIN",
    "sys_xml_editmenu.xml": "SYS_XML_EDITMENU",
    "sys_xml_msgbox.xml": "SYS_XML_MSGBOX",
    "theme_color.xml": "THEME_COLOR",
}


def c_escape(data):
    """One C string literal worth of \\xHH escapes, wrapped at 32 bytes."""
    lines = []
    for i in range(0, len(data), 32):
        chunk = data[i:i + 32]
        lines.append('"' + "".join("\\x%02x" % b for b in chunk) + '"')
    return "\n".join(lines)


def read(path):
    with open(path, "rb") as f:
        return f.read()


# Win32/SOUI "draw this window into an offscreen cache, then AlphaBlend the
# whole window" attribute.  On the ESP32 it is a net loss: every invalidation
# of a cached window (the animated boxes are children of a cache="1" root)
# makes _PaintClient() clear and repaint the FULL window into the cache surface
# and then blend all 320x240 pixels again -- measured at ~160ms per repaint on
# this board versus ~15ms when the paint goes straight into the dirty rect.
# Desktop renders in video memory, so there the cache pays off; here it does not.
CACHE_ATTR = re.compile(rb'\bcache\s*=\s*"1"')

def strip_window_cache(data):
    return CACHE_ATTR.sub(b'cache="0"', data)


def walk_files(root):
    for base, _dirs, files in os.walk(root):
        for name in sorted(files):
            yield os.path.join(base, name)


def app_entries(uires_dir):
    """Parse uires.idx so the (type, name) pairs match what the app asks for."""
    idx = os.path.join(uires_dir, "uires.idx")
    out = []
    tree = ET.parse(idx)
    for group in tree.getroot():
        rtype = group.tag
        for f in group:
            name = f.get("name")
            rel = f.get("path").replace("\\", "/")
            out.append((rtype, name, os.path.join(uires_dir, rel)))
    return out


def sys_entries(sys_dir):
    out = []
    for path in walk_files(sys_dir):
        fname = os.path.basename(path)
        ext = os.path.splitext(fname)[1].lower()
        if ext == ".svg":
            out.append(("SVG", os.path.splitext(fname)[0].upper(), path))
        elif ext == ".xml":
            rid = SYS_XML_MAP.get(fname)
            if rid is None:
                print("  skip (unmapped xml): %s" % fname)
                continue
            out.append(("XML", rid, path))
        elif ext == ".png":
            out.append(("PNG", os.path.splitext(fname)[0].upper(), path))
        else:
            print("  skip (unknown ext): %s" % fname)
    return out


def emit(fh, table_name, entries, blobs):
    fh.write("const SouiResEntry %s[] = {\n" % table_name)
    for i, (rtype, name, _path) in enumerate(entries):
        fh.write('    {"%s", "%s", k_res_%d, (unsigned int)sizeof(k_res_%d) - 1},\n'
                 % (rtype, name, blobs[i], blobs[i]))
    fh.write("};\n")
    fh.write("const int %s_count = (int)(sizeof(%s) / sizeof(%s[0]));\n\n"
             % (table_name, table_name, table_name))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app-uires", required=True)
    ap.add_argument("--sys-res", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--strip-window-cache", action="store_true",
                    help='rewrite cache="1" to cache="0" in app XML layouts '
                         '(see CACHE_ATTR: the offscreen window cache costs a '
                         'full-window repaint per frame on this target)')
    args = ap.parse_args()

    app = app_entries(args.app_uires)
    sysres = sys_entries(args.sys_res)

    fh = io.open(args.out, "w", encoding="utf-8", newline="\n")
    fh.write("// GENERATED by tools/gen_uires.py -- do not edit.\n")
    fh.write("//\n")
    fh.write("// app uires:  %s\n" % os.path.normpath(args.app_uires))
    fh.write("// system res: %s\n" % os.path.normpath(args.sys_res))
    fh.write("//\n")
    fh.write("// Served through the in-memory IResProvider in mem_res_provider.cpp:\n")
    fh.write("// the ESP-IDF linker does not collect the windres \".section .rdata\"\n")
    fh.write("// blob the desktop/Cortex-M3 builds rely on.\n")
    fh.write('#include "soui_res_table.h"\n\n')

    blobs = []
    for i, (_t, name, path) in enumerate(app + sysres):
        data = read(path)
        if args.strip_window_cache and path.lower().endswith(".xml"):
            patched = strip_window_cache(data)
            if patched != data:
                print("  cache=1 -> 0: %s" % os.path.basename(path))
                data = patched
        fh.write("// %s  (%d bytes)\n" % (name, len(data)))
        fh.write("static const char k_res_%d[] =\n%s;\n\n"
                 % (i, c_escape(data + b"\0")))
        blobs.append(i)

    napp = len(app)
    emit(fh, "g_appResTable", app, blobs[:napp])
    emit(fh, "g_sysResTable", sysres, blobs[napp:])
    fh.close()

    total = sum(os.path.getsize(p) for _t, _n, p in app + sysres)
    print("%s: %d app + %d sys entries, %d bytes of payload"
          % (args.out, len(app), len(sysres), total))
    for t, n, p in app + sysres:
        print("  %-8s %-20s %7d  %s" % (t, n, os.path.getsize(p), os.path.basename(p)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
