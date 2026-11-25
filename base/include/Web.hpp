#ifndef BH_WEB
#define BH_WEB

#if BH_OLED
#include "Oled.hpp"
#endif
#include "esp_http_server.h"
#include "esp_netif.h"

namespace bh {

struct KitConfig;

class Web {
public:
  explicit Web(KitConfig &kit) noexcept;
  ~Web() noexcept;

  Web(const Web &) = delete;
  Web &operator=(const Web &) = delete;

#if BH_OLED
  void drawServerInfo(bh::Oled &oled) noexcept;
#else
  void drawServerInfo() noexcept;
#endif

private:
  static constexpr char m_ssid[] = "BandHero-Drumkit";
  char m_password[sizeof("BH-00000000")] = "BH-";
  char m_ip[sizeof("255.255.255.255")]{};

  ::esp_netif_t *m_apNetif;
  ::httpd_handle_t m_server;
};

} // namespace bh

#endif
