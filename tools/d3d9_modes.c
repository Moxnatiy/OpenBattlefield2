// Найменша можлива перевірка: скільки режимів екрана бачить D3D9 у пляшці.
// Саме цього списку бракує BF2 — він каже «Unknown DynamicOption value»
// геть на все, навіть на «Off».
#include <windows.h>
#include <stdio.h>

typedef struct { UINT Width, Height, RefreshRate; DWORD Format; } MODE;
typedef void* (WINAPI *CREATE9)(UINT);

int main(void) {
    HMODULE lib = LoadLibraryA("d3d9.dll");
    if (!lib) { printf("d3d9.dll не завантажилась: %lu\n", GetLastError()); return 1; }
    CREATE9 create = (CREATE9)GetProcAddress(lib, "Direct3DCreate9");
    if (!create) { printf("немає Direct3DCreate9\n"); return 1; }
    void* d3d = create(32);           // D3D_SDK_VERSION
    if (!d3d) { printf("Direct3DCreate9 повернув NULL\n"); return 1; }

    void** vt = *(void***)d3d;
    UINT (WINAPI *adapterCount)(void*) = (void*)vt[4];
    UINT (WINAPI *modeCount)(void*, UINT, DWORD) = (void*)vt[6];
    HRESULT (WINAPI *enumModes)(void*, UINT, DWORD, UINT, MODE*) = (void*)vt[7];

    printf("адаптерів: %u\n", adapterCount(d3d));
    // D3DFMT_X8R8G8B8 = 22, R5G6B5 = 23, A8R8G8B8 = 21
    DWORD formats[] = {22, 23, 21};
    for (int f = 0; f < 3; ++f) {
        UINT n = modeCount(d3d, 0, formats[f]);
        printf("формат %lu: режимів %u\n", formats[f], n);
        for (UINT i = 0; i < n; ++i) {
            MODE m; 
            if (enumModes(d3d, 0, formats[f], i, &m) == 0)
                printf("   %ux%u @%uHz\n", m.Width, m.Height, m.RefreshRate);
        }
    }
    return 0;
}
