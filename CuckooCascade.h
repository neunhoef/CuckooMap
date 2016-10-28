#include <cstring>
#include <vector>
#include <mutex>
#include <stdexcept>
#include <memory>

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

// In the following template:
//   Key is the key type, it must be copyable and movable, furthermore, Key
//     must be default constructible (without arguments) as empty and 
//     must have an empty() method to indicate that the instance is
//     empty, and a clear() method to make an initialized instance empty. 
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
class CuckooMap {

  size_t _firstSize;
  size_t _valueSize;
  size_t _valueAlign;
  int32_t _logNrShards;     // logarithm base
  uint32_t _nrShards;       // = 2^_logNrShards
  uint64_t _shardMask;      // = _nrShards - 1

 public:

  CuckooMap(size_t firstSize,
            uint32_t nrShards = 8,
            size_t valueSize = sizeof(Value),
            size_t valueAlign = alignof(Value))
    : _firstSize(firstSize),
      _valueSize(valueSize),
      _valueAlign(valueAlign) {

    _logNrShards = 0;
    _nrShards = 1;
    while (_nrShards < nrShards && _logNrShards < 16) {
      _logNrShards += 1;
      _nrShards <<= 1;
    }
    _shardMask = _nrShards - 1;

    _tables.reserve(_nrShards);
    for (uint32_t s = 0; s < _nrShards; ++s) {
      _tables.emplace_back();
      auto t = new Subtable(firstSize, valueSize, valueAlign);
      try {
        _tables.back().emplace_back(t);
      } catch (...) {
        delete t;
        throw;
      }
      _mutexes.emplace_back(new std::mutex());
    }
  }

  ~CuckooMap() {
  }

  struct Finding {
    friend class CuckooMap;
    Key* key;
    Value* value;
   private:
    int32_t layer;
    uint32_t shard;
    CuckooMap* map;
   public:
    Finding(Key* k, Value* v, int32_t l, uint32_t s, CuckooMap* m)
      : key(k), value(v), layer(l), shard(s), map(m) {
    }
    ~Finding() {
      if (layer >= 0) {
        map->release(*this);
      }
    }
    bool found() {
      return layer >= 0;
    }
    void remove() {
      if (layer >= 0) {
        map->remove(*this);
      }
    }
  };

  Finding lookup(Key const& k) {
    // look up a key, return an object describing the findings. If
    // .found is -1 then no pair with key k was found and the pointers
    // are nullptr. If .found is a non-negative value, then pointers key
    // and value are set to point to the pair in the table and one may
    // modify *k and *v, provided the hash values of *k do not change.
    // Note that all accesses to the table are blocked as long as the 
    // resulting object stays alive.
    // Usage example:
    //   Key k;
    //   { // this scope is necessary to unlock the table
    //     auto res = lookup(k);
    //     if (res.found) {
    //       // work with *res.key and *res.value
    //     }
    //   }

    uint32_t shard = findShard(k);
    std::vector<std::unique_ptr<Subtable>>& s = _tables[shard];
    
    MyMutexGuard guard(*_mutexes[shard]);

    int32_t layer = 0;
    while (static_cast<uint32_t>(layer) < s.size()) {
      Subtable& sub = *s[layer];
      Key* key;
      Value* value;
      if (sub.lookup(k, key, value)) {
        Finding f(key, value, layer, shard, this);
        guard.release();
        return f;
      };
      
      ++layer;
    }
    return Finding(nullptr, nullptr, -1, 0, this);
  }

  bool insert(Key const& k, Value const* v) {
    // inserts a pair (k, v) into the table
    // returns true if the insertion took place and false if there was
    // already a pair with the same key k in the table, in which case
    // the table is unchanged.
    
    uint32_t shard = findShard(k);
    std::vector<std::unique_ptr<Subtable>>& s = _tables[shard];
    
    MyMutexGuard guard(*_mutexes[shard]);

    Key kCopy = k;
    char buffer[_valueSize];
    memcpy(buffer, v, _valueSize);
    Value* vCopy = reinterpret_cast<Value*>(&buffer);

    int32_t layer = 0;
    while (static_cast<uint32_t>(layer) < s.size()) {
      Subtable& sub = *s[layer];
      for (int i = 0; i < 3; ++i) {
        int res = sub.insert(kCopy, vCopy);
        if (res < 0) {   // key is already in the table
          return false;
        } else if (res == 0) {
          return true;
        }
      }
      ++layer;
    }
    // If we get here, then some pair has been expunged from all tables and
    // we have to append a new table:
    uint64_t lastSize = s.back()->size();
    auto t = new Subtable(lastSize * 4, _valueSize, _valueAlign);
    try {
      s.emplace_back(t);
    } catch (...) {
      delete t;
      throw;
    }
    while (s.back()->insert(kCopy, vCopy) > 0) { }
    return true;
  }

  bool remove(Key const& k) {
    // remove the pair with key k, if one is in the table. Return true if
    // a pair was removed and false otherwise.
    Finding f = lookup(k);
    if (!f.found()) {
      return false;
    }
    f.remove();
    return true;
  }

 private:
  void release(Finding& f) {
    _mutexes[f.layer]->unlock();
  }

  void remove(Finding& f) {
    _tables[f.shard][f.layer]->remove(f.key, f.value);
  }

  uint32_t findShard(Key const& k) {
    uint64_t hash = _hasher1(k);
    hash = hash ^ (hash >> 32);
    hash = hash ^ (hash >> 16);
    if (_logNrShards <= 8) {
      hash = hash ^ (hash >> 8);
      return static_cast<uint32_t>(hash & _shardMask);
    } else {
      return static_cast<uint32_t>(hash & _shardMask);
    }
  }
    
  typedef InternalCuckooMap<Key, Value, HashKey1, HashKey2, CompKey> 
          Subtable;
  std::vector<std::vector<std::unique_ptr<Subtable>>> _tables;
  std::vector<std::unique_ptr<std::mutex>> _mutexes;
  HashKey1 _hasher1;    // Instance to compute the first hash function
};

