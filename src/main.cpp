#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

struct DurationParseResult {
  bool ok = false;
  std::chrono::milliseconds value{0};
  std::string error;
};

DurationParseResult parse_duration_ms(const std::string& text) {
  // Supports: 150ms, 10s, 5m, 1h, 42 (seconds)
  if (text.empty()) return {false, {}, "empty duration"};

  std::string number_part = text;
  std::string unit = "s";
  if (text.size() >= 2 && text.substr(text.size() - 2) == "ms") {
    number_part = text.substr(0, text.size() - 2);
    unit = "ms";
  } else {
    char last = text.back();
    if (last == 's' || last == 'm' || last == 'h') {
      number_part = text.substr(0, text.size() - 1);
      unit = std::string(1, last);
    }
  }

  if (number_part.empty()) return {false, {}, "invalid duration: " + text};
  char* end = nullptr;
  long long n = std::strtoll(number_part.c_str(), &end, 10);
  if (end == nullptr || *end != '\0') return {false, {}, "invalid duration: " + text};
  if (n < 0) return {false, {}, "duration must be >= 0: " + text};

  long long ms = 0;
  if (unit == "ms") ms = n;
  else if (unit == "s") ms = n * 1000LL;
  else if (unit == "m") ms = n * 60LL * 1000LL;
  else if (unit == "h") ms = n * 60LL * 60LL * 1000LL;
  else return {false, {}, "unsupported duration unit: " + unit};

  return {true, std::chrono::milliseconds(ms), {}};
}

std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (unsigned char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[7];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  return out;
}

std::string now_rfc3339_utc() {
  using namespace std::chrono;
  auto now = system_clock::now();
  std::time_t t = system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return oss.str();
}

struct Target {
  std::string name;
  std::string host;
  std::string username;
  std::string password;
  std::string protocol; // redfish|ipmi|auto
};

struct Command {
  std::string text;
};

struct CommandResult {
  std::string cmd;
  bool ok = false;
  std::string error;
  std::string data_json; // JSON object (without surrounding quotes)
  long long duration_ms = 0;
};

struct TargetRunResult {
  std::string target;
  bool ok = true;
  std::vector<CommandResult> results;
};

struct RunResult {
  std::string run_id;
  std::string started_at;
  std::string ended_at;
  std::vector<TargetRunResult> targets;
};

struct Options {
  std::string host;
  std::string targets_file;
  std::string username;
  std::string password;
  std::string protocol = "auto";
  std::string output = "table"; // table|json
  int concurrency = 20;
  int repeat = 1;
  std::chrono::milliseconds every{0};
  bool debug = false;
  std::vector<std::string> cmd_texts;
  std::string cmd_file;
  std::vector<std::string> positional_cmd;
};

void print_usage(std::ostream& os) {
  os <<
      "bmcli (C++ skeleton)\n"
      "\n"
      "Usage:\n"
      "  bmcli [--host HOST | --targets FILE|-] [--user USER] [--password PASS]\n"
      "        [--protocol redfish|ipmi|auto] [-o table|json]\n"
      "        [--concurrency N] [--repeat N] [--every 10s]\n"
      "        [--cmd \"...\"]... [--cmd-file FILE] [SUBCOMMAND...]\n"
      "\n"
      "Examples:\n"
      "  bmcli --host 10.0.0.12 --user admin --password xxx power status -o json\n"
      "  bmcli --targets targets.txt --concurrency 20 --cmd \"power status\" --cmd \"health summary\"\n"
      "  bmcli --targets targets.txt --cmd-file commands.txt --every 30s --repeat 10\n"
      "\n"
      "Notes:\n"
      "  - This is an execution framework only (no real Redfish/IPMI yet).\n"
      "  - targets.txt format: host[,user[,pass[,protocol]]]\n"
      "  - commands.txt format: one command per line; blank lines and lines starting with # are ignored.\n";
}

bool is_flag(const std::string& s) { return !s.empty() && s[0] == '-'; }

bool parse_args(int argc, char** argv, Options& opt, std::string& err) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto require_value = [&](const char* flag) -> std::string {
      if (i + 1 >= argc) {
        err = std::string("missing value for ") + flag;
        return {};
      }
      return std::string(argv[++i]);
    };

    if (a == "-h" || a == "--help") {
      print_usage(std::cout);
      std::exit(0);
    } else if (a == "--host") {
      opt.host = require_value("--host");
      if (!err.empty()) return false;
    } else if (a == "--targets") {
      opt.targets_file = require_value("--targets");
      if (!err.empty()) return false;
    } else if (a == "--user") {
      opt.username = require_value("--user");
      if (!err.empty()) return false;
    } else if (a == "--password" || a == "--pass") {
      opt.password = require_value("--password");
      if (!err.empty()) return false;
    } else if (a == "--protocol") {
      opt.protocol = require_value("--protocol");
      if (!err.empty()) return false;
      if (opt.protocol != "redfish" && opt.protocol != "ipmi" && opt.protocol != "auto") {
        err = "invalid --protocol: " + opt.protocol;
        return false;
      }
    } else if (a == "-o" || a == "--output") {
      opt.output = require_value("-o");
      if (!err.empty()) return false;
      if (opt.output != "table" && opt.output != "json") {
        err = "invalid output format: " + opt.output;
        return false;
      }
    } else if (a == "--concurrency") {
      std::string v = require_value("--concurrency");
      if (!err.empty()) return false;
      opt.concurrency = std::atoi(v.c_str());
      if (opt.concurrency <= 0) {
        err = "invalid --concurrency: " + v;
        return false;
      }
    } else if (a == "--repeat") {
      std::string v = require_value("--repeat");
      if (!err.empty()) return false;
      opt.repeat = std::atoi(v.c_str());
      if (opt.repeat <= 0) {
        err = "invalid --repeat: " + v;
        return false;
      }
    } else if (a == "--every") {
      std::string v = require_value("--every");
      if (!err.empty()) return false;
      auto parsed = parse_duration_ms(v);
      if (!parsed.ok) {
        err = parsed.error;
        return false;
      }
      opt.every = parsed.value;
    } else if (a == "--cmd") {
      std::string v = require_value("--cmd");
      if (!err.empty()) return false;
      opt.cmd_texts.push_back(v);
    } else if (a == "--cmd-file") {
      opt.cmd_file = require_value("--cmd-file");
      if (!err.empty()) return false;
    } else if (a == "--debug") {
      opt.debug = true;
    } else if (!is_flag(a)) {
      // Treat the remainder as a single positional command (subcommand tree).
      for (; i < argc; ++i) opt.positional_cmd.push_back(argv[i]);
      break;
    } else {
      err = "unknown flag: " + a;
      return false;
    }
  }

  if (!opt.host.empty() && !opt.targets_file.empty()) {
    err = "use either --host or --targets, not both";
    return false;
  }

  if (opt.host.empty() && opt.targets_file.empty()) {
    err = "missing target: provide --host or --targets";
    return false;
  }

  int command_sources = 0;
  if (!opt.cmd_texts.empty()) command_sources++;
  if (!opt.cmd_file.empty()) command_sources++;
  if (!opt.positional_cmd.empty()) command_sources++;
  if (command_sources == 0) {
    err = "missing command: provide SUBCOMMAND... or --cmd / --cmd-file";
    return false;
  }
  if (command_sources > 1) {
    err = "command sources conflict: choose one of SUBCOMMAND..., --cmd, or --cmd-file";
    return false;
  }

  if (opt.repeat > 1 && opt.every.count() == 0) {
    // Reasonable default: 0 means back-to-back.
  }
  return true;
}

std::vector<std::string> split_tokens(std::string s) {
  for (char& c : s) {
    if (c == ',') c = ' ';
  }
  std::istringstream iss(s);
  std::vector<std::string> out;
  for (std::string t; iss >> t;) out.push_back(t);
  return out;
}

std::vector<Target> load_targets(const Options& opt, std::string& err) {
  std::vector<Target> targets;
  if (!opt.host.empty()) {
    Target t;
    t.name = opt.host;
    t.host = opt.host;
    t.username = opt.username;
    t.password = opt.password;
    t.protocol = opt.protocol;
    targets.push_back(std::move(t));
    return targets;
  }

  std::istream* in = nullptr;
  std::ifstream f;
  if (opt.targets_file == "-") {
    in = &std::cin;
  } else {
    f.open(opt.targets_file);
    if (!f.is_open()) {
      err = "failed to open targets file: " + opt.targets_file;
      return {};
    }
    in = &f;
  }

  std::string line;
  size_t line_no = 0;
  while (std::getline(*in, line)) {
    line_no++;
    auto trimmed = line;
    trimmed.erase(trimmed.begin(),
                  std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char c) { return !std::isspace(c); }));
    if (trimmed.empty() || trimmed[0] == '#') continue;

    auto parts = split_tokens(trimmed);
    if (parts.empty()) continue;

    Target t;
    t.host = parts[0];
    t.name = t.host;
    t.username = (parts.size() >= 2) ? parts[1] : opt.username;
    t.password = (parts.size() >= 3) ? parts[2] : opt.password;
    t.protocol = (parts.size() >= 4) ? parts[3] : opt.protocol;
    if (t.protocol.empty()) t.protocol = "auto";
    if (t.protocol != "redfish" && t.protocol != "ipmi" && t.protocol != "auto") {
      err = "invalid protocol in targets file at line " + std::to_string(line_no) + ": " + t.protocol;
      return {};
    }
    targets.push_back(std::move(t));
  }

  if (targets.empty()) {
    err = "no targets loaded";
    return {};
  }
  return targets;
}

std::vector<Command> load_commands(const Options& opt, std::string& err) {
  std::vector<Command> cmds;

  if (!opt.positional_cmd.empty()) {
    std::ostringstream oss;
    for (size_t i = 0; i < opt.positional_cmd.size(); ++i) {
      if (i) oss << ' ';
      oss << opt.positional_cmd[i];
    }
    cmds.push_back(Command{oss.str()});
    return cmds;
  }

  if (!opt.cmd_texts.empty()) {
    for (const auto& t : opt.cmd_texts) cmds.push_back(Command{t});
    return cmds;
  }

  std::ifstream f(opt.cmd_file);
  if (!f.is_open()) {
    err = "failed to open command file: " + opt.cmd_file;
    return {};
  }
  std::string line;
  while (std::getline(f, line)) {
    auto trimmed = line;
    trimmed.erase(trimmed.begin(),
                  std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char c) { return !std::isspace(c); }));
    if (trimmed.empty() || trimmed[0] == '#') continue;
    cmds.push_back(Command{trimmed});
  }
  if (cmds.empty()) {
    err = "no commands loaded";
    return {};
  }
  return cmds;
}

CommandResult execute_command_stub(const Target& t, const Command& c, bool debug) {
  using clock = std::chrono::steady_clock;
  auto start = clock::now();

  // Stub: just echo what would be executed.
  // When real implementations land, this is where protocol dispatch happens.
  bool ok = true;
  std::string error;

  // Minimal validation for now.
  if (c.text.empty()) {
    ok = false;
    error = "empty command";
  }

  std::ostringstream data;
  data << "{"
       << "\"protocol\":\"" << json_escape(t.protocol) << "\","
       << "\"host\":\"" << json_escape(t.host) << "\","
       << "\"note\":\"" << json_escape("stub execution (no real BMC call)") << "\""
       << "}";

  auto end = clock::now();
  long long dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

  if (debug) {
    std::cerr << "[debug] target=" << t.host << " protocol=" << t.protocol << " cmd=" << c.text
              << " user=" << (t.username.empty() ? "<empty>" : t.username) << " pass=<redacted>\n";
  }

  return CommandResult{c.text, ok, error, data.str(), dur_ms};
}

RunResult execute_run(const Options& opt, const std::vector<Target>& targets, const std::vector<Command>& cmds,
                      int run_index) {
  RunResult run;
  run.run_id = now_rfc3339_utc() + "#"+ std::to_string(run_index);
  run.started_at = now_rfc3339_utc();
  run.targets.resize(targets.size());

  std::atomic<size_t> next{0};

  int workers = std::min<int>(opt.concurrency, static_cast<int>(targets.size()));
  std::vector<std::thread> threads;
  threads.reserve(static_cast<size_t>(workers));

  for (int w = 0; w < workers; ++w) {
    threads.emplace_back([&]() {
      while (true) {
        size_t idx = next.fetch_add(1);
        if (idx >= targets.size()) return;
        const Target& t = targets[idx];

        TargetRunResult tr;
        tr.target = t.host;
        tr.ok = true;
        tr.results.reserve(cmds.size());
        for (const auto& c : cmds) {
          auto r = execute_command_stub(t, c, opt.debug);
          if (!r.ok) tr.ok = false;
          tr.results.push_back(std::move(r));
        }
        run.targets[idx] = std::move(tr);
      }
    });
  }

  for (auto& th : threads) th.join();

  run.ended_at = now_rfc3339_utc();
  return run;
}

int compute_exit_code(const std::vector<RunResult>& runs) {
  for (const auto& run : runs) {
    for (const auto& t : run.targets) {
      if (!t.ok) return 6; // operation rejected/state not allowed (closest placeholder)
      for (const auto& r : t.results) {
        if (!r.ok) return 6;
      }
    }
  }
  return 0;
}

void print_table(const std::vector<RunResult>& runs) {
  for (const auto& run : runs) {
    for (const auto& t : run.targets) {
      for (const auto& r : t.results) {
        std::cout << "run=" << run.run_id << " target=" << t.target << " ok=" << (r.ok ? "true" : "false")
                  << " dur_ms=" << r.duration_ms << " cmd=\"" << r.cmd << "\"";
        if (!r.ok) std::cout << " error=\"" << r.error << "\"";
        std::cout << "\n";
      }
    }
  }
}

void print_json(const std::vector<RunResult>& runs) {
  std::cout << "{";
  std::cout << "\"runs\":[";
  for (size_t ri = 0; ri < runs.size(); ++ri) {
    const auto& run = runs[ri];
    if (ri) std::cout << ",";
    std::cout << "{";
    std::cout << "\"run_id\":\"" << json_escape(run.run_id) << "\",";
    std::cout << "\"started_at\":\"" << json_escape(run.started_at) << "\",";
    std::cout << "\"ended_at\":\"" << json_escape(run.ended_at) << "\",";
    std::cout << "\"targets\":[";
    for (size_t ti = 0; ti < run.targets.size(); ++ti) {
      const auto& t = run.targets[ti];
      if (ti) std::cout << ",";
      std::cout << "{";
      std::cout << "\"target\":\"" << json_escape(t.target) << "\",";
      std::cout << "\"ok\":" << (t.ok ? "true" : "false") << ",";
      std::cout << "\"results\":[";
      for (size_t ci = 0; ci < t.results.size(); ++ci) {
        const auto& r = t.results[ci];
        if (ci) std::cout << ",";
        std::cout << "{";
        std::cout << "\"cmd\":\"" << json_escape(r.cmd) << "\",";
        std::cout << "\"ok\":" << (r.ok ? "true" : "false") << ",";
        std::cout << "\"duration_ms\":" << r.duration_ms << ",";
        std::cout << "\"data\":" << r.data_json;
        if (!r.ok) std::cout << ",\"error\":\"" << json_escape(r.error) << "\"";
        std::cout << "}";
      }
      std::cout << "]";
      std::cout << "}";
    }
    std::cout << "]";
    std::cout << "}";
  }
  std::cout << "]";
  std::cout << "}\n";
}

} // namespace

int main(int argc, char** argv) {
  Options opt;
  std::string err;
  if (!parse_args(argc, argv, opt, err)) {
    std::cerr << "error: " << err << "\n\n";
    print_usage(std::cerr);
    return 2;
  }

  std::vector<Target> targets = load_targets(opt, err);
  if (!err.empty()) {
    std::cerr << "error: " << err << "\n";
    return 2;
  }

  std::vector<Command> cmds = load_commands(opt, err);
  if (!err.empty()) {
    std::cerr << "error: " << err << "\n";
    return 2;
  }

  std::vector<RunResult> runs;
  runs.reserve(static_cast<size_t>(opt.repeat));

  for (int i = 1; i <= opt.repeat; ++i) {
    runs.push_back(execute_run(opt, targets, cmds, i));
    if (i < opt.repeat && opt.every.count() > 0) {
      std::this_thread::sleep_for(opt.every);
    }
  }

  if (opt.output == "json") print_json(runs);
  else print_table(runs);

  return compute_exit_code(runs);
}
