#ifndef CUCKOO_HELPERS_H
#define CUCKOO_HELPERS_H 1

#include <cstdint>
#include <mutex>

// For fasthash64:
static inline uint64_t mix(uint64_t h) {
  h ^= h >> 23;
  h *= 0x2127599bf4325c37ULL;
  h ^= h >> 47;
  return h;
}

// A default hash function:
uint64_t fasthash64(const void* buf, size_t len, uint64_t seed) {
  uint64_t const m = 0x880355f21e6d1965ULL;
  uint64_t const* pos = (uint64_t const*)buf;
  uint64_t const* end = pos + (len / 8);
  const unsigned char* pos2;
  uint64_t h = seed ^ (len * m);
  uint64_t v;

  while (pos != end) {
    v = *pos++;
    h ^= mix(v);
    h *= m;
  }

  pos2 = (const unsigned char*)pos;
  v = 0;

  switch (len & 7) {
    case 7:
      v ^= (uint64_t)pos2[6] << 48;
    case 6:
      v ^= (uint64_t)pos2[5] << 40;
    case 5:
      v ^= (uint64_t)pos2[4] << 32;
    case 4:
      v ^= (uint64_t)pos2[3] << 24;
    case 3:
      v ^= (uint64_t)pos2[2] << 16;
    case 2:
      v ^= (uint64_t)pos2[1] << 8;
    case 1:
      v ^= (uint64_t)pos2[0];
      h ^= mix(v);
      h *= m;
  }

  return mix(h);
}

// C++ wrapper for the hash function:
template<class T, uint64_t Seed>
class HashWithSeed {
 public:
  uint64_t operator()(T const& t) {
    // Some implementation like Fnv or xxhash looking at bytes in type T,
    // taking the seed into account.
    auto p = reinterpret_cast<void const*>(&t);
    return fasthash64(p, sizeof(T), Seed);
  }
};

class MyMutexGuard {
  std::mutex& _mutex;
  bool _locked;
 public:
  MyMutexGuard(std::mutex& m) : _mutex(m), _locked(true) {
    _mutex.lock();
  }
  ~MyMutexGuard() {
    if (_locked) {
      _mutex.unlock();
    }
  }
  void release() {
    if (_locked) {
      _locked = false;
      _mutex.unlock();
    }
  }
};

#endif
