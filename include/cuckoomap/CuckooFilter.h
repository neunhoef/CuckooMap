#ifndef CUCKOO_FILTER_H
#define CUCKOO_FILTER_H 1

#include <cstdint>
#include <cstring>
#include <iostream>

#include "CuckooHelpers.h"

// In the following template:
//   Key is the key type, it must be copyable and movable, furthermore, Key
//     must be default constructible (without arguments) as empty and
//     must have an empty() method to indicate that the instance is
//     empty.
//     If using fasthash64 on all bytes of the object is not
//     a suitable hash function, one has to instanciate the template
//     with two hash function types as 3rd and 4th argument. If
//     std::equal_to<Key> is not implemented or does not behave correctly,
//     one has to supply a comparison class as well.
//   Value is the value type, it is not actually used anywhere in the
//     template except as Value* for input and output of values. The
//     template parameter basically only serves as a convenience to
//     provide defaults for valueAlign and valueSize and to reduce
//     casts. Values are passed in and out as a Value* to allow for
//     runtime configuration of the byte size and alignment. Within the
//     table no constructors or destructors or assignment operators are
//     called for Value, the data is only copied with std::memcpy. So Value
//     must only contain POD!
// This class is not thread-safe!

template <class Key, class HashKey = HashWithSeed<Key, 0xdeadbeefdeadbeefULL>,
          class HashShort = HashWithSeed<uint16_t, 0xabcdefabcdef1234ULL>,
          class CompKey = std::equal_to<Key>>
class CuckooFilter {
  // Note that the following has to be a power of two and at least 4!
  static constexpr uint32_t SlotsPerBucket = 4;

 public:
  CuckooFilter(uint64_t size) : _randState(0x2636283625154737ULL) {
    // Sort out offsets and alignments:
    _slotSize = sizeof(uint16_t);

    // First find the smallest power of two that is not smaller than size:
    size /= SlotsPerBucket;
    _size = 16;
    _logSize = 4;
    while (_size < size) {
      _size <<= 1;
      _logSize += 1;
    }
    _sizeMask = _size - 1;
    _sizeShift = (64 - _logSize) / 2;
    _allocSize = _size * _slotSize * SlotsPerBucket +
                 64;  // give 64 bytes padding to enable 64-byte alignment
    _allocBase = new char[_allocSize];

    _base = reinterpret_cast<char*>(
        (reinterpret_cast<uintptr_t>(_allocBase) + 63) &
        ~((uintptr_t)0x3fu));  // to actually implement the 64-btye alignment,
                               // shift base pointer within allocated space to
                               // 64-byte boundary

    // Now initialize all slots in all buckets with zero data:
    for (uint32_t b = 0; b < _size; ++b) {
      for (size_t i = 0; i < SlotsPerBucket; ++i) {
        uint16_t* f = findSlot(b, i);
        *f = 0;  //
      }
    }
  }

  ~CuckooFilter() { delete[] _allocBase; }

  CuckooFilter(CuckooFilter const&) = delete;
  CuckooFilter(CuckooFilter&&) = delete;
  CuckooFilter& operator=(CuckooFilter const&) = delete;
  CuckooFilter& operator=(CuckooFilter&&) = delete;

  bool lookup(Key const& k) {
    // look up a key, return either false if no pair with key k is
    // found or true.
    uint64_t hash1 = _hasherKey(k);
    uint64_t pos1 = hashToPos(hash1);
    uint16_t fingerprint = hashToFingerprint(hash1);
    // We compute the second hash already here to allow the result to
    // survive a mispredicted branch in the first loop. Is this sensible?
    uint64_t hash2 = _hasherPosFingerprint(pos1, fingerprint);
    uint64_t pos2 = hashToPos(hash2);
    for (uint64_t i = 0; i < SlotsPerBucket; ++i) {
      uint16_t* fTable = findSlot(pos1, i);
      if (fingerprint == *fTable) {
        return true;
      }
    }
    for (uint64_t i = 0; i < SlotsPerBucket; ++i) {
      uint16_t* fTable = findSlot(pos2, i);
      if (fingerprint == *fTable) {
        return true;
      }
    }
    return false;
  }

  void insert(Key& k) {
    // insert the key k
    //
    // The inserted key will have its fingerprint input entered in the table. If
    // there is a collision and a fingerprint needs to be cuckooed, a certain
    // number of attempts will be made. After that, a given fingerprint may
    // simply be expunged.
    uint16_t* fTable;

    uint64_t hash1 = _hasherKey(k);
    uint64_t pos1 = hashToPos(hash1);
    uint16_t fingerprint = hashToFingerprint(hash1);
    // We compute the second hash already here to let it survive a
    // mispredicted
    // branch in the first loop:
    uint64_t hash2 = _hasherPosFingerprint(pos1, fingerprint);
    uint64_t pos2 = hashToPos(hash2);

    for (uint64_t i = 0; i < SlotsPerBucket; ++i) {
      fTable = findSlot(pos1, i);
      if (!*fTable) {
        *fTable = fingerprint;
        ++_nrUsed;
        return;
      }
    }
    for (uint64_t i = 0; i < SlotsPerBucket; ++i) {
      fTable = findSlot(pos2, i);
      if (!*fTable) {
        *fTable = fingerprint;
        ++_nrUsed;
        return;
      }
    }

    uint8_t r = pseudoRandomChoice();
    if ((r & 1) != 0) {
      std::swap(pos1, pos2);
    }
    for (unsigned attempt = 0; attempt < 3; attempt++) {
      std::swap(pos1, pos2);
      // Now expunge a random element from any of these slots:
      uint64_t i = (r >> 1) & (SlotsPerBucket - 1);
      // We expunge the element at position pos1 and slot i:
      fTable = findSlot(pos1, i);
      uint16_t fDummy = *fTable;
      *fTable = fingerprint;
      fingerprint = fDummy;

      uint64_t hash2 = _hasherPosFingerprint(pos1, fingerprint);
      uint64_t pos2 = hashToPos(hash2);

      for (uint64_t i = 0; i < SlotsPerBucket; ++i) {
        fTable = findSlot(pos2, i);
        if (!*fTable) {
          *fTable = fingerprint;
          ++_nrUsed;
          return;
        }
      }
    }

    return;
  }

  bool remove(Key const& k) {
    // remove one element with key k, if one is in the table. Return true if
    // a key was removed and false otherwise.
    // look up a key, return either false if no pair with key k is
    // found or true.
    uint64_t hash1 = _hasherKey(k);
    uint64_t pos1 = hashToPos(hash1);
    uint16_t fingerprint = hashToFingerprint(hash1);
    // We compute the second hash already here to allow the result to
    // survive a mispredicted branch in the first loop. Is this sensible?
    uint64_t hash2 = _hasherPosFingerprint(pos1, fingerprint);
    uint64_t pos2 = hashToPos(hash2);
    for (uint64_t i = 0; i < SlotsPerBucket; ++i) {
      uint16_t* fTable = findSlot(pos1, i);
      if (fingerprint == *fTable) {
        *fTable = 0;
        return true;
      }
    }
    for (uint64_t i = 0; i < SlotsPerBucket; ++i) {
      uint16_t* fTable = findSlot(pos2, i);
      if (fingerprint == *fTable) {
        *fTable = 0;
        return true;
      }
    }
    return false;
  }

  uint64_t capacity() { return _size * SlotsPerBucket; }

  uint64_t nrUsed() { return _nrUsed; }

  uint64_t memoryUsage() { return sizeof(CuckooFilter) + _allocSize; }

 private:  // methods
  uint16_t* findSlot(uint64_t pos, uint64_t slot) {
    char* address = _base + _slotSize * (pos * SlotsPerBucket + slot);
    auto ret = reinterpret_cast<uint16_t*>(address);
    check(ret, true);
    return ret;
  }

  bool check(void* p, bool isKey) {
    char* address = reinterpret_cast<char*>(p);
    if ((address - _allocBase) + _slotSize - 1 >= _allocSize) {
      std::cout << "ALARM" << std::endl;
      return true;
    }
    return false;
  }

  uint64_t hashToPos(uint64_t hash) { return (hash >> _sizeShift) & _sizeMask; }

  uint16_t hashToFingerprint(uint64_t hash) {
    return (uint16_t)((hash ^ (hash >> 16) ^ (hash >> 32) ^ (hash >> 48)) &
                      0xFFFF);
  }

  uint64_t _hasherPosFingerprint(uint64_t pos, uint16_t fingerprint) {
    return (pos ^ _hasherShort(fingerprint));
  }

  uint8_t pseudoRandomChoice() {
    _randState = _randState * 997 + 17;  // ignore overflows
    return static_cast<uint8_t>((_randState >> 37) & 0xff);
  }

 private:               // member variables
  uint64_t _randState;  // pseudo random state for expunging

  size_t _slotSize;  // total size of a slot

  uint64_t _logSize;    // logarithm (base 2) of number of buckets
  uint64_t _size;       // number of buckets, == 2^_logSize
  uint64_t _sizeMask;   // used to mask out some bits from the hash
  uint32_t _sizeShift;  // used to shift the bits down to get a position
  uint64_t _allocSize;  // number of allocated bytes,
                        // == _size * SlotsPerBucket * _slotSize + 64
  char* _base;          // pointer to allocated space, 64-byte aligned
  char* _allocBase;     // base of original allocation
  uint64_t _nrUsed;     // number of pairs stored in the table

  HashKey _hasherKey;      // Instance to compute the first hash function
  HashShort _hasherShort;  // Instance to compute the second hash function
  CompKey _compKey;        // Instance to compare keys
};

#endif
