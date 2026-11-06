#include <assert.h>
#include "narm.tch"
typedef unsigned uint;
int mc_menu_pointer_x=160,mc_menu_pointer_y=120;
static struct NarmMouseState state={320,240,0,3};
static struct NarmMouseMotion motion;
static struct NarmMouseEvent queue[16];
static int head,tail,hidden,mode_calls,stuck_status,poll_count;
int narm_mouse_get_state(struct NarmMouseState *out){*out=state;return 0;}
int narm_mouse_poll_event(struct NarmMouseEvent *out)
{
    poll_count++;
    if(stuck_status){*out=(struct NarmMouseEvent){0,state.x,state.y,0,0,0,0,0,0};return stuck_status;}
    if(head==tail)return 0;*out=queue[head++];return 1;
}
int narm_mouse_read_motion(struct NarmMouseMotion *out)
{*out=motion;motion=(struct NarmMouseMotion){0};return 0;}
int narm_mouse_set_cursor_visible(unsigned visible){hidden=!visible;return 0;}
int narm_mouse_set_relative_mode(unsigned mode)
{state.flags=(state.flags&~4u)|(mode?4u:0u);head=tail;motion=(struct NarmMouseMotion){0};mode_calls++;return 0;}
#include "../src/mc10_mouse.tc"
static void push(uint type,int x,int y,uint buttons,uint changes,int wheel)
{queue[tail++]=(struct NarmMouseEvent){type,x,y,0,0,wheel,buttons,changes,0};}
int main(void)
{
    struct NarmMouseMotion got;
    hidden=1;mc_mouse_ui_reset();assert(!hidden&&mc_menu_pointer_x==160);
    state.x=220;state.y=400;
    assert(mc_mouse_ui_sync_pointer()==1&&mc_menu_pointer_x==110&&mc_menu_pointer_y==200);
    assert(mc_mouse_ui_sync_pointer()==0);
    state.flags=NARM_MOUSE_FOCUSED;assert(mc_mouse_ui_sync_pointer()==-1);
    state.flags=NARM_MOUSE_CONNECTED|NARM_MOUSE_FOCUSED;
    push(NARM_MOUSE_EVENT_CONNECT,220,400,0,0,0);
    assert(mc_mouse_ui_event()==0&&!hidden);
    push(3,639,479,3,3,0);
    assert(mc_mouse_ui_event()==1&&mc_mouse_ui_left&&mc_menu_pointer_x==319&&mc_menu_pointer_y==239);
    assert(mc_mouse_ui_event()==2&&mc_mouse_ui_event()==3);
    push(5,500,400,0,3,0);
    assert(mc_mouse_ui_event()==3&&mc_mouse_ui_left&&mc_menu_pointer_x==250);
    assert(mc_mouse_ui_event()==4&&!mc_mouse_ui_left&&mc_mouse_ui_event()==5);
    push(2,500,400,1,1,0);assert(mc_mouse_ui_event()==1);
    push(64,500,400,1,1,1);assert(mc_mouse_ui_event()==4&&mc_mouse_ui_event()==0);
    push(8,500,400,0,0,-2);assert(mc_mouse_ui_event()==6&&mc_mouse_ui_wheel==-2);
    push(2,500,400,1,1,0);mc_mouse_ui_reset();assert(mc_mouse_ui_event()==0);
    assert(mc_mouse_game_poll(1,&got)==0&&mode_calls==1&&got.dx==0);
    assert((state.flags&NARM_MOUSE_RELATIVE)&&mc_mouse_ui_sync_pointer()==-1);
    head=tail=0;push(10,500,400,1,1,3);push(8,500,400,1,0,1);push(8,500,400,1,0,-1);
    motion=(struct NarmMouseMotion){-19,7,3};
    assert(mc_mouse_game_poll(1,&got)==1&&got.dx==-19&&got.dy==7&&got.wheel==1);
    assert(mc_mouse_game_poll(1,&got)==0&&!got.dx&&!got.dy&&!got.wheel);
    push(2,500,400,1,1,0);motion.dx=40;
    assert(mc_mouse_game_poll(0,&got)==0&&got.dx==0&&head==tail);
    hidden=1;mc_mouse_ui_reset();assert(!(state.flags&NARM_MOUSE_RELATIVE)&&mode_calls==2&&!hidden);
    state.flags=2;assert(mc_mouse_game_poll(1,&got)==0&&mode_calls==2);
    state.flags=3;stuck_status=1;poll_count=0;mc_mouse_ui_reset();
    assert(poll_count==MC_MOUSE_POLL_LIMIT);
    poll_count=0;assert(mc_mouse_ui_event()==0&&poll_count==MC_MOUSE_POLL_LIMIT);
    state.flags=7;poll_count=0;assert(mc_mouse_game_poll(1,&got)==0&&poll_count==MC_MOUSE_POLL_LIMIT);
    stuck_status=2;poll_count=0;assert(mc_mouse_ui_event()==0&&poll_count==1);
}
