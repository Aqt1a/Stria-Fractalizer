#pragma once
#include <stdint.h>
#include <math.h>
#include <stdio.h>

// 512-bit fixed point: 16 limbs of 32 bits
// Binary point is after 8 bits of limbs[0]
// [8 bits integer] . [24 bits fr] [32 bits fr] ... [32 bits fr]
// Total fractional bits: 24 + 15 * 32 = 504 bits

#define LIMBS 16

struct BigFloat {
    uint32_t limbs[LIMBS]; // Big-endian: limbs[0] is most significant
    bool isNegative;

    BigFloat() {
        for (int i = 0; i < LIMBS; ++i) limbs[i] = 0;
        isNegative = false;
    }

    BigFloat(double d) {
        if (d < 0) { isNegative = true; d = -d; } else { isNegative = false; }
        for (int i = 0; i < LIMBS; ++i) limbs[i] = 0;

        double integral;
        double fractional = modf(d, &integral);
        
        // Integer part (clamped to 8 bits for safety)
        uint32_t intPart = (uint32_t)integral;
        limbs[0] = (intPart & 0xFF) << 24;

        // Fractional part
        for (int i = 0; i < LIMBS; ++i) {
            fractional *= 4294967296.0; // 2^32
            uint32_t limbVal = (uint32_t)fractional;
            
            if (i == 0) {
                // For limb 0, we only have 24 bits of fractional space left
                // Shift the double-precision fractional part accordingly
                double f24 = fractional / 256.0; // Rescale to fit 24 bits
                limbs[0] |= ((uint32_t)f24) & 0x00FFFFFF;
                fractional = fmod(f24, 1.0) * 256.0; // Keep remainder for limbs[1]
            } else {
                limbs[i] = limbVal;
                fractional -= limbVal;
            }
        }
    }

    double toDouble() const {
        double res = (double)(limbs[0] >> 24); // Integer part
        double fracBase = 1.0 / 16777216.0; // 2^-24
        res += (double)(limbs[0] & 0x00FFFFFF) * fracBase;
        
        double p32 = 1.0 / 4294967296.0; // 2^-32
        double currentP = fracBase * p32;
        for (int i = 1; i < LIMBS; ++i) {
            res += (double)limbs[i] * currentP;
            currentP *= p32;
            if (currentP < 1e-18) break; // Precision limit of double
        }
        return isNegative ? -res : res;
    }

    BigFloat operator+(const BigFloat& other) const {
        if (isNegative == other.isNegative) {
            BigFloat res;
            res.isNegative = isNegative;
            uint64_t carry = 0;
            for (int i = LIMBS - 1; i >= 0; --i) {
                uint64_t sum = (uint64_t)limbs[i] + other.limbs[i] + carry;
                res.limbs[i] = (uint32_t)(sum & 0xFFFFFFFF);
                carry = sum >> 32;
            }
            return res;
        } else {
            // Addition with different signs is subtraction
            BigFloat a = *this; a.isNegative = false;
            BigFloat b = other; b.isNegative = false;
            if (a > b) {
                BigFloat res = a - b;
                res.isNegative = isNegative;
                return res;
            } else {
                BigFloat res = b - a;
                res.isNegative = other.isNegative;
                return res;
            }
        }
    }

    BigFloat operator-(const BigFloat& other) const {
        if (isNegative != other.isNegative) {
            BigFloat res = *this; res.isNegative = false;
            BigFloat b = other; b.isNegative = false;
            res = res + b;
            res.isNegative = isNegative;
            return res;
        }
        // Same signs: check which is larger
        BigFloat a = *this; a.isNegative = false;
        BigFloat b = other; b.isNegative = false;
        if (a > b) {
            BigFloat res;
            res.isNegative = isNegative;
            int64_t borrow = 0;
            for (int i = LIMBS - 1; i >= 0; --i) {
                int64_t diff = (int64_t)limbs[i] - other.limbs[i] - borrow;
                if (diff < 0) {
                    res.limbs[i] = (uint32_t)(diff + 4294967296LL);
                    borrow = 1;
                } else {
                    res.limbs[i] = (uint32_t)diff;
                    borrow = 0;
                }
            }
            return res;
        } else {
            BigFloat res;
            res.isNegative = !isNegative;
            int64_t borrow = 0;
            for (int i = LIMBS - 1; i >= 0; --i) {
                int64_t diff = (int64_t)other.limbs[i] - limbs[i] - borrow;
                if (diff < 0) {
                    res.limbs[i] = (uint32_t)(diff + 4294967296LL);
                    borrow = 1;
                } else {
                    res.limbs[i] = (uint32_t)diff;
                    borrow = 0;
                }
            }
            return res;
        }
    }

    bool operator>(const BigFloat& other) const {
        if (isNegative != other.isNegative) return !isNegative;
        for (int i = 0; i < LIMBS; ++i) {
            if (limbs[i] > other.limbs[i]) return !isNegative;
            if (limbs[i] < other.limbs[i]) return isNegative;
        }
        return false;
    }

    BigFloat operator*(const BigFloat& other) const {
        uint32_t full[LIMBS * 2];
        for (int i = 0; i < LIMBS * 2; ++i) full[i] = 0;

        // Simple O(N^2) multiplication with limb-by-limb multiplication
        for (int i = LIMBS - 1; i >= 0; --i) {
            uint64_t carry = 0;
            for (int j = LIMBS - 1; j >= 0; --j) {
                int k = i + j + 1;
                uint64_t prod = (uint64_t)limbs[i] * other.limbs[j] + full[k] + carry;
                full[k] = (uint32_t)(prod & 0xFFFFFFFF);
                carry = prod >> 32;
            }
            full[i] = (uint32_t)carry;
        }

        BigFloat res;
        res.isNegative = (isNegative != other.isNegative);

        // Result selection: The product has 1008 fractional bits. 
        // We need to shift it right by 504 bits (15.75 limbs) to get back to 504 fractional bits.
        // Wait, shifting 504 bits right = shift 15 limbs right + 24 bits right.
        
        // Let's use src_idx from the right end for easier logic.
        // Last element of 'full' is index 31.
        // We want to pick bits [504, 504+511] from the right.
        
        for (int i = 0; i < LIMBS; ++i) {
            // Picking out the window of 512 bits starting 504 bits from the right.
            // Indices in 'full' are [0..31].
            // Limbs from the right: full[31] is bits 0-31. full[30] is bits 32-63, etc.
            // 504 bits / 32 = 15 limbs + 24 bits.
            // So we start at 15 limbs from the right (full[16]) and shift 24 bits.
            
            int src_idx = i + 15; 
            if (src_idx + 1 >= LIMBS * 2) {
                res.limbs[i] = 0;
                continue;
            }
            uint64_t combined = ((uint64_t)full[src_idx] << 32) | full[src_idx + 1];
            res.limbs[i] = (uint32_t)((combined >> 8) & 0xFFFFFFFF); // Shift 32-24=8? 
            // Wait, if it's 24 bits from the right within the limb, we shift by 24?
            // Limb bits: [31..0]. 24 bits right is bit 24.
            // To align bit 24 to the top, we shift?
            // Let's re-verify: 1008 bits frac. We want 504 bits frac.
            // Bit 1008 is bit 24 of full[15].
            // Bit 504 is bit 24 of full[31].
            // So we want the range starting at bit 24 of full[31] and going back 512 bits.
            // That range ends at bit 24 of full[15].
            // res.limbs[0] should be bits from full[15] and full[16].
        }
        
        // Accurate shift:
        for (int i = 0; i < LIMBS; ++i) {
            int idx = i + 15;
            uint64_t high = (uint64_t)full[idx];
            uint64_t low = (uint64_t)full[idx+1];
            res.limbs[i] = (uint32_t)((high << 8) | (low >> 24));
        }

        return res;
    }
};

struct BigComplex {
    BigFloat r, i;
    BigComplex() {}
    BigComplex(double _r, double _i) : r(_r), i(_i) {}
    BigComplex(BigFloat _r, BigFloat _i) : r(_r), i(_i) {}

    BigComplex operator+(const BigComplex& other) const {
        return { r + other.r, i + other.i };
    }

    BigComplex operator*(const BigComplex& other) const {
        return { r * other.r - i * other.i, r * other.i + i * other.r };
    }

    double magnitudeSq() const {
        double dr = r.toDouble();
        double di = i.toDouble();
        return dr*dr + di*di;
    }
};
