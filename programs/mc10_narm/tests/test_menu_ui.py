"""Pixel oracle for actual options/pause UI using the user's owned packed font.

No game emulator: compile just the production UI functions against 2D service
stubs. Compare every color pixel, with an untouched populated depth buffer, in
both cached legacy-pixel and native-glyph modes. No owned inputs are exported.
"""
import ctypes as C
import _ctypes
import os
from pathlib import Path
import struct
import subprocess
import tempfile

PROJECT = Path(__file__).resolve().parents[1]
PRODUCTION = Path(os.environ.get('MC10_PROJECT', str(PROJECT)))
OPTIONS = Path(os.environ.get('MC10_TEST_OPTIONS', str(PROJECT/'src/mc10_options.tc')))


def function(source, name):
    start = source.rfind('\n', 0, source.index(name+'(')) + 1
    brace = source.index('{', start)
    end, depth = brace+1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]+'\n'


def records(blob):
    assert blob[:4] == b'MCA1'
    count, size, offset = struct.unpack_from('<III', blob, 8)
    assert size == 40
    return {row[0]: row for row in (struct.unpack_from('<10I', blob, offset+i*40) for i in range(count))}


def name_hash(name):
    result = 0x811c9dc5
    for byte in name.encode():
        result = ((result ^ byte)*0x01000193) & 0xffffffff
    return result


def run():
    blob = (PRODUCTION/'MC10/assets.mc10').read_bytes()
    directory = records(blob)

    def asset(name, mask=False):
        row = directory[name_hash(name)]
        offset, size = row[7:9] if mask else row[5:7]
        return blob[offset:offset+size]

    mask = struct.unpack('<512I', asset('font/default.png', True))
    metrics = asset('mc10/font/default_metrics')
    assert len(metrics) == 256
    menu = (PROJECT/'src/mc10_menu.tc').read_text()
    game = (PROJECT/'src/mc10_game.tc').read_text()
    menu_test = menu[:menu.index('void mc_menu_draw_rotated_glyph_pixel')]
    for name in ('mc_menu_button_hovered', 'mc_menu_button_pixels', 'mc_menu_draw_button', 'mc_menu_move_pointer', 'mc_menu_update_pointer_held', 'mc_menu_text_equals'):
        menu_test += function(menu, name)
    pause_test = 'struct MCMenuButton mc_game_buttons[5];\n'
    for name in ('mc_game_pause_button', 'mc_game_build_pause', 'mc_game_draw_pause'):
        pause_test += function(game, name)

    def rgb332(value):
        return ((value>>16 & 255)>>5)<<5 | ((value>>8 & 255)>>5)<<2 | ((value & 255)>>6)

    def draw_string(frame, text, x, y, color):
        for px, py, rgb in ((x+1, y+1, (color & 0xfcfcfc)>>2), (x, y, color)):
            packed = rgb332(rgb)
            for glyph in text.encode('ascii'):
                for row in range(8):
                    for col in range(8):
                        bit = ((glyph>>4)*8+row)*128+(glyph & 15)*8+col
                        if mask[bit>>5] & (1 << (31-(bit & 31))) and 0 <= px+col < 320 and 0 <= py+row < 240:
                            frame[(py+row)*320+px+col] = packed
                px += metrics[glyph]

    def centered(frame, text, x, y, color=0xe0e0e0):
        width = sum(metrics[g] for g in text.encode('ascii'))
        draw_string(frame, text, x-width//2, y, color)

    expected = {
        0: ['Music: 100%', 'Sound: 100%', 'Invert Mouse: OFF', 'Sensitivity: 100%', 'FOV: Normal', 'Difficulty: Normal', 'Video Settings...', 'Controls...', 'Done', 'Cursor Speed: 100%'],
        1: ['Graphics: Fancy', 'Render Distance: Far', 'Smooth Lighting: ON', 'Performance: Balanced', '3D Anaglyph: OFF', 'View Bobbing: ON', 'GUI Scale: Auto', 'Advanced OpenGL: OFF', 'Brightness: Moody', 'Clouds: ON', 'Particles: All', 'Done', 'Occlusion Culling: OFF'],
        2: ['Button 1', 'Button 2', 'W', 'A', 'S', 'D', 'SPACE', 'LSHIFT', 'Q', 'E', 'T', 'TAB', 'Button 3', 'Done'],
    }
    control_labels = ['Attack', 'Use Item', 'Forward', 'Left', 'Back', 'Right', 'Jump', 'Sneak', 'Drop', 'Inventory', 'Chat', 'List Players', 'Pick Block']
    cases = 0
    with tempfile.TemporaryDirectory(prefix='mc10-ui-') as temporary:
        temporary = Path(temporary)
        (temporary/'menu_ui_under_test.tc').write_text(menu_test)
        (temporary/'options_ui_under_test.tc').write_text(OPTIONS.read_text())
        (temporary/'pause_ui_under_test.tc').write_text(pause_test)
        for legacy in (False, True):
            dllpath = temporary/('legacy.dll' if legacy else 'native.dll')
            command = ['gcc', '-shared', '-O2'] + (['-DMC10_LEGACY_FONT'] if legacy else [])
            command += [str(PROJECT/'tests/menu_ui_fixture.c'), '-I', str(temporary), '-I', str(PRODUCTION.parents[3]/'Compiler_For_NARM'), '-o', str(dllpath)]
            result = subprocess.run(command, capture_output=True, text=True)
            assert result.returncode == 0, result.stderr
            dll = C.CDLL(str(dllpath))
            pixels = (C.c_ubyte*(320*240)).in_dll(dll, 'fixture_pixels')
            depth = (C.c_uint*(320*240)).in_dll(dll, 'fixture_depth')

            def load(name, data, offset=0):
                target = (C.c_ubyte*(len(data)+offset)).in_dll(dll, name)
                C.memmove(C.addressof(target)+offset, data, len(data))

            load('mc_menu_font_mask', asset('font/default.png', True))
            load('mc_menu_font_metrics', metrics)
            dll.narm_half_blit.argtypes = [C.c_int, C.c_int, C.c_int, C.c_int, C.POINTER(C.c_uint)]
            for width, stride in ((200, 4000), (98, 2000), (150, 3040), (70, 1440)):
                for state, suffix in enumerate(('disabled', 'normal', 'hover')):
                    data = asset(f'mc10/menu/button_{width}x20_{suffix}')
                    assert len(data) == ((width+3)//4)*4*20
                    load('mc_menu_button_'+str(width), data, state*stride)
                    # Independent packed-asset oracle: visible rows skip the
                    # word-alignment bytes. Test unclipped and clipped blits,
                    # not just a text overlay on the fixture's own background.
                    source = (C.c_uint*(len(data)//4)).from_buffer_copy(data)
                    for left, top in ((13,17),(-3,233)):
                        dll.fixture_reset(0)
                        wanted = bytearray(pixels)
                        for row in range(20):
                            for col in range(width):
                                if 0 <= left+col < 320 and 0 <= top+row < 240:
                                    wanted[(top+row)*320+left+col] = data[row*((width+3)&~3)+col]
                        dll.narm_half_blit(left, top, width, 20, source)
                        assert bytes(pixels) == wanted, ('packed asset row padding', legacy, width, state, left, top)
            load('mc_options_slider_knob', asset('mc10/options/slider_knob_8x20'))
            # GuiScreen uses UV (screen_x/32, screen_y/32), not one source
            # pixel per screen pixel. The owned 16x16 texture repeats every
            # 32 screen pixels, so nearest sampling expands each pixel 2x.
            background_source = asset('gui/background.png')
            background_width, background_height = directory[name_hash('gui/background.png')][2:4]
            assert (background_width, background_height) in ((16,16),(32,32))
            background_stride = (background_width+3)&~3
            background = bytearray()
            for row in range(32):
                for col in range(32):
                    c = background_source[(row*background_height//32)*background_stride+col*background_width//32]
                    background.append(((((c>>5)>>2)<<5)|((((c>>2)&7)>>2)<<2)|((c&3)>>2)))
            assert len(background) == 1024
            load('mc_options_background', bytes(background))
            dll.fixture_reset(0)
            dll.mc_options_draw_background()
            wanted = bytes(background[(y%32)*32+x%32] for y in range(240) for x in range(320))
            assert bytes(pixels) == wanted, ('owned background 32px texture period', legacy)
            labels = asset('mc10/options/labels')
            load('mc_options_labels', labels)
            C.c_uint.in_dll(dll, 'mc_options_label_bytes').value = len(labels)
            dll.mc_menu_prepare_glyph_cache()
            dll.mc_options_defaults()
            C.c_int.in_dll(dll, 'mc_menu_pointer_x').value = 0
            C.c_int.in_dll(dll, 'mc_menu_pointer_y').value = 0
            dll.fixture_button_text.restype = C.c_char_p

            for screen in range(3):
                dll.fixture_draw_options(screen, 0)
                wanted = bytearray(pixels)
                centered(wanted, ('Options', 'Video Settings', 'Controls')[screen], 160, 20, 0xffffff)
                if screen == 1:
                    centered(wanted, 'A 64 bit Java installation is recommended', 160, 184, 0xaf0000)
                    centered(wanted, "for 'Far' render distance (you have 32 bit)", 160, 196, 0xaf0000)
                for i, text in enumerate(expected[screen]):
                    # Full expected strings are source-derived independently of the
                    # formatter. In particular, values alone must never pass.
                    assert dll.fixture_button_text(i).decode() == text, (screen, i, dll.fixture_button_text(i), text)
                    x, y, width = (dll.fixture_button_word(i, word) for word in range(3))
                    centered(wanted, text, x+width//2, y+6)
                    if screen == 2 and i < 13:
                        draw_string(wanted, control_labels[i], x+76, y+7, 0xffffff)
                dll.fixture_draw_options(screen, 1)
                assert bytes(pixels) == wanted, ('UI font/color/placement mismatch', legacy, screen)
                assert all(value == 0x01000000+i for i, value in enumerate(depth)), 'UI changed terrain depth'
                assert C.c_int.in_dll(dll, 'fixture_text_pixels').value > 1500
                cases += 1

            dll.fixture_draw_pause(0)
            wanted = bytearray(pixels)
            centered(wanted, 'Game menu', 160, 40, 0xffffff)
            for text, x, y, color in [('Back to game',160,74,0xe0e0e0), ('Achievements',109,98,0xa0a0a0), ('Statistics',211,98,0xa0a0a0), ('Options...',160,146,0xe0e0e0), ('Exit without saving',160,170,0xe0e0e0)]:
                centered(wanted, text, x, y, color)
            centered(wanted, 'World saving is not implemented', 160, 210, 0xa0a0a0)
            dll.fixture_draw_pause(1)
            assert bytes(pixels) == wanted, ('pause full text / disabled colors / placement', legacy)
            assert all(value == 0x01000000+i for i, value in enumerate(depth))
            cases += 1

            # Speed is a presentation-only rate; preserve two-pixel movement.
            held = (C.c_int*256).in_dll(dll, 'fixture_held')
            held[0x4f] = 1
            dll.mc_menu_set_cursor_speed.argtypes = [C.c_float]
            for setting, interval in ((0,32), (.5,8), (1,4), (-1,32), (2,4), (float('nan'),8)):
                dll.mc_menu_set_cursor_speed(setting)
                assert C.c_uint.in_dll(dll, 'mc_menu_pointer_interval_ms').value == interval
                C.c_uint.in_dll(dll, 'mc_menu_pointer_last_move').value = 100
                C.c_int.in_dll(dll, 'mc_menu_pointer_x').value = 160
                assert dll.mc_menu_update_pointer_held(100+interval-1) == 0
                assert dll.mc_menu_update_pointer_held(100+interval) == 1
                assert C.c_int.in_dll(dll, 'mc_menu_pointer_x').value == 162
            if os.name == 'nt':
                _ctypes.FreeLibrary(dll._handle)
                dll._handle = 0
    print(f'Menu UI pixels: PASS {cases} complete options/video/controls/pause screens in legacy/native font paths, full option prefixes, live depth preserved; 48 owned button row-padding/clipping cases, 32px background period, six cursor rate/clamp cases')


if __name__ == '__main__':
    run()
