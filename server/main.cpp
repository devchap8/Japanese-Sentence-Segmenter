// jss_server: loads the dictionary once, then serves the page in web/ and
//
//   GET /api/search?q=<utf8>&mode=prefix|predictive&limit=50   (see api.hpp)
//   GET /api/health                                            entry and key counts
//
// Usage: jss_server [--dict data/dict.tsv] [--web web] [--host 127.0.0.1] [--port 8080]
// Use --host 0.0.0.0 to let other machines on the network connect.
#include <chrono>
#include <cstddef>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "api.hpp"
#include "jss/dictionary.hpp"

#ifndef JSS_DEFAULT_DICT
#define JSS_DEFAULT_DICT "data/dict.tsv"
#endif
#ifndef JSS_DEFAULT_WEB
#define JSS_DEFAULT_WEB "web"
#endif

namespace {

using Clock = std::chrono::steady_clock;
using nlohmann::json;

struct Options {
    std::string dict_path = JSS_DEFAULT_DICT;
    std::string web_dir = JSS_DEFAULT_WEB;
    std::string host = "127.0.0.1";
    int port = 8080;
};

Options parse_args(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error(arg + " needs a value");
            return argv[++i];
        };
        if (arg == "--dict") opt.dict_path = value();
        else if (arg == "--web") opt.web_dir = value();
        else if (arg == "--host") opt.host = value();
        else if (arg == "--port") opt.port = std::stoi(value());
        else throw std::runtime_error("unknown argument: " + arg +
                                      "\nusage: jss_server [--dict path] [--web dir] [--host addr] [--port n]");
    }
    return opt;
}

void send_json(httplib::Response& res, const json& body, int status = 200) {
    res.status = status;
    // dump() writes UTF-8 as is and escapes " \ and control characters.
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

void send_error(httplib::Response& res, int status, const std::string& message) {
    send_json(res, {{"error", message}}, status);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options opt = parse_args(argc, argv);

        const auto t0 = Clock::now();
        const jss::Dictionary dict = jss::load_dictionary_file(opt.dict_path);
        const auto load_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
        std::cout << "loaded " << dict.entries.size() << " entries, " << dict.key_count << " keys in "
                  << load_ms << " ms\n";

        httplib::Server server;

        server.Get("/api/search", [&dict](const httplib::Request& req, httplib::Response& res) {
            if (!req.has_param("q")) return send_error(res, 400, "missing parameter q");

            jss::SearchMode mode = jss::SearchMode::prefix;
            const std::string mode_name = req.has_param("mode") ? req.get_param_value("mode") : "prefix";
            if (mode_name == "predictive") mode = jss::SearchMode::predictive;
            else if (mode_name != "prefix") return send_error(res, 400, "mode must be prefix or predictive");

            std::size_t limit = jss::kDefaultLimit;
            if (req.has_param("limit")) {
                try {
                    limit = static_cast<std::size_t>(std::stoul(req.get_param_value("limit")));
                } catch (const std::exception&) {
                    return send_error(res, 400, "limit must be a positive number");
                }
            }

            const auto start = Clock::now();
            json body = jss::search_json(dict, req.get_param_value("q"), mode, limit);
            body["elapsed_us"] =
                std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
            send_json(res, body);
        });

        server.Get("/api/health", [&dict](const httplib::Request&, httplib::Response& res) {
            send_json(res, {{"entries", dict.entries.size()}, {"keys", dict.key_count}});
        });

        server.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
            std::string message = "internal error";
            try {
                if (ep) std::rethrow_exception(ep);
            } catch (const std::exception& e) {
                message = e.what();
            } catch (...) {
            }
            std::cerr << "error: " << message << '\n';
            send_error(res, 500, message);
        });

        if (!server.set_mount_point("/", opt.web_dir)) {
            throw std::runtime_error("web directory not found: " + opt.web_dir);
        }

        std::cout << "serving " << opt.web_dir << " on http://" << opt.host << ':' << opt.port << "/\n";
        if (!server.listen(opt.host, opt.port)) {
            throw std::runtime_error("can't listen on " + opt.host + ':' + std::to_string(opt.port));
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "jss_server: " << e.what() << '\n';
        return 1;
    }
}
