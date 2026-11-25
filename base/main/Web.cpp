#include "Web.hpp"

#include "KitConfig.hpp"
#include "Utils.hpp"
#include "cJSON.h"
#include "esp_mac.h"
#include "esp_wifi.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <sys/stat.h>

namespace bh {
namespace {

constexpr char tempPath[] = "/sdcard/config.tmp";
constexpr const char *types[] = {"Drum", "Cymbal", "Pedal"};

std::string number(float value) {
  char buf[32];
  const auto result = std::to_chars(buf, buf + sizeof(buf), value);
  return {buf, result.ptr};
}

std::string macString(const MAC &mac) {
  char buf[18];
  std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],
                mac[1], mac[2], mac[3], mac[4], mac[5]);
  return buf;
}

enum class Format { Json, Lua };

std::string quoted(std::string_view value, Format format) {
  std::string out = "\"";
  for (unsigned char c : value) {
    if (c < 32) {
      char buf[7];
      std::snprintf(buf, sizeof(buf),
                    format == Format::Lua ? "\\%03u" : "\\u%04x", c);
      out += buf;
    } else {
      if (c == '"' || c == '\\') {
        out += '\\';
      }
      out += static_cast<char>(c);
    }
  }
  return out + '"';
}

std::string padJson(const PadConfig &pad) {
  std::string out =
      "{\"type\":" +
      quoted(types[static_cast<unsigned>(pad.type)], Format::Json) +
      ",\"audio\":" + quoted(pad.audio, Format::Json) + ",\"position\":";
  if (pad.type == PadType::Pedal) {
    return out + number(pad.pedalIndex + 1) + "}";
  }
  out +=
      "{\"x\":" + number(pad.position.x) + ",\"y\":" + number(pad.position.y) +
      ",\"z\":" + number(pad.position.z) + "},\"radius\":" + number(pad.radius);
  if (pad.tilt != 0) {
    out += ",\"tilt\":" + number(pad.tilt);
  }
  if (pad.hasAlternate) {
    out += ",\"alternate_radius\":" + number(pad.alternateRadius) +
           ",\"alternate_audio\":" + quoted(pad.alternateAudio, Format::Json);
  }
  return out + "}";
}

const cJSON *field(const cJSON *obj, const char *key) {
  return cJSON_GetObjectItemCaseSensitive(obj, key);
}

// Emit only known fields and literal values. Paths are always quoted as Lua
// strings.
bool appendPad(std::string &out, const cJSON *pad) {
  const char *type = cJSON_GetStringValue(field(pad, "type"));
  if (!type ||
      std::ranges::find(types, std::string_view(type)) == std::end(types)) {
    return false;
  }
  out += "  { type = ";
  out += type;
  const auto *pos = field(pad, "position");
  out += ", position = ";
  if (std::string_view(type) == "Pedal") {
    if (!cJSON_IsNumber(pos)) {
      return false;
    }
    out += number(pos->valuedouble);
  } else {
    out += "{ ";
    for (const char *key : {"x", "y", "z"}) {
      const auto *value = field(pos, key);
      if (!cJSON_IsNumber(value) ||
          !std::isfinite(static_cast<float>(value->valuedouble))) {
        return false;
      }
      out += std::string(key) + " = " + number(value->valuedouble) + ", ";
    }
    out += "}";
  }
  for (const char *key :
       {"radius", "tilt", "alternate_radius", "audio", "alternate_audio"}) {
    const auto *value = field(pad, key);
    if (!value) {
      continue;
    }
    out += ", " + std::string(key) + " = ";
    if (cJSON_IsString(value)) {
      out += quoted(value->valuestring, Format::Lua);
    } else if (cJSON_IsNumber(value) &&
               std::isfinite(static_cast<float>(value->valuedouble))) {
      out += number(value->valuedouble);
    } else {
      return false;
    }
  }
  out += " },\n";
  return true;
}

bool replaceConfig(const std::string &source) {
  auto *file = std::fopen(tempPath, "wb");
  if (!file) {
    return false;
  }
  bool written =
      std::fwrite(source.data(), 1, source.size(), file) == source.size();
  if (std::fclose(file) != 0 || !written) {
    return false;
  }
  if (std::remove(backupPath) != 0 && errno != ENOENT) {
    return false;
  }
  bool backedUp = std::rename(configPath, backupPath) == 0;
  if (!backedUp && errno != ENOENT) {
    return false;
  }
  if (std::rename(tempPath, configPath) != 0) {
    if (backedUp) {
      std::rename(backupPath, configPath);
    }
    return false;
  }
  if (backedUp) {
    std::remove(backupPath);
  }
  return true;
}

// Accept complete MPEG Layer III frames, with optional ID3v2 and ID3v1 tags.
// The playback decoder then checks that the frame payloads produce usable PCM.
bool completeMp3(const char *path, long length) {
  File file(path, "rb");
  unsigned char header[10]{};
  if (!file || length < 10 || std::fread(header, 1, 10, file.get()) != 10) {
    return false;
  }
  long offset = 0;
  if (std::memcmp(header, "ID3", 3) == 0) {
    if (header[3] < 2 || header[3] > 4 ||
        ((header[6] | header[7] | header[8] | header[9]) & 0x80)) {
      return false;
    }
    // ID3v2 encodes its payload length in four seven-bit groups, excluding the ten-
    // byte header.
    offset = 10 + (header[6] << 21) + (header[7] << 14) + (header[8] << 7) +
             header[9];
    if (header[3] == 4 && (header[5] & 0x10)) {
      offset += 10;
    }
  }
  bool frames = false;
  while (length - offset >= 4) {
    if (std::fseek(file.get(), offset, SEEK_SET) ||
        std::fread(header, 1, 4, file.get()) != 4) {
      return false;
    }
    if (length - offset == 128 && std::memcmp(header, "TAG", 3) == 0) {
      return frames;
    }
    const int version = (header[1] >> 3) & 3;
    const int bitrate = header[2] >> 4;
    const int rate = (header[2] >> 2) & 3;
    if (header[0] != 0xff || (header[1] & 0xe6) != 0xe2 || version == 1 ||
        bitrate == 0 || bitrate == 15 || rate == 3) {
      return false;
    }
    constexpr int mpeg1[] = {0,   32,  40,  48,  56,  64,  80, 96,
                             112, 128, 160, 192, 224, 256, 320};
    constexpr int mpeg2[] = {0,  8,  16, 24,  32,  40,  48, 56,
                             64, 80, 96, 112, 128, 144, 160};
    constexpr int rates[] = {44100, 48000, 32000};
    const int sampleRate = rates[rate] >> (version == 3   ? 0
                                           : version == 2 ? 1
                                                          : 2);
    // Layer III frames span 1152 samples in MPEG-1 and 576 in MPEG-2/2.5. Padding
    // adds one byte.
    const int frameBytes =
        (version == 3 ? 144000 * mpeg1[bitrate] : 72000 * mpeg2[bitrate]) /
            sampleRate +
        ((header[2] >> 1) & 1);
    offset += frameBytes;
    frames = true;
  }
  return frames && offset == length;
}

constexpr long maxUploadBytes = 2 * 1024 * 1024;
constexpr std::string_view sampleNameChars =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
constexpr char soundsDir[] = "/sdcard/sounds";
constexpr char uploadTemp[] = "/sdcard/sounds/upload.tmp";

bool verifiedMp3(const char *path) {
  struct stat info{};
  return stat(path, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0 &&
         info.st_size <= maxUploadBytes && completeMp3(path, info.st_size) &&
         mp3File(path, 4 * 1024 * 1024).getFile() != nullptr;
}

esp_err_t uploadMp3(httpd_req_t *req) {
  if (!req->content_len || req->content_len > maxUploadBytes) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                               "MP3 must be between 1 byte and 2 MiB");
  }
  char stem[49]{};
  if (httpd_req_get_hdr_value_str(req, "X-Sample-Name", stem, sizeof(stem)) !=
          ESP_OK ||
      !stem[0] ||
      std::string_view(stem).find_first_not_of(sampleNameChars) !=
          std::string_view::npos) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                               "Invalid sample name");
  }
  if (!prepareKitStorage() || (mkdir(soundsDir, 0775) && errno != EEXIST)) {
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Cannot access sounds folder");
  }
  auto *file = std::fopen(uploadTemp, "wb");
  if (!file) {
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Cannot create upload file");
  }
  struct Cleanup {
    ~Cleanup() { std::remove(uploadTemp); }
  } cleanup;
  char chunk[2048];
  for (size_t left = req->content_len; left;) {
    int n = httpd_req_recv(req, chunk, std::min(left, sizeof(chunk)));
    if (n <= 0) {
      std::fclose(file);
      return ESP_FAIL;
    }
    if (std::fwrite(chunk, 1, n, file) != static_cast<size_t>(n)) {
      std::fclose(file);
      return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                 "SD card write failed");
    }
    left -= n;
  }
  if (std::fclose(file) != 0) {
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "SD card write failed");
  }
  if (!verifiedMp3(uploadTemp)) {
    return httpd_resp_send_err(
        req, HTTPD_400_BAD_REQUEST,
        "Expected a complete playable MP3 within the 4 MiB decoded limit");
  }
  // FatFS rename refuses an existing destination, including case-insensitive
  // matches.
  std::string base = std::string(soundsDir) + "/" + stem;
  for (int i = 0; i < 1000; ++i) {
    const auto path = base + (i ? "-" + std::to_string(i) : "") + ".mp3";
    if (std::rename(uploadTemp, path.c_str()) == 0) {
      httpd_resp_set_type(req, "text/plain");
      return httpd_resp_send(req, path.data(), path.size());
    }
    if (errno != EEXIST) {
      break;
    }
  }
  return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                             "Cannot store verified MP3");
}

esp_err_t getKit(httpd_req_t *req) {
  auto &kit = *static_cast<KitConfig *>(req->user_ctx);
  if (const char *error = kit.load()) {
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, error);
  }
  std::string json = "{\"tolerance\":" + number(kit.tolerance) +
                     ",\"macAddress\":\"" + macString(kit.macAddress) +
                     "\",\"pads\":[";
  for (const auto &pad : kit.pads) {
    if (&pad != &kit.pads.front()) {
      json += ',';
    }
    json += padJson(pad);
  }
  json += "]}";
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, json.data(), json.size());
}

esp_err_t putKit(httpd_req_t *req) {
  if (!req->content_len || req->content_len > 128 * 1024) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid kit size");
  }
  std::string body(req->content_len, '\0');
  for (size_t n = 0; n < body.size();) {
    int count = httpd_req_recv(req, body.data() + n, body.size() - n);
    if (count <= 0) {
      return ESP_FAIL;
    }
    n += count;
  }
  // cJSON exposes NUL-terminated strings. Reject embedded NUL escapes before
  // decoding.
  for (size_t i = 0; i < body.size(); ++i) {
    if (body[i] == '\\' && body.compare(++i, 5, "u0000") == 0) {
      return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                 "Invalid NUL in JSON string");
    }
  }
  const char *end = nullptr;
  std::unique_ptr<cJSON, decltype(&cJSON_Delete)> kit(
      cJSON_ParseWithLengthOpts(body.c_str(), body.size() + 1, &end, true),
      cJSON_Delete);
  if (!kit || end != body.data() + body.size()) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
  }
  const auto *pads = field(kit.get(), "pads");
  const auto *tolerance = field(kit.get(), "tolerance");
  const char *mac = cJSON_GetStringValue(field(kit.get(), "macAddress"));
  MAC parsed{};
  if (!cJSON_IsArray(pads) || cJSON_GetArraySize(pads) > 30 || !mac ||
      !parseHubMac(mac, parsed) || !cJSON_IsNumber(tolerance) ||
      !std::isfinite(static_cast<float>(tolerance->valuedouble)) ||
      tolerance->valuedouble < 0) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                               "Invalid kit settings");
  }
  std::string source = "tolerance = " + number(tolerance->valuedouble) +
                       "\nmacAddress = \"" + macString(parsed) +
                       "\"\nhit = {\n";
  for (auto *pad = pads->child; pad; pad = pad->next) {
    if (!appendPad(source, pad)) {
      return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                 "Invalid pad fields");
    }
  }
  source += "}\n";
  kit.reset();
  std::unique_ptr<lua_State, decltype(&lua_close)> lua(luaL_newstate(),
                                                       lua_close);
  if (!lua) {
    return httpd_resp_send_500(req);
  }
  registerPadTypes(lua.get());
  KitConfig updated;
  if (luaL_dostring(lua.get(), source.c_str()) != LUA_OK) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                               "Invalid pad values");
  }
  if (const char *error = updated.read(lua.get())) {
    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, error);
  }
  if (!prepareKitStorage()) {
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Cannot access SD card");
  }
  auto &active = *static_cast<KitConfig *>(req->user_ctx);
  // Existing SD kits can reference their original paths. New assignments must
  // resolve to a playable upload, even when a client bypasses the editor.
  std::vector<std::string_view> known;
  for (const auto &pad : active.pads) {
    known.push_back(pad.audio);
    if (pad.hasAlternate) {
      known.push_back(pad.alternateAudio);
    }
  }
  for (const auto &pad : updated.pads) {
    for (const auto *path : {&pad.audio, &pad.alternateAudio}) {
      if (path->empty() || std::ranges::find(known, *path) != known.end()) {
        continue;
      }
      constexpr std::string_view prefix = "/sdcard/sounds/";
      const std::string_view name = *path;
      if (!name.starts_with(prefix) || !name.ends_with(".mp3") ||
          name.size() <= prefix.size() + 4 ||
          name.substr(prefix.size(), name.size() - prefix.size() - 4)
                  .find_first_not_of(sampleNameChars) !=
              std::string_view::npos ||
          !verifiedMp3(path->c_str())) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                                   "Upload a verified MP3 for each new sound");
      }
      known.push_back(name);
    }
  }
  if (!replaceConfig(source)) {
    return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                               "Cannot save config.lua to SD card");
  }
  active = std::move(updated);
  return httpd_resp_send(req, nullptr, 0);
}
} // namespace
static constexpr unsigned char faviconSvg[]{
#embed "web/favicon.svg"
};

static constexpr unsigned char indexHtml[]{
#embed "web/index.html"
};

static constexpr unsigned char appJs[]{
#embed "web/app.js"
};

static constexpr unsigned char styleCss[]{
#embed "web/style.css"
};

template <auto &data>
static ::esp_err_t sendAsset(::httpd_req_t *req,
                             const char *mimeType) noexcept {
  auto result = ::httpd_resp_set_type(req, mimeType);
  if (result != ESP_OK) {
    return result;
  }
  return ::httpd_resp_send(req, reinterpret_cast<const char *>(data),
                           sizeof(data));
}

bh::Web::Web(KitConfig &kit) noexcept {
  m_apNetif = ::esp_netif_create_default_wifi_ap();

  ::wifi_init_config_t wifiInit = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(::esp_wifi_init(&wifiInit));

  bh::MAC macAddress;
  ESP_ERROR_CHECK(::esp_read_mac(macAddress.data(), ::ESP_MAC_EFUSE_FACTORY));
  const auto suffix = MACToBase64(macAddress);

  constexpr auto prefixLength = sizeof("BH-") - 1;
  static_assert(prefixLength + suffix.size() + 1 == sizeof(m_password));
  std::memcpy(m_password + prefixLength, suffix.data(), suffix.size());

  ::wifi_config_t config{};
  static_assert(sizeof(m_ssid) <= sizeof(config.ap.ssid));
  static_assert(sizeof(m_password) <= sizeof(config.ap.password));

  std::memcpy(config.ap.ssid, m_ssid, sizeof(m_ssid));

  config.ap.ssid_len = sizeof(m_ssid) - 1;

  std::memcpy(config.ap.password, m_password, sizeof(m_password));

  config.ap.channel = 1;
  config.ap.max_connection = 4;
  config.ap.authmode = ::WIFI_AUTH_WPA_WPA2_PSK;

  ESP_ERROR_CHECK(::esp_wifi_set_mode(::WIFI_MODE_AP));
  ESP_ERROR_CHECK(::esp_wifi_set_config(::WIFI_IF_AP, &config));
  ESP_ERROR_CHECK(::esp_wifi_start());

  ::esp_netif_ip_info_t ipInfo{};
  ESP_ERROR_CHECK(::esp_netif_get_ip_info(m_apNetif, &ipInfo));
  ESP_ERROR_CHECK(
      ::esp_ip4addr_ntoa(&ipInfo.ip, m_ip, sizeof(m_ip)) ? ESP_OK : ESP_FAIL);

  ::httpd_config_t serverConfig = HTTPD_DEFAULT_CONFIG();
#if CONFIG_FREERTOS_NUMBER_OF_CORES > 1
  serverConfig.core_id = 1;
#endif
  serverConfig.stack_size = 16384;
  ESP_ERROR_CHECK(::httpd_start(&m_server, &serverConfig));

  auto registerRoute = [this, &kit](const char *uri, httpd_method_t method,
                                    ::esp_err_t (*handler)(::httpd_req_t *)) {
    ::httpd_uri_t route{};
    route.uri = uri;
    route.user_ctx = &kit;
    route.method = method;
    route.handler = handler;
    ESP_ERROR_CHECK(::httpd_register_uri_handler(m_server, &route));
  };
  registerRoute("/", HTTP_GET, [](::httpd_req_t *req) {
    return sendAsset<indexHtml>(req, "text/html");
  });
  registerRoute("/favicon.svg", HTTP_GET, [](::httpd_req_t *req) {
    return sendAsset<faviconSvg>(req, "image/svg+xml");
  });
  registerRoute("/style.css", HTTP_GET, [](::httpd_req_t *req) {
    return sendAsset<styleCss>(req, "text/css");
  });
  registerRoute("/app.js", HTTP_GET, [](::httpd_req_t *req) {
    return sendAsset<appJs>(req, "application/javascript");
  });
  registerRoute("/api/kit", HTTP_GET, getKit);
  registerRoute("/api/kit", HTTP_PUT, putKit);

  registerRoute("/api/samples", HTTP_POST, uploadMp3);
}

bh::Web::~Web() noexcept {
  ESP_ERROR_CHECK_WITHOUT_ABORT(::httpd_stop(m_server));
  ESP_ERROR_CHECK_WITHOUT_ABORT(::esp_wifi_stop());
  ::esp_netif_destroy_default_wifi(m_apNetif);
  ESP_ERROR_CHECK_WITHOUT_ABORT(::esp_wifi_deinit());
}

#if BH_OLED
void bh::Web::drawServerInfo(bh::Oled &oled) noexcept {
  oled.drawWeb(m_ssid, m_password, m_ip);
}
#else
void bh::Web::drawServerInfo() noexcept {
  std::printf("SSID: %s\nPASS: %s\nIP: %s\n", m_ssid, m_password, m_ip);
}
#endif

} // namespace bh
