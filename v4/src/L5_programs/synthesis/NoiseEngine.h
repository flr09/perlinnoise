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

    // Raw-Passthrough zur unterliegenden SimplexNoise-Instanz. Wird genutzt,
    // wo die Caller-Math nicht zur getVal-Symmetrie passt (z.B. tick() mit
    // i*mspace-Offset statt (i-1.5)*spacing) oder kein Shape gewollt ist
    // (Preview-Visualisierung).
    float noise(float x, float y) { return sn.noise(x, y); }

    float getVal(float flightX, float flightY, int motorIdx, float spacing, const NoiseConfig& cfg) {
        float offsetX = (motorIdx - 1.5f) * spacing;
        float sampleX = (flightX + offsetX) * cfg.framesize;
        float sampleY = (flightY) * cfg.framesize;
        float n = sn.noise(sampleX, sampleY);
        
        // Shape it
        float nNorm = (n + 1.0f) / 2.0f;
        float exponent = abs(cfg.zShape); if (exponent < 0.1f) exponent = 0.1f;
        float nShaped = powf(nNorm, exponent);
        if (cfg.zShape < 0) nShaped = 1.0f - nShaped;
        float finalNoise = (nShaped * 2.0f) - 1.0f;
        
        // Edge contrast
        float edge = 1.0f - abs(finalNoise);
        float edgeMix = fmaxf(0.0f, fminf(2.0f, cfg.edgeC));
        float edgeBoost = (edge * 2.0f - 1.0f) * edgeMix;
        
        return finalNoise + edgeBoost;
    }

private:
    SimplexNoise sn;
};

#endif
