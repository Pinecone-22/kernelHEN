// SPDX-License-Identifier: GPL-3.0-or-later
#include "port_updater.hpp"
#include "port_safety.hpp"
#include "common_utils.h"
#include <json.hpp>

#include <curl/curl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr const char* kApiReleases = "https://api.github.com/repos/Pinecone-22/etaHEN-/releases/latest";
constexpr const char* kManifest = "https://raw.githubusercontent.com/Pinecone-22/etaHEN-/main/update.json";
constexpr const char* kStaging = "/data/etaHENbin/etaHEN.update.elf";
constexpr const char* kUserAgent = "etaHEN-updater/1.0";

size_t append_cb(void* contents, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  out->append(static_cast<const char*>(contents), size * nmemb);
  return size * nmemb;
}

bool http_get(const char* url, std::string& body, long& code, std::string& error) {
  body.clear();
  code = 0;
  CURL* curl = curl_easy_init();
  if (!curl) {
    error = "curl init failed";
    return false;
  }

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
  headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 25L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

  CURLcode res = curl_easy_perform(curl);
  if (res == CURLE_OK) {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
  } else {
    error = curl_easy_strerror(res);
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
  return res == CURLE_OK;
}

uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

void sha256_block(uint32_t state[8], const uint8_t block[64]) {
  static const uint32_t k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
  };

  uint32_t w[64];
  for (int i = 0; i < 16; i++) {
    w[i] = (uint32_t(block[i * 4]) << 24) | (uint32_t(block[i * 4 + 1]) << 16) |
           (uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
  }
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

  for (int i = 0; i < 64; i++) {
    uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = h + s1 + ch + k[i] + w[i];
    uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = s0 + maj;
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2;
  }

  state[0] += a; state[1] += b; state[2] += c; state[3] += d;
  state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

std::string sha256_hex(const uint8_t* data, size_t length) {
  uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint64_t bits = uint64_t(length) * 8;
  size_t full = length / 64;
  for (size_t i = 0; i < full; i++) sha256_block(state, data + i * 64);

  uint8_t tail[128]{};
  size_t rem = length % 64;
  memcpy(tail, data + full * 64, rem);
  tail[rem] = 0x80;
  size_t tail_len = rem < 56 ? 64 : 128;
  for (int i = 0; i < 8; i++) tail[tail_len - 1 - i] = uint8_t(bits >> (i * 8));
  sha256_block(state, tail);
  if (tail_len == 128) sha256_block(state, tail + 64);

  static const char* hex = "0123456789abcdef";
  std::string out;
  out.reserve(64);
  for (uint32_t word : state) {
    for (int shift = 28; shift >= 0; shift -= 4) out += hex[(word >> shift) & 0xf];
  }
  return out;
}

bool sha256_file(const char* path, std::string& digest) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) return false;

  std::vector<uint8_t> data;
  uint8_t buffer[16384];
  ssize_t n;
  while ((n = read(fd, buffer, sizeof(buffer))) > 0) data.insert(data.end(), buffer, buffer + n);
  close(fd);
  digest = sha256_hex(data.data(), data.size());
  return true;
}

bool is_elf_file(const char* path) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) return false;
  uint8_t h[4]{};
  bool ok = read(fd, h, 4) == 4 && h[0] == 0x7f && h[1] == 'E' && h[2] == 'L' && h[3] == 'F';
  close(fd);
  return ok;
}

bool parse_asset(const nlohmann::json& doc, PortUpdateInfo& info, std::string& error) {
  if (!doc.is_object()) {
    error = "invalid GitHub response";
    return false;
  }
  if (doc.contains("tag_name") && doc["tag_name"].is_string()) info.version = doc["tag_name"].get<std::string>();
  if (doc.contains("body") && doc["body"].is_string()) info.notes = doc["body"].get<std::string>();
  if (!doc.contains("assets") || !doc["assets"].is_array()) {
    error = "release has no assets";
    return false;
  }

  const nlohmann::json* chosen = nullptr;
  for (const auto& asset : doc["assets"]) {
    if (!asset.is_object() || !asset.contains("name") || !asset["name"].is_string()) continue;
    std::string name = asset["name"].get<std::string>();
    std::string lower = name;
    for (char& c : lower) c = char(tolower(uint8_t(c)));
    if (lower.size() < 4) continue;
    if (lower.find(".elf") == std::string::npos && lower.find(".bin") == std::string::npos) continue;
    if (lower.find("etahen") != std::string::npos) { chosen = &asset; break; }
    if (!chosen) chosen = &asset;
  }

  if (!chosen) {
    error = "release has no etaHEN ELF asset";
    return false;
  }

  if (!chosen->contains("browser_download_url") || !(*chosen)["browser_download_url"].is_string()) {
    error = "asset has no download URL";
    return false;
  }
  info.url = (*chosen)["browser_download_url"].get<std::string>();
  if (chosen->contains("size") && (*chosen)["size"].is_number()) info.size = (*chosen)["size"].get<long long>();
  if ((*chosen).contains("digest") && (*chosen)["digest"].is_string()) {
    std::string digest = (*chosen)["digest"].get<std::string>();
    if (digest.rfind("sha256:", 0) == 0) digest = digest.substr(7);
    info.sha256 = digest;
  }
  if (info.version.empty()) info.version = "unknown";
  info.available = info.version != info.local_version;
  return true;
}

bool parse_manifest(const std::string& body, PortUpdateInfo& info, std::string& error) {
  auto doc = nlohmann::json::parse(body, nullptr, false);
  if (doc.is_discarded() || !doc.is_object()) {
    error = "invalid update manifest";
    return false;
  }
  if (doc.contains("version") && doc["version"].is_string()) info.version = doc["version"].get<std::string>();
  if (doc.contains("url") && doc["url"].is_string()) info.url = doc["url"].get<std::string>();
  if (doc.contains("notes") && doc["notes"].is_string()) info.notes = doc["notes"].get<std::string>();
  if (doc.contains("sha256") && doc["sha256"].is_string()) info.sha256 = doc["sha256"].get<std::string>();
  if (doc.contains("size") && doc["size"].is_number()) info.size = doc["size"].get<long long>();
  if (info.url.empty() || info.version.empty()) {
    error = "manifest is missing version or url";
    return false;
  }
  info.available = info.version != info.local_version;
  return true;
}

}

bool port_update_check(PortUpdateInfo& info, std::string& error) {
  error.clear();
  info = PortUpdateInfo{};

  std::string body;
  long code = 0;
  if (http_get(kApiReleases, body, code, error) && code == 200) {
    auto doc = nlohmann::json::parse(body, nullptr, false);
    if (!doc.is_discarded() && parse_asset(doc, info, error)) return true;
  }

  error.clear();
  if (http_get(kManifest, body, code, error) && code == 200) {
    if (parse_manifest(body, info, error)) return true;
  }

  if (error.empty()) error = "no update channel available";
  return false;
}

bool port_update_install(const PortUpdateInfo& info, std::string& error) {
  error.clear();
  if (info.url.empty()) {
    error = "no download URL";
    return false;
  }

  port_safety::mkdir_p(port_safety::kBinRoot);
  if (!download_file(info.url.c_str(), kStaging)) {
    error = "download failed";
    unlink(kStaging);
    return false;
  }

  struct stat st{};
  if (stat(kStaging, &st) != 0 || st.st_size < 65536) {
    error = "downloaded file is too small";
    unlink(kStaging);
    return false;
  }
  if (info.size > 0 && st.st_size != info.size) {
    error = "downloaded size does not match the release";
    unlink(kStaging);
    return false;
  }
  if (!is_elf_file(kStaging)) {
    error = "downloaded file is not an ELF";
    unlink(kStaging);
    return false;
  }
  if (!info.sha256.empty()) {
    std::string digest;
    if (!sha256_file(kStaging, digest) || digest != info.sha256) {
      error = "SHA-256 mismatch";
      unlink(kStaging);
      return false;
    }
  }

  if (port_safety::exists(port_safety::kEtahenElf)) {
    port_safety::copy_file_atomic(port_safety::kEtahenElf, port_safety::kPreviousElf, true, true);
  }

  port_safety::mkdir_p("/user/data/etaHEN");

  if (!port_safety::copy_file_atomic(kStaging, port_safety::kEtahenElf, true, true) ||
      !port_safety::copy_file_atomic(kStaging, port_safety::kEtahenBin, true, true) ||
      !port_safety::copy_file_atomic(kStaging, "/data/etaHEN/etaHEN.bin", true, true)) {
    error = "failed to install the update";
    return false;
  }

  port_safety::copy_file_atomic(kStaging, "/user/data/etaHEN/etaHEN.bin", true, true);

  port_safety::write_text_atomic(port_safety::kUpdatePending, info.version + "\n" + info.url + "\n");
  unlink(kStaging);
  port_safety::append_log(port_safety::kCrashLog, "update", ("installed " + info.version).c_str());
  return true;
}
