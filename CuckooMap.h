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
// This class is thread safe and can safely be used from multiple threads.
// Mutexes are built in, note that a lookup returns a `Finding` object which
// keeps a mutex until it is destroyed. This for example allows to change
// values that are actually currently stored in the map. Keys must only be
// changed as long as their hash and fingerprint does not change!

template<class Key,
         class Value,
         class HashKey1 = HashWithSeed<Key, 0xdeadbeefdeadbeefULL>,
         class HashKey2 = HashWithSeed<Key, 0xabcdefabcdef1234ULL>,
         class CompKey = std::equal_to<Key>>
class CuckooMap {

 public:
  typedef Key KeyType;       // these are for ShardedMap
  typedef Value ValueType;  
  typedef HashKey1 HashKey1Type;
  typedef HashKey2 HashKey2Type;
  typedef CompKey CompKeyType;

 private:
  size_t _firstSize;
  size_t _valueSize;
  size_t _valueAlign;

 public:

  CuckooMap(size_t firstSize,
            size_t valueSize = sizeof(Value),
            size_t valueAlign = alignof(Value))
    : _firstSize(firstSize),
      _valueSize(valueSize),
      _valueAlign(valueAlign) {

    auto t = new Subtable(firstSize, valueSize, valueAlign);
    try {
      _tables.emplace_back(t);
    } catch (...) {
      delete t;
      throw;
    }
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
    CuckooMap* _map;

   public:
    Finding(Key* k, Value* v, int32_t l, CuckooMap* m)
      : _key(k), _value(v), _layer(l), _map(m) {
    }

    ~Finding() {
      if (_layer >= 0) {
        _map->release(*this);
      }
    }

#if 0
    // Forbid copying and moving:
    Finding(Finding const& other) = delete;
    Finding(Finding&& other) = delete;
    Finding& operator=(Finding const& other) = delete;
    Finding& operator=(Finding && other) = delete;
#endif

    Finding(Finding const& other) {
      std::cout << "CuckooMap::Finding copy construct" << std::endl;
      _key = other._key;
      _value = other._value;
      _layer = other._layer;
      _map = other._map;
      other._layer = -1;
    }
    Finding(Finding&& other) {
      std::cout << "CuckooMap::Finding move construct" << std::endl;
      _key = other._key;
      _value = other._value;
      _layer = other._layer;
      _map = other._map;
      other._layer = -1;
    }
    Finding& operator=(Finding const& other) {
      std::cout << "CuckooMap::Finding copy assign" << std::endl;
      _key = other._key;
      _value = other._value;
      _layer = other._layer;
      _map = other._map;
      return *this;
    }
    Finding& operator=(Finding && other) {
      std::cout << "CuckooMap::Finding move assign" << std::endl;
      _key = other._key;
      _value = other._value;
      _layer = other._layer;
      _map = other._map;
      other._layer = -1;
      return *this;
    }
    // Return 1 if something was found and 0 otherwise. If this returns 0,
    // then key() and value() are undefined.
    int32_t found() {
      return _layer >= 0 ? 1 : 0;
    }

    void remove() {
      if (_layer >= 0) {
        _map->remove(*this);
      }
    }

    // The following is only relevant for CuckooMultiMaps, we add the method
    // here to keep the API consistent.
    bool next() {
      return false;
    }
  };

  Finding lookup(Key const& k) {
    // look up a key, return an object describing the findings. If
    // this->found() returns 0 then no pair with key k was found and the
    // pointers are undefined. If this->found() is a non-negative value,
    // then pointers key and value are set to point to the pair in the
    // table and one may modify *k and *v, provided the hash values of
    // *k do not change. Note that all accesses to the table are blocked
    // as long as the resulting object stays alive.
    // Usage example:
    //   Key k;
    //   { // this scope is necessary to unlock the table
    //     auto res = lookup(k);
    //     if (res.found() > 0) {
    //       // work with *res.key and *res.value
    //     }
    //   }

    MyMutexGuard guard(_mutex);

    int32_t layer = 0;
    Finding f(nullptr, nullptr, -1, this);
    while (static_cast<uint32_t>(layer) < _tables.size()) {
      Subtable& sub = *_tables[layer];
      Key* key;
      Value* value;
      if (sub.lookup(k, key, value)) {
        f._key = key;
        f._value = value;
        f._layer = layer;
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
    
    MyMutexGuard guard(_mutex);

    Key kCopy = k;
    char buffer[_valueSize];
    memcpy(buffer, v, _valueSize);
    Value* vCopy = reinterpret_cast<Value*>(&buffer);

    int32_t layer = 0;
    while (static_cast<uint32_t>(layer) < _tables.size()) {
      Subtable& sub = *_tables[layer];
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
    uint64_t lastSize = _tables.back()->size();
    auto t = new Subtable(lastSize * 4, _valueSize, _valueAlign);
    try {
      _tables.emplace_back(t);
    } catch (...) {
      delete t;
      throw;
    }
    while (_tables.back()->insert(kCopy, vCopy) > 0) { }
    return true;
  }

  bool remove(Key const& k) {
    // remove the pair with key k, if one is in the table. Return true if
    // a pair was removed and false otherwise.
    Finding f = lookup(k);
    if (f.found() == 0) {
      return false;
    }
    f.remove();
    return true;
  }

 private:
  void release(Finding& f) {
    _mutex.unlock();
  }

  void remove(Finding& f) {
    _tables[f._layer]->remove(f._key, f._value);
  }

  typedef InternalCuckooMap<Key, Value, HashKey1, HashKey2, CompKey> 
          Subtable;
  std::vector<std::unique_ptr<Subtable>> _tables;
  std::mutex _mutex;
};

#endif
