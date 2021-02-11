#include "mmtkThreads.h"

namespace v8 {
namespace internal {
namespace third_party_heap {

void MMTkWorkerThread::Run() { start_worker((void*)this, this->worker_); }

void MMTkControllerThread::Run() { start_control_collector((void*)this); }

}  // namespace third_party_heap
}  // namespace internal
}  // namespace v8
