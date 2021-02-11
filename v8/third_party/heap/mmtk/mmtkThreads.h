#ifndef MMTK_THREADS_H
#define MMTK_THREADS_H

#include "mmtk.h"
#include "src/base/platform/platform.h"

namespace v8 {
namespace internal {
namespace third_party_heap {

class MMTkWorkerThread : public v8::base::Thread {
 public:
  MMTkWorkerThread(void* worker)
      : v8::base::Thread(v8::base::Thread::Options("MMTkWorkerThread")),
        worker_(worker) {}

  void Run();

 private:
  void* worker_;
};

class MMTkControllerThread : public v8::base::Thread {
 public:
  MMTkControllerThread()
      : v8::base::Thread(
            v8::base::Thread::Options("Controller Context Thread")) {}

  void Run();
};

}  // namespace third_party_heap
}  // namespace internal
}  // namespace v8

#endif  // MMTK_THREADS_H
