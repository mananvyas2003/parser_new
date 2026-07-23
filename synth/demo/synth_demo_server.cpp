// ============================================================================
//  synth_demo_server.cpp
//  Localhost demo for the circuit-synthesis compiler.
//  Serves a browser UI and runs the full pipeline on demand.
//
//  Build:
//    g++ -std=c++17 -I synth synth/demo/synth_demo_server.cpp -lws2_32 -o build/synth_demo
//  Run:
//    ./build/synth_demo.exe
//  Open:
//    http://127.0.0.1:8765
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <shellapi.h>

#include "parts_catalog.h"
#include "catalog_source.h"
#include "synth_pipeline.h"
#include "ir_json.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::mutex g_mutex;
std::string g_last_sch;
std::string g_last_json;
std::string g_html_dir = "synth/demo";

std::string JsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:   o += c;      break;
        }
    }
    return o;
}

std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string ExtractJsonString(const std::string& body, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto pos = body.find(needle);
    if (pos == std::string::npos) return {};
    pos = body.find(':', pos);
    if (pos == std::string::npos) return {};
    pos = body.find('"', pos);
    if (pos == std::string::npos) return {};
    ++pos;
    std::string out;
    while (pos < body.size() && body[pos] != '"') {
        if (body[pos] == '\\' && pos + 1 < body.size()) {
            out += body[pos + 1];
            pos += 2;
        } else {
            out += body[pos++];
        }
    }
    return out;
}

bool ExtractJsonBool(const std::string& body, const std::string& key, bool def = false) {
    const std::string needle = "\"" + key + "\"";
    auto pos = body.find(needle);
    if (pos == std::string::npos) return def;
    pos = body.find(':', pos);
    if (pos == std::string::npos) return def;
    while (pos < body.size() && (body[pos] == ':' || body[pos] == ' ')) ++pos;
    if (body.compare(pos, 4, "true") == 0) return true;
    if (body.compare(pos, 5, "false") == 0) return false;
    return def;
}

std::string HttpResponse(int code, const std::string& ctype,
                         const std::string& body,
                         const std::string& extra_headers = "") {
    const char* status = "OK";
    if (code == 400) status = "Bad Request";
    if (code == 404) status = "Not Found";
    if (code == 500) status = "Internal Server Error";

    std::ostringstream o;
    o << "HTTP/1.1 " << code << " " << status << "\r\n"
      << "Content-Type: " << ctype << "\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Access-Control-Allow-Origin: *\r\n"
      << "Connection: close\r\n"
      << extra_headers
      << "\r\n"
      << body;
    return o.str();
}

std::string ResultToJson(const pipeline::PipelineResult& r) {
    std::ostringstream o;
    if (!r.ok) {
        o << "{\"ok\":false,\"error\":\"" << JsonEscape(r.error) << "\"}";
        return o.str();
    }

    // Binding trace
    std::ostringstream bind;
    for (const auto& out : r.bind_report.outcomes) {
        bind << out.ref << " (" << out.role << "): ";
        if (out.skipped) bind << "SKIPPED\n";
        else if (out.bound) bind << "BOUND -> " << out.chosen_mpn << "\n";
        else bind << "FAILED\n";
        for (const auto& rc : out.rejected)
            bind << "    rejected " << rc.mpn << " : " << rc.reason << "\n";
    }

    // Nets text
    std::ostringstream nets;
    std::vector<std::vector<std::string>> terminals(r.design.nodes.size());
    for (const auto& c : r.design.components)
        for (const auto& p : c.pins)
            if (p.node != ir::INVALID_NODE && p.node < terminals.size())
                terminals[p.node].push_back(c.ref + "." + p.name);
    for (size_t i = 0; i < r.design.nodes.size(); ++i) {
        nets << r.design.nodes[i].name << ": ";
        for (size_t j = 0; j < terminals[i].size(); ++j) {
            if (j) nets << ", ";
            nets << terminals[i][j];
        }
        nets << "\n";
    }

    const std::string netlist = ir::ToJson(r.design);

    o << "{\n";
    o << "  \"ok\": true,\n";
    o << "  \"topology\": \"" << JsonEscape(r.design.topology) << "\",\n";
    o << "  \"kicad_path\": \"" << JsonEscape(r.kicad_path) << "\",\n";
    o << "  \"erc_ok\": " << (r.erc.ok() ? "true" : "false") << ",\n";
    o << "  \"spec\": {\"vin_v\":" << r.spec.vin_v
      << ",\"vout_v\":" << r.spec.vout_v
      << ",\"iout_a\":" << r.spec.iout_a
      << ",\"fsw_hz\":" << r.spec.fsw_hz << "},\n";
    o << "  \"sizing\": {\"duty\":" << r.sizing.duty
      << ",\"delta_il\":" << r.sizing.delta_il
      << ",\"il_peak\":" << r.sizing.il_peak
      << ",\"l_henries\":" << r.sizing.l_henries
      << ",\"cin_farads\":" << r.sizing.cin_farads
      << ",\"cout_farads\":" << r.sizing.cout_farads << "},\n";

    o << "  \"nodes\": [";
    for (size_t i = 0; i < r.design.nodes.size(); ++i) {
        if (i) o << ",";
        o << "\"" << JsonEscape(r.design.nodes[i].name) << "\"";
    }
    o << "],\n";

    o << "  \"components\": [\n";
    for (size_t i = 0; i < r.design.components.size(); ++i) {
        const auto& c = r.design.components[i];
        o << "    {\"ref\":\"" << JsonEscape(c.ref) << "\","
          << "\"role\":\"" << JsonEscape(c.role) << "\","
          << "\"mpn\":" << (c.mpn.empty() ? "null" : ("\"" + JsonEscape(c.mpn) + "\"")) << ","
          << "\"value\":" << c.value << ","
          << "\"package\":\"" << JsonEscape(c.package) << "\"}"
          << (i + 1 < r.design.components.size() ? "," : "") << "\n";
    }
    o << "  ],\n";

    o << "  \"bind_trace\": \"" << JsonEscape(bind.str()) << "\",\n";
    o << "  \"nets_text\": \"" << JsonEscape(nets.str()) << "\",\n";
    o << "  \"netlist\": " << netlist << "\n";
    o << "}\n";
    return o.str();
}

std::string HandleSynth(const std::string& body) {
    std::string prompt = ExtractJsonString(body, "prompt");
    if (prompt.empty()) prompt = "design a 12 V to 5 V buck converter at 3 A";
    const bool controller = ExtractJsonBool(body, "controller", false);

    cat::MockCatalogSource catalog(cat::BuildMockCatalog());
    const std::string out_path = "build/demo_output.kicad_sch";

    pipeline::PipelineResult r =
        pipeline::Run(prompt, catalog, out_path, controller);

    if (r.ok) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_last_sch = ReadFile(out_path);
        g_last_json = ir::ToJson(r.design);
        // Also write netlist next to schematic for convenience.
        ir::WriteJson(r.design, "build/demo_netlist.json");
    }

    std::cout << (r.ok ? "[OK] " : "[FAIL] ") << prompt
              << (controller ? " (+controller)" : "") << "\n";
    if (!r.ok) std::cout << "       " << r.error << "\n";

    return ResultToJson(r);
}

void HandleClient(SOCKET client) {
    char buf[16384];
    int n = recv(client, buf, sizeof(buf) - 1, 0);
    if (n <= 0) {
        closesocket(client);
        return;
    }
    buf[n] = '\0';
    std::string req(buf, n);

    // Drain extra body bytes if Content-Length > what we got.
    auto cl_pos = req.find("Content-Length:");
    if (cl_pos != std::string::npos) {
        size_t len = static_cast<size_t>(std::atoi(req.c_str() + cl_pos + 15));
        auto hdr_end = req.find("\r\n\r\n");
        if (hdr_end != std::string::npos) {
            size_t have = req.size() - (hdr_end + 4);
            while (have < len) {
                int m = recv(client, buf, sizeof(buf) - 1, 0);
                if (m <= 0) break;
                req.append(buf, m);
                have += static_cast<size_t>(m);
            }
        }
    }

    std::string response;
    if (req.rfind("GET / ", 0) == 0 || req.rfind("GET /index.html", 0) == 0) {
        std::string html = ReadFile(g_html_dir + "/index.html");
        if (html.empty()) {
            response = HttpResponse(404, "text/plain", "index.html not found. Run from repo root.");
        } else {
            response = HttpResponse(200, "text/html; charset=utf-8", html);
        }
    } else if (req.rfind("POST /api/synth", 0) == 0) {
        auto hdr_end = req.find("\r\n\r\n");
        std::string body = (hdr_end == std::string::npos) ? "" : req.substr(hdr_end + 4);
        std::string json = HandleSynth(body);
        response = HttpResponse(200, "application/json; charset=utf-8", json);
    } else if (req.rfind("GET /download/schematic", 0) == 0) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_last_sch.empty()) {
            response = HttpResponse(404, "text/plain", "No schematic yet. Run synthesize first.");
        } else {
            response = HttpResponse(
                200, "application/octet-stream", g_last_sch,
                "Content-Disposition: attachment; filename=\"design.kicad_sch\"\r\n");
        }
    } else if (req.rfind("GET /download/netlist", 0) == 0) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_last_json.empty()) {
            response = HttpResponse(404, "text/plain", "No netlist yet. Run synthesize first.");
        } else {
            response = HttpResponse(
                200, "application/json", g_last_json,
                "Content-Disposition: attachment; filename=\"netlist.json\"\r\n");
        }
    } else if (req.rfind("OPTIONS ", 0) == 0) {
        response = HttpResponse(200, "text/plain", "",
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n");
    } else {
        response = HttpResponse(404, "text/plain", "Not found");
    }

    send(client, response.c_str(), static_cast<int>(response.size()), 0);
    closesocket(client);
}

} // namespace

int main(int argc, char** argv) {
    const int port = (argc > 1) ? std::atoi(argv[1]) : 8765;
    if (argc > 2) g_html_dir = argv[2];

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        std::cerr << "socket() failed\n";
        WSACleanup();
        return 1;
    }

    BOOL yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind() failed on port " << port << "\n";
        closesocket(server);
        WSACleanup();
        return 1;
    }
    if (listen(server, 8) == SOCKET_ERROR) {
        std::cerr << "listen() failed\n";
        closesocket(server);
        WSACleanup();
        return 1;
    }

    std::cout << "=========================================================\n";
    std::cout << "  Circuit Synthesis Compiler — local demo\n";
    std::cout << "=========================================================\n";
    std::cout << "  Open:  http://127.0.0.1:" << port << "\n";
    std::cout << "  HTML:  " << g_html_dir << "/index.html\n";
    std::cout << "  Ctrl+C to stop\n";
    std::cout << "=========================================================\n";

    // Try to open the browser.
    {
        std::string url = "http://127.0.0.1:" + std::to_string(port);
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    while (true) {
        SOCKET client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        HandleClient(client);
    }

    closesocket(server);
    WSACleanup();
    return 0;
}
