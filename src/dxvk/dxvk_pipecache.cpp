#include "dxvk_pipecache.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <vector>

#include <windows.h>

namespace dxvk {

  // Fork: the pipeline cache persists across launches. DXVK leaves it in memory and relies on the
  // driver's own disk cache, but NVIDIA caps its Vulkan disk cache (NVIDIA\GLCache) at 1 GB, and a few
  // runtime builds' worth of Remix ray tracing pipelines fill it: past that every launch compiled all
  // of them again (about 30 s). The cache data the driver returns holds the compiled pipelines and is
  // stamped with the driver's pipeline cache UUID; data from another driver or GPU is ignored by the
  // driver on load (Vulkan spec), so a stale file only costs one recompile.
  DxvkPipelineCache::DxvkPipelineCache(
    const Rc<vk::DeviceFn>& vkd)
  : m_vkd(vkd) {
    std::string dir = env::getEnvVar("DXVK_STATE_CACHE_PATH");
    if (!dir.empty() && *dir.rbegin() != '/' && *dir.rbegin() != '\\')
      dir += '/';
    m_path = dir + env::getExeBaseName() + ".dxvk-pipelines";

    std::vector<char> data;
    {
      std::ifstream file(str::tows(m_path.c_str()).c_str(), std::ios_base::binary | std::ios_base::ate);
      if (file) {
        const std::streamsize size = file.tellg();
        if (size > 0) {
          data.resize(size_t(size));
          file.seekg(0);
          if (!file.read(data.data(), size))
            data.clear();
        }
      }
    }

    VkPipelineCacheCreateInfo info;
    info.sType            = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    info.pNext            = nullptr;
    info.flags            = 0;
    info.initialDataSize  = data.size();
    info.pInitialData     = data.empty() ? nullptr : data.data();

    VkResult status = m_vkd->vkCreatePipelineCache(m_vkd->device(), &info, nullptr, &m_handle);
    if (status != VK_SUCCESS && !data.empty()) {
      // A file the driver refuses outright: start empty, it is rewritten after the next compile.
      Logger::warn(str::format("DXVK: Pipeline cache file ", m_path, " rejected, starting empty"));
      info.initialDataSize = 0;
      info.pInitialData = nullptr;
      data.clear();
      status = m_vkd->vkCreatePipelineCache(m_vkd->device(), &info, nullptr, &m_handle);
    }
    if (status != VK_SUCCESS)
      throw DxvkError("DxvkPipelineCache: Failed to create cache");

    m_savedSize = data.size();
    Logger::info(str::format("DXVK: Pipeline cache ", m_path, ": loaded ", data.size() >> 20, " MB"));
  }


  DxvkPipelineCache::~DxvkPipelineCache() {
    save();
    m_vkd->vkDestroyPipelineCache(
      m_vkd->device(), m_handle, nullptr);
  }


  void DxvkPipelineCache::save() {
    std::lock_guard<dxvk::mutex> lock(m_saveMutex);

    const auto t0 = std::chrono::steady_clock::now();

    size_t size = 0;
    if (m_vkd->vkGetPipelineCacheData(m_vkd->device(), m_handle, &size, nullptr) != VK_SUCCESS || size == 0)
      return;
    // Nothing compiled since the last save (or the load): the data would be the same.
    if (size == m_savedSize)
      return;

    std::vector<char> data(size);
    if (m_vkd->vkGetPipelineCacheData(m_vkd->device(), m_handle, &size, data.data()) != VK_SUCCESS)
      return;

    // Written beside the old file and moved over it, so an interrupted save never leaves a torn cache.
    const std::wstring path = str::tows(m_path.c_str());
    const std::wstring tmp = path + L".tmp";
    {
      std::ofstream file(tmp.c_str(), std::ios_base::binary | std::ios_base::trunc);
      if (!file || !file.write(data.data(), std::streamsize(size)))
        return;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
      DeleteFileW(tmp.c_str());
      return;
    }
    m_savedSize = size;
    Logger::info(str::format("DXVK: Pipeline cache ", m_path, ": saved ", size >> 20, " MB in ",
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(), " ms"));
  }

}
