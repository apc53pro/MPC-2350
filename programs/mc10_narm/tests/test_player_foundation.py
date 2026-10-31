"""Deterministic bounded inventory and original-asset HUD/UI host tests.

Inputs remain local: no Minecraft source/assets or owned save data are copied
into test output. These are C function/service fixtures, not a NARM emulator.
"""
import ctypes as C
import _ctypes
import gzip
import importlib.util
import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile

HERE=Path(__file__).resolve().parents[1]
PROJECT=Path(os.environ.get('MC10_PROJECT',r'C:\Users\enzob\OneDrive\Documentos\RP2350_GP_Computer\Firmware\resource_development\programs\mc10_narm'))
ORACLE=Path.home()/'AppData/Roaming/.minecraft/versions/1.0/minecraft/src/net/minecraft/src'

class Stack(C.Structure):
    _fields_=[(name,C.c_int) for name in ('id','count','damage','tag_offset','tag_bytes')]

def named(kind,name,payload):
    data=name.encode();return bytes([kind])+struct.pack('>H',len(data))+data+payload
def compound(fields):return b''.join(fields)+b'\0'
def scalar(kind,name,value):
    return named(kind,name,struct.pack({1:'>b',2:'>h',3:'>i',5:'>f'}[kind],value))
def stack_nbt(slot,item,count=1,damage=0,tag=None):
    fields=[scalar(1,'Slot',slot),scalar(2,'id',item),scalar(1,'Count',count),scalar(2,'Damage',damage)]
    if tag is not None:fields.append(named(10,'tag',tag))
    return compound(fields)
def player_nbt(stacks,**values):
    fields=[named(9,'Inventory',bytes([10])+struct.pack('>i',len(stacks))+b''.join(stacks))]
    for key,value in values.items():fields.append(scalar(2 if key in ('Health','Air') else 5 if key in ('XpP','foodSaturationLevel') else 3,key,value))
    return named(10,'',compound(fields))

def extract(source,name):
    start=source.rfind('\n',0,source.index(name+'('))+1;end=source.index('{',start)+1;depth=1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]+'\n'

def run():
    pack=(PROJECT/'MC10/assets.mc10').read_bytes();n,size,offset=struct.unpack_from('<III',pack,8)
    directory={row[0]:row for row in (struct.unpack_from('<10I',pack,offset+i*40) for i in range(n))}
    def hash_name(name):
        result=0x811c9dc5
        for byte in name.encode():result=((result^byte)*0x01000193)&0xffffffff
        return result
    def image(name):
        row=directory[hash_name(name)];return row[2],row[3],pack[row[5]:row[5]+row[6]],pack[row[7]:row[7]+row[8]]
    def payload(name):
        row=directory[hash_name(name)];return pack[row[5]:row[5]+row[6]]
    menu=(PROJECT/'src/mc10_menu.tc').read_text();menu=menu[:menu.index('void mc_menu_draw_rotated_glyph_pixel')]+extract(menu,'mc_menu_move_pointer')+extract(menu,'mc_menu_update_pointer_held')
    rand=(PROJECT/'src/mc10_java_random.tc').read_text();rand=rand[:rand.index('void mc_java_random_next_double_bits')]
    with tempfile.TemporaryDirectory(prefix='mc10-foundation-',ignore_cleanup_errors=True) as temporary:
        temporary=Path(temporary)
        for output,source in [('nbt',PROJECT/'src/mc10_nbt.tc'),('assets',PROJECT/'src/mc10_assets.tc'),('items',HERE/'src/mc10_items.tc'),('inventory',HERE/'src/mc10_inventory.tc'),('player_ui',HERE/'src/mc10_player_ui.tc')]:
            (temporary/(output+'_under_test.tc')).write_text(source.read_text())
        (temporary/'random_under_test.tc').write_text(rand);(temporary/'menu_under_test.tc').write_text(menu)
        (temporary/'key_map_under_test.tc').write_text(extract((PROJECT/'src/mc10_options.tc').read_text(),'mc_options_narm_to_lwjgl'))
        dllpath=temporary/'foundation.dll'
        result=subprocess.run(['gcc','-shared','-O2','-fno-strict-aliasing',str(HERE/'tests/inventory_fixture.c'),'-I',str(temporary),'-I',str(PROJECT.parents[3]/'Compiler_For_NARM'),'-o',str(dllpath)],capture_output=True,text=True)
        assert result.returncode==0,result.stderr
        dll=C.CDLL(str(dllpath));dll.mc_inventory_current.restype=C.POINTER(Stack)
        slots=(Stack*40).in_dll(dll,'mc_inventory_slots');held=Stack.in_dll(dll,'mc_inventory_carried')
        pixels=(C.c_ubyte*76800).in_dll(dll,'fixture_pixels');depth=(C.c_uint*76800).in_dll(dll,'fixture_depth')
        tags=(C.c_ubyte*16384).in_dll(dll,'mc_inventory_tags')
        def integer(name):return C.c_int.in_dll(dll,name)
        def load(data):
            data=C.create_string_buffer(data);return dll.fixture_load_player(data,len(data)-1)
        def copy_global(name,data):C.memmove(C.addressof((C.c_ubyte*len(data)).in_dll(dll,name)),data,len(data))
        def total():return sum(stack.count for stack in slots)+held.count
        def state():return [(s.id,s.count,s.damage,s.tag_offset,s.tag_bytes) for s in slots]+[(held.id,held.count,held.damage,held.tag_offset,held.tag_bytes)]
        assert dll.mc_inventory_load_player_nbt(-1)==1 and integer('mc_inventory_ready').value
        assert not total() and integer('mc_inventory_health').value==20 and integer('mc_inventory_selected').value==0
        # Registry IDs and icon facts are independently checked against the
        # user's Item registration declarations, never copied as source text.
        import re
        original=(ORACLE/'Item.java').read_text()
        rows=re.findall(r'new Item\w*\((\d+)[^\r\n]*?setIconCoord\((\d+), (\d+)\)',original)
        for number,x,y in rows:
            item=int(number)+256
            assert dll.mc_item_valid(item)
            if item not in (345,347,351,373):assert dll.mc_item_icon(item,0)==int(x)+int(y)*16
        assert not any(dll.mc_item_valid(i) for i in (0,123,255,383,2255,2267,31999,-1))
        for item,limit in [(1,64),(259,1),(261,1),(282,1),(332,16),(344,16),(368,16),(373,1),(2256,1)]:assert dll.mc_item_limit(item)==limit
        for item,damage in [(256,250),(268,59),(272,131),(276,1561),(283,32),(298,55),(311,528),(317,91),(346,64),(359,238)]:assert dll.mc_item_max_damage(item)==damage
        for damage in range(16):assert dll.mc_item_icon(351,damage)==78+(damage%8)*16+damage//8
        # Independent retention, original slot-byte mapping, 1.0 selection0,
        # loaded state, unknown ID ignore and duplicate-slot replacement.
        special=compound([named(8,'name',b'\0\x03tag'),named(7,'raw',struct.pack('>i',4)+b'\0\xff\x80\x01')])
        initial=player_nbt([stack_nbt(0,262,12,0,special),stack_nbt(1,259,1,16),stack_nbt(100,317),stack_nbt(103,86),stack_nbt(36,264),stack_nbt(7,999),stack_nbt(8,264,2),stack_nbt(8,265,3)],Health=17,Air=290,foodLevel=13,foodSaturationLevel=2.5,XpP=.5,XpLevel=7,XpTotal=70,SelectedItemSlot=8)
        assert load(initial)==1
        assert slots[0].id==262 and slots[0].count==12 and slots[1].damage==16
        assert slots[36].id==317 and slots[39].id==86 and slots[7].count==0 and slots[8].id==265
        assert integer('mc_inventory_selected').value==0
        assert [integer('mc_inventory_'+name).value for name in ('health','air','food','xp_level','xp_total')]==[17,290,13,7,70]
        retained=bytes(tags[slots[0].tag_offset:slots[0].tag_offset+slots[0].tag_bytes]);assert retained==named(10,'tag',special)
        assert slots[1].tag_bytes==0,'missing compound must not turn into an invented empty tag'
        dll.mc_nbt_reset();assert bytes(tags[:len(retained)])==retained
        bad=named(10,'',compound([scalar(3,'Inventory',2)]))
        assert load(bad)==0 and integer('mc_inventory_error').value==-1
        bad=named(10,'',compound([named(9,'Inventory',bytes([10])+struct.pack('>i',1)+compound([scalar(1,'Slot',0),scalar(2,'id',262),scalar(1,'Count',1),scalar(2,'Damage',0),scalar(3,'tag',2)]))]))
        assert load(bad)==0 and integer('mc_inventory_error').value==-1
        assert load(initial)==1
        # Source Container click cases, including 1.0's subtype-only equality
        # and armor rejection. No item use/drop callback is invented.
        before=total();assert dll.mc_inventory_click(0,1)==1 and held.count==6 and slots[0].count==6
        assert not dll.mc_inventory_can_close() and integer('mc_inventory_error').value==-4
        assert dll.mc_inventory_click(-1,0)==-1 and total()==before
        assert dll.mc_inventory_click(2,1)==1 and slots[2].count==1 and held.count==5
        assert dll.mc_inventory_click(36,0)==0 and total()==before
        assert dll.mc_inventory_click(0,0)==1 and slots[0].count==11 and held.count==0
        assert dll.mc_inventory_can_close()
        assert dll.mc_inventory_click(39,0)==1 and held.id==86
        assert dll.mc_inventory_click(36,0)==0
        assert dll.mc_inventory_click(39,0)==1
        # Randomized source-compatible click oracle, exact stack counts/IDs /
        # survivor tags, and conservation across all supported armor slots.
        rng=random.Random(1028)
        assert load(player_nbt([stack_nbt(i,item,rng.randint(1,32) if item in (1,262,263,351) else 1,rng.randrange(4) if item in (263,351) else 0) for i,item in enumerate((1,262,263,351,259,298,317))]))==1
        model=[list(row) for row in state()]
        def clear(s):s[:]=[0]*5
        def valid(slot,s):return slot<36 or dll.mc_item_armor_type(s[0])==39-slot or (slot==39 and s[0]==86)
        def same(a,b):return a[0]==b[0] and (not dll.mc_item_subtypes(a[0]) or a[2]==b[2])
        for _ in range(1800):
            slot=rng.randrange(40);right=rng.randrange(2);target=model[slot];hand=model[40];limit=64 if slot<36 else 1
            original_total=total()
            if target[1]<=0:
                if hand[1]>0 and valid(slot,hand):amount=min(1 if right else hand[1],limit);target[:]=hand[:];target[1]=amount;hand[1]-=amount
            elif hand[1]<=0:amount=(target[1]+1)//2 if right else target[1];hand[:]=target[:];hand[1]=amount;target[1]-=amount
            elif valid(slot,hand):
                if not same(target,hand):
                    if hand[1]<=limit:target[:],hand[:]=hand[:],target[:]
                else:amount=max(0,min(1 if right else hand[1],limit-target[1],dll.mc_item_limit(hand[0])-target[1]));target[1]+=amount;hand[1]-=amount
            elif same(target,hand) and dll.mc_item_limit(hand[0])>1 and hand[1]+target[1]<=dll.mc_item_limit(hand[0]):hand[1]+=target[1];target[1]=0
            if target[1]<=0:clear(target)
            if hand[1]<=0:clear(hand)
            dll.mc_inventory_click(slot,right)
            assert state()==[tuple(s) for s in model]
            assert total()==original_total
        assert load(player_nbt([stack_nbt(9,262,32),stack_nbt(0,262,60),stack_nbt(36,265)]))==1
        assert dll.mc_inventory_quick_transfer(9)==1 and slots[0].count==64 and slots[1].count==28
        assert dll.mc_inventory_quick_transfer(0)==1 and slots[9].count==64
        # Fixed tag arena overflow must fail visibly, not discard compounds.
        huge=compound([named(7,'data',struct.pack('>i',17000)+b'\x55'*17000)])
        assert load(player_nbt([stack_nbt(0,262,1,0,huge)]))==0 and integer('mc_inventory_error').value==-2 and not integer('mc_inventory_ready').value
        # Read the local owned save directly, comparing all forty source slots.
        spec=importlib.util.spec_from_file_location('nbt_oracle',PROJECT/'tests/test_nbt.py');oracle=importlib.util.module_from_spec(spec);spec.loader.exec_module(oracle)
        owned=0
        for path in (PROJECT/'MC10/saves').glob('*/level.dat'):
            raw=gzip.decompress(path.read_bytes());_,root=oracle.parse(raw);player=root['Data'][1].get('Player')
            if player is None:continue
            value=player[1];expected=[(0,0,0)]*40
            for entry in value.get('Inventory',(9,(10,[])))[1][1]:
                slot=entry['Slot'][1]&255;slot=slot-64 if 100<=slot<104 else slot if slot<36 else -1
                item=entry['id'][1];count=entry['Count'][1]
                if slot>=0 and count>0 and dll.mc_item_valid(item):expected[slot]=(item,count,entry.get('Damage',(2,0))[1])
            # Locate the original Player compound in level Data while the C
            # parser is valid; production world loading uses the same boundary.
            dll.mc_nbt_parse.argtypes=[C.c_void_p,C.c_uint,C.POINTER(C.c_int)]
            root_id=C.c_int();buffer=C.create_string_buffer(raw);assert dll.mc_nbt_parse(buffer,len(raw),C.byref(root_id))==0
            dll.mc_nbt_compound_find.argtypes=[C.c_int,C.c_char_p]
            data_id=dll.mc_nbt_compound_find(root_id.value,b'Data');player_id=dll.mc_nbt_compound_find(data_id,b'Player')
            assert dll.mc_inventory_load_player_nbt(player_id)==1
            assert [(s.id,s.count,s.damage) for s in slots]==expected;owned+=1
        # Actual asset reader populates every cache before shared terrain scratch
        # is overwritten; compare unchanged pixels after complete scratch reuse.
        assert load(initial)==1
        pack_buffer=C.create_string_buffer(pack);dll.fixture_set_pack(pack_buffer,len(pack))
        copy_global('mc_menu_font_mask',image('font/default.png')[3]);copy_global('mc_menu_font_metrics',payload('mc10/font/default_metrics'));dll.mc_menu_prepare_glyph_cache()
        assert dll.mc_player_ui_init()==1
        assert integer('mc_ui_pixel_used').value*4<42000 and integer('mc_ui_mask_used').value*4<5600
        dll.fixture_reset_pixels();dll.mc_hud_draw();first=bytes(pixels)
        C.memset(C.addressof((C.c_uint*19200).in_dll(dll,'mc_asset_pixels')),0xaa,76800)
        C.memset(C.addressof((C.c_uint*2400).in_dll(dll,'mc_asset_mask')),0x55,9600)
        dll.fixture_reset_pixels();dll.mc_hud_draw();assert bytes(pixels)==first
        assert all(value==0x01000000+i for i,value in enumerate(depth))
        # Independent source-coordinate / mask / padding pixel oracle.
        metrics=payload('mc10/font/default_metrics');font_mask=struct.unpack('<512I',image('font/default.png')[3])
        def color(rgb):return ((rgb>>16&255)>>5)<<5|((rgb>>8&255)>>5)<<2|((rgb&255)>>6)
        def text(frame,string,x,y,rgb,shadow=False):
            if shadow:text(frame,string,x+1,y+1,(rgb&0xfcfcfc)>>2)
            for g in string.encode():
                for r in range(8):
                    for c in range(8):
                        bit=((g>>4)*8+r)*128+(g&15)*8+c
                        if font_mask[bit>>5]&(1<<(31-(bit&31))) and 0<=x+c<320 and 0<=y+r<240:frame[(y+r)*320+x+c]=color(rgb)
                x+=metrics[g]
        def blit(frame,name,u,v,w,h,x,y,tint=0xffffff):
            iw,ih,data,mask=image(name);words=struct.unpack('<'+'I'*(len(mask)//4),mask) if mask else []
            tint=color(tint)
            for r in range(h):
                for c in range(w):
                    bit=(v+r)*iw+u+c
                    if (not words or words[bit>>5]&(1<<(31-(bit&31)))) and 0<=x+c<320 and 0<=y+r<240:
                        pixel=data[(v+r)*((iw+3)&~3)+u+c]
                        frame[(y+r)*320+x+c]=((((pixel>>5)*(tint>>5)+3)//7)<<5)|((((pixel>>2&7)*(tint>>2&7)+3)//7)<<2)|((pixel&3)*(tint&3)+1)//3
        def item(frame,s,x,y):
            if s.count<=0:return
            tile=dll.mc_item_icon(s.id,s.damage)
            if tile>=0:blit(frame,'terrain.png' if s.id<256 else 'gui/items.png',(tile&15)*16,(tile>>4)*16,16,16,x,y,dll.mc_item_icon_color(s.id,s.damage))
            else:text(frame,'?',x+5,y+4,0xffff55)
            if s.count>1:
                number=str(s.count);text(frame,number,x+17-sum(metrics[g] for g in number.encode()),y+9,0xffffff,True)
            maximum=dll.mc_item_max_damage(s.id)
            if maximum>0 and s.damage>0:
                length=max(0,min(13,(26*maximum-s.damage*26+maximum)//(2*maximum)));green=max(0,min(255,(510*maximum-s.damage*510+maximum)//(2*maximum)))
                for width,height,rgb in [(13,2,0),(12,1,((255-green)//4<<16)|16128),(length,1,((255-green)<<16)|(green<<8))]:
                    for r in range(height):
                        for c in range(width):frame[(y+13+r)*320+x+2+c]=color(rgb)
        def hud_oracle():
            frame=bytearray((i*13+i//320)&255 for i in range(76800));selected=integer('mc_inventory_selected').value
            blit(frame,'gui/gui.png',0,0,182,22,69,218);blit(frame,'gui/gui.png',0,22,24,22,68+selected*20,217);blit(frame,'gui/icons.png',0,0,16,16,153,113)
            blit(frame,'gui/icons.png',0,64,182,5,69,211);blit(frame,'gui/icons.png',0,69,91,5,69,211)
            armor=dll.mc_inventory_armor_value();health=integer('mc_inventory_health').value;food=integer('mc_inventory_food').value;hy=45 if integer('mc_hud_hardcore').value else 0
            for i in range(10):
                if armor:blit(frame,'gui/icons.png',34 if i*2+1<armor else 25 if i*2+1==armor else 16,9,9,9,69+i*8,191)
                blit(frame,'gui/icons.png',16,hy,9,9,69+i*8,201)
                if i*2+1<=health:blit(frame,'gui/icons.png',52 if i*2+1<health else 61,hy,9,9,69+i*8,201)
            for i in range(10):
                blit(frame,'gui/icons.png',16,27,9,9,242-i*8,201)
                if i*2+1<=food:blit(frame,'gui/icons.png',52 if i*2+1<food else 61,27,9,9,242-i*8,201)
            if integer('mc_hud_underwater').value:
                full=(290-2)*10//300+bool((290-2)*10%300);partial=(2900+299)//300-full
                for i in range(full+partial):blit(frame,'gui/icons.png',16 if i<full else 25,18,9,9,242-i*8,191)
            for i in range(9):item(frame,slots[i],72+i*20,221)
            number='7';x=(320-sum(metrics[g] for g in number.encode()))//2
            for dx,dy in ((1,0),(-1,0),(0,1),(0,-1)):text(frame,number,x+dx,205+dy,0)
            text(frame,number,x,205,8453920);return bytes(frame)
        for selected in range(9):
            dll.mc_inventory_select(selected);dll.fixture_reset_pixels();dll.mc_hud_draw();assert bytes(pixels)==hud_oracle(),('HUD pixel oracle',selected)
        integer('mc_hud_hardcore').value=1;integer('mc_hud_underwater').value=1;dll.fixture_reset_pixels();dll.mc_hud_draw();assert bytes(pixels)==hud_oracle()
        # Actual inventory panel, storage layout/counts, carried stack, source
        # title and explicit unsupported-dependency labels are all pixel checked.
        integer('mc_menu_pointer_x').value=0;integer('mc_menu_pointer_y').value=0;dll.mc_inventory_draw();actual=bytes(pixels)
        frame=bytearray([36])*76800;blit(frame,'gui/inventory.png',0,0,176,166,72,37)
        for i,s in enumerate(slots):item(frame,s,dll.mc_inventory_slot_x(i),dll.mc_inventory_slot_y(i))
        for string,x,y,rgb in [('Crafting',158,53,4210752),('pending',162,94,0xaa0000),('Preview',103,67,0xa0a0a0),('pending',103,77,0xa0a0a0)]:text(frame,string,x,y,rgb)
        for string,y,rgb in [('Enter: left   Space: right   Shift: quick move',210,0xffffff),('? = item visual pending; changes are not saved',224,0xa0a0a0)]:text(frame,string,160-sum(metrics[g] for g in string.encode())//2,y,rgb,True)
        assert actual==frame,'inventory original panel / slots / text pixel oracle'
        # UI event state machine freezes gameplay (only its own clock runs),
        # blocks source-drop-dependent close with a cursor stack, then closes.
        assert load(initial)==1
        integer('mc_menu_pointer_x').value=80;integer('mc_menu_pointer_y').value=179
        keys=(C.c_int*32).in_dll(dll,'fixture_script_keys');actions=(C.c_int*32).in_dll(dll,'fixture_script_actions')
        for i,(key,action) in enumerate(((0x28,1),(0x28,2),(0x29,1),(0x29,2),(0x28,1),(0x28,2),(0x29,1))):keys[i]=key;actions[i]=action
        integer('fixture_script_count').value=7;dll.fixture_script_reset();before=state();assert dll.mc_inventory_run()==1 and state()==before
        # Real HID->LWJGL translation and held-key gating honor a rebound R
        # binding, not the old hard-coded E key. Release of R is required;
        # unrelated held E must not prevent closing with a fresh R press.
        fixture_held=(C.c_int*256).in_dll(dll,'fixture_held')
        for initial_key,script,clock in [(0x15,[(0x15,1),(0x15,2),(0x15,1)],6),(0x08,[(0x15,1)],2)]:
            dll.fixture_script_reset()
            (C.c_int*13).in_dll(dll,'mc_options')[9]=19
            fixture_held[initial_key]=1
            for i,(key,action) in enumerate(script):keys[i]=key;actions[i]=action
            integer('fixture_script_count').value=len(script)
            closed=dll.mc_inventory_run();assert closed==1 and integer('fixture_clock').value==clock,(initial_key,closed,integer('fixture_clock').value,clock,dll.mc_inventory_close_key_held())
        # Audited non-3D ItemBlock sprites use terrain, never the item atlas or
        # a made-up generic cube. Source tint is constant inventory foliage.
        flat=[(6,0,15,0xffffff),(6,1,63,0xffffff),(6,2,79,0xffffff),(27,0,179,0xffffff),(28,0,195,0xffffff),(30,0,11,0xffffff),(31,0,55,0xffffff),(31,1,39,4764952),(31,2,56,4764952),(32,0,55,0xffffff),(37,0,13,0xffffff),(38,0,12,0xffffff),(39,0,29,0xffffff),(40,0,28,0xffffff),(50,0,80,0xffffff),(55,0,164,0xffffff),(59,0,88,0xffffff),(64,0,97,0xffffff),(65,0,83,0xffffff),(66,0,128,0xffffff),(69,0,96,0xffffff),(71,0,98,0xffffff),(75,0,115,0xffffff),(76,0,99,0xffffff),(77,0,1,0xffffff),(83,0,73,0xffffff),(101,0,85,0xffffff),(102,0,49,0xffffff),(106,0,143,4764952),(111,0,76,2129968)]
        assert load(player_nbt([stack_nbt(i,block,2,damage) for i,(block,damage,tile,tint) in enumerate(flat)]))==1
        assert dll.mc_player_ui_init()==1 and integer('mc_ui_icon_used').value==len(flat)
        assert dll.mc_item_icon(1,0)==-1 and dll.mc_item_icon(44,0)==-1 and dll.mc_item_icon(54,0)==-1,'unported 3D shapes must remain explicit'
        for i,(block,damage,tile,tint) in enumerate(flat):
            assert dll.mc_item_icon(block,damage)==tile and dll.mc_item_icon_color(block,damage)==tint
            for x,y in ((20,30),(-5,233)):
                dll.fixture_reset_pixels();frame=bytearray(pixels)
                item(frame,slots[i],x,y);dll.mc_ui_draw_stack(C.byref(slots[i]),x,y)
                assert bytes(pixels)==frame,('flat block pixel/mask/tint/clip',block,damage,x,y)
        assert integer('fixture_writes').value==0,'inventory/HUD must never write the SD card'
        if os.name=='nt':_ctypes.FreeLibrary(dll._handle);dll._handle=0
    print(f'Player foundation PASS: source item facts; independent NBT/tags ({owned} owned save); 1800 exact/conserving clicks; shift/armor/drop/capacity gates; 11 full HUD/inventory +60 flat block pixel oracles; scratch reuse; rebound GUI close gating; zero SD writes')

if __name__=='__main__':run()
