#pragma once

// \author Avishay Yanay
// \organization Bar-Ilan University
// \email ay.yanay@gmail.com
//
// MIT License
//
// Copyright (c) 2018 AvishayYanay
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <x86intrin.h>

#include "NTL/ZZ.h"
#include "NTL/ZZ_p.h"
#include "gmp.h"
typedef unsigned char byte;

#include <sstream>
#include <string>
#include <vector>

using namespace NTL;

class ZpMersenneIntElement1 {
  // private:
 public:  // TODO return to private after tesing
  static const unsigned int p = 2147483647;
  unsigned int elem;

 public:
  ZpMersenneIntElement1() { elem = 0; };
  ZpMersenneIntElement1(int elem) {
    this->elem = elem;
    if (this->elem < p) {
      return;
    }
    this->elem -= p;
    if (this->elem < p) {
      return;
    }
    this->elem -= p;
  }

  ZpMersenneIntElement1& operator=(const ZpMersenneIntElement1& other) {
    elem = other.elem;
    return *this;
  };
  bool operator!=(const ZpMersenneIntElement1& other) {
    return !(other.elem == elem);
  };

  ZpMersenneIntElement1 operator+(const ZpMersenneIntElement1& f2) {
    ZpMersenneIntElement1 answer;

    answer.elem = (elem + f2.elem);

    if (answer.elem >= p) answer.elem -= p;

    return answer;
  }
  ZpMersenneIntElement1 operator-(const ZpMersenneIntElement1& f2) {
    ZpMersenneIntElement1 answer;

    int temp = (int)elem - (int)f2.elem;

    if (temp < 0) {
      answer.elem = temp + p;
    } else {
      answer.elem = temp;
    }

    return answer;
  }
  ZpMersenneIntElement1 operator/(const ZpMersenneIntElement1& f2) {
    // code taken from NTL for the function XGCD
    int a = f2.elem;
    int b = p;
    long s;

    int u, v, q, r;
    long u0, v0, u1, v1, u2, v2;

    int aneg = 0;

    if (a < 0) {
      if (a < -NTL_MAX_LONG) Error("XGCD: integer overflow");
      a = -a;
      aneg = 1;
    }

    if (b < 0) {
      if (b < -NTL_MAX_LONG) Error("XGCD: integer overflow");
      b = -b;
    }

    u1 = 1;
    v1 = 0;
    u2 = 0;
    v2 = 1;
    u = a;
    v = b;

    while (v != 0) {
      q = u / v;
      r = u % v;
      u = v;
      v = r;
      u0 = u2;
      v0 = v2;
      u2 = u1 - q * u2;
      v2 = v1 - q * v2;
      u1 = u0;
      v1 = v0;
    }

    if (aneg) u1 = -u1;

    s = u1;

    if (s < 0) s = s + p;

    ZpMersenneIntElement1 inverse(s);

    return inverse * (*this);
  }

  ZpMersenneIntElement1 operator*(const ZpMersenneIntElement1& f2) {
    ZpMersenneIntElement1 answer;

    long multLong = (long)elem * (long)f2.elem;

    // get the bottom 31 bit
    unsigned int bottom = multLong & p;

    // get the top 31 bits
    unsigned int top = (multLong >> 31);

    answer.elem = bottom + top;

    // maximim the value of 2p-2
    if (answer.elem >= p) answer.elem -= p;

    // return ZpMersenneIntElement((bottom + top) %p);
    return answer;
  }

  ZpMersenneIntElement1& operator+=(const ZpMersenneIntElement1& f2) {
    elem = (f2.elem + elem) % p;
    return *this;
  };
  ZpMersenneIntElement1& operator*=(const ZpMersenneIntElement1& f2) {
    long multLong = (long)elem * (long)f2.elem;

    // get the bottom 31 bit
    unsigned int bottom = multLong & p;

    // get the top 31 bits
    unsigned int top = (multLong >> 31);

    elem = bottom + top;

    // maximim the value of 2p-2
    if (elem >= p) elem -= p;

    return *this;
  }
};

inline std::ostream& operator<<(std::ostream& s,
                                const ZpMersenneIntElement1& a) {
  return s << a.elem;
};

class ZpMersenneLongElement1 {
  // private:
 public:  // TODO return to private after tesing
  static const unsigned long long p = 2305843009213693951;
  unsigned long long elem;

  ZpMersenneLongElement1() { elem = 0; };
  ZpMersenneLongElement1(unsigned long elem) {
    this->elem = elem;
    if (this->elem >= p) {
      this->elem = (this->elem & p) + (this->elem >> 61);

      if (this->elem >= p) this->elem -= p;
    }
  }

  inline ZpMersenneLongElement1& operator=(const ZpMersenneLongElement1& other)

  {
    elem = other.elem;
    return *this;
  };
  inline bool operator!=(const ZpMersenneLongElement1& other)

  {
    return !(other.elem == elem);
  };

  ZpMersenneLongElement1 operator+(const ZpMersenneLongElement1& f2) {
    ZpMersenneLongElement1 answer;

    answer.elem = (elem + f2.elem);

    if (answer.elem >= p) answer.elem -= p;

    return answer;
  }

  ZpMersenneLongElement1 operator-(const ZpMersenneLongElement1& f2) {
    ZpMersenneLongElement1 answer;

    int64_t temp = elem - f2.elem;

    if (temp < 0) {
      answer.elem = temp + p;
    } else {
      answer.elem = temp;
    }

    return answer;
  }

  ZpMersenneLongElement1 operator/(const ZpMersenneLongElement1& f2) {
    ZpMersenneLongElement1 answer;
    mpz_t d;
    mpz_t result;
    mpz_t mpz_elem;
    mpz_t mpz_me;
    mpz_init_set_str(d, "2305843009213693951", 10);
    mpz_init(mpz_elem);
    mpz_init(mpz_me);

    mpz_set_ui(mpz_elem, f2.elem);
    mpz_set_ui(mpz_me, elem);

    mpz_init(result);

    mpz_invert(result, mpz_elem, d);

    mpz_mul(result, result, mpz_me);
    mpz_mod(result, result, d);

    answer.elem = mpz_get_ui(result);

    return answer;
  }

  ZpMersenneLongElement1 operator*(const ZpMersenneLongElement1& f2) {
    ZpMersenneLongElement1 answer;

    unsigned long long high;
    unsigned long long low = _mulx_u64(elem, f2.elem, &high);

    unsigned long long low61 = (low & p);
    unsigned long long low61to64 = (low >> 61);
    unsigned long long highShift3 = (high << 3);

    unsigned long long res = low61 + low61to64 + highShift3;

    if (res >= p) res -= p;

    answer.elem = res;

    return answer;
  }

  ZpMersenneLongElement1& operator+=(const ZpMersenneLongElement1& f2) {
    elem = (elem + f2.elem);

    if (elem >= p) elem -= p;

    return *this;
  }

  ZpMersenneLongElement1& operator*=(const ZpMersenneLongElement1& f2) {
    unsigned long long high;
    unsigned long long low = _mulx_u64(elem, f2.elem, &high);

    unsigned long long low61 = (low & p);
    unsigned long long low61to64 = (low >> 61);
    unsigned long long highShift3 = (high << 3);

    unsigned long long res = low61 + low61to64 + highShift3;

    if (res >= p) res -= p;

    elem = res;

    return *this;
  }
};

inline std::ostream& operator<<(std::ostream& s,
                                const ZpMersenneLongElement1& a) {
  return s << a.elem;
};

template <class FieldType>
class TemplateField {
 private:
  long fieldParam;
  int elementSizeInBytes;
  int elementSizeInBits;
  FieldType* m_ZERO;
  FieldType* m_ONE;

 public:
  /**
   * the function create a field by:
   * generate the irreducible polynomial x^8 + x^4 + x^3 + x + 1 to work with
   * init the field with the newly generated polynomial
   */
  TemplateField(long fieldParam);

  /**
   * return the field
   */

  std::string elementToString(const FieldType& element);
  FieldType stringToElement(const std::string& str);

  void elementToBytes(unsigned char* output, FieldType& element);

  FieldType bytesToElement(unsigned char* elemenetInBytes);
  void elementVectorToByteVector(std::vector<FieldType>& elementVector,
                                 std::vector<byte>& byteVector);

  FieldType* GetZero();
  FieldType* GetOne();

  int getElementSizeInBytes() { return elementSizeInBytes; }
  int getElementSizeInBits() { return elementSizeInBits; }
  /*
   * The i-th field element. The ordering is arbitrary, *except* that
   * the 0-th field element must be the neutral w.r.t. addition, and the
   * 1-st field element must be the neutral w.r.t. multiplication.
   */
  FieldType GetElement(long b);
  FieldType Random();
  ~TemplateField();
};

template <class FieldType>
std::string TemplateField<FieldType>::elementToString(
    const FieldType& element) {
  std::ostringstream stream;
  stream << element;
  std::string str = stream.str();
  return str;
}

template <class FieldType>
FieldType TemplateField<FieldType>::stringToElement(const std::string& str) {
  FieldType element;

  std::istringstream iss(str);
  iss >> element;

  return element;
}

template <class FieldType>
FieldType* TemplateField<FieldType>::GetZero() {
  return m_ZERO;
}

template <class FieldType>
FieldType* TemplateField<FieldType>::GetOne() {
  return m_ONE;
}

template <class FieldType>
TemplateField<FieldType>::~TemplateField() {
  delete m_ZERO;
  delete m_ONE;
}
