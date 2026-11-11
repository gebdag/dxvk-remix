/*
* Copyright (c) 2019-2022, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/
#include <cstring>

#include "../util/util_time.h"

#include "dxvk_compute.h"
#include "dxvk_device.h"
#include "dxvk_pipemanager.h"
#include "dxvk_spec_const.h"
#include "dxvk_state_cache.h"

namespace dxvk {
  // Fork (DS1): a pipeline compile over 100 ms (5 ms on dxvk-cs, where the frame waits) is logged with its name and the thread that waited for it (dxvk-cs or the render
  // thread: a stall the player sees; a prewarm or compiler worker: background), to find what a fresh build compiles too late.
  inline void ds1LogSlowPipeline(const char* kind, const char* name, double ms) {
    if (ms < 5.0) {
      return;
    }
    std::string thread = "?";
    PWSTR desc = nullptr;
    if (SUCCEEDED(GetThreadDescription(GetCurrentThread(), &desc)) && desc) {
      thread.clear();
      for (const wchar_t* c = desc; *c; ++c) {
        thread.push_back(static_cast<char>(*c));
      }
      LocalFree(desc);
    }
    if (thread != "dxvk-cs" && ms < 100.0) {
      return;
    }
    Logger::info(str::format("[DS1 pipeline] ", kind, " ", name ? name : "<unnamed>", " compiled in ", ms, " ms on thread '", thread, "'"));
  }
}

namespace dxvk {
  
  DxvkComputePipeline::DxvkComputePipeline(
          DxvkPipelineManager*        pipeMgr,
          DxvkComputePipelineShaders  shaders)
  : m_vkd(pipeMgr->m_device->vkd()), m_pipeMgr(pipeMgr),
    m_shaders(std::move(shaders)) {
    m_shaders.cs->defineResourceSlots(m_slotMapping);
    
    m_slotMapping.makeDescriptorsDynamic(
      m_pipeMgr->m_device->options().maxNumDynamicUniformBuffers,
      m_pipeMgr->m_device->options().maxNumDynamicStorageBuffers);
    
    // NV-DXVK start: descriptor set(s)
    m_layout = new DxvkPipelineLayout(m_vkd,
      m_slotMapping, VK_PIPELINE_BIND_POINT_COMPUTE, m_shaders.cs->shaderOptions().extraLayouts);
    // NV-DXVK end
  }
  
  
  DxvkComputePipeline::~DxvkComputePipeline() {
    if (m_noSpecConstantPipelines) {
      this->destroyPipeline(m_noSpecConstantPipelines->pipeline());
    }

    for (const auto& instance : m_pipelines)
      this->destroyPipeline(instance.pipeline());
  }
  
  
  VkPipeline DxvkComputePipeline::getPipelineHandle(
    const DxvkComputePipelineStateInfo& state) {
    DxvkComputePipelineInstance* instance = nullptr;

    { std::lock_guard<sync::Spinlock> lock(m_mutex);

      instance = this->findInstance(state);

      if (instance)
        return instance->pipeline();
    
      // If no pipeline instance exists with the given state
      // vector, create a new one and add it to the list.
      instance = this->createInstance(state);
    }
    
    if (!instance)
      return VK_NULL_HANDLE;

    // Note: Only write pipelines with actual spec constant state to the cache as
    // without this state there is nothing to cache (and circumventing this disk caching
    // dependency is part of the point of the flag to force no spec constants anyways).
    if (!m_shaders.forceNoSpecConstants) {
      this->writePipelineStateToCache(state);
    }

    return instance->pipeline();
  }


  void DxvkComputePipeline::compilePipeline(
    const DxvkComputePipelineStateInfo& state) {
    ScopedCpuProfileZone();

    std::lock_guard<sync::Spinlock> lock(m_mutex);

    if (!this->findInstance(state))
      this->createInstance(state);
  }
  
  
  DxvkComputePipelineInstance* DxvkComputePipeline::createInstance(
    const DxvkComputePipelineStateInfo& state) {
    VkPipeline newPipelineHandle = this->createPipeline(state);

    m_pipeMgr->m_numComputePipelines += 1;

    if (m_shaders.forceNoSpecConstants) {
      return &m_noSpecConstantPipelines.emplace(state, newPipelineHandle);
    } else {
      return &m_pipelines.emplace_back(state, newPipelineHandle);
    }
  }

  
  DxvkComputePipelineInstance* DxvkComputePipeline::findInstance(
    const DxvkComputePipelineStateInfo& state) {
    // Handle forced no spec constant case

    if (m_shaders.forceNoSpecConstants) {
      if (m_noSpecConstantPipelines) {
        return &*m_noSpecConstantPipelines;
      } else {
        return nullptr;
      }
    }

    // Handle typical pipeline state case

    for (auto& instance : m_pipelines) {
      if (instance.isCompatible(state))
        return &instance;
    }
    
    return nullptr;
  }
  
  
  VkPipeline DxvkComputePipeline::createPipeline(
    const DxvkComputePipelineStateInfo& state) const {
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    if (Logger::logLevel() <= LogLevel::Debug) {
      Logger::debug("Compiling compute pipeline..."); 
      Logger::debug(str::format("  cs  : ", m_shaders.cs->debugName()));
    }
    
    DxvkSpecConstants specData;

    // Note: Only set spec constants if they are needed.
    if (!m_shaders.forceNoSpecConstants) {
      for (uint32_t i = 0; i < m_layout->bindingCount(); i++)
        specData.set(i, state.bsBindingMask.test(i), true);

      for (uint32_t i = 0; i < MaxNumSpecConstants; i++)
        specData.set(getSpecId(i), state.sc.specConstants[i], 0u);
    }

    VkSpecializationInfo specInfo = specData.getSpecInfo();
    
    DxvkShaderModuleCreateInfo moduleInfo;
    moduleInfo.fsDualSrcBlend = false;

    auto csm = m_shaders.cs->createShaderModule(m_vkd, m_slotMapping, moduleInfo);

    VkComputePipelineCreateInfo info;
    info.sType                = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.pNext                = nullptr;
    info.flags                = 0;
    info.stage                = csm.stageInfo(&specInfo);
    info.layout               = m_layout->pipelineLayout();
    info.basePipelineHandle   = VK_NULL_HANDLE;
    info.basePipelineIndex    = -1;
    
    // Time pipeline compilation for debugging purposes
    dxvk::high_resolution_clock::time_point t0, t1;

    t0 = dxvk::high_resolution_clock::now();
    
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (m_vkd->vkCreateComputePipelines(m_vkd->device(),
          m_pipeMgr->m_cache->handle(), 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
      Logger::err("DxvkComputePipeline: Failed to compile pipeline");
      Logger::err(str::format("  cs  : ", m_shaders.cs->debugName()));
      return VK_NULL_HANDLE;
    }
    
    t1 = dxvk::high_resolution_clock::now();
    ds1LogSlowPipeline("compute", (m_shaders.cs->debugName() + " key " + m_shaders.cs->getShaderKey().toString() + (m_shaders.forceNoSpecConstants ? " no-sc" : " sc")).c_str(), std::chrono::duration<double, std::milli>(t1 - t0).count());
    if (Logger::logLevel() <= LogLevel::Debug) {
      auto td = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0);
      Logger::debug(str::format("DxvkComputePipeline: Finished in ", td.count(), " ms"));
    }

    return pipeline;
  }


  void DxvkComputePipeline::destroyPipeline(VkPipeline pipeline) {
    m_vkd->vkDestroyPipeline(m_vkd->device(), pipeline, nullptr);
  }
  
  
  void DxvkComputePipeline::writePipelineStateToCache(
    const DxvkComputePipelineStateInfo& state) const {
    assert(!m_shaders.forceNoSpecConstants);

    if (m_pipeMgr->m_stateCache == nullptr)
      return;
    
    DxvkStateCacheKey key;

    if (m_shaders.cs != nullptr)
      key.cs = m_shaders.cs->getShaderKey();

    m_pipeMgr->m_stateCache->addComputePipeline(key, state);
  }
  
}
