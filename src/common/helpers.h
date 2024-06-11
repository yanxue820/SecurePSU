#pragma once


#include <vector>
#include <cinttypes>

namespace ENCRYPTO {

std::vector<uint64_t> GeneratePseudoRandomElements(const std::size_t n, const std::size_t bitlen,
                                                   const std::size_t seed = 12345);

std::vector<uint64_t> GenerateSequentialElements(const std::size_t n);

}
