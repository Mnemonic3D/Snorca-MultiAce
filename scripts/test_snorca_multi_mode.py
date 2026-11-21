#!/usr/bin/env python3
"""Offline C++ regression harness. Usage: script.py /absolute/build-directory.
Compiles the production normalizer, matcher and request-body builder without
linking the GUI or connecting to a printer.
"""
import pathlib
import subprocess
import sys
root = pathlib.Path(__file__).resolve().parents[1]
out = pathlib.Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
s = (root / "src/slic3r/Utils/MultiACE.cpp").read_text()
x = s[s.index("namespace {", s.index("// SNORCA: automatic plan")):s.index("// SNORCA: multiACE puts its initial")]
(out / "matcher.inc").write_text(x.replace("MultiACE::snorca_assign_head_targets", "assign"))
x = s[s.index('    json body = {{"token"'):s.index('    BOOST_LOG_TRIVIAL(warning) << "multiACE: print options bed=')]
(out / "request.inc").write_text('json request(const json& rep,const json& st,const std::map<std::string,std::string>& asn,std::string& reason){std::string token="test";bool bed_mesh=false,flow_cal=false;\n' + x + 'return body;}\n')
subprocess.run(["c++", "-std=c++17", "-O0", "-g", "-I" + str(root / "src"), "-I" + str(root / "deps_src"), "-I" + str(out), str(root / "tests/slic3rutils/SnorcaMultiMode_standalone.cpp"), "-o", str(out / "check")], check=True)
subprocess.run([str(out / "check")], check=True)
