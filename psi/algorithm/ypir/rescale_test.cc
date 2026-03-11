#include "gtest/gtest.h"

#include "psi/algorithm/spiral/arith/arith.h"
#include "psi/algorithm/spiral/params.h"

namespace psi::ypir {

using namespace psi::spiral;

TEST(RescaleTest, SimplePIRRescale) {
  // SimplePIR parameters
  uint64_t q = 66974689739603969ULL;  // modulus
  uint64_t pt_modulus = 256;
  uint64_t scale_k = q / pt_modulus;  // 261619881795328

  // Test: encode db value 100, then decode
  uint8_t db_value = 100;

  // When query encrypts scale_k at position i, and we compute dot product
  // with db, we get approximately: scale_k * db_value (plus noise)
  uint64_t encoded = scale_k * db_value;

  // After decryption, rescale should recover the original value
  uint64_t decoded = arith::Rescale(encoded, q, pt_modulus);

  EXPECT_EQ(decoded, db_value)
      << "Rescale failed: expected " << (int)db_value << ", got " << decoded;

  // Test multiple values
  for (uint8_t val : {0, 1, 100, 101, 200, 250, 255}) {
    uint64_t enc = scale_k * val;
    uint64_t dec = arith::Rescale(enc, q, pt_modulus);
    EXPECT_EQ(dec, val) << "Failed for value " << (int)val;
  }
}

TEST(RescaleTest, WithNoise) {
  uint64_t q = 66974689739603969ULL;
  uint64_t pt_modulus = 256;
  uint64_t scale_k = q / pt_modulus;

  uint8_t db_value = 100;
  uint64_t encoded = scale_k * db_value;

  // Add small noise (simulating LWE noise)
  // Noise should be << q / (2 * pt_modulus) to decode correctly
  for (int64_t noise = -1000; noise <= 1000; noise += 100) {
    uint64_t noisy =
        static_cast<uint64_t>(static_cast<int64_t>(encoded) + noise);
    uint64_t decoded = arith::Rescale(noisy, q, pt_modulus);

    EXPECT_EQ(decoded, db_value)
        << "Failed with noise=" << noise << ", encoded=" << encoded
        << ", noisy=" << noisy << ", decoded=" << decoded;
  }
}

}  // namespace psi::ypir
