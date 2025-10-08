#!/usr/bin/env python3
import sys, re, os
basename = sys.argv[1]
def patch_llvmir(rttype):
    # we need to patch
    result = []
    filename = basename+"."+rttype
    if os.path.isfile(filename):
        with open(filename) as f:
            for line in f:
                if rttype=="amdgpu" or rttype=="nvvm" or rttype=="ll":
                    # patch to opaque identity functions
                    m = re.match(r'^declare (.*) @(magic_.*_id)\((.*)\) (?:local_)?unnamed_addr(?: #[0-9]+)?\n$', line)
                    if m is not None:
                        ty1, fname, ty2 = m.groups()
                        assert ty1 == ty2, "Argument and return types of magic IDs must match"
                        print("Patching magic ID {0} in {1}".format(fname, filename))
                        # emit definition instead
                        result.append('define {0} @{1}({0} %name) {{\n'.format(ty1, fname))
                        result.append('  ret {0} %name\n'.format(ty1))
                        result.append('}\n')
                        continue

                    result.append(line)
        # we have the patched thing, write it
        with open(filename, "w") as f:
            for line in result:
                f.write(line)
    return

def patch_cfiles(rttype):
    # we need to patch
    channel_line = {}
    channel_type = {}
    result = []
    channel_decl_name = None
    channel_decl_type = None
    channel_decl_line = 0
    if rttype == "cuda":
        filename = basename+"."+"cu"
    elif rttype == "opencl":
        filename = basename+"."+"cl"
    elif rttype == "hls":
        filename = basename+"."+"hls"

    if os.path.isfile(filename):
        with open(filename) as f:
            for line in f:
                # patch to opaque identity functions
                m = re.match(r'^(.*) = (magic_.*_id)\((.*)\);\n$', line)
                if m is not None:
                    lhs, fname, arg = m.groups()
                    print("Patching magic ID {0} in {1}".format(fname, filename))
                    # emit definition instead
                    result.append('{0} = {1};\n'.format(lhs, arg))
                else:
                    result.append(line)

        # we have the patched thing, write it
        with open(filename, "w") as f:
            for line in result:
                f.write(line)
    return

def patch_defs(rttype):
    nvvm_defs = {
    }

    if rttype == "nvvm":
        result = []
        filename = basename+".nvvm"
        if os.path.isfile(filename):
            with open(filename) as f:
                for line in f:
                    matched = False

                    for (func, code) in iter(nvvm_defs.items()):
                        m = re.match(r'^declare (.*) (@' + func + r')\((.*)\)\n$', line)
                        if m is not None:
                            result.append(code)
                            matched = True
                            break

                    if not matched:
                        result.append(line)

            with open(filename, "w") as f:
                for line in result:
                    f.write(line)
    return

patch_llvmir("ll")
patch_llvmir("amdgpu")
patch_llvmir("nvvm")
patch_cfiles("cuda")
patch_cfiles("opencl")
patch_cfiles("hls")
patch_defs("nvvm")
