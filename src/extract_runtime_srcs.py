#!/usr/bin/env python3
import sys

def main():
    col, maxcols = 0, 10
    for f in sys.argv[1:]:
        with open(f, "r") as fd:
            for b in fd.read():
                sys.stdout.write("{:3}, ".format(ord(b)))
                col += 1
                if col == maxcols:
                    sys.stdout.write("\n")
                    col = 0

if __name__ == "__main__":
    main()
