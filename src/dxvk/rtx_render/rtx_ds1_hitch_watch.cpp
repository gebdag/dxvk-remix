#include "rtx_ds1_hitch_watch.h"

#include "rtx_ds1_menu.h"
#include "../../util/log/log.h"
#include "../../util/util_string.h"

#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dxvk {

  namespace {

    constexpr int kMaxFrames = 40;
    constexpr int kMaxSamples = 60;
    constexpr int kSampleEveryMs = 50;
    constexpr int kShownFrames = 14;

    enum Slot { kRender = 0, kApi = 1, kSlots = 2 };
    const char* const kSlotNames[kSlots] = { "render thread", "API thread" };

    struct Sample {
      int slot = 0;
      int count = 0;
      void* frames[kMaxFrames] = {};
    };

    std::atomic<HANDLE> s_threads[kSlots] = {};
    std::atomic<DWORD> s_threadIds[kSlots] = {};
    std::atomic<int64_t> s_lastFrame { 0 };
    std::atomic<uint64_t> s_frames { 0 };
    std::atomic<bool> s_started { false };

    // The thread being walked and when the walk must be over: a walk that blocks (the suspended thread holds a lock the
    // unwinder wants) would leave that thread suspended for good, so the guardian resumes it.
    std::atomic<HANDLE> s_walked { nullptr };
    std::atomic<int64_t> s_walkDeadline { 0 };

    int64_t now() {
      LARGE_INTEGER t;
      QueryPerformanceCounter(&t);
      return t.QuadPart;
    }

    double toMs(int64_t ticks) {
      static const double msPerTick = []() {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return 1000.0 / static_cast<double>(f.QuadPart);
      }();
      return static_cast<double>(ticks) * msPerTick;
    }

    // The frames of a suspended thread, innermost first. No C++ objects: structured exception handling.
    int walk(HANDLE thread, void** frames) {
      int count = 0;
      __try {
        CONTEXT context;
        context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (!GetThreadContext(thread, &context)) {
          return 0;
        }
        while (count < kMaxFrames && context.Rip != 0) {
          frames[count++] = reinterpret_cast<void*>(context.Rip);
          DWORD64 imageBase = 0;
          PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &imageBase, nullptr);
          if (function != nullptr) {
            void* handlerData = nullptr;
            DWORD64 establisherFrame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, context.Rip, function, &context, &handlerData, &establisherFrame, nullptr);
          } else {
            // A leaf function: its return address is on top of its stack.
            context.Rip = *reinterpret_cast<DWORD64*>(context.Rsp);
            context.Rsp += 8;
          }
        }
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        // The thread ran on while it was walked (the guardian resumed it): what was read is kept up to here.
      }
      return count;
    }

    bool sample(int slot, Sample& out) {
      const HANDLE thread = s_threads[slot].load();
      if (thread == nullptr || SuspendThread(thread) == static_cast<DWORD>(-1)) {
        return false;
      }
      s_walkDeadline.store(now() + static_cast<int64_t>(250.0 / toMs(1)));
      s_walked.store(thread);
      out.slot = slot;
      out.count = walk(thread, out.frames);
      // Whoever takes the handle back resumes the thread: this thread, or the guardian if the walk overran.
      HANDLE expected = thread;
      const bool inTime = s_walked.compare_exchange_strong(expected, nullptr);
      if (inTime) {
        ResumeThread(thread);
      }
      return inTime && out.count > 0;
    }

    void guardian() {
      for (;;) {
        Sleep(50);
        const HANDLE thread = s_walked.load();
        if (thread != nullptr && now() > s_walkDeadline.load()) {
          HANDLE expected = thread;
          if (s_walked.compare_exchange_strong(expected, nullptr)) {
            ResumeThread(thread);
          }
        }
      }
    }

    std::string nameOf(void* address) {
      static std::mutex mutex;
      static bool initialized = false;
      std::lock_guard<std::mutex> lock(mutex);
      const HANDLE process = GetCurrentProcess();
      if (!initialized) {
        initialized = true;
        SymSetOptions(SymGetOptions() | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        if (!SymInitialize(process, nullptr, TRUE)) {
          SymRefreshModuleList(process);
        }
      }
      char buffer[sizeof(SYMBOL_INFO) + 256] = {};
      SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
      symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
      symbol->MaxNameLen = 255;
      DWORD64 displacement = 0;
      if (SymFromAddr(process, reinterpret_cast<DWORD64>(address), &displacement, symbol)) {
        return symbol->Name;
      }
      // No symbol: the module and the offset in it.
      HMODULE module = nullptr;
      char path[MAX_PATH] = {};
      if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             static_cast<LPCSTR>(address), &module) && module != nullptr &&
          GetModuleFileNameA(module, path, MAX_PATH) != 0) {
        const char* file = strrchr(path, '\\');
        return str::format(file != nullptr ? file + 1 : path, "+0x", std::hex,
                           reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module));
      }
      return str::format("0x", std::hex, reinterpret_cast<uintptr_t>(address));
    }

    void report(double gapMs, const std::vector<Sample>& samples) {
      // Equal stacks together, per thread.
      std::map<std::string, int> stacks[kSlots];
      int taken[kSlots] = {};
      std::map<void*, std::string> names;
      for (const Sample& s : samples) {
        std::string line;
        for (int i = 0; i < s.count && i < kShownFrames; i++) {
          auto found = names.find(s.frames[i]);
          if (found == names.end()) {
            found = names.emplace(s.frames[i], nameOf(s.frames[i])).first;
          }
          line += (i != 0 ? " < " : "") + found->second;
        }
        stacks[s.slot][line]++;
        taken[s.slot]++;
      }
      // Both threads waiting for work in every sample: the runtime was idle, the game sent nothing (its own loading or
      // waiting). One short line then.
      bool idle = true;
      for (const auto& [line, count] : stacks[kRender]) {
        idle &= line.find("SleepConditionVariableSRW < dxvk::DxvkCsThread::threadFunc") != std::string::npos;
      }
      for (const auto& [line, count] : stacks[kApi]) {
        idle &= line.find("::waitForData < Bridge<BridgeId::Device>::waitForCommand") != std::string::npos;
      }
      if (idle) {
        Logger::info(str::format("[DS1 hitch stacks] ", static_cast<int>(gapMs), " ms without a frame: the runtime was idle, waiting for the game (",
                                 taken[kRender], " + ", taken[kApi], " samples)"));
        return;
      }
      std::string text = str::format("[DS1 hitch stacks] ", static_cast<int>(gapMs), " ms without a frame, ",
                                     taken[kRender], " + ", taken[kApi], " samples");
      for (int slot = 0; slot < kSlots; slot++) {
        for (const auto& [line, count] : stacks[slot]) {
          text += str::format("\n    ", kSlotNames[slot], " ", count, "x: ", line);
        }
      }
      Logger::info(text);
    }

    void watchdog() {
      std::vector<Sample> samples;
      uint64_t frameSeen = s_frames.load();
      int64_t lateSince = 0;
      int64_t lastSample = 0;
      for (;;) {
        Sleep(10);
        const int limitMs = Ds1HitchWatch::hitchStackMs();
        const uint64_t frame = s_frames.load();
        const int64_t t = now();
        if (frame != frameSeen) {
          // A frame came through: the hitch, if there was one, is over.
          if (!samples.empty()) {
            report(toMs(t - lateSince), samples);
            samples.clear();
          }
          frameSeen = frame;
          continue;
        }
        const int64_t last = s_lastFrame.load();
        if (limitMs <= 0 || last == 0 || toMs(t - last) < limitMs) {
          continue;
        }
        if (samples.empty()) {
          lateSince = last;
        }
        if (samples.size() >= kMaxSamples * kSlots || toMs(t - lastSample) < kSampleEveryMs) {
          continue;
        }
        lastSample = t;
        for (int slot = 0; slot < kSlots; slot++) {
          Sample s;
          if (sample(slot, s)) {
            samples.push_back(s);
          }
        }
      }
    }

    void nameThread(int slot) {
      const DWORD id = GetCurrentThreadId();
      if (s_threadIds[slot].load(std::memory_order_relaxed) == id) {
        return;
      }
      HANDLE handle = nullptr;
      if (DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &handle,
                          THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0)) {
        // The old handle is left open: the watchdog may be using it.
        s_threads[slot].store(handle);
        s_threadIds[slot].store(id);
      }
    }

    void start() {
      bool expected = false;
      if (s_started.compare_exchange_strong(expected, true)) {
        std::thread(watchdog).detach();
        std::thread(guardian).detach();
      }
    }

  }

  void Ds1HitchWatch::onApiPresent() {
    if (DxvkDs1Dev::enable()) {
      nameThread(kApi);
    }
  }

  void Ds1HitchWatch::onFrame() {
    if (!DxvkDs1Dev::enable()) {
      return;
    }
    nameThread(kRender);
    s_lastFrame.store(now());
    s_frames.fetch_add(1);
    start();
  }

}
