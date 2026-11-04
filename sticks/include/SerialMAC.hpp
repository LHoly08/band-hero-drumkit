#ifndef BH_SERIAL_MAC
#define BH_SERIAL_MAC

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace bh {

// Accept compact MACs immediately; a newline resets incomplete/invalid input.
class SerialMAC {
public:
  using Address = std::array<std::uint8_t, 6>;

  std::optional<Address> push(char ch) noexcept {
    if (ch == '\r' || ch == '\n') {
      m_digits = 0;
      m_invalid = false;
      return std::nullopt;
    }
    if (m_invalid) {
      return std::nullopt;
    }
    const int nibble = ch >= '0' && ch <= '9' ? ch - '0'
                       : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
                       : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10
                                               : -1;
    if (nibble < 0) {
      m_invalid = true;
      return std::nullopt;
    }
    auto &byte = m_address[m_digits / 2];
    if (m_digits % 2 == 0) {
      byte = static_cast<std::uint8_t>(nibble << 4);
    } else {
      byte |= static_cast<std::uint8_t>(nibble);
    }
    if (++m_digits == 12) {
      m_digits = 0;
      return m_address;
    }
    return std::nullopt;
  }

private:
  Address m_address{};
  std::size_t m_digits{};
  bool m_invalid{};
};

} // namespace bh

#endif
