#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "CuckooHelpers.h"
#include "CuckooMap.h"

struct Key {
  int k;
  static constexpr char padding[KEY_PAD] = {0};
  Key() : k(0) {}
  Key(int i) : k(i) {}
  bool empty() { return k == 0; }
};

struct Value {
  int v;
  static constexpr char padding[VALUE_PAD] = {0};
  Value() : v(0) {}
  Value(int i) : v(i) {}
  bool empty() { return v == 0; }
};

namespace std {
template <>
struct equal_to<Key> {
  bool operator()(Key const& a, Key const& b) { return a.k == b.k; }
};
}

class RandomNumber {
 private:
  unsigned _current;

 public:
  RandomNumber(unsigned seed) : _current(seed) {}
  unsigned next() {
    // https://en.wikipedia.org/wiki/Linear_congruential_generator
    _current = (48271 * _current % 2147483647);
    return _current;
  }
  unsigned nextInRange(int range) {
    next();
    return next() % range;
  }
};

class WeightedSelector {
 private:
  RandomNumber _r;
  std::vector<unsigned> _cutoffs;

 public:
  WeightedSelector(unsigned seed, std::vector<double> weights) : _r(seed) {
    double totalWeight = 0.0;
    for (unsigned i = 0; i < weights.size(); i++) {
      totalWeight += weights[i];
      _cutoffs.push_back(std::ceil(totalWeight * 2147483647.0));
    }
  }
  unsigned next() {
    unsigned sample = _r.next();
    unsigned i = 0;
    for (; i < _cutoffs.size(); i++) {
      if (sample < _cutoffs[i]) break;
    }
    return i;
  }
};

typedef HashWithSeed<Key, 0xdeadbeefdeadbeefULL> KeyHash;
typedef std::unordered_map<Key, Value*, KeyHash> unordered_map;

class TestMap {
 private:
  int _useCuckoo;
  CuckooMap<Key, Value>* _cuckoo;
  unordered_map* _unordered;

 public:
  TestMap(int useCuckoo, size_t initialSize) : _useCuckoo(useCuckoo) {
    if (_useCuckoo) {
      _cuckoo = new CuckooMap<Key, Value>(initialSize);
    } else {
      _unordered = new unordered_map(initialSize);
    }
  }
  ~TestMap() {
    if (_useCuckoo) {
      delete _cuckoo;
    } else {
      delete _unordered;
    }
  }
  Value* lookup(Key const& k) {
    if (_useCuckoo) {
      auto element = _cuckoo->lookup(k);
      return (element.found() ? element.value() : nullptr);
    } else {
      auto element = _unordered->find(k);
      return (element == _unordered->end() ? (*element).second : nullptr);
    }
  }
  bool insert(Key const& k, Value const* v) {
    if (_useCuckoo) {
      return _cuckoo->insert(k, v);
    } else {
      return _unordered->emplace(k, v).second;
    }
  }
  bool remove(Key const& k) {
    if (_useCuckoo) {
      return _cuckoo->remove(k);
    } else {
      return (_unordered->erase(k) > 0);
    }
  }
};

// Usage: PerformanceTest [cuckoo] [nInitial] [nTotal] [nWorking] [pInsert]
//          [pLookup] [pRemove] [pWorking] [pMiss] [seed]
//    [cuckoo]: 1 = use CuckooMap; 0 = Use std::unordered_map
//    [nInitial]: Initial number of elements
//    [nTotal]: Maximum number of elements
//    [nWorking]: Size of working set
//    [pInsert]: Probability of insert
//    [pLookup]: Probability of lookup
//    [pRemove]: Probability of remove
//    [pWorking]: Probability of operation staying in working set
//    [pMiss]: Probability of lookup for missing element
//    [seed]: Seed for PRNG
int main(int argc, char* argv[]) {
  if (argc < 11) {
    std::cerr << "Incorrect number of parameters." << std::endl;
    exit(-1);
  }

  unsigned useCuckoo = atoi(argv[1]);
  unsigned nInitial = atoi(argv[2]);
  unsigned nTotal = atoi(argv[3]);
  unsigned nWorking = atoi(argv[4]);
  double pInsert = atof(argv[5]);
  double pLookup = atof(argv[6]);
  double pRemove = atof(argv[7]);
  double pWorking = atof(argv[8]);
  double pMiss = atof(argv[9]);
  unsigned seed = atoi(argv[10]);

  if (nInitial > nTotal || nWorking > nTotal) {
    std::cerr << "Invalid initial/total/working numbers." << std::endl;
    exit(-1);
  }

  if (pWorking < 0.0 || pWorking > 1.0) {
    std::cerr << "Keep 0 < pWorking < 1." << std::endl;
    exit(-1);
  }

  if (pMiss < 0.0 || pMiss > 1.0) {
    std::cerr << "Keep 0 < pMiss < 1." << std::endl;
    exit(-1);
  }

  RandomNumber r(seed);

  std::vector<double> weights;
  weights.push_back(pInsert);
  weights.push_back(pLookup);
  weights.push_back(pRemove);
  WeightedSelector operations(seed, weights);

  TestMap map(useCuckoo, nInitial);
  unsigned min = 0;
  unsigned max = 0;
  for (; max < nInitial; max++) {
    Key k(max);
    Value* v = new Value(max);
    bool success = map.insert(k, v);
    if (!success) {
      std::cout << "Failed to insert " << max << std::endl;
      exit(-1);
    }
  }

  for (unsigned i = min; i < max; i++) {
    Key k(i);
    Value* v = map.lookup(k);
    if (v == nullptr) {
      std::cout << "Failed to find " << i << std::endl;
      exit(-1);
    }
  }

  for (unsigned i = min; i < max; i++) {
    Key k(i);
    Value* v = map.lookup(k);
    bool success = map.remove(k);
    if (!success) {
      std::cout << "Failed to remove " << i << std::endl;
      exit(-1);
    }
    delete v;
  }

  std::cout << "Done." << std::endl;

  exit(0);
}
