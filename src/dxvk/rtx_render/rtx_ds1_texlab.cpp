#include "rtx_ds1_texlab.h"

#include "rtx_asset_replacer.h"
#include "rtx_context.h"
#include "rtx_debug_view.h"
#include "rtx_imgui.h"
#include "rtx_material_data.h"
#include "rtx_objectpicking.h"
#include "rtx_scene_manager.h"
#include "../dxvk_device.h"
#include "../dxvk_objects.h"
#include "../imgui/imgui.h"
#include "../../util/util_env.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dxvk {

  namespace {
    namespace fs = std::filesystem;
    using Clock = std::chrono::steady_clock;

    const fs::path kLabDir = "rtx_comp/texlab";
    const fs::path kAiModDir = "rtx-remix/mods/ai_generated";
    constexpr float kHeartbeatStaleSeconds = 6.0f;
    constexpr float kHelperStartSeconds = 20.0f;
    constexpr float kPollSeconds = 0.5f;
    constexpr float kNameRetrySeconds = 5.0f;
    constexpr float kMaxDepthScale = 8.0f;
    constexpr float kMaxRoughnessScale = 3.0f;
    constexpr float kSameScale = 0.0005f;       // the file keeps four decimals
    constexpr int kUploadFramesAfterEdit = 3;   // an edit can land after this frame's materials were built
    const char* const kResolutions[] = { "1k", "2k", "4k" };

    enum GeneratedMap { kAlbedoMap, kNormalMap, kRoughnessMap, kHeightMap, kGeneratedMapCount };
    const char* const kMapKeys[kGeneratedMapCount] = { "albedo", "normal", "roughness", "height" };
    const char* const kMapLabels[kGeneratedMapCount] = { "Albedo", "Normal", "Roughness", "Height (POM)" };
    constexpr float kMapNameColumn = 130.0f;

    using ScaleTable = std::unordered_map<XXH64_hash_t, float>;

    std::atomic<int> g_uploadFrames { 0 };

    float scaleIn(const ScaleTable& table, XXH64_hash_t hash) {
      const auto found = table.find(hash);
      return found == table.end() ? 1.0f : found->second;
    }

    void assignScale(ScaleTable& table, XXH64_hash_t hash, float scale) {
      if (std::abs(scale - 1.0f) < kSameScale) {
        table.erase(hash);
      } else {
        table[hash] = scale;
      }
    }

    // Per-material multipliers: the live ones the renderer reads, and the ones rtx_comp/texlab/<name> holds. They differ only by unsaved edits.
    class TuningFile {
    public:
      struct Change {
        XXH64_hash_t hash;
        float saved;
        float live;
      };

      explicit TuningFile(const char* name) : m_path(kLabDir / name) { }

      float get(XXH64_hash_t hash) const {
        std::shared_lock lock(m_mutex);
        return scaleIn(m_live, hash);
      }

      void set(XXH64_hash_t hash, float scale) {
        std::unique_lock lock(m_mutex);
        assignScale(m_live, hash, scale);
        g_uploadFrames.store(kUploadFramesAfterEdit);
      }

      void revert(XXH64_hash_t hash) {
        std::unique_lock lock(m_mutex);
        assignScale(m_live, hash, scaleIn(m_saved, hash));
        g_uploadFrames.store(kUploadFramesAfterEdit);
      }

      std::vector<Change> changes() const {
        std::shared_lock lock(m_mutex);
        return unsavedChanges();
      }

      size_t savedCount() const {
        std::shared_lock lock(m_mutex);
        return m_saved.size();
      }

      // Writes the chosen changes to the file; the others stay live and unsaved. Returns how many were written.
      int save(const std::function<bool(XXH64_hash_t)>& chosen) {
        std::unique_lock lock(m_mutex);
        int written = 0;
        for (const Change& change : unsavedChanges()) {
          if (chosen(change.hash)) {
            assignScale(m_saved, change.hash, change.live);
            ++written;
          }
        }
        if (written > 0) {
          writeSaved();
        }
        return written;
      }

      // Takes in a file changed from outside (the helper's bake), keeping the unsaved edits on top of it.
      void reloadIfChanged() {
        std::error_code ec;
        const auto time = fs::last_write_time(m_path, ec);
        std::unique_lock lock(m_mutex);
        if (ec || time == m_time) {
          return;
        }
        m_time = time;
        const std::vector<Change> edits = unsavedChanges();
        m_saved = parseFile();
        m_live = m_saved;
        for (const Change& edit : edits) {
          assignScale(m_live, edit.hash, edit.live);
        }
        g_uploadFrames.store(kUploadFramesAfterEdit);
      }

    private:
      std::vector<Change> unsavedChanges() const {
        std::vector<Change> out;
        for (const auto& [hash, live] : m_live) {
          const float saved = scaleIn(m_saved, hash);
          if (std::abs(saved - live) >= kSameScale) {
            out.push_back({ hash, saved, live });
          }
        }
        for (const auto& [hash, saved] : m_saved) {
          if (m_live.count(hash) == 0 && std::abs(saved - 1.0f) >= kSameScale) {
            out.push_back({ hash, saved, 1.0f });
          }
        }
        std::sort(out.begin(), out.end(), [](const Change& a, const Change& b) { return a.hash < b.hash; });
        return out;
      }

      ScaleTable parseFile() const {
        ScaleTable table;
        std::ifstream in(m_path);
        std::string line;
        while (std::getline(in, line)) {
          char* end = nullptr;
          const unsigned long long hash = std::strtoull(line.c_str(), &end, 16);
          char* scaleEnd = end;
          const float scale = end != line.c_str() ? std::strtof(end, &scaleEnd) : 0.0f;
          if (hash != 0 && scaleEnd != end && scale >= 0.0f) {
            table[hash] = scale;
          }
        }
        return table;
      }

      void writeSaved() {
        std::error_code ec;
        fs::create_directories(kLabDir, ec);
        fs::path temp = m_path;
        temp += ".tmp";
        {
          std::ofstream out(temp, std::ios::trunc);
          char line[64];
          for (const auto& [hash, scale] : m_saved) {
            snprintf(line, sizeof(line), "%016llX %.4f\n", static_cast<unsigned long long>(hash), scale);
            out << line;
          }
        }
        fs::rename(temp, m_path, ec);
        m_time = fs::last_write_time(m_path, ec);
      }

      fs::path m_path;
      mutable std::shared_mutex m_mutex;
      ScaleTable m_live;
      ScaleTable m_saved;
      fs::file_time_type m_time {};
    };

    TuningFile g_depth { "pom_depth.txt" };
    TuningFile g_roughness { "roughness.txt" };

    std::unordered_map<std::string, fs::file_time_type> g_statusSeen;
    bool g_statusScanned = false;

    std::atomic<XXH64_hash_t> g_selected { 0 };
    bool g_picking = false;
    int g_resolution = 1;
    bool g_mapTicked[kGeneratedMapCount] = { true, true, true, true };
    XXH64_hash_t g_ticksFor = 0;
    std::string g_state, g_message;
    XXH64_hash_t g_statusHash = 0;
    Clock::time_point g_lastStatusRead {};
    Clock::time_point g_requestedAt {};
    std::unordered_set<std::string> g_saveSkip;
    std::string g_saveStatus;

    std::string hexHash(XXH64_hash_t hash) {
      char text[17];
      snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(hash));
      return text;
    }

    template<typename... Args>
    std::string formatted(const char* format, Args... args) {
      char text[256];
      snprintf(text, sizeof(text), format, args...);
      return text;
    }

    void tip(const char* text) {
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", text);
      }
    }

    std::string fileStem(const std::string& path) {
      const size_t slash = path.find_last_of("\\/");
      const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
      const size_t dot = name.rfind('.');
      return dot == std::string::npos ? name : name.substr(0, dot);
    }

    // Material hash -> the game texture's name, from the helper's flat {"hash": "path", ...} index.
    std::unordered_map<XXH64_hash_t, std::string> loadTextureNames() {
      std::unordered_map<XXH64_hash_t, std::string> table;
      std::ifstream in(kLabDir / "hash_files.json");
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
        table[std::strtoull(strings[i].c_str(), nullptr, 16)] = fileStem(strings[i + 1]);
      }
      return table;
    }

    // The helper builds the index on its first generation, which can be after the game started.
    const std::unordered_map<XXH64_hash_t, std::string>& textureNames() {
      static std::unordered_map<XXH64_hash_t, std::string> names;
      static Clock::time_point lastTry {};
      const auto now = Clock::now();
      if (names.empty() && std::chrono::duration<float>(now - lastTry).count() > kNameRetrySeconds) {
        lastTry = now;
        names = loadTextureNames();
      }
      return names;
    }

    std::string materialName(XXH64_hash_t hash) {
      const auto found = textureNames().find(hash);
      return found == textureNames().end() ? hexHash(hash) : found->second;
    }

    std::string textureFile(const TextureRef& texture) {
      const Rc<ManagedTexture>& managed = texture.getManagedTexture();
      return managed != nullptr && managed->m_assetData != nullptr ? std::string(managed->m_assetData->info().filename) : std::string();
    }

    const OpaqueMaterialData* opaqueReplacement(const std::shared_ptr<MaterialData>& replacement) {
      return replacement && replacement->getType() == MaterialDataType::Opaque ? &replacement->getOpaqueMaterialData() : nullptr;
    }

    bool helperAlive() {
      std::error_code ec;
      const auto beat = fs::last_write_time(kLabDir / "heartbeat.txt", ec);
      if (ec) {
        return false;
      }
      return std::chrono::duration<float>(fs::file_time_type::clock::now() - beat).count() < kHeartbeatStaleSeconds;
    }

    void startHelper() {
      if (DxvkDs1TexLab::helperCommand().empty()) {
        return;
      }
      std::string command = "cmd.exe /c start \"texlab\" /min " + DxvkDs1TexLab::helperCommand();
      STARTUPINFOA startup {};
      startup.cb = sizeof(startup);
      PROCESS_INFORMATION process {};
      if (CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
      }
    }

    bool writeFileAtomic(const fs::path& target, const std::string& text) {
      std::error_code ec;
      fs::create_directories(target.parent_path(), ec);
      fs::path temp = target;
      temp += ".tmp";
      {
        std::ofstream out(temp, std::ios::trunc);
        out << text;
      }
      fs::rename(temp, target, ec);
      return !ec;
    }

    // Hands the helper one job for `hash` (texlab.py handle) and starts the helper if it is not running.
    void sendRequest(XXH64_hash_t hash, const std::string& request) {
      const std::string name = hexHash(hash);
      const fs::path statusFile = kLabDir / "status" / (name + ".txt");
      // The status goes to "queued" first: a "done" left from an earlier run must not read as this request's result.
      writeFileAtomic(statusFile, "queued\nwaiting for the helper\n");
      const bool written = writeFileAtomic(kLabDir / "requests" / (name + ".json"), request);
      g_requestedAt = Clock::now();
      g_statusHash = 0;
      if (!written) {
        writeFileAtomic(statusFile, "error\ncould not write the request\n");
      } else if (!helperAlive()) {
        if (DxvkDs1TexLab::helperCommand().empty()) {
          writeFileAtomic(statusFile, "error\nhelper not running: start pbr_tools/texlab/texlab.py\n");
        } else {
          startHelper();
        }
      }
    }

    // `maps` go in place of the maps in use, or with `keep` are added to them.
    void requestGeneration(XXH64_hash_t hash, const std::vector<GeneratedMap>& maps, bool keep) {
      std::string mapList;
      for (const GeneratedMap map : maps) {
        mapList += (mapList.empty() ? "\"" : ", \"") + std::string(kMapKeys[map]) + "\"";
      }
      sendRequest(hash, std::string("{\"resolution\": \"") + kResolutions[g_resolution] + "\", \"maps\": [" + mapList +
        "], \"keep\": " + (keep ? "true" : "false") + "}");
    }

    // The ai_generated mod's materials (its generated.json), read again at most every poll interval.
    bool hasAiMaterial(XXH64_hash_t hash) {
      static auto lastRead = Clock::time_point {};
      static std::string text;
      const auto now = Clock::now();
      if (std::chrono::duration<float>(now - lastRead).count() >= kPollSeconds) {
        lastRead = now;
        std::ifstream in(kAiModDir / "generated.json");
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
      }
      return text.find("\"" + hexHash(hash) + "\"") != std::string::npos;
    }

    void readStatus(XXH64_hash_t hash) {
      const auto now = Clock::now();
      if (hash == g_statusHash && std::chrono::duration<float>(now - g_lastStatusRead).count() < kPollSeconds) {
        return;
      }
      g_lastStatusRead = now;
      if (hash != g_statusHash) {
        g_statusHash = hash;
        g_state.clear();
        g_message.clear();
      }
      std::ifstream in(kLabDir / "status" / (hexHash(hash) + ".txt"));
      std::string state, message;
      if (std::getline(in, state)) {
        std::getline(in, message);
        g_state = state;
        g_message = message;
      }
    }

    bool generationRunning() {
      if (g_state != "queued" && g_state != "generating" && g_state != "converting") {
        return false;
      }
      return helperAlive() || std::chrono::duration<float>(Clock::now() - g_requestedAt).count() < kHelperStartSeconds;
    }

    void pickFromWorld(DxvkObjects* common, DxvkContext* ctx) {
      const ImVec2 mouse = ImGui::GetMousePos();
      const Vector2i pixel { static_cast<int>(mouse.x), static_cast<int>(mouse.y) };
      const bool worldUnderCursor = !ImGui::GetIO().WantCaptureMouse;
      if (worldUnderCursor) {
        common->metaDebugView().Highlighting.requestHighlighting(pixel, HighlightColor::World, ctx->getDevice()->getCurrentFrameId());
      }
      if (worldUnderCursor && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        g_picking = false;
        common->metaDebugView().ObjectPicking.request(pixel, pixel + Vector2i { 1, 1 },
          [](std::vector<ObjectPickingValue>&&, std::optional<XXH64_hash_t> legacyTextureHash) {
            if (legacyTextureHash) {
              g_selected.store(*legacyTextureHash);
            }
          });
      }
    }

    void scaleSlider(const char* label, TuningFile& file, XXH64_hash_t hash, float maximum) {
      float scale = file.get(hash);
      if (ImGui::DragFloat(label, &scale, 0.01f, 0.0f, maximum, "x %.2f", ImGuiSliderFlags_AlwaysClamp)) {
        file.set(hash, scale);
      }
      ImGui::SameLine();
      ImGui::PushID(label);
      if (ImGui::Button("x1")) {
        file.set(hash, 1.0f);
      }
      ImGui::PopID();
    }

    std::string mapInUse(const TextureRef& texture, const char* whenNone) {
      if (!texture.isValid()) {
        return whenNone;
      }
      const std::string file = textureFile(texture);
      return file.empty() ? std::string("a map, still loading") : fileStem(file);
    }

    std::string roughnessInUse(const OpaqueMaterialData* opaque) {
      if (opaque && opaque->getRoughnessTexture().isValid()) {
        return mapInUse(opaque->getRoughnessTexture(), "");
      }
      return formatted("flat %.2f", opaque ? opaque->getRoughnessConstant() : LegacyMaterialDefaults::roughnessConstant());
    }

    std::string heightInUse(const OpaqueMaterialData* opaque) {
      if (!opaque || !opaque->getHeightTexture().isValid()) {
        return "none";
      }
      return mapInUse(opaque->getHeightTexture(), "") + formatted(", depth %.4f", opaque->getDisplaceIn());
    }

    void showMapRow(GeneratedMap map, const std::string& inUse, bool available, const char* note) {
      ImGui::BeginDisabled(!available);
      bool ticked = available && g_mapTicked[map];
      if (ImGui::Checkbox(kMapLabels[map], &ticked)) {
        g_mapTicked[map] = ticked;
      }
      ImGui::EndDisabled();
      tip(note);
      ImGui::SameLine(kMapNameColumn);
      ImGui::TextDisabled("%s", inUse.c_str());
    }

    void showFixedMapRow(const char* label, const TextureRef& texture) {
      if (texture.isValid()) {
        ImGui::TextUnformatted(label);
        ImGui::SameLine(kMapNameColumn);
        ImGui::TextDisabled("%s", mapInUse(texture, "").c_str());
      }
    }

    // The maps the material has now, each with a tick for whether Generate should supply it. An unticked map stays as the material has it without AI.
    std::vector<GeneratedMap> showMapList(XXH64_hash_t hash, const OpaqueMaterialData* opaque) {
      if (g_ticksFor != hash) {
        g_ticksFor = hash;
        std::fill(std::begin(g_mapTicked), std::end(g_mapTicked), true);
      }
      const bool hasNormal = opaque && opaque->getNormalTexture().isValid();
      const bool heightPossible = hasNormal || g_mapTicked[kNormalMap];
      ImGui::PushID("maps");
      showMapRow(kAlbedoMap, opaque ? mapInUse(opaque->getAlbedoOpacityTexture(), "the game's texture") : std::string("the game's texture"), true, "Ticked: PBRFusion's upscaled albedo. Unticked: stays as it is.");
      showMapRow(kNormalMap, opaque ? mapInUse(opaque->getNormalTexture(), "none") : std::string("none"), true, "Ticked: PBRFusion's normal map. Unticked: stays as it is, or none.");
      showMapRow(kRoughnessMap, roughnessInUse(opaque), true, "Ticked: PBRFusion's roughness map. Unticked: stays as it is, or flat; the Roughness slider scales the flat value.");
      showMapRow(kHeightMap, heightInUse(opaque), heightPossible, heightPossible ? "Ticked: integrated from the normal map in use. Unticked: stays as it is, or none." : "Needs a normal map: tick Normal.");
      if (opaque) {
        showFixedMapRow("Metallic", opaque->getMetallicTexture());
        showFixedMapRow("Specular", opaque->getSpecularColorTexture());
        showFixedMapRow("Emissive", opaque->getEmissiveColorTexture());
      }
      ImGui::PopID();

      std::vector<GeneratedMap> chosen;
      for (int map = 0; map < kGeneratedMapCount; ++map) {
        if (g_mapTicked[map] && (map != kHeightMap || heightPossible)) {
          chosen.push_back(static_cast<GeneratedMap>(map));
        }
      }
      return chosen;
    }

    void showGenerate(XXH64_hash_t hash, const OpaqueMaterialData* opaque) {
      const std::vector<GeneratedMap> chosen = showMapList(hash, opaque);
      ImGui::SetNextItemWidth(70.0f);
      ImGui::Combo("Size", &g_resolution, kResolutions, IM_ARRAYSIZE(kResolutions));
      ImGui::SameLine();
      readStatus(hash);
      const bool running = generationRunning();
      ImGui::BeginDisabled(running || chosen.empty());
      if (ImGui::Button("Generate")) {
        requestGeneration(hash, chosen, false);
      }
      ImGui::EndDisabled();
      tip("Makes the ticked maps. With the same ticks as last time PBRFusion runs again; with changed ticks at the same size the maps it already made are reused.");
      ImGui::SameLine();
      ImGui::BeginDisabled(running || !opaque || !opaque->getNormalTexture().isValid());
      if (ImGui::Button("Generate Height Only")) {
        requestGeneration(hash, { kHeightMap }, true);
      }
      ImGui::EndDisabled();
      tip("Integrates a height map from the normal map this material already has, and changes nothing else. No ComfyUI needed.");
      ImGui::SameLine();
      ImGui::BeginDisabled(running || !hasAiMaterial(hash));
      if (ImGui::Button("Revert to Original")) {
        sendRequest(hash, "{\"action\": \"remove\"}");
      }
      ImGui::EndDisabled();
      tip("Takes this texture's AI material out: it renders as it did before Generate. The AI maps are kept in rtx_comp/texlab/removed.");
      if (!g_state.empty()) {
        ImGui::TextWrapped("%s: %s", g_state.c_str(), g_message.c_str());
      }
    }

    void showPickedMaterial(DxvkObjects* common, XXH64_hash_t hash) {
      const std::string title = materialName(hash) + "  " + hexHash(hash);
      ImGui::TextUnformatted(title.c_str());

      const auto replacement = common->getSceneManager().getAssetReplacer()->getReplacementMaterial(hash);
      const OpaqueMaterialData* opaque = opaqueReplacement(replacement);
      ImGui::TextUnformatted(!opaque ? "Material: the game's, no PBR" : opaque->getNormalTexture().isValid() ? "Material: has PBR maps" : "Material: replaced, no normal map");
      showGenerate(hash, opaque);

      ImGui::Separator();
      const bool hasHeight = opaque && opaque->getHeightTexture().isValid();
      ImGui::BeginDisabled(!hasHeight);
      scaleSlider("POM Depth", g_depth, hash, kMaxDepthScale);
      ImGui::EndDisabled();
      if (hasHeight) {
        ImGui::Text("Depth value %.4f", opaque->getDisplaceIn() * g_depth.get(hash));
      } else {
        ImGui::TextUnformatted("No height map: generate one to set its depth.");
      }

      scaleSlider("Roughness", g_roughness, hash, kMaxRoughnessScale);
      if (opaque && opaque->getRoughnessTexture().isValid()) {
        ImGui::TextUnformatted("Scales this material's roughness map. Below 1 is shinier.");
      } else {
        ImGui::Text("Roughness value %.3f. Below 1 is shinier.", (opaque ? opaque->getRoughnessConstant() : LegacyMaterialDefaults::roughnessConstant()) * g_roughness.get(hash));
      }
    }

    struct PendingItem {
      std::string id;
      std::string what;
      std::string change;
      std::string detail;
      TuningFile* file;
      XXH64_hash_t hash;
    };

    PendingItem depthItem(const TuningFile::Change& change, const OpaqueMaterialData* opaque) {
      const bool hasHeight = opaque && opaque->getHeightTexture().isValid();
      std::string text = formatted("POM depth x%.2f -> x%.2f", change.saved, change.live);
      text += hasHeight ? formatted(" (%.4f -> %.4f)", opaque->getDisplaceIn() * change.saved, opaque->getDisplaceIn() * change.live) : std::string(", no height map: nothing to bake");
      return { "depth:" + hexHash(change.hash), materialName(change.hash), std::move(text),
               hasHeight ? std::string("The bake writes the new depth into the mod's tuned depth layer.") : std::string("Saved, but this material has no height map, so the bake leaves it in the file."),
               &g_depth, change.hash };
    }

    PendingItem roughnessItem(const TuningFile::Change& change, const OpaqueMaterialData* opaque) {
      std::string text = formatted("roughness x%.2f -> x%.2f", change.saved, change.live);
      std::string detail;
      const std::string map = opaque ? textureFile(opaque->getRoughnessTexture()) : std::string();
      if (!opaque) {
        text += ", no PBR material: live only";
        detail = "The game's own material has no roughness in a mod. Saved, but the bake leaves it in the file.";
      } else if (!map.empty()) {
        text += ", map " + fileStem(map);
        detail = "Map rendered now: " + map + "\nThe bake makes a new scaled map from it. This file is never changed, and texlab.py revert undoes it.";
      } else {
        const float constant = opaque->getRoughnessConstant();
        const std::string values = formatted("%.3f -> %.3f", std::min(constant * change.saved, 1.0f), std::min(constant * change.live, 1.0f));
        if (opaque->isRoughnessConstantModified()) {
          text += ", constant " + values;
          detail = "This material has no roughness map. The bake writes the new constant into the mod's tuned layer.";
        } else {
          text += ", flat " + values + ", live only";
          detail = "This material authors no roughness, which keeps the game's specular response; authoring one would switch that off. Saved, but the bake leaves it in the file.";
        }
      }
      return { "rough:" + hexHash(change.hash), materialName(change.hash), std::move(text), std::move(detail), &g_roughness, change.hash };
    }

    std::vector<PendingItem> pendingItems(AssetReplacer& replacer) {
      std::vector<PendingItem> items;
      for (const auto& change : g_depth.changes()) {
        const auto replacement = replacer.getReplacementMaterial(change.hash);
        items.push_back(depthItem(change, opaqueReplacement(replacement)));
      }
      for (const auto& change : g_roughness.changes()) {
        const auto replacement = replacer.getReplacementMaterial(change.hash);
        items.push_back(roughnessItem(change, opaqueReplacement(replacement)));
      }
      return items;
    }

    void forgetSkipsOfSettledItems(const std::vector<PendingItem>& items) {
      for (auto it = g_saveSkip.begin(); it != g_saveSkip.end();) {
        const bool listed = std::any_of(items.begin(), items.end(), [&](const PendingItem& item) { return item.id == *it; });
        it = listed ? std::next(it) : g_saveSkip.erase(it);
      }
    }

    void saveTicked(TuningFile& file, const char* prefix, int& written) {
      written += file.save([&](XXH64_hash_t hash) { return g_saveSkip.count(prefix + hexHash(hash)) == 0; });
    }

    void showPendingRows(const std::vector<PendingItem>& items) {
      const PendingItem* revert = nullptr;
      if (ImGui::BeginChild("##pending_save", ImVec2(0.0f, ImGui::GetFontSize() * 7.0f), true)) {
        if (items.empty()) {
          ImGui::TextDisabled("Save would write nothing: every slider is as the files have it.");
        }
        for (const PendingItem& item : items) {
          ImGui::PushID(item.id.c_str());
          bool include = g_saveSkip.count(item.id) == 0;
          if (ImGui::Checkbox("##save", &include)) {
            if (include) {
              g_saveSkip.erase(item.id);
            } else {
              g_saveSkip.insert(item.id);
            }
          }
          tip("Ticked: Save writes this. Unticked: Save leaves it as the file has it; the change stays here, unsaved.");
          ImGui::SameLine();
          if (ImGui::SmallButton("Revert")) {
            revert = &item;
          }
          tip("Puts back what the file holds for this one.");
          ImGui::SameLine();
          ImGui::TextUnformatted(item.what.c_str());
          tip(item.detail.c_str());
          ImGui::SameLine();
          ImGui::TextDisabled("%s", item.change.c_str());
          tip(item.detail.c_str());
          ImGui::PopID();
        }
      }
      ImGui::EndChild();
      if (revert) {
        revert->file->revert(revert->hash);
      }
    }

    // What Save will write, as the light menu lists it: each change with its old and new value and a tick to leave it out.
    void showPendingSave(DxvkObjects* common) {
      const std::vector<PendingItem> items = pendingItems(*common->getSceneManager().getAssetReplacer());
      forgetSkipsOfSettledItems(items);
      const auto ticked = std::count_if(items.begin(), items.end(), [](const PendingItem& item) { return g_saveSkip.count(item.id) == 0; });

      ImGui::Separator();
      ImGui::BeginDisabled(ticked == 0);
      if (ImGui::Button("Save")) {
        int written = 0;
        saveTicked(g_depth, "depth:", written);
        saveTicked(g_roughness, "rough:", written);
        g_saveStatus = formatted("saved %d", written);
      }
      ImGui::EndDisabled();
      tip("Writes the ticked changes to rtx_comp/texlab/pom_depth.txt and roughness.txt. They stay live after restarts. The mods themselves change only when you run texlab.py bake with the game closed.");
      ImGui::SameLine();
      ImGui::BeginDisabled(items.empty());
      if (ImGui::Button("Discard All")) {
        for (const PendingItem& item : items) {
          item.file->revert(item.hash);
        }
        g_saveStatus = "discarded";
      }
      ImGui::EndDisabled();
      tip("Throws away every unsaved change and puts back what the files hold.");
      ImGui::SameLine();
      if (items.empty()) {
        ImGui::TextDisabled("%s", g_saveStatus.empty() ? "no changes" : g_saveStatus.c_str());
      } else {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%d unsaved change%s, %d ticked to save", static_cast<int>(items.size()), items.size() == 1 ? "" : "s", static_cast<int>(ticked));
      }
      showPendingRows(items);

      const size_t saved = g_depth.savedCount() + g_roughness.savedCount();
      ImGui::TextDisabled("Sliders are live and lost on exit unless saved. %d saved value%s wait for texlab.py bake.", static_cast<int>(saved), saved == 1 ? "" : "s");
    }
  }

  float DxvkDs1TexLab::pomDepthScale(XXH64_hash_t materialHash) {
    return g_depth.get(materialHash);
  }

  float DxvkDs1TexLab::roughnessScale(XXH64_hash_t materialHash) {
    return g_roughness.get(materialHash);
  }

  void DxvkDs1TexLab::requestMaterialUpload() {
    g_uploadFrames.store(kUploadFramesAfterEdit);
  }

  bool DxvkDs1TexLab::consumeMaterialUpload() {
    int frames = g_uploadFrames.load();
    while (frames > 0 && !g_uploadFrames.compare_exchange_weak(frames, frames - 1)) { }
    return frames > 0;
  }

  void DxvkDs1TexLab::update(DxvkContext* ctx) {
    if (!enable()) {
      return;
    }
    static Clock::time_point lastPoll {};
    const auto now = Clock::now();
    if (std::chrono::duration<float>(now - lastPoll).count() < 1.0f) {
      return;
    }
    lastPoll = now;
    g_depth.reloadIfChanged();
    g_roughness.reloadIfChanged();

    bool finished = false;
    std::error_code ec;
    fs::directory_iterator entry(kLabDir / "status", ec);
    const fs::directory_iterator end;
    for (; !ec && entry != end; entry.increment(ec)) {
      const std::string file = entry->path().string();
      std::error_code timeError;
      const auto time = entry->last_write_time(timeError);
      auto& seen = g_statusSeen[file];
      if (timeError || time == seen) {
        continue;
      }
      seen = time;
      std::ifstream in(entry->path());
      std::string state;
      std::getline(in, state);
      // What was already finished when the game started is already in the mod it loaded.
      finished = finished || (g_statusScanned && state == "done");
    }
    g_statusScanned = true;
    if (finished) {
      ctx->getCommonObjects()->getSceneManager().getAssetReplacer()->requestAiGeneratedReload();
    }
  }

  void DxvkDs1TexLab::showImguiSettings(const Rc<DxvkContext>& ctx) {
    if (!enable() || !ImGui::CollapsingHeader("Texture Lab (development)", ImGuiTreeNodeFlags_DefaultOpen)) {
      return;
    }
    DxvkObjects* common = ctx->getCommonObjects();
    ImGui::PushID("ds1TexLab");

    RemixGui::Checkbox("Highlight Materials Without PBR (flash red)", &RtxOptions::useHighlightLegacyModeObject());
    if (RemixGui::Checkbox("Use AI Generated Layer", &aiLayerObject())) {
      requestMaterialUpload();
    }

    if (ImGui::Button(g_picking ? "Click a surface in the game..." : "Pick Texture")) {
      g_picking = !g_picking;
    }
    if (g_picking) {
      pickFromWorld(common, ctx.ptr());
    }

    const XXH64_hash_t hash = g_selected.load();
    if (hash != 0) {
      showPickedMaterial(common, hash);
    } else {
      ImGui::TextUnformatted("No texture picked.");
    }
    showPendingSave(common);

    ImGui::PopID();
  }

}
