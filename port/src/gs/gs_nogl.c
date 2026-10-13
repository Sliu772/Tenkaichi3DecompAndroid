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
#endif
