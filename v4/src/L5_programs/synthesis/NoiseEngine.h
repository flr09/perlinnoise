#ifndef NOISE_ENGINE_H
#define NOISE_ENGINE_H

#include "SimplexNoise.h"

struct NoiseConfig {
    float framesize = 0.01;
    float contrast = 1.0;
    float zShape = 1.0;
    float edgeC = 1.0;
};

class NoiseEngine {
public:
    NoiseEngine() : sn() {}

    // Raw-Passthrough zur unterliegenden SimplexNoise-Instanz.
    float noise(float x, float y) { return sn.noise(x, y); }

    // Noise-Shaping Math (Fix ID 25): jetzt public für Synthesis.cpp
    // mappt rohes Noise (-1..1) auf geformtes Noise (-1..1).
    float applyShape(float n, float zShape, float edgeC) {
        float nNorm = (n + 1.0f) / 2.0f;
        float exponent = fabsf(zShape);
        if (exponent < 0.1f) exponent = 0.1f;
        float nShaped = powf(nNorm, exponent);
        if (zShape < 0.0f) nShaped = 1.0f - nShaped;
        float finalNoise = (nShaped * 2.0f) - 1.0f;
        
        float edge = 1.0f - fabsf(finalNoise);
        float edgeMix = fmaxf(0.0f, fminf(2.0f, edgeC));
        float edgeBoost = (edge * 2.0f - 1.0f) * edgeMix;
        
        return finalNoise + edgeBoost;
    }

    float getVal(float flightX, float flightY, int motorIdx, float spacing, const NoiseConfig& cfg) {
        float offsetX = (motorIdx - 1.5f) * spacing;
        float sampleX = (flightX + offsetX) * cfg.framesize;
        float sampleY = (flightY) * cfg.framesize;
        float n = sn.noise(sampleX, sampleY);
        return applyShape(n, cfg.zShape, cfg.edgeC);
    }

private:
    SimplexNoise sn;
};

#endif
