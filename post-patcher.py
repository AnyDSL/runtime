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
    else:
        filename = basename+"."+"hls"

    if os.path.isfile(filename):
        with open(filename) as f:
            for line in f:
                # patch print_pragma
                m = re.match('(\s*)print_pragma\("(.*)"\);', line)
                if m is not None:
                    space, pragma = m.groups()
                    result.append('{0}{1}\n'.format(space, pragma))
                    print("Patching pragma '{0}' in '{1}'".format(pragma, filename))
                    continue

                # patch channel struct
                m = re.match('} struct_channel_(.*);', line)
                if m is not None:
                    channel_decl_name = m.groups()[0].strip()
                    typeline = result[len(result)-1]
                    type_m = re.match('(.*) (.*) *.;', typeline)
                    channel_decl_type = type_m.groups()[0].strip();
                    # clean previous channel declaration
                    if rttype == "opencl":
                        result[len(result)-3] = ''
                    result[len(result)-2] = ''
                    result[len(result)-1] = ''
                    # channel declaration location
                    channel_decl_line = len(result) - 1;
                    continue
                # a dirty hack: last array_type is registered as channel type
                m = re.match('\} array_(.*);', line)
                if m is not None:
                    result.append(line)
                    channel_decl_type = 'array_' + m.groups()[0].strip()
                    # change declaration location
                    result.append("");
                    channel_decl_line = len(result) - 1;
                    continue

                # patch channel declarations and read/write functions
                m = re.match('(.*)struct_channel_(.*) \*(.*) =.*', line)
                #m = re.match('^(__device__|__constant|) struct_channel_(.*) \*(.*) =.*', line)
                if m is not None:
                    channel_name = m.groups()[2]
                    result.append('//dummy channel\n')
                    channel_line[channel_name] = len(result)-1
                    channel_type[channel_name] = 'struct_channel_' + m.groups()[1]
                    continue
                m = re.match('(.*)struct_channel_.*slot.*', line)
                #m = re.match('^(__device__|__constant) struct_channel_.*slot.*', line)
                if m is not None:
                    continue

                m = re.match('(.*) (.*) = read_channel\((.*)\);', line)
                if m is not None:
                    prefix, value, channel_name = m.groups()
                    if rttype == "opencl":
                        result.append(' {0}{1} = read_channel_intel({2});\n'.format(prefix, value, channel_name))
                        continue
                    else:
                        continue

                m = re.match('(.*)write_channel\((.*), (.*)\);', line)
                if m is not None:
                    prefix, channel_name, value = m.groups()
                    if rttype == "opencl":
                        result.append('{0}write_channel_intel({1}, {2});\n'.format(prefix, channel_name, value))
                        continue
                    else:
                        continue

                # patch to opaque identity functions
                m = re.match('^(.*) = (magic_.*_id)\((.*)\);\n$', line)
                if m is not None:
                    lhs, fname, arg = m.groups()
                    print("Patching magic ID {0} in {1}".format(fname, filename))
                    # emit definition instead
                    result.append('{0} = {1};\n'.format(lhs, arg))
                else:
                    result.append(line)

        # add channel typename
        if channel_decl_type is not None :
            if channel_decl_name is not None :
                result[channel_decl_line] = 'typedef ' + channel_decl_type + ' struct_channel_' + channel_decl_name + ';\n'

        # replace channel placeholder with channel declaration
        for name, line in channel_line.items():
            if rttype == "opencl":
                result[line] = 'channel {0} {1};\n'.format(channel_type[name], name)
            else:
                continue

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
patch_cfiles("hls")
patch_defs("nvvm")
