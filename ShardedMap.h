#ifndef SHARDED_MAP_H
#define SHARDED_MAP_H 1

template<class InternalMap>
class ShardedMap {

  int32_t _logNrShards;     // logarithm base
  uint32_t _nrShards;       // = 2^_logNrShards
  uint64_t _shardMask;      // = _nrShards - 1

 public:

  ShardedMap(size_t firstSize,
             uint32_t nrShards = 8,
             size_t valueSize = sizeof(typename InternalMap::ValueType),
             size_t valueAlign = alignof(typename InternalMap::ValueType)) {

    _logNrShards = 0;
    _nrShards = 1;
    while (_nrShards < nrShards && _logNrShards < 16) {
      _logNrShards += 1;
      _nrShards <<= 1;
    }
    _shardMask = _nrShards - 1;

    _tables.reserve(_nrShards);
    for (uint32_t s = 0; s < _nrShards; ++s) {
      auto t = new InternalMap(firstSize, valueSize, valueAlign);
      try {
        _tables.emplace_back(t);
      } catch (...) {
        delete t;
        throw;
      }
    }
  }

  typename InternalMap::Finding lookup(typename InternalMap::KeyType const& k) {
    uint32_t shard = findShard(k);
    InternalMap& t = *_tables[shard];
    return t.lookup(k);
  }

  bool lookup(typename InternalMap::KeyType const& k,
              typename InternalMap::Finding& f) {
    uint32_t shard = findShard(k);
    InternalMap& t = *_tables[shard];
    return t.lookup(k, f);
  }

  bool insert(typename InternalMap::KeyType const& k,
              typename InternalMap::ValueType const* v) {
    uint32_t shard = findShard(k);
    InternalMap& t = *_tables[shard];
    return t.insert(k, v);
  }

  bool insert(typename InternalMap::KeyType const& k,
              typename InternalMap::ValueType const* v,
              typename InternalMap::Finding& f) {
    uint32_t shard = findShard(k);
    InternalMap& t = *_tables[shard];
    return t.insert(k, v, f);
  }

  bool remove(typename InternalMap::KeyType const& k) {
    uint32_t shard = findShard(k);
    InternalMap& t = *_tables[shard];
    return t.remove(k);
  }

  bool remove(typename InternalMap::Finding& f) {
    uint32_t shard = findShard(*f.key());
    InternalMap& t = *_tables[shard];
    return t.remove(f);
  }

 private:

  uint32_t findShard(typename InternalMap::KeyType const& k) {
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
    
  std::vector<std::unique_ptr<InternalMap>> _tables;
  typename InternalMap::HashKey1Type _hasher1;
};

#endif
