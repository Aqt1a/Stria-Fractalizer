#include "../../Raylib/src/external/glad.h"
#include "../../Raylib/src/raylib.h"
#include "../../Raylib/src/raymath.h"
#include "../../Raylib/src/rlgl.h"

#define RAYGUI_TEXTBOX_AUTO_CURSOR_DELAY 5
#define RAYGUI_CUSTOM_ROUNDED_RECTS
#define RAYGUI_IMPLEMENTATION
#include "../../Raylib/raygui.h"

#include "../resources/tinyfiledialogs.h"
#include <fstream>
#include <iomanip>
#include <math.h>
#include <string>
#include <vector>

/*
Future...
perturbation implementation
advanced error detection + multiple reference orbits
Series approximation for iteration skipping

Color Palette editor
Fix load from file bugs...
*/

const int screenWidth = 1280;
const int screenHeight = 720;
const float zoomSpeed = 1.01f;
const float offsetSpeedMul = 2.0f;

float AASpread = .6f; // Spread of the subpixel jitter for multisampling

const float startingZoom = 0.f;
const Vector2 startingSeed = { -0.75f, 0.0f };

int frameCount = 0; // Number of frames rendered since last movement

Vector2 rotatePoint(Vector2 v, float theta){
    return {float(v.x*cos(theta)-v.y*sin(theta)), float(v.x*sin(theta)+v.y*cos(theta))};
}

// Used for pixel jitter
float Halton(int index, int base) {
    float f = 1.0f, r = 0.0f;
    while (index > 0) {
        f /= base;
        r += f * (index % base);
        index /= base;
    }
    return r;
}
// Multisampling jitter
Vector2 GetSubpixelJitter(int frameCount) {
    if (frameCount == 0) return { 0.0f, 0.0f };
    return {  (Halton(frameCount, 2) - 0.5f)*AASpread
            , (Halton(frameCount, 3) - 0.5f)*AASpread
           };
}

struct ViewPort {
public:
    float zoom;
    float zoomLevel;
    Vector2 seed;
    float rotation = 0.f; // Rotation in radians
    void SetZoomLevel(float newZoomLevel){
        zoom = 0.3 * powf(10, -newZoomLevel);
        zoomLevel = newZoomLevel;
    }
    void ZoomIn(float deltaZoomLevel){
        SetZoomLevel(zoomLevel + deltaZoomLevel);
    }
    ViewPort() {
        SetZoomLevel(startingZoom);
        seed = startingSeed;
    }
};

Vector2 ScreenToWorld(Vector2 mousePos, float screenW, float screenH, float scaleX, float scaleY, ViewPort vp) {
    // 1. Convert screen coordinates to centered normalized coordinates (-0.5 to 0.5)
    // Note: Y is inverted because Raylib has Y=0 at top, but shader's gl_FragCoord has Y=0 at bottom
    Vector2 norm = {
        (mousePos.x / screenW) - 0.5f,
        (screenH - mousePos.y) / screenH - 0.5f
    };

    // 2. Scale by aspect ratio and zoom level
    Vector2 scaled = {
        norm.x * scaleX * vp.zoom,
        norm.y * scaleY * vp.zoom
    };

    // 3. Rotate offset vector by current viewport rotation angle
    Vector2 rotated = rotatePoint(scaled, vp.rotation);

    // 4. Translate by viewport center seed
    return Vector2{ rotated.x + vp.seed.x, rotated.y + vp.seed.y };
}

float max(float a, float b) {
    return (a > b) ? a : b;
}

float clamp(float value, float minVal, float maxVal) {
    if (value < minVal) return minVal;
    if (value > maxVal) return maxVal;
    return value;
}

// ========== Menu System ==========

struct VariableMetadata {
    const char* name;           // "Max Iterations Multiplier"
    const char* category;       // "Iteration" / "Shading" / "Palette" (for future grouping)
    float* valuePtr;            // Pointer to actual float variable
    float minValue, maxValue;   // Safe bounds
    const char* units;          // "" / "x" / "%" (display only)
};

bool SaveSettings(const char* filePath, const std::vector<VariableMetadata>& registry,
                  float maxIterationsMultiplier, int maxSamples) {
    std::ofstream settingsFile(filePath);
    if (!settingsFile) return false;

    settingsFile << "MANDELBROT_SETTINGS 1\n";
    settingsFile << std::setprecision(9) << registry.size() << "\n";
    for (const VariableMetadata& variable : registry) {
        settingsFile << *variable.valuePtr << "\n";
    }
    settingsFile << maxIterationsMultiplier << "\n";
    settingsFile << maxSamples << "\n";
    return settingsFile.good();
}

bool LoadSettings(const char* filePath, const std::vector<VariableMetadata>& registry,
                  ViewPort& viewPort, float& maxIterationsMultiplier, int& maxSamples) {
    std::ifstream settingsFile(filePath);
    std::string header;
    int version = 0;
    size_t valueCount = 0;
    if (!(settingsFile >> header >> version >> valueCount)
        || header != "MANDELBROT_SETTINGS" || version != 1 || valueCount != registry.size()) {
        return false;
    }

    std::vector<float> values(valueCount);
    for (size_t i = 0; i < valueCount; i++) {
        if (!(settingsFile >> values[i]) || !std::isfinite(values[i])
            || values[i] < registry[i].minValue || values[i] > registry[i].maxValue) {
            return false;
        }
    }

    float loadedMaxIterationsMultiplier = 0.0f;
    int loadedMaxSamples = 0;
    if (!(settingsFile >> loadedMaxIterationsMultiplier >> loadedMaxSamples)
        || !std::isfinite(loadedMaxIterationsMultiplier)
        || loadedMaxIterationsMultiplier <= 0.0f
        || loadedMaxSamples < 1 || loadedMaxSamples > 1000) {
        return false;
    }

    for (size_t i = 0; i < valueCount; i++) {
        *registry[i].valuePtr = values[i];
    }
    viewPort.SetZoomLevel(viewPort.zoomLevel);
    maxIterationsMultiplier = loadedMaxIterationsMultiplier;
    maxSamples = loadedMaxSamples;
    return true;
}

struct MenuState {
    bool isOpen = false;
    Vector2 scroll = {0.0f, 0.0f};
    Rectangle panelBounds = {30.0f, 130.0f, 500.0f, 500.0f};
    std::vector<VariableMetadata>* registry = nullptr;
    ViewPort *vp = nullptr;
};

// ========== End Menu System ==========

// Global state for menu textbox editing (to persist across frames)
static int activeMenuTextBoxIndex = -1;
static char menuTextBuffers[30][32] = {0};  // Support up to 30 parameters

// Render the parameter menu using raygui
void RenderMenu(MenuState& menuState, bool* updateComputeShader, bool* updateColorShader) {
    if (!menuState.isOpen || !menuState.registry) return;

    const float rowHeight = 35.0f;
    const float labelWidth = 200.0f;
    const float textBoxWidth = 150.0f;
    const float padding = 20.0f;
    
    std::vector<VariableMetadata>& registry = *menuState.registry;
    
    // Calculate content bounds based on number of paramete rs
    Rectangle contentBounds = {0.0f, 0.0f, labelWidth + textBoxWidth + padding * 4.0f, (float)registry.size() * rowHeight + padding * 2};
    
    // Apply dark theme styling for better readability
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR, 0x1e1e1cdF);       // Actual background color used by GuiScrollPanel
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, 0x1e1e1ecF);      // Dark base for controls
    GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED, 0x2a2a2aFF);     // Darker when focused
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, 0xE0E0E0FF);      // Light gray text
    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, 0x404040FF);    // Dark border
    GuiSetStyle(DEFAULT, LINE_COLOR, 0x404040FF);             // Dark line color
    GuiSetStyle(SCROLLBAR, BASE_COLOR_NORMAL, 0x2a2a2aFF);    // Dark scrollbar
    GuiSetStyle(SCROLLBAR, BORDER_COLOR_NORMAL, 0x404040FF);  // Dark scrollbar border
    GuiSetStyle(TEXTBOX, BASE_COLOR_NORMAL, 0x2a2a2aFF);      // Dark textbox background
    GuiSetStyle(TEXTBOX, TEXT_COLOR_NORMAL, 0xE0E0E0FF);      // Light text in textbox
    GuiSetStyle(TEXTBOX, BORDER_COLOR_NORMAL, 0x404040FF);    // Dark textbox border
    GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED, 0x000000FF);     // Black text when focused
    GuiSetStyle(LABEL, TEXT_ALIGNMENT, TEXT_ALIGN_LEFT);      // Align labels to left
    GuiSetStyle(DEFAULT, TEXT_SIZE, 20);                      // Set label text size
    
    // Draw scrollable panel with the dark background color raygui actually uses
    GuiScrollPanel(menuState.panelBounds, "Parameters [TAB to close]", contentBounds, &menuState.scroll, nullptr);

    const float headerPadding = 15.0f;

    // Draw parameter controls inside the scroll panel
    for (size_t i = 0; i < registry.size(); i++) {
        VariableMetadata& var = registry[i];
        
        // Calculate Y position with scroll offset
        float y = menuState.panelBounds.y + padding + headerPadding + (float)i * rowHeight + menuState.scroll.y;
        
        // Only draw if visible in panel
        if (y + rowHeight < menuState.panelBounds.y || y > menuState.panelBounds.y + menuState.panelBounds.height) {
            continue;
        }
        
        // Label
        Rectangle labelRect = {menuState.panelBounds.x + padding, y, labelWidth - padding, rowHeight - 5.0f};
        
        // 2. Enable scissor clipping around the content loop
        int scissorX = menuState.panelBounds.x + 2;
        int scissorY = menuState.panelBounds.y + padding + 2;
        int scissorW = menuState.panelBounds.width - 4;
        int scissorH = menuState.panelBounds.height - padding - 4;
        BeginScissorMode(scissorX, scissorY, scissorW, scissorH);
            GuiLabel(labelRect, var.name);
            
            
            // Text input box for numeric value
            Rectangle textBoxRect = {menuState.panelBounds.x + labelWidth + padding, y, textBoxWidth, rowHeight - 5.0f};

            // Clicking a box activates it so raygui enters text-edit mode and accepts keyboard input.
            if (CheckCollisionPointRec(GetMousePosition(), textBoxRect) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                activeMenuTextBoxIndex = (int)i;
            }
            
            // Initialize buffer if needed
            if (i < 30) {
                if (activeMenuTextBoxIndex != (int)i && menuTextBuffers[i][0] == '\0') {
                    snprintf(menuTextBuffers[i], sizeof(menuTextBuffers[i]), "%.6g", *var.valuePtr);
                }
                
                // Update display value if not actively editing
                if (activeMenuTextBoxIndex != (int)i) {
                    snprintf(menuTextBuffers[i], sizeof(menuTextBuffers[i]), "%.6g", *var.valuePtr);
                }

                // Explicit clipboard support for the active variable textbox.
                if (activeMenuTextBoxIndex == (int)i && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
                    if (IsKeyPressed(KEY_C)) {
                        SetClipboardText(menuTextBuffers[i]);
                    } else if (IsKeyPressed(KEY_V)) {
                        const char* clipboardText = GetClipboardText();
                        if (clipboardText != nullptr && clipboardText[0] != '\0') {
                            char sanitized[64] = {0};
                            int sanitizedIndex = 0;

                            for (int k = 0; clipboardText[k] != '\0' && sanitizedIndex < (int)sizeof(sanitized) - 1; k++) {
                                char c = clipboardText[k];
                                if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') {
                                    sanitized[sanitizedIndex++] = c;
                                }
                            }

                            if (sanitizedIndex > 0) {
                                snprintf(menuTextBuffers[i], sizeof(menuTextBuffers[i]), "%s", sanitized);
                            }
                        }
                    }
                }
                
                int textBoxResult = GuiTextBox(textBoxRect, menuTextBuffers[i], sizeof(menuTextBuffers[i]), activeMenuTextBoxIndex == (int)i);
                
                if (textBoxResult == 1 && activeMenuTextBoxIndex == (int)i) {  // ENTER pressed
                    float newValue = std::strtof(menuTextBuffers[i], nullptr);
                    newValue = clamp(newValue, var.minValue, var.maxValue);
                    if (*var.valuePtr != newValue) {
                        // Handle special case for zoomLevel
                        if (i == 2) {
                            menuState.vp->SetZoomLevel(newValue);
                        } else {
                            *var.valuePtr = newValue;
                        }
                        *updateComputeShader = true;
                        *updateColorShader = true;
                    }
                    activeMenuTextBoxIndex = -1;
                } else if (textBoxResult == -1 && activeMenuTextBoxIndex == (int)i) {  // ESC pressed
                    activeMenuTextBoxIndex = -1;
                    snprintf(menuTextBuffers[i], sizeof(menuTextBuffers[i]), "%.6g", *var.valuePtr);
                } else if (textBoxResult == 2) {  // Text changed
                    activeMenuTextBoxIndex = i;
                }
            }
            
            // Units label (optional)
            if (var.units && var.units[0] != '\0') {
                Rectangle unitsRect = {menuState.panelBounds.x + labelWidth + textBoxWidth + padding * 2, y, 60.0f, rowHeight - 5.0f};
                GuiLabel(unitsRect, var.units);
            }
        }
    EndScissorMode();
}

struct ShaderStorageData {
public:
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
};

RenderTexture2D LoadRenderTexture32Bit(int width, int height) {
    RenderTexture2D dataTransport = { 0 };

    dataTransport.id = rlLoadFramebuffer(width, height);
    if (dataTransport.id > 0) {
        rlEnableFramebuffer(dataTransport.id);

        // Use RGBA 32-bit float (128 bits per pixel)
        dataTransport.texture.id = rlLoadTexture(
            NULL, 
            width, 
            height, 
            RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 
            1
        );
        dataTransport.texture.width = width;
        dataTransport.texture.height = height;
        dataTransport.texture.format = PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;
        dataTransport.texture.mipmaps = 1;

        // Attach texture to FBO
        rlActiveDrawBuffers(1);
        rlFramebufferAttach(
            dataTransport.id, 
            dataTransport.texture.id, 
            RL_ATTACHMENT_COLOR_CHANNEL0, 
            RL_ATTACHMENT_TEXTURE2D, 
            0
        );

        rlDisableFramebuffer();
    }
    return dataTransport;
}

RenderTexture2D LoadRenderTexture16Bit(int width, int height) {
    RenderTexture2D dataTransport = { 0 };

    dataTransport.id = rlLoadFramebuffer(width, height);
    if (dataTransport.id > 0) {
        rlEnableFramebuffer(dataTransport.id);

        // Use RGBA 16-bit float (64 bits per pixel)
        dataTransport.texture.id = rlLoadTexture(
            NULL, 
            width, 
            height, 
            RL_PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 
            1
        );
        dataTransport.texture.width = width;
        dataTransport.texture.height = height;
        dataTransport.texture.format = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
        dataTransport.texture.mipmaps = 1;

        // Attach texture to FBO
        rlActiveDrawBuffers(1);
        rlFramebufferAttach(
            dataTransport.id, 
            dataTransport.texture.id, 
            RL_ATTACHMENT_COLOR_CHANNEL0, 
            RL_ATTACHMENT_TEXTURE2D, 
            0
        );

        rlDisableFramebuffer();
    }
    return dataTransport;
}

struct KeyRepeater {
    int key;
    float delay; // Delay before repeat starts (e.g., 0.4s)
    float rate;  // Interval between repeats (e.g., 0.05s)
    float timer = 0.0f;
    bool* updateColorShader; // Pointer to the flag for updating the color shader

    KeyRepeater(int keyNew, float delayNew, float rateNew, bool &updateColorShaderLoc) {
        key = keyNew;
        delay = delayNew;
        rate = rateNew;
        updateColorShader = &updateColorShaderLoc;
    } 

    // Returns true on the initial press and every repeat interval while held
    bool Update() {
        if (!IsKeyDown(key)) {
            timer = 0.0f;
            return false;
        } else {
            frameCount = 0; // Reset accumulation state when key is held
        }

        if (IsKeyPressed(key)) {
            timer = -delay;
            *updateColorShader = true; // Set the flag to update the shader
            return true; // Initial press
        }

        timer += GetFrameTime();
        if (timer >= rate) {
            timer -= rate;
            *updateColorShader = true; // Set the flag to update the shader
            return true; // Repeated trigger
        }

        return false;
    }
};

struct KeyHandler {
    bool *updateComputeShader;

    // Helper for additive pairs (e.g., Key 1 = increase, Key Q = decrease)
    void handlePair(KeyRepeater& inc, KeyRepeater& dec, float& val, float delta, float minVal, float maxVal, int uniformLoc, Shader shader) {
        if (inc.Update()) {
            val = clamp(val + delta, minVal, maxVal);
            if (uniformLoc != -1) SetShaderValue(shader, uniformLoc, &val, SHADER_UNIFORM_FLOAT);
            *updateComputeShader = true;
        } else if (dec.Update()) {
            val = clamp(val - delta, minVal, maxVal);
            if (uniformLoc != -1) SetShaderValue(shader, uniformLoc, &val, SHADER_UNIFORM_FLOAT);
            *updateComputeShader = true;
        }
    }

    // Helper for multiplicative pairs (e.g., Key S = multiply, Key X = divide)
    void handlePairMul(KeyRepeater& inc, KeyRepeater& dec, float& val, float factor, float minVal, float maxVal) {
        if (inc.Update()) {
            val = clamp(val * factor, minVal, maxVal);
            *updateComputeShader = true;
        } else if (dec.Update()) {
            val = clamp(val / factor, minVal, maxVal);
            *updateComputeShader = true;
        }
    }
};

// This function assumes that all variables and uniforms have been updated before calling it
void ExportHighResMandelbrot(const char* fileName, int exportW, int exportH, int targetSamples,
                             Shader computeShader, Shader colorShader, 
                             int locFrameCount, int locHistoryTex,ShaderStorageData shaderParams,
                            int shaderParamsSSBO) {
    TraceLog(LOG_INFO, "MANDELBROT: Starting high-res render (%d x %d, %d samples)...", exportW, exportH, targetSamples);

    float aspectRatio = (float)exportW / (float)exportH;
    float targetAspectRatio = 16.f / 9.f;
    float scaleX = 16.f;
    float scaleY = 9.f;

    // Adjust scale to maintain aspect ratio using physical dimensions
    if (aspectRatio > targetAspectRatio) {
        scaleX *= aspectRatio / targetAspectRatio;
    } else {
        scaleY *= targetAspectRatio / aspectRatio;
    }

    shaderParams.screenWidth = (float)exportW;
    shaderParams.screenHeight = (float)exportH;
    shaderParams.scaleX = scaleX;
    shaderParams.scaleY = scaleY;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, shaderParamsSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(shaderParams), &shaderParams);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // 1. Create temporary high-resolution render textures for the export process
    RenderTexture2D hiResTransport = LoadRenderTexture32Bit(exportW, exportH);
    RenderTexture2D hiResBufA = LoadRenderTexture(exportW, exportH);
    RenderTexture2D hiResBufB = LoadRenderTexture(exportW, exportH);

    // Initialize accumulation buffers to black
    BeginTextureMode(hiResBufA); ClearBackground(BLACK); EndTextureMode();
    BeginTextureMode(hiResBufB); ClearBackground(BLACK); EndTextureMode();

    int localFrameCount = 0;

    // 2. Synchronously execute all samples in a blocking loop
    while (localFrameCount < targetSamples) {

        Vector2 jitter = GetSubpixelJitter(localFrameCount);
        shaderParams.jitterX = jitter.x;
        shaderParams.jitterY = jitter.y;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, shaderParamsSSBO);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(shaderParams), &shaderParams);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        
        // PASS 1: Compute Iteration Shader -> hiResTransport
        BeginTextureMode(hiResTransport);
            ClearBackground(BLACK);
            BeginShaderMode(computeShader);
                DrawRectangleRec(Rectangle{ 0.0f, 0.0f, (float)exportW, (float)exportH }, WHITE);
            EndShaderMode();
        EndTextureMode();

        // PASS 2: Color Mapping & History Blend -> hiResBufA
        BeginTextureMode(hiResBufA);
            ClearBackground(BLACK);
            BeginShaderMode(colorShader);
                SetShaderValue(colorShader, locFrameCount, &localFrameCount, SHADER_UNIFORM_INT);

                rlActiveTextureSlot(1);
                rlEnableTexture(hiResBufB.texture.id);
                SetShaderValueTexture(colorShader, locHistoryTex, hiResBufB.texture);

                Rectangle srcRec = { 0.0f, 0.0f, (float)exportW, (float)exportH };
                Rectangle dstRec = { 0.0f, 0.0f, (float)exportW, (float)exportH };
                DrawTexturePro(hiResTransport.texture, srcRec, dstRec, Vector2{ 0.0f, 0.0f }, 0.0f, WHITE);

                rlDisableTexture();
                rlActiveTextureSlot(0);
            EndShaderMode();
        EndTextureMode();

        // Ping-pong buffer swap
        RenderTexture2D temp = hiResBufA;
        hiResBufA = hiResBufB;
        hiResBufB = temp;

        localFrameCount++;
    }

    // 3. Extract the image from the final GPU texture
    Image img = LoadImageFromTexture(hiResBufB.texture);
    
    // OpenGL render textures are vertically flipped relative to standard image file formats
    //ImageFlipVertical(&img);
    
    // Save to disk (Raylib automatically detects format from extension like .png or .jpg)
    ExportImage(img, fileName);
    UnloadImage(img);

    // 4. Clean up temporary high-res GPU resources
    UnloadRenderTexture(hiResTransport);
    UnloadRenderTexture(hiResBufA);
    UnloadRenderTexture(hiResBufB);

    TraceLog(LOG_INFO, "MANDELBROT: High-res image successfully saved to %s", fileName);
}

void SetColorShaderUniforms(Shader colorShader,
                            int locPaletteShift, int locPaletteScale, int locPaletteMode,
                            int locIBri, int locIBriScale, int locIBriBase,
                            int locStripeLift, int locStripeContrast, int locStripeGamma,
                            float paletteShift, float paletteScale, int paletteMode,
                            float iBri, float iBriScale, float iBriBase,
                            float stripeLift, float stripeContrast, float stripeGamma) {
    SetShaderValue(colorShader, locPaletteShift, &paletteShift, SHADER_UNIFORM_FLOAT);
    SetShaderValue(colorShader, locPaletteScale, &paletteScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(colorShader, locPaletteMode, &paletteMode, SHADER_UNIFORM_INT);
    SetShaderValue(colorShader, locIBri, &iBri, SHADER_UNIFORM_FLOAT);
    SetShaderValue(colorShader, locIBriScale, &iBriScale, SHADER_UNIFORM_FLOAT);
    SetShaderValue(colorShader, locIBriBase, &iBriBase, SHADER_UNIFORM_FLOAT);
    SetShaderValue(colorShader, locStripeLift, &stripeLift, SHADER_UNIFORM_FLOAT);
    SetShaderValue(colorShader, locStripeContrast, &stripeContrast, SHADER_UNIFORM_FLOAT);
    SetShaderValue(colorShader, locStripeGamma, &stripeGamma, SHADER_UNIFORM_FLOAT);
}

//------------------------------------------------------------------------------------
// Program main entry point
//------------------------------------------------------------------------------------
int main(void) {
    // Initialization
    //--------------------------------------------------------------------------------------
    // Setup window configuration flags
    SetConfigFlags(FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_INTERLACED_HINT | FLAG_WINDOW_ALWAYS_RUN );  // Enable HighDPI support for better shader precision on HighDPI devices
    InitWindow(screenWidth, screenHeight, "Stria-Fractalizer");
    MaximizeWindow();

    Font font = LoadFont("resources/arial.ttf");
    GuiSetFont(font);
    // Ensure linear filtering is active for smooth scaling/rendering
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

    // Load mandelbrot compute and color shaders
    // NOTE: Defining 0 (NULL) for vertex shader forces usage of internal default vertex shader
    Shader computeShader = LoadShader(0, "resources/shaders/compute.glsl");
    Shader colorShader = LoadShader(0, "resources/shaders/color.glsl");

    // Create a RenderTexture2D to be used for render to texture
    //Image bitmapRaw = LoadImageRaw("resources/bitmap.raw", GetScreenWidth(), GetScreenHeight(), PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 0);
    //RenderTexture2D dataTransport = LoadTextureFromImage(bitmapRaw);  // Upload CPU (RAM) image to GPU (VRAM)
    //UnloadImage(bitmapRaw);                                // Unload CPU (RAM) image data
    RenderTexture2D dataTransport = LoadRenderTexture32Bit(GetScreenWidth(), GetScreenHeight());
    SetTextureFilter(dataTransport.texture, TEXTURE_FILTER_POINT);
    // Multi Sampling Buffers
    RenderTexture2D accumBufferA = LoadRenderTexture16Bit(GetScreenWidth(), GetScreenHeight());
    RenderTexture2D accumBufferB = LoadRenderTexture16Bit(GetScreenWidth(), GetScreenHeight());

    int maxSamples = 9; // Maximum number of samples for multisampling
    
    // Offset and zoom to draw the mandelbrot set at. (centered on screen and default size)
    ViewPort viewPort;
    // Depending on the zoom the mximum number of iterations must be adapted to get more detail as we zoom in
    // The solution is not perfect, so a control has been added to increase/decrease the number of iterations with UP/DOWN keys
    int maxIterations = 333;
    float maxIterationsMultiplier = 166.5f;
    float lightHeight = 1.4f;
    float stripeFreq = 1.0f;
    float stripeOffset = 0.0f;
    float escapeRadius = 100000.0f;
    float derivativeEscapeRadius = 90.0f;

    // Setup SSBO for compute shader
    // This functions like a uniform buffer, but allows for more data to be sent to the shader
    // The reason an SSBO is used here is to future proof when large arrays of reference points are sent to the shader.
    ShaderStorageData shaderParams{};
    shaderParams.zoom = viewPort.zoom; 
    shaderParams.seedX = viewPort.seed.x;
    shaderParams.seedY = viewPort.seed.y;
    shaderParams.rotation = viewPort.rotation;
    shaderParams.maxIterations = maxIterations;
    shaderParams.screenWidth = (float)GetScreenWidth();
    shaderParams.screenHeight = (float)GetScreenHeight();
    shaderParams.scaleX = 16.0f;
    shaderParams.scaleY = 9.0f;
    shaderParams.lightHeight = lightHeight;
    shaderParams.stripeFreq = stripeFreq;
    shaderParams.stripeOffset = stripeOffset;
    shaderParams.escapeRadius = escapeRadius;
    shaderParams.derivativeEscapeRadius = derivativeEscapeRadius;

    unsigned int shaderParamsSSBO = 0;
    glGenBuffers(1, &shaderParamsSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, shaderParamsSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(shaderParams), &shaderParams, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    glUseProgram(computeShader.id);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, shaderParamsSSBO);
    glUseProgram(0);

    // Get shader uniform locations for the color shader
    int locPaletteShift = GetShaderLocation(colorShader, "paletteShift");
    int locPaletteScale = GetShaderLocation(colorShader, "paletteScale");
    int locPaletteMode = GetShaderLocation(colorShader, "paletteMode");
    int locIBri = GetShaderLocation(colorShader, "iBri");
    int locIBriScale = GetShaderLocation(colorShader, "iBriScale");
    int locIBriBase = GetShaderLocation(colorShader, "iBriBase");
    int locStripeLift = GetShaderLocation(colorShader, "stripeLift");
    int locStripeContrast = GetShaderLocation(colorShader, "stripeContrast");
    int locStripeGamma = GetShaderLocation(colorShader, "stripeGamma");
    int locFrameCount = GetShaderLocation(colorShader, "frameCount");
    int locHistoryTex = GetShaderLocation(colorShader, "historyTexture");

    float paletteShift = 0.68f;
    float paletteScale = 0.5f;
    int paletteMode = 0;
    float stripeLift = -.5f;
    float stripeContrast = 2.0f;
    float stripeGamma = 1.0f;
    float iBri = 10.f;
    float iBriScale = 1.0f;
    float iBriBase = 1.0f;

    SetColorShaderUniforms(colorShader,
                           locPaletteShift, locPaletteScale, locPaletteMode,
                           locIBri, locIBriScale, locIBriBase,
                           locStripeLift, locStripeContrast, locStripeGamma,
                           paletteShift, paletteScale, paletteMode,
                           iBri, iBriScale, iBriBase,
                           stripeLift, stripeContrast, stripeGamma);
    
    bool showControls = true;           // Show controls

    // Mouse dragging variables
    bool isDragging = false;
    Vector2 lastMousePos = { 0.0f, 0.0f };
    // World-space point that was under the cursor when dragging started
    Vector2 dragAnchorWorld = { 0.0f, 0.0f };

    SetTargetFPS(60);                   // Set our game to run at 60 frames-per-second
    //--------------------------------------------------------------------------------------
    float screenW = (float)GetScreenWidth();
    float screenH = (float)GetScreenHeight();
    float renderW = (float)GetRenderWidth();
    float renderH = (float)GetRenderHeight();

    float aspectRatio = screenW / screenH;
    float targetAspectRatio = 16.f / 9.f;
    float scaleX = 16.f;
    float scaleY = 9.f;

    bool updateScreenSize = true;
    bool updateComputeShader = false;
    bool updateColorShader = false;
    bool exportDialogOpen = false;
    bool exportDimensionsInvalid = false;
    int exportActiveTextBox = -1;
    char exportWidthBuffer[16] = "1920";
    char exportHeightBuffer[16] = "1080";

    KeyHandler keyHandler;
    keyHandler.updateComputeShader = &updateComputeShader;

    // Initialize Menu System
    MenuState menuState;
    menuState.vp = &viewPort;
    std::vector<VariableMetadata> variableRegistry = {
        // Viewport Position
        {"Seed X", "Viewport", &viewPort.seed.x, -10.0f, 10.0f, ""},
        {"Seed Y", "Viewport", &viewPort.seed.y, -10.0f, 10.0f, ""},
        {"Viewport Zoom", "Viewport", &viewPort.zoomLevel, -100.f, 100.0f, "0.3*10^-x"},
        {"Viewport Rotation", "Viewport", &viewPort.rotation, -100.0f, 100.0f, "S/X"},
        
        // Palette & Colors
        {"Palette Shift", "Palette", &paletteShift, -1000.0f, 1000.0f, "1/Q"},
        {"Palette Scale", "Palette", &paletteScale, 0.01f, 100.0f, "2/W"},
        
        // Shading & stripes
        {"Light Height", "Shading", &lightHeight, 0.01f, 100.0f, "G/B"},
        {"Stripe Frequency", "Shading", &stripeFreq, 0.0f, 100.0f, "3/E"},
        {"Stripe Offset", "Shading", &stripeOffset, 0.0f, 100.0f, "4/R"},
        {"Stripe Shift", "Shading", &stripeLift, -10.0f, 10.0f, "5/T"},
        {"Stripe Contrast", "Shading", &stripeContrast, 0.0f, 10.0f, "6/Y"},
        {"Stripe Gamma", "Shading", &stripeGamma, -10.0f, 10.0f, "7/U"},
        
        // Exp Iter Brightness
        {"Iteration Brightness", "Brightness", &iBri, -500.0f, 500.0f, "8/I"},
        {"Brightness Scale", "Brightness", &iBriScale, 0.05f, 500.0f, "9/O"},
        {"Brightness Base", "Brightness", &iBriBase, 0.0f, 1.0f, "0/P"},
        
        // Iteration & Precision
        {"Escape Radius", "Iteration", &escapeRadius, 2.0f, 1000000000.0f, "D/C"},
        {"Derivative Escape Radius", "Iteration", &derivativeEscapeRadius, 1.0f, 1000.0f, "F/V"}

        //{"Derivative Escape Radius", "Iteration", &derivativeEscapeRadius, 1.0f, 1000.0f, "D/C"},
        
    };
    
    menuState.registry = &variableRegistry;


    // Palette Shift
    KeyRepeater paletteShiftUpKey = KeyRepeater(KEY_ONE, 0.1f, 0.025f, updateColorShader);
    KeyRepeater paletteShiftDownKey = KeyRepeater(KEY_Q, 0.1f, 0.025f, updateColorShader);
    // Palette Scale
    KeyRepeater paletteScaleUpKey = KeyRepeater(KEY_TWO, 0.1f, 0.05f, updateColorShader);
    KeyRepeater paletteScaleDownKey = KeyRepeater(KEY_W, 0.1f, 0.05f, updateColorShader);
    // Stripe frequency
    KeyRepeater stripeFrequencyUpKey = KeyRepeater(KEY_THREE, 0.4f, 0.25f, updateColorShader);
    KeyRepeater stripeFrequencyDownKey = KeyRepeater(KEY_E, 0.4f, 0.25f, updateColorShader);
    // Stripe offset
    KeyRepeater stripeOffsetUpKey = KeyRepeater(KEY_FOUR, 0.1f, 0.05f, updateColorShader);
    KeyRepeater stripeOffsetDownKey = KeyRepeater(KEY_R, 0.1f, 0.05f, updateColorShader);
    // Stripe shift
    KeyRepeater stripeLiftUpKey = KeyRepeater(KEY_FIVE, 0.1f, 0.05f, updateColorShader);
    KeyRepeater stripeLiftDownKey = KeyRepeater(KEY_T, 0.1f, 0.05f, updateColorShader);
    // Escape radius
    KeyRepeater escapeRadiusUpKey = KeyRepeater(KEY_D, 0.1f, 0.05f, updateColorShader);
    KeyRepeater escapeRadiusDownKey = KeyRepeater(KEY_C, 0.1f, 0.05f, updateColorShader);
    // Derivative escape radius
    KeyRepeater derivativeEscapeRadiusUpKey = KeyRepeater(KEY_F, 0.1f, 0.05f, updateColorShader);
    KeyRepeater derivativeEscapeRadiusDownKey = KeyRepeater(KEY_V, 0.1f, 0.05f, updateColorShader);
    // Brightness Shift
    KeyRepeater iterationBrightnessUpKey = KeyRepeater(KEY_EIGHT, 0.25f, 0.0125f, updateColorShader);
    KeyRepeater iterationBrightnessDownKey = KeyRepeater(KEY_I, 0.5f, 0.025f, updateColorShader);
    // Brightness scale
    KeyRepeater brightnessScaleUpKey = KeyRepeater(KEY_NINE, 0.1f, 0.05f, updateColorShader);
    KeyRepeater brightnessScaleDownKey = KeyRepeater(KEY_O, 0.1f, 0.05f, updateColorShader);
    // Brightness base
    KeyRepeater brightnessBaseUpKey = KeyRepeater(KEY_ZERO, 0.1f, 0.05f, updateColorShader);
    KeyRepeater brightnessBaseDownKey = KeyRepeater(KEY_P, 0.1f, 0.05f, updateColorShader);
    // Stripe contrast
    KeyRepeater stripeContrastUpKey = KeyRepeater(KEY_SIX, 0.1f, 0.05f, updateColorShader);
    KeyRepeater stripeContrastDownKey = KeyRepeater(KEY_Y, 0.1f, 0.05f, updateColorShader);
    // Stripe gamma
    KeyRepeater stripeGammaUpKey = KeyRepeater(KEY_SEVEN, 0.1f, 0.05f, updateColorShader);
    KeyRepeater stripeGammaDownKey = KeyRepeater(KEY_U, 0.1f, 0.05f, updateColorShader);
    // Light height
    KeyRepeater lightHeightUpKey = KeyRepeater(KEY_G, 0.1f, 0.05f, updateColorShader);
    KeyRepeater lightHeightDownKey = KeyRepeater(KEY_B, 0.1f, 0.05f, updateColorShader);
    // World Rotation
    KeyRepeater rotationUpKey = KeyRepeater(KEY_X, 0.1f, 0.05f, updateColorShader);
    KeyRepeater rotationDownKey = KeyRepeater(KEY_S, 0.1f, 0.05f, updateColorShader);
    
    // Main game loop
    while (!WindowShouldClose() || exportDialogOpen) {        // Detect window close button or ESC key, but don't close mid export
        // Update
        //----------------------------------------------------------------------------------
    
        if(IsWindowResized()) updateScreenSize = true;
        
        // Check if window was resized and recreate render texture if needed
        if (updateScreenSize) {
            screenW = (float)GetScreenWidth();
            screenH = (float)GetScreenHeight();

            renderW = (float)GetRenderWidth();
            renderH = (float)GetRenderHeight();

            aspectRatio = screenW / screenH;
            targetAspectRatio = 16.f / 9.f;
            scaleX = 16.f;
            scaleY = 9.f;

            // Adjust scale to maintain aspect ratio using physical dimensions
            if (aspectRatio > targetAspectRatio) {
                scaleX *= aspectRatio / targetAspectRatio;
            } else {
                scaleY *= targetAspectRatio / aspectRatio;
            }

            // 2. Unload old framebuffers
            UnloadRenderTexture(dataTransport);
            UnloadRenderTexture(accumBufferA);
            UnloadRenderTexture(accumBufferB);

            // 3. Recreate framebuffers using PHYSICAL render dimensions (1:1 pixel mapping)
            dataTransport = LoadRenderTexture32Bit(renderW, renderH);
            SetTextureFilter(dataTransport.texture, TEXTURE_FILTER_POINT);

            accumBufferA = LoadRenderTexture16Bit(renderW, renderH);
            SetTextureFilter(accumBufferA.texture, TEXTURE_FILTER_POINT);

            accumBufferB = LoadRenderTexture16Bit(renderW, renderH);
            SetTextureFilter(accumBufferB.texture, TEXTURE_FILTER_POINT);

            // 4. Update SSBO screen parameters with physical dimensions
            shaderParams.screenWidth = renderW;
            shaderParams.screenHeight = renderH;

            updateScreenSize = false;
            updateComputeShader = true;
            frameCount = 0; // Reset accumulation state on resize
        }

        Rectangle exportButtonBounds = { screenW - 180.0f, 15.0f, 160.0f, 30.0f };
        Rectangle saveSettingsButtonBounds = { screenW - 180.0f, 50.0f, 160.0f, 30.0f };
        Rectangle loadSettingsButtonBounds = { screenW - 180.0f, 85.0f, 160.0f, 30.0f };
        
        if (IsKeyPressed(KEY_F11) || IsKeyPressed(KEY_HOME)) {
            updateScreenSize = true; // Set flag to update screen size and render texture
            ToggleBorderlessWindowed();  // Toggle borderless windowed mode
        }
        if (IsKeyPressed(KEY_F1) && !exportDialogOpen) showControls = !showControls;  // Toggle whether or not to show controls
        
        // Toggle menu with TAB
        if (IsKeyPressed(KEY_TAB) && !exportDialogOpen) {
            menuState.isOpen = !menuState.isOpen;
        }

        // Only process normal keyboard input if menu is closed
        if (!menuState.isOpen && !exportDialogOpen) {
            // Change number of max iterations with A and Z keys
            if (IsKeyPressed(KEY_A)) {
                maxIterationsMultiplier *= 1.4f;
                updateComputeShader = true;
            }
            else if (IsKeyPressed(KEY_Z)) {
                maxIterationsMultiplier /= 1.4f;
                updateComputeShader = true;
            }
        }

        Vector2 mousePos = GetMousePosition();

        // Disable mouse controls when menu is open
        if (!menuState.isOpen && !exportDialogOpen
            && !CheckCollisionPointRec(mousePos, exportButtonBounds)
            && !CheckCollisionPointRec(mousePos, saveSettingsButtonBounds)
            && !CheckCollisionPointRec(mousePos, loadSettingsButtonBounds)) {
            // Handle mouse wheel for zooming
            float wheelMove = GetMouseWheelMove();
            if (wheelMove != 0.0f) {
                Vector2 mouseWorldBefore = ScreenToWorld(mousePos, screenW, screenH, scaleX, scaleY, viewPort);
                //float newZoomLevel = viewPort.zoomLevel + wheelMove * 0.05f;
                //viewPort.SetZoomLevel(newZoomLevel);
                viewPort.ZoomIn(wheelMove * 0.05f);

                // New Logic with rotation support
                Vector2 norm = { (mousePos.x / screenW) - 0.5f, (screenH - mousePos.y) / screenH - 0.5f };
                Vector2 scaled = { norm.x * scaleX * viewPort.zoom, norm.y * scaleY * viewPort.zoom };
                Vector2 rotated = rotatePoint(scaled, viewPort.rotation);

                viewPort.seed.x = mouseWorldBefore.x - rotated.x;
                viewPort.seed.y = mouseWorldBefore.y - rotated.y;

                updateComputeShader = true;
            }

            // Handle mouse dragging for panning
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                isDragging = true;

                // Compute the world-space coordinate under the cursor when drag starts

                dragAnchorWorld = ScreenToWorld(mousePos, screenW, screenH, scaleX, scaleY, viewPort);
            } else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                isDragging = false;
            }
            
            if (isDragging) {
                // Recompute seed so the original world point stays under the cursor

                // New: 
                Vector2 norm = { (mousePos.x / screenW) - 0.5f, (screenH - mousePos.y) / screenH - 0.5f };
                Vector2 scaled = { norm.x * scaleX * viewPort.zoom, norm.y * scaleY * viewPort.zoom };
                Vector2 rotated = rotatePoint(scaled, viewPort.rotation);
                viewPort.seed.x = dragAnchorWorld.x - rotated.x;
                viewPort.seed.y = dragAnchorWorld.y - rotated.y;    

                updateComputeShader = true;
            }
        } else {
            // Release drag when menu opens
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mousePos, menuState.panelBounds)
                && !CheckCollisionPointRec(mousePos, exportButtonBounds)
                && !CheckCollisionPointRec(mousePos, saveSettingsButtonBounds)
                && !CheckCollisionPointRec(mousePos, loadSettingsButtonBounds)
                && !exportDialogOpen) {
                menuState.isOpen = false; // Close menu if user clicks outside of it
                isDragging = true;
                dragAnchorWorld = ScreenToWorld(mousePos, screenW, screenH, scaleX, scaleY, viewPort);
            } else {
                isDragging = false;
            }
        }

        // Only process parameter adjustments if menu is closed
        if (!menuState.isOpen && !exportDialogOpen) {
            keyHandler.handlePair(paletteShiftUpKey, paletteShiftDownKey, paletteShift, 0.005f, -1000.0f, 1000.0f, locPaletteShift, colorShader);
            keyHandler.handlePair(paletteScaleUpKey, paletteScaleDownKey, paletteScale, 0.01f, 0.01f, 100.0f, locPaletteScale, colorShader);
            keyHandler.handlePair(stripeFrequencyUpKey, stripeFrequencyDownKey, stripeFreq, 1.0f, 0.0f, 100.0f, -1, colorShader);
            keyHandler.handlePair(stripeOffsetUpKey, stripeOffsetDownKey, stripeOffset, 0.05f, 0.0f, 100.0f, -1, colorShader);
            keyHandler.handlePair(stripeLiftUpKey, stripeLiftDownKey, stripeLift, 0.01f, -10.0f, 10.0f, locStripeLift, colorShader);

            if (escapeRadiusUpKey.Update()) {
                escapeRadius *= 1.1f;
                updateComputeShader = true;
            }
            if (escapeRadiusDownKey.Update()) {
                escapeRadius = max(escapeRadius / 1.1f, 1.0f);
                updateComputeShader = true;
            }
            if (derivativeEscapeRadiusUpKey.Update()) {
                derivativeEscapeRadius *= 1.1f;
                updateComputeShader = true;
            }
            if (derivativeEscapeRadiusDownKey.Update()) {
                derivativeEscapeRadius = max(derivativeEscapeRadius / 1.1f, 1.0f);
                updateComputeShader = true;
            }
            if (iterationBrightnessUpKey.Update()) {
                iBri = clamp(iBri + 0.05f, -maxIterations, maxIterations);
                SetShaderValue(colorShader, locIBri, &iBri, SHADER_UNIFORM_FLOAT);
                updateComputeShader = true;
            }
            if (iterationBrightnessDownKey.Update()) {
                iBri = clamp(iBri - 0.05f, -maxIterations, maxIterations);
                SetShaderValue(colorShader, locIBri, &iBri, SHADER_UNIFORM_FLOAT);
                updateComputeShader = true;
            }
            if (brightnessScaleUpKey.Update()) {
                iBriScale = clamp(iBriScale + 0.05f, 0.05f, 500.0f);
                SetShaderValue(colorShader, locIBriScale, &iBriScale, SHADER_UNIFORM_FLOAT);
                updateComputeShader = true;
            }
            if (brightnessScaleDownKey.Update()){
                iBriScale = clamp(iBriScale - 0.05f, 0.05f, 500.0f);
                SetShaderValue(colorShader, locIBriScale, &iBriScale, SHADER_UNIFORM_FLOAT);
                updateComputeShader = true;
            }
            if (brightnessBaseUpKey.Update()) {
                iBriBase = clamp(iBriBase + 0.05f, 0.0f, 1.0f);
                SetShaderValue(colorShader, locIBriBase, &iBriBase, SHADER_UNIFORM_FLOAT);
                updateComputeShader = true;
            }
            if (brightnessBaseDownKey.Update()) {
                iBriBase = clamp(iBriBase - 0.05f, 0.0f, 1.0f);
                SetShaderValue(colorShader, locIBriBase, &iBriBase, SHADER_UNIFORM_FLOAT);
                updateComputeShader = true;
            }
            if (stripeContrastUpKey.Update()) {
                stripeContrast = clamp(stripeContrast + 0.02f, 0.0f, 10.0f);
                SetShaderValue(colorShader, locStripeContrast, &stripeContrast, SHADER_UNIFORM_FLOAT);
                updateComputeShader = true;
            }
            if (stripeContrastDownKey.Update()) {
                stripeContrast = clamp(stripeContrast - 0.02f, 0.0f, 10.0f);
                SetShaderValue(colorShader, locStripeContrast, &stripeContrast, SHADER_UNIFORM_FLOAT);
                updateComputeShader = true;
            }
            if (stripeGammaUpKey.Update()) {
                stripeGamma = clamp(stripeGamma + 0.02f, -10.0f, 10.0f);
                SetShaderValue(colorShader, locStripeGamma, &stripeGamma, SHADER_UNIFORM_FLOAT);
                updateComputeShader = true;
            }
            if (stripeGammaDownKey.Update()) {
                stripeGamma = clamp(stripeGamma - 0.02f, -10.0f, 10.0f);
                SetShaderValue(colorShader, locStripeGamma, &stripeGamma, SHADER_UNIFORM_FLOAT);
                updateComputeShader = true;
            }
            keyHandler.handlePair(lightHeightUpKey, lightHeightDownKey, lightHeight, 0.01f, 0.01f, 100.0f, -1, colorShader);
            if (rotationUpKey.Update()) {
                viewPort.rotation += 0.01f;
                updateComputeShader = true;
            }
            if (rotationDownKey.Update()) {
                viewPort.rotation -= 0.01f;
                updateComputeShader = true;
            }
        }  // End of if (!menuState.isOpen)

        // ------------------------------------------------------------------------------
        // Multisampling
        // ------------------------------------------------------------------------------

        if (updateComputeShader) {
            frameCount = 0; // Reset accumulation state on movement
            // zoomLevel is logarithmic, so each decade of zoom adds a predictable amount of detail.
            maxIterations = (int)((2.0f + viewPort.zoomLevel) * maxIterationsMultiplier);
        }

        bool isAccumulating = (frameCount < maxSamples);

        // If we have more samples to check
        if (isAccumulating) {
            Vector2 jitter = GetSubpixelJitter(frameCount);

            // Update SSBO with parameters and current sub-pixel jitter
            shaderParams.zoom = viewPort.zoom;
            shaderParams.seedX = viewPort.seed.x;
            shaderParams.seedY = viewPort.seed.y;
            shaderParams.rotation = viewPort.rotation;
            shaderParams.maxIterations = maxIterations;
            shaderParams.scaleX = scaleX;
            shaderParams.scaleY = scaleY;
            shaderParams.lightHeight = lightHeight;
            shaderParams.stripeFreq = stripeFreq;
            shaderParams.stripeOffset = stripeOffset;
            shaderParams.escapeRadius = escapeRadius;
            shaderParams.derivativeEscapeRadius = derivativeEscapeRadius;
            shaderParams.jitterX = jitter.x; 
            shaderParams.jitterY = jitter.y;

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, shaderParamsSSBO);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(shaderParams), &shaderParams);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

            // PASS 1: Compute Iteration Shader -> dataTransport
            BeginTextureMode(dataTransport);
                ClearBackground(BLACK);
                BeginShaderMode(computeShader);
                    DrawRectangleRec(Rectangle{ 0.0f, 0.0f, renderW, renderH }, WHITE);
                EndShaderMode();
            EndTextureMode();

            // PASS 2: Color Mapping & History Blend -> accumBufferA
            BeginTextureMode(accumBufferA);
                ClearBackground(BLACK);
                
                BeginShaderMode(colorShader);
                    // 1. Pass frame count uniform
                    SetShaderValue(colorShader, locFrameCount, &frameCount, SHADER_UNIFORM_INT);

                    // 2. Bind accumBufferB (history texture) to texture unit 1 while shader is bound
                    rlActiveTextureSlot(1);
                    rlEnableTexture(accumBufferB.texture.id);
                    SetShaderValueTexture(colorShader, locHistoryTex, accumBufferB.texture);

                    // 3. Draw dataTransport.texture (iteration data) through color shader
                    Rectangle srcRec = { 0.0f, 0.0f, (float)dataTransport.texture.width, (float)dataTransport.texture.height };
                    Rectangle dstRec = { 0.0f, 0.0f, renderW, renderH };
                    DrawTexturePro(dataTransport.texture, srcRec, dstRec, Vector2{ 0.0f, 0.0f }, 0.0f, WHITE);

                    // 4. Clean up texture unit 1 and revert to unit 0
                    rlDisableTexture();
                    rlActiveTextureSlot(0);
                EndShaderMode();
            EndTextureMode();

            // Ping-pong buffer swap so accumBufferA becomes history for frameCount + 1
            RenderTexture2D temp = accumBufferA;
            accumBufferA = accumBufferB;
            accumBufferB = temp;

            frameCount++;
            updateComputeShader = false;
        }


        BeginDrawing();
            ClearBackground(BLACK);     // Clear screen background

            // -------------------------------------------------------------------------------
            // 3. Display Pass (Blits accumulated texture to the full window)
            // -------------------------------------------------------------------------------

            // Define source rectangle: Note the negative height to flip OpenGL's upside-down Y-axis.
            // Use the actual physical texture width/height for the source size.
            Rectangle sourceRec = {0.0f, 0.0f, renderW, renderH};

            // Define destination rectangle: Use logical window dimensions so it scales 
            // to fill the entire window correctly across all DPI displays.
            Rectangle destRec = {0.0f, 0.0f, screenW, screenH};

            Vector2 origin = { 0.0f, 0.0f };

            // Draw the final accumulation buffer scaled to fill the screen
            DrawTexturePro(accumBufferB.texture, sourceRec, destRec, origin, 0.0f, WHITE);

            if (showControls) {
                DrawTextEx(font, TextFormat("Samples: %d / %d spp %s", 
                        frameCount, maxSamples, 
                        (frameCount >= maxSamples) ? "(Done)" : "(Accumulating...)"), 
                        Vector2{ 10, 15 }, 15, 2.0f, (frameCount >= maxSamples) ? GREEN : YELLOW);
            }

            if (showControls){
                DrawTextEx(font, "Mouse wheel to zoom in/out",Vector2{10, 30}, 15, 2.0f, RAYWHITE);
                DrawTextEx(font, "Left mouse button + drag to pan",Vector2{10, 45}, 15, 2.0f, RAYWHITE);
                DrawTextEx(font, "F1 to toggle these controls",Vector2{10, 60}, 15, 2.0f, RAYWHITE);
                //DrawText("Press [1 - 6] to change point of interest", 10, 75, 15, RAYWHITE);
                DrawTextEx(font, "A | Z to change number of iterations",Vector2{10, 75}, 15, 2.0f, RAYWHITE);
                DrawTextEx(font, "TAB to open parameter menu",Vector2{10, 90}, 15, 2.0f, RAYWHITE);
            }

            // Render the parameter menu overlay
            // Interal function prevents rendering when menu is closed
            RenderMenu(menuState, &updateComputeShader, &updateColorShader);

            bool exportRequested = false;
            bool exportConfirmed = false;
            bool exportCancelled = false;
            bool saveSettingsRequested = false;
            bool loadSettingsRequested = false;

            if (!exportDialogOpen && menuState.isOpen) {
                exportRequested = GuiButton(exportButtonBounds, "Export Image") == 1;
                saveSettingsRequested = GuiButton(saveSettingsButtonBounds, "Save Settings") == 1;
                loadSettingsRequested = GuiButton(loadSettingsButtonBounds, "Load Settings") == 1;
            }

            if (exportDialogOpen) {
                Rectangle dialogBounds = { screenW / 2.0f - 220.0f, screenH / 2.0f - 145.0f, 440.0f, 290.0f };
                Rectangle widthBox = { dialogBounds.x + 170.0f, dialogBounds.y + 70.0f, 220.0f, 32.0f };
                Rectangle heightBox = { dialogBounds.x + 170.0f, dialogBounds.y + 120.0f, 220.0f, 32.0f };
                Rectangle widthLabel = { dialogBounds.x + 25.0f, dialogBounds.y + 72.0f, 130.0f, 30.0f };
                Rectangle heightLabel = { dialogBounds.x + 25.0f, dialogBounds.y + 122.0f, 130.0f, 30.0f };

                DrawRectangle(0, 0, (int)screenW, (int)screenH, Fade(BLACK, 0.65f));
                GuiSetStyle(DEFAULT, BACKGROUND_COLOR, 0x1e1e1cCF);
                GuiPanel(dialogBounds, "Export Image Dimensions");
                GuiLabel(widthLabel, "Width (pixels)");
                GuiLabel(heightLabel, "Height (pixels)");

                if (CheckCollisionPointRec(mousePos, widthBox) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    exportActiveTextBox = 0;
                } else if (CheckCollisionPointRec(mousePos, heightBox) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    exportActiveTextBox = 1;
                }

                int widthResult = GuiTextBox(widthBox, exportWidthBuffer, sizeof(exportWidthBuffer), exportActiveTextBox == 0);
                int heightResult = GuiTextBox(heightBox, exportHeightBuffer, sizeof(exportHeightBuffer), exportActiveTextBox == 1);
                if (widthResult == 1) {
                    exportActiveTextBox = 1;
                }
                if (heightResult == 1) {
                    exportConfirmed = true;
                }

                Rectangle exportConfirmBounds = { dialogBounds.x + 100.0f, dialogBounds.y + 205.0f, 110.0f, 36.0f };
                Rectangle exportCancelBounds = { dialogBounds.x + 230.0f, dialogBounds.y + 205.0f, 110.0f, 36.0f };
                if (exportDimensionsInvalid) {
                    DrawTextEx(font, "Use whole numbers from 64 to 16384.", Vector2{ dialogBounds.x + 25, dialogBounds.y + 175 }, 10, 2.0f, RED);
                }
                exportConfirmed = GuiButton(exportConfirmBounds, "Export") == 1 || exportConfirmed;
                exportCancelled = GuiButton(exportCancelBounds, "Cancel") == 1 || IsKeyPressed(KEY_ESCAPE);
            }

        EndDrawing();

        if (updateColorShader) {
            SetColorShaderUniforms(colorShader,
                                   locPaletteShift, locPaletteScale, locPaletteMode,
                                   locIBri, locIBriScale, locIBriBase,
                                   locStripeLift, locStripeContrast, locStripeGamma,
                                   paletteShift, paletteScale, paletteMode,
                                   iBri, iBriScale, iBriBase,
                                   stripeLift, stripeContrast, stripeGamma);
            updateColorShader = false;
        }

        if (exportRequested) {
            exportDialogOpen = true;
            exportDimensionsInvalid = false;
            exportActiveTextBox = 0;
        }

        if (exportCancelled) {
            exportDialogOpen = false;
            exportDimensionsInvalid = false;
            exportActiveTextBox = -1;
        }

        if (saveSettingsRequested) {
            const char *filterPatterns[1] = { "*.mset" };
            const char *filePath = tinyfd_saveFileDialog(
                "Save Mandelbrot Settings",
                "mandelbrot_settings.mset",
                1,
                filterPatterns,
                "Mandelbrot Settings (*.mset)"
            );
            if (filePath != NULL && !SaveSettings(filePath, variableRegistry,
                                                   maxIterationsMultiplier, maxSamples)) {
                TraceLog(LOG_WARNING, "MANDELBROT: Could not save settings to %s.", filePath);
            }
        }

        if (loadSettingsRequested) {
            const char *filterPatterns[1] = { "*.mset" };
            const char *filePath = tinyfd_openFileDialog(
                "Load Mandelbrot Settings",
                "",
                1,
                filterPatterns,
                "Mandelbrot Settings (*.mset)",
                0
            );
            if (filePath != NULL) {
                if (LoadSettings(filePath, variableRegistry, viewPort, maxIterationsMultiplier, maxSamples)) {
                    updateColorShader = true;
                    updateComputeShader = true;
                    frameCount = 0;
                } else {
                    TraceLog(LOG_WARNING, "MANDELBROT: Could not load settings from %s.", filePath);
                }
            }
        }

        if (exportConfirmed) {
            IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE) ? ToggleBorderlessWindowed() : void();
            char *widthEnd = nullptr;
            char *heightEnd = nullptr;
            long exportWidth = std::strtol(exportWidthBuffer, &widthEnd, 10);
            long exportHeight = std::strtol(exportHeightBuffer, &heightEnd, 10);
            bool validDimensions = widthEnd != exportWidthBuffer && *widthEnd == '\0'
                && heightEnd != exportHeightBuffer && *heightEnd == '\0'
                && exportWidth >= 64 && exportWidth <= 16384
                && exportHeight >= 64 && exportHeight <= 16384;

            if (!validDimensions) {
                exportDimensionsInvalid = true;
                TraceLog(LOG_WARNING, "MANDELBROT: Export dimensions must be whole numbers from 64 to 16384.");
            } else {
                exportDialogOpen = false;
                exportDimensionsInvalid = false;
                exportActiveTextBox = -1;

            // Keep the export parameters in sync even when the viewport is fully accumulated.
            shaderParams.zoom = viewPort.zoom;
            shaderParams.seedX = viewPort.seed.x;
            shaderParams.seedY = viewPort.seed.y;
            shaderParams.rotation = viewPort.rotation;
            shaderParams.maxIterations = maxIterations;
            shaderParams.scaleX = scaleX;
            shaderParams.scaleY = scaleY;
            shaderParams.lightHeight = lightHeight;
            shaderParams.stripeFreq = stripeFreq;
            shaderParams.stripeOffset = stripeOffset;
            shaderParams.escapeRadius = escapeRadius;
            shaderParams.derivativeEscapeRadius = derivativeEscapeRadius;

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, shaderParamsSSBO);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(shaderParams), &shaderParams);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

            const char *filterPatterns[1] = { "*.png" };
            const char *filePath = tinyfd_saveFileDialog(
                "Save High-Res Mandelbrot",
                "mandelbrot_render.png",
                1,
                filterPatterns,
                "PNG Image (*.png)"
            );

            if (filePath != NULL) {
                TraceLog(LOG_INFO, "User selected path: %s", filePath);
                ExportHighResMandelbrot(filePath, (int)exportWidth, (int)exportHeight, 4,
                                        computeShader, colorShader, locFrameCount,
                                        locHistoryTex, shaderParams, shaderParamsSSBO);
            }
            }
        }
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    UnloadShader(computeShader);         // Unload compute shader
    UnloadShader(colorShader);           // Unload color shader

    UnloadRenderTexture(dataTransport);  // Unload render texture
    UnloadRenderTexture(accumBufferA);   // Unload multisampling buffer A
    UnloadRenderTexture(accumBufferB);   // Unload multisampling buffer B

    UnloadFont(font);                    // Unload font

    CloseWindow();                       // Close window and OpenGL context
    //--------------------------------------------------------------------------------------
    return 0;
}