#ifndef INTERNAL_CUCKOO_MAP_H
#define INTERNAL_CUCKOO_MAP_H 1

#include <cstring>

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

template<class Key,
         class Value,
         class HashKey1 = HashWithSeed<Key, 0xdeadbeefdeadbeefULL>,
         class HashKey2 = HashWithSeed<Key, 0xabcdefabcdef1234ULL>,
         class CompKey = std::equal_to<Key>>
class InternalCuckooMap {

  // Note that the following has to be a power of two!
  static constexpr uint32_t SlotsPerBucket = 2;

 public:

  InternalCuckooMap(uint64_t size, size_t valueSize = sizeof(Value),
                                   size_t valueAlign = alignof(Value))
    : _randState(0x2636283625154737ULL),
      _valueSize(valueSize), _valueAlign(valueAlign) {

    // Sort out offsets and alignments:
    _valueOffset = sizeof(Key);
    size_t mask = _valueAlign - 1;
    _valueOffset = (_valueOffset + _valueAlign - 1) & (~mask);
    size_t keyAlign = alignof(Key);
    // Align the key and thus the slot at least as strong as the value!
    // We assume two powers for all alignments.
    if (keyAlign < valueAlign) {
      keyAlign = valueAlign;
    }
    mask = keyAlign - 1;   
    _slotSize = _valueOffset + _valueSize;
    _slotSize = (_slotSize + alignof(Key) - 1) & (~mask);

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
    _allocSize = _size * _slotSize * SlotsPerBucket + 64;  // for alignment
    _allocBase = new char[_allocSize];

    _base = reinterpret_cast<char*>(
              (reinterpret_cast<uintptr_t>(_allocBase) + 63)
              & ~((uintptr_t)0x3fu) );

    try {
      _theBuffer = new char[_valueSize];
    } catch (...) {
      delete [] _allocBase;
      throw;
    }

    // Now initialize all slots in all buckets with empty pairs:
    for (uint32_t b = 0; b < _size; ++b) {
      for (size_t i = 0; i < SlotsPerBucket; ++i) {
        Key* k = findSlotKey(b, i);
        k = new (k) Key();    // placement new, default constructor
        Value* v = findSlotValue(b, i);
        std::memset(v, 0, _valueSize);
      }
    }
  }

  ~InternalCuckooMap() {
    // destroy objects:
    for (size_t b = 0; b < _size; ++b) {
      for (size_t i = 0; i < SlotsPerBucket; ++i) {
        Key* k = findSlotKey(b, i);
        k->~Key();
      }
    }
    delete [] _allocBase;
    delete [] _theBuffer;
  }

  bool lookup(Key const& k, Key*& kOut, Value*& vOut) {
    // look up a key, return either false if no pair with key k is
    // found or true. In the latter case the pointers kOut and vOut
    // are set to point to the pair in the table. This pointers are only
    // valid until the next operation on this table is called.
    uint64_t hash = _hasher1(k);
    uint64_t pos = hashToPos(hash);
    // We compute the second hash already here to allow the result to
    // survive a mispredicted branch in the first loop. Is this sensible?
    uint64_t hash2 = _hasher2(k);
    uint64_t pos2 = hashToPos(hash2);
    for (uint64_t i = 0; i < SlotsPerBucket; ++i) {
      Key* kTable = findSlotKey(pos, i);
      if (_compKey(*kTable, k)) {
        kOut = kTable;
        vOut = findSlotValue(pos, i);
        return true;
      }
    }
    for (uint64_t i = 0; i < SlotsPerBucket; ++i) {
      Key* kTable = findSlotKey(pos2, i);
      if (_compKey(*kTable, k)) {
        kOut = kTable;
        vOut = findSlotValue(pos2, i);
        return true;
      }
    }
    return false;
  }

  int insert(Key& k, Value* v) {
    // insert the pair (k, *v), unless there is already a pair with
    // key k.
    // 
    // If there is already a pair with key k, then -1 is returned
    // and the table is unchanged, in this case k and *v are
    // also unchanged. Otherwise, if there has not yet been a pair with
    // key k in the table, true is returned and the new pair is
    // inserted no matter what. If there is no collision then 0 is
    // returend and k and *v are unchanged. If however, a pair needs
    // to be expunged from the table, then k and *v are overwritten
    // with the values of the expunged pair and 1 is returned.
    //
    // Sample code to insert:
    //
    //    Key k;
    //    Value v;
    //    int res = 1;
    //    for (int count = 0; res == 1 && count < 20; ++count) {
    //      res = insert(k, &v);
    //    }
    //
    //  res in the end indicates whether
    //    -1 : there is already another pair with key k in the table
    //    0  : all is well
    //    1  : k, v is now another pair which has been expunged from the 
    //         table but the original one is inserted
    //

    Key *kTable;
    Value *vTable;

    uint64_t hash1 = _hasher1(k);
    uint64_t pos1 = hashToPos(hash1);
    // We compute the second hash already here to let it survive a mispredicted
    // branch in the first loop:
    uint64_t hash2 = _hasher2(k);
    uint64_t pos2 = hashToPos(hash2);

    for (uint64_t i = 0; i < SlotsPerBucket; ++i) {
      kTable = findSlotKey(pos1, i);
      if (kTable->empty()) {
        *kTable = k;
	vTable = findSlotValue(pos1, i);
	std::memcpy(vTable, v, _valueSize);
        return 0;
      }
      if (_compKey(*kTable, k)) {
        return -1;
      }
    }
    for (uint64_t i = 0; i < SlotsPerBucket; ++i) {
      kTable = findSlotKey(pos2, i);
      if (kTable->empty()) {
        *kTable = k;
	vTable = findSlotValue(pos2, i);
	std::memcpy(vTable, v, _valueSize);
        return 0;
      }
      if (_compKey(*kTable, k)) {
        return -1;
      }
    }

    // Now expunge a random element from any of these slots:
    uint8_t r = pseudoRandomChoice();
    if ((r & 1) != 0) {
      pos1 = pos2;
    }
    uint64_t i = (r >> 1) & (SlotsPerBucket - 1);
    // We expunge the element at position pos1 and slot i:
    kTable = findSlotKey(pos1, i);
    Key kDummy = std::move(*kTable);
    *kTable = std::move(k);
    k = std::move(kDummy);
    vTable = findSlotValue(pos1, i);
    std::memcpy(_theBuffer, vTable, _valueSize);
    std::memcpy(vTable, v, _valueSize);
    std::memcpy(v, _theBuffer, _valueSize);
    return 1;
  }

  void remove(Key* k, Value* v) {
    // remove the pair to which k and v point to in the table, this
    // pointer must have been returned by lookup before and no insert or
    // remove action must have been issued between that and this call.
    k->~Key();
    new (k) Key();
    std::memset(v, 0, _valueSize);
  }

  bool remove(Key const& k) {
    // remove the pair with key k, if one is in the table. Return true if
    // a pair was removed and false otherwise.
    Key* kTable;
    Value* vTable;
    if (!lookup(k, kTable, vTable)) {
      return false;
    }
    remove(kTable, vTable);
    return true;
  }

  uint64_t size() {
    return _size * SlotsPerBucket;
  }

 private:  // methods

  Key* findSlotKey(uint64_t pos, uint64_t slot) {
    char* address = _base + _slotSize * (pos * SlotsPerBucket + slot);
    auto ret = reinterpret_cast<Key*>(address);
    check(ret, true);
    return ret;
  }

  Value* findSlotValue(uint64_t pos, uint64_t slot) {
    char* address =_base + _slotSize * (pos * SlotsPerBucket + slot) 
                         + _valueOffset ;
    auto ret = reinterpret_cast<Value*>(address);
    check(ret, false);
    return ret;
  }

  bool check(void* p, bool isKey) {
    char* address = reinterpret_cast<char*>(p);
    if ((address - _allocBase) + (isKey ? _slotSize : _valueSize) - 1 >= _allocSize) {
      std::cout << "ALARM" << std::endl;
      return true;
    }
    return false;
  }

  uint64_t hashToPos(uint64_t hash) {
    return (hash >> _sizeShift) & _sizeMask;
  }

  uint8_t pseudoRandomChoice() {
    _randState = _randState * 997 + 17;  // ignore overflows
    return static_cast<uint8_t>((_randState >> 37) & 0xff);
  }

 private:  // member variables

  uint64_t _randState;   // pseudo random state for expunging

  size_t   _valueSize;   // size in bytes reserved for one element
  size_t   _valueAlign;  // alignment for value type
  size_t   _slotSize;    // total size of a slot
  size_t   _valueOffset; // offset from start of slot to value start

  uint64_t _logSize;    // logarithm (base 2) of number of buckets
  uint64_t _size;       // number of buckets, == 2^_logSize
  uint64_t _sizeMask;   // used to mask out some bits from the hash
  uint32_t _sizeShift;  // used to shift the bits down to get a position
  uint64_t _allocSize;  // number of allocated bytes,
                        // == _size * SlotsPerBucket * _slotSize + 64
  char* _base;          // pointer to allocated space, 64-byte aligned
  char* _allocBase;     // base of original allocation
  char* _theBuffer;     // pointer to an area of size _valueSize for value swap
  HashKey1 _hasher1;    // Instance to compute the first hash function
  HashKey2 _hasher2;    // Instance to compute the second hash function
  CompKey _compKey;     // Instance to compare keys
};

#endif
