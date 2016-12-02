#ifndef CUCKOO_FILTER_H
#define CUCKOO_FILTER_H 1

#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef CUCKOO_MAP_ANON
#ifdef MAP_ANONYMOUS
#define CUCKOO_MAP_ANON MAP_ANONYMOUS
#elif MAP_ANON
#define CUCKOO_MAP_ANON MAP_ANON
#endif
#endif

#include <cstdint>
#include <cstdio>
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
          class Fingerprint = HashWithSeed<Key, 0xabcdefabcdef1234ULL>,
          class HashShort = HashWithSeed<uint16_t, 0xfedcbafedcba4321ULL>,
          class CompKey = std::equal_to<Key>>
class CuckooFilter {
  // Note that the following has to be a power of two and at least 4!
  static constexpr uint32_t SlotsPerBucket = 4;

 public:
  CuckooFilter(bool useMmap, uint64_t size)
      : _useMmap(useMmap), _randState(0x2636283625154737ULL) {
    // Sort out offsets and alignments:
    _slotSize = sizeof(uint16_t);

    // Inflate size so that we have some padding to avoid failure
    size *= 2.0;
    size = (size >= 1024) ? size : 1024;  // want 256 buckets minimum

    // First find the smallest power of two that is not smaller than size:
    size /= SlotsPerBucket;
    _size = size;
    _niceSize = 256;
    _logSize = 8;
    while (_niceSize < size) {
      _niceSize <<= 1;
      _logSize += 1;
    }
    _sizeMask = _niceSize - 1;
    _sizeShift = (64 - _logSize) / 2;
    _maxRounds = _size;  // TODO: tune this
    _allocSize = _size * _slotSize * SlotsPerBucket +
                 64;  // give 64 bytes padding to enable 64-byte alignment

    if (_useMmap) {
      char* namePicked = std::tmpnam(_tmpFileName);
      if (namePicked == nullptr) {
        throw;
      }
      _tmpFile = open(_tmpFileName, O_RDWR | O_CREAT | O_TRUNC, (mode_t)0600);
      if (_tmpFile == -1) {
        throw;
      }
      try {
        int result = lseek(_tmpFile, _allocSize - 1, SEEK_SET);
        if (result == -1) {
          throw;
        }
        result = write(_tmpFile, "", 1);  // make the file a certain size
        if (result == -1) {
          throw;
        }

        _allocBase = reinterpret_cast<char*>(mmap(nullptr, _allocSize,
                                                  PROT_READ | PROT_WRITE,
                                                  MAP_SHARED, _tmpFile, 0));
        if (_allocBase == MAP_FAILED) {
          std::cout << "MAP_FAILED in filter" << std::endl;
          throw;
        }
      } catch (...) {
        close(_tmpFile);
        std::remove(_tmpFileName);
      }
      _base = _allocBase;
    } else {
      _allocBase = new char[_allocSize];

      _base = reinterpret_cast<char*>(
          (reinterpret_cast<uintptr_t>(_allocBase) + 63) &
          ~((uintptr_t)0x3fu));  // to actually implement the 64-byte alignment,
                                 // shift base pointer within allocated space to
                                 // 64-byte boundary
    }

    // Now initialize all slots in all buckets with zero data:
    for (uint32_t b = 0; b < _size; ++b) {
      for (size_t i = 0; i < SlotsPerBucket; ++i) {
        uint16_t* f = findSlot(b, i);
        *f = 0;  //
      }
    }
  }

  ~CuckooFilter() {
    if (_useMmap) {
      munmap(_allocBase, _allocSize);
      close(_tmpFile);
      std::remove(_tmpFileName);
    } else {
      delete[] _allocBase;
    }
  }

  CuckooFilter(CuckooFilter const&) = delete;
  CuckooFilter(CuckooFilter&&) = delete;
  CuckooFilter& operator=(CuckooFilter const&) = delete;
  CuckooFilter& operator=(CuckooFilter&&) = delete;

  bool lookup(Key const& k) const {
    // look up a key, return either false if no pair with key k is
    // found or true.
    uint64_t hash1 = _hasherKey(k);
    uint64_t pos1 = hashToPos(hash1);
    uint16_t fingerprint = keyToFingerprint(k);
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

  bool insert(Key& k) {
    // insert the key k
    //
    // The inserted key will have its fingerprint input entered in the table. If
    // there is a collision and a fingerprint needs to be cuckooed, a certain
    // number of attempts will be made. After that, a given fingerprint may
    // simply be expunged. If something is expunged, the function will return
    // false, otherwise true.
    uint16_t* fTable;

    uint64_t hash1 = _hasherKey(k);
    uint64_t pos1 = hashToPos(hash1);
    uint16_t fingerprint = keyToFingerprint(k);
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
        return true;
      }
    }
    for (uint64_t i = 0; i < SlotsPerBucket; ++i) {
      fTable = findSlot(pos2, i);
      if (!*fTable) {
        *fTable = fingerprint;
        ++_nrUsed;
        return true;
      }
    }

    uint8_t r = pseudoRandomChoice();
    if ((r & 1) != 0) {
      std::swap(pos1, pos2);
    }
    uint64_t i;
    uint16_t fDummy;
    for (unsigned attempt = 0; attempt < _maxRounds; attempt++) {
      std::swap(pos1, pos2);
      // Now expunge a random element from any of these slots:
      r = pseudoRandomChoice();
      i = r & (SlotsPerBucket - 1);
      // We expunge the element at position pos1 and slot i:
      fTable = findSlot(pos1, i);
      fDummy = *fTable;
      *fTable = fingerprint;
      fingerprint = fDummy;

      hash2 = _hasherPosFingerprint(pos1, fingerprint);
      pos2 = hashToPos(hash2);

      for (uint64_t i = 0; i < SlotsPerBucket; ++i) {
        fTable = findSlot(pos2, i);
        if (!*fTable) {
          *fTable = fingerprint;
          ++_nrUsed;
          return true;
        }
      }
    }

    return false;
  }

  bool remove(Key const& k) {
    // remove one element with key k, if one is in the table. Return true if
    // a key was removed and false otherwise.
    // look up a key, return either false if no pair with key k is
    // found or true.
    uint64_t hash1 = _hasherKey(k);
    uint64_t pos1 = hashToPos(hash1);
    uint16_t fingerprint = keyToFingerprint(k);
    // We compute the second hash already here to allow the result to
    // survive a mispredicted branch in the first loop. Is this sensible?
    uint64_t hash2 = _hasherPosFingerprint(pos1, fingerprint);
    uint64_t pos2 = hashToPos(hash2);
    for (uint64_t i = 0; i < SlotsPerBucket; ++i) {
      uint16_t* fTable = findSlot(pos1, i);
      if (fingerprint == *fTable) {
        *fTable = 0;
        _nrUsed--;
        return true;
      }
    }
    for (uint64_t i = 0; i < SlotsPerBucket; ++i) {
      uint16_t* fTable = findSlot(pos2, i);
      if (fingerprint == *fTable) {
        *fTable = 0;
        _nrUsed--;
        return true;
      }
    }
    return false;
  }

  uint64_t capacity() const { return _size * SlotsPerBucket; }

  uint64_t nrUsed() const { return _nrUsed; }

  uint64_t memoryUsage() const { return sizeof(CuckooFilter) + _allocSize; }

 private:  // methods
  uint16_t* findSlot(uint64_t pos, uint64_t slot) const {
    char* address = _base + _slotSize * (pos * SlotsPerBucket + slot);
    auto ret = reinterpret_cast<uint16_t*>(address);
    return ret;
  }

  uint64_t hashToPos(uint64_t hash) const {
    uint64_t relevantBits = (hash >> _sizeShift) & _sizeMask;
    return ((relevantBits < _size) ? relevantBits : (relevantBits - _size));
  }

  uint16_t keyToFingerprint(Key const& k) const {
    uint64_t hash = _fingerprint(k);
    uint16_t fingerprint = (uint16_t)(
        (hash ^ (hash >> 16) ^ (hash >> 32) ^ (hash >> 48)) & 0xFFFF);
    return (fingerprint ? fingerprint : 1);
  }

  uint64_t _hasherPosFingerprint(uint64_t pos, uint16_t fingerprint) const {
    return ((pos << _sizeShift) ^ _hasherShort(fingerprint));
  }

  uint8_t pseudoRandomChoice() {
    _randState = _randState * 997 + 17;  // ignore overflows
    return static_cast<uint8_t>((_randState >> 37) & 0xff);
  }

 private:               // member variables
  uint64_t _randState;  // pseudo random state for expunging

  size_t _slotSize;  // total size of a slot

  uint64_t _logSize;    // logarithm (base 2) of number of buckets
  uint64_t _size;       // actual number of buckets
  uint64_t _niceSize;   // smallest power of 2 at least number of buckets, ==
                        // 2^_logSize
  uint64_t _sizeMask;   // used to mask out some bits from the hash
  uint32_t _sizeShift;  // used to shift the bits down to get a position
  uint64_t _allocSize;  // number of allocated bytes,
                        // == _size * SlotsPerBucket * _slotSize + 64
  bool _useMmap;
  char* _base;  // pointer to allocated space, 64-byte aligned
  char _tmpFileName[L_tmpnam + 1];
  int _tmpFile;
  char* _allocBase;     // base of original allocation
  uint64_t _nrUsed;     // number of pairs stored in the table
  unsigned _maxRounds;  // maximum number of cuckoo rounds on insertion

  HashKey _hasherKey;        // Instance to compute the first hash function
  Fingerprint _fingerprint;  // Instance to compute a fingerprint of a key
  HashShort _hasherShort;    // Instance to compute the second hash function
  CompKey _compKey;          // Instance to compare keys
};

#endif
