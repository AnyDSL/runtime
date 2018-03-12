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
                if rttype=="nvvm":

                    # patch to opaque identity functions
                    m = re.match('^declare (.*) @(magic_.*_id)\((.*)\)\n$', line)
                    if m is not None:
                        ty1, fname, ty2 = m.groups()
                        assert ty1 == ty2, "Argument and return types of magic IDs must match"
                        print("Patching magic ID {0} in {1}".format(fname, filename))
                        # emit definition instead
                        result.append('define {0} @{1}({0} %name) {{\n'.format(ty1, fname))
                        result.append('  ret {0} %name\n'.format(ty1))
                        result.append('}\n')
                        continue

                    # remove source_filename information
                    if 'source_filename = ' in line:
                        print("Removing 'source_filename' in {0}".format(filename))
                        result.append(';{0}'.format(line))
                        continue

                    matched = True
                    while matched:
                        matched = False
                        # patch unnamed_addr and local_unnamed_addr attributes
                        m = re.match('^((declare|define) .* @.*\(.*\)) (unnamed_addr|local_unnamed_addr)(.*)\n$', line)
                        if m is not None:
                            first, decl, attr, last = m.groups()
                            print("Patching '{0}' in {1}".format(attr, filename))
                            line = '{0}{1}\n'.format(first, last)
                            matched = True

                        # patch readonly and writeonly attributes
                        m = re.match('^((declare|define) .*@.*\(.*)(readonly|writeonly)(.*\).*)\n$', line)
                        if m is not None:
                            first, decl, attr, last = m.groups()
                            print("Patching '{0}' in {1}".format(attr, filename))
                            line = '{0}{1}\n'.format(first, last)
                            matched = True

                        # patch speculatable
                        m = re.match('^(attributes .* = {.*) (speculatable)(.*})\n$', line)
                        if m is not None:
                            first, attr, last = m.groups()
                            print("Patching '{0}' in {1}".format(attr, filename))
                            line = '{0}{1}\n'.format(first, last)
                            matched = True

                    result.append(line)
        # we have the patched thing, write it
        with open(filename, "w") as f:
            for line in result:
                f.write(line)
    return

def patch_cfiles(rttype):
    # we need to patch
    result = []
    if rttype == "cuda":
        filename = basename+"."+"cu"
    else:
        filename = basename+"."+"cl"
    if os.path.isfile(filename):
        with open(filename) as f:
            for line in f:
                # patch to opaque identity functions
                m = re.match('^(.*) = (magic_.*_id)\((.*)\);\n$', line)
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
                        m = re.match('^declare (.*) (@' + func + ')\((.*)\)\n$', line)
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

patch_llvmir("nvvm")
patch_cfiles("cuda")
patch_cfiles("opencl")
patch_defs("nvvm")
