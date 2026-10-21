#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
import subprocess, sys
outputs = []
for binary in sys.argv[1:]:
    result = subprocess.run([binary], text=True, capture_output=True, check=True)
    print(result.stdout.strip())
    outputs.append(result.stdout)
if len(outputs) != 2 or outputs[0] != outputs[1]:
    raise SystemExit('wheel idle shortcut changed audio, rotor clocks or restart state')
print('wheel-idle: original and optimized restart/rotor/audio comparison PASS')
