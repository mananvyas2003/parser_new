Circuit Synthesis Compiler — Debug / Share Build
================================================

HOW TO RUN (your friend)
------------------------
1. Double-click:  CircuitSynth_Debug.html
2. It opens in Chrome / Edge / Firefox
3. Type a prompt (or click an example) and press Synthesize

No install. No internet. No Node/Python/C++ needed.

TRY THESE PROMPTS
-----------------
- design a 12 V to 5 V buck converter at 3 A
- 24V to 3.3V buck at 2 A, 1 MHz  (+ check "Include controller IC")

WHAT IT DOES
------------
Prompt -> topology -> physics sizing -> part binding (reject/retry)
      -> verified electrical IR -> downloadable .kicad_sch + netlist JSON

Built from parser_new / web-synth (offline single-file package).
