"""Actual cursor functions: rate must survive slow menu redraws and clock wrap."""
import subprocess,tempfile
from pathlib import Path
PROJECT=Path(__file__).resolve().parents[1]
def function(source,name):
    start=source.rfind('\n',0,source.index(name+'('))+1
    brace=source.index('{',start);end=brace+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]+'\n'
def run():
    source=(PROJECT/'src/mc10_menu.tc').read_text()
    prefix='''#include <assert.h>
#include <stdio.h>
typedef unsigned int uint;
#define MC_SCREEN_WIDTH 320
#define MC_SCREEN_HEIGHT 240
#define NARM_KEY_LEFT 0
#define NARM_KEY_RIGHT 1
#define NARM_KEY_UP 2
#define NARM_KEY_DOWN 3
int held[4],mc_menu_pointer_x,mc_menu_pointer_y;
uint mc_menu_pointer_last_move,mc_menu_pointer_interval_ms=8,mc_menu_pointer_pending_ms;
int narm_key_down(uint key){return held[key];}
'''
    body=''.join(function(source,name) for name in ('mc_menu_set_cursor_speed','mc_menu_move_pointer','mc_menu_update_pointer_held'))
    checks='''
static int travel(float setting,int step){
 uint now=100;int i,total=0;
 mc_menu_set_cursor_speed(setting);mc_menu_pointer_last_move=now;
 for(i=0;i<1000/step;i++){
  mc_menu_pointer_x=0;mc_menu_pointer_y=100;now+=(uint)step;
  mc_menu_update_pointer_held(now);total+=mc_menu_pointer_x;
 }
 return total;
}
int main(void){
 int dt,slow,normal,fast;
 held[NARM_KEY_RIGHT]=1;
 for(dt=4;dt<=100;dt+=4){
  slow=travel(0,dt);normal=travel(.5f,dt);fast=travel(1,dt);
  assert(slow<normal&&normal<fast);
  assert(normal>=220&&normal<=250&&fast>=2*normal&&fast<=2*normal+2);
 }
 assert(travel(0,40)==62&&travel(.5f,40)==250&&travel(1,40)==500);
 mc_menu_set_cursor_speed(.5f);mc_menu_pointer_x=100;mc_menu_pointer_y=100;
 mc_menu_pointer_last_move=0xfffffffcu;
 assert(mc_menu_update_pointer_held(4)&&mc_menu_pointer_x==102);
 held[1]=0;mc_menu_update_pointer_held(12);assert(!mc_menu_pointer_pending_ms);
 held[1]=1;mc_menu_pointer_x=319;assert(!mc_menu_update_pointer_held(20));
 mc_menu_pointer_x=0;mc_menu_pointer_last_move=0;mc_menu_pointer_pending_ms=0;
 assert(mc_menu_update_pointer_held(100000)&&mc_menu_pointer_x==32);
 puts("Cursor timing PASS: 25 redraw cadences (4..100ms), speed ratios under slow rendering, elapsed remainder, idle, clamp, stall bound and clock wrap");
 return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='mc10-cursor-') as temp:
        root=Path(temp);c=root/'cursor.c';exe=root/'cursor.exe'
        c.write_text(prefix+body+checks)
        built=subprocess.run(['gcc','-O2',str(c),'-o',str(exe)],capture_output=True,text=True)
        assert built.returncode==0,built.stderr
        ran=subprocess.run([str(exe)],capture_output=True,text=True)
        assert ran.returncode==0,ran.stdout+ran.stderr
        print(ran.stdout.strip())
if __name__=='__main__':run()
