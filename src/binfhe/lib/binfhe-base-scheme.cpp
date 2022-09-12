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

#include "binfhe-base-scheme.h"

#include <string>

namespace lbcrypto {

// wrapper for KeyGen methods
RingGSWBTKey BinFHEScheme::KeyGen(const std::shared_ptr<BinFHECryptoParams> params, ConstLWEPrivateKey LWEsk) const {
    auto& LWEParams        = params->GetLWEParams();
    ConstLWEPrivateKey skN = LWEscheme->KeyGen(LWEParams->GetN(), LWEParams->GetQ());

    RingGSWBTKey ek;
    ek.KSkey = LWEscheme->KeySwitchGen(LWEParams, LWEsk, skN);

    auto& RGSWParams   = params->GetRingGSWParams();
    auto polyParams    = RGSWParams->GetPolyParams();
    NativePoly skNPoly = NativePoly(polyParams);
    skNPoly.SetValues(skN->GetElement(), Format::COEFFICIENT);
    skNPoly.SetFormat(Format::EVALUATION);

    ek.BSkey = ACCscheme->KeyGenAcc(RGSWParams, skNPoly, LWEsk);

    return ek;
}

// Full evaluation as described in https://eprint.iacr.org/2020/08
LWECiphertext BinFHEScheme::EvalBinGate(const std::shared_ptr<BinFHECryptoParams> params, const BINGATE gate,
                                        const RingGSWBTKey& EK, ConstLWECiphertext ct1, ConstLWECiphertext ct2) const {
    if (ct1 == ct2) {
        std::string errMsg = "ERROR: Please only use independent ciphertexts as inputs.";
        OPENFHE_THROW(config_error, errMsg);
    }

    // By default, we compute XOR/XNOR using a combination of AND, OR, and NOT
    // gates
    if ((gate == XOR) || (gate == XNOR)) {
        auto ct1NOT = EvalNOT(params, ct1);
        auto ct2NOT = EvalNOT(params, ct2);
        auto ctAND1 = EvalBinGate(params, AND, EK, ct1, ct2NOT);
        auto ctAND2 = EvalBinGate(params, AND, EK, ct1NOT, ct2);
        auto ctOR   = EvalBinGate(params, OR, EK, ctAND1, ctAND2);
        // NOT is free so there is not cost to do it an extra time for XNOR
        if (gate == XOR)
            return ctOR;
        else  // XNOR
            return EvalNOT(params, ctOR);
    }
    else {
        LWECiphertext ctprep = std::make_shared<LWECiphertextImpl>(*ct1);

        // the additive homomorphic operation for XOR/NXOR is different from the
        // other gates we compute 2*(ct1 - ct2) mod 4 for XOR, me map 1,2 -> 1 and
        // 3,0 -> 0
        if ((gate == XOR_FAST) || (gate == XNOR_FAST)) {
            LWEscheme->EvalSubEq(ctprep, ct2);
            LWEscheme->EvalAddEq(ctprep, ctprep);
        }
        else {
            // for all other gates, we simply compute (ct1 + ct2) mod 4
            // for AND: 0,1 -> 0 and 2,3 -> 1
            // for OR: 1,2 -> 1 and 3,0 -> 0
            LWEscheme->EvalAddEq(ctprep, ct2);
        }

        auto acc = BootstrapCore(params, gate, EK.BSkey, ctprep);

        std::vector<NativePoly>& accVec = acc->GetElements();

        // the accumulator result is encrypted w.r.t. the transposed secret key
        // we can transpose "a" to get an encryption under the original secret key
        accVec[0] = accVec[0].Transpose();
        accVec[0].SetFormat(Format::COEFFICIENT);
        ctprep->SetA(accVec[0].GetValues());

        accVec[1].SetFormat(Format::COEFFICIENT);
        // we add Q/8 to "b" to to map back to Q/4 (i.e., mod 2) arithmetic.
        NativeInteger Q  = accVec[1].GetModulus();
        NativeInteger Q8 = Q / NativeInteger(8) + 1;
        ctprep->SetB(Q8.ModAddFast(accVec[1][0], Q));

        auto& LWEParams = params->GetLWEParams();
        // Modulus switching to a middle step Q'
        auto eQN = LWEscheme->ModSwitch(LWEParams->GetqKS(), ctprep);

        // Key switching
        auto eQ = LWEscheme->KeySwitch(LWEParams, EK.KSkey, eQN);

        // Modulus switching
        return LWEscheme->ModSwitch(ct1->GetModulus(), eQ);
    }
}

// Full evaluation as described in https://eprint.iacr.org/2020/08
LWECiphertext BinFHEScheme::Bootstrap(const std::shared_ptr<BinFHECryptoParams> params, const RingGSWBTKey& EK,
                                      ConstLWECiphertext ct1) const {
    LWECiphertext ctprep = std::make_shared<LWECiphertextImpl>(*ct1);
    NativeInteger q      = ctprep->GetModulus();
    LWEscheme->EvalAddConstEq(ctprep, q >> 2);

    auto acc = BootstrapCore(params, AND, EK.BSkey, ctprep);

    std::vector<NativePoly>& accVec = acc->GetElements();
    // the accumulator result is encrypted w.r.t. the transposed secret key
    // we can transpose "a" to get an encryption under the original secret key
    accVec[0] = accVec[0].Transpose();
    accVec[0].SetFormat(Format::COEFFICIENT);
    ctprep->SetA(accVec[0].GetValues());

    accVec[1].SetFormat(Format::COEFFICIENT);
    // we add Q/8 to "b" to to map back to Q/4 (i.e., mod 2) arithmetic.
    NativeInteger Q  = accVec[1].GetModulus();
    NativeInteger Q8 = Q / NativeInteger(8) + 1;
    ctprep->SetB(Q8.ModAddFast(accVec[1][0], Q));

    auto& LWEParams = params->GetLWEParams();
    // Modulus switching to a middle step Q'
    auto eQN = LWEscheme->ModSwitch(LWEParams->GetqKS(), ctprep);

    // Key switching
    auto eQ = LWEscheme->KeySwitch(LWEParams, EK.KSkey, eQN);

    // Modulus switching
    return LWEscheme->ModSwitch(q, eQ);
}

// Evaluation of the NOT operation; no key material is needed
LWECiphertext BinFHEScheme::EvalNOT(const std::shared_ptr<BinFHECryptoParams> params, ConstLWECiphertext ct) const {
    NativeInteger q = ct->GetModulus();
    uint32_t n      = ct->GetLength();

    NativeVector a(n, q);
    for (uint32_t i = 0; i < n; i++) {
        a[i] = q - ct->GetA(i);
    }

    NativeInteger b = (q >> 2).ModSubFast(ct->GetB(), q);

    return std::make_shared<LWECiphertextImpl>(std::move(a), b);
}

// Check what type of function the input function is.
int checkInputFunction(std::vector<NativeInteger> lut, NativeInteger bigger_q) {
    int ret = 0;  // 0 for negacyclic, 1 for periodic, 2 for arbitrary
    if (lut[0] == (bigger_q - lut[lut.size() / 2])) {
        for (size_t i = 1; i < lut.size() / 2; i++) {
            if (lut[i] != (bigger_q - lut[lut.size() / 2 + i])) {
                ret = 2;
                break;
            }
        }
    }
    else if (lut[0] == lut[lut.size() / 2]) {
        ret = 1;
        for (size_t i = 1; i < lut.size() / 2; i++) {
            if (lut[i] != lut[lut.size() / 2 + i]) {
                ret = 2;
                break;
            }
        }
    }
    else {
        ret = 2;
    }

    return ret;
}

// Evaluate Arbitrary Function homomorphically
LWECiphertext BinFHEScheme::EvalFunc(const std::shared_ptr<BinFHECryptoParams> params, const RingGSWBTKey& EK,
                                     ConstLWECiphertext ct1, const std::vector<NativeInteger>& LUT,
                                     const NativeInteger beta, const NativeInteger bigger_q) const {
    auto& LWEParams  = params->GetLWEParams();
    auto& RGSWParams = params->GetRingGSWParams();

    NativeInteger q              = ct1->GetModulus();
    NativeInteger bigger_q_local = bigger_q;
    if (bigger_q == 0)
        bigger_q_local = q;

    // Get what time of function it is
    int functionProperty = checkInputFunction(LUT, bigger_q_local);
    if (functionProperty == 0) {  // negacyclic function only needs one bootstrap
        auto f_neg = [LUT](NativeInteger x, NativeInteger q, NativeInteger Q) -> NativeInteger {
            return LUT[x.ConvertToInt()];
        };

        auto ct0 = std::make_shared<LWECiphertextImpl>(*ct1);
        LWEscheme->EvalAddConstEq(ct0, beta);
        return Bootstrap(params, EK, ct0, f_neg, q);
    }
    else if (functionProperty == 2) {  // arbitary funciton
        uint32_t N = LWEParams->GetN();
        if (q > N) {  // need q to be at most = N for arbitary function
            std::string errMsg =
                "ERROR: ciphertext modulus q needs to be <= ring dimension for arbitrary function evaluation";
            OPENFHE_THROW(not_implemented_error, errMsg);
        }

        auto ct0 = std::make_shared<LWECiphertextImpl>(*ct1);
        ct0->GetA().SetModulus(q << 1);

        LWEParams->SetQ(q * 2);
        RGSWParams->SetQ(q * 2);

        std::vector<NativeInteger> LUT_local = LUT;
        LUT_local.insert(LUT_local.end(), LUT.begin(), LUT.end());  // repeat the LUT to make it periodic
        // re-evaluate since it's now periodic
        auto ct2 = EvalFunc(params, EK, ct0, LUT_local, beta, bigger_q_local * 2);

        LWEscheme->SetModulus(ct2, bigger_q_local);

        LWEParams->SetQ(bigger_q_local);
        RGSWParams->SetQ(bigger_q_local);

        return ct2;
    }

    // Else it's periodic function so we evaluate directly

    auto f1 = [](NativeInteger x, NativeInteger q, NativeInteger Q) -> NativeInteger {
        if (x < q / 2)
            return Q - q / 4;
        else
            return q / 4;
    };

    // this is 1/4q_small or -1/4q_small mod q
    auto ct0 = std::make_shared<LWECiphertextImpl>(*ct1);
    LWEscheme->EvalAddConstEq(ct0, beta);
    auto ct2 = Bootstrap(params, EK, ct0, f1, q);

    auto a2 = ct1->GetA() - ct2->GetA();
    auto b2 = ct1->GetB().ModAddFast(beta, q).ModSubFast(ct2->GetB(), q);
    a2.SetModulus(q);
    b2.ModSubFastEq(q / 4, q);

    auto ct2_adj = std::make_shared<LWECiphertextImpl>(std::move(a2), std::move(b2));

    auto f_neg = [LUT](NativeInteger x, NativeInteger q, NativeInteger Q) -> NativeInteger {
        if (x < q / 2)
            return LUT[x.ConvertToInt()];
        else
            return Q - LUT[x.ConvertToInt() - q.ConvertToInt() / 2];
    };

    // Now the input is within the range [0, q/2).
    // Note that for non-periodic function, the input q is boosted up to 2q
    return Bootstrap(params, EK, ct2_adj, f_neg, bigger_q_local);
}

// Evaluate Homomorphic Flooring
LWECiphertext BinFHEScheme::EvalFloor(const std::shared_ptr<BinFHECryptoParams> params, const RingGSWBTKey& EK,
                                      ConstLWECiphertext ct1, const NativeInteger beta,
                                      const NativeInteger bigger_q) const {
    auto f1 = [](NativeInteger x, NativeInteger q, NativeInteger Q) -> NativeInteger {
        if (x < q / 2)
            return Q - q / 4;
        else
            return q / 4;
    };

    auto f2 = [](NativeInteger m, NativeInteger q, NativeInteger Q) -> NativeInteger {
        if (m < q / 4)
            return Q - q / 2 - m;
        else if ((q / 4 <= m) && (m < 3 * q / 4))
            return m;
        else
            return Q + q / 2 - m;
    };

    auto& LWEParams = params->GetLWEParams();

    NativeInteger q = ct1->GetModulus();

    const auto bigger_q_local = (bigger_q == 0) ? q : bigger_q;
    uint32_t n                = LWEParams->Getn();

    NativeVector a(n, bigger_q_local);
    NativeInteger b;

    auto a1 = ct1->GetA();
    auto b1 = ct1->GetB();
    a1.SetModulus(bigger_q_local);
    b1.ModAddFastEq(beta, bigger_q_local);

    auto a1_mod_q  = a1.Mod(q);
    auto b1_mod_q  = b1.Mod(q);
    auto ct0_mod_q = std::make_shared<LWECiphertextImpl>(std::move(a1_mod_q), std::move(b1_mod_q));

    // this is 1/4q_small or -1/4q_small mod q
    auto ct2 = Bootstrap(params, EK, ct0_mod_q, f1, bigger_q_local);
    auto a2  = a1 - ct2->GetA();
    a2.SetModulus(bigger_q_local);
    auto b2 = b1.ModSubFast(ct2->GetB(), bigger_q_local);

    auto a2_mod_q = a2.Mod(q);
    auto b2_mod_q = b2.Mod(q);
    a2_mod_q.SetModulus(q);
    auto ct2_mod_q = std::make_shared<LWECiphertextImpl>(std::move(a2_mod_q), std::move(b2_mod_q));

    // now the input is only within the range [0, q/2)
    auto ct3 = Bootstrap(params, EK, ct2_mod_q, f2, bigger_q_local);

    auto a3 = a2 - ct3->GetA();
    a3.SetModulus(bigger_q_local);
    auto b3 = b2.ModSubFast(ct3->GetB(), bigger_q_local);

    return std::make_shared<LWECiphertextImpl>(std::move(a3), std::move(b3));
}

// Evaluate large-precision sign
LWECiphertext BinFHEScheme::EvalSign(const std::shared_ptr<BinFHECryptoParams> params,
                                     const std::map<uint32_t, RingGSWBTKey>& EKs, ConstLWECiphertext ct1,
                                     const NativeInteger beta, const NativeInteger bigger_q) const {
    auto theBigger_q = bigger_q;
    auto& LWEParams  = params->GetLWEParams();
    auto& RGSWParams = params->GetRingGSWParams();

    NativeInteger q = LWEParams->Getq();

    if (theBigger_q <= q) {
        std::string errMsg =
            "ERROR: EvalSign is only for large precision. For small precision, please use bootstrapping directly";
        OPENFHE_THROW(not_implemented_error, errMsg);
    }

    const auto curBase = RGSWParams->GetBaseG();
    auto search        = EKs.find(curBase);
    if (search == EKs.end()) {
        std::string errMsg("ERROR: No key [" + std::to_string(curBase) + "] found in the map");
        OPENFHE_THROW(openfhe_error, errMsg);
    }
    RingGSWBTKey curEK(search->second);

    auto ct    = std::make_shared<LWECiphertextImpl>(ct1->GetA(), ct1->GetB());
    uint32_t n = LWEParams->Getn();
    while (theBigger_q > q) {
        ct          = EvalFloor(params, curEK, ct, beta, theBigger_q);
        auto temp   = theBigger_q;
        theBigger_q = theBigger_q / q * 2 * beta;

        if (EKs.size() == 3) {  // if dynamic
            uint32_t base = 0;
            if (ceil(log2(theBigger_q.ConvertToInt())) <= 17)
                base = 1 << 27;
            else if (ceil(log2(theBigger_q.ConvertToInt())) <= 26)
                base = 1 << 18;

            if (0 != base) {  // if base is to change ...
                RGSWParams->Change_BaseG(base);

                auto search = EKs.find(base);
                if (search == EKs.end()) {
                    std::string errMsg("ERROR: No key [" + std::to_string(curBase) + "] found in the map");
                    OPENFHE_THROW(openfhe_error, errMsg);
                }
                curEK = search->second;
            }
        }

        // round Q to 2betaQ/q
        NativeVector a_round(n, theBigger_q);
        for (uint32_t i = 0; i < n; ++i)
            a_round[i] = RoundqQ(ct->GetA()[i], theBigger_q, temp);
        NativeInteger b_round = RoundqQ(ct->GetB(), theBigger_q, temp);
        ct                    = std::make_shared<LWECiphertextImpl>(LWECiphertextImpl(a_round, b_round));
    }

    auto a1 = ct->GetA();
    auto b1 = ct->GetB();
    a1.SetModulus(theBigger_q);
    b1       = b1.ModAddFast(beta, theBigger_q);
    auto ct2 = std::make_shared<LWECiphertextImpl>(std::move(a1), std::move(b1));

    auto f3 = [](NativeInteger m, NativeInteger q, NativeInteger Q) -> NativeInteger {
        return (m < q / 2) ? (Q / 4) : (Q - Q / 4);
    };

    LWEParams->SetQ(theBigger_q);
    RGSWParams->SetQ(theBigger_q);

    // if the ended q is smaller than q, we need to change the param for the final boostrapping
    auto tmp = Bootstrap(params, curEK, ct2, f3, q);  // this is 1/4q_small or -1/4q_small mod q

    LWEParams->SetQ(q);   // if the ended q is smaller than q, we need to change the param for the final boostrapping
    RGSWParams->SetQ(q);  // if the ended q is smaller than q, we need to change the param for the final boostrapping

    NativeVector a_round  = tmp->GetA();
    NativeInteger b_round = tmp->GetB();
    a_round.SetModulus(q);
    b_round  = b_round.ModSubFast(q / 4, q);
    auto res = std::make_shared<LWECiphertextImpl>(LWECiphertextImpl(a_round, b_round));

    RGSWParams->Change_BaseG(curBase);
    return res;
}

// Evaluate Ciphertext Decomposition
std::vector<LWECiphertext> BinFHEScheme::EvalDecomp(const std::shared_ptr<BinFHECryptoParams> params,
                                                    const std::map<uint32_t, RingGSWBTKey>& EKs, ConstLWECiphertext ct1,
                                                    const NativeInteger beta, const NativeInteger bigger_q) const {
    auto theBigger_q = bigger_q;
    auto& LWEParams  = params->GetLWEParams();
    auto& RGSWParams = params->GetRingGSWParams();

    NativeInteger q = LWEParams->Getq();
    if (theBigger_q <= q) {
        std::string errMsg =
            "ERROR: EvalSign is only for large precision. For small precision, please use bootstrapping directly";
        OPENFHE_THROW(not_implemented_error, errMsg);
    }

    const auto curBase = RGSWParams->GetBaseG();
    auto search        = EKs.find(curBase);
    if (search == EKs.end()) {
        std::string errMsg("ERROR: No key [" + std::to_string(curBase) + "] found in the map");
        OPENFHE_THROW(openfhe_error, errMsg);
    }
    RingGSWBTKey curEK(search->second);

    auto ct    = std::make_shared<LWECiphertextImpl>(ct1->GetA(), ct1->GetB());
    uint32_t n = LWEParams->Getn();
    std::vector<LWECiphertext> ret;
    while (theBigger_q > q) {
        NativeVector a  = ct->GetA().Mod(q);
        NativeInteger b = ct->GetB().Mod(q);
        a.SetModulus(q);
        ret.push_back(std::make_shared<LWECiphertextImpl>(std::move(a), std::move(b)));

        // Floor the input sequentially to obtain the most significant bit
        ct          = EvalFloor(params, curEK, ct, beta, theBigger_q);
        auto temp   = theBigger_q;
        theBigger_q = theBigger_q / q * 2 * beta;

        if (EKs.size() == 3) {  // if dynamic
            uint32_t base = 0;
            if (ceil(log2(theBigger_q.ConvertToInt())) <= 17)
                base = 1 << 27;
            else if (ceil(log2(theBigger_q.ConvertToInt())) <= 26)
                base = 1 << 18;

            if (0 != base) {  // if base is to change ...
                RGSWParams->Change_BaseG(base);

                auto search = EKs.find(base);
                if (search == EKs.end()) {
                    std::string errMsg("ERROR: No key [" + std::to_string(curBase) + "] found in the map");
                    OPENFHE_THROW(openfhe_error, errMsg);
                }
                curEK = search->second;
            }
        }

        // round Q to 2betaQ/q
        NativeVector a_round(n, theBigger_q);
        for (uint32_t i = 0; i < n; ++i)
            a_round[i] = RoundqQ(ct->GetA()[i], theBigger_q, temp);
        NativeInteger b_round = RoundqQ(ct->GetB(), theBigger_q, temp);
        ct                    = std::make_shared<LWECiphertextImpl>(LWECiphertextImpl(a_round, b_round));
    }

    auto a1 = ct->GetA();
    auto b1 = ct->GetB();
    a1.SetModulus(theBigger_q);
    b1       = b1.ModAddFast(beta, theBigger_q);
    auto ct2 = std::make_shared<LWECiphertextImpl>(std::move(a1), std::move(b1));

    auto f3 = [](NativeInteger m, NativeInteger q, NativeInteger Q) -> NativeInteger {
        return (m < q / 2) ? (Q / 4) : (Q - Q / 4);
    };

    // if the ended q is smaller than q, we need to change the param for the final boostrapping
    LWEParams->SetQ(theBigger_q);
    RGSWParams->SetQ(theBigger_q);

    auto tmp = Bootstrap(params, curEK, ct2, f3, q);  // this is 1/4q_small or -1/4q_small mod q

    LWEParams->SetQ(q);   // if the ended q is smaller than q, we need to change the param for the final boostrapping
    RGSWParams->SetQ(q);  // if the ended q is smaller than q, we need to change the param for the final boostrapping

    NativeVector a_round  = tmp->GetA();
    NativeInteger b_round = tmp->GetB();
    a_round.SetModulus(q);
    b_round = b_round.ModSubFast(q / 4, q);
    ret.push_back(std::make_shared<LWECiphertextImpl>(LWECiphertextImpl(a_round, b_round)));

    RGSWParams->Change_BaseG(curBase);
    return ret;
}

// private:

RLWECiphertext BinFHEScheme::BootstrapCore(const std::shared_ptr<BinFHECryptoParams> params, const BINGATE gate,
                                           const RingGSWACCKey ek, ConstLWECiphertext ct) const {
    if (ek == nullptr) {
        std::string errMsg =
            "Bootstrapping keys have not been generated. Please call BTKeyGen "
            "before calling bootstrapping.";
        OPENFHE_THROW(config_error, errMsg);
    }

    auto& LWEParams  = params->GetLWEParams();
    auto& RGSWParams = params->GetRingGSWParams();
    auto polyParams  = RGSWParams->GetPolyParams();

    // Specifies the range [q1,q2) that will be used for mapping
    NativeInteger q  = ct->GetModulus();
    uint32_t qHalf   = q.ConvertToInt() >> 1;
    NativeInteger q1 = RGSWParams->GetGateConst()[static_cast<int>(gate)];
    NativeInteger q2 = q1.ModAddFast(NativeInteger(qHalf), q);

    // depending on whether the value is the range, it will be set
    // to either Q/8 or -Q/8 to match binary arithmetic
    NativeInteger Q     = LWEParams->GetQ();
    NativeInteger Q8    = Q / NativeInteger(8) + 1;
    NativeInteger Q8Neg = Q - Q8;

    uint32_t N = LWEParams->GetN();
    NativeVector m(N, Q);
    // Since q | (2*N), we deal with a sparse embedding of Z_Q[x]/(X^{q/2}+1) to
    // Z_Q[x]/(X^N+1)
    uint32_t factor = (2 * N / q.ConvertToInt());

    const NativeInteger& b = ct->GetB();
    for (uint32_t j = 0; j < qHalf; j++) {
        NativeInteger temp = b.ModSub(j, q);
        if (q1 < q2)
            m[j * factor] = ((temp >= q1) && (temp < q2)) ? Q8Neg : Q8;
        else
            m[j * factor] = ((temp >= q2) && (temp < q1)) ? Q8 : Q8Neg;
    }
    std::vector<NativePoly> res(2);
    // no need to do NTT as all coefficients of this poly are zero
    res[0] = NativePoly(polyParams, Format::EVALUATION, true);
    res[1] = NativePoly(polyParams, Format::COEFFICIENT, false);
    res[1].SetValues(std::move(m), Format::COEFFICIENT);
    res[1].SetFormat(Format::EVALUATION);

    // main accumulation computation
    // the following loop is the bottleneck of bootstrapping/binary gate
    // evaluation
    auto acc = std::make_shared<RLWECiphertextImpl>(std::move(res));

    ACCscheme->EvalAcc(RGSWParams, ek, acc, ct->GetA());

    return acc;
}

// Functions below are for large-precision sign evaluation,
// flooring, homomorphic digit decomposition, and arbitrary
// funciton evaluation, from https://eprint.iacr.org/2021/1337
template <typename Func>
RLWECiphertext BinFHEScheme::BootstrapCore(const std::shared_ptr<BinFHECryptoParams> params, const BINGATE gate,
                                           const RingGSWACCKey ek, ConstLWECiphertext ct, const Func f,
                                           const NativeInteger bigger_q) const {
    if (ek == nullptr) {
        std::string errMsg =
            "Bootstrapping keys have not been generated. Please call BTKeyGen "
            "before calling bootstrapping.";
        OPENFHE_THROW(config_error, errMsg);
    }

    auto& LWEParams  = params->GetLWEParams();
    auto& RGSWParams = params->GetRingGSWParams();
    auto polyParams  = RGSWParams->GetPolyParams();

    NativeInteger Q = LWEParams->GetQ();
    uint32_t N      = LWEParams->GetN();
    NativeVector m(N, Q);
    // For specific function evaluation instead of general bootstrapping
    //    NativeInteger q = ct->GetModulus();
    NativeInteger q        = LWEParams->Getq();
    uint32_t factor        = (2 * N / q.ConvertToInt());
    const NativeInteger& b = ct->GetB();
    for (uint32_t j = 0; j < q / 2; j++) {
        NativeInteger temp = b.ModSub(j, q);
        m[j * factor]      = Q.ConvertToInt() / bigger_q.ConvertToInt() * f(temp, q, bigger_q);
    }
    std::vector<NativePoly> res(2);
    // no need to do NTT as all coefficients of this poly are zero
    res[0] = NativePoly(polyParams, Format::EVALUATION, true);
    res[1] = NativePoly(polyParams, Format::COEFFICIENT, false);
    res[1].SetValues(std::move(m), Format::COEFFICIENT);
    res[1].SetFormat(Format::EVALUATION);

    // main accumulation computation
    // the following loop is the bottleneck of bootstrapping/binary gate
    // evaluation
    auto acc = std::make_shared<RLWECiphertextImpl>(std::move(res));
    ACCscheme->EvalAcc(RGSWParams, ek, acc, ct->GetA());

    return acc;
}

// Full evaluation as described in https://eprint.iacr.org/2020/08
template <typename Func>
LWECiphertext BinFHEScheme::Bootstrap(const std::shared_ptr<BinFHECryptoParams> params, const RingGSWBTKey& EK,
                                      ConstLWECiphertext ct1, const Func f, const NativeInteger bigger_q) const {
    NativeInteger toAdd = 0;  // we add beta outside as it's now dependent on plaintext space

    auto acc                        = BootstrapCore(params, AND, EK.BSkey, ct1, f, bigger_q);
    std::vector<NativePoly>& accVec = acc->GetElements();

    // the accumulator result is encrypted w.r.t. the transposed secret key
    // we can transpose "a" to get an encryption under the original secret key
    accVec[0] = accVec[0].Transpose();
    accVec[0].SetFormat(Format::COEFFICIENT);
    NativeVector aNew = accVec[0].GetValues();

    accVec[1].SetFormat(Format::COEFFICIENT);
    NativeInteger bNew = toAdd.ModAddFast(accVec[1][0], accVec[1].GetModulus());

    auto& LWEParams = params->GetLWEParams();
    // Modulus switching to a middle step Q'
    auto eQN = LWEscheme->ModSwitch(LWEParams->GetqKS(), std::make_shared<LWECiphertextImpl>(aNew, bNew));

    // Key switching
    auto eQ = LWEscheme->KeySwitch(LWEParams, EK.KSkey, eQN);

    // Modulus switching
    return LWEscheme->ModSwitch(bigger_q, eQ);
}

};  // namespace lbcrypto
