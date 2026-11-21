#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

namespace bh::SerialMAC {

using Address = std::array<std::uint8_t, 6>;

inline std::optional<Address> parse(std::string_view field) noexcept {
  Address address{};
  if (field.size() != 2 * address.size()) {
    return std::nullopt;
  }
  for (auto &byte : address) {
    const char *end = field.data() + 2;
    const auto result = std::from_chars(field.data(), end, byte, 16);
    if (result.ptr != end || result.ec != std::errc{}) {
      return std::nullopt;
    }
    field.remove_prefix(2);
  }
  return address;
}

} // namespace bh::SerialMAC
