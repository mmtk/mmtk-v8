#ifndef MMTK_LOG_H
#define MMTK_LOG_H

#define ENABLE_LOGGING false

#define MMTK_LOG(...)             \
  if (ENABLE_LOGGING) {           \
    fprintf(stderr, __VA_ARGS__); \
  }

#endif // MMTK_LOG_H