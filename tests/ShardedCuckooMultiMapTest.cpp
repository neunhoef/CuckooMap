#include <iostream>
#include <cassert>

#include <cuckoomap/CuckooMultiMap.h>
#include <cuckoomap/ShardedMap.h>

struct Key {
  int k;
  Key() : k(0) {
  }
  Key(int i) : k(i) {
  }
};

namespace std {

template<> struct equal_to<Key> {
  bool operator()(Key const& a, Key const& b) { return a.k == b.k; }
};

}

struct Value {
  int v;
  Value() : v(0) {
  }
  Value(int i) : v(i) {
  }
};

int main(int argc, char* argv[]) {
  ShardedMap<CuckooMultiMap<Key, Value>> m(16, 8);
  auto insert = [&]() -> void {
    for (int x = 0; x < 10; ++x) {
      Key k(x);
      for (int y = 0; y < 10; ++y) {
        Value v(x + y * 10);
        if (m.insert(k, &v)) {
          std::cout << "Inserted pair ";
        } else {
          std::cout << "Could not insert pair ";
          assert(false);
        }
        std::cout << "(" << x << ", " << v.v << ")" << std::endl;
      }
    }
  };
  auto show = [&]() {
    for (int x = 9; x >= 0; --x) {
      Key k(x);
      auto f = m.lookup(k);
      if (f.found()) {
        do {
          std::cout << "Found key " << x << " with value " << f.value()->v
              << std::endl;
          assert(f.value()->v % 10 == x);
          assert(f.key()->k == x);
        } while (f.next());
      } else {
        std::cout << "Did not find key " << x << std::endl;
      }
    }
  };
  auto remove = [&]() -> void {
    for (int i = 0; i < 5; ++i) {
      Key k(i);
      if (m.remove(k)) {
        std::cout << "Removed key " << i << std::endl;
      } else {
        std::cout << "Did not find key " << i << std::endl;
        assert(false);
      }
    }
  };
  std::cout << "map was made" << std::endl;
  insert();
  show();
  remove();
  show();
}
