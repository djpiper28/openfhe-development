//==================================================================================
// BSD 2-Clause License
//
// Copyright (c) 2014-2022, NJIT, Duality Technologies Inc. and other contributors
//
// All rights reserved.
//
// Author TPOC: contact@openfhe.org
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//==================================================================================

#ifndef _BINFHE_CONSTANTS_H_
#define _BINFHE_CONSTANTS_H_

#include <iosfwd>

namespace lbcrypto {

/**
 * @brief Security levels for predefined parameter sets
 */
enum BINFHEPARAMSET {
    TOY,             // no security
    MEDIUM,          // 108 bits of security for classical and 100 bits for quantum
    STD128_AP,       // Optimized for AP (has higher failure probability for GINX) -
                     // more than 128 bits of security for classical
                     // computer attacks - uses the same setup as HE standard
    STD128_APOPT,    // Optimized for AP (has higher failure probability for GINX) -
                     // more than 128 bits of security for classical computer attacks -
                     // optimize runtime by finding a non-power-of-two n
    STD128,          // more than 128 bits of security for classical
                     // computer attacks - uses the same setup as HE standard
    STD128_OPT,      // more than 128 bits of security for classical computer attacks -
                     // optimize runtime by finding a non-power-of-two n
    STD192,          // more than 192 bits of security for classical computer attacks -
                     // uses the same setup as HE standard
    STD192_OPT,      // more than 192 bits of security for classical computer attacks -
                     // optimize runtime by finding a non-power-of-two n
    STD256,          // more than 256 bits of security for classical computer attacks -
                     // uses the same setup as HE standard
    STD256_OPT,      // more than 256 bits of security for classical computer attacks -
                     // optimize runtime by finding a non-power-of-two n
    STD128Q,         // more than 128 bits of security for quantum attacks - uses the
                     // same setup as HE standard
    STD128Q_OPT,     // more than 128 bits of security for quantum attacks -
                     // optimize runtime by finding a non-power-of-two n
    STD192Q,         // more than 192 bits of security for quantum attacks - uses the
                     // same setup as HE standard
    STD192Q_OPT,     // more than 192 bits of security for quantum attacks -
                     // optimize runtime by finding a non-power-of-two n
    STD256Q,         // more than 256 bits of security for quantum attacks - uses the
                     // same setup as HE standard
    STD256Q_OPT,     // more than 256 bits of security for quantum attacks -
                     // optimize runtime by finding a non-power-of-two n
    SIGNED_MOD_TEST  // special parameter set for confirming the signed modular
                     // reduction in the accumulator updates works correctly
};
std::ostream& operator<<(std::ostream& s, BINFHEPARAMSET f);

/**
 * @brief Type of ciphertext generated by the Encrypt method
 */
enum BINFHEOUTPUT {
    FRESH,        // a fresh encryption
    BOOTSTRAPPED  // a freshly encrypted ciphertext is bootstrapped
};
std::ostream& operator<<(std::ostream& s, BINFHEOUTPUT f);

enum BINFHEMETHOD {
    AP,   // Ducas-Micciancio variant
    GINX  // Chillotti-Gama-Georgieva-Izabachene variant
};
std::ostream& operator<<(std::ostream& s, BINFHEMETHOD f);

enum BINGATE { OR, AND, NOR, NAND, XOR_FAST, XNOR_FAST, XOR, XNOR };
std::ostream& operator<<(std::ostream& s, BINGATE f);

}  // namespace lbcrypto

#endif  // _BINFHE_CONSTANTS_H_
