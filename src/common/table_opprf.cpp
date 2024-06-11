

#include "table_opprf.h"
#include <openssl/sha.h>
#include <random>
#include<cstring>

std::uint64_t hashToPosition(std::uint64_t element, osuCrypto::block nonce) {
  SHA_CTX ctx;
  unsigned char hash[SHA_DIGEST_LENGTH];

  unsigned char* message=(unsigned char*)malloc(sizeof(uint64_t)+sizeof(osuCrypto::block));
  memcpy(message, &element,sizeof(uint64_t));
  memcpy(message+sizeof(uint64_t), &nonce, sizeof(osuCrypto::block));

  SHA1_Init(&ctx);
  SHA1_Update(&ctx, message, sizeof(uint64_t)+sizeof(osuCrypto::block));
  SHA1_Final(hash, &ctx);

  uint64_t result = 0;
  std::copy(hash, hash + sizeof(result), reinterpret_cast<unsigned char*>(&result));

  return result;
}
