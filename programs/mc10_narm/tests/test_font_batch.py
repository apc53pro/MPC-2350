"""Compare buffered native glyph output to the owned original bitmap font."""
import ctypes
import io
import random
import subprocess
import sys
import tempfile
from pathlib import Path
from zipfile import ZipFile
from PIL import Image

PROJECT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(PROJECT/"tools"))
from mc10_asset_pack import pack_image,font_metrics

def rgb332(value):return (((value>>16)&255)>>5)<<5|(((value>>8)&255)>>5)<<2|((value&255)>>6)

def run(legacy=False,legacy_define="MC10_STARTUP_DIAGNOSTIC",cache_ready=True):
    with ZipFile(Path.home()/"AppData/Roaming/.minecraft/versions/1.0/1.0.jar") as jar:
        font=Image.open(io.BytesIO(jar.read("font/default.png"))).convert("RGBA")
    _,mask=pack_image(font);metrics=font_metrics(font)
    glyphs=[[(x,y) for y in range(8) for x in range(8) if font.getpixel(((g&15)*8+x,(g>>4)*8+y))[3]>=128] for g in range(256)]
    background=bytes((i*13+i//320)&255 for i in range(320*240))
    calls=[]
    def glyph(out,g,x,y,c):
        for dx,dy in glyphs[g]:
            if 0<=x+dx<320 and 0<=y+dy<240:
                index=(y+dy)*320+x+dx;out[index]=c;calls.append((index,c))
    with tempfile.TemporaryDirectory(prefix="mc10-font-") as tmp:
        tmp=Path(tmp);dll=tmp/"font.dll"
        source=(PROJECT/"src/mc10_menu.tc").read_text();source=source[:source.index("void mc_menu_draw_rotated_glyph_pixel")]
        (tmp/"menu_text_under_test.tc").write_text(source,encoding="utf-8")
        defines=[f"-D{legacy_define}=1"] if legacy else []
        result=subprocess.run(["gcc","-shared","-O2",*defines,str(PROJECT/"tests/font_fixture.c"),"-I",str(tmp),"-I",str(PROJECT),"-o",str(dll)],capture_output=True,text=True)
        assert result.returncode==0,result.stderr
        lib=ctypes.CDLL(str(dll));pixels=(ctypes.c_ubyte*(320*240)).in_dll(lib,"fixture_pixels")
        atlas=(ctypes.c_uint32*512).in_dll(lib,"mc_menu_font_mask");widths=(ctypes.c_char*256).in_dll(lib,"mc_menu_font_metrics")
        ctypes.memmove(ctypes.addressof(atlas),mask,len(mask));ctypes.memmove(ctypes.addressof(widths),metrics,len(metrics))
        lib.mc_menu_prepare_glyph_cache()
        cached=(ctypes.c_uint32*512).in_dll(lib,"mc_menu_glyph_masks")
        indices=(ctypes.c_uint32*32768).in_dll(lib,"fixture_pixel_indices")
        colors=(ctypes.c_uint32*32768).in_dll(lib,"fixture_pixel_colors")
        ready=ctypes.c_int.in_dll(lib,"mc_menu_glyph_cache_ready")
        def same_pixel_calls():
            assert ctypes.c_int.in_dll(lib,"fixture_pixel_calls").value==len(calls)
            assert [(indices[i],colors[i]) for i in range(len(calls))]==calls,"pixel service order/index/color changed"
            assert ready.value==cache_ready,"pixel traversal changed cache initialization state"
        for g in range(256):
            for y in range(8):
                for x in range(8):
                    n=y*8+x
                    assert bool(cached[g*2+(n>>5)]&(1<<(31-(n&31))))==((x,y) in glyphs[g]),("glyph mask mirrored/changed",g,x,y)
        ready.value=cache_ready;cases=0
        for g in range(256):
            for x,y in [(13,17),(-4,10),(318,238),(-8,0),(0,-3),(320,240)]:
                calls.clear();lib.fixture_font_reset();out=bytearray(background);glyph(out,g,x,y,0xe7)
                lib.mc_menu_draw_glyph(x,y,g,0xe7);assert bytes(pixels)==out,(g,x,y)
                if not legacy:assert ctypes.c_int.in_dll(lib,"fixture_pixel_calls").value==0
                if legacy:
                    assert ctypes.c_int.in_dll(lib,"fixture_native_calls").value==0
                    same_pixel_calls()
                assert ctypes.c_int.in_dll(lib,"fixture_native_calls").value<=1
                cases+=1
        rng=random.Random(1500)
        for i in range(80):
            text=bytes(rng.randrange(1,256) for _ in range(rng.randrange(0,45)))
            x,y=rng.randrange(-30,310),rng.randrange(-8,240);color=rng.randrange(1<<24)
            calls.clear();out=bytearray(background)
            for ox,oy,c in [(x+1,y+1,rgb332((color&0xfcfcfc)>>2)),(x,y,rgb332(color))]:
                for g in text:glyph(out,g,ox,oy,c);ox+=metrics[g]
            for fallback in (0,1):
                ctypes.c_int.in_dll(lib,"fixture_glyph_error").value=fallback;lib.fixture_font_reset()
                lib.mc_menu_draw_string(text,x,y,color);assert bytes(pixels)==out,("string/shadow/clipping",i,fallback)
                if legacy:
                    assert ctypes.c_int.in_dll(lib,"fixture_native_calls").value==0
                    same_pixel_calls()
                elif fallback:same_pixel_calls()
                elif not fallback:assert ctypes.c_int.in_dll(lib,"fixture_pixel_calls").value==0
            cases+=1
        import _ctypes
        _ctypes.FreeLibrary(lib._handle)
    backend=("legacy pixel font; cache="+str(cache_ready)+"; zero native glyph calls") if legacy else "buffered font; <=1 native call/glyph"
    print(f"Font: PASS {backend}, all 256 original masks, {cases} pixel-identical glyph/string cases, clipping, RGB332, shadows, overlap and service fallback")

if __name__=="__main__":
    run()
    run(True,"MC10_LEGACY_FONT",True)
    run(True,"MC10_LEGACY_FONT",False)
