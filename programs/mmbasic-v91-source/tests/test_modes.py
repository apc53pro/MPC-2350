"""Run the real compiled interpreter as Thumb, recording native video calls."""
from pathlib import Path
import collections
import struct
from PIL import Image, ImageDraw
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_R3, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_SP,
    UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_FPEXC)

ROOT = Path(__file__).resolve().parent
PALETTE = [255, 0, 2, 16, 224, 128, 97, 236, 252, 28, 9, 159, 7, 227, 73, 146]

def rgb(c):
    r, g, b = c >> 5, (c >> 2) & 7, c & 3
    return ((r << 5) | (r << 2) | (r >> 1),
            (g << 5) | (g << 2) | (g >> 1), b * 85)

def event(kind, x, y, dx=0, dy=0, wheel=0, buttons=0, changed=0):
    return struct.pack('<IiiiiiIII', kind, x, y, dx, dy, wheel, buttons, changed, 0)

def click(x, y):
    return [event(2, x, y, buttons=1, changed=1), event(4, x, y, changed=1)]

def execute(scenario):
    data = (ROOT/'modes_probe.narm').read_bytes()
    u32 = lambda n: struct.unpack_from('<I', data, n)[0]
    align = lambda n, a: (n+a-1) & ~(a-1)
    text, arena, ctx, stack = 0x100000, 0x2000000, 0x3000000, 0x4000000
    names = ['stop', 'invoke', 'get', 'state', 'poll', 'motion', 'keys',
             'submit', 'exit', 'clear4', 'pixel4', 'rect4', 'line4', 'present4','file']
    handlers = {name: ctx+0x1000+i*8 for i, name in enumerate(names)}
    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    for addr, size in ((text,align(u32(0x20),4096)), (arena,0x800000),
                       (ctx,0x2000), (stack,0x10000)):
        cpu.mem_map(addr,size)
    cpu.mem_write(text, data[u32(0x1c):u32(0x1c)+u32(0x20)])
    ro_size = u32(0x2c)
    if ro_size: cpu.mem_write(arena,data[u32(0x28):u32(0x28)+ro_size])
    if u32(0x38): cpu.mem_write(arena+align(ro_size,u32(0x3c)),data[u32(0x34):u32(0x34)+u32(0x38)])
    def write32(addr, val): cpu.mem_write(addr,struct.pack('<I',val))
    for i in range(u32(0x54)):
        patch,target,kind,_ = struct.unpack_from('<IIHH',data,u32(0x50)+12*i)
        if kind:
            value = arena+target if kind == 1 else (text+target)|(kind==3) if kind in (2,3) else ctx
            write32(arena+patch,value)
    api, mouse, key, video = ctx+0x100,ctx+0x200,ctx+0x300,ctx+0x400
    for addr,val in ((ctx+8,api), (api+16,handlers['invoke']|1),
        (api+24,handlers['exit']|1), (api+28,handlers['get']|1),
        (ctx+52,key), (ctx+56,video), (key+8,handlers['keys']|1),
        (mouse+8,handlers['state']|1), (mouse+12,handlers['poll']|1),
        (mouse+16,handlers['motion']|1), (ctx+64,ctx+0x500), (ctx+0x508,handlers['file']|1)):
        write32(addr,val)
    for off,name in ((8,'clear4'),(12,'pixel4'),(16,'rect4'),(20,'line4'),(24,'present4'),(28,'submit')):
        write32(video+off,handlers[name]|1)
    for addr in handlers.values(): cpu.mem_write(addr,b'\x70\x47')
    for reg,val in ((UC_ARM_REG_R9,arena),(UC_ARM_REG_R10,ctx),(UC_ARM_REG_SP,stack+0xf000),
                    (UC_ARM_REG_LR,handlers['stop']|1),(UC_ARM_REG_FPEXC,1<<30)):
        cpu.reg_write(reg,val)
    by_address = {v:k for k,v in handlers.items()}
    queue = collections.deque()
    phase = ticks = frames = modal = 0
    main_key = False
    sd_bytes = 0
    bas = (ROOT.parent/'COLORMODES.BAS').read_bytes()
    counts = collections.Counter()
    commands = []
    presents = []
    screens = {5:Image.new('RGB',(320,240)), 3:Image.new('RGB',(640,480))}
    def signed(n): return n-2**32 if n & 2**31 else n
    def record(mode,op,args):
        nonlocal frames
        counts[(mode,op)] += 1
        if op in ('rect','line','pixel','glyph','tri'):
            color = args[-1]
            assert 0 <= color <= (15 if mode==3 else 255), (mode,op,args)
        commands.append((mode,op,args))
        screen = screens[mode]
        draw = ImageDraw.Draw(screen)
        def color(c): return rgb(PALETTE[c] if mode==3 else c)
        if op == 'clear':
            assert 0 <= args[0] <= (15 if mode==3 else 255)
            draw.rectangle((0,0,screen.width-1,screen.height-1),fill=color(args[0]))
        elif op == 'pixel':
            assert 0 <= args[0] < screen.width and 0 <= args[1] < screen.height
            draw.point(args[:2],fill=color(args[2]))
        elif op == 'rect':
            # Native spans in the triangle path can be reversed; firmware sorts them.
            x0,y0,x1,y1,c = args
            x0,x1 = sorted((x0,x1)); y0,y1 = sorted((y0,y1))
            draw.rectangle((x0,y0,x1,y1),fill=color(c))
        elif op == 'line': draw.line(args[:4],fill=color(args[4]))
        elif op == 'tri': draw.polygon([args[0:2],args[2:4],args[4:6]],fill=color(args[6]))
        elif op == 'glyph':
            x,y,w,h,addr,c = args
            words = struct.unpack(f'<{(w*h+31)//32}I',cpu.mem_read(addr,((w*h+31)//32)*4))
            for p in range(w*h):
                if words[p//32] >> (31-p%32) & 1:
                    draw.point((x+p%w,y+p//w),fill=color(c))
        elif op == 'present':
            frames += 1
            presents.append(mode)
            if scenario in (1,2,3,12): screen.save(ROOT/f'modes-{scenario}-{mode}-{frames}.png')

    def hook(uc,addr,size,_):
        nonlocal phase,ticks,modal,main_key,sd_bytes
        name = by_address[addr]
        if name == 'stop': uc.emu_stop(); return
        r0,r1,r2,r3 = (uc.reg_read(reg) for reg in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3))
        assert r0 == ctx, (name,hex(r0))
        result = 0
        if name == 'invoke':
            if r1 == 1: ticks += 10; result = ticks
            elif r1 == 3:
                phase += 1; result = scenario
                if scenario == 3:
                    queue.extend({2:[event(2,421,317,buttons=1,changed=1)],
                        3:[event(1,451,359,buttons=1),event(4,451,359,changed=1)],
                        4:[event(8,451,359,wheel=-2)],5:click(170,475)}.get(phase,[]))
                if scenario == 10 and phase in (2,3):
                    queue.append(event(1,639,479,dx=900,dy=-500))
            else: assert r1 in (0,12), r1
        elif name == 'get':
            assert (r1,r2,r3) == (0x0101,1,0); result = mouse
        elif name == 'state': cpu.mem_write(r1,struct.pack('<iiII',639,479,0,11))
        elif name == 'poll':
            if scenario == 11:
                if frames == 1 and modal == 0:
                    queue.extend(click(20,475)); modal = 1
                elif frames == 2 and modal == 1:
                    queue.clear(); modal = 2
                elif frames == 2 and modal == 2:
                    queue.extend(click(20,99)); modal = 3
                elif frames == 3 and modal == 3:
                    queue.extend(click(258,475)); modal = 4
            if queue: cpu.mem_write(r1,queue.popleft()); result = 1
        elif name == 'motion': cpu.mem_write(r1,struct.pack('<iii',900,-500,0))
        elif name == 'keys':
            if scenario == 12:
                if r1 == 0x29:
                    main_key = True
                    result = int(frames >= 6)
                if r1 == 0x2c:
                    result = int(main_key and frames in (2,4))
                    main_key = False
        elif name == 'file':
            args = struct.unpack(f'<{r3}I',uc.mem_read(r2,r3*4))
            if r1==0: result=1
            else:
                assert bytes(uc.mem_read(args[0],16)).split(b'\0')[0] == b'/COLORMODES.BAS'
                if r1==1: result=1
                elif r1==5: write32(args[2],len(bas))
                elif r1==6:
                    payload=bas[args[2]:args[2]+args[3]]
                    if payload: cpu.mem_write(args[1],payload)
                    result=len(payload); sd_bytes+=result
                else: raise AssertionError(('file',r1,args))
        elif name == 'exit': raise AssertionError(('unexpected exit',r1))
        elif name == 'submit':
            args = struct.unpack(f'<{r3}I',uc.mem_read(r2,r3*4)) if r3 else ()
            if r1 == 0: record(5,'present',())
            elif r1 == 1: record(5,'clear',args)
            elif r1 == 2: record(5,'pixel',(args[0]%320,args[0]//320,args[1]))
            elif r1 in (3,4,5):
                record(5,{3:'rect',4:'line',5:'tri'}[r1],tuple(map(signed,args[:-1]))+args[-1:])
            elif r1 in (8,0x100):
                record(5 if r1==8 else 3,'glyph',tuple(map(signed,args[:4]))+args[4:])
            else: raise AssertionError(('submit',r1,args))
        else:
            op = name[:-1]
            if op == 'present': record(3,'present',())
            else:
                n = {'clear':1,'pixel':3,'rect':5,'line':5}[op]
                args = struct.unpack(f'<{n}I',uc.mem_read(r1,n*4))
                record(3,op,tuple(map(signed,args[:-1]))+args[-1:])
        uc.reg_write(UC_ARM_REG_R0,result&0xffffffff)
        uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))

    cpu.hook_add(UC_HOOK_CODE,hook,begin=handlers['stop'],end=handlers['file'])
    cpu.emu_start((text+u32(0x18))|1,handlers['stop'],count=100_000_000)
    assert cpu.reg_read(UC_ARM_REG_PC) == handlers['stop'], ('timeout',scenario,frames,modal,ticks)
    assert cpu.reg_read(UC_ARM_REG_R0) == 0, ('result',scenario,cpu.reg_read(UC_ARM_REG_R0))
    if scenario == 1:
        assert (3,'pixel',(639,479,0)) in commands
        assert (5,'pixel',(319,239,255)) in commands
        assert not any(op=='rect' and args[:4]==(0,0,0,0) for mode,op,args in commands)
        assert (3,'rect',(610,450,639,479,12)) in commands
        assert presents == [3,5], presents
    if scenario == 2:
        assert presents == [3,3,5,5],presents
        assert (3,'pixel',(639,479,0)) in commands and (5,'pixel',(319,239,255)) in commands
        assert counts[(3,'line')] >= 4 and counts[(5,'line')] >= 4
        assert counts[(5,'tri')] == 3
        spans = [a for m,op,a in commands if m==3 and op=='rect' and a[1]==a[3] and 130<=a[1]<=150]
        assert len(spans) == 22, len(spans)
        assert (3,'rect',(90,130,90,130,9)) in commands
    if scenario == 3:
        assert frames == 1 and all(mode==3 for mode,op,args in commands)
        assert any(op=='glyph' and args[0]>320 and args[1]>240 for mode,op,args in commands)
    if scenario == 11: assert frames==4 and modal==4 and presents==[3]*4,(frames,modal,presents)
    if scenario == 12:
        assert presents==[3,3,5,5,3,3,5],presents
        assert sd_bytes==len(bas), (sd_bytes,len(bas))
        assert len(screens[3].getcolors(1000))==16
        # MODE 5 is cleared on exit. Inspect its previous published frame.
        low=Image.open(ROOT/'modes-12-5-4.png')
        assert len(low.getcolors(1000))==256
    print(f'Mode scenario {scenario} PASS ({frames} presents)',flush=True)

if __name__ == '__main__':
    for scenario in range(1,13): execute(scenario)
