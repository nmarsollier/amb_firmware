#!/usr/bin/env python3
"""Enter and verify ROM download mode using the shared flash workflow."""
import sys
from flash import main

if __name__ == "__main__":
    sys.exit(main(["--enter-only", *sys.argv[1:]]))
