#include <filesystem>

#include "Utils.hpp"

#include "esp_heap_caps.h"
#include "esp_psram.h"

namespace bh {

void IRAM_ATTR modeButtonPressed(void *args) noexcept {
  if (args != nullptr && mode_button_debounce.accept()) {
    static_cast<bh::AtomicState *>(args)->store(bh::State::Config,
                                              std::memory_order_relaxed);
  }
}

bh::mp3File::mp3File(std::string_view file) noexcept {
  std::error_code ec;
  m_size = std::filesystem::file_size(file.data(), ec);
  if (ec) {
    m_size = 0;
    return;
  }
  /*
  m_file = static_cast<std::uint16_t *>(
      ::heap_caps_malloc(m_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
*/
}

bh::mp3File::~mp3File() noexcept {
  if (m_file) {
    ::heap_caps_free(m_file);
  }
}

} // namespace bh
