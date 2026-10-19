#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

#include "serial/serialib.h"

struct Position {
  float x;
  float y;
  float z;
};

// Example targets on the 27.5 cm sphere; replace with your HitXYZ! logs.
// Meters: X right, Y forward, Z up.
constexpr Position drum{0.0f, 0.275f, 0.0f};
constexpr Position cymbal{-0.275f, 0.0f, 0.0f};
constexpr Position kick{0.275f, 0.0f, 0.0f};
constexpr float tolerance = 0.10f; // Inclusive 3D distance from target (5 cm).
static_assert(tolerance >= 0.0f);

struct Target {
  std::string_view name;
  Position position;
};
constexpr std::array targets{Target{"drum", drum}, Target{"cymbal", cymbal},
                             Target{"kick", kick}};

std::optional<Position> parse_hit(const std::string &line) {
  constexpr std::string_view marker = "HitXYZ!";
  const auto start = line.find(marker);
  if (start == std::string::npos)
    return std::nullopt;
  std::istringstream input(line.substr(start + marker.size()));
  Position result{};
  unsigned seen = 0;
  for (int i = 0; i < 3; ++i) {
    std::string label;
    float value;
    if (!(input >> label >> value) || !std::isfinite(value))
      return std::nullopt;
    unsigned bit;
    if (label == "X:") {
      result.x = value;
      bit = 1;
    } else if (label == "Y:") {
      result.y = value;
      bit = 2;
    } else if (label == "Z:") {
      result.z = value;
      bit = 4;
    } else
      return std::nullopt;
    if (seen & bit)
      return std::nullopt;
    seen |= bit;
  }
  std::string suffix;
  std::getline(input >> std::ws, suffix);
  if (!suffix.empty() && suffix != "(m)")
    return std::nullopt;
  return result;
}

void process_line(const std::string &line) {
  const auto hit = parse_hit(line);
  if (!hit)
    return; // Ignore boot messages and incomplete/malformed hits.
  std::string_view name = "unmatched";
  double best = std::numeric_limits<double>::infinity();
  for (const auto &target : targets) {
    const double x = double(hit->x) - target.position.x;
    const double y = double(hit->y) - target.position.y;
    const double z = double(hit->z) - target.position.z;
    const double score = x * x + y * y + z * z;
    if (score <= double(tolerance) * tolerance && score < best) {
      name = target.name;
      best = score;
    }
  }
  std::cout << name << std::endl;
}

int main(int argc, char **argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: " << argv[0] << " PORT [BAUD]\n"
              << "       " << argv[0] << " --stdin\n";
    return 1;
  }
  if (std::string_view(argv[1]) == "--stdin") {
    std::string line;
    while (std::getline(std::cin, line)) {
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      process_line(line);
    }
    return 0;
  }
  unsigned long baud = 115200;
  if (argc == 3) {
    try {
      const std::string value(argv[2]);
      std::size_t end;
      baud = std::stoul(value, &end);
      if (end != value.size() || value.front() == '-' || baud == 0 ||
          baud > std::numeric_limits<unsigned int>::max())
        throw 0;
    } catch (...) {
      std::cerr << "Invalid baud rate\n";
      return 1;
    }
  }
  serialib serial;
  const int opened =
      serial.openDevice(argv[1], static_cast<unsigned int>(baud));
  if (opened != 1) {
    std::cerr << "Cannot open " << argv[1] << " (serialib error " << opened
              << ")\n";
    return 1;
  }
  std::cerr << "Listening on " << argv[1] << " at " << baud << " baud\n";
  std::string line;
  bool discard = false;
  for (;;) {
    const int available = serial.available();
    if (available < 0) {
      std::cerr << "Serial port error\n";
      return 1;
    }
    if (available == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    char ch;
    const int read = serial.readChar(&ch, 10);
    if (read < 0) {
      std::cerr << "Serial read failed\n";
      return 1;
    }
    if (read == 0)
      continue;
    if (ch == '\n') {
      if (!discard)
        process_line(line);
      line.clear();
      discard = false;
    } else if (ch != '\r' && !discard) {
      if (line.size() >= 4096) {
        line.clear();
        discard = true;
      } else
        line += ch;
    }
  }
}
