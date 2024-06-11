
#include "helpers.h"

#include <algorithm>
#include <cassert>
#include <random>
#include <unordered_set>

#include "HashingTables/common/hashing.h"
#include "constants.h"

namespace ENCRYPTO {

std::vector<uint64_t> GeneratePseudoRandomElements(const std::size_t n, const std::size_t bitlen,
                                                   const std::size_t seed) {
  std::vector<uint64_t> elements;
  elements.reserve(n);

  std::mt19937 engine(seed);

  bool not_finished = true;
  while (not_finished) {
    std::uniform_int_distribution<std::uint64_t> dist(0, (1ull << bitlen) - 1);

    const auto my_rand = [&engine, &dist]() { return dist(engine); };
    while (elements.size() != n) {
      elements.push_back(my_rand());
    }
    // check that the elements are unique
    // if there are duplicated, remove them and add some more random elements, then recheck
    std::unordered_set<uint64_t> s;
    for (auto e : elements) {
      s.insert(e);
    }
    elements.assign(s.begin(), s.end());

    if (elements.size() == n) {
      not_finished = false;
    }
  }

  std::sort(elements.begin(), elements.end());
  for (auto i = 1ull; i < elements.size(); ++i) {
    assert(elements.at(i - 1) != elements.at(i));
  }

  for (auto &e : elements) {
    e = HashingTable::ElementToHash(e) & __61_bit_mask;
  }

  return elements;
}

std::vector<uint64_t> GenerateSequentialElements(const std::size_t n) {
  std::vector<uint64_t> elements(n);
  std::size_t i = 0;
  std::generate(elements.begin(), elements.end(), [&i]() mutable { return i++; });

  for (auto &e : elements) {
    e = HashingTable::ElementToHash(e) & __61_bit_mask;
  }

  return elements;
}

}
