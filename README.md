CuckooMap
=========

This is an implementation experiment of a new style key/value table using
a cascade of ever larger cuckoo hash tables and associated cuckoo filters to
combine the speed of hash lookups with data locality properties for a hot
subset of the data.

This is experimental code.

There are altogether four classes with different properties, all with
the same interface:

  - `CuckooMap`

      - cascade of `InternalCuckooMap`s, each constant size
      - sizes grow exponentially
      - hot set is heuristically kept in the early, smaller tables
      - Cuckoo filters are used to have a fast path if a key is not in
        the table
      - unique keys
      - thread-safe
      - keys must be movable and copyable and default constructable and
        must have an `empty()` method to indicate an empty value.
        Default-constructed keys must be empty.
      - values must be memcpy-able and must not rely on proper
        construction and destruction, use POD data!
      - one can specify custom size and alignment of the value type
      - CuckooMaps are not default constructable, not copyable and not
        movable. They properly destruct keys stored in the table but do
        not destruct values.

  - `CuckooMultiMap`

    As `CuckooMap`, but:

      - based on CuckooMap by deriving the Key class and adding one int32_t
      - keys may be repeated
      - linear time to find all pairs with a given key
      - constant time to find a certain pair

  - `ShardedMap<CuckooMap>`

    As CuckooMap, but with a configurable number of shards (pairs are
    distributed amongst the shards according to a hash function on the key).

  - `ShardedMap<CuckooMultiMap>`

    As CuckooMultiMap, but with a configurable number of shards (pairs are
    distributed amongst the shards according to a hash function on the key).

The interface basically allows the following operations:

  1. lookup a pair with a given key, returning a `Finding` object
  2. use an existing Finding object to do another lookup in the same shard,
     this happens still under the mutex (see below).
  3. insert of a pair
  4. insert a new pair using the mutex in an existing `Finding` object
  5. remove all pairs with a given key
  6. remove a pair referenced by a `Finding` object.

`Finding` objects are returned by value by the lookup method with return
value optimization, that is, they are directly built up at the caller's
site.

A `Finding` object has two objectives:

  - It keeps a mutex on the (shard of) the map, in which the key was
    found. This mutex is kept as long as the `Finding` object lives.
  - It allows modification of the key (only up to hash value changes)
    and the value **in the map**.

Furthermore, one can perform the following operations on a `Finding`
object:

  - `int32_t found()` returns 0 if no key was found, or otherwise the
    number of pairs with the given key in the map (at most 1 for unique
    key maps).
  - `Key* key()` returns a pointer to the key of the found pair in the map.
  - `Value* value()` returns a pointer to the value of the found pair in
    the map.
  - `bool next()` moves the lookup to the next pair with the same key.
    Returns `false` if there is no more such pair and `true` otherwise.
  - `bool get(int32_t pos)` moves the lookup to the pair with the same key,
    `pos` needs to be non-negative and less than the number of pairs
    with the key in the table

`Finding` objects cannot be copied but can be moved.

Performance Testing
-------------------

We include a performance testing tool which runs a configurable randomized workload and can compare the performance of `CuckooMap` against that of `std::unordered_map`. The `PerformanceTest` executable takes 11 parameters as follows:

  - `useCuckoo`: Set to 0 to use `std::unordered_map` and 1 to use `CuckooMap`
  - `nOpCount`: The target number of operations to run in total; Integer, 0 < `opCount`
  - `nInitialSize`: The initial size of the table; Integer, 0 < `nInitialSize`
  - `nMaxSize`: The maximum number of elements in the set at any given time; Integer, 0 < `nMaxSize`
  - `nWorking`: The size of the 'hot' set; Integer, 0 < `nWorking`
  - `pInsert`: The probability that the chosen operation will be an `insert`; Double, 0 <= `pInsert` <= 1
  - `pLookup`: The probability that the chosen operation will be a `lookup`; Double, 0 <= `pLookup` <= 1
  - `pRemove`: The probability that the chosen operation will be a `remove`; Double, 0 <= `pRemove` <= 1
  - `pWorking`: The probability that a `lookup` or `remove` operation will be executed on an element in the 'hot' set; Double, 0 <= `pWorking` <= 1
  - `pMiss`: The probability that a `lookup` operation will search for an item not in the table; Double, 0 <= `pMiss` <= 1
  - `seed`: The seed for the PRNG; Integer, 0 < `seed` < 2^64

Proper usage then is as follows:
```
PerformanceTest [useCuckoo] [nOpCount] [nInitialSize] [nMaxSize] [nWorking] [pInsert] [pLookup] [pRemove] [pWorking] [pMiss] [seed]
```  

In order to generate readable results, one should use the scripts included in `/performance`. Here we provide tools which should make running a variety of comparison tests much easier. To run the tests, do the following:
```
mkdir {BUILD_DIR}
cd {BUILD_DIR}
cmake {SRC_DIR}
make

cd performance
./RunBattery.sh
```
The results will then be output to a GitHub-flavored markdown file located at `{BUILD_DIR}/performance/results.md`. For a given result set, we display the test parameters as well as operation latencies at various percentiles for both `std::unordered_map` and `CuckooMap`. A latency column is labeled as `UM Xp` to denoted the Xth percentile for `std::unordered_map` and `CM Yp` to denote the Yth percentile for `CuckooMap`.

In order to change which tests are run, one needs to edit `/performance/battery.csv`. This is a CSV file where each line contains the parameters necessary to execute `PerformanceTest`, omitting `useCuckoo` and `seed` (these will automatically be set by `/performance/RunBattery.sh` as necessary).

When making changes to the code, we suggest running the same battery of tests against the existing version and the updated one and comparing the results (`{SRC_DIR}/performance/results.md` vs. `{BUILD_DIR}/performance/results.md`). When commiting changes, please make sure to check in the new performance results as well.

You can find the performance results for the current version [here](performance/results.md).
