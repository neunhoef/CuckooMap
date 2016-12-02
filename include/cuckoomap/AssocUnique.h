////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2016 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler
/// @author Martin Schoenert
/// @author Michael Hackstein
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGODB_BASICS_ASSOC_UNIQUE_H
#define ARANGODB_BASICS_ASSOC_UNIQUE_H 1

#include <cassert>
#include <chrono>
#include <thread>
#include <vector>

#define TRI_ERROR_NO_ERROR 0
#define TRI_ERROR_ARANGO_UNIQUE_CONSTRAINT_VIOLATED -1

static uint64_t const Primes[251] = {
    7ULL,          11ULL,         13ULL,         17ULL,         19ULL,
    23ULL,         29ULL,         31ULL,         37ULL,         41ULL,
    47ULL,         53ULL,         59ULL,         67ULL,         73ULL,
    79ULL,         89ULL,         97ULL,         107ULL,        127ULL,
    137ULL,        149ULL,        163ULL,        179ULL,        193ULL,
    211ULL,        227ULL,        251ULL,        271ULL,        293ULL,
    317ULL,        347ULL,        373ULL,        401ULL,        431ULL,
    467ULL,        503ULL,        541ULL,        587ULL,        641ULL,
    691ULL,        751ULL,        809ULL,        877ULL,        947ULL,
    1019ULL,       1097ULL,       1181ULL,       1277ULL,       1381ULL,
    1487ULL,       1601ULL,       1733ULL,       1867ULL,       2011ULL,
    2179ULL,       2347ULL,       2531ULL,       2729ULL,       2939ULL,
    3167ULL,       3413ULL,       3677ULL,       3967ULL,       4273ULL,
    4603ULL,       4957ULL,       5347ULL,       5779ULL,       6229ULL,
    6709ULL,       7229ULL,       7789ULL,       8389ULL,       9041ULL,
    9739ULL,       10499ULL,      11311ULL,      12197ULL,      13147ULL,
    14159ULL,      15259ULL,      16433ULL,      17707ULL,      19069ULL,
    20543ULL,      22123ULL,      23827ULL,      25667ULL,      27647ULL,
    29789ULL,      32083ULL,      34583ULL,      37243ULL,      40111ULL,
    43201ULL,      46549ULL,      50129ULL,      53987ULL,      58147ULL,
    62627ULL,      67447ULL,      72643ULL,      78233ULL,      84263ULL,
    90749ULL,      97729ULL,      105251ULL,     113357ULL,     122081ULL,
    131477ULL,     141601ULL,     152501ULL,     164231ULL,     176887ULL,
    190507ULL,     205171ULL,     220973ULL,     237971ULL,     256279ULL,
    275999ULL,     297233ULL,     320101ULL,     344749ULL,     371281ULL,
    399851ULL,     430649ULL,     463781ULL,     499459ULL,     537883ULL,
    579259ULL,     623839ULL,     671831ULL,     723529ULL,     779189ULL,
    839131ULL,     903691ULL,     973213ULL,     1048123ULL,    1128761ULL,
    1215623ULL,    1309163ULL,    1409869ULL,    1518329ULL,    1635133ULL,
    1760917ULL,    1896407ULL,    2042297ULL,    2199401ULL,    2368589ULL,
    2550791ULL,    2747021ULL,    2958331ULL,    3185899ULL,    3431009ULL,
    3694937ULL,    3979163ULL,    4285313ULL,    4614959ULL,    4969961ULL,
    5352271ULL,    5763991ULL,    6207389ULL,    6684907ULL,    7199147ULL,
    7752929ULL,    8349311ULL,    8991599ULL,    9683263ULL,    10428137ULL,
    11230309ULL,   12094183ULL,   13024507ULL,   14026393ULL,   15105359ULL,
    16267313ULL,   17518661ULL,   18866291ULL,   20317559ULL,   21880459ULL,
    23563571ULL,   25376179ULL,   27328211ULL,   29430391ULL,   31694281ULL,
    34132321ULL,   36757921ULL,   39585457ULL,   42630499ULL,   45909769ULL,
    49441289ULL,   53244481ULL,   57340211ULL,   61750999ULL,   66501077ULL,
    71616547ULL,   77125553ULL,   83058289ULL,   89447429ULL,   96328003ULL,
    103737857ULL,  111717757ULL,  120311453ULL,  129566201ULL,  139532831ULL,
    150266159ULL,  161825107ULL,  174273193ULL,  187678831ULL,  202115701ULL,
    217663079ULL,  234406397ULL,  252437677ULL,  271855963ULL,  292767983ULL,
    315288607ULL,  339541597ULL,  365660189ULL,  393787907ULL,  424079291ULL,
    456700789ULL,  491831621ULL,  529664827ULL,  570408281ULL,  614285843ULL,
    661538611ULL,  712426213ULL,  767228233ULL,  826245839ULL,  889803241ULL,
    958249679ULL,  1031961197ULL, 1111342867ULL, 1196830801ULL, 1288894709ULL,
    1388040461ULL, 1494812807ULL, 1609798417ULL, 1733629067ULL, 1866985157ULL,
    2010599411ULL, 2165260961ULL, 2331819499ULL, 2511190229ULL, 2704358747ULL,
    2912386343ULL, 3136416067ULL, 3377678861ULL, 3637500323ULL, 3917308049ULL,
    4218639443ULL};

static_assert(sizeof(Primes) / sizeof(Primes[0]) == 251,
              "invalid prime table size");

////////////////////////////////////////////////////////////////////////////////
/// @brief return a prime number not lower than value
////////////////////////////////////////////////////////////////////////////////

uint64_t TRI_NearPrime(uint64_t value) {
  for (unsigned int i = 0; i < sizeof(Primes) / sizeof(Primes[0]); ++i) {
    if (Primes[i] >= value) {
      return Primes[i];
    }
  }
  return value;
}

static inline uint64_t TRI_IncModU64(uint64_t i, uint64_t len) {
  // Note that the dummy variable gives the compiler a (good) chance to
  // use a conditional move instruction instead of a branch. This actually
  // works on modern gcc.
  uint64_t dummy;
  dummy = (++i) - len;
  return i < len ? i : dummy;
}

namespace arangodb {
namespace basics {

struct BucketPosition {
  size_t bucketId;
  uint64_t position;

  BucketPosition() : bucketId(SIZE_MAX), position(0) {}

  void reset() {
    bucketId = SIZE_MAX - 1;
    position = 0;
  }

  bool operator==(BucketPosition const& other) const {
    return position == other.position && bucketId == other.bucketId;
  }
};

////////////////////////////////////////////////////////////////////////////////
/// @brief associative array
////////////////////////////////////////////////////////////////////////////////

template <class Key, class Element>
class AssocUnique {
 private:
  typedef void UserData;

 public:
  typedef std::function<uint64_t(UserData*, Key const*)> HashKeyFuncType;
  typedef std::function<uint64_t(UserData*, Element const&)>
      HashElementFuncType;
  typedef std::function<bool(UserData*, Key const*, uint64_t hash,
                             Element const&)>
      IsEqualKeyElementFuncType;
  typedef std::function<bool(UserData*, Element const&, Element const&)>
      IsEqualElementElementFuncType;

  typedef std::function<bool(Element&)> CallbackElementFuncType;

 private:
  struct Bucket {
    uint64_t _nrAlloc;  // the size of the table
    uint64_t _nrUsed;   // the number of used entries

    Element* _table;  // the table itself, aligned to a cache line boundary
  };

  std::vector<Bucket> _buckets;
  size_t _bucketsMask;

  HashKeyFuncType const _hashKey;
  HashElementFuncType const _hashElement;
  IsEqualKeyElementFuncType const _isEqualKeyElement;
  IsEqualElementElementFuncType const _isEqualElementElement;
  IsEqualElementElementFuncType const _isEqualElementElementByKey;

  std::function<std::string()> _contextCallback;

 public:
  AssocUnique(HashKeyFuncType hashKey, HashElementFuncType hashElement,
              IsEqualKeyElementFuncType isEqualKeyElement,
              IsEqualElementElementFuncType isEqualElementElement,
              IsEqualElementElementFuncType isEqualElementElementByKey,
              size_t numberBuckets = 1,
              std::function<std::string()> contextCallback =
                  []() -> std::string { return ""; })
      : _hashKey(hashKey),
        _hashElement(hashElement),
        _isEqualKeyElement(isEqualKeyElement),
        _isEqualElementElement(isEqualElementElement),
        _isEqualElementElementByKey(isEqualElementElementByKey),
        _contextCallback(contextCallback) {
    // Make the number of buckets a power of two:
    size_t ex = 0;
    size_t nr = 1;
    numberBuckets >>= 1;
    while (numberBuckets > 0) {
      ex += 1;
      numberBuckets >>= 1;
      nr <<= 1;
    }
    numberBuckets = nr;
    _bucketsMask = nr - 1;

    try {
      for (size_t j = 0; j < numberBuckets; j++) {
        _buckets.emplace_back();
        Bucket& b = _buckets.back();
        b._nrAlloc = initialSize();
        b._table = nullptr;

        // may fail...
        b._table = new Element[static_cast<size_t>(b._nrAlloc)]();
      }
    } catch (...) {
      for (auto& b : _buckets) {
        delete[] b._table;
        b._table = nullptr;
        b._nrAlloc = 0;
      }
      throw;
    }
  }

  ~AssocUnique() {
    for (auto& b : _buckets) {
      delete[] b._table;
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  /// @brief adhere to the rule of five
  //////////////////////////////////////////////////////////////////////////////

  AssocUnique(AssocUnique const&) = delete;             // copy constructor
  AssocUnique(AssocUnique&&) = delete;                  // move constructor
  AssocUnique& operator=(AssocUnique const&) = delete;  // op =
  AssocUnique& operator=(AssocUnique&&) = delete;       // op =

  //////////////////////////////////////////////////////////////////////////////
  /// @brief initial preallocation size of the hash table when the table is
  /// first created
  /// setting this to a high value will waste memory but reduce the number of
  /// reallocations/repositionings necessary when the table grows
  //////////////////////////////////////////////////////////////////////////////

 private:
  static uint64_t initialSize() { return 251; }

  //////////////////////////////////////////////////////////////////////////////
  /// @brief resizes the array
  //////////////////////////////////////////////////////////////////////////////

  void resizeInternal(UserData* userData, Bucket& b, uint64_t targetSize,
                      bool allowShrink) {
    if (b._nrAlloc >= targetSize && !allowShrink) {
      return;
    }

    std::string const cb(_contextCallback());

    Element* oldTable = b._table;
    uint64_t oldAlloc = b._nrAlloc;

    assert(targetSize > 0);

    targetSize = TRI_NearPrime(targetSize);

    // This might throw, is catched outside
    b._table = new Element[static_cast<size_t>(targetSize)]();

    b._nrAlloc = targetSize;

    if (b._nrUsed > 0) {
      uint64_t const n = b._nrAlloc;
      assert(n > 0);

      for (uint64_t j = 0; j < oldAlloc; j++) {
        Element const& element = oldTable[j];

        if (!(element.empty())) {
          uint64_t i, k;
          i = k = _hashElement(userData, element) % n;

          for (; i < n && !(b._table[i].empty()); ++i)
            ;
          if (i == n) {
            for (i = 0; i < k && !(b._table[i].empty()); ++i)
              ;
          }

          b._table[i] = element;
        }
      }
    }

    delete[] oldTable;
  }

  //////////////////////////////////////////////////////////////////////////////
  /// @brief check a resize of the hash array
  //////////////////////////////////////////////////////////////////////////////

  bool checkResize(UserData* userData, Bucket& b, uint64_t expected) {
    if (2 * b._nrAlloc < 3 * (b._nrUsed + expected)) {
      try {
        resizeInternal(userData, b, 2 * (b._nrAlloc + expected) + 1, false);
      } catch (...) {
        return false;
      }
    }
    return true;
  }

  //////////////////////////////////////////////////////////////////////////////
  /// @brief Finds the element at the given position in the buckets.
  ///        Iterates using the given step size
  //////////////////////////////////////////////////////////////////////////////

  Element findElementSequentialBucketsRandom(
      UserData* userData, BucketPosition& position, uint64_t const step,
      BucketPosition const& initial) const {
    Element found;
    Bucket b = _buckets[position.bucketId];
    do {
      found = b._table[position.position];
      position.position += step;
      while (position.position >= b._nrAlloc) {
        position.position -= b._nrAlloc;
        position.bucketId = (position.bucketId + 1) % _buckets.size();
        b = _buckets[position.bucketId];
      }
      if (position == initial) {
        // We are done. Return the last element we have in hand
        return found;
      }
    } while (!found);
    return found;
  }

  //////////////////////////////////////////////////////////////////////////////
  /// @brief Insert a document into the given bucket
  ///        This does not resize and expects to have enough space
  //////////////////////////////////////////////////////////////////////////////

  int doInsert(UserData* userData, Element const& element, Bucket& b,
               uint64_t hash) {
    uint64_t const n = b._nrAlloc;
    uint64_t i = hash % n;
    uint64_t k = i;

    for (; i < n && !(b._table[i].empty()) &&
           !_isEqualElementElementByKey(userData, element, b._table[i]);
         ++i)
      ;
    if (i == n) {
      for (i = 0; i < k && !(b._table[i].empty()) &&
                  !_isEqualElementElementByKey(userData, element, b._table[i]);
           ++i)
        ;
    }

    Element const& arrayElement = b._table[i];

    if (!(arrayElement.empty())) {
      return TRI_ERROR_ARANGO_UNIQUE_CONSTRAINT_VIOLATED;
    }

    b._table[i] = element;
    b._nrUsed++;

    return TRI_ERROR_NO_ERROR;
  }

 public:
  size_t buckets() const { return _buckets.size(); }

  //////////////////////////////////////////////////////////////////////////////
  /// @brief checks if this index is empty
  //////////////////////////////////////////////////////////////////////////////

  bool isEmpty() const {
    for (auto& b : _buckets) {
      if (b._nrUsed > 0) {
        return false;
      }
    }
    return true;
  }

  //////////////////////////////////////////////////////////////////////////////
  /// @brief get the hash array's memory usage
  //////////////////////////////////////////////////////////////////////////////

  size_t memoryUsage() const {
    size_t sum = 0;
    for (auto& b : _buckets) {
      sum += static_cast<size_t>(b._nrAlloc * sizeof(Element));
    }
    return sum;
  }

  //////////////////////////////////////////////////////////////////////////////
  /// @brief get the number of elements in the hash
  //////////////////////////////////////////////////////////////////////////////

  size_t size() const {
    size_t sum = 0;
    for (auto& b : _buckets) {
      sum += static_cast<size_t>(b._nrUsed);
    }
    return sum;
  }

  //////////////////////////////////////////////////////////////////////////////
  /// @brief finds an element equal to the given element.
  //////////////////////////////////////////////////////////////////////////////

  Element find(UserData* userData, Element const& element) const {
    uint64_t i = _hashElement(userData, element);
    Bucket const& b = _buckets[i & _bucketsMask];

    uint64_t const n = b._nrAlloc;
    i = i % n;
    uint64_t k = i;

    for (; i < n && b._table[i] &&
           !_isEqualElementElementByKey(userData, element, b._table[i]);
         ++i)
      ;
    if (i == n) {
      for (i = 0; i < k && b._table[i] &&
                  !_isEqualElementElementByKey(userData, element, b._table[i]);
           ++i)
        ;
    }

    // ...........................................................................
    // return whatever we found, this is nullptr if the thing was not found
    // and otherwise a valid pointer
    // ...........................................................................

    return b._table[i];
  }

  //////////////////////////////////////////////////////////////////////////////
  /// @brief finds an element given a key, returns a default-constructed Element
  /// if not found
  //////////////////////////////////////////////////////////////////////////////

  Element findByKey(UserData* userData, Key const* key) const {
    uint64_t hash = _hashKey(userData, key);
    uint64_t i = hash;
    uint64_t bucketId = i & _bucketsMask;
    Bucket const& b = _buckets[static_cast<size_t>(bucketId)];

    uint64_t const n = b._nrAlloc;
    i = i % n;
    uint64_t k = i;

    for (; i < n && !(b._table[i].empty()) &&
           !_isEqualKeyElement(userData, key, hash, b._table[i]);
         ++i)
      ;
    if (i == n) {
      for (i = 0; i < k && !(b._table[i].empty()) &&
                  !_isEqualKeyElement(userData, key, hash, b._table[i]);
           ++i)
        ;
    }

    // ...........................................................................
    // return whatever we found, this is nullptr if the thing was not found
    // and otherwise a valid pointer
    // ...........................................................................

    return b._table[i];
  }

  Element* findByKeyRef(UserData* userData, Key const* key) const {
    uint64_t hash = _hashKey(userData, key);
    uint64_t i = hash;
    uint64_t bucketId = i & _bucketsMask;
    Bucket const& b = _buckets[static_cast<size_t>(bucketId)];

    uint64_t const n = b._nrAlloc;
    i = i % n;
    uint64_t k = i;

    for (; i < n && b._table[i] &&
           !_isEqualKeyElement(userData, key, hash, b._table[i]);
         ++i)
      ;
    if (i == n) {
      for (i = 0; i < k && b._table[i] &&
                  !_isEqualKeyElement(userData, key, hash, b._table[i]);
           ++i)
        ;
    }

    // ...........................................................................
    // return whatever we found, this is nullptr if the thing was not found
    // and otherwise a valid pointer
    // ...........................................................................

    return &b._table[i];
  }

  //////////////////////////////////////////////////////////////////////////////
  /// @brief finds an element given a key, returns a default-constructed Element
  /// if not found
  /// also returns the internal hash value and the bucket position the element
  /// was found at (or would be placed into)
  //////////////////////////////////////////////////////////////////////////////

  Element findByKey(UserData* userData, Key const* key,
                    BucketPosition& position, uint64_t& hash) const {
    hash = _hashKey(userData, key);
    uint64_t i = hash;
    uint64_t bucketId = i & _bucketsMask;
    Bucket const& b = _buckets[static_cast<size_t>(bucketId)];

    uint64_t const n = b._nrAlloc;
    i = i % n;
    uint64_t k = i;

    for (; i < n && b._table[i] &&
           !_isEqualKeyElement(userData, key, hash, b._table[i]);
         ++i)
      ;
    if (i == n) {
      for (i = 0; i < k && b._table[i] &&
                  !_isEqualKeyElement(userData, key, hash, b._table[i]);
           ++i)
        ;
    }

    // if requested, pass the position of the found element back
    // to the caller
    position.bucketId = static_cast<size_t>(bucketId);
    position.position = i;

    // ...........................................................................
    // return whatever we found, this is nullptr if the thing was not found
    // and otherwise a valid pointer
    // ...........................................................................

    return b._table[i];
  }

  //////////////////////////////////////////////////////////////////////////////
  /// @brief adds an element to the array
  //////////////////////////////////////////////////////////////////////////////

  int insert(UserData* userData, Element const& element) {
    uint64_t hash = _hashElement(userData, element);
    Bucket& b = _buckets[hash & _bucketsMask];

    if (!checkResize(userData, b, 0)) {
      throw std::bad_alloc();
    }

    return doInsert(userData, element, b, hash);
  }

  //////////////////////////////////////////////////////////////////////////////
  /// @brief helper to heal a hole where we deleted something
  //////////////////////////////////////////////////////////////////////////////

  void healHole(UserData* userData, Bucket& b, uint64_t i) {
    // ...........................................................................
    // remove item - destroy any internal memory associated with the
    // element structure
    // ...........................................................................

    b._table[i] = Element();
    b._nrUsed--;

    uint64_t const n = b._nrAlloc;

    // ...........................................................................
    // and now check the following places for items to move closer together
    // so that there are no gaps in the array
    // ...........................................................................

    uint64_t k = TRI_IncModU64(i, n);

    while (!(b._table[k].empty())) {
      uint64_t j = _hashElement(userData, b._table[k]) % n;

      if ((i < k && !(i < j && j <= k)) || (k < i && !(i < j || j <= k))) {
        b._table[i] = b._table[k];
        b._table[k] = Element();
        i = k;
      }

      k = TRI_IncModU64(k, n);
    }

    if (b._nrUsed == 0) {
      resizeInternal(userData, b, initialSize(), true);
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  /// @brief removes an element from the array based on its key,
  /// returns nullptr if the element
  /// was not found and the old value, if it was successfully removed
  //////////////////////////////////////////////////////////////////////////////

  Element removeByKey(UserData* userData, Key const* key) {
    uint64_t hash = _hashKey(userData, key);
    uint64_t i = hash;
    Bucket& b = _buckets[i & _bucketsMask];

    uint64_t const n = b._nrAlloc;
    i = i % n;
    uint64_t k = i;

    for (; i < n && !(b._table[i].empty()) &&
           !_isEqualKeyElement(userData, key, hash, b._table[i]);
         ++i)
      ;
    if (i == n) {
      for (i = 0; i < k && !(b._table[i].empty()) &&
                  !_isEqualKeyElement(userData, key, hash, b._table[i]);
           ++i)
        ;
    }

    Element old = b._table[i];

    if (!old.empty()) {
      healHole(userData, b, i);
    }
    return old;
  }

  //////////////////////////////////////////////////////////////////////////////
  /// @brief removes an element from the array, returns nullptr if the element
  /// was not found and the old value, if it was successfully removed
  //////////////////////////////////////////////////////////////////////////////

  Element remove(UserData* userData, Element const& element) {
    uint64_t i = _hashElement(userData, element);
    Bucket& b = _buckets[i & _bucketsMask];

    uint64_t const n = b._nrAlloc;
    i = i % n;
    uint64_t k = i;

    for (; i < n && b._table[i] &&
           !_isEqualElementElement(userData, element, b._table[i]);
         ++i)
      ;
    if (i == n) {
      for (i = 0; i < k && b._table[i] &&
                  !_isEqualElementElement(userData, element, b._table[i]);
           ++i)
        ;
    }

    Element old = b._table[i];

    if (old) {
      healHole(userData, b, i);
    }

    return old;
  }

  /// @brief a method to iterate over all elements in the hash. this method
  /// can NOT be used for deleting elements
  void invokeOnAllElements(CallbackElementFuncType const& callback) {
    for (auto& b : _buckets) {
      if (b._table == nullptr) {
        continue;
      }
      if (!invokeOnAllElements(callback, b)) {
        return;
      }
    }
  }

  /// @brief a method to iterate over all elements in a bucket. this method
  /// can NOT be used for deleting elements
  bool invokeOnAllElements(CallbackElementFuncType const& callback, Bucket& b) {
    for (size_t i = 0; i < b._nrAlloc; ++i) {
      if (!b._table[i] || b._nrUsed == 0) {
        continue;
      }
      if (!b._table[i]) {
        continue;
      }
      if (!callback(b._table[i])) {
        return false;
      }
    }
    return true;
  }

  //////////////////////////////////////////////////////////////////////////////
  /// @brief a method to iterate over all elements in the hash. this method
  /// can be used for deleting elements as well
  //////////////////////////////////////////////////////////////////////////////

  void invokeOnAllElementsForRemoval(CallbackElementFuncType callback) {
    for (auto& b : _buckets) {
      if (b._table == nullptr || b._nrUsed == 0) {
        continue;
      }
      for (size_t i = 0; i < b._nrAlloc; /* no hoisting */) {
        if (!b._table[i]) {
          ++i;
          continue;
        }
        Element old = b._table[i];
        if (!callback(b._table[i])) {
          return;
        }
        if (b._nrUsed == 0) {
          break;
        }
        if (b._table[i] == old) {
          ++i;
        }
      }
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  /// @brief a method to iterate over all elements in the index in
  ///        a sequential order.
  ///        Returns nullptr if all documents have been returned.
  ///        Convention: position.bucketId == SIZE_MAX indicates a new start.
  ///        Convention: position.bucketId == SIZE_MAX - 1 indicates a restart.
  ///        During a continue the total will not be modified.
  //////////////////////////////////////////////////////////////////////////////

  Element findSequential(UserData* userData, BucketPosition& position,
                         uint64_t& total) const {
    if (position.bucketId >= _buckets.size()) {
      // bucket id is out of bounds. now handle edge cases
      if (position.bucketId < SIZE_MAX - 1) {
        return Element();
      }

      if (position.bucketId == SIZE_MAX) {
        // first call, now fill total
        total = 0;
        for (auto const& b : _buckets) {
          total += b._nrUsed;
        }

        if (total == 0) {
          return Element();
        }

        assert(total > 0);
      }

      position.bucketId = 0;
      position.position = 0;
    }

    while (true) {
      Bucket const& b = _buckets[position.bucketId];
      uint64_t const n = b._nrAlloc;

      for (; position.position < n && !b._table[position.position];
           ++position.position)
        ;

      if (position.position != n) {
        // found an element
        Element found = b._table[position.position];

        // move forward the position indicator one more time
        if (++position.position == n) {
          position.position = 0;
          ++position.bucketId;
        }

        return found;
      }

      // reached end
      position.position = 0;
      if (++position.bucketId >= _buckets.size()) {
        // Indicate we are done
        return Element();
      }
      // continue iteration with next bucket
    }
  }

  //////////////////////////////////////////////////////////////////////////////
  /// @brief a method to iterate over all elements in the index in
  ///        reversed sequential order.
  ///        Returns nullptr if all documents have been returned.
  ///        Convention: position === UINT64_MAX indicates a new start.
  //////////////////////////////////////////////////////////////////////////////

  Element findSequentialReverse(UserData* userData,
                                BucketPosition& position) const {
    if (position.bucketId >= _buckets.size()) {
      // bucket id is out of bounds. now handle edge cases
      if (position.bucketId < SIZE_MAX - 1) {
        return Element();
      }

      if (position.bucketId == SIZE_MAX && isEmpty()) {
        return Element();
      }

      position.bucketId = _buckets.size() - 1;
      position.position = _buckets[position.bucketId]._nrAlloc - 1;
    }

    Bucket b = _buckets[position.bucketId];
    Element found;
    do {
      found = b._table[position.position];

      if (position.position == 0) {
        if (position.bucketId == 0) {
          // Indicate we are done
          position.bucketId = _buckets.size();
          return Element();
        }

        --position.bucketId;
        b = _buckets[position.bucketId];
        position.position = b._nrAlloc - 1;
      } else {
        --position.position;
      }
    } while (!found);

    return found;
  }
};
}  // namespace basics
}  // namespace arangodb

#endif
