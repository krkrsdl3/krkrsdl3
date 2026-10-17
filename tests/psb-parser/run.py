#!/usr/bin/env python3
"""Compile isolated production parser routines with a bounds-checked stream.

No renderer/game assets required. The stream and numeric variant are test doubles;
parser bodies and the PSB type enum are extracted verbatim from the source tree.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def function(text, signature):
    start = text.index(signature)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    # These selected routines have no braces in string literals/comments.
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2])
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--compat-only', action='store_true')
args = parser.parse_args()
root = args.source
psb = (root / 'plugins/psbfile/PSBFile.cpp').read_text()
emote = (root / 'plugins/emoteplayer/emotefile.cpp').read_text()
header = (root / 'plugins/psbfile/PSBData.h').read_text()
code = (Path(__file__).parent / 'harness.cpp').read_text()
enum = header[header.index('enum PSBObjType'):header.index('};') + 2]
code += '\nnamespace PSB {\n' + enum + '\n'
code += function((root / 'plugins/psbfile/PSBData.cpp').read_text(), 'bool parsePSBArray(') + '\n}\n'
for name, source in [('tTJSNI_PsbFile', psb), ('emotefile', emote)]:
    code += function(source, 'void ' + name + '::refreshListInfo(') + '\n'
    # Exercise the actual scalar cases used by readAllObjs without constructing
    # a TJS object tree. Other object kinds are outside this isolated test.
    body = function(source, 'tTJSVariant ' + name + '::readAllObjs(')
    first = body.index('case PSB::PSBObjType::NumberN0:')
    last_case = body.index('case PSB::PSBObjType::NumberN8:', first)
    last = function(body[last_case:], 'case PSB::PSBObjType::NumberN8:')
    code += 'int64_t ' + name + '::number() { switch(filePtr->ReadI8LE()) {\n'
    code += body[first:last_case] + last + '\ndefault: throw std::runtime_error("not number"); }}\n'
    # Extract the v1 name-loading loop, retaining the production loop bound.
    begin = source.index('        namesCache.reserve(nameIndexes.size());')
    loop = source.index('        for (', begin)
    code += 'void ' + name + '::names() {\n' + source[begin:loop]
    code += function(source[loop:], 'for (') + '\n}\n'
code += function(emote, 'bool emotefile::parseNumber(') + '\n'
code += function(emote, 'bool emotefile::parseReal(') + '\n'
code += (Path(__file__).parent / 'cases.cpp').read_text()
with tempfile.TemporaryDirectory(prefix='psb-parser-') as tmp:
    cpp, exe = Path(tmp) / 'test.cpp', Path(tmp) / 'test'
    cpp.write_text(code)
    flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie'] if args.sanitize else []
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++17', '-g', '-O1', *flags, str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe), *(['compat'] if args.compat_only else [])], check=True)
