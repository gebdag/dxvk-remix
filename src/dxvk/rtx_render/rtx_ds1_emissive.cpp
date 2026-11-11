#include "rtx_ds1_emissive.h"

#include "rtx_context.h"
#include "rtx_debug_view.h"
#include "rtx_imgui.h"
#include "rtx_objectpicking.h"
#include "../dxvk_device.h"
#include "../dxvk_objects.h"
#include "../imgui/imgui.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace dxvk {

  namespace {
    namespace fs = std::filesystem;
    using Clock = std::chrono::steady_clock;

    const fs::path kFile = "rtx_comp/emissive_scales.txt";
    // The Texture Lab helper's index of game texture names, when it is there (a development machine): names instead of hashes.
    const fs::path kNames = "rtx_comp/texlab/hash_files.json";
    constexpr float kMaxScale = 32.0f;
    constexpr float kSame = 0.0005f;        // the file keeps four decimals
    constexpr uint32_t kShownFrames = 90;   // a material stays in the list this long after its last draw
    constexpr float kFlashScale = 6.0f;

    struct Entry {
      float live = 1.0f;
      float saved = 1.0f;
      DxvkDs1Emissive::RenderAs liveAs = DxvkDs1Emissive::kAsDrawn;
      DxvkDs1Emissive::RenderAs savedAs = DxvkDs1Emissive::kAsDrawn;
      std::string note;

      bool edited() const { return std::abs(live - saved) >= kSame || liveAs != savedAs; }
      bool isDefault() const {
        return std::abs(live - 1.0f) < kSame && std::abs(saved - 1.0f) < kSame && liveAs == DxvkDs1Emissive::kAsDrawn && savedAs == DxvkDs1Emissive::kAsDrawn;
      }
      bool written() const { return std::abs(live - 1.0f) >= kSame || liveAs != DxvkDs1Emissive::kAsDrawn; }
    };

    // The words of the file for each RenderAs, and the menu's names.
    const char* const kAsWords[] = { "", "unlit", "selflit" };
    const char* const kAsNames[] = { "As drawn", "Unlit", "Self-lit" };

    struct Seen {
      uint32_t frame = 0;
      DxvkDs1Emissive::Kind kind = DxvkDs1Emissive::kMaterial;
    };

    std::mutex g_mutex;
    std::unordered_map<XXH64_hash_t, Entry> g_entries;   // only those with a live or saved scale other than 1
    std::unordered_map<XXH64_hash_t, Seen> g_seen;
    // How many textures are drawn other than as drawn now: none, and renderAs needs no lock (it is asked for every draw).
    std::atomic<int> g_renderAsCount { 0 };
    fs::file_time_type g_time {};
    bool g_loaded = false;
    std::atomic<XXH64_hash_t> g_picked { 0 };
    std::atomic<XXH64_hash_t> g_flash { 0 };
    std::atomic<uint32_t> g_flashFrame { 0 };   // the frame the row was last hovered: the blink ends when the menu goes away under the mouse
    constexpr uint32_t kFlashFrames = 30;
    bool g_picking = false;
    std::string g_status;

    uint32_t g_uiFrame = 0;   // the device's frame as the menu last saw it (the ImGui thread's)

    std::string hexHash(XXH64_hash_t hash) {
      char text[17];
      snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(hash));
      return text;
    }

    void tip(const char* text) {
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", text);
      }
    }

    const std::unordered_map<XXH64_hash_t, std::string>& textureNames() {
      static std::unordered_map<XXH64_hash_t, std::string> names;
      static bool tried = false;
      if (tried) {
        return names;
      }
      tried = true;
      std::ifstream in(kNames);
      const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      std::vector<std::string> strings;
      for (size_t open = text.find('"'); open != std::string::npos;) {
        size_t close = open + 1;
        while (close < text.size() && text[close] != '"') {
          close += text[close] == '\\' ? 2 : 1;
        }
        strings.push_back(text.substr(open + 1, close - open - 1));
        open = close < text.size() ? text.find('"', close + 1) : std::string::npos;
      }
      for (size_t i = 0; i + 1 < strings.size(); i += 2) {
        const std::string& path = strings[i + 1];
        const size_t slash = path.find_last_of("\\/");
        std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        const size_t dot = name.rfind('.');
        names[std::strtoull(strings[i].c_str(), nullptr, 16)] = dot == std::string::npos ? name : name.substr(0, dot);
      }
      return names;
    }

    // The texture's name where known, else what the file noted for it, else nothing.
    std::string nameOf(XXH64_hash_t hash, const std::string& note) {
      const auto found = textureNames().find(hash);
      return found != textureNames().end() ? found->second : note;
    }

    // Needs g_mutex.
    void recount() {
      int count = 0;
      for (const auto& [hash, entry] : g_entries) {
        count += entry.liveAs != DxvkDs1Emissive::kAsDrawn ? 1 : 0;
      }
      g_renderAsCount.store(count);
    }

    // Needs g_mutex.
    void readFile() {
      std::error_code ec;
      g_time = fs::last_write_time(kFile, ec);
      // Unsaved edits stay on top of what the file now says, a Reset to 1 among them.
      std::unordered_map<XXH64_hash_t, std::pair<float, DxvkDs1Emissive::RenderAs>> edits;
      for (auto& [hash, entry] : g_entries) {
        if (entry.edited()) {
          edits[hash] = { entry.live, entry.liveAs };
        }
      }
      g_entries.clear();
      std::ifstream in(kFile);
      std::string line;
      while (std::getline(in, line)) {
        char* end = nullptr;
        const unsigned long long hash = std::strtoull(line.c_str(), &end, 16);
        char* scaleEnd = end;
        const float scale = end != line.c_str() ? std::strtof(end, &scaleEnd) : -1.0f;
        if (hash == 0 || scaleEnd == end || !(scale >= 0.0f)) {
          continue;
        }
        Entry& entry = g_entries[hash];
        entry.saved = std::min(scale, kMaxScale);
        entry.live = entry.saved;
        std::string note = scaleEnd;
        // An optional word before the note: how the texture is drawn.
        const size_t word = note.find_first_not_of(" \t");
        for (int as = 1; as <= 2 && word != std::string::npos; ++as) {
          const size_t length = strlen(kAsWords[as]);
          if (note.compare(word, length, kAsWords[as]) == 0 && (word + length == note.size() || note[word + length] == ' ' || note[word + length] == '\t' || note[word + length] == '\r')) {
            entry.savedAs = entry.liveAs = static_cast<DxvkDs1Emissive::RenderAs>(as);
            note = note.substr(word + length);
            break;
          }
        }
        const size_t first = note.find_first_not_of(" \t#");
        const size_t last = note.find_last_not_of(" \t\r\n");
        entry.note = first == std::string::npos ? std::string() : note.substr(first, last - first + 1);
      }
      for (const auto& [hash, live] : edits) {
        g_entries[hash].live = live.first;
        g_entries[hash].liveAs = live.second;
      }
      for (auto it = g_entries.begin(); it != g_entries.end();) {
        it = it->second.isDefault() ? g_entries.erase(it) : std::next(it);
      }
      recount();
    }

    // Needs g_mutex. Writes every live value; they become the saved ones.
    bool writeFile() {
      std::error_code ec;
      fs::create_directories(kFile.parent_path(), ec);
      fs::path temp = kFile;
      temp += ".tmp";
      {
        std::ofstream out(temp, std::ios::trunc);
        out << "# Emission scale per game texture (Alt+X > Developer Settings Menu > Dark Souls > Emissive per texture).\n"
               "# texture hash, scale (1 = as it was, 0 = no glow), optionally unlit or selflit (how it is drawn), the texture's name as a note\n";
        std::vector<XXH64_hash_t> order;
        for (const auto& [hash, entry] : g_entries) {
          if (entry.written()) {
            order.push_back(hash);
          }
        }
        std::sort(order.begin(), order.end());
        for (const XXH64_hash_t hash : order) {
          const Entry& entry = g_entries[hash];
          char line[96];
          snprintf(line, sizeof(line), "%016llX %.4f%s%s", static_cast<unsigned long long>(hash), entry.live,
                   entry.liveAs != DxvkDs1Emissive::kAsDrawn ? " " : "", kAsWords[entry.liveAs]);
          const std::string name = nameOf(hash, entry.note);
          out << line << (name.empty() ? "" : "  # ") << name << "\n";
        }
        if (!out.good()) {
          return false;
        }
      }
      fs::rename(temp, kFile, ec);
      if (ec) {
        return false;
      }
      g_time = fs::last_write_time(kFile, ec);
      for (auto it = g_entries.begin(); it != g_entries.end();) {
        it->second.saved = it->second.live;
        it->second.savedAs = it->second.liveAs;
        it->second.note = nameOf(it->first, it->second.note);
        it = it->second.isDefault() ? g_entries.erase(it) : std::next(it);
      }
      recount();
      return true;
    }

    // Needs g_mutex.
    void setLive(XXH64_hash_t hash, float scale) {
      // An entry at 1 with nothing saved stays until Save or Revert All: a drag passing through 1 must not lose its row.
      g_entries[hash].live = std::clamp(scale, 0.0f, kMaxScale);
    }

    // Needs g_mutex.
    void setLiveAs(XXH64_hash_t hash, DxvkDs1Emissive::RenderAs as) {
      g_entries[hash].liveAs = as;
      recount();
    }

    float liveOf(XXH64_hash_t hash) {
      const auto found = g_entries.find(hash);
      return found == g_entries.end() ? 1.0f : found->second.live;
    }

    const char* kindText(DxvkDs1Emissive::Kind kind) {
      switch (kind) {
      case DxvkDs1Emissive::kAdditive: return "additive layer";
      case DxvkDs1Emissive::kUnlit: return "unlit sprite";
      case DxvkDs1Emissive::kSelfLit: return "self-lit";
      default: return "material's emission";
      }
    }

    // One row: the texture, what it is, its scale. Hovering the row flashes the texture in the game.
    void row(XXH64_hash_t hash, const char* what) {
      ImGui::PushID(static_cast<int>(hash ^ (hash >> 32)));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      std::string note;
      float scale = 1.0f, saved = 1.0f;
      int as = DxvkDs1Emissive::kAsDrawn, savedAs = DxvkDs1Emissive::kAsDrawn;
      {
        std::lock_guard lock(g_mutex);
        const auto found = g_entries.find(hash);
        if (found != g_entries.end()) {
          note = found->second.note;
          scale = found->second.live;
          saved = found->second.saved;
          as = found->second.liveAs;
          savedAs = found->second.savedAs;
        }
      }
      const std::string name = nameOf(hash, note);
      ImGui::Selectable((name.empty() ? hexHash(hash) : name + "  " + hexHash(hash)).c_str(), g_picked.load() == hash, ImGuiSelectableFlags_None);
      if (ImGui::IsItemHovered()) {
        g_flash.store(hash);
        g_flashFrame.store(g_uiFrame);
      }
      if (ImGui::IsItemClicked()) {
        g_picked.store(hash);
      }
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(what);
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1.0f);
      if (ImGui::Combo("##as", &as, "As drawn\0Unlit\0Self-lit\0")) {
        std::lock_guard lock(g_mutex);
        setLiveAs(hash, static_cast<DxvkDs1Emissive::RenderAs>(as));
      }
      tip("How the texture glows.\n"
          "As drawn: as the game and the proxy draw it.\n"
          "Unlit: its texture and vertex colour are its colour on screen, no light falls on it; the scale is not capped.\n"
          "Self-lit: it emits its colour as light: the exposure, haze and bloom take it like any surface, and it lights what is near.\n"
          "Self-lit additive and blended layers are capped at full colour per layer, as the game's are.");
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1.0f);
      if (ImGui::DragFloat("##scale", &scale, 0.01f, 0.0f, kMaxScale, "x %.2f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic)) {
        std::lock_guard lock(g_mutex);
        setLive(hash, scale);
      }
      ImGui::TableNextColumn();
      if (std::abs(scale - saved) >= kSame || as != savedAs) {
        if (ImGui::SmallButton("Revert")) {
          std::lock_guard lock(g_mutex);
          setLive(hash, saved);
          setLiveAs(hash, static_cast<DxvkDs1Emissive::RenderAs>(savedAs));
        }
        tip("Back to the saved scale and way of drawing.");
      } else if (std::abs(scale - 1.0f) >= kSame || as != DxvkDs1Emissive::kAsDrawn) {
        if (ImGui::SmallButton("Reset")) {
          std::lock_guard lock(g_mutex);
          setLive(hash, 1.0f);
          setLiveAs(hash, DxvkDs1Emissive::kAsDrawn);
        }
        tip("Back to 1, as drawn: as the game and the mods give it. Takes it out of the file on Save.");
      }
      ImGui::PopID();
    }

    bool beginTable(const char* id) {
      if (!ImGui::BeginTable(id, 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingFixedFit)) {
        return false;
      }
      ImGui::TableSetupColumn("Texture", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Glows as", ImGuiTableColumnFlags_WidthFixed, 130.0f);
      ImGui::TableSetupColumn("Render as", ImGuiTableColumnFlags_WidthFixed, 100.0f);
      ImGui::TableSetupColumn("Emission", ImGuiTableColumnFlags_WidthFixed, 110.0f);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60.0f);
      ImGui::TableHeadersRow();
      return true;
    }
  }

  DxvkDs1Emissive::RenderAs DxvkDs1Emissive::renderAs(XXH64_hash_t materialHash) {
    if (materialHash == 0 || g_renderAsCount.load() == 0) {
      return kAsDrawn;
    }
    std::lock_guard lock(g_mutex);
    const auto found = g_entries.find(materialHash);
    return found == g_entries.end() ? kAsDrawn : found->second.liveAs;
  }

  float DxvkDs1Emissive::scale(XXH64_hash_t materialHash, Kind kind, uint32_t frame) {
    if (materialHash == 0) {
      return 1.0f;
    }
    std::lock_guard lock(g_mutex);
    if (!g_loaded) {
      g_loaded = true;
      readFile();
    }
    Seen& seen = g_seen[materialHash];
    seen.frame = frame;
    seen.kind = kind;
    float value = liveOf(materialHash);
    // The texture under the mouse in the menu's lists blinks, to tell which one it is. Two values only: each is a material of its own.
    if (g_flash.load() == materialHash && static_cast<int32_t>(frame - g_flashFrame.load()) < static_cast<int32_t>(kFlashFrames) && (frame / 12) % 2 == 0) {
      value = std::max(value, 0.25f) * kFlashScale;
    }
    return value;
  }

  void DxvkDs1Emissive::update() {
    static Clock::time_point lastPoll {};
    const auto now = Clock::now();
    if (std::chrono::duration<float>(now - lastPoll).count() < 1.0f) {
      return;
    }
    lastPoll = now;
    std::error_code ec;
    const auto time = fs::last_write_time(kFile, ec);
    std::lock_guard lock(g_mutex);
    if (!g_loaded || (!ec && time != g_time)) {
      g_loaded = true;
      readFile();
    }
  }

  void DxvkDs1Emissive::showImguiSettings(const Rc<DxvkContext>& ctx) {
    // Nothing is hovered once the section is closed. (With the whole menu gone this is not called: the blink then ends by its frame.)
    g_flash.store(0);
    g_uiFrame = ctx->getDevice()->getCurrentFrameId();
    if (!ImGui::CollapsingHeader("Emissive per texture", ImGuiTreeNodeFlags_None)) {
      g_picking = false;
      return;
    }
    DxvkObjects* common = ctx->getCommonObjects();
    ImGui::PushID("ds1Emissive");
    ImGui::Indent();
    ImGui::TextWrapped("How strongly one game texture glows: an effect, a ghost, a flame. The scale goes on whatever makes it emit "
                       "(an additive or unlit sprite, a self-lit layer, a mod material's emission) and on nothing else. "
                       "Render as switches a texture between an unlit layer and a self-lit surface, when the way it is drawn caps "
                       "how bright it can get. Hover a row to make that texture blink in the game.");

    if (ImGui::Button(g_picking ? "Click it in the game..." : "Pick Texture")) {
      g_picking = !g_picking;
    }
    tip("Then click the glowing thing in the game. Thin effects can let the click through to what is behind them:\n"
        "take those from the list of what glows now instead.");
    if (g_picking) {
      const ImVec2 mouse = ImGui::GetMousePos();
      const Vector2i pixel { static_cast<int>(mouse.x), static_cast<int>(mouse.y) };
      if (!ImGui::GetIO().WantCaptureMouse) {
        common->metaDebugView().Highlighting.requestHighlighting(pixel, HighlightColor::World, ctx->getDevice()->getCurrentFrameId());
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
          g_picking = false;
          common->metaDebugView().ObjectPicking.request(pixel, pixel + Vector2i { 1, 1 },
            [](std::vector<ObjectPickingValue>&&, std::optional<XXH64_hash_t> legacyTextureHash) {
              if (legacyTextureHash) {
                g_picked.store(*legacyTextureHash);
              }
            });
        }
      }
    }

    const uint32_t frame = g_uiFrame;
    std::vector<std::pair<XXH64_hash_t, Kind>> glowing;
    std::vector<XXH64_hash_t> changed;
    int unsaved = 0;
    {
      std::lock_guard lock(g_mutex);
      for (auto it = g_seen.begin(); it != g_seen.end();) {
        // Signed: the render thread may already have stamped a later frame.
        if (static_cast<int32_t>(frame - it->second.frame) > static_cast<int32_t>(kShownFrames)) {
          it = g_seen.erase(it);
        } else {
          glowing.emplace_back(it->first, it->second.kind);
          ++it;
        }
      }
      for (const auto& [hash, entry] : g_entries) {
        changed.push_back(hash);
        unsaved += entry.edited() ? 1 : 0;
      }
    }
    std::sort(glowing.begin(), glowing.end());
    std::sort(changed.begin(), changed.end());

    const XXH64_hash_t picked = g_picked.load();
    if (picked != 0) {
      ImGui::Separator();
    ImGui::TextUnformatted("Picked");
      if (beginTable("##picked")) {
        const auto seen = std::find_if(glowing.begin(), glowing.end(), [&](const auto& g) { return g.first == picked; });
        row(picked, seen != glowing.end() ? kindText(seen->second) : "not glowing now");
        ImGui::EndTable();
      }
    }

    ImGui::Separator();
    ImGui::TextUnformatted(("Set (" + std::to_string(changed.size()) + ")").c_str());
    if (changed.empty()) {
      ImGui::TextDisabled("No texture has a scale of its own yet.");
    } else if (beginTable("##set")) {
      for (const XXH64_hash_t hash : changed) {
        const auto seen = std::find_if(glowing.begin(), glowing.end(), [&](const auto& g) { return g.first == hash; });
        row(hash, seen != glowing.end() ? kindText(seen->second) : "not on screen");
      }
      ImGui::EndTable();
    }
    ImGui::BeginDisabled(unsaved == 0);
    if (ImGui::Button("Save")) {
      std::lock_guard lock(g_mutex);
      g_status = writeFile() ? "saved to rtx_comp/emissive_scales.txt" : "could not write rtx_comp/emissive_scales.txt";
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert All")) {
      std::lock_guard lock(g_mutex);
      for (auto it = g_entries.begin(); it != g_entries.end();) {
        it->second.live = it->second.saved;
        it->second.liveAs = it->second.savedAs;
        it = it->second.isDefault() ? g_entries.erase(it) : std::next(it);
      }
      recount();
      g_status.clear();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", unsaved ? (std::to_string(unsaved) + " unsaved").c_str() : g_status.c_str());

    ImGui::Separator();
    ImGui::TextUnformatted(("Glowing now (" + std::to_string(glowing.size()) + ")").c_str());
    if (ImGui::BeginChild("##glowing", ImVec2(0.0f, 260.0f), true)) {
      if (beginTable("##now")) {
        for (const auto& [hash, kind] : glowing) {
          row(hash, kindText(kind));
        }
        ImGui::EndTable();
      }
    }
    ImGui::EndChild();

    ImGui::Unindent();
    ImGui::PopID();
  }

}
