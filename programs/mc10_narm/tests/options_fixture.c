/* Host-only services exercise the actual options module, not a UI reimplementation. */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "narm.tch"
typedef unsigned int uint;
#define MC_SCREEN_WIDTH 320
#define MC_SCREEN_HEIGHT 240
#define MC_COLOR_DARK 8
#define MC_MENU_BUTTON_200_WORDS 1000
struct MCMenuButton {int id,x_position,y_position,width,height,enabled,draw_button;char *display_string;};
uint mc_menu_button_200[3000],mc_asset_pixels[256];
int mc_asset_loaded_width,mc_asset_loaded_height,mc_menu_pointer_x,mc_menu_pointer_y;
int fixture_hook_calls,fixture_label_available,fixture_label_bytes,fixture_file_exists,fixture_short_write;
int fixture_draw_strings,fixture_file_bytes,fixture_enter_down,fixture_writes,fixture_background_width;
float fixture_cursor_value;
char fixture_disk[4096],fixture_labels[2048];
void mc_menu_set_cursor_speed(float value){fixture_hook_calls++;fixture_cursor_value=value;}
int mc_menu_text_equals(char *a,char *b){return !strcmp(a,b);}
void mc_world_append(char *out,int cap,char *text){snprintf(out+strlen(out),cap-strlen(out),"%s",text);}
void mc_world_append_int(char *out,int cap,int value){snprintf(out+strlen(out),cap-strlen(out),"%d",value);}
uint *mc_menu_button_pixels(struct MCMenuButton *b,int state){return mc_menu_button_200;}
int mc_menu_button_hovered(struct MCMenuButton *b)
{return mc_menu_pointer_x>=b->x_position&&mc_menu_pointer_x<b->x_position+b->width&&mc_menu_pointer_y>=b->y_position&&mc_menu_pointer_y<b->y_position+b->height;}
void mc_menu_draw_centered_string(char *text,int x,int y,uint color){assert(text&&*text);fixture_draw_strings++;}
void mc_menu_draw_string(char *text,int x,int y,uint color){assert(text&&*text);fixture_draw_strings++;}
void mc_menu_draw_button(struct MCMenuButton *b){assert(b->display_string&&*b->display_string);fixture_draw_strings++;}
void mc_menu_draw_pointer(void){}
int mc_menu_font_width(char *text){return (int)strlen(text)*6;}
int mc_menu_update_pointer_held(uint now){return 0;}
int narm_half_blit(int x,int y,int width,int height,const uint *pixels){return 0;}
int narm_half_present(void){return 0;}
uint narm_millis(void){return 0;}
int narm_sleep_ms(uint ms){return 0;}
int narm_next_key_event(uint *key,uint *action,uint *mods,uint *character){return 0;}
int narm_poll_system_event(void){return NARM_EVENT_CLOSE_REQUESTED;}
int narm_key_down(uint key){return key==NARM_KEY_ENTER?fixture_enter_down:0;}
int mc_asset_load_image(char *name)
{mc_asset_loaded_width=mc_asset_loaded_height=fixture_background_width;return fixture_background_width!=0;}
int mc_asset_read_payload(char *name,void *out,uint cap,uint *bytes)
{
 if(strcmp(name,"mc10/options/labels")||!fixture_label_available)return 0;
 assert((uint)fixture_label_bytes<=cap);memcpy(out,fixture_labels,fixture_label_bytes);*bytes=(uint)fixture_label_bytes;return 1;
}
static void check_path(const char *path,uint storage){assert(!strcmp(path,"/options.txt")&&storage==NARM_STORAGE_SD);}
int narm_file_exists(const char *path,uint storage){check_path(path,storage);return fixture_file_exists;}
int narm_file_size(const char *path,uint storage,uint *size){check_path(path,storage);*size=(uint)fixture_file_bytes;return 0;}
int narm_file_read(const char *path,void *out,uint offset,uint bytes,uint storage)
{check_path(path,storage);assert(offset+bytes<sizeof(fixture_disk));memcpy(out,fixture_disk+offset,bytes);return (int)bytes;}
int narm_file_delete(const char *path,uint storage){check_path(path,storage);fixture_file_exists=0;return 0;}
int narm_file_create(const char *path,uint storage){check_path(path,storage);fixture_file_exists=1;fixture_file_bytes=0;return 1;}
int narm_file_write(const char *path,const void *data,uint offset,uint bytes,uint storage)
{check_path(path,storage);fixture_writes++;assert(offset+bytes<sizeof(fixture_disk));if(fixture_short_write&&bytes)bytes--;memcpy(fixture_disk+offset,data,bytes);fixture_file_bytes=(int)(offset+bytes);fixture_disk[fixture_file_bytes]=0;return (int)bytes;}
#include "../src/mc10_paths.tc"
#include "../src/mc10_options.tc"

static void load_text(const char *text)
{snprintf(fixture_disk,sizeof(fixture_disk),"%s",text);fixture_file_bytes=(int)strlen(text);fixture_file_exists=1;mc_options_load();}
static void near(float a,float b){assert(fabsf(a-b)<0.00001f);}
static void make_labels(void)
{
 int i,n;fixture_label_bytes=0;fixture_label_available=1;
 for(i=0;i<67;i++)
 {n=(int)strlen(mc_options_fallback_label(i))+1;assert(fixture_label_bytes+n<2048);memcpy(fixture_labels+fixture_label_bytes,mc_options_fallback_label(i),n);fixture_label_bytes+=n;}
}
static int check_labels(void)
{
 int i,count=0;char value[64],expected[64],actual[64];
 for(i=0;i<67;i++){assert(*mc_options_label(i));count++;}
 assert(!strcmp(mc_options_label(67),"Cursor Speed")&&!strcmp(mc_options_label(68),"Occlusion Culling"));
 for(i=0;i<=18;i++)
 {
  snprintf(value,sizeof(value),"STALE");mc_options_text_value(i,value);assert(*value&&!strstr(value,"STALE"));
  snprintf(expected,sizeof(expected),"%s: %s",mc_options_label(mc_options_label_for_option(i)),value);
  mc_options_format_option(i,actual);assert(!strcmp(actual,expected));count++;
 }
 return count;
}
int main(void)
{
 int i,screen,calls,labels=0,old_id[9]={0,1,2,3,4,11,101,100,200};
 int video_id[12]={12,6,13,10,8,7,14,9,5,15,16,200};
 float cursor,camera;char text[64];
 mc_options_fallback_labels=1;mc_options_defaults();
 near(mc_options.cursor_speed,.5f);near(fixture_cursor_value,.5f);near(mc_options.mouse_sensitivity,.5f);
 assert(!mc_options.occlusion_culling);
 /* Existing options files acquire independent port defaults without migration. */
 load_text("mouseSensitivity:0.2\nadvancedOpengl:true\nao:true\nunknown_source_key:retained-old-behavior\n");
 near(mc_options.mouse_sensitivity,.2f);near(mc_options.cursor_speed,.5f);near(fixture_cursor_value,.5f);
 assert(mc_options.advanced_opengl&&mc_options.ambient_occlusion&&!mc_options.occlusion_culling);
 load_text("mouseSensitivity:0.8\ncursorSpeed:-10\nocclusionCulling:true\n");
 near(mc_options.mouse_sensitivity,.8f);near(mc_options.cursor_speed,0);near(fixture_cursor_value,0);assert(mc_options.occlusion_culling);
 load_text("cursorSpeed:20\nocclusionCulling:false\n");near(mc_options.cursor_speed,1);assert(!mc_options.occlusion_culling);
 /* Camera and cursor setters/labels never overwrite or call each other. */
 camera=mc_options.mouse_sensitivity;mc_options_set_float(MC_OPT_CURSOR_SPEED,.25f);near(mc_options.mouse_sensitivity,camera);
 calls=fixture_hook_calls;cursor=mc_options.cursor_speed;mc_options_set_float(MC_OPT_SENSITIVITY,.7f);
 near(mc_options.cursor_speed,cursor);near(mc_options.mouse_sensitivity,.7f);assert(calls==fixture_hook_calls);
 mc_options_set_float(MC_OPT_CURSOR_SPEED,NAN);near(mc_options.cursor_speed,.5f);near(fixture_cursor_value,.5f);
 mc_options_format_option(MC_OPT_CURSOR_SPEED,text);assert(!strcmp(text,"Cursor Speed: 100%"));
 mc_options_set_float(MC_OPT_CURSOR_SPEED,-1);mc_options_format_option(MC_OPT_CURSOR_SPEED,text);assert(!strcmp(text,"Cursor Speed: 25%"));
 mc_options_set_float(MC_OPT_CURSOR_SPEED,2);mc_options_format_option(MC_OPT_CURSOR_SPEED,text);assert(!strcmp(text,"Cursor Speed: 200%"));
 mc_options_set_float(MC_OPT_CURSOR_SPEED,.375f);camera=mc_options.mouse_sensitivity;
 assert(mc_options_save()&&strstr(fixture_disk,"cursorSpeed:0.375\n")&&strstr(fixture_disk,"occlusionCulling:false\n"));
 assert(strstr(fixture_disk,"mouseSensitivity:0.7")&&!strstr(fixture_disk,"unknown_source_key"));
 mc_options_defaults();mc_options_load();near(mc_options.cursor_speed,.375f);near(mc_options.mouse_sensitivity,camera);assert(!mc_options.occlusion_culling);
 /* Original button positions/IDs remain; additions fill otherwise empty slots. */
 mc_options_build_screen(MC_OPTIONS_MAIN);assert(mc_options_button_count==10);
 for(i=0;i<9;i++)assert(mc_options_buttons[i].id==old_id[i]);
 assert(mc_options_buttons[6].y_position==148&&mc_options_buttons[7].y_position==172&&mc_options_buttons[8].y_position==208);
 assert(mc_options_buttons[9].x_position==5&&mc_options_buttons[9].y_position==112&&mc_options_button_kind[9]==MC_OPTIONS_KIND_SLIDER);
 mc_menu_pointer_x=9;mc_options_activate(9);near(mc_options.cursor_speed,0);
 mc_menu_pointer_x=151;mc_options_activate(9);near(mc_options.cursor_speed,1);near(mc_options.mouse_sensitivity,camera);
 calls=fixture_hook_calls;mc_menu_pointer_x=169;mc_options_activate(3);near(mc_options.mouse_sensitivity,0);
 near(mc_options.cursor_speed,1);assert(calls==fixture_hook_calls);
 mc_options_activate(6);assert(mc_options_screen==MC_OPTIONS_VIDEO&&mc_options_button_count==13);
 for(i=0;i<12;i++)assert(mc_options_buttons[i].id==video_id[i]);
 assert(mc_options_buttons[12].x_position==165&&mc_options_buttons[12].y_position==160&&mc_options_buttons[12].id==MC_OPT_OCCLUSION_CULLING);
 mc_options_format_option(MC_OPT_OCCLUSION_CULLING,text);assert(!strcmp(text,"Occlusion Culling: OFF"));
 mc_options_activate(12);assert(mc_options.occlusion_culling&&strstr(fixture_disk,"occlusionCulling:true\n"));
 mc_options_format_option(MC_OPT_OCCLUSION_CULLING,text);assert(!strcmp(text,"Occlusion Culling: ON"));
 mc_options_cycle(MC_OPT_AMBIENT_OCCLUSION);mc_options_cycle(MC_OPT_ADVANCED_OPENGL);assert(mc_options.occlusion_culling);
 mc_options_activate(12);assert(!mc_options.occlusion_culling);
 mc_options_build_screen(MC_OPTIONS_MAIN);mc_options_activate(7);assert(mc_options_screen==MC_OPTIONS_CONTROLS&&mc_options_button_count==14);
 mc_options_activate(13);assert(mc_options_screen==MC_OPTIONS_MAIN);
 /* GuiSlider mouse0=Enter hold/drag, no write per movement, one on release. */
 mc_options_set_float(MC_OPT_CURSOR_SPEED,.5f);fixture_enter_down=0;
 mc_options_build_screen(MC_OPTIONS_MAIN);mc_menu_pointer_x=80;mc_menu_pointer_y=112;
 fixture_enter_down=1;calls=fixture_writes;mc_options_press_enter();
 assert(mc_options_drag_slider==9&&!mc_options_drag_changed);
 mc_options_drag_step();assert(fixture_writes==calls);
 camera=mc_options.mouse_sensitivity;mc_menu_pointer_x=1000;mc_options_drag_step();
 near(mc_options.cursor_speed,1);near(mc_options.mouse_sensitivity,camera);assert(mc_options_drag_changed&&fixture_writes==calls);
 mc_options_drag_step();assert(fixture_writes==calls);
 fixture_enter_down=0;mc_options_drag_step();assert(mc_options_drag_slider==-1&&fixture_writes==calls+1);
 mc_options_release_enter();assert(fixture_writes==calls+1);
 /* A held Enter cannot activate a control on the newly entered screen. */
 fixture_enter_down=1;mc_options_build_screen(MC_OPTIONS_VIDEO);assert(mc_options_enter_blocked);
 mc_menu_pointer_x=170;mc_menu_pointer_y=160;cursor=(float)mc_options.occlusion_culling;
 mc_options_press_enter();assert(mc_options.occlusion_culling==(int)cursor);
 fixture_enter_down=0;mc_options_release_enter();fixture_enter_down=1;
 mc_options_press_enter();assert(mc_options.occlusion_culling!=(int)cursor);
 fixture_enter_down=0;mc_options_release_enter();
 /* Done/navigation saves current dragged value and ends capture, once. */
 mc_options_build_screen(MC_OPTIONS_MAIN);mc_menu_pointer_x=9;mc_menu_pointer_y=112;fixture_enter_down=1;
 mc_options_press_enter();assert(mc_options_drag_changed);calls=fixture_writes;
 mc_options_activate(6);assert(mc_options_screen==MC_OPTIONS_VIDEO&&mc_options_drag_slider==-1&&fixture_writes==calls+1);
 fixture_enter_down=0;mc_options_release_enter();assert(fixture_writes==calls+1);
 /* Missing/truncated/empty-but-successful language resources use all labels. */
 fixture_label_available=0;assert(mc_options_init()&&mc_options_fallback_labels);labels+=check_labels();
 make_labels();assert(mc_options_init()&&!mc_options_fallback_labels&&mc_options_self_test());labels+=check_labels();
 fixture_label_bytes--;assert(mc_options_init()&&mc_options_fallback_labels);labels+=check_labels();
 make_labels();fixture_labels[0]=0;assert(mc_options_init()&&mc_options_fallback_labels);labels+=check_labels();
 fixture_label_available=1;fixture_label_bytes=0;assert(mc_options_init()&&mc_options_fallback_labels);labels+=check_labels();
 mc_options_label_bytes=MC_OPTIONS_LABEL_BYTES+1;assert(!mc_options_labels_valid());mc_options_label_bytes=0;
 for(screen=0;screen<3;screen++)
 {
  mc_options_build_screen(screen);mc_options_draw();
  for(i=0;i<mc_options_button_count;i++)assert(*mc_options_buttons[i].display_string);
 }
 assert(fixture_draw_strings>30);
 /* The owned 16x16 dirt texture spans 32 screen pixels in GuiScreen.
  * Also retain compatibility with already-32x32 converted resources. */
 for(screen=16;screen<=32;screen+=16)
 {
  char *source=(char *)mc_asset_pixels,*background=(char *)mc_options_background;
  fixture_background_width=screen;
  for(i=0;i<1024;i++)source[i]=(char)((i*13+7)&255);
  assert(mc_options_init());
  for(i=0;i<1024;i++)
  {
   int at=screen==16?(i>>6)*16+((i&31)>>1):i;
   uint c=(uint)source[at]&255;
   assert(((uint)background[i]&255)==((((c>>5)>>2)<<5)|((((c>>2)&7)>>2)<<2)|((c&3)>>2)));
  }
 }
 fixture_background_width=0;
 fixture_short_write=1;assert(!mc_options_save());fixture_short_write=0;
 printf("Options runtime PASS: independent cursor/camera sliders, old-file defaults, clamps/roundtrip/clicks/held drag and release gating with single save, occlusion OFF default/toggle, original layout retained; %d label/composition checks and malformed-pack fallbacks\n",labels);
 return 0;
}
