# NUTELLA-SHADER-SRC
# nutella — OpenGL post-process shader for BlueStacks

A Windows DLL plus a Python control panel that injects a live, tunable
post-processing shader into BlueStacks (`HD-Player.exe`). Every frame the
emulator draws gets redirected through a fragment shader before hitting the
screen. Sliders update in real time — no restart, no re-inject.

## What's in this repo

|
 File                 
|
 What it is                                                
|
|
----------------------
|
-----------------------------------------------------------
|
|
`dllmain.cpp`
|
 Source of the injected DLL — MinHook hooks + GL pipeline. 
|
|
`control.py`
|
 PyQt6 injector GUI. Writes to shared memory every tick.   
|
|
`nutella_shader.dll`
|
 Prebuilt DLL the injector loads into BlueStacks.          
|
## How it works

1. `control.py` finds `HD-Player.exe`, allocates memory in it via
   `VirtualAllocEx`, writes the DLL path, and runs
   `CreateRemoteThread(LoadLibraryA)` — standard Windows DLL injection.
2. Inside the target, `DllMain` boots and installs two MinHook detours:
   `opengl32.dll!wglSwapBuffers` and `libEGL.dll!eglSwapBuffers`. Whichever
   one the emulator actually uses gets frames; the other stays dormant.
3. On every swap, the current framebuffer is blitted into an offscreen FBO,
   then a full-screen quad is drawn back with a fragment shader that reads
   12 live parameters.
4. Those parameters live in a named shared-memory section
   (`Local\NutellaParams`, 48 bytes, `struct.pack("<10f2i", ...)` — 10
   floats + 2 ints). The Python side writes; the C++ side reads each frame.
   No pipes, no sockets, no polling.

## Tunable parameters

|
 Slot 
|
 Float field   
|
 Int field    
|
|
------
|
---------------
|
--------------
|
|
 1    
|
 saturation    
|
`enabled`
|
|
 2    
|
 contrast      
|
`effect`
|
|
 3    
|
 brightness    
|
|
|
 4    
|
 shadow        
|
|
|
 5    
|
 warmth        
|
|
|
 6    
|
 bloom         
|
|
|
 7    
|
 lift          
|
|
|
 8    
|
 hdr           
|
|
|
 9    
|
 ambient       
|
|
|
 10   
|
 vignette      
|
|
## Build the DLL from source

You need MSYS2 UCRT64 with `gcc` and a copy of
[MinHook](https://github.com/tsa9x1/MinHook) sitting in a `minhook/` folder
next to `dllmain.cpp`. Then:

```bat
C:\msys64\ucrt64\bin\g++.exe -shared -O2 -static-libgcc -static-libstdc++ ^
  -I minhook\include ^
  -o nutella_shader.dll ^
  dllmain.cpp ^
  minhook\src\hook.c ^
  minhook\src\trampoline.c ^
  minhook\src\buffer.c ^
  minhook\src\hde\hde64.c ^
  -lkernel32 -luser32 -lopengl32 -lgdi32
```

The output filename must be `nutella_shader.dll` — `control.py` looks it up
by that name in the same folder.

## Run

```
pip install PyQt6
python control.py
```

Run as Administrator. Start BlueStacks first, then hit inject in the panel.
Adjust the sliders — the emulator updates on the next frame.

## Notes

- Log line `nutella: shader compiled + linked OK` in `shader_log.txt` means
  the pipeline is live.
- Both swap-buffer hooks are installed at attach time. Only one will actually
  receive frames depending on how BlueStacks was launched (ANGLE vs native
  GL). The log tells you which.
- Antivirus may flag the injector. It's doing exactly what it says on the
  tin; add a directory exclusion while testing.
- Not affiliated with BlueStacks. Personal-use / research project.

## License

MIT for `dllmain.cpp` and `control.py`. MinHook is upstream under its own
BSD-2 licence.
