#version 430

precision highp float;

#define PI 3.14159265358979323846

in vec2 fragTexCoord;
out vec4 finalColor;

// --- Textures & Accumulation ---
uniform sampler2D texture0;        // Raw iteration data from compute shader
uniform sampler2D historyTexture;  // Temporal AA accumulation buffer
uniform int frameCount;            // 0/1 = fresh frame, >1 = static accumulation

// --- Color & Shading Uniforms ---
uniform float paletteShift;
uniform float paletteScale;
uniform float iBri;
uniform float iBriScale;
uniform float iBriBase;
uniform float stripeLift;
uniform float stripeContrast;
uniform float stripeGamma;

// --- Palette Definition ---
const int palettecount = 3;
vec3 colorpalette[palettecount] = {
    vec3(20.0, -50.0, 255.0) / 255.0,
    vec3(220.0,   0.0, 200.0) / 255.0,
    vec3(255.0, 100.0,   0.0) / 255.0
};

// --- Helper Functions ---

float shift_map(float x, float shift, float contrast, float gamma) {
    return pow(x, exp(gamma)) * contrast + shift;
}

vec3 catmull(vec3 P[4], float t) {
    vec3 ret = P[0] * (-1.0 * pow(t, 3.0) + 2.0 * pow(t, 2.0) - t) +
               P[1] * ( 3.0 * pow(t, 3.0) - 5.0 * pow(t, 2.0) + 2.0) +
               P[2] * (-3.0 * pow(t, 3.0) + 4.0 * pow(t, 2.0) + t) +
               P[3] * (       pow(t, 3.0) -       pow(t, 2.0));
    return ret / 2.0;
}

vec3 interpolate(vec3 P[palettecount], float t) {
    vec3 cat[4];
    vec3 wrap[2 * palettecount + 1];

    for (int i = 0; i < palettecount; i++) {
        wrap[i] = P[i];
    }
    for (int i = palettecount; i < 2 * palettecount - 1; i++) {
        wrap[i] = P[palettecount - 2 - (i - palettecount)];
    }
    wrap[2 * palettecount] = P[1];

    t = t / 2.0;
    int l = (2 * palettecount) - 2;

    cat[0] = wrap[int(mod(float(l) * t,       float(l)))];
    cat[1] = wrap[int(mod(float(l) * t + 1.0, float(l)))];
    cat[2] = wrap[int(mod(float(l) * t + 2.0, float(l)))];
    cat[3] = wrap[int(mod(float(l) * t + 3.0, float(l)))];

    return catmull(cat, mod(float(l) * t, 1.0));
}

vec3 awesomePalette(in float t) {
    return interpolate(colorpalette, t);
}

vec4 CalculateColor(vec2 uv) {
    vec4 data = texture(texture0, uv);
    if (data.x < 0.0) {
        return vec4(0.0, 0.0, 0.0, 1.0); // Inside the Mandelbrot set
    }
    
    float iter = log(data.x);
    vec3 col = awesomePalette(paletteScale * iter + paletteShift);

    float stripe = 0.0;
    if (data.z > 0.0) {
        stripe = shift_map(data.z, stripeLift, stripeContrast, stripeGamma);
    }
    
    vec3 rgb = col * data.y 
             + exp((iter - iBri) / iBriScale) 
             + iBriBase - 1.0 
             + stripe;
             
    return vec4(rgb, 1.0);
}

// --- Main Execution ---

void main() {
    vec4 currentColor = CalculateColor(fragTexCoord);

    // Progressive Temporal AA Blending
    if (frameCount <= 1) {
        finalColor = currentColor;
    } else {
        // Invert Y-axis for the history texture lookup
        vec2 flippedUV = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
        vec4 historyColor = texture(historyTexture, flippedUV);
        
        float alpha = 1.0 / float(frameCount);
        finalColor = mix(historyColor, currentColor, alpha);
    }
}