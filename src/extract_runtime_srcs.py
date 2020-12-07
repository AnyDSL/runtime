#!/usr/bin/env python3
import sys

def main():
    col, maxcols = 0, 10
    sys.stdout.write("extern const char runtime_srcs[];\n");
    sys.stdout.write("const char runtime_srcs[] = {");
    for f in sys.argv[1:]:
        with open(f, "r") as fd:
            for b in fd.read():
                sys.stdout.write("{:3}, ".format(ord(b)))
                col += 1
                if col == maxcols:
                    sys.stdout.write("\n")
                    col = 0
    sys.stdout.write("0\n};");

if __name__ == "__main__":
    main()
