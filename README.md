### Stria Fractalizer
is a Mndelbrot set renderer designed to support the deep zoom capabilities of perturbation theory, the beauty of the stripe coloring method, and the speed of modern GLSL renderers

Currently I have implemented the last two and am working on perturbation theory on another branch.

I should also note that while this project is NOT vibe coded. I have never made a perturbation renderer before, so extensive LLM research and code writing was needed.

# Raylib Instructions

Both Raylib and RayGui must be in a folder named "Raylib" right next to the folder of the project

Also, while it isn't necessary, replace DrawGuiRectangle() at line 4935 with:

```
static void GuiDrawRectangle(Rectangle rec, int borderWidth, Color borderColor, Color color)
{
#if defined(RAYGUI_CUSTOM_ROUNDED_RECTS)
    // Desired corner radius in absolute pixels
    float targetRadius = 12.0f; 
    
    // Find the smaller side to ensure the radius fits
    float smallerDimension = (rec.width < rec.height) ? rec.width : rec.height;
    
    // Convert absolute pixel radius to raylib's normalized roundness (0.0f to 1.0f)
    float roundness = 0.0f;
    if (smallerDimension > 0.0f)
    {
        roundness = (targetRadius * 2.0f) / smallerDimension;
        if (roundness > 1.0f) roundness = 1.0f;
    }
    
    int segments = 16;

    if (color.a > 0)
    {
        DrawRectangleRounded(rec, roundness, segments, GuiFade(color, guiAlpha));
    }

    if (borderWidth > 0)
    {
        DrawRectangleRoundedLines(rec, roundness, segments, (float)borderWidth, GuiFade(borderColor, guiAlpha));
    }
#else
    // Default standard raygui sharp rectangles
    if (color.a > 0)
    {
        DrawRectangle((int)rec.x, (int)rec.y, (int)rec.width, (int)rec.height, GuiFade(color, guiAlpha));
    }

    if (borderWidth > 0)
    {
        DrawRectangle((int)rec.x, (int)rec.y, (int)rec.width, borderWidth, GuiFade(borderColor, guiAlpha));
        DrawRectangle((int)rec.x, (int)rec.y + borderWidth, borderWidth, (int)rec.height - 2*borderWidth, GuiFade(borderColor, guiAlpha));
        DrawRectangle((int)rec.x + (int)rec.width - borderWidth, (int)rec.y + borderWidth, borderWidth, (int)rec.height - 2*borderWidth, GuiFade(borderColor, guiAlpha));
        DrawRectangle((int)rec.x, (int)rec.y + (int)rec.height - borderWidth, (int)rec.width, borderWidth, GuiFade(borderColor, guiAlpha));
    }
#endif

#if defined(RAYGUI_DEBUG_RECS_BOUNDS)
    DrawRectangle((int)rec.x, (int)rec.y, (int)rec.width, (int)rec.height, Fade(RED, 0.4f));
#endif
}
```
