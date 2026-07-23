#pragma once

// ============================================================================
//  prompt_parser.h
//  Front-end: turn a natural-language prompt like
//     "design a 12 V to 5 V buck converter at 3 A"
//  into a structured phys::BuckSpec. This is the "parsing / specification
//  extraction" stage from the design brief -- it converts intent + engineering
//  quantities into numbers the synthesis pipeline can act on.
//
//  Deliberately simple: it scans "<number> <unit>" tokens and classifies them
//  into voltages / currents / frequencies. First voltage = Vin, second = Vout
//  (swapped if needed so Vin > Vout for a buck).
// ============================================================================

#include "buck_sizing.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <vector>

namespace prompt {

struct ParseResult {
    bool           ok = false;
    std::string    error;
    std::string    topology;   // e.g. "buck_converter"
    phys::BuckSpec spec;
};

inline std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

inline ParseResult ParseBuckPrompt(const std::string& text) {
    ParseResult r;
    const std::string lower = ToLower(text);

    if (lower.find("buck") != std::string::npos ||
        lower.find("step-down") != std::string::npos ||
        lower.find("step down") != std::string::npos) {
        r.topology = "buck_converter";
    } else {
        r.error = "no recognized topology in prompt";
        return r;
    }

    // number + unit (units ordered longest-first so "mhz" beats "hz", etc.)
    std::regex re(R"(([0-9]+(?:\.[0-9]+)?)\s*(mhz|khz|hz|ma|mv|kv|uh|nh|uf|nf|pf|v|a|w)\b)",
                  std::regex::icase);

    std::vector<double> volts, amps, freqs;

    for (auto it = std::sregex_iterator(lower.begin(), lower.end(), re);
         it != std::sregex_iterator(); ++it) {
        const double val = std::stod((*it)[1].str());
        std::string unit = ToLower((*it)[2].str());

        if      (unit == "v")   volts.push_back(val);
        else if (unit == "mv")  volts.push_back(val / 1000.0);
        else if (unit == "kv")  volts.push_back(val * 1000.0);
        else if (unit == "a")   amps.push_back(val);
        else if (unit == "ma")  amps.push_back(val / 1000.0);
        else if (unit == "hz")  freqs.push_back(val);
        else if (unit == "khz") freqs.push_back(val * 1e3);
        else if (unit == "mhz") freqs.push_back(val * 1e6);
        // uh/uf/etc. are ignored here (values are computed by the sizing layer)
    }

    if (volts.size() < 2) {
        r.error = "need two voltages (Vin and Vout), e.g. '12 V to 5 V'";
        return r;
    }
    if (amps.empty()) {
        r.error = "need an output current, e.g. 'at 3 A'";
        return r;
    }

    double vin = volts[0];
    double vout = volts[1];
    if (vin < vout) std::swap(vin, vout); // buck steps down

    r.spec.vin_v  = vin;
    r.spec.vout_v = vout;
    r.spec.iout_a = amps[0];
    if (!freqs.empty()) r.spec.fsw_hz = freqs[0];

    r.ok = true;
    return r;
}

} // namespace prompt
