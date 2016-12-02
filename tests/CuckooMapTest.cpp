#include <cassert>
#include <iostream>

#include <cuckoomap/CuckooMap.h>

struct Key {
  int k;
  Key() : k(0) {}
  Key(int i) : k(i) {}
  bool empty() { return k == 0; }
};

namespace std {

template <>
struct equal_to<Key> {
  bool operator()(Key const& a, Key const& b) const { return a.k == b.k; }
};
}

struct Value {
  int v;
  Value() : v(0) {}
  Value(int i) : v(i) {}
  bool empty() { return v == 0; }
};

int main(int argc, char* argv[]) {
  CuckooMap<Key, Value> m(16);
  auto insert = [&]() -> void {
    for (int i = 1; i < 100; ++i) {
      Key k(i);
      Value v(i * i);
      if (m.insert(k, &v)) {
        std::cout << "Inserted pair ";
      } else {
        std::cout << "Could not insert pair ";
        assert(false);
      }
      std::cout << "(" << i << ", " << i * i << ")" << std::endl;
    }
  };
  auto show = [&]() {
    for (int i = 99; i > 0; --i) {
      Key k(i);
      auto f = m.lookup(k);
      if (f.found()) {
        std::cout << "Found key " << i << " with value " << f.value()->v
                  << std::endl;
        assert(f.value()->v == i * i);
        assert(f.key()->k == i);
      } else {
        std::cout << "Did not find expected key " << i << std::endl;
        assert(false);
      }
    }
  };
  auto remove = [&]() -> void {
    for (int i = 1; i < 50; ++i) {
      Key k(i);
      if (m.remove(k)) {
        std::cout << "Removed key " << i << std::endl;
      } else {
        std::cout << "Did not find key " << i << " to remove" << std::endl;
        assert(false);
      }
    }
  };
  auto showSome = [&]() {
    for (int i = 99; i > 0; --i) {
      Key k(i);
      auto f = m.lookup(k);
      if (f.found()) {
        std::cout << "Found key " << i << " with value " << f.value()->v
                  << std::endl;
        assert(f.value()->v == i * i);
        assert(f.key()->k == i);
      } else {
        std::cout << "Did not find key " << i << std::endl;
        assert(i < 50);
      }
    }
  };
  std::cout << "map was made" << std::endl;
  insert();
  show();
  remove();
  showSome();
}
