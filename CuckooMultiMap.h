#ifndef CUCKOO_MULTI_MAP_H
#define CUCKOO_MULTI_MAP_H 1

#include <cstdint>
#include <cstring>
#include <memory>

#include "CuckooMap.h"

// In the following template:
//   Key is the key type, it must be copyable and movable, furthermore, Key
//     must be default constructible (without arguments).
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

template<class Key,
         class Value,
         class HashKey1 = HashWithSeed<Key, 0xdeadbeefdeadbeefULL>,
         class HashKey2 = HashWithSeed<Key, 0xabcdefabcdef1234ULL>,
         class CompKey = std::equal_to<Key>>
class CuckooMultiMap {

  struct InnerKey : Key {
    int32_t seq;
    InnerKey() : Key(), seq(0) {
    }
    InnerKey(Key const& other, int32_t s) : Key(other), seq(s) {
    }
    bool empty() {
      return seq == 0;
    }
  };

  struct InnerHashKey1 {
    HashKey1 hasher;
    uint64_t operator()(InnerKey const& k) {
      uint64_t hash = hasher(k.k);
      int32_t seq = k.seq > 0 ? k.seq : 0;
      return fasthash64(&seq, sizeof(seq), hash);
    }
  };

  struct InnerHashKey2 {
    HashKey2 hasher;
    uint64_t operator()(InnerKey const& k) {
      uint64_t hash = hasher(k.k);
      int32_t seq = k.seq > 0 ? k.seq : 0;
      return fasthash64(&seq, sizeof(seq), hash);
    }
  };

  struct InnerCompKey {
    CompKey comp;
    bool operator()(InnerKey const& a, InnerKey const& b) {
      if (!comp(a.k, b.k)) {
        return false;
      }
      int32_t aSeq = a.seq > 0 ? a.seq : 0;
      int32_t bSeq = b.seq > 0 ? b.seq : 0;
      return aSeq == bSeq;
    }
  };

  typedef CuckooMap<InnerKey, Value, InnerHashKey1, InnerHashKey2, InnerCompKey>
          InnerCuckooMap;

  InnerCuckooMap _innerMap;
  size_t _valueSize;

 public:

  typedef Key KeyType;       // these are for ShardedMap
  typedef Value ValueType;  
  typedef HashKey1 HashKey1Type;
  typedef HashKey2 HashKey2Type;
  typedef CompKey CompKeyType;

  CuckooMultiMap(size_t firstSize,
                 size_t valueSize = sizeof(Value),
                 size_t valueAlign = alignof(Value))
    : _innerMap(firstSize, valueSize, valueAlign), _valueSize(valueSize) {
  }

  // Destruction, copying and moving exactly as CuckooMap

  // This struct basically behaves like the corresponding struct in CuckooMap.
  // Additionally, it maintains the number of elements with the current key
  // and the current position, basically providing iterator functionality.
  // The object holds a mutex if and only if _innerFinding does. It contains
  // a current key if and only if _innerFinding does.

  struct Finding {
    friend class CuckooMultiMap;

   private:
    CuckooMultiMap* _map;
    InnerKey _innerKey;
    typename InnerCuckooMap::Finding _innerFinding;
    int32_t _count;
   public:
    Finding(Key const& k, CuckooMultiMap* m)
      : _map(m), _innerKey(k, 0), _innerFinding(m->_innerMap.lookup(_innerKey)),
        _count(_innerFinding.found() == 0 ? 0 : -_innerFinding.key()->seq) {
    }

    // This class is automatically movable (because InnerKey and _innerFinding
    // are, and not copyable, because _innerFinding is not. Destruction
    // destructs _innerFinding and thus releases the mutex.

    int32_t found() {
      if (_innerFinding.found() == 0) {
        return 0;
      }
      return _count;
    }

    Key* key() {
      return static_cast<Key*>(_innerFinding.key());
    }

    Value* value() {
      return _innerFinding.value();
    }

    bool next() {
      if (_map == nullptr && _innerFinding.found() == 0) {
        return false;
      }
      if (_innerKey.seq+1 < _count) {
        ++_innerKey.seq;
        return _map->_innerMap.lookup(_innerKey, _innerFinding);
      }
      return false;
    }

    bool get(int32_t pos) {
      if (_map == nullptr &&_innerFinding.found() == 0) {
        return false;
      }
      if (pos < 0 || pos >= _count) {
        return false;
      }
      _innerKey.seq = pos;
      return _map->_innerMap.lookup(_innerKey, _innerFinding);
    }
  };

  Finding lookup(Key const& k) {
    // look up a key, return an object describing the findings. If
    // this->found() returns 0 then no pair with key k was found and the
    // pointers are nullptr. If this->found() is a non-negative value,
    // then pointers key and value are set to point to a pair in the
    // table and one may modify *k and *v, provided the hash values of
    // *k do not change. Note that all accesses to the table are blocked
    // as long as the resulting object stays alive.
    // Usage example:
    //   Key k;
    //   { // this scope is necessary to unlock the table
    //     auto res = lookup(k);
    //     if (res.found) {
    //       do {
    //         work with *res.key() and *res.value()
    //       } while(res.next());
    //     }
    //   }
    return Finding(k, this);
  }

  bool lookup(Key const& k, Finding& f) {
    *static_cast<Key*>(&f._innerKey) = k;
    f._innerKey.seq = 0;
    if (!_innerMap.lookup(f._innerKey, f)) {
      f._count = 0;
      return false;
    }
    f._count = -f._innerFinding.key()->seq;
    return true;
  }

  bool insert(Key const& k, Value const* v) {
    // inserts a pair (k, v) into the table
    // returns true if the insertion took place and false if there was
    // already a pair with the same key k in the table.
    Finding f(k, this);
    return innerInsert(f, v);
  }
 
  bool insert(Key const& k, Value const* v, Finding& f) {
    *static_cast<Key*>(&f._innerKey) = k;
    f._innerKey.seq = 0;
    _innerMap.lookup(f._innerKey, f._innerFinding);
    return innerInsert(f, v);
  }

  bool remove(Key const& k) {
    Finding f(k, this);
    if (f._count == 0) {
      return false;
    }
    for (int32_t i = f._count-1; i >= 0; --i) {
      f._innerKey.seq = i;
      _innerMap.lookup(f._innerKey, f._innerFinding);
      _innerMap.remove(f._innerFinding);
    }
    return true;
  }

  bool remove(Finding& f) {
    if (f.innerFinding.key() == nullptr) {
      return false;
    }
    // We can assume that f._innerKey is the one to be removed and that
    // f._innerFinding has found that one in _innerMap just before.
    int32_t pos = f._innerKey.seq;
    if (pos < f._count-1) {
      Value* v = f._innerFinding.value();
      f._innerKey.seq = f._count-1;
      _innerMap.lookup(f._innerKey, f._innerFinding);
      memcpy(v, f._innerFinding.value(), _valueSize);
    }
    _innerMap.remove(f._innerFinding);
    if (pos > 0) {
      f._innerKey.seq = 0;
      _innerMap.lookup(f._innerKey, f._innerFinding);
      ++f._innerFinding.key()->seq;
    }
    return true;
  }

 private:
  bool innerInsert(Finding& f, Value const* v) {
    // f must just have been used to look for f._innerKey
    if (f.found() == 0) {
      // First with this key, and we now hold the mutex
      f._innerKey.seq = -1;
      _innerMap.insert(f._innerKey, v, f._innerFinding);
      return true;
    }
    // There are already nr pairs with key k:
    f._innerKey.seq = -(f._innerFinding.key()->seq--);
    _innerMap.insert(f._innerKey, v, f._innerFinding);
    return true;
  }
};

#endif
