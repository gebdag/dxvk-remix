#include "rtx_dark_souls_light_falloff.h"
#include "rtx_lights.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace dxvk {

  namespace {
    struct Falloff {
      float begin;
      float end;
    };

    // The parsed table and the text it came from: the option changes when the proxy's set of
    // listed lights does, far less often than this runs.
    std::string s_parsedText;
    std::unordered_map<uint64_t, Falloff> s_table;

    void parse(const std::string& text) {
      s_parsedText = text;
      s_table.clear();
      const char* p = text.c_str();
      while (*p) {
        char* next = nullptr;
        const uint64_t hash = std::strtoull(p, &next, 16);
        if (next == p || *next != ':') {
          break;
        }
        p = next + 1;
        const float begin = std::strtof(p, &next);
        if (next == p || *next != ':') {
          break;
        }
        p = next + 1;
        const float end = std::strtof(p, &next);
        if (next == p) {
          break;
        }
        p = next;
        if (end > begin && end > 0.0f) {
          s_table[hash] = Falloff { std::max(begin, 0.0f), end };
        }
        while (*p == ';' || *p == ' ') {
          ++p;
        }
      }
    }
  }

  void DxvkDarkSoulsLightFalloff::apply(uint64_t handle, RtLight& light) {
    const std::string& text = falloffTable();
    if (text != s_parsedText) {
      parse(text);
    }
    if (auto found = s_table.find(handle); found != s_table.end()) {
      light.setDs1Falloff(found->second.begin, found->second.end);
      return;
    }
    light.setDs1Falloff(0.0f, 0.0f);
  }

} // namespace dxvk
