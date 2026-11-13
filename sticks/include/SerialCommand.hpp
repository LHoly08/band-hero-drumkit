#pragma once

#include <cmath>
#include <cstddef>
#include <cstdlib>

#include "SerialMAC.hpp"

namespace bh {

struct SerialCommand {
  enum class Kind { None, Receiver, Radius, Invalid };
  Kind kind{Kind::None};
  bh::SerialMAC::Address address{};
  float radius_meters{};
};

// Commands are complete lines: M <12 hex digits> or R <positive decimal cm>.
// Preserve partial input across reads and discard malformed/oversized lines.
class SerialCommandReader {
public:
  SerialCommand push(char ch) noexcept {
    if (ch == '\r' || ch == '\n') {
      m_line[m_size] = '\0';
      const auto command = m_discard ? SerialCommand{SerialCommand::Kind::Invalid}
                                    : parse();
      m_size = 0;
      m_discard = false;
      return command;
    }
    if (ch == '\0' || m_size == sizeof(m_line) - 1) {
      m_discard = true;
    }
    if (!m_discard) {
      m_line[m_size++] = ch;
    }
    return {};
  }

private:
  SerialCommand parse() const noexcept {
    using Kind = SerialCommand::Kind;
    if (m_size == 0) {
      return {}; // Also consumes the LF after a CRLF terminator.
    }
    if (m_size < 3 || m_line[1] != ' ') {
      return {Kind::Invalid};
    }
    if (m_line[0] == 'M' || m_line[0] == 'm') {
      if (m_size != 14) {
        return {Kind::Invalid};
      }
      bh::SerialMAC parser;
      for (std::size_t i = 2; i < m_size; ++i) {
        if (const auto address = parser.push(m_line[i])) {
          return {Kind::Receiver, *address};
        }
      }
      return {Kind::Invalid};
    }
    if (m_line[0] == 'R' || m_line[0] == 'r') {
      bool digit = false;
      bool decimal_point = false;
      for (std::size_t i = 2; i < m_size; ++i) {
        if (m_line[i] >= '0' && m_line[i] <= '9') {
          digit = true;
        } else if (m_line[i] == '.' && !decimal_point) {
          decimal_point = true;
        } else {
          return {Kind::Invalid};
        }
      }
      if (digit) {
        const float meters = std::strtof(m_line + 2, nullptr) / 100.0f;
        if (std::isfinite(meters) && meters > 0.0f) {
          return {Kind::Radius, {}, meters};
        }
      }
    }
    return {Kind::Invalid};
  }

  char m_line[32]{};
  std::size_t m_size{};
  bool m_discard{};
};

} // namespace bh
