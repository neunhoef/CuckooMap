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

