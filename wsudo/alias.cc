////
#include <json.hpp>
#include <Shlwapi.h>
#include <PathCch.h>
#include <bela/path.hpp>
#include <bela/terminal.hpp>
#include <bela/io.hpp>
#include <vfsenv.hpp>
#include <file.hpp>
#include <filesystem>
#include "alias.hpp"
#include "wsudo.hpp"

wsudo::AliasEngine::~AliasEngine() {
  if (updated) {
    Apply();
  }
}

bool wsudo::AliasEngine::Initialize(bool verbose) {
  auto file = priv::PathSearcher::Instance().JoinAppData(L"Privexec\\Privexec.json");
  DbgPrint(L"use %s", file);
  priv::FD fd;
  if (auto en = _wfopen_s(&fd.fd, file.data(), L"rb"); en != 0) {
    auto ec = bela::make_error_code_from_errno(en);
    DbgPrint(L"open %s: %s", file, ec.message);
    return false;
  }
  try {
    auto json = nlohmann::json::parse(fd.fd, nullptr, true, true);
    auto cmds = json["alias"];
    for (auto &cmd : cmds) {
      alias.emplace(bela::encode_into<char, wchar_t>(cmd["name"].get<std::string_view>()),
                    AliasTarget(cmd["target"].get<std::string_view>(), cmd["description"].get<std::string_view>()));
    }
  } catch (const std::exception &e) {
    bela::FPrintF(stderr, L"\x1b[31mAliasEngine::Initialize: %s\x1b[0m\n", e.what());
    return false;
  }
  return true;
}

std::optional<std::wstring> wsudo::AliasEngine::Target(std::wstring_view al) {
  auto it = alias.find(al);
  if (it == alias.end()) {
    return std::nullopt;
  }
  return std::make_optional(it->second.target);
}

bool wsudo::AliasEngine::Apply() {
  auto file = priv::PathSearcher::Instance().JoinAppData(L"Privexec\\Privexec.json");
  DbgPrint(L"use %s", file);
  std::filesystem::path p(file);
  auto parent = p.parent_path();
  std::error_code e;
  if (!std::filesystem::exists(parent, e)) {
    if (!std::filesystem::create_directories(parent, e)) {
      auto ec = bela::make_error_code_from_std(e);
      bela::FPrintF(stderr, L"\x1b[31mAliasEngine::Apply: unable create dir %s %s\x1b[0m\n", parent.c_str(),
                    ec.message);
      return false;
    }
  }
  try {
    nlohmann::json root, av;
    for (const auto &i : alias) {
      nlohmann::json a;
      a["name"] = bela::encode_into<wchar_t, char>(i.first);
      a["target"] = bela::encode_into<wchar_t, char>(i.second.target);
      a["description"] = bela::encode_into<wchar_t, char>(i.second.desc);
      av.emplace_back(std::move(a));
    }
    root["alias"] = av;
    bela::error_code ec;
    if (bela::io::AtomicWriteText(file, bela::io::as_bytes<char>(root.dump(4)), ec)) {
      bela::FPrintF(stderr, L"\x1b[31mAliasEngine::Apply: %s\x1b[0m\n", ec.message);
      return false;
    }
    updated = false;
  } catch (const std::exception &e) {
    bela::FPrintF(stderr, L"\x1b[31mAliasEngine::Apply: %s\x1b[0m\n", e.what());
    return false;
  }
  return true;
}

int wsudo::AliasSubcmd(const std::vector<std::wstring> &argv) {
  if (argv.size() < 2) {
    bela::FPrintF(stderr, L"\x1b[31mwsudo alias missing argument, current have: %d\x1b[0m\n", argv.size());
    return 1;
  }
  AliasEngine ae;
  if (!ae.Initialize()) {
    bela::FPrintF(stderr, L"\x1b[31mwsudo Alias Engine Initialize error, Please check it exists\x1b[0m\n");
    return 1;
  }
  if (bela::EqualsIgnoreCase(argv[1], L"delete") || bela::EqualsIgnoreCase(argv[1], L"del")) {
    for (size_t i = 2; i < argv.size(); i++) {
      ae.Delete(argv[i]);
      DbgPrint(L"wsudo alias delete: %s", argv[i]);
    }
    return 0;
  }
  if (bela::EqualsIgnoreCase(argv[1], L"add")) {
    if (argv.size() < 5) {
      bela::FPrintF(stderr, L"\x1b[31mwsudo alias add command style is:  alias target description\x1b[0m\n");
      return 1;
    }
    ae.Set(argv[2], argv[3], argv[4]);
    DbgPrint(L"wsudo alias set: %s to %s (%s)", argv[2], argv[3], argv[4]);
    return 0;
  }
  bela::FPrintF(stderr, L"\x1b[31mwsudo alias unsupported command '%s'\x1b[0m\n", argv[1]);
  return 1;
}
