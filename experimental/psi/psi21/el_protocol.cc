// Copyright 2024 The secretflow authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <algorithm>
#include <cmath>
#include <memory>

// #include <libscapi/include/primitives/Matrix.hpp>

#include <libscapi/include/primitives/Matrix.hpp>

#include "experimental/psi/psi21/Mersenne.h"
#include "experimental/psi/psi21/el_protocol.h"
#include "experimental/psi/psi21/el_sender.h"
#include "yacl/crypto/rand/rand.h"
#include "yacl/crypto/tools/ro.h"
#include "yacl/kernel/algorithms/base_ot.h"
#include "yacl/kernel/algorithms/iknp_ote.h"
#include "yacl/kernel/algorithms/kkrt_ote.h"
#include "yacl/link/link.h"
#include "yacl/utils/serialize.h"

namespace psi::psi {

namespace yc = yacl::crypto;

namespace {

// PSI-related constants
// Ref https://eprint.iacr.org/2017/799.pdf (Table 2)
constexpr float ZETA[]{1.12f, 0.17f};
// constexpr size_t BETA[]{31, 63};
constexpr size_t TABLE_SIZE[]{32, 64};  // next power of BETAs

// OTe-related constants
constexpr size_t NUM_BASE_OT{128};
constexpr size_t NUM_INKP_OT{512};
constexpr size_t BATCH_SIZE{896};

static auto ro = yc::RandomOracle::GetDefault();

}  // namespace

// template <class FieldType>
//  vector<shared_ptr<ProtocolPartyData>>  parties;
//	std::vector<FieldType> a_vals;

// Ref https://eprint.iacr.org/2017/799.pdf (Figure 6, 7)
#define flag_print false

template <class FieldType>
class ProtocolParty {
  uint64_t num_bins;
  int N, M, T, m_partyId;
  TemplateField<FieldType>* field;
  std::vector<ProtocolParty*> parties;
  std::vector<FieldType> randomTAnd2TShares;
  VDMTranspose<FieldType> matrix_vand_transpose;
  VDM<FieldType> matrix_vand;
  HIM<FieldType> matrix_for_interpolate;

 public:
  std::vector<uint128_t> items_;
  size_t count_;
  std::vector<std::vector<uint64_t>> shares_;
  std::vector<std::shared_ptr<yacl::link::Context>> p2p_;
  size_t me;
  size_t wsize;

 public:
  void roundFunctionSync(std::vector<std::vector<byte>>& sendBufs,
                         std::vector<std::vector<byte>>& recBufs, int round);
  void roundFunctionSyncForP1(std::vector<byte>& myShare,
                              std::vector<std::vector<byte>>& recBufs);
  void generateRandomShares(int numOfRandoms,
                            std::vector<FieldType>& randomElementsToFill);
  void modDoubleRandom(uint64_t no_random,
                       std::vector<FieldType>& randomElementsToFill);
  void generateRandom2TAndTShares(int numOfRandomPairs,
                                  std::vector<FieldType>& randomElementsToFill);
  void addShareOpen(uint64_t num_vals, std::vector<FieldType>& shares,
                    std::vector<FieldType>& secrets);
  void reshare(std::vector<FieldType>& vals, std::vector<FieldType>& shares);
  void additiveToThreshold();
  void sendFromP1(std::vector<byte>& sendBuf);
  // void DNHonestMultiplication(FieldType *a, FieldType *b,
  // std::vector<FieldType> &cToFill, int numOfTrupples);
  FieldType interpolate(std::vector<FieldType> x);
  void DNHonestMultiplication(std::vector<FieldType> a,
                              std::vector<FieldType> b,
                              std::vector<FieldType>& cToFill,
                              uint64_t numOfTrupples, int offset);
  void mult_sj();
  uint64_t evaluateCircuit();
  void protocolIni(int M_, int N_, size_t me_id);
  // void randomParse();
};

template <class FieldType>
void ProtocolParty<FieldType>::roundFunctionSync(
    std::vector<std::vector<byte>>& sendBufs,
    std::vector<std::vector<byte>>& recBufs, int round) {
  // cout<<"in roundFunctionSync "<< round<< endl;

  int numThreads = parties.size();
  int numPartiesForEachThread;

  if (parties.size() <= numThreads) {
    numThreads = parties.size();
    numPartiesForEachThread = 1;
  } else {
    numPartiesForEachThread = (parties.size() + numThreads - 1) / numThreads;
  }

  recBufs[m_partyId] = move(sendBufs[m_partyId]);
  count_ = count_ + 1;
  // auto [ctx, wsize, me, leader] = CollectContext();
  // std::vector<uint128_t> recv_shares(count);

  for (size_t id{}; id != me; ++id) {
    ElSend(p2p_[id], items_, shares_[id]);
    ElRecv(p2p_[id], items_);
    return;
  }

  for (size_t id{me + 1}; id != wsize; ++id) {
    ElRecv(p2p_[id], items_);
    ElSend(p2p_[id], items_, shares_[id]);
    return;
  }
}
template <class FieldType>
void ProtocolParty<FieldType>::roundFunctionSyncForP1(
    std::vector<byte>& myShare, std::vector<std::vector<byte>>& recBufs) {
  // cout<<"in roundFunctionSyncBroadcast "<< endl;

  int numThreads = parties.size();
  // int numThreads = 10;
  int numPartiesForEachThread;

  if (parties.size() <= numThreads) {
    numThreads = parties.size();
    numPartiesForEachThread = 1;
  } else {
    numPartiesForEachThread = (parties.size() + numThreads - 1) / numThreads;
  }

  recBufs[m_partyId] = myShare;
  count_ = count_ + 1;
  for (size_t id{}; id != me; ++id) {
    ElSend(p2p_[id], items_, shares_[id]);
    ElRecv(p2p_[id], items_);
  }
}

template <class FieldType>
void ProtocolParty<FieldType>::generateRandomShares(
    int numOfRandoms, std::vector<FieldType>& randomElementsToFill) {
  numOfRandoms = 100;
  randomElementsToFill.resize(numOfRandoms);
  int index = 0;
  std::vector<std::vector<byte>> recBufsBytes(N);
  // std::vector<std::vector<unsigned char>> recBufsBytes(N);
  int robin = 0;
  int no_random = numOfRandoms;

  std::vector<FieldType> x1(N), y1(N), x2(N), y2(N), t1(N), r1(N), t2(N), r2(N);
  ;

  std::vector<std::vector<FieldType>> sendBufsElements(N);
  std::vector<std::vector<byte>> sendBufsBytes(N);
  // std:: vector<std::vector<unsigned char>> sendBufsBytes(N);

  // the number of buckets (each bucket requires one double-sharing
  // from each party and gives N-2T random double-sharings)
  int no_buckets = (no_random / (N - T)) + 1;

  // sharingBufTElements.resize(no_buckets*(N-2*T)); // my shares of the
  // double-sharings sharingBuf2TElements.resize(no_buckets*(N-2*T)); // my
  // shares of the double-sharings

  // maybe add some elements if a partial bucket is needed
  randomElementsToFill.resize(no_buckets * (N - T));

  for (int i = 0; i < N; i++) {
    sendBufsElements[i].resize(no_buckets);
    sendBufsBytes[i].resize(no_buckets * field->getElementSizeInBytes());
    recBufsBytes[i].resize(no_buckets * field->getElementSizeInBytes());
  }

  /**
   *  generate random sharings.
   *  first degree t.
   *
   */
  for (int k = 0; k < no_buckets; k++) {
    // generate random degree-T polynomial
    for (int i = 0; i < T + 1; i++) {
      // A random field element, uniform distribution, note that x1[0] is the
      // secret which is also random
      x1[i] = field->Random();
    }
    matrix_vand.MatrixMult(x1, y1, T + 1);  // eval poly at alpha-positions

    // prepare shares to be sent
    for (int i = 0; i < N; i++) {
      // cout << "y1[ " <<i<< "]" <<y1[i] << endl;
      sendBufsElements[i][k] = y1[i];
    }
  }

  if (flag_print) {
    for (int i = 0; i < N; i++) {
      for (int k = 0; k < sendBufsElements[0].size(); k++) {
        // cout << "before roundfunction4 send to " <<i <<" element: "<< k << "
        // " << sendBufsElements[i][k] << endl;
      }
    }
    std::cout << "sendBufs" << std::endl;
    std::cout << "N" << N << std::endl;
    std::cout << "T" << T << std::endl;
  }

  int fieldByteSize = field->getElementSizeInBytes();
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < sendBufsElements[i].size(); j++) {
      field->elementToBytes((sendBufsBytes[i].data() + (j * fieldByteSize)),
                            sendBufsElements[i][j]);
    }
  }

  roundFunctionSync(sendBufsBytes, recBufsBytes, 4);

  if (flag_print) {
    for (int i = 0; i < N; i++) {
      for (int k = 0; k < sendBufsBytes[0].size(); k++) {
        std::cout << "roundfunction4 send to " << i << " element: " << k << " "
                  << (int)sendBufsBytes[i][k] << std::endl;
      }
    }
    for (int i = 0; i < N; i++) {
      for (int k = 0; k < recBufsBytes[0].size(); k++) {
        std::cout << "roundfunction4 receive from " << i << " element: " << k
                  << " " << (int)recBufsBytes[i][k] << std::endl;
      }
    }
  }

  for (int k = 0; k < no_buckets; k++) {
    for (int i = 0; i < N; i++) {
      t1[i] =
          field->bytesToElement((recBufsBytes[i].data() + (k * fieldByteSize)));
    }
    matrix_vand_transpose.MatrixMult(t1, r1, N - T);

    // copy the resulting vector to the array of randoms
    for (int i = 0; i < N - T; i++) {
      randomElementsToFill[index] = r1[i];
      index++;
    }
  }
}

/*
 * prepare additive and T-threshold sharings of secret random value r_j using
 * DN07's protocol
 */
#define mpsi_print false
template <class FieldType>
void ProtocolParty<FieldType>::modDoubleRandom(
    uint64_t no_random, std::vector<FieldType>& randomElementsToFill) {
  TemplateField<FieldType>* field = new TemplateField<FieldType>(0);
  // int N = 5;//佈°彗¶佀~Y仼 佅¥
  // int T = N/2 -1;//佈°彗¶佀~Y仼 佅¥

  // cout << this->m_partyId <<  ": Generating double sharings..." << endl;
  int index = 0;

  std::vector<FieldType> x1(N), y1(N), y2(N), t1(N), r1(N), t2(N), r2(N);

  std::vector<std::vector<FieldType>> sendBufsElements(N);

  std::vector<std::vector<byte>> sendBufsBytes(N);
  std::vector<std::vector<byte>> recBufsBytes(N);
  // the number of buckets (each bucket requires one double-sharing
  // from each party and gives N-2T random double-sharings)
  uint64_t no_buckets = (no_random / (N - T)) + 1;

  // int fieldByteSize = this->field->getElementSizeInBytes();
  int fieldByteSize = field->getElementSizeInBytes();

  // maybe add some elements if a partial bucket is needed
  randomElementsToFill.resize(no_buckets * (N - T) * 2);

  for (int i = 0; i < N; i++) {
    sendBufsElements[i].resize(no_buckets * 2);
    sendBufsBytes[i].resize(no_buckets * fieldByteSize * 2);
    recBufsBytes[i].resize(no_buckets * fieldByteSize * 2);
  }

  // cout << this->m_partyId << ": no_random: " << no_random << " no_buckets: "
  // << no_buckets << " N: " << N << " T: " << T << endl;

  /**
   *  generate random sharings.
   *  first degree T, then additive
   *
   */
  for (uint64_t k = 0; k < no_buckets; k++) {
    // generate random degree-T polynomial
    for (int i = 0; i < T + 1; i++) {
      // A random field element, uniform distribution,
      // note that x1[0] is the secret which is also random
      // x1[i] = this->field->Random();
      x1[i] = field->Random();
    }

    matrix_vand.MatrixMult(x1, y1, T + 1);  // eval poly at alpha-positions

    y2[0] = x1[0];
    // generate N-1 random elements
    for (int i = 1; i < N; i++) {
      // A random field element, uniform distribution
      // y2[i] = this->field->Random();
      y2[i] = field->Random();
      // all y2[i] generated so far are additive shares of the secret x1[0]
      y2[0] = y2[0] - y2[i];
    }

    // prepare shares to be sent
    for (int i = 0; i < N; i++) {
      // cout << "y1[ " <<i<< "]" <<y1[i] << " y2[ " << i << "]" << y2[i] <<
      // endl;
      sendBufsElements[i][2 * k] = y1[i];
      sendBufsElements[i][2 * k + 1] = y2[i];
    }
  }

  for (int i = 0; i < N; i++) {
    for (uint64_t j = 0; j < sendBufsElements[i].size(); j++) {
      // this->field->elementToBytes(sendBufsBytes[i].data() + (j *
      // fieldByteSize), sendBufsElements[i][j]);
      field->elementToBytes(sendBufsBytes[i].data() + (j * fieldByteSize),
                            sendBufsElements[i][j]);
    }
  }

  roundFunctionSync(sendBufsBytes, recBufsBytes, 1);

  for (uint64_t k = 0; k < no_buckets; k++) {
    for (int i = 0; i < N; i++) {
      t1[i] = field->bytesToElement(recBufsBytes[i].data() +
                                    (2 * k * fieldByteSize));
      t2[i] = field->bytesToElement(recBufsBytes[i].data() +
                                    ((2 * k + 1) * fieldByteSize));
      // t1[i] = this->field->bytesToElement(recBufsBytes[i].data() + (2*k *
      // fieldByteSize)); t2[i] =
      // this->field->bytesToElement(recBufsBytes[i].data() + ((2*k +1) *
      // fieldByteSize));
    }

    matrix_vand_transpose.MatrixMult(t1, r1, N - T);
    matrix_vand_transpose.MatrixMult(t2, r2, N - T);

    // copy the resulting vector to the array of randoms
    for (int i = 0; i < (N - T); i++) {
      randomElementsToFill[index * 2] = r1[i];
      randomElementsToFill[index * 2 + 1] = r2[i];
      index++;
    }
  }

  if (mpsi_print == true) {
    // std::cout << this->m_partyId << ": First pair of shares is " <<
    // randomElementsToFill[0] << " " << randomElementsToFill[1] << std::endl;
    std::cout << ": First pair of shares is " << randomElementsToFill[0] << " "
              << randomElementsToFill[1] << std::endl;
  }
}

template <class FieldType>
void ProtocolParty<FieldType>::generateRandom2TAndTShares(
    int numOfRandomPairs, std::vector<FieldType>& randomElementsToFill) {
  TemplateField<FieldType>* field = new TemplateField<FieldType>(0);
  // int N = 5;//佈°彗¶佀~Y仼 佅¥
  // int T = N/2 -1;//佈°彗¶佀~Y仼 佅¥
  int index = 0;
  std::vector<std::vector<byte>> recBufsBytes(N);
  int robin = 0;
  int no_random = numOfRandomPairs;

  std::vector<FieldType> x1(N), y1(N), x2(N), y2(N), t1(N), r1(N), t2(N), r2(N);
  ;

  std::vector<std::vector<FieldType>> sendBufsElements(N);
  std::vector<std::vector<byte>> sendBufsBytes(N);

  // the number of buckets (each bucket requires one double-sharing
  // from each party and gives N-2T random double-sharings)
  int no_buckets = (no_random / (N - T)) + 1;

  // sharingBufTElements.resize(no_buckets*(N-2*T)); // my shares of the
  // double-sharings sharingBuf2TElements.resize(no_buckets*(N-2*T)); // my
  // shares of the double-sharings

  // maybe add some elements if a partial bucket is needed
  randomElementsToFill.resize(no_buckets * (N - T) * 2);

  for (int i = 0; i < N; i++) {
    sendBufsElements[i].resize(no_buckets * 2);
    sendBufsBytes[i].resize(no_buckets * field->getElementSizeInBytes() * 2);
    recBufsBytes[i].resize(no_buckets * field->getElementSizeInBytes() * 2);
  }

  /**
   *  generate random sharings.
   *  first degree t.
   *
   */
  for (int k = 0; k < no_buckets; k++) {
    // generate random degree-T polynomial
    for (int i = 0; i < T + 1; i++) {
      // A random field element, uniform distribution, note that x1[0] is the
      // secret which is also random
      x1[i] = field->Random();
    }

    matrix_vand.MatrixMult(x1, y1, T + 1);  // eval poly at alpha-positions

    x2[0] = x1[0];
    // generate random degree-T polynomial
    for (int i = 1; i < 2 * T + 1; i++) {
      // A random field element, uniform distribution, note that x1[0] is the
      // secret which is also random
      x2[i] = field->Random();
    }

    matrix_vand.MatrixMult(x2, y2, 2 * T + 1);

    // prepare shares to be sent
    for (int i = 0; i < N; i++) {
      // cout << "y1[ " <<i<< "]" <<y1[i] << endl;
      sendBufsElements[i][2 * k] = y1[i];
      sendBufsElements[i][2 * k + 1] = y2[i];
    }
  }

  if (flag_print) {
    for (int i = 0; i < N; i++) {
      for (int k = 0; k < sendBufsElements[0].size(); k++) {
        // cout << "before roundfunction4 send to " <<i <<" element: "<< k << "
        // " << sendBufsElements[i][k] << endl;
      }
    }
    std::cout << "sendBufs" << std::endl;
    std::cout << "N" << N << std::endl;
    std::cout << "T" << T << std::endl;
  }

  int fieldByteSize = field->getElementSizeInBytes();
  for (int i = 0; i < N; i++) {
    for (int j = 0; j < sendBufsElements[i].size(); j++) {
      field->elementToBytes(sendBufsBytes[i].data() + (j * fieldByteSize),
                            sendBufsElements[i][j]);
    }
  }

  roundFunctionSync(sendBufsBytes, recBufsBytes, 4);

  if (flag_print) {
    for (int i = 0; i < N; i++) {
      for (int k = 0; k < sendBufsBytes[0].size(); k++) {
        std::cout << "roundfunction4 send to " << i << " element: " << k << " "
                  << (int)sendBufsBytes[i][k] << std::endl;
      }
    }
    for (int i = 0; i < N; i++) {
      for (int k = 0; k < recBufsBytes[0].size(); k++) {
        std::cout << "roundfunction4 receive from " << i << " element: " << k
                  << " " << (int)recBufsBytes[i][k] << std::endl;
      }
    }
  }

  for (int k = 0; k < no_buckets; k++) {
    for (int i = 0; i < N; i++) {
      t1[i] = field->bytesToElement(recBufsBytes[i].data() +
                                    (2 * k * fieldByteSize));
      t2[i] = field->bytesToElement(recBufsBytes[i].data() +
                                    ((2 * k + 1) * fieldByteSize));
    }
    matrix_vand_transpose.MatrixMult(t1, r1, N - T);
    matrix_vand_transpose.MatrixMult(t2, r2, N - T);

    // copy the resulting vector to the array of randoms
    for (int i = 0; i < (N - T); i++) {
      randomElementsToFill[index * 2] = r1[i];
      randomElementsToFill[index * 2 + 1] = r2[i];
      index++;
    }
  }
}

template <class FieldType>
void /*CircuitPSI<FieldType>::*/ ProtocolParty<FieldType>::addShareOpen(
    uint64_t num_vals, std::vector<FieldType>& shares,
    std::vector<FieldType>& secrets) {
  // cout << this->m_partyId << ": Reconstructing additive shares..." << endl;

  // int fieldByteSize = this->field->getElementSizeInBytes();
  int fieldByteSize = 10;
  std::vector<std::vector<byte>> recBufsBytes;
  std::vector<byte> sendBufsBytes;
  std::vector<byte> aPlusRSharesBytes(num_vals * fieldByteSize);
  int i;
  uint64_t j;
  // int N = 10;//this->N;
  // int m_partyId = 0;	   //

  secrets.resize(num_vals);
  for (j = 0; j < num_vals; j++) {
    field->elementToBytes(aPlusRSharesBytes.data() + (j * fieldByteSize),
                          shares[j]);
  }
  if (m_partyId == 0) {
    recBufsBytes.resize(N);

    for (i = 0; i < N; i++) {
      recBufsBytes[i].resize(num_vals * fieldByteSize);
    }

    roundFunctionSyncForP1(aPlusRSharesBytes, recBufsBytes);
  }

  if (/*this-*>*/ m_partyId == 0) {
    for (j = 0; j < num_vals; j++) {
      // secrets[j] = *(this->field->GetZero());
      for (i = 0; i < N; i++) {
        secrets[j] +=
            field->bytesToElement(recBufsBytes[i].data() + (j * fieldByteSize));
      }
    }
  }
}

template <class FieldType>
void ProtocolParty<FieldType>::reshare(std::vector<FieldType>& vals,
                                       std::vector<FieldType>& shares) {
  uint64_t no_vals = vals.size();

  std::vector<FieldType> x1(N), y1(N);
  // std::vector<std::vector<FieldType>> sendBufsElements(N);
  std::vector<std::vector<FieldType>> sendBufsElements(N);
  std::vector<std::vector<byte>> sendBufsBytes(N);
  std::vector<std::vector<byte>> recBufsBytes(N);
  std::vector<std::vector<FieldType>> recBufsElements(N);

  int fieldByteSize = 10;  /// this->field->getElementSizeInBytes();

  // int m_partyId = 0;
  if (/*///this->*/ m_partyId == 0) {
    // generate T-sharings of the values in vals
    for (uint64_t k = 0; k < no_vals; k++) {
      // set x1[0] as the secret to be shared
      x1[0] = vals[k];
      // generate random degree-T polynomial
      for (int i = 1; i < T + 1; i++) {
        // A random field element, uniform distribution
        // x1[i] = this->field->Random();
      }

      /// this->matrix_vand.MatrixMult(x1, y1,T+1); // eval poly at
      /// alpha-positions

      // prepare shares to be sent
      for (int i = 0; i < N; i++) {
        // cout << "y1[ " <<i<< "]" <<y1[i] << endl;
        sendBufsElements[i].push_back(y1[i]);
      }
      shares[k] = y1[0];
    }

    // cout << "Sharings generated \n";

    for (int i = 0; i < N; i++) {
      sendBufsBytes[i].resize(sendBufsElements[i].size() * fieldByteSize);
      recBufsBytes[i].resize(num_bins * fieldByteSize);
      for (uint64_t j = 0; j < sendBufsElements[i].size(); j++) {
        /// this->field->elementToBytes(sendBufsBytes[i].data() + (j *
        /// fieldByteSize), sendBufsElements[i][j]);
      }
      // cout << sendBufsElements[i].size() << " " << sendBufsBytes[i].size() <<
      // " " << recBufsBytes[i].size();
    }
  } else {
    for (int i = 0; i < N; i++) {
      sendBufsBytes[i].resize(num_bins * fieldByteSize);
      recBufsBytes[i].resize(num_bins * fieldByteSize);
      for (uint64_t j = 0; j < num_bins; j++) {
        /// this->field->elementToBytes(sendBufsBytes[i].data(),
        /// *(this->field->GetZero()));
      }
    }
  }

  // cout << "byte conversion done \n";

  //	this->
  roundFunctionSync(sendBufsBytes, recBufsBytes, 2);

  // cout << "roundFunctionSync() done ";

  if (/*///this->*/ m_partyId != 0) {
    for (uint64_t k = 0; k < no_vals; k++) {
      /// shares[k] = this->field->bytesToElement(recBufsBytes[0].data() + (k *
      /// fieldByteSize));
    }
  }
  // cout << "converted back to field elements...\n";

  if (mpsi_print == true) {
    /// cout << this->m_partyId << "First t-sharing received is: " << shares[0]
    /// << endl;
  }
}

template <class FieldType>
void /*MPSI_Party<FieldType>::*/
ProtocolParty<FieldType>::additiveToThreshold() {
  uint64_t j;
  std::vector<FieldType> reconar;  // reconstructed aj+rj
  std::vector<FieldType> add_a;
  uint64_t num_bins = 10;
  std::vector<FieldType> a_vals;
  std::vector<FieldType> randomTAndAddShares;

  reconar.resize(num_bins);
  // add additive share of rj to corresponding share of aj
  for (j = 0; j < num_bins; j++) {
    add_a[j] = add_a[j] + randomTAndAddShares[j * 2 + 1];
  }

  // reconstruct additive shares, store in reconar
  addShareOpen(num_bins, add_a, reconar);

  // reshare and save in a_vals;
  reshare(reconar, a_vals);

  // subtract T-threshold shares of rj
  for (j = 0; j < num_bins; j++) {
    a_vals[j] = a_vals[j] - randomTAndAddShares[j * 2];
  }
}

template <class FieldType>
void ProtocolParty<FieldType>::sendFromP1(std::vector<byte>& sendBuf) {
  // cout<<"in roundFunctionSyncBroadcast "<< endl;

  int numThreads = parties.size();
  int numPartiesForEachThread;

  if (parties.size() <= numThreads) {
    numThreads = parties.size();
    numPartiesForEachThread = 1;
  } else {
    numPartiesForEachThread = (parties.size() + numThreads - 1) / numThreads;
  }

  // recieve the data using threads
  // vector<thread> threads(numThreads);
  for (int t = 0; t < numThreads; t++) {
    /*if ((t + 1) * numPartiesForEachThread <= parties.size()) {
      //  threads[t] = thread(&ProtocolParty::sendDataFromP1, this,
    ref(sendBuf),
      //                      t * numPartiesForEachThread, (t + 1) *
    numPartiesForEachThread); } else {
      //  threads[t] = thread(&ProtocolParty::sendDataFromP1, this,
    ref(sendBuf), t * numPartiesForEachThread, parties.size());
    }*/
  }
  for (int t = 0; t < numThreads; t++) {
    // threads[t].join();
  }

  count_ = count_ + 1;
  // auto [ctx, wsize, me, leader] = CollectContext();
  // std::vector<uint128_t> recv_shares(count);

  for (size_t id{}; id != me; ++id) {
    ElSend(p2p_[id], items_, shares_[id]);
    ElRecv(p2p_[id], items_);
  }
}

// Interpolate polynomial at position Zero
template <class FieldType>
FieldType ProtocolParty<FieldType>::interpolate(std::vector<FieldType> x) {
  std::vector<FieldType> y(N);  // result of interpolate
  matrix_for_interpolate.MatrixMult(x, y);
  return y[0];
}

template <class FieldType>
void ProtocolParty<FieldType>::DNHonestMultiplication(
    std::vector<FieldType> a, std::vector<FieldType> b,
    std::vector<FieldType>& cToFill, uint64_t numOfTrupples, int offset) {
  int index = 0;
  // int numOfMultGates = circuit.getNrOfMultiplicationGates();
  uint64_t numOfMultGates = numOfTrupples;
  int fieldByteSize = field->getElementSizeInBytes();
  std::vector<FieldType> xyMinusRShares(
      numOfTrupples);  // hold both in the same vector to send in one batch
  std::vector<byte> xyMinusRSharesBytes(
      numOfTrupples *
      fieldByteSize);  // hold both in the same vector to send in one batch

  std::vector<FieldType>
      xyMinusR;  // hold both in the same vector to send in one batch
  std::vector<byte> xyMinusRBytes;

  std::vector<std::vector<byte>> recBufsBytes;

  // int offset = numOfMultGates*2;
  // int offset = 0;
  // generate the shares for x+a and y+b. do it in the same array to send once
  for (int k = 0; k < numOfTrupples; k++)  // go over only the logit gates
  {
    xyMinusRShares[k] = a[k] * b[k] - randomTAnd2TShares[offset + 2 * k + 1];
  }
  // set the acctual number of mult gate proccessed in this layer
  xyMinusRSharesBytes.resize(numOfTrupples * fieldByteSize);
  xyMinusR.resize(numOfTrupples);
  xyMinusRBytes.resize(numOfTrupples * fieldByteSize);
  for (int j = 0; j < xyMinusRShares.size(); j++) {
    field->elementToBytes(xyMinusRSharesBytes.data() + (j * fieldByteSize),
                          xyMinusRShares[j]);
  }

  if (m_partyId == 0) {
    // just party 1 needs the recbuf
    recBufsBytes.resize(N);

    for (int i = 0; i < N; i++) {
      recBufsBytes[i].resize(numOfTrupples * fieldByteSize);
    }

    // receive the shares from all the other parties
    roundFunctionSyncForP1(xyMinusRSharesBytes, recBufsBytes);
  }

  // reconstruct the shares recieved from the other parties
  if (m_partyId == 0) {
    std::vector<FieldType> xyMinurAllShares(N), yPlusB(N);

    for (int k = 0; k < numOfTrupples; k++)  // go over only the logit gates
    {
      for (int i = 0; i < N; i++) {
        xyMinurAllShares[i] =
            field->bytesToElement(recBufsBytes[i].data() + (k * fieldByteSize));
      }

      // reconstruct the shares by P0
      xyMinusR[k] = interpolate(xyMinurAllShares);

      // convert to bytes
      field->elementToBytes(xyMinusRBytes.data() + (k * fieldByteSize),
                            xyMinusR[k]);
    }

    // send the reconstructed vector to all the other parties
    sendFromP1(xyMinusRBytes);
  }

  // fill the xPlusAAndYPlusB array for all the parties except for party 1 that
  // already have this array filled
  if (m_partyId != 0) {
    for (int i = 0; i < numOfTrupples; i++) {
      xyMinusR[i] =
          field->bytesToElement(xyMinusRBytes.data() + (i * fieldByteSize));
    }
  }

  index = 0;

  // after the xPlusAAndYPlusB array is filled, we are ready to fill the output
  // of the mult gates
  for (int k = 0; k < numOfTrupples; k++)  // go over only the logit gates
  {
    cToFill[k] = randomTAnd2TShares[offset + 2 * k] + xyMinusR[k];
  }
}

template <class FieldType>
void ProtocolParty<FieldType>::mult_sj() {
  int fieldByteSize = field->getElementSizeInBytes();
  std::vector<FieldType> masks;
  std::vector<FieldType> a_vals;

  ////
  std::vector<FieldType> mult_outs;  // threshold shares of s_j*a_j
  ///
  int num_bins = 64;
  ;
  std::vector<byte> multbytes(num_bins * fieldByteSize);
  std::vector<std::vector<byte>> recBufsBytes;
  int i;
  uint64_t j;

  DNHonestMultiplication(masks, a_vals, mult_outs, num_bins, 0);
  for (j = 0; j < num_bins; j++) {
    // this->
    field->elementToBytes(multbytes.data() + (j * fieldByteSize), mult_outs[j]);
  }

  if (m_partyId == 0) {
    recBufsBytes.resize(N);
    for (i = 0; i < N; i++) {
      recBufsBytes[i].resize(num_bins * fieldByteSize);
    }
    // this->
    roundFunctionSyncForP1(multbytes, recBufsBytes);
  }

  if (m_partyId == 0) {
    std::vector<FieldType> x1(N);
    for (j = 0; j < num_bins; j++) {
      for (i = 0; i < N; i++) {
        x1[i] =
            field->bytesToElement(recBufsBytes[i].data() + (j * fieldByteSize));
      }
      // outputs[j] = this->interpolate(x1);
    }
  }
}

template <class FieldType>
uint64_t ProtocolParty<FieldType>::evaluateCircuit() {
  additiveToThreshold();
  mult_sj();
  return 0;
}

template <class FieldType>
void ProtocolParty<FieldType>::protocolIni(int M_, int N_, size_t me_id) {
  N = N_;
  T = N / 2 - 1;
  m_partyId = me_id;
  M = M_;
  // ini party num
}

void randomParse(std::vector<uint128_t>& items, size_t count,
                 std::vector<std::vector<uint64_t>>& shares,
                 std::vector<std::shared_ptr<yacl::link::Context>> p2p,
                 size_t me_id, size_t wsize, int M, int N) {
  uint64_t num_bins = 64;
  std::vector<ZpMersenneLongElement1> masks;
  std::vector<ZpMersenneLongElement1> randomTAndAddShares;
  std::vector<ZpMersenneLongElement1> randomTAnd2TShares;
  masks.resize(num_bins);

  ProtocolParty<ZpMersenneLongElement1> mpsi;
  mpsi.items_ = items;
  mpsi.count_ = count;
  mpsi.shares_ = shares;
  mpsi.p2p_ = p2p;
  mpsi.protocolIni(M, N, me_id);
  mpsi.me = me_id;
  mpsi.wsize = wsize;
  mpsi.generateRandomShares(num_bins, masks);
  mpsi.modDoubleRandom(num_bins, randomTAndAddShares);
  mpsi.generateRandom2TAndTShares(num_bins, randomTAnd2TShares);

  mpsi.evaluateCircuit();
}

}  // namespace psi::psi
