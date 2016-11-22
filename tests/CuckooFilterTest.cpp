#include <cassert>
#include <iostream>

#include <cuckoomap/CuckooFilter.h>

struct Key {
  int k;
  Key() : k(0) {}
  Key(int i) : k(i) {}
  bool empty() { return k == 0; }
};

namespace std {

template <>
struct equal_to<Key> {
  bool operator()(Key const& a, Key const& b) { return a.k == b.k; }
};
}

int main(int argc, char* argv[]) {
  CuckooFilter<Key> m(false, 100);
  auto insert = [&]() -> void {
    for (int i = 0; i < 100; ++i) {
      Key k(i);
      m.insert(k);
      std::cout << "Inserted key " << i << std::endl;
    }
  };
  auto show = [&]() {
    for (int i = 99; i >= 0; --i) {
      Key k(i);
      if (m.lookup(k)) {
        std::cout << "Found key " << i << std::endl;
      } else {
        std::cout << "Did not find key " << i << std::endl;
        assert(false);
      }
    }
  };
  auto remove = [&]() -> void {
    for (int i = 0; i < 50; ++i) {
      Key k(i);
      if (m.remove(k)) {
        std::cout << "Removed key " << i << std::endl;
      } else {
        std::cout << "Did not find key " << i << std::endl;
        assert(false);
      }
    }
  };
  auto notShow = [&]() {
    for (int i = 0; i < 50; ++i) {
      Key k(i);
      if (m.lookup(k)) {
        std::cout << "Found removed key " << i << std::endl;
        assert(false);
      } else {
        std::cout << "Did not find key " << i << std::endl;
      }
    }
  };
  std::cout << "map was made" << std::endl;
  insert();
  show();
  remove();
  notShow();
}
