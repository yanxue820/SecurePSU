#pragma once

namespace ENCRYPTO {

struct PsiAnalyticsContext {
  uint16_t port;
  uint32_t role;
  uint64_t bitlen;
  uint64_t neles;
  uint64_t nbins;
  uint64_t nfuns;  // number of hash functions in the hash table
  uint64_t radix;
  double epsilon;
  uint64_t ffuns;
  uint64_t fbins;
  double fepsilon;
  std::string address;

  std::vector<uint64_t> sci_io_start;

  uint64_t sentBytesOPRF;
  uint64_t recvBytesOPRF;
  uint64_t sentBytesHint;
  uint64_t recvBytesHint;
  uint64_t sentBytesSCI;
  uint64_t recvBytesSCI;

  uint64_t sentBytes;
  uint64_t recvBytes;

  enum {
    PSM1,
    PSM2
  } psm_type;

  struct {
    double hashing;
    double base_ots_sci;
    double base_ots_libote=0;
    double osn_setup;
    double triples;
    double oprf;
    double hint_transmission;
    double hint_computation;
    double psm_time;
    double total;
  } timings;
};

}
