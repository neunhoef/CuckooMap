#ifndef CUCKOO_MAP_H
#define CUCKOO_MAP_H 1

#include <iostream>
#include <memory>
#include <mutex>
#include <vector>

#include "InternalCuckooMap.h"

// In the following template:
//   Key is the key type, it must be copyable and movable, furthermore, Key
//     must be default constructible (without arguments) as empty and
//     must have an empty() method to indicate that the instance is
//     empty. If using fasthash64 on all bytes of the object is not
//     a suitable hash function, one has to instanciate the template
//     with two hash function types as 3rd and 4th argument. If
//     std::equal_to<Key> is not implemented or does not behave
//     correctly, one has to supply a comparison class as well.
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

template <class Key, class Value,
          class HashKey1 = HashWithSeed<Key, 0xdeadbeefdeadbeefULL>,
          class HashKey2 = HashWithSeed<Key, 0xabcdefabcdef1234ULL>,
          class CompKey = std::equal_to<Key>,
          class HashShort = HashWithSeed<uint16_t, 0xfedcbafedcba4321ULL>>
class CuckooMap {
 public:
  typedef Key KeyType;  // these are for ShardedMap
  typedef Value ValueType;
  typedef HashKey1 HashKey1Type;
  typedef HashKey2 HashKey2Type;
  typedef CompKey CompKeyType;
  typedef InternalCuckooMap<Key, Value, HashKey1, HashKey2, CompKey> Subtable;
  typedef CuckooFilter<Key, HashKey1, HashKey2, HashShort, CompKey> Filter;

 private:
  size_t _firstSize;
  size_t _valueSize;
  size_t _valueAlign;
  CompKey _compKey;

 public:
  CuckooMap(size_t firstSize, size_t valueSize = sizeof(Value),
            size_t valueAlign = alignof(Value), bool useFilters = false)
      : _firstSize(firstSize),
        _valueSize(valueSize),
        _valueAlign(valueAlign),
        _randState(0x2636283625154737ULL),
        _nrUsed(0),
        _useFilters(useFilters),
        _dummyFilter(false, 0) {
    auto t = new Subtable(false, firstSize, valueSize, valueAlign);
    try {
      _tables.emplace_back(t);
    } catch (...) {
      delete t;
      throw;
    }
    if (_useFilters) {
      auto f = new Filter(false, _tables.back()->capacity());
      try {
        _filters.emplace_back(f);
      } catch (...) {
        delete f;
        throw;
      }
    }
  }

  struct Finding {
    // This struct has two different duties: First it represents a guard
    // for the _mutex of a CuckooMap. Secondly, it indicates what the
    // result of a lookup was and allows subsequently to modify key
    // (provided the hash values remain the same) and the value. This means
    // that the Finding object can have a current pair or not. If it has a
    // crrent key/value pair, one can remove it and one can lookup
    // further keys or insert pairs whilst holding the mutex. The fact
    // whether or not a key was found is indicated by key() returning
    // a non-null pointer or nullptr. The instances releases the mutex
    // at destruction, if _map is not a nullptr.

    friend class CuckooMap;

    Key* key() const { return _key; }

    Value* value() const { return _value; }

   private:
    Key* _key;
    Value* _value;
    CuckooMap* _map;
    int32_t _layer;

   public:
    Finding(Key* k, Value* v, CuckooMap* m, int32_t l)
        : _key(k), _value(v), _map(m), _layer(l) {}

    Finding() : _key(nullptr), _value(nullptr), _map(nullptr), _layer(-1) {}

    ~Finding() {
      if (_map != nullptr) {
        _map->release();
      }
    }

    Finding(Finding const& other) = delete;
    Finding& operator=(Finding const& other) = delete;

    // Allow moving, we need this to allow for copy elision where we
    // return by value:
   public:
    Finding(Finding&& other) {
      std::cout << "move" << std::endl;
      if (_map != nullptr) {
        _map->release();
      }
      _map = other._map;
      other._map = nullptr;

      _key = other._key;
      _value = other._value;
      _layer = other._layer;
      other._key = nullptr;
    }

    Finding& operator=(Finding&& other) {
      std::cout << "move assign" << std::endl;
      if (_map != nullptr) {
        _map->release();
      }
      _map = other._map;
      other._map = nullptr;

      _key = other._key;
      _value = other._value;
      _layer = other._layer;
      other._key = nullptr;
      return *this;
    }

    // Return 1 if something was found and 0 otherwise. If this returns 0,
    // then key() and value() are undefined.
    int32_t found() { return (_map != nullptr && _key != nullptr) ? 1 : 0; }

    // The following are only relevant for CuckooMultiMaps, we add the method
    // here to keep the API consistent.

    bool next() { return false; }

    bool get(int32_t pos) { return false; }
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
    //       // work with *res.key() and *res.value()
    //     }
    //   }
    MyMutexGuard guard(_mutex);
    Finding f(nullptr, nullptr, this, -1);
    innerLookup(k, f, true);
    guard.release();
    return f;
  }

  bool lookup(Key const& k, Finding& f) {
    if (f._map != this) {
      f._map->_mutex.unlock();
      f._map = this;
      _mutex.lock();
    }
    f._key = nullptr;
    innerLookup(k, f, true);
    return f.found() > 0;
  }

  bool insert(Key const& k, Value const* v) {
    // inserts a pair (k, v) into the table
    // returns true if the insertion took place and false if there was
    // already a pair with the same key k in the table, in which case
    // the table is unchanged.
    MyMutexGuard guard(_mutex);
    return innerInsert(k, v, nullptr, -1);
  }

  bool insert(Key const& k, Value const* v, Finding& f) {
    if (f._map != this) {
      f._map->_mutex.unlock();
      f._map = this;
      _mutex.lock();
    }
    bool res = innerInsert(k, v, nullptr, -1);
    f._key = nullptr;
    return res;
  }

  bool remove(Key const& k) {
    // remove the pair with key k, if one is in the table. Return true if
    // a pair was removed and false otherwise.
    MyMutexGuard guard(_mutex);
    Finding f(nullptr, nullptr, this, -1);
    innerLookup(k, f, false);
    guard.release();
    if (f.found() == 0) {
      return false;
    }
    innerRemove(f);
    return true;
  }

  bool remove(Finding& f) {
    if (f._map != this) {
      f._map->_mutex.unlock();
      f._map = this;
      _mutex.lock();
    }
    if (f._key == nullptr) {
      return false;
    }
    innerRemove(f);
    return true;
  }

  uint64_t nrUsed() const {
    MyMutexGuard guard(_mutex);
    return _nrUsed;
  }

 private:
  void innerLookup(Key const& k, Finding& f, bool moveToFront) {
    char buffer[_valueSize];
    // f must be initialized with _key == nullptr
    for (int32_t layer = 0; static_cast<uint32_t>(layer) < _tables.size();
         ++layer) {
      Subtable& sub = *_tables[layer];
      Filter& filter = _useFilters ? *_filters[layer] : _dummyFilter;
      Key* key;
      Value* value;
      bool found = _useFilters ? (filter.lookup(k) && sub.lookup(k, key, value))
                               : sub.lookup(k, key, value);
      if (found) {
        f._key = key;
        f._value = value;
        f._layer = layer;
        if (moveToFront && layer > 0) {
          uint8_t fromBack = _tables.size() - layer;
          uint8_t denominator = (fromBack >= 6) ? (2 << 6) : (2 << fromBack);
          uint8_t mask = denominator - 1;
          uint8_t r = pseudoRandomChoice();
          if ((r & mask) == 0) {
            Key kCopy = *key;
            memcpy(buffer, value, _valueSize);
            Value* vCopy = reinterpret_cast<Value*>(&buffer);

            innerRemove(f);
            innerInsert(kCopy, vCopy, &f, layer - 1);
          }
        }
        return;
      };
    }
  }

  bool innerInsert(Key const& k, Value const* v, Finding* f, int layerHint) {
    // inserts a pair (k, v) into the table
    // returns true if the insertion took place and false if there was
    // already a pair with the same key k in the table, in which case
    // the table is unchanged.

    Key kCopy = k;
    Key originalKey = k;
    Key originalKeyAtLayer = k;
    char buffer[_valueSize];
    memcpy(buffer, v, _valueSize);
    Value* vCopy = reinterpret_cast<Value*>(&buffer);

    int32_t layer = (layerHint < 0) ? (_tables.size() - 1) : layerHint;
    int res;
    bool filterRes;
    while (static_cast<uint32_t>(layer) < _tables.size()) {
      Subtable& sub = *_tables[layer];
      Filter& filter = _useFilters ? *_filters[layer] : _dummyFilter;
      int maxRounds = (layerHint < 0) ? sub.maxRounds() : 8;
      for (int i = 0; i < maxRounds; ++i) {
        if (f != nullptr && _compKey(originalKey, kCopy)) {
          res = sub.insert(kCopy, vCopy, &(f->_key), &(f->_value));
          f->_layer = layer;
        } else {
          res = sub.insert(kCopy, vCopy, nullptr, nullptr);
        }
        if (res < 0) {  // key is already in the table
          return false;
        } else if (res == 0) {
          if (_useFilters) {
            filterRes = filter.insert(originalKeyAtLayer);
            if (!filterRes) {
              throw;
            }
          }
          ++_nrUsed;
          return true;
        }
      }
      if (_useFilters && !_compKey(kCopy, originalKeyAtLayer)) {
        filterRes = filter.remove(kCopy);
        if (!filterRes) {
          throw;
        }
        filterRes = filter.insert(originalKeyAtLayer);
        if (!filterRes) {
          throw;
        }
        originalKeyAtLayer = kCopy;
      }
      ++layer;
    }
    // If we get here, then some pair has been expunged from all tables and
    // we have to append a new table:
    uint64_t lastSize = _tables.back()->capacity();
    /*std::cout << "Insertion failure at level " << _tables.size() - 1 << " at "
              << 100.0 *
                     (((double)_tables.back()->nrUsed()) / ((double)lastSize))
              << "% capacity with cold " << coldInsert << std::endl;*/
    bool useMmap = (_tables.size() >= 3);
    auto t = new Subtable(useMmap, lastSize * 4, _valueSize, _valueAlign);
    try {
      _tables.emplace_back(t);
    } catch (...) {
      delete t;
      throw;
    }
    if (_useFilters) {
      auto fil = new Filter(useMmap, lastSize * 4);
      try {
        _filters.emplace_back(fil);
      } catch (...) {
        delete f;
        throw;
      }
    }
    originalKeyAtLayer = kCopy;
    while (res > 0) {
      if (f != nullptr && _compKey(originalKey, kCopy)) {
        res = _tables.back()->insert(kCopy, vCopy, &(f->_key), &(f->_value));
        f->_layer = layer;
      } else {
        res = _tables.back()->insert(kCopy, vCopy, nullptr, nullptr);
      }
    }
    if (_useFilters) {
      filterRes = _filters.back()->insert(originalKeyAtLayer);
      if (!filterRes) {
        throw;
      }
    }
    ++_nrUsed;
    return true;
  }

  uint8_t pseudoRandomChoice() {
    _randState = _randState * 997 + 17;  // ignore overflows
    return static_cast<uint8_t>((_randState >> 37) & 0xff);
  }

  void release() { _mutex.unlock(); }

  void innerRemove(Finding& f) {
    if (_useFilters) {
      _filters[f._layer]->remove(*(f._key));
    }
    _tables[f._layer]->remove(f._key, f._value);
    f._key = nullptr;
    --_nrUsed;
  }

  uint64_t _randState;  // pseudo random state for move-to-front heuristic
  std::vector<std::unique_ptr<Subtable>> _tables;
  std::vector<std::unique_ptr<Filter>> _filters;
  Filter _dummyFilter;
  std::mutex _mutex;
  uint64_t _nrUsed;
  bool _useFilters;
};

#endif
