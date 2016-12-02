#include <cassert>
#include <iostream>

#include <cuckoomap/InternalCuckooMap.h>

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

int main(int /*argc*/, char* /*argv*/[]) {
  InternalCuckooMap<Key, Value> m(false, 1000);
  auto insert = [&]() -> void {
    for (int i = 1; i < 100; ++i) {
      Key k(i);
      Value v(i * i);
      int res = 1;
      while (res > 0) {
        res = m.insert(k, &v, nullptr, nullptr);
      }
      if (res == 0) {
        std::cout << "Inserted pair ";
      } else {
        std::cout << "Could not insert pair ";
        assert(false);
      }
      std::cout << "(" << i << ", " << i * i << ")" << std::endl;
    }
  };
  auto show = [&]() {
    for (int i = 99; i >= 1; --i) {
      Key k(i);
      Key* kFound;
      Value* vFound;
      if (m.lookup(k, kFound, vFound)) {
        std::cout << "Found key " << i << " with value " << vFound->v
                  << std::endl;
        assert(vFound->v == i * i);
        assert(kFound->k == i);
      } else {
        std::cout << "Did not find key " << i << std::endl;
      }
    }
  };
  auto remove = [&]() -> void {
    for (int i = 1; i < 50; ++i) {
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
