// ============================================================================
//  prompt_parser_test.cpp
//  Task 2 test: verify natural-language prompts are decomposed into a BuckSpec.
//
//  Build:
//    g++ -std=c++17 -I synth synth/tests/prompt_parser_test.cpp -o build/prompt_parser_test
//  Run:
//    ./build/prompt_parser_test
// ============================================================================

#include "prompt_parser.h"

#include <cassert>
#include <cmath>
#include <iostream>

static bool Close(double a, double b, double rel = 1e-6) {
    const double d = std::max(1.0, std::fabs(b));
    return std::fabs(a - b) / d <= rel;
}

int main() {
    // Canonical prompt from the brief.
    auto r1 = prompt::ParseBuckPrompt("design a 12 V to 5 V buck converter at 3 A");
    assert(r1.ok);
    assert(r1.topology == "buck_converter");
    assert(Close(r1.spec.vin_v, 12.0));
    assert(Close(r1.spec.vout_v, 5.0));
    assert(Close(r1.spec.iout_a, 3.0));
    assert(Close(r1.spec.fsw_hz, 500e3)); // default when unspecified
    std::cout << "prompt1: " << r1.spec.vin_v << "V -> " << r1.spec.vout_v
              << "V @ " << r1.spec.iout_a << "A, " << r1.spec.fsw_hz / 1e3 << "kHz\n";

    // Decimals, explicit frequency, mA current.
    auto r2 = prompt::ParseBuckPrompt("step-down 24V to 3.3V at 1500 mA, 1 MHz");
    assert(r2.ok);
    assert(Close(r2.spec.vin_v, 24.0));
    assert(Close(r2.spec.vout_v, 3.3));
    assert(Close(r2.spec.iout_a, 1.5));
    assert(Close(r2.spec.fsw_hz, 1e6));
    std::cout << "prompt2: " << r2.spec.vin_v << "V -> " << r2.spec.vout_v
              << "V @ " << r2.spec.iout_a << "A, " << r2.spec.fsw_hz / 1e6 << "MHz\n";

    // Order-independence: still Vin > Vout even if written low-to-high.
    auto r3 = prompt::ParseBuckPrompt("buck 3.3 V from 5 V at 2 A");
    assert(r3.ok);
    assert(Close(r3.spec.vin_v, 5.0));
    assert(Close(r3.spec.vout_v, 3.3));

    // Failure cases.
    auto bad1 = prompt::ParseBuckPrompt("make me a sandwich");
    assert(!bad1.ok);
    auto bad2 = prompt::ParseBuckPrompt("a buck converter at 2 A"); // no voltages
    assert(!bad2.ok);

    std::cout << "\n[PASS] Task 2: prompt parser test passed.\n";
    return 0;
}
