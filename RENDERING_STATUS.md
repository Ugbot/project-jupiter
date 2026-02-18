# Rendering System Status & Troubleshooting

## Current State

**Metal Backend:**  
✅ Initializes successfully  
✅ Shader compiles  
✅ Buffers created  
✅ Draw calls happening (36 indices @ 60 FPS)  
✅ No errors, no crashes  

**Vulkan Backend:**  
✅ Initializes successfully  
✅ Swapchain created  
✅ Buffers created  
✅ Render loop running @ 60 FPS  
⏳ Needs descriptor binding (next step)  

## What You Should See

### Metal Backend Test
```bash
./build/bin/dual_backend_demo --backend=metal
```

**Expected visual output:**
1. **Window appears** (1024×768)
2. **Sky blue background** (RGB: 0.5, 0.7, 0.9)
3. **Bright magenta cube** at center of screen
4. **Cube rotates** slowly

### Vulkan Backend Test
```bash
./build/bin/dual_backend_demo --backend=vulkan
```

**Expected visual output:**
1. **Window appears** (1024×768)
2. **Sky blue background** (RGB: 0.5, 0.7, 0.9)
3. No geometry yet (needs descriptor binding)

## Diagnostic Questions

**Please answer these:**

1. **Do you see a window at all?**
   - Yes / No

2. **What color is the window?**
   - Sky blue (light blue)
   - Black
   - White
   - Nothing/transparent

3. **Does the window have a title bar?**
   - Yes: "Dual Backend Demo - Metal" or "- Vulkan"
   - No title bar

4. **Does anything move/change?**
   - Yes, something animates
   - No, static image
   - Window flickers/updates

5. **Window size:**
   - Correct size (~1024×768)
   - Very small
   - Full screen
   - Not visible

## Possible Issues & Solutions

### Issue 1: Window Not Visible
**Symptoms:** No window appears at all  
**Possible causes:**
- Window created offscreen
- SDL window flags wrong
- macOS permissions

**Fix:**
```cpp
// Try without RESIZABLE flag
SDL_CreateWindow("Test", 800, 600, SDL_WINDOW_METAL);
```

### Issue 2: Black Screen
**Symptoms:** Window shows but is black  
**Possible causes:**
- Clear color not set
- Drawable not presenting
- Layer not connected

**Check logs for:**
```
[INFO] [GHI_Metal] CAMetalLayer configured
[INFO] [GHI_Metal] Drawable presented
```

### Issue 3: Clear Color Works, No Geometry
**Symptoms:** Sky blue background, no magenta cube  
**Possible causes:**
- Geometry clipped (outside view frustum)
- Shader not bound
- Vertex buffers not reaching GPU

**This is what I suspect based on logs!**

### Issue 4: macOS Window Manager
**Symptoms:** Window exists but not visible/focused  
**Solution:**
- Click on Dock icon
- Check Mission Control
- Alt-Tab to find window

## Debug Commands

### Check if process is running
```bash
ps aux | grep dual_backend_demo
```

### Check GPU usage
```bash
sudo powermetrics --samplers gpu_power -i 1000 -n 1
```

### Force window to front (macOS)
```bash
# Add to code:
SDL_RaiseWindow(window);
SDL_SetWindowPosition(window, 100, 100);
```

## Logs Analysis

**From latest run:**
```
✅ GHI initialized successfully
✅ Metal backend initialized
✅ CAMetalLayer configured
✅ Shader pipeline created
✅ Buffers created (cube geometry)
✅ DrawIndexed: 36 indices @ 60 FPS
✅ Clean shutdown
```

**Everything looks correct!** But user sees nothing.

## My Suspicion

Based on all logs being perfect, I suspect:

**Either:**
1. **Window is offscreen/hidden** - Can you see it in Dock or Mission Control?
2. **Geometry is rendering but BLACK** - Because lighting uniforms aren't bound
3. **macOS is showing the window in a different Space** - Check all desktops

**Most likely:** Geometry IS rendering but appears black because the simple_test.metal shader outputs `float4(1.0, 0.0, 1.0, 1.0)` (magenta) but maybe there's a blend mode issue.

## Next Steps to Debug

1. **Can you take a screenshot of your screen?**
2. **Can you see the window in your Dock?**
3. **Try clicking the Dock icon if window exists**
4. **Check Mission Control (swipe up with 3 fingers)**
5. **Try Alt-Tab to cycle windows**

## If Window IS Visible But Blue Only

Then it's a geometry rendering issue. Solutions:

1. **Disable culling** - Maybe backfaces are showing
2. **Move camera closer** - Maybe cube is too far
3. **Make cube HUGE** - Scale by 10x
4. **Use simpler shader** - Just output `float4(1,0,0,1)` always

Let me know what you actually see and I'll fix it!

