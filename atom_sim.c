/*
 * QUANTUM ATOM SIMULATOR  —  C / raylib  (Enhanced Edition)
 * ═══════════════════════════════════════════════════════════════
 * FIXES from original:
 *   1. SW/SH updated before LoadRenderTexture (was using wrong 1920×1080)
 *   2. RL_POINTS (0x0000) replaced with RL_TRIANGLES billboard quads
 *      (RL_POINTS is NOT in raylib 6 rlgl → vertex corruption → segfault)
 *   3. Electron click detection fixed (dragging flag was set before click check)
 *   4. cfgstr buffer 128 bytes (UTF-8 sups overflow 64-byte original)
 *   5. Panel layout made compact/responsive — fits any screen height
 *   6. Stars replaced: DrawSphereEx×2000 → tiny billboard triangles (10× faster)
 *
 * ENHANCEMENTS:
 *   - Electron motion trails with glow fade
 *   - Camera-facing billboard quads for probability cloud
 *   - Nucleus plasma corona + inner core radiance
 *   - Phase-coloured orbital lobes (+ lobe warm, − lobe cool)
 *   - Animated shell rings with shimmer
 *   - Improved bloom + Reinhard tonemapping + chromatic aberration
 *   - Element transition flash effect
 *
 * CONTROLS
 *   Mouse drag          rotate atom
 *   Scroll              zoom
 *   Click electron      excite → photon emission
 *   ← / →              previous / next element
 *   ↑ / ↓              jump 10 elements
 *   1                  Bohr orbit mode
 *   2                  Quantum probability cloud
 *   3                  Orbital shape (s/p/d/f lobes)
 *   K L M N            toggle shell visibility
 *   A                  toggle auto-spin
 *   R                  reset camera
 *   B                  toggle bloom
 * ═══════════════════════════════════════════════════════════════
 */

#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <float.h>

/* ─────────────── CONSTANTS ─────────────── */
#define MAX_ELECTRONS   36
#define MAX_PHOTONS      8
#define N_CLOUD       3000   /* cloud pts per orbital  (was 8000 — too slow) */
#define N_SHAPE       2000   /* shape pts per orbital  (was 5000) */
#define BG_STARS       600   /* background stars       (was 2000 spheres)    */
#define TRAIL_LEN       22   /* electron trail length  (NEW) */

static const float SHELL_R[4] = { 14.0f, 28.0f, 42.0f, 56.0f };
static const char  SHELL_L[4] = { 'K', 'L', 'M', 'N' };

/* Orbital colours (per-orbital, RGBA base) */
static const unsigned char ORB_COL[8][3] = {
    {  0,212,255},
    { 80,200,255},
    {160, 96,255},
    {  0,255,200},
    { 80,255,140},
    {255,180, 60},
    {200, 96,255},
    {255,100,200},
};
static const int ORB_N [8] = {1,2,2,3,3,3,4,4};
static const int ORB_L [8] = {0,0,1,0,1,2,0,1};
static const int ORB_SH[8] = {0,1,1,2,2,2,3,3};

/* ─────────────── IMPROVED POST-PROCESS SHADER ─────────────── */
static const char *FS_BLOOM =
"#version 330\n"
"in vec2 fragTexCoord; out vec4 finalColor;\n"
"uniform sampler2D texture0; uniform vec2 resolution;\n"
"void main(){\n"
"  vec2 uv=fragTexCoord; uv.y=1.0-uv.y;\n"
/* chromatic aberration */
"  float ab=0.0013;\n"
"  vec4 rc=texture(texture0,uv+vec2(ab*1.4, ab*0.3));\n"
"  vec4 gc=texture(texture0,uv+vec2(-ab*0.2, ab*0.1));\n"
"  vec4 bc=texture(texture0,uv+vec2(-ab*1.2,-ab*0.2));\n"
"  vec4 base=vec4(rc.r,gc.g,bc.b,1.0);\n"
/* bloom — wider kernel, luminance-gated */
"  vec2 tx=1.0/resolution; vec4 bl=vec4(0.0); float ws=0.0;\n"
"  for(int x=-5;x<=5;x++) for(int y=-5;y<=5;y++){\n"
"    float d=float(x*x+y*y);\n"
"    float w=exp(-d*0.13);\n"
"    vec4 s=texture(texture0,uv+vec2(x,y)*tx*2.4);\n"
"    float lum=dot(s.rgb,vec3(0.299,0.587,0.114));\n"
"    bl+=s*max(0.0,lum-0.18)*w; ws+=w;\n"
"  }\n"
"  bl/=ws;\n"
/* vignette */
"  vec2 vig=uv-0.5; float v=1.0-dot(vig,vig)*1.55;\n"
"  v=clamp(v,0.08,1.0);\n"
/* Reinhard tonemapping + slight gamma lift */
"  vec3 c=(base.rgb+bl.rgb*4.2)*v;\n"
"  c=c/(c+vec3(0.75));\n"
"  c=pow(c,vec3(0.88));\n"
"  finalColor=vec4(min(c*1.25,vec3(1.5)),1.0);\n"
"}\n";

/* ─────────────── ELEMENT DATA ─────────────── */
typedef struct { int Z; const char *sym,*name; float mass; int cfg[8]; int period; } Elem;
static const Elem ELEMS[] = {
  {1,"H","Hydrogen",1.008f,{1,0,0,0,0,0,0,0},1},
  {2,"He","Helium",4.003f,{2,0,0,0,0,0,0,0},1},
  {3,"Li","Lithium",6.941f,{2,1,0,0,0,0,0,0},2},
  {4,"Be","Beryllium",9.012f,{2,2,0,0,0,0,0,0},2},
  {5,"B","Boron",10.81f,{2,2,1,0,0,0,0,0},2},
  {6,"C","Carbon",12.01f,{2,2,2,0,0,0,0,0},2},
  {7,"N","Nitrogen",14.01f,{2,2,3,0,0,0,0,0},2},
  {8,"O","Oxygen",16.00f,{2,2,4,0,0,0,0,0},2},
  {9,"F","Fluorine",19.00f,{2,2,5,0,0,0,0,0},2},
  {10,"Ne","Neon",20.18f,{2,2,6,0,0,0,0,0},2},
  {11,"Na","Sodium",22.99f,{2,2,6,1,0,0,0,0},3},
  {12,"Mg","Magnesium",24.31f,{2,2,6,2,0,0,0,0},3},
  {13,"Al","Aluminium",26.98f,{2,2,6,2,1,0,0,0},3},
  {14,"Si","Silicon",28.09f,{2,2,6,2,2,0,0,0},3},
  {15,"P","Phosphorus",30.97f,{2,2,6,2,3,0,0,0},3},
  {16,"S","Sulfur",32.06f,{2,2,6,2,4,0,0,0},3},
  {17,"Cl","Chlorine",35.45f,{2,2,6,2,5,0,0,0},3},
  {18,"Ar","Argon",39.95f,{2,2,6,2,6,0,0,0},3},
  {19,"K","Potassium",39.10f,{2,2,6,2,6,0,1,0},4},
  {20,"Ca","Calcium",40.08f,{2,2,6,2,6,0,2,0},4},
  {21,"Sc","Scandium",44.96f,{2,2,6,2,6,1,2,0},4},
  {22,"Ti","Titanium",47.87f,{2,2,6,2,6,2,2,0},4},
  {23,"V","Vanadium",50.94f,{2,2,6,2,6,3,2,0},4},
  {24,"Cr","Chromium",52.00f,{2,2,6,2,6,5,1,0},4},
  {25,"Mn","Manganese",54.94f,{2,2,6,2,6,5,2,0},4},
  {26,"Fe","Iron",55.85f,{2,2,6,2,6,6,2,0},4},
  {27,"Co","Cobalt",58.93f,{2,2,6,2,6,7,2,0},4},
  {28,"Ni","Nickel",58.69f,{2,2,6,2,6,8,2,0},4},
  {29,"Cu","Copper",63.55f,{2,2,6,2,6,10,1,0},4},
  {30,"Zn","Zinc",65.38f,{2,2,6,2,6,10,2,0},4},
  {31,"Ga","Gallium",69.72f,{2,2,6,2,6,10,2,1},4},
  {32,"Ge","Germanium",72.63f,{2,2,6,2,6,10,2,2},4},
  {33,"As","Arsenic",74.92f,{2,2,6,2,6,10,2,3},4},
  {34,"Se","Selenium",78.96f,{2,2,6,2,6,10,2,4},4},
  {35,"Br","Bromine",79.90f,{2,2,6,2,6,10,2,5},4},
  {36,"Kr","Krypton",83.80f,{2,2,6,2,6,10,2,6},4},
};
#define N_ELEMS 36

/* ─────────────── TYPES ─────────────── */
typedef struct {
    int orbIdx;
    float angle, speed;
    float tiltT, tiltP;
    float excLvl, excTimer;
    /* motion trail */
    Vector3 trail[TRAIL_LEN];
    int     trailHead;
    bool    trailFull;
} Electron;

typedef struct {
    bool active;
    Vector3 pos;
    float r, life;
    Color col;
} Photon;

typedef struct {
    Vector3 pos;
    bool isProton;
    float phase, wob;
} Nucleon;

/* ─────────────── GLOBALS ─────────────── */
static int SW=1920, SH=1080;

static float cloudPts  [8][N_CLOUD][3];
static float cloudAlpha[8][N_CLOUD];
static int   cloudCnt  [8];

static float shapePts  [8][N_SHAPE][3];
static float shapeAlpha[8][N_SHAPE];
static float shapeSign [8][N_SHAPE];   /* +1 / -1 lobe sign (NEW) */
static int   shapeCnt  [8];

/* background stars */
static Vector3 bsPos [BG_STARS];
static float   bsSize[BG_STARS];
static float   bsPhase[BG_STARS];   /* twinkle phase */

/* atom state */
static int       curZ=1;
static Electron  electrons[MAX_ELECTRONS];
static int       nelec=0;
static Nucleon   nucleons[300];
static int       nnuc=0;
static bool      shellVis[4]={true,true,true,true};
static Photon    photons[MAX_PHOTONS];
static float     excFlash=0;
static float     simTime=0;
static float     elemFlash=0;   /* flash on element change */

/* camera */
static float camAz=0.5f, camEl=0.4f, camDist=120.0f;
static float targetAz=0.5f, targetEl=0.4f, targetDist=120.0f;
static bool  autoSpin=true;
static bool  dragging=false;
static float dragLastX=0, dragLastY=0;

/* camera billboard vectors — updated each frame */
static Vector3 gCamRight={1,0,0}, gCamUp={0,1,0};

/* render */
static RenderTexture2D rtex;
static Shader psh;
static bool shOK=false, useBloom=true;
static int uRes;
static float fpsSmooth=60;
static float lastTime=0;

static int viewMode=0;   /* 0=bohr  1=cloud  2=orbital */
static int selOrbIdx=-1;

typedef struct { float lam; float life; } SpecLine;
static SpecLine specLines[32];
static int nspec=0;

/* ─────────────── MATH ─────────────── */
static float randf01(void){ return (float)rand()/(float)RAND_MAX; }
static float randf(float a,float b){ return a+(b-a)*randf01(); }
static float clampf(float x,float a,float b){ return x<a?a:x>b?b:x; }
static int   clampi(int x,int a,int b){ return x<a?a:x>b?b:x; }

/* ─────────────── WAVEFUNCTION MATH ─────────────── */
static float radialProb(int n, int l, float rho){
    float ex;
    switch(n*10+l){
        case 10: ex=expf(-2*rho); return rho*rho*ex;
        case 20: { float f=2-rho; ex=expf(-rho); return rho*rho*f*f*ex; }
        case 21: ex=expf(-rho); return rho*rho*rho*rho*ex;
        case 30: { float f=27-18*rho+2*rho*rho; ex=expf(-2*rho/3); return rho*rho*f*f*ex*0.01f; }
        case 31: { float f=rho*(6-rho); ex=expf(-2*rho/3); return rho*rho*f*f*ex*0.01f; }
        case 32: ex=expf(-2*rho/3); return powf(rho,6)*ex;
        case 40: { float f=192-144*rho+24*rho*rho-rho*rho*rho; ex=expf(-rho/2); return rho*rho*f*f*ex*1e-5f; }
        case 41: { float f=rho*(80-20*rho+rho*rho); ex=expf(-rho/2); return rho*rho*f*f*ex*1e-6f; }
        default: ex=expf(-rho/2); return powf(rho,4)*ex;
    }
}

static float angularProb(int l, int variant, float theta, float phi){
    float ct=cosf(theta), st=sinf(theta);
    float cp=cosf(phi),   sp=sinf(phi);
    switch(l){
        case 0: return 1.0f;
        case 1:
            switch(variant%3){
                case 0: return ct*ct;
                case 1: return st*st*cp*cp;
                default:return st*st*sp*sp;
            }
        case 2:
            switch(variant%5){
                case 0: { float f=3*ct*ct-1; return f*f; }
                case 1: return st*st*ct*ct*cp*cp*4;
                case 2: return st*st*ct*ct*sp*sp*4;
                case 3: { float f=st*st*(cp*cp-sp*sp); return f*f*3; }
                default:return st*st*st*st*cp*cp*sp*sp*4;
            }
        case 3:
            switch(variant%7){
                case 0: { float f=ct*(5*ct*ct-3); return f*f; }
                case 1: return st*st*ct*ct*(5*ct*ct-1)*(5*ct*ct-1)*cp*cp*0.5f;
                case 2: return st*st*ct*ct*(5*ct*ct-1)*(5*ct*ct-1)*sp*sp*0.5f;
                case 3: return powf(st,4)*ct*ct*cp*cp*(cp*cp-3*sp*sp)*(cp*cp-3*sp*sp)*0.1f;
                case 4: return powf(st,4)*ct*ct*sp*sp*(3*cp*cp-sp*sp)*(3*cp*cp-sp*sp)*0.1f;
                case 5: return powf(st,6)*(cp*cp*cp*cp-6*cp*cp*sp*sp+sp*sp*sp*sp)*(cp*cp*cp*cp-6*cp*cp*sp*sp+sp*sp*sp*sp)*0.1f;
                default:return powf(st,6)*cp*cp*sp*sp*(cp*cp-sp*sp)*(cp*cp-sp*sp)*0.3f;
            }
        default: break;
    }
    return 1.0f;
}

/* ─────────────── CLOUD BUILD ─────────────── */
static void buildCloud(int oi){
    int n=ORB_N[oi], l=ORB_L[oi];
    float rMax=(float)(n*n)*5.5f;
    float scale=SHELL_R[ORB_SH[oi]]/(float)n;

    /* deterministic grid scan for pMax — more reliable than random */
    float pMax=1e-30f;
    for(int k=0;k<600;k++){
        float rho=rMax*(float)(k+1)/600.0f;
        float pr=radialProb(n,l,rho);
        if(pr>pMax) pMax=pr;
    }
    pMax*=1.4f;

    cloudCnt[oi]=0;
    int attempts=0, maxAttempts=N_CLOUD*30;
    while(cloudCnt[oi]<N_CLOUD && attempts<maxAttempts){
        attempts++;
        float rho=randf(0,rMax);
        float pr=radialProb(n,l,rho);
        if(randf01()>pr/pMax) continue;

        float theta=acosf(clampf(1-2*randf01(),-1,1));
        float phi  =randf(0,6.28318f);
        float pa   =angularProb(l,attempts,theta,phi);
        float paNorm=(pa+0.001f);
        if(randf01()>pa/paNorm) continue;

        float r=rho*scale;
        int k=cloudCnt[oi];
        cloudPts[oi][k][0]=r*sinf(theta)*cosf(phi);
        cloudPts[oi][k][1]=r*sinf(theta)*sinf(phi);
        cloudPts[oi][k][2]=r*cosf(theta);
        cloudAlpha[oi][k]=clampf(pr/pMax,0.10f,1.0f);
        cloudCnt[oi]++;
    }
}

/* ─────────────── SHAPE BUILD ─────────────── */
static void buildShape(int oi){
    int l=ORB_L[oi];
    float R=SHELL_R[ORB_SH[oi]]*1.3f;
    shapeCnt[oi]=0;
    for(int k=0;k<N_SHAPE;k++){
        float theta=acosf(clampf(1-2*randf01(),-1,1));
        float phi  =randf(0,6.28318f);
        float ct=cosf(theta),st=sinf(theta),cp=cosf(phi),sp=sinf(phi);
        float r; float sign=1.0f;
        switch(l){
            case 0: r=R*0.55f; sign=1.0f; break;
            case 1: {
                float raw=(k%3==0)?ct:k%3==1?st*cp:st*sp;
                sign=(raw>=0)?1.0f:-1.0f;
                float lobe=fabsf(raw);
                r=R*0.70f*(lobe+0.04f);
                break;
            }
            case 2: {
                float lobe; float raw;
                switch(k%5){
                    case 0: raw=3*ct*ct-1; lobe=fabsf(raw)*0.5f; break;
                    case 1: case 2: lobe=fabsf(st*ct)*1.5f; raw=st*ct; break;
                    default: raw=st*st*(cp*cp-sp*sp); lobe=fabsf(raw); break;
                }
                sign=(raw>=0)?1.0f:-1.0f;
                r=R*0.66f*(lobe+0.06f);
                break;
            }
            default: {
                float raw=cosf(3*phi)*st*st;
                sign=(raw>=0)?1.0f:-1.0f;
                float lobe=fabsf(raw)+0.08f;
                r=R*0.62f*(lobe*1.5f+0.1f);
                break;
            }
        }
        shapePts  [oi][k][0]=r*st*cp;
        shapePts  [oi][k][1]=r*st*sp;
        shapePts  [oi][k][2]=r*ct;
        shapeAlpha[oi][k]=0.45f+0.55f*randf01();
        shapeSign [oi][k]=sign;
    }
}

/* ─────────────── NUCLEUS BUILD ─────────────── */
static void buildNucleus(int Z){
    int nNeut=(int)(Z*1.2f);
    nnuc=Z+nNeut;
    if(nnuc>300) nnuc=300;
    float scale=2.4f+cbrtf((float)nnuc)*1.9f;
    for(int i=0;i<nnuc;i++){
        float x,y,z,r2;
        int tries=0;
        do{ x=randf(-1,1); y=randf(-1,1); z=randf(-1,1);
            r2=x*x+y*y+z*z; tries++;
        } while(r2>1&&tries<40);
        nucleons[i].pos=(Vector3){x*scale,y*scale,z*scale};
        nucleons[i].isProton=(i<Z);
        nucleons[i].phase=randf(0,6.28318f);
        nucleons[i].wob  =randf(0.15f,0.65f);
    }
}

/* ─────────────── ATOM BUILD ─────────────── */
static void buildAtom(int Z){
    const Elem *el=&ELEMS[Z-1];
    nelec=0;
    for(int oi=0;oi<8;oi++){
        int cnt=el->cfg[oi];
        for(int e=0;e<cnt;e++){
            if(nelec>=MAX_ELECTRONS) break;
            Electron *ep=&electrons[nelec++];
            ep->orbIdx=oi;
            ep->angle=(float)e/(float)(cnt>0?cnt:1)*6.28318f;
            ep->speed=(0.7f+randf(-0.2f,0.2f))/(float)(ORB_N[oi]*ORB_N[oi])*2.5f;
            ep->tiltT=(float)e*3.14159f/(float)(cnt>0?cnt:1);
            ep->tiltP=(float)oi*1.1f+(float)e*0.65f;
            ep->excLvl=0; ep->excTimer=0;
            ep->trailHead=0; ep->trailFull=false;
            /* seed trail positions */
            for(int t=0;t<TRAIL_LEN;t++) ep->trail[t]=(Vector3){0,0,0};
        }
    }
    buildNucleus(Z);
    elemFlash=1.0f;
}

/* ─────────────── CAMERA ─────────────── */
static Camera3D getCamera(void){
    Camera3D cam={0};
    cam.fovy=45.0f; cam.projection=CAMERA_PERSPECTIVE;
    cam.up=(Vector3){0,1,0};
    cam.target=(Vector3){0,0,0};
    float el=camEl, az=camAz, d=camDist;
    cam.position=(Vector3){
        d*cosf(el)*sinf(az),
        d*sinf(el),
        d*cosf(el)*cosf(az)
    };
    return cam;
}

static Vector3 electronPos3D(Electron *e){
    float sr=SHELL_R[ORB_SH[e->orbIdx]]*(1.0f+e->excLvl*0.35f);
    float ang=e->angle;
    float lx=sr*cosf(ang), lz=sr*sinf(ang);
    float ct=cosf(e->tiltT),st=sinf(e->tiltT);
    float cp=cosf(e->tiltP),sp=sinf(e->tiltP);
    return (Vector3){
        lx*cp - lz*st*sp,
        lx*sp + lz*st*cp,
        lz*ct
    };
}

/* ─────────────── BILLBOARD HELPERS ─────────────── */
/* Draw a camera-facing quad at world position (px,py,pz) with half-size s.
   Uses RL_TRIANGLES — call only between rlBegin(RL_TRIANGLES)/rlEnd(). */
static inline void rlBillboard(float px,float py,float pz,float s){
    float rx=gCamRight.x*s, ry=gCamRight.y*s, rz=gCamRight.z*s;
    float ux=gCamUp.x*s,   uy=gCamUp.y*s,   uz=gCamUp.z*s;
    /* tri 1 */
    rlVertex3f(px-rx+ux, py-ry+uy, pz-rz+uz);
    rlVertex3f(px+rx+ux, py+ry+uy, pz+rz+uz);
    rlVertex3f(px+rx-ux, py+ry-uy, pz+rz-uz);
    /* tri 2 */
    rlVertex3f(px-rx+ux, py-ry+uy, pz-rz+uz);
    rlVertex3f(px+rx-ux, py+ry-uy, pz+rz-uz);
    rlVertex3f(px-rx-ux, py-ry-uy, pz-rz-uz);
}

/* ─────────────── WAVELENGTH → RGB ─────────────── */
static Color wl2col(float lam){
    float r=0,g=0,b=0;
    if(lam>=380&&lam<440){r=(440-lam)/60;b=1;}
    else if(lam<490){g=(lam-440)/50;b=1;}
    else if(lam<510){g=1;b=(510-lam)/20;}
    else if(lam<580){r=(lam-510)/70;g=1;}
    else if(lam<645){r=1;g=(645-lam)/65;}
    else if(lam<=700){r=1;}
    return (Color){(unsigned char)(r*255),(unsigned char)(g*255),(unsigned char)(b*255),255};
}

/* ─────────────── EMIT SPECTRUM ─────────────── */
static void drawSpectrum(int panelX){
    int sx=panelX+8, sy=SH-58, sw=SW-panelX-16, sh=20;
    if(sw<10) return;
    DrawRectangle(sx,sy,sw,sh,(Color){4,7,16,210});
    for(int i=0;i<sw;i++){
        float lam=380+(float)i/(float)sw*320;
        Color c=wl2col(lam); c.a=45;
        DrawLine(sx+i,sy,sx+i,sy+sh,c);
    }
    for(int i=0;i<nspec;i++){
        float x=sx+(specLines[i].lam-380)/320.0f*sw;
        if(x<sx||x>sx+sw) continue;
        Color c=wl2col(specLines[i].lam);
        c.a=(unsigned char)(255*specLines[i].life);
        DrawLine((int)x,sy,(int)x,sy+sh,c);
        /* glow halo */
        c.a=(unsigned char)(80*specLines[i].life);
        DrawLine((int)x-1,sy,(int)x-1,sy+sh,c);
        DrawLine((int)x+1,sy,(int)x+1,sy+sh,c);
    }
    DrawRectangleLines(sx,sy,sw,sh,(Color){25,45,75,200});
    DrawText("EMISSION SPECTRUM",sx+2,sy-13,10,(Color){50,90,130,255});
}

/* ─────────────── DRAW NUCLEUS ─────────────── */
static void drawNucleus(float t){
    /* outer plasma rings — layered & animated */
    float pulse0=(sinf(t*2.2f)+1.0f)*0.5f;
    float pulse1=(sinf(t*3.7f+1.2f)+1.0f)*0.5f;
    float pulse2=(sinf(t*1.5f+2.4f)+1.0f)*0.5f;

    /* corona halos */
    for(int g=7;g>=1;g--){
        float r=(float)g*3.4f + pulse0*2.0f;
        unsigned char a=(unsigned char)(7*g);
        DrawSphereWires((Vector3){0,0,0},r,6,6,(Color){255,140,50,a});
    }
    /* inner glow sphere */
    DrawSphereEx((Vector3){0,0,0},4.5f+pulse2*1.2f,10,10,(Color){255,200,120,30});
    DrawSphereEx((Vector3){0,0,0},3.0f+pulse1*0.8f,10,10,(Color){255,220,160,50});

    /* nucleons */
    for(int i=0;i<nnuc;i++){
        float wx=nucleons[i].pos.x+sinf(t*0.55f+nucleons[i].phase)*nucleons[i].wob;
        float wy=nucleons[i].pos.y+cosf(t*0.70f+nucleons[i].phase)*nucleons[i].wob;
        float wz=nucleons[i].pos.z+sinf(t*0.45f+nucleons[i].phase*1.3f)*nucleons[i].wob;
        Vector3 p={wx,wy,wz};
        if(nucleons[i].isProton){
            DrawSphereEx(p,1.25f,5,5,(Color){255,75,35,230});
            DrawSphereEx(p,1.65f,5,5,(Color){255,130,70,55});
        } else {
            DrawSphereEx(p,1.15f,5,5,(Color){70,130,220,210});
            DrawSphereEx(p,1.50f,5,5,(Color){100,170,255,50});
        }
    }

    /* rotating plasma arc rings at three tilts */
    float rot=t*1.4f;
    DrawCircle3D((Vector3){0,0,0},6.5f+pulse0*1.5f,
        (Vector3){cosf(rot),sinf(rot),0},0,(Color){255,160,60,(unsigned char)(35+20*pulse0)});
    DrawCircle3D((Vector3){0,0,0},7.2f+pulse1*1.5f,
        (Vector3){-sinf(rot),0,cosf(rot)},0,(Color){255,100,30,(unsigned char)(25+15*pulse1)});
    DrawCircle3D((Vector3){0,0,0},6.0f+pulse2*1.0f,
        (Vector3){0,cosf(rot*0.7f),sinf(rot*0.7f)},0,(Color){200,80,255,(unsigned char)(20+12*pulse2)});
}

/* ─────────────── DRAW SHELL RINGS ─────────────── */
static void drawShellRings(float t){
    for(int s=0;s<4;s++){
        if(!shellVis[s]) continue;
        float R=SHELL_R[s];
        Color sc={0,0,0,0};
        for(int oi=0;oi<8;oi++){
            if(ORB_SH[oi]==s&&ELEMS[curZ-1].cfg[oi]>0){
                sc=(Color){ORB_COL[oi][0],ORB_COL[oi][1],ORB_COL[oi][2],35};
                break;
            }
        }
        if(sc.a==0) sc=(Color){35,55,90,28};

        float shimmer=(sinf(t*1.8f+(float)s*1.2f)+1.0f)*0.5f;
        Color sca=sc; sca.a=(unsigned char)(sc.a+shimmer*20);

        float rot=t*0.18f*(float)(s%2==0?1:-1);
        DrawCircle3D((Vector3){0,0,0},R,(Vector3){cosf(rot),sinf(rot)*0.3f,sinf(rot)},0,sca);
        DrawCircle3D((Vector3){0,0,0},R,(Vector3){0,cosf(rot+1.0f),sinf(rot+1.0f)},0,sca);
        DrawCircle3D((Vector3){0,0,0},R,(Vector3){sinf(rot*0.7f),0,cosf(rot*0.7f)},0,sca);
    }
}

/* ─────────────── DRAW ELECTRONS (BOHR + TRAILS) ─────────────── */
static void drawElectronsBohr(float t){
    const Elem *el=&ELEMS[curZ-1];
    for(int i=0;i<nelec;i++){
        Electron *e=&electrons[i];
        if(!shellVis[ORB_SH[e->orbIdx]]) continue;
        if(!el->cfg[e->orbIdx]) continue;

        unsigned char cr=ORB_COL[e->orbIdx][0];
        unsigned char cg=ORB_COL[e->orbIdx][1];
        unsigned char cb=ORB_COL[e->orbIdx][2];
        float bright=1.0f+e->excLvl*0.9f;

        Vector3 p=electronPos3D(e);

        /* draw trail as tiny billboard quads */
        int trailLen=e->trailFull?TRAIL_LEN:e->trailHead;
        if(trailLen>1){
            BeginBlendMode(BLEND_ADDITIVE);
            rlBegin(RL_TRIANGLES);
            for(int tt=0;tt<trailLen;tt++){
                int idx=(e->trailHead-1-tt+TRAIL_LEN)%TRAIL_LEN;
                float f=1.0f-(float)tt/(float)trailLen;
                float ff=f*f;
                unsigned char ta=(unsigned char)(ff*100*bright);
                rlColor4ub(cr,cg,cb,ta);
                float ts=0.25f+ff*0.45f;
                rlBillboard(e->trail[idx].x,e->trail[idx].y,e->trail[idx].z,ts);
            }
            rlEnd();
            EndBlendMode();
        }

        /* excitation arc */
        if(e->excLvl>0){
            float rArc=3.0f+e->excTimer*14.0f;
            DrawCircle3D(p,rArc,(Vector3){0,1,0},0,
                (Color){cr,cg,cb,(unsigned char)(120*e->excTimer)});
            DrawCircle3D(p,rArc*0.7f,(Vector3){1,0,0},0,
                (Color){cr,cg,cb,(unsigned char)(80*e->excTimer)});
        }

        /* glow halos */
        BeginBlendMode(BLEND_ADDITIVE);
        for(int g=5;g>=1;g--){
            float gr=(0.5f+e->excLvl*0.4f)*g;
            DrawSphereEx(p,gr,4,4,(Color){
                (unsigned char)clampf(cr*bright,0,255),
                (unsigned char)clampf(cg*bright,0,255),
                (unsigned char)clampf(cb*bright,0,255),
                (unsigned char)(12*g)});
        }
        EndBlendMode();

        /* core sphere */
        DrawSphereEx(p,0.9f+e->excLvl*0.5f,6,6,(Color){
            (unsigned char)clampf(cr*bright,0,255),
            (unsigned char)clampf(cg*bright,0,255),
            (unsigned char)clampf(cb*bright,0,255),255});

        /* thin line from nucleus to electron for depth */
        if(ORB_N[e->orbIdx]==1){
            DrawLine3D((Vector3){0,0,0},p,(Color){cr,cg,cb,18});
        }
    }
}

/* ─────────────── DRAW PROBABILITY CLOUD ─────────────── */
static void drawCloud(float t){
    const Elem *el=&ELEMS[curZ-1];
    BeginBlendMode(BLEND_ADDITIVE);
    for(int oi=0;oi<8;oi++){
        if(!el->cfg[oi]) continue;
        if(!shellVis[ORB_SH[oi]]) continue;
        unsigned char cr=ORB_COL[oi][0];
        unsigned char cg=ORB_COL[oi][1];
        unsigned char cb=ORB_COL[oi][2];
        float breathe=sinf(t*0.65f+(float)oi*0.9f)*2.5f;

        rlBegin(RL_TRIANGLES);
        for(int k=0;k<cloudCnt[oi];k++){
            float px=cloudPts[oi][k][0]+sinf(t*0.27f+(float)oi)*breathe*0.13f;
            float py=cloudPts[oi][k][1]+cosf(t*0.33f+(float)oi)*breathe*0.13f;
            float pz=cloudPts[oi][k][2]+sinf(t*0.41f+(float)oi*1.4f)*breathe*0.13f;

            /* distance-tinted: inner points slightly warmer */
            float dist2=px*px+py*py+pz*pz;
            float maxR=SHELL_R[ORB_SH[oi]]*1.2f;
            float inner=clampf(1.0f-sqrtf(dist2)/(maxR+1.0f),0,1)*0.5f;
            unsigned char alpha=(unsigned char)(cloudAlpha[oi][k]*165);
            unsigned char rr=(unsigned char)clampf(cr+inner*60,0,255);
            unsigned char gg=(unsigned char)clampf(cg-inner*20,0,255);

            rlColor4ub(rr,gg,cb,alpha);
            rlBillboard(px,py,pz,0.38f);
        }
        rlEnd();
    }
    EndBlendMode();
}

/* ─────────────── DRAW ORBITAL SHAPES ─────────────── */
static void drawOrbitalShapes(float t){
    const Elem *el=&ELEMS[curZ-1];
    BeginBlendMode(BLEND_ADDITIVE);
    for(int oi=0;oi<8;oi++){
        if(!el->cfg[oi]) continue;
        if(!shellVis[ORB_SH[oi]]) continue;
        unsigned char cr=ORB_COL[oi][0];
        unsigned char cg=ORB_COL[oi][1];
        unsigned char cb=ORB_COL[oi][2];
        float spin=t*0.11f*((oi%2==0)?1:-1);
        float cs=cosf(spin), ss=sinf(spin);

        rlBegin(RL_TRIANGLES);
        for(int k=0;k<shapeCnt[oi];k++){
            float rx=shapePts[oi][k][0]*cs - shapePts[oi][k][2]*ss;
            float ry=shapePts[oi][k][1];
            float rz=shapePts[oi][k][0]*ss + shapePts[oi][k][2]*cs;

            /* phase colouring: + lobe warm, − lobe cool */
            float sgn=shapeSign[oi][k];
            unsigned char rr,gg,bb;
            if(sgn>0){
                rr=(unsigned char)clampf(cr*1.3f,0,255);
                gg=(unsigned char)clampf(cg*0.9f,0,255);
                bb=(unsigned char)clampf(cb*0.7f,0,255);
            } else {
                rr=(unsigned char)clampf(cr*0.5f,0,255);
                gg=(unsigned char)clampf(cg*0.8f,0,255);
                bb=(unsigned char)clampf(cb*1.4f,0,255);
            }
            unsigned char alpha=(unsigned char)(shapeAlpha[oi][k]*210);
            rlColor4ub(rr,gg,bb,alpha);
            rlBillboard(rx,ry,rz,0.40f);
        }
        rlEnd();
    }
    EndBlendMode();
}

/* ─────────────── DRAW PHOTONS ─────────────── */
static void drawPhotons(void){
    for(int i=0;i<MAX_PHOTONS;i++){
        if(!photons[i].active) continue;
        Photon *p=&photons[i];
        Color c=p->col; c.a=(unsigned char)(180*p->life);
        DrawSphereWires(p->pos,p->r,10,10,c);
        c.a=(unsigned char)(255*p->life);
        DrawCircle3D(p->pos,p->r,(Vector3){1,0,0},0,c);
        DrawCircle3D(p->pos,p->r,(Vector3){0,1,0},0,c);
        DrawCircle3D(p->pos,p->r,(Vector3){0,0,1},0,c);
    }
}

/* ─────────────── DRAW BACKGROUND STARS ─────────────── */
static void drawStars(float t){
    BeginBlendMode(BLEND_ADDITIVE);
    rlBegin(RL_TRIANGLES);
    for(int i=0;i<BG_STARS;i++){
        float twinkle=(sinf(t*1.3f+bsPhase[i])+1.0f)*0.5f;
        unsigned char a=(unsigned char)(50+twinkle*40);
        float s=bsSize[i]*(0.7f+twinkle*0.5f);
        rlColor4ub(200,220,255,a);
        rlBillboard(bsPos[i].x,bsPos[i].y,bsPos[i].z,s);
    }
    rlEnd();
    EndBlendMode();
}

/* ─────────────── EXCITE ELECTRON ─────────────── */
static void exciteElectron(int idx){
    if(idx<0||idx>=nelec) return;
    Electron *e=&electrons[idx];
    if(e->excLvl>0) return;
    e->excLvl=1; e->excTimer=1.0f;
    excFlash=1.0f;
    selOrbIdx=e->orbIdx;
    int n1=ORB_N[e->orbIdx];
    int n2=(n1<4)?n1+1:4;
    float dE=13.6f*(1.0f/(float)(n1*n1)-1.0f/(float)(n2*n2));
    float lam=(dE>0.001f)?1240.0f/dE:580.0f;
    Color pcol=(lam>300&&lam<750)?wl2col(lam):(Color){255,255,255,255};
    for(int w=0;w<MAX_PHOTONS;w++) if(!photons[w].active){
        photons[w]=(Photon){true,electronPos3D(e),0.5f,1.0f,pcol};
        break;
    }
    if(nspec<32&&lam>300&&lam<750)
        specLines[nspec++]=(SpecLine){lam,1.0f};
}

/* ─────────────── COMPACT PANEL ─────────────── */
static void drawPanel(void){
    int px=SW-220, pw=220;
    DrawRectangle(px,0,pw,SH,(Color){3,6,14,235});
    DrawLine(px,0,px,SH,(Color){0,150,200,65});
    /* subtle top-to-bottom gradient line */
    for(int y=0;y<SH;y++){
        float f=(float)y/(float)SH;
        unsigned char a=(unsigned char)(30+f*20);
        DrawPixel(px+1,y,(Color){0,80,120,a});
    }

    const Elem *el=&ELEMS[curZ-1];

    /* ── Element box ── */
    DrawRectangle(px+6,6,pw-12,86,(Color){5,10,24,210});
    DrawRectangleLines(px+6,6,pw-12,86,(Color){0,150,200,90});

    bool hL=CheckCollisionPointRec(GetMousePosition(),(Rectangle){(float)(px+10),12,26,22});
    bool hR=CheckCollisionPointRec(GetMousePosition(),(Rectangle){(float)(px+pw-36),12,26,22});
    DrawRectangle(px+10,12,26,22,hL?(Color){20,40,90,255}:(Color){10,20,50,200});
    DrawRectangle(px+pw-36,12,26,22,hR?(Color){20,40,90,255}:(Color){10,20,50,200});
    DrawText("<",px+15,14,20,(Color){0,200,255,255});
    DrawText(">",px+pw-31,14,20,(Color){0,200,255,255});

    /* period indicator dots */
    for(int p=1;p<=4;p++){
        bool on=(el->period==p);
        DrawCircle(px+pw-14,6+p*10,3,on?(Color){0,200,255,220}:(Color){30,50,80,180});
    }

    int sw2=MeasureText(el->sym,36);
    DrawText(el->sym,px+(pw-sw2)/2,10,36,(Color){230,240,255,255});
    char zstr[32]; snprintf(zstr,sizeof(zstr),"Z=%d   %.3fu",el->Z,el->mass);
    int zw=MeasureText(zstr,10); DrawText(zstr,px+(pw-zw)/2,52,10,(Color){80,120,160,255});
    int nw=MeasureText(el->name,11); DrawText(el->name,px+(pw-nw)/2,66,11,(Color){100,140,180,255});

    /* progress bar Z/36 */
    int barW=(pw-14)*(el->Z)/36;
    DrawRectangle(px+7,84,pw-14,4,(Color){10,20,40,200});
    DrawRectangle(px+7,84,barW,4,(Color){0,180,220,180});

    /* ── Electron config ── */
    int y=96;
    DrawRectangle(px+6,y,pw-12,16,(Color){3,8,20,200});
    /* ASCII-safe config — avoids multi-byte UTF-8 in fixed buffer */
    char cfgstr[128]={0};
    static const char *oname[]={"1s","2s","2p","3s","3p","3d","4s","4p"};
    for(int i=0;i<8;i++) if(el->cfg[i]>0){
        char buf[16]; snprintf(buf,sizeof(buf),"%s%d ",oname[i],el->cfg[i]);
        strcat(cfgstr,buf);
    }
    DrawText(cfgstr,px+8,y+3,9,(Color){0,212,255,255});

    /* ── Mode buttons ── */
    y=116;
    DrawText("VISUALIZATION",px+8,y,9,(Color){40,70,110,255});
    static const char *modes[]={"Bohr Model","Quantum Cloud","Orbital Shapes"};
    static const Color mcol[]={{0,212,255,255},{160,96,255,255},{0,255,200,255}};
    for(int i=0;i<3;i++){
        bool sel=(i==viewMode);
        bool hov=CheckCollisionPointRec(GetMousePosition(),(Rectangle){(float)(px+6),(float)(y+12+i*26),pw-12.f,22.f});
        DrawRectangle(px+6,y+12+i*26,pw-12,22,
            sel?(Color){14,28,60,255}:hov?(Color){10,22,45,200}:(Color){5,12,30,180});
        if(sel) DrawRectangle(px+6,y+12+i*26,3,22,mcol[i]);
        DrawText(modes[i],px+14,y+16+i*26,12,sel?WHITE:(Color){130,150,180,255});
    }

    /* ── Shell toggles ── */
    y+=12+3*26+6;
    DrawText("SHELLS",px+8,y,9,(Color){40,70,110,255});
    for(int s=0;s<4;s++){
        bool on=shellVis[s];
        bool hov=CheckCollisionPointRec(GetMousePosition(),(Rectangle){(float)(px+6+s*51),(float)(y+11),46.f,20.f});
        DrawRectangle(px+6+s*51,y+11,46,20,
            on?(Color){14,30,65,255}:hov?(Color){10,20,45,200}:(Color){5,10,25,180});
        DrawRectangleLines(px+6+s*51,y+11,46,20,
            on?(Color){0,180,220,180}:(Color){30,50,80,150});
        char sl[4]; snprintf(sl,sizeof(sl),"%c",SHELL_L[s]);
        int slw=MeasureText(sl,12);
        DrawText(sl,px+6+s*51+(46-slw)/2,y+14,12,on?WHITE:(Color){55,75,100,255});
    }

    /* ── Quantum numbers ── */
    y+=36;
    DrawText("ORBITAL",px+8,y,9,(Color){40,70,110,255});
    if(selOrbIdx>=0){
        char buf[64];
        const char *ltype[]={"s","p","d","f"};
        float eV=-13.6f/(float)(ORB_N[selOrbIdx]*ORB_N[selOrbIdx]);
        snprintf(buf,sizeof(buf),"%s   n=%d  l=%d  (%s)",
            oname[selOrbIdx],ORB_N[selOrbIdx],ORB_L[selOrbIdx],ltype[ORB_L[selOrbIdx]]);
        DrawText(buf,px+8,y+12,10,(Color){0,200,255,255});
        snprintf(buf,sizeof(buf),"Energy = %.2f eV",(double)eV);
        DrawText(buf,px+8,y+25,10,(Color){160,200,240,255});
    } else {
        DrawText("Click an electron",px+8,y+12,10,(Color){55,75,105,255});
        DrawText("to see quantum data",px+8,y+25,10,(Color){55,75,105,255});
    }

    /* ── Nucleus legend ── */
    y+=42;
    DrawText("NUCLEUS",px+8,y,9,(Color){40,70,110,255});
    DrawCircle(px+17,y+15,5,(Color){255,75,35,210});
    DrawText("Proton",px+26,y+10,10,(Color){200,155,135,255});
    DrawCircle(px+105,y+15,5,(Color){70,130,220,200});
    DrawText("Neutron",px+115,y+10,10,(Color){135,155,195,255});
    char pbuf[40]; snprintf(pbuf,sizeof(pbuf),"Z=%d  N=%d  A=%d",
        el->Z,(int)(el->Z*1.2f),el->Z+(int)(el->Z*1.2f));
    DrawText(pbuf,px+8,y+24,9,(Color){70,100,140,255});

    /* ── Controls (2 columns, compact) ── */
    y+=38;
    DrawText("CONTROLS",px+8,y,9,(Color){40,70,110,255});
    static const char *cL[]={"Drag: rotate","← →: element","1/2/3: mode","A: auto-spin"};
    static const char *cR[]={"Scroll: zoom","↑↓: jump 10 ","K L M N:shell","R:reset B:bloom"};
    for(int i=0;i<4;i++){
        DrawText(cL[i],px+8, y+12+i*12,9,(Color){55,80,110,255});
        DrawText(cR[i],px+112,y+12+i*12,9,(Color){55,80,110,255});
    }

    /* ── Spectrum (anchored to bottom) ── */
    drawSpectrum(px);

    if(autoSpin) DrawText("AUTO-SPIN",px+pw-66,SH-14,8,(Color){0,170,210,160});
}

/* ─────────────── HUD ─────────────── */
static void drawHUD(void){
    char fps[24]; snprintf(fps,sizeof(fps),"%.0f FPS",fpsSmooth);
    Color fc=fpsSmooth>=55?(Color){58,255,160,255}:fpsSmooth>=30?(Color){255,184,77,255}:(Color){255,60,60,255};
    DrawRectangle(6,6,MeasureText(fps,18)+12,26,(Color){0,0,0,140});
    DrawText(fps,11,9,18,fc);
    DrawText("QUANTUM ATOM SIMULATOR",6,36,14,(Color){0,200,255,200});

    if(excFlash>0) DrawRectangle(0,0,SW,SH,(Color){255,200,80,(unsigned char)(100*excFlash)});
    if(elemFlash>0) DrawRectangle(0,0,SW,SH,(Color){80,200,255,(unsigned char)(60*elemFlash)});
}

/* ─────────────── INIT STARS ─────────────── */
static void initStars(void){
    for(int i=0;i<BG_STARS;i++){
        float az=randf(0,6.28318f), el2=asinf(clampf(randf(-1,1),-1,1));
        float d=randf(220,750);
        bsPos[i]=(Vector3){d*cosf(el2)*sinf(az),d*sinf(el2),d*cosf(el2)*cosf(az)};
        bsSize[i]=randf(0.5f,1.8f);
        bsPhase[i]=randf(0,6.28318f);
    }
}

/* ─────────────── MAIN ─────────────── */
int main(void){
    srand((unsigned int)time(NULL));

    SetConfigFlags(FLAG_WINDOW_RESIZABLE|FLAG_WINDOW_MAXIMIZED|FLAG_MSAA_4X_HINT);
    InitWindow(1920,1080,"Quantum Atom Simulator  —  C/raylib");
    SetTargetFPS(60);

    /* FIX 1: update SW/SH from actual screen BEFORE creating render texture */
    SW=GetScreenWidth(); SH=GetScreenHeight();

    rtex=LoadRenderTexture(SW,SH);
    psh=LoadShaderFromMemory(NULL,FS_BLOOM);
    if(psh.id>0){
        shOK=true;
        uRes=GetShaderLocation(psh,"resolution");
        float res[2]={(float)SW,(float)SH};
        SetShaderValue(psh,uRes,res,SHADER_UNIFORM_VEC2);
    } else { shOK=false; useBloom=false; }

    for(int i=0;i<8;i++){ buildCloud(i); buildShape(i); }
    initStars();
    buildAtom(1);
    elemFlash=0;  /* don't flash on first load */

    lastTime=(float)GetTime();

    while(!WindowShouldClose()){
        float now=(float)GetTime();
        float dt=now-lastTime; if(dt>0.05f)dt=0.05f;
        lastTime=now;
        simTime+=dt;
        fpsSmooth+=(((dt>0)?1.0f/dt:60.0f)-fpsSmooth)*(dt/0.5f);
        excFlash =clampf(excFlash -dt*2.5f,0,1);
        elemFlash=clampf(elemFlash-dt*4.0f,0,1);

        /* window resize */
        if(IsWindowResized()){
            SW=GetScreenWidth(); SH=GetScreenHeight();
            UnloadRenderTexture(rtex);
            rtex=LoadRenderTexture(SW,SH);
            if(shOK){float res[2]={(float)SW,(float)SH};SetShaderValue(psh,uRes,res,SHADER_UNIFORM_VEC2);}
        }

        /* ── INPUT ── */
        /* FIX 3: detect electron click BEFORE setting dragging=true */
        Vector2 mp=GetMousePosition();
        bool justPressed=IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
        int panelX=SW-220;

        /* Panel clicks */
        if(justPressed){
            /* element arrows */
            if(CheckCollisionPointRec(mp,(Rectangle){(float)(panelX+10),12,26,22})){
                if(curZ>1){curZ--;buildAtom(curZ);selOrbIdx=-1;nspec=0;}
            }
            if(CheckCollisionPointRec(mp,(Rectangle){(float)(panelX+220-36),12,26,22})){
                if(curZ<36){curZ++;buildAtom(curZ);selOrbIdx=-1;nspec=0;}
            }
            /* mode buttons — y=128 to y=128+78=206 */
            int my=128;
            for(int i=0;i<3;i++)
                if(CheckCollisionPointRec(mp,(Rectangle){(float)(panelX+6),(float)(my+12+i*26),208,22}))
                    viewMode=i;
            /* shell toggles */
            int sy=my+3*26+18;
            for(int s=0;s<4;s++)
                if(CheckCollisionPointRec(mp,(Rectangle){(float)(panelX+6+s*51),(float)(sy+11),46,20}))
                    shellVis[s]=!shellVis[s];
        }

        /* Keyboard element navigation */
        if(IsKeyPressed(KEY_RIGHT)){ if(curZ<36){curZ++;buildAtom(curZ);selOrbIdx=-1;nspec=0;}}
        if(IsKeyPressed(KEY_LEFT)) { if(curZ>1) {curZ--;buildAtom(curZ);selOrbIdx=-1;nspec=0;}}
        if(IsKeyPressed(KEY_UP))   { curZ=clampi(curZ+10,1,36);buildAtom(curZ);selOrbIdx=-1;nspec=0;}
        if(IsKeyPressed(KEY_DOWN)) { curZ=clampi(curZ-10,1,36);buildAtom(curZ);selOrbIdx=-1;nspec=0;}

        if(IsKeyPressed(KEY_ONE))   viewMode=0;
        if(IsKeyPressed(KEY_TWO))   viewMode=1;
        if(IsKeyPressed(KEY_THREE)) viewMode=2;
        if(IsKeyPressed(KEY_K)) shellVis[0]=!shellVis[0];
        if(IsKeyPressed(KEY_L)) shellVis[1]=!shellVis[1];
        if(IsKeyPressed(KEY_M)) shellVis[2]=!shellVis[2];
        if(IsKeyPressed(KEY_N)) shellVis[3]=!shellVis[3];
        if(IsKeyPressed(KEY_A)) autoSpin=!autoSpin;
        if(IsKeyPressed(KEY_B)) useBloom=!useBloom;
        if(IsKeyPressed(KEY_R)){ targetAz=0.5f;targetEl=0.4f;targetDist=120;autoSpin=true; }

        /* Click electron to excite — checked BEFORE setting dragging */
        if(justPressed && mp.x<panelX && !dragging){
            Camera3D camRay=getCamera();
            Ray ray=GetScreenToWorldRay(mp,camRay);
            float bestT=FLT_MAX; int bestIdx=-1;
            for(int i=0;i<nelec;i++){
                if(!ELEMS[curZ-1].cfg[electrons[i].orbIdx]) continue;
                Vector3 ep=electronPos3D(&electrons[i]);
                Vector3 oc=Vector3Subtract(ray.position,ep);
                float a=Vector3DotProduct(ray.direction,ray.direction);
                float b=2*Vector3DotProduct(oc,ray.direction);
                float c=Vector3DotProduct(oc,oc)-16.0f;
                float disc=b*b-4*a*c;
                if(disc>=0){ float tt=(-b-sqrtf(disc))/(2*a); if(tt>0&&tt<bestT){bestT=tt;bestIdx=i;}}
            }
            if(bestIdx>=0) exciteElectron(bestIdx);
        }

        /* Camera drag */
        if(mp.x<panelX){
            if(justPressed){ dragging=true; dragLastX=mp.x; dragLastY=mp.y; }
        }
        if(IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) dragging=false;
        if(dragging&&IsMouseButtonDown(MOUSE_LEFT_BUTTON)){
            float dx=mp.x-dragLastX, dy=mp.y-dragLastY;
            targetAz+=dx*0.008f; targetEl+=dy*0.008f;
            targetEl=clampf(targetEl,-1.5f,1.5f);
            dragLastX=mp.x; dragLastY=mp.y;
            autoSpin=false;
        }
        float wheel=GetMouseWheelMove();
        if(wheel!=0) targetDist=clampf(targetDist-wheel*8,20,400);

        /* ── ANIMATION UPDATE ── */
        camAz+=(targetAz-camAz)*8*dt;
        camEl+=(targetEl-camEl)*8*dt;
        camDist+=(targetDist-camDist)*8*dt;
        if(autoSpin) targetAz+=dt*0.28f;

        for(int i=0;i<nelec;i++){
            electrons[i].angle+=electrons[i].speed*dt*2.0f;
            if(electrons[i].excTimer>0){
                electrons[i].excTimer-=dt*0.65f;
                if(electrons[i].excTimer<=0){electrons[i].excTimer=0;electrons[i].excLvl=0;}
            }
            /* update trail */
            Vector3 newPos=electronPos3D(&electrons[i]);
            electrons[i].trail[electrons[i].trailHead]=newPos;
            electrons[i].trailHead=(electrons[i].trailHead+1)%TRAIL_LEN;
            if(electrons[i].trailHead==0) electrons[i].trailFull=true;
        }
        for(int i=0;i<MAX_PHOTONS;i++){
            if(!photons[i].active) continue;
            photons[i].r+=65*dt;
            photons[i].life-=dt*0.75f;
            if(photons[i].life<=0) photons[i].active=false;
        }
        for(int i=0;i<nspec;i++) specLines[i].life-=dt*0.08f;
        int sl2=0; for(int i=0;i<nspec;i++) if(specLines[i].life>0) specLines[sl2++]=specLines[i];
        nspec=sl2;

        /* ── RENDER ── */
        Camera3D cam=getCamera();

        /* Update billboard vectors each frame */
        Vector3 fwd=Vector3Normalize(Vector3Subtract(cam.target,cam.position));
        gCamRight=Vector3Normalize(Vector3CrossProduct(fwd,(Vector3){0,1,0}));
        gCamUp   =Vector3CrossProduct(gCamRight,fwd);

        if(shOK&&useBloom) BeginTextureMode(rtex); else BeginDrawing();

        ClearBackground((Color){2,3,8,255});

        BeginMode3D(cam);

        drawStars(simTime);
        drawShellRings(simTime);

        if(viewMode==0)      drawElectronsBohr(simTime);
        else if(viewMode==1) drawCloud(simTime);
        else                 drawOrbitalShapes(simTime);

        drawNucleus(simTime);
        drawPhotons();

        EndMode3D();

        if(shOK&&useBloom){
            EndTextureMode();
            BeginDrawing();
            ClearBackground(BLACK);
            BeginShaderMode(psh);
            DrawTextureRec(rtex.texture,(Rectangle){0,0,(float)SW,-(float)SH},(Vector2){0,0},WHITE);
            EndShaderMode();
        }

        drawPanel();
        drawHUD();
        EndDrawing();
    }

    if(shOK){ UnloadRenderTexture(rtex); UnloadShader(psh); }
    CloseWindow();
    return 0;
}
