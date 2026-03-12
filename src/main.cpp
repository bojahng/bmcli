#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "Ws2_32.lib")
#else
#  include <netdb.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#endif

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
  bool insecure = false;
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
      "        [--protocol redfish|ipmi|auto] [--insecure] [-o table|json]\n"
      "        [--concurrency N] [--repeat N] [--every 10s]\n"
      "        [--cmd \"...\"]... [--cmd-file FILE] [SUBCOMMAND...]\n"
      "\n"
      "Examples:\n"
      "  bmcli --host 10.0.0.12 --user admin --password xxx power status -o json\n"
      "  bmcli --targets targets.txt --concurrency 20 --cmd \"power status\" --cmd \"health summary\"\n"
      "  bmcli --targets targets.txt --cmd-file commands.txt --every 30s --repeat 10\n"
      "\n"
      "Notes:\n"
      "  - Redfish is implemented for local testing against mock server (HTTP only).\n"
      "  - IPMI is not implemented yet.\n"
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
    } else if (a == "--insecure") {
      opt.insecure = true;
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

struct ParsedEndpoint {
  bool ok = false;
  std::string scheme;
  std::string host;
  std::string port;
  std::string error;
};

ParsedEndpoint parse_endpoint(std::string host) {
  ParsedEndpoint ep;
  ep.ok = false;
  ep.scheme = "http";

  auto pos = host.find("://");
  if (pos != std::string::npos) {
    ep.scheme = host.substr(0, pos);
    host = host.substr(pos + 3);
  }

  if (ep.scheme != "http" && ep.scheme != "https") {
    ep.error = "unsupported scheme: " + ep.scheme;
    return ep;
  }
  if (ep.scheme == "https") {
    ep.error = "https not supported in MVP (use mock over http)";
    return ep;
  }

  // Strip path if accidentally included.
  auto slash = host.find('/');
  if (slash != std::string::npos) host = host.substr(0, slash);

  if (host.empty()) {
    ep.error = "empty host";
    return ep;
  }

  auto colon = host.rfind(':');
  if (colon != std::string::npos) {
    ep.host = host.substr(0, colon);
    ep.port = host.substr(colon + 1);
  } else {
    ep.host = host;
    ep.port = "80";
  }
  if (ep.host.empty() || ep.port.empty()) {
    ep.error = "invalid host:port: " + host;
    return ep;
  }

  ep.ok = true;
  return ep;
}

std::string base64_encode(const std::string& in) {
  static const char* kTable = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((in.size() + 2) / 3) * 4);
  size_t i = 0;
  while (i < in.size()) {
    unsigned char b0 = static_cast<unsigned char>(in[i++]);
    bool has_b1 = i < in.size();
    unsigned char b1 = has_b1 ? static_cast<unsigned char>(in[i++]) : 0;
    bool has_b2 = i < in.size();
    unsigned char b2 = has_b2 ? static_cast<unsigned char>(in[i++]) : 0;

    out.push_back(kTable[(b0 >> 2) & 0x3F]);
    out.push_back(kTable[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)]);
    out.push_back(has_b1 ? kTable[((b1 & 0x0F) << 2) | ((b2 >> 6) & 0x03)] : '=');
    out.push_back(has_b2 ? kTable[b2 & 0x3F] : '=');
  }
  return out;
}

struct HttpResponse {
  bool ok = false;
  int status = 0;
  std::string body;
  std::string error;
};

class Socket {
 public:
  Socket() = default;
  ~Socket() { close(); }
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  bool connect_tcp(const std::string& host, const std::string& port, std::string& err) {
#if defined(_WIN32)
    static std::once_flag once;
    std::call_once(once, []() {
      WSADATA wsaData;
      if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        // Best effort; subsequent socket calls will fail with connect error.
      }
    });
#endif

    struct addrinfo hints {};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_UNSPEC;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* result = nullptr;
    int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
    if (rc != 0 || result == nullptr) {
#if defined(_WIN32)
      err = "getaddrinfo failed";
#else
      err = std::string("getaddrinfo failed: ") + gai_strerror(rc);
#endif
      return false;
    }

    for (struct addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
      socket_t s = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
      if (s == invalid()) continue;
      if (::connect(s, rp->ai_addr, static_cast<int>(rp->ai_addrlen)) == 0) {
        fd_ = s;
        freeaddrinfo(result);
        return true;
      }
      close_socket(s);
    }
    freeaddrinfo(result);
    err = "connect failed";
    return false;
  }

  bool send_all(const std::string& data, std::string& err) {
    const char* p = data.data();
    size_t remaining = data.size();
    while (remaining > 0) {
#if defined(_WIN32)
      int n = ::send(fd_, p, static_cast<int>(remaining), 0);
#else
      ssize_t n = ::send(fd_, p, remaining, 0);
#endif
      if (n <= 0) {
        err = "send failed";
        return false;
      }
      p += n;
      remaining -= static_cast<size_t>(n);
    }
    return true;
  }

  bool recv_all(std::string& out, std::string& err) {
    char buf[4096];
    while (true) {
#if defined(_WIN32)
      int n = ::recv(fd_, buf, static_cast<int>(sizeof(buf)), 0);
#else
      ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
#endif
      if (n < 0) {
        err = "recv failed";
        return false;
      }
      if (n == 0) break;
      out.append(buf, buf + n);
    }
    return true;
  }

  void close() {
    if (fd_ == invalid()) return;
    close_socket(fd_);
    fd_ = invalid();
  }

 private:
#if defined(_WIN32)
  using socket_t = SOCKET;
  static socket_t invalid() { return INVALID_SOCKET; }
  static void close_socket(socket_t s) { closesocket(s); }
#else
  using socket_t = int;
  static socket_t invalid() { return -1; }
  static void close_socket(socket_t s) { ::close(s); }
#endif

  socket_t fd_ = invalid();
};

HttpResponse http_request(const std::string& host, const std::string& port, const std::string& method,
                          const std::string& path, const std::map<std::string, std::string>& headers,
                          const std::string& body) {
  HttpResponse resp;
  Socket s;
  std::string err;
  if (!s.connect_tcp(host, port, err)) {
    resp.error = err;
    return resp;
  }

  std::ostringstream req;
  req << method << " " << path << " HTTP/1.1\r\n";
  req << "Host: " << host << "\r\n";
  req << "Connection: close\r\n";
  for (const auto& kv : headers) req << kv.first << ": " << kv.second << "\r\n";
  if (!body.empty()) {
    req << "Content-Type: application/json\r\n";
    req << "Content-Length: " << body.size() << "\r\n";
  } else {
    req << "Content-Length: 0\r\n";
  }
  req << "\r\n";
  req << body;

  if (!s.send_all(req.str(), err)) {
    resp.error = err;
    return resp;
  }
  std::string raw;
  if (!s.recv_all(raw, err)) {
    resp.error = err;
    return resp;
  }

  auto header_end = raw.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    resp.error = "invalid http response";
    return resp;
  }
  std::string header = raw.substr(0, header_end);
  resp.body = raw.substr(header_end + 4);

  std::istringstream hs(header);
  std::string httpver;
  hs >> httpver >> resp.status;
  if (resp.status <= 0) {
    resp.error = "invalid http status";
    return resp;
  }

  resp.ok = true;
  return resp;
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

CommandResult execute_redfish_command(const Options& opt, const Target& t, const Command& c) {
  using clock = std::chrono::steady_clock;
  auto start = clock::now();

  auto ep = parse_endpoint(t.host);
  if (!ep.ok) {
    auto end = clock::now();
    long long dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::ostringstream data;
    data << "{"
         << "\"http_status\":0,"
         << "\"body\":\"\","
         << "\"error\":\"" << json_escape(ep.error) << "\""
         << "}";
    return CommandResult{c.text, false, ep.error, data.str(), dur_ms};
  }

  auto tokens = split_tokens(c.text);
  if (tokens.size() < 2) {
    auto end = clock::now();
    long long dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    return CommandResult{c.text, false, "invalid command: " + c.text, "{\"http_status\":0,\"body\":\"\"}", dur_ms};
  }

  std::string module = tokens[0];
  std::string action = tokens[1];

  std::string method = "GET";
  std::string path;
  std::string body;

  if (module == "power") {
    if (action == "status") {
      path = "/redfish/v1/Systems/1";
      method = "GET";
    } else if (action == "on" || action == "off" || action == "cycle" || action == "reset") {
      path = "/redfish/v1/Systems/1/Actions/ComputerSystem.Reset";
      method = "POST";
      std::string reset_type = "On";
      if (action == "off") reset_type = "ForceOff";
      else if (action == "cycle") reset_type = "PowerCycle";
      else if (action == "reset") reset_type = "ForceRestart";
      body = std::string("{\"ResetType\":\"") + reset_type + "\"}";
    }
  } else if (module == "sel") {
    if (action == "list" || action == "get") {
      path = "/redfish/v1/Managers/1/LogServices/SEL/Entries";
      method = "GET";
    } else if (action == "clear") {
      path = "/redfish/v1/Managers/1/LogServices/SEL/Actions/LogService.ClearLog";
      method = "POST";
      body = "{}";
    }
  } else if (module == "sensor") {
    if (action == "list" || action == "get") {
      path = "/redfish/v1/Chassis/1/Thermal";
      method = "GET";
    }
  } else if (module == "health") {
    if (action == "summary") {
      path = "/redfish/v1/Systems/1";
      method = "GET";
    }
  }

  if (path.empty()) {
    auto end = clock::now();
    long long dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    return CommandResult{c.text, false, "unsupported command for redfish: " + c.text,
                         "{\"http_status\":0,\"body\":\"\"}", dur_ms};
  }

  std::map<std::string, std::string> headers;
  if (!t.username.empty() || !t.password.empty()) {
    headers["Authorization"] = "Basic " + base64_encode(t.username + ":" + t.password);
  }

  auto http = http_request(ep.host, ep.port, method, path, headers, body);
  auto end = clock::now();
  long long dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

  bool ok = http.ok && http.status >= 200 && http.status < 300;
  std::ostringstream data;
  data << "{"
       << "\"http_status\":" << (http.ok ? http.status : 0) << ","
       << "\"body\":\"" << json_escape(http.body) << "\""
       << "}";

  if (opt.debug) {
    std::cerr << "[debug] redfish " << method << " " << ep.host << ":" << ep.port << path
              << " status=" << (http.ok ? std::to_string(http.status) : "0") << "\n";
  }

  std::string err = ok ? "" : (http.ok ? ("http " + std::to_string(http.status)) : http.error);
  return CommandResult{c.text, ok, err, data.str(), dur_ms};
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
          CommandResult r;
          if (t.protocol == "redfish" || t.protocol == "auto") {
            r = execute_redfish_command(opt, t, c);
          } else {
            r = execute_command_stub(t, c, opt.debug);
          }
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
