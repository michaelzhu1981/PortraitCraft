/* Original implementation. PhotoCraft WebAssembly filter ABI v1, no imports.
 * Weighted local statistics at three scales, with variance-based edge retention.
 * All spatial operations have finite support <= manifest.overlap (48 pixels).
 */
#include <stdint.h>
#include <stddef.h>
#include "manifest.h"
#define EXPORT(name) __attribute__((export_name(name)))

static float clamp(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static float sq(float x) { return x*x; }
static int bound(int x, int n) { return x < 0 ? 0 : x >= n ? n-1 : x; }
static int finite(float x) { return __builtin_isfinite(x); }
static float smooth(float lo, float hi, float x) {
    float t = clamp((x-lo)/(hi-lo), 0, 1); return t*t*(3-2*t);
}
static int equal(const char *s, int n, const char *t) {
    int i=0; while(t[i]) { if(i>=n || s[i]!=t[i]) return 0; i++; } return i==n;
}

EXPORT("pc_abi_version") uint32_t pc_abi_version(void) { return 1; }
EXPORT("pc_manifest") uint64_t pc_manifest(void) {
    return ((uint64_t)sizeof(manifest)<<32) | (uint32_t)(uintptr_t)manifest;
}
EXPORT("pc_alloc") uint32_t pc_alloc(uint32_t size) {
    if(!size || size > 536870912u) return 0;
    if(__builtin_wasm_memory_size(0)+(size+65535u)/65536u>8192) return 0;
    size_t old = __builtin_wasm_memory_grow(0, (size+65535u)/65536u);
    return old==(size_t)-1 ? 0 : (uint32_t)(old*65536u);
}

/* Bounded JSON scanner: only top-level fields are returned. The host validates
 * parameter types/ranges and serializes UTF-8. Nested _image is read separately.
 * Unknown fields/strings cannot shadow a top-level parameter. */
typedef struct { const char *s; int n; } Json;
static int ws(char c) { return c==' ' || c=='\n' || c=='\r' || c=='\t'; }
static int skip(Json j, int p) {
    while(p<j.n && ws(j.s[p])) p++;
    if(p>=j.n) return p;
    if(j.s[p]=='"') {
        for(p++;p<j.n;p++) { if(j.s[p]=='\\') p++; else if(j.s[p]=='"') return p+1; }
        return j.n;
    }
    if(j.s[p]=='{' || j.s[p]=='[') {
        int depth=0;
        for(;p<j.n;p++) {
            char c=j.s[p];
            if(c=='"') { p=skip(j,p)-1; continue; }
            if(c=='{' || c=='[') depth++;
            if(c=='}' || c==']') { if(--depth==0) return p+1; }
        }
        return j.n;
    }
    while(p<j.n && j.s[p]!=',' && j.s[p]!='}' && j.s[p]!=']' && !ws(j.s[p])) p++;
    return p;
}
static Json field(Json j, const char *key) {
    int p=0; while(p<j.n && ws(j.s[p])) p++;
    if(p>=j.n || j.s[p++]!='{') return (Json){0,0};
    while(p<j.n) {
        while(p<j.n && (ws(j.s[p]) || j.s[p]==',')) p++;
        if(p>=j.n || j.s[p]!='"') break;
        int end=skip(j,p), match=equal(j.s+p+1,end-p-2,key);
        p=end; while(p<j.n && ws(j.s[p])) p++;
        if(p>=j.n || j.s[p++]!=':') break;
        while(p<j.n && ws(j.s[p])) p++;
        end=skip(j,p);
        if(match) return (Json){j.s+p,end-p};
        p=end;
    }
    return (Json){0,0};
}
static float number(Json j, const char *key, float def, float lo, float hi) {
    Json v=field(j,key); if(!v.n) return def;
    int p=0, sign=1, digits=0; double n=0, f=0.1;
    if(v.s[p]=='-') { sign=-1; p++; }
    while(p<v.n && v.s[p]>='0' && v.s[p]<='9') { n=n*10+v.s[p++]-'0'; digits++; }
    if(p<v.n && v.s[p]=='.') {
        p++; while(p<v.n && v.s[p]>='0' && v.s[p]<='9') { n+=(v.s[p++]-'0')*f; f*=0.1; digits++; }
    }
    if(p<v.n && (v.s[p]=='e' || v.s[p]=='E')) {
        p++; int es=1, exp=0;
        if(p<v.n && (v.s[p]=='-' || v.s[p]=='+')) { if(v.s[p]=='-') es=-1; p++; }
        while(p<v.n && v.s[p]>='0' && v.s[p]<='9') { exp=exp*10+v.s[p++]-'0'; if(exp>100) return def; }
        while(exp--) n=es>0 ? n*10 : n/10;
    }
    if(!digits || p!=v.n || !__builtin_isfinite(n)) return def;
    return clamp((float)(sign*n),lo,hi);
}
static int choice(Json j, const char *key, const char *value) {
    Json v=field(j,key); return v.n>=2 && v.s[0]=='"' && equal(v.s+1,v.n-2,value);
}

typedef struct {
    float amount, fine, medium, large, texture, epsilon, sharpen;
    float warmth, tint, brightness, contrast, saturation, tolerance, cb, cr;
    int radius, mask, view, origin_x, canvas_width;
} Config;
static Config config(Json j, int width, int height) {
    Config c;
    c.amount=number(j,"amount",85,0,100)*0.01f;
    /* Presets are complete smoothing recipes. ABI v1 cannot update the host's
     * sliders, so absolute sliders belong to the explicit custom mode. Mask,
     * amount and enhancements remain common to all modes. */
    float fine=85, medium=65, large=35, texture=35, edge=75, threshold=45;
    int scale=0, min_scale=1;
    if(choice(j,"preset","Gentle")) {
        fine=55; medium=30; large=10; texture=65; edge=85; threshold=20;
    } else if(choice(j,"preset","Strong smoothing")) {
        fine=100; medium=90; large=65; texture=15; edge=65; threshold=70;
        min_scale=2;
    } else if(choice(j,"preset","Custom")) {
        fine=number(j,"fine",85,0,100);
        medium=number(j,"medium",65,0,100);
        large=number(j,"large",35,0,100);
        texture=number(j,"texture",35,0,100);
        edge=number(j,"edge",75,0,100);
        threshold=number(j,"threshold",45,0,100);
        scale=(int)number(j,"scale",0,0,4);
    }
    c.texture=texture*0.01f;
    c.fine=fine*0.01f*(1-0.65f*c.texture);
    c.medium=medium*0.01f*(1-0.25f*c.texture);
    c.large=large*0.01f;
    /* A separate threshold controls which local contrast is smoothed; edge
     * protection still suppresses smoothing at strong feature boundaries. */
    c.epsilon=0.0001f+0.016f*sq(threshold*0.01f)*(1-edge*0.008f);
    Json geo=field(j,"_image");
    c.canvas_width=(int)number(geo,"canvasWidth",width,1,100000000);
    double area=(double)c.canvas_width*number(geo,"canvasHeight",height,1,100000000);
    int auto_scale=area<4000000 ? 1 : area<10000000 ? 2 : area<20000000 ? 3 : 4;
    c.radius=scale ? scale : (auto_scale>min_scale ? auto_scale : min_scale);
    c.sharpen=number(j,"sharpen",0,0,100)*0.005f;
    c.warmth=number(j,"warmth",0,-100,100)*0.0008f;
    c.tint=number(j,"tint",0,-100,100)*0.0005f;
    c.brightness=number(j,"brightness",0,-100,100)*0.002f;
    c.contrast=1+number(j,"contrast",0,-100,100)*0.005f;
    c.saturation=1+number(j,"saturation",0,-100,100)*0.01f;
    c.tolerance=number(j,"tolerance",50,0,100)*0.01f;
    float r=number(j,"sampleR",190,0,255)/255, g=number(j,"sampleG",130,0,255)/255;
    float b=number(j,"sampleB",100,0,255)/255, y=0.299f*r+0.587f*g+0.114f*b;
    c.cb=(b-y)*0.564f; c.cr=(r-y)*0.713f;
    c.mask=choice(j,"mask","Whole image") ? 2 : choice(j,"mask","Sampled skin") ? 1 : 0;
    c.view=choice(j,"view","Skin mask") ? 1 : choice(j,"view","Red overlay") ? 2 : choice(j,"view","Split view") ? 3 : 0;
    c.origin_x=(int)number(geo,"x",0,-100000000,100000000);
    return c;
}
static float skin(float r,float g,float b,Config c) {
    if(c.mask==2) return 1;
    /* Classification is display-referred; float HDR values remain unmodified in
     * the image. Chroma matching is a heuristic, not face/semantic segmentation. */
    r=clamp(r,0,1); g=clamp(g,0,1); b=clamp(b,0,1);
    float y=.299f*r+.587f*g+.114f*b, cb=(b-y)*.564f, cr=(r-y)*.713f;
    float d;
    if(c.mask==1) {
        float rad=.018f+.16f*c.tolerance;
        d=(sq(cb-c.cb)+sq(cr-c.cr))/sq(rad);
    } else {
        float spread=.65f+c.tolerance;
        d=sq((cb+.065f)/(.080f*spread))+sq((cr-.095f)/(.100f*spread));
    }
    float m=1-smooth(.25f,1,d);
    /* Exclude neutral colors and clipped highlights/shadows in automatic mode. */
    if(c.mask==0) m*=smooth(.006f,.03f,cr-cb)*smooth(.015f,.10f,y)*(1-smooth(.94f,1,y));
    return m;
}

/* Each source pixel contributes alpha/skin-weighted RGB and second moment.
 * Transparent RGB and non-finite samples never contaminate neighboring pixels. */
static void moments(const float *p,float weight,float out[5]) {
    if(weight<=0) { for(int k=0;k<5;k++) out[k]=0; return; }
    out[0]=p[0]*weight; out[1]=p[1]*weight; out[2]=p[2]*weight;
    out[3]=(sq(p[0])+sq(p[1])+sq(p[2]))*weight/3; out[4]=weight;
}
static void smooth_scale(const float *src,const float *weights,float *horizontal,float *result,
                         int w,int h,int ch,int radius,float epsilon,float coefficient) {
    /* Sliding sums use double precision: long bands must not develop seams from
     * float accumulation drift. The intermediate image is f32 to bound memory. */
    for(int y=0;y<h;y++) {
        double s[5]={0}; float a[5],b[5];
        for(int dx=-radius;dx<=radius;dx++) {
            int i=y*w+bound(dx,w); moments(src+i*ch,weights[i],a);
            for(int k=0;k<5;k++) s[k]+=a[k];
        }
        for(int x=0;x<w;x++) {
            int i=y*w+x; for(int k=0;k<5;k++) horizontal[i*5+k]=(float)s[k];
            int left=y*w+bound(x-radius,w), right=y*w+bound(x+radius+1,w);
            moments(src+left*ch,weights[left],a); moments(src+right*ch,weights[right],b);
            for(int k=0;k<5;k++) s[k]+=(double)b[k]-a[k];
        }
    }
    for(int x=0;x<w;x++) {
        double s[5]={0};
        for(int dy=-radius;dy<=radius;dy++) {
            const float *p=horizontal+(bound(dy,h)*w+x)*5;
            for(int k=0;k<5;k++) s[k]+=p[k];
        }
        for(int y=0;y<h;y++) {
            int i=y*w+x;
            if(weights[i]>0 && s[4]>1e-8) {
                float r=s[0]/s[4],g=s[1]/s[4],b=s[2]/s[4];
                float variance=clamp((float)(s[3]/s[4])-(sq(r)+sq(g)+sq(b))/3,0,1e20f);
                float blend=epsilon/(epsilon+variance);
                result[i*3]   +=coefficient*(r-src[i*ch])*blend;
                result[i*3+1] +=coefficient*(g-src[i*ch+1])*blend;
                result[i*3+2] +=coefficient*(b-src[i*ch+2])*blend;
            }
            const float *a=horizontal+(bound(y-radius,h)*w+x)*5;
            const float *b=horizontal+(bound(y+radius+1,h)*w+x)*5;
            for(int k=0;k<5;k++) s[k]+=(double)b[k]-a[k];
        }
    }
}
static int region(uint32_t ptr,uint32_t n) {
    return ptr && (uint64_t)ptr+n <= (uint64_t)__builtin_wasm_memory_size(0)*65536;
}
EXPORT("pc_filter") int pc_filter(uint32_t ptr,uint32_t len,int w,int h,int ch,uint32_t format,uint32_t params,uint32_t params_len) {
    if(w<=0 || h<=0 || ch<=0 || ch>4 || ptr%4 || !region(ptr,len) || !region(params,params_len) || params_len>65536) return 1;
    uint64_t count=(uint64_t)w*h;
    if(count*ch*4!=len || count>0x7fffffffu/20u) return 1;
    int alpha=(format>>8)&1, mode=(format>>16)&255, depth=format&255;
    if(mode!=3 || ch!=3+alpha || (depth!=8 && depth!=16 && depth!=32)) return 2;
    float *pixels=(float *)(uintptr_t)ptr;
    Config c=config((Json){(const char *)(uintptr_t)params,(int)params_len},w,h);
    if(c.amount==0 && (c.view==0 || c.view==3)) return 0;
    uint32_t scratch=pc_alloc((uint32_t)count*10*4);
    if(!scratch) return 3;
    float *masks=(float *)(uintptr_t)scratch, *weights=masks+count;
    float *horizontal=weights+count, *result=horizontal+count*5;
    for(uint32_t i=0;i<count;i++) {
        const float *p=pixels+i*ch;
        int valid=finite(p[0]) && finite(p[1]) && finite(p[2]) && (!alpha || finite(p[3]));
        /* Extremely large HDR inputs are kept, but excluded from local moments
         * to avoid overflow. Ordinary negative/above-white HDR is supported. */
        valid=valid && __builtin_fabsf(p[0])<1e10f && __builtin_fabsf(p[1])<1e10f && __builtin_fabsf(p[2])<1e10f;
        float a=alpha ? clamp(p[3],0,1) : 1;
        masks[i]=valid && a>0 ? skin(p[0],p[1],p[2],c) : 0;
        weights[i]=masks[i]>0 ? masks[i]*a : 0;
        for(int k=0;k<3;k++) result[i*3+k]=0;
    }
    if(c.view!=1 && c.view!=2) {
        /* Telescoping three-band decomposition, including masked fine sharpening:
         * delta = f(B1-I) + m(B2-B1) + l(B3-B2) - sharp(B1-I). */
        float coeffs[3]={c.fine-c.medium-c.sharpen,c.medium-c.large,c.large};
        int radii[3]={2*c.radius,5*c.radius,12*c.radius};
        for(int s=0;s<3;s++) if(coeffs[s]!=0)
            smooth_scale(pixels,weights,horizontal,result,w,h,ch,radii[s],c.epsilon,coeffs[s]);
    }
    for(uint32_t i=0;i<count;i++) {
        float *p=pixels+i*ch, m=masks[i];
        if(alpha && !(p[3]>0)) continue;
        if(!finite(p[0]) || !finite(p[1]) || !finite(p[2])) continue;
        if(c.view==1) { p[0]=p[1]=p[2]=m; continue; }
        if(c.view==2) { p[0]+=m*.5f*(1-p[0]); p[1]*=1-m*.5f; p[2]*=1-m*.5f; continue; }
        if(m<=0 || (c.view==3 && c.origin_x+(int)(i%(uint32_t)w)<c.canvas_width/2)) continue;
        float rgb[3];
        for(int k=0;k<3;k++) rgb[k]=p[k]+result[i*3+k];
        rgb[0]+=c.warmth+c.tint*.5f; rgb[1]-=c.tint; rgb[2]-=c.warmth-c.tint*.5f;
        float y=.299f*rgb[0]+.587f*rgb[1]+.114f*rgb[2];
        for(int k=0;k<3;k++) {
            float v=y+(rgb[k]-y)*c.saturation;
            v=(v-.5f)*c.contrast+.5f+c.brightness;
            v=p[k]+m*c.amount*(v-p[k]);
            if(finite(v)) p[k]=depth==32 ? v : clamp(v,0,1);
        }
    }
    return 0;
}
