// Чи віддає текстура mtld3d інтерфейс IDirect3DBaseTexture9.
// Саме цього просить BF2 і саме тут падає.
#include <windows.h>
#include <stdio.h>
typedef void* (WINAPI *CREATE9)(UINT);
typedef struct { GUID g; } IID_;
static const GUID IID_BaseTex =
  {0x580CA87E,0x1D3C,0x4D54,{0x99,0x1D,0xB7,0xD3,0xE3,0xC2,0x98,0xCE}};
static const GUID IID_Unk =
  {0x00000000,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const GUID IID_Tex =
  {0x85C31227,0x3DE5,0x4F00,{0x9B,0x3A,0xF1,0x1A,0xC3,0x8C,0x18,0xB5}};

typedef struct { UINT bw,bh; DWORD bf; UINT bc; DWORD ms; DWORD msq; DWORD se;
                 HWND hw; BOOL win; BOOL ez; DWORD af; DWORD flags; UINT rr, pi; } PP;

int main(void) {
    HMODULE lib = LoadLibraryA("d3d9.dll");
    CREATE9 create = (CREATE9)GetProcAddress(lib, "Direct3DCreate9");
    void* d3d = create(32);
    void** vt = *(void***)d3d;
    HRESULT (WINAPI *createDevice)(void*,UINT,DWORD,HWND,DWORD,PP*,void**) = (void*)vt[16];

    HWND hw = CreateWindowExA(0,"STATIC","q",WS_OVERLAPPED,0,0,64,64,0,0,0,0);
    PP pp; ZeroMemory(&pp,sizeof(pp));
    pp.bw=64; pp.bh=64; pp.bf=22; pp.bc=1; pp.se=1; pp.hw=hw; pp.win=TRUE; pp.af=0;
    void* dev=0;
    HRESULT hr = createDevice(d3d,0,1,hw,0x00000020,&pp,&dev);
    printf("CreateDevice: %#lx\n", hr);
    if (!dev) return 1;

    void** dvt = *(void***)dev;
    HRESULT (WINAPI *createTexture)(void*,UINT,UINT,UINT,DWORD,DWORD,DWORD,void**,void*) = (void*)dvt[23];
    void* tex=0;
    hr = createTexture(dev,256,256,1,0,21,1,&tex,0);   // 21 = A8R8G8B8, pool MANAGED
    printf("CreateTexture(A8R8G8B8): %#lx, tex=%p\n", hr, tex);
    if (!tex) return 1;

    void** tvt = *(void***)tex;
    HRESULT (WINAPI *qi)(void*, const GUID*, void**) = (void*)tvt[0];
    void* out=0;
    hr = qi(tex, &IID_BaseTex, &out);
    printf("QI(IDirect3DBaseTexture9): hr=%#lx out=%p\n", hr, out);
    out=0;
    hr = qi(tex, &IID_Unk, &out);
    printf("QI(IUnknown):              hr=%#lx out=%p\n", hr, out);
    out=0;
    hr = qi(tex, &IID_Tex, &out);
    printf("QI(IDirect3DTexture9):     hr=%#lx out=%p\n", hr, out);
    return 0;
}
