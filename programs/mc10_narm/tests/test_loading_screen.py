"""Check source loading layout and absence of the custom compilation screen."""
import os
from pathlib import Path
import subprocess
import tempfile

PROJECT=Path(os.environ.get('MC10_PROJECT',str(Path(__file__).resolve().parents[1])))

def run():
    world=(PROJECT/'src/mc10_world.tc').read_text()
    # Skip its earlier forward declaration.
    start=world.index('void mc_runtime_draw_loading(struct MCWorldInfo *world, int loaded)\n{')
    end=world.index('\nchar *mc_runtime_error_text',start)
    function=world[start:end]
    for name in ('mc10_game.tc','mc10_benchmark.tc'):
        assert 'compiling' not in (PROJECT/'src'/name).read_text().lower()
    fixture=r'''
#include <assert.h>
#include <string.h>
typedef unsigned int uint;
#define MC10_PLAYER_FOUNDATION
struct MCWorldInfo {int unused;};
unsigned char screen[76800];int text_calls,background_calls,presents;
uint mc_menu_rgb888_to_rgb332(uint c){return ((c>>16)&224)|((c>>11)&28)|((c>>6)&3);}
void mc_options_draw_background(void){memset(screen,3,sizeof(screen));background_calls++;}
void mc_menu_draw_centered_string(char *text,int x,int y,uint color){
assert(x==160&&color==0xffffff);
assert(text_calls==0?(!strcmp(text,"Loading level")&&y==100):(!strcmp(text,"Building terrain")&&y==124));text_calls++;}
void narm_half_fill_rect(int x0,int y0,int x1,int y1,uint c){
assert(x0>=0&&x1<320&&y0>=0&&y1<240);for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++)screen[y*320+x]=(unsigned char)c;}
void narm_half_present(void){presents++;}
'''+function+r'''
int main(void){for(int loaded=0;loaded<=9;loaded++){
text_calls=background_calls=presents=0;mc_runtime_draw_loading(0,loaded);
assert(text_calls==2&&background_calls==1&&presents==1);
for(int y=0;y<240;y++)for(int x=0;x<320;x++){
uint expected=3;if(y>=136&&y<138&&x>=110&&x<210)expected=mc_menu_rgb888_to_rgb332(x<110+loaded*100/9?0x80ff80:0x808080);
assert(screen[y*320+x]==expected);}}return 0;}
'''
    with tempfile.TemporaryDirectory(prefix='mc10-loading-') as temporary:
        temp=Path(temporary);source=temp/'loading.c';binary=temp/'loading.exe'
        source.write_text(fixture)
        subprocess.run(['gcc','-O2',str(source),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
    print('Loading screen PASS: original title/subtext positions and exact100x2 progress pixels at10 stages; no custom compiling messages in gameplay/profiler.')

if __name__=='__main__':run()
