#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef unsigned int uint;
static uint fixture_us;
static int fixture_micros_calls;
uint narm_micros(void){fixture_micros_calls++;return fixture_us;}
int narm_yield(void){fixture_us+=250;return 0;}
#include "../src/mc10_profile.tc"

int main(void)
{
    fixture_us=100;mc_profile_begin();
    mc_profile_render_us=1000;mc_profile_draw_us=700;
    mc_profile_pump_us=500;mc_profile_tick_us=400;
    mc_profile_yield_us=200;mc_profile_setup_us=50;mc_profile_ui_us=100;
    mc_profile_hud_us=70;mc_profile_present_us=30;
    fixture_us=2100;mc_profile_finish(3831,211,0,70);
    assert(mc_profile_valid&&mc_profile_active);
    assert(mc_profile_last.total_us==2000);
    assert(mc_profile_last.geometry_us==300&&mc_profile_last.draw_us==700);
    assert(mc_profile_last.input_us==100&&mc_profile_last.tick_us==400);
    assert(mc_profile_last.yield_us==200&&mc_profile_last.setup_us==50);
    assert(mc_profile_last.ui_us==100&&mc_profile_last.other_us==150);
    assert(mc_profile_last.hud_us==70&&mc_profile_last.present_us==30);
    assert(mc_profile_last.faces==3831&&mc_profile_last.batches==211);
    assert(mc_profile_last.pending==0&&mc_profile_last.ticks==70);
    assert(mc_profile_render_us==0&&mc_profile_tick_us==0&&mc_profile_yield_us==0);
    assert(mc_profile_hud_us==0&&mc_profile_present_us==0);

    /* The yield after present is counted in the following complete interval. */
    mc_profile_yield();assert(mc_profile_yield_us==250);
    mc_profile_render_us=300;mc_profile_draw_us=100;
    mc_profile_pump_us=200;mc_profile_tick_us=100;
    mc_profile_setup_us=10;mc_profile_ui_us=40;
    fixture_us+=600;mc_profile_finish(24,3,1,71);
    assert(mc_profile_last.total_us==850&&mc_profile_last.yield_us==250);
    assert(mc_profile_last.geometry_us==200&&mc_profile_last.input_us==100);
    assert(mc_profile_last.other_us==50&&mc_profile_last.faces==24);

    /* Pause/Options wall time must not contaminate the next sample. */
    mc_profile_cancel();fixture_us+=5000000;
    mc_profile_finish(1,1,1,1);assert(mc_profile_last.faces==24);
    mc_profile_begin();fixture_us+=1000;mc_profile_finish(2,1,0,72);
    assert(mc_profile_last.total_us==1000&&mc_profile_last.other_us==1000);

    mc_profile_cancel();fixture_us=UINT32_MAX-255;mc_profile_begin();
    fixture_us+=512;mc_profile_finish(3,1,0,73);
    assert(mc_profile_last.total_us==512&&mc_profile_last.other_us==512);
    /* Inconsistent nested counters never underflow into huge HUD values. */
    mc_profile_render_us=2;mc_profile_draw_us=3;
    mc_profile_pump_us=2;mc_profile_tick_us=3;
    fixture_us+=1;mc_profile_finish(4,1,0,74);
    assert(mc_profile_last.geometry_us==0&&mc_profile_last.input_us==0);
    assert(mc_profile_last.other_us==0);
    mc_profile_cancel();mc_profile_enabled=0;
    int previous_calls=fixture_micros_calls;
    mc_profile_begin();mc_profile_yield();mc_profile_finish(999,999,999,999);
    assert(!mc_profile_active&&fixture_micros_calls==previous_calls&&mc_profile_last.faces==4);
    mc_profile_enabled=1;mc_profile_begin();assert(mc_profile_active);
    puts("Frame profiling: PASS nested accounting, frame boundaries, pause exclusion, rollover and underflow guards");
    return 0;
}
