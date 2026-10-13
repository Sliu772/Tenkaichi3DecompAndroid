/*
 * iOS: what gs_gl.c (the OpenGL ES renderer of the Android build) provides and the rest of the engine still names.
 * gs_gl.c is not built for iOS: the renderer is SDL_GPU only (gs_gpu.c). The file is empty on every other platform.
 */
typedef int gs_nogl_unit; /* (a file with no declaration is a warning) */

#ifdef __APPLE__
/* gs_draw.c / gs_gpu.c: 1 when the renderer cannot take BC1-BC3 textures. gs_gpu.c sets it from the device. */
int gGsNoBcTextures;

/* ui.cpp: the name strips as GL textures; only the OpenGL back end asks (ui.cpp's sGL), which never runs here. */
unsigned GsGl_StripTexture(const void *rgba, int w, int h) {
    (void)rgba;
    (void)w;
    (void)h;
    return 0;
}

void GsGl_StripTextureFree(unsigned id) {
    (void)id;
}

/* gs_draw.c names the OpenGL back end (GsBackend sGlBackend, gs_draw.h) to choose between the two and as the one to try
   when the other cannot start. Here it is a back end that never starts: the layout is GsBackend's (a name and 15 function
   pointers), `init` returns 0, so a failing SDL_GPU ends in "could not start" and not in a jump through a null pointer. */
static int nogl_init(void) {
    return 0;
}

struct {
    const char *name;
    int (*init)(void);
    void *rest[15];
} sGlBackend = {"opengl (not built for iOS)", nogl_init, {0}};
#endif
