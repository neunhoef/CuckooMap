#ifndef CUCKOO_MAP_H
#define CUCKOO_MAP_H 1

#include <cstring>
#include <vector>
#include <mutex>
#include <memory>

#include "CuckooHelpers.h"
#include "InternalCuckooMap.h"

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
    Key* key() const {
      return _key;
    }
    Value* value() const {
      return _value;
    }
   private:
    Key* _key;
    Value* _value;
    int32_t _layer;
    uint32_t _shard;
    CuckooMap* _map;
   public:
    Finding(Key* k, Value* v, int32_t l, uint32_t s, CuckooMap* m)
      : _key(k), _value(v), _layer(l), _shard(s), _map(m) {
    }
    ~Finding() {
      if (_layer >= 0) {
        _map->release(*this);
      }
    }
    Finding(Finding const& other) {
      std::cout << "Copy construct" << std::endl;
      _key = other._key;
      _value = other._value;
      _layer = other._layer;
      _shard = other._shard;
      _map = other._map;
      other._layer = -1;
    }
    Finding(Finding&& other) {
      std::cout << "Move construct" << std::endl;
      _key = other._key;
      _value = other._value;
      _layer = other._layer;
      _shard = other._shard;
      _map = other._map;
      other._layer = -1;
    }
    Finding& operator=(Finding const& other) {
      std::cout << "Copy assign" << std::endl;
      _key = other._key;
      _value = other._value;
      _layer = other._layer;
      _shard = other._shard;
      _map = other._map;
      return *this;
    }
    Finding& operator=(Finding && other) {
      std::cout << "Move assign" << std::endl;
      _key = other._key;
      _value = other._value;
      _layer = other._layer;
      _shard = other._shard;
      _map = other._map;
      other._layer = -1;
      return *this;
    }
    bool found() {
      return _layer >= 0;
    }
    void remove() {
      if (_layer >= 0) {
        _map->remove(*this);
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
    Finding f(nullptr, nullptr, -1, 0, this);
    while (static_cast<uint32_t>(layer) < s.size()) {
      Subtable& sub = *s[layer];
      Key* key;
      Value* value;
      if (sub.lookup(k, key, value)) {
        f._key = key;
        f._value = value;
        f._layer = layer;
        f._shard = shard;
        guard.release();
        break;
      };
      
      ++layer;
    }
    return f;
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
    _mutexes[f._layer]->unlock();
  }

  void remove(Finding& f) {
    _tables[f._shard][f._layer]->remove(f._key, f._value);
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

#endif
