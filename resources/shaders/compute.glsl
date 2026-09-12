#version 430
#extension GL_ARB_shader_storage_buffer_object : require

precision highp float;

// Input vertex attributes (from vertex shader)
in vec2 fragTexCoord;

// Output fragment value (iteration / derivative / stripe data)
out vec4 finalColor;

// SSBO Layout
layout(std430, binding = 0) buffer ShaderParamsBuffer {
    float zoom;
    float seedX;
    float seedY;
    float rotation;
    int maxIterations;
    float screenWidth;
    float screenHeight;
    float scaleX;
    float scaleY;
    float lightHeight;
    float stripeFreq;
    float stripeOffset;
    float escapeRadius;
    float derivativeEscapeRadius;
    float jitterX;
    float jitterY;
} params;

// --- Helper Functions ---

vec2 cmul(vec2 a, vec2 b) {
    return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}

vec2 cdivShade(vec2 a, vec2 b) {
    return vec2((a.x * b.x + a.y * b.y), (a.y * b.x - a.x * b.y));
}

vec2 rotatePoint(vec2 v, float theta) {
    float c = cos(theta);
    float s = sin(theta);
    return vec2(v.x * c - v.y * s, v.x * s + v.y * c);
}

vec2 ConvertToSetCoord(vec2 uv, float scaleX, float scaleY) {
    vec2 c = vec2((uv.x - 0.5) * scaleX, (uv.y - 0.5) * scaleY);
    c = rotatePoint(c, params.rotation);
    c *= params.zoom;
    c += vec2(params.seedX, params.seedY);
    return c;
}

bool singleIteration(in vec2 c, inout float a, inout float b, inout float aa, inout float bb, inout vec2 d, in float maxPot2, in float derPot2, inout bool saved, inout vec3 slope) {
    float twoab = 2.0 * a * b;
    aa = a * a;
    bb = b * b;

    if (!saved && aa + bb > derPot2) {
        slope = normalize(vec3(cdivShade(vec2(a, b), d), 1.0));
        saved = true;
    } else if (!saved) {
        d = 2.0 * cmul(d, vec2(a, b)) + vec2(1.0, 0.0);
    }

    if (aa + bb > maxPot2) {
        return true;
    }
    a = aa - bb + c.x;
    b = twoab + c.y;
    return false;
}

vec4 renderPixel(vec2 c, float lightAngle) {
    int maxIter = params.maxIterations;
    float lightHeightValue = params.lightHeight;
    float maxPot2 = params.escapeRadius * params.escapeRadius;
    float derPot2 = params.derivativeEscapeRadius * params.derivativeEscapeRadius;
    float a = 0.0;
    float b = 0.0;
    float aa = a * a;
    float bb = b * b;
    vec2 d = vec2(1.0, 0.0);
    vec3 slope = vec3(0.0);
    bool saved = false;

    float stripeAdd = 0.0;
    float stripeAvg = 0.0;

    int iter = 0;
    
    if (params.stripeFreq > 0.0) {
        for (iter = 0; iter <= maxIter; iter++) {
            if(singleIteration(c, a, b, aa, bb, d, maxPot2, derPot2, saved, slope)) {
                break;
            }

            stripeAdd = 0.5 - 0.5 * sin(params.stripeFreq * atan(b, a) + params.stripeOffset);
            stripeAvg += stripeAdd;
        }
    } else {
        for (iter = 0; iter <= maxIter; iter++) {
            if(singleIteration(c, a, b, aa, bb, d, maxPot2, derPot2, saved, slope)) {
                break;
            }
        }
    }

    if (iter >= maxIter) {
        return vec4(-1.0, 0.0, 0.0, 1.0);
    }

    vec3 lightVec = normalize(vec3(cos(lightAngle + params.rotation), -sin(lightAngle + params.rotation), params.lightHeight));
    float shade = dot(slope, lightVec) + params.lightHeight;
    shade /= (1.0 + params.lightHeight);

    float frac = log2(log(a * a + b * b) / log(maxPot2));
    float escapeSmooth = float(iter) + 1.0 - frac;

    float prevIterDenom = float(max(iter - 1, 1));

    float stripeReturn = -100.f;
    if (params.stripeFreq > 0.0) {
        stripeReturn = (1.0 - frac) * (stripeAvg / float(iter)) + frac * ((stripeAvg - stripeAdd) / prevIterDenom);
    }

    return vec4(escapeSmooth, shade, stripeReturn, 1.0);
}

// --- Main Execution ---

void main() {
    // 1. Unpack SSBO parameters inside function scope
    vec2 screenSizeValue = vec2(params.screenWidth, params.screenHeight);
    vec2 scaleValue = vec2(params.scaleX, params.scaleY);
    const float lightAngle = -0.785398163397; // 45 degrees in radians

    // 2. Compute sub-pixel jittered UV coordinate
    vec2 pixelCoord = gl_FragCoord.xy + vec2(params.jitterX, params.jitterY);
    vec2 uv = pixelCoord / screenSizeValue;

    // 3. Map to complex coordinate space C & execute fractal iteration
    vec2 c = ConvertToSetCoord(uv, scaleValue.x, scaleValue.y);
    finalColor = renderPixel(c, lightAngle);
}