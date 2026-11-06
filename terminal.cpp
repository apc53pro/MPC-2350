#include "terminal_grid.h"
#include "gpu_display.h"
#include "globals.h"
#include "terminal.h"
#include "terminal_funcs.h"
#include "keyboard_runtime.h"

struct terminal_aspects terminal_aspects;

int lastInputLine = 3;

const int inputHistoryAmount = 5;
String inputHistoryStr[inputHistoryAmount];
int inputHistoryPtr = 0;

int leftBorder = 2;
int rightBorder = 39;

void scroll_terminal_up(int times)
{
  for(int k = 0; k < times; k++)
  {
    for(int i = 1; i < 60; i++)
    {
      for(int j = 0; j < 40; j++)
      {
        gridBuffer_char[j][i-1] = gridBuffer_char[j][i];
      }
    }

    for(int i = 0; i < 40; i++)
    {
      gridBuffer_char[i][59] = ' ';
    }
  }

}

int textLinePtr = 0;

void draw_line_pointer(int x, int y, bool show_line_pointer)
{
  if(terminal_aspects.LP.drawn == false && show_line_pointer == true)
  {
    terminal_write_grid(gridBuffer_char, x, y, terminal_aspects.LP.line_pointer_str);
    terminal_aspects.LP.drawn = true;
  }
}

void echo_message(String message)
{
  Serial.println(message);
  gridBuffer_char[terminal_aspects.cur.locationX][terminal_aspects.cur.locationY] = ' ';
  terminal_write_grid(gridBuffer_char, 0, terminal_aspects.cur.locationY, message);
  
  if(terminal_aspects.cur.locationY < 58)
  {
    terminal_aspects.cur.locationY++;
    lastInputLine = terminal_aspects.cur.locationY;

    if(terminal_aspects.cur.locationY > 30 + char_grid_buffer_V_draw_offset - 2 && char_grid_buffer_V_draw_offset < 30)
    {
      char_grid_buffer_V_draw_offset = terminal_aspects.cur.locationY - 30 + 2;
    }
  }else
  {
    scroll_terminal_up(1);

    if(terminal_aspects.cur.locationY > 30 + char_grid_buffer_V_draw_offset - 2 && char_grid_buffer_V_draw_offset < 30)
    {
      char_grid_buffer_V_draw_offset = terminal_aspects.cur.locationY - 30 + 2;
    }
  }
  
  terminal_aspects.LP.drawn = false;
}

char textChar; // holds the last char at cursor coordinates

static KeyboardEvent terminal_key_event;
static bool terminal_key_event_pending = false;

static inline __attribute__((always_inline)) bool terminalKeyEventPending()
{
  if (terminal_key_event_pending) return true;

  // The terminal acts only on key-down activity. Release and modifier events
  // still update the shared state bitmap, but must not consume whole terminal
  // frames or build up behind printable input.
  while (keyboard_poll_event(KEYBOARD_FOCUS_TERMINAL, terminal_key_event))
  {
    const uint16_t key = terminal_key_event.key;
    const bool modifier =
      key == KEYBOARD_KEY_LEFT_SHIFT || key == KEYBOARD_KEY_RIGHT_SHIFT ||
      key == KEYBOARD_KEY_LEFT_CONTROL || key == KEYBOARD_KEY_RIGHT_CONTROL ||
      key == KEYBOARD_KEY_LEFT_ALT || key == KEYBOARD_KEY_RIGHT_ALT ||
      key == KEYBOARD_KEY_LEFT_GUI || key == KEYBOARD_KEY_RIGHT_GUI ||
      key == KEYBOARD_KEY_CAPS_LOCK || key == KEYBOARD_KEY_NUM_LOCK;
    if (terminal_key_event.action != KEYBOARD_ACTION_RELEASE && !modifier)
    {
      terminal_key_event_pending = true;
      break;
    }
  }
  return terminal_key_event_pending;
}

static inline __attribute__((always_inline)) void terminalKeyEventConsume()
{
  terminal_key_event_pending = false;
}

void userInput()
{
  if(!terminalKeyEventPending()) return;

  if(terminal_key_event.character != 0 &&
     terminal_key_event.action != KEYBOARD_ACTION_RELEASE)
  {
       char c = (char)terminal_key_event.character;

       terminal_write_grid(gridBuffer_char, terminal_aspects.cur.locationX, terminal_aspects.cur.locationY, String(c));
    
       if(textLinePtr >= line.length())
       {
         line.concat(" ");
       }
       line.setCharAt(textLinePtr, c);

       textLinePtr++;

       terminal_aspects.cur.locationX++;

       if(terminal_aspects.cur.locationX > rightBorder)
       {
         terminal_aspects.cur.locationX = leftBorder;
         terminal_aspects.cur.locationY++;
       }

       if(terminal_aspects.cur.locationY > 58)
       {
         terminal_aspects.cur.locationY--;
         terminal_aspects.cur.locationX = leftBorder;

         scroll_terminal_up(1);
         if(terminal_aspects.cur.locationY > 30 + char_grid_buffer_V_draw_offset - 2 && char_grid_buffer_V_draw_offset < 30)
         {
           char_grid_buffer_V_draw_offset = terminal_aspects.cur.locationY - 30 + 2;
         }
       }

       terminalKeyEventConsume();
       
  }else if(terminal_key_event.action != KEYBOARD_ACTION_RELEASE &&
           terminal_key_event.key == KEYBOARD_KEY_BACKSPACE)
  {
       if(terminal_aspects.cur.locationX > leftBorder || terminal_aspects.cur.locationY > lastInputLine)
       {
         textLinePtr--;
         line.setCharAt(textLinePtr, ' ');
         

         terminal_aspects.cur.locationX--;
         gridBuffer_char[terminal_aspects.cur.locationX+1][terminal_aspects.cur.locationY] = textChar;
         if(terminal_aspects.cur.locationX < leftBorder)
         {
           terminal_aspects.cur.locationX = rightBorder;
           terminal_aspects.cur.locationY--;
         }

         if(terminal_aspects.cur.locationY < 0)
         {
           terminal_aspects.cur.locationY = 0;
           terminal_aspects.cur.locationX = leftBorder;
         }

         gridBuffer_char[terminal_aspects.cur.locationX][terminal_aspects.cur.locationY] = ' ';
       }

       terminalKeyEventConsume();
       
  }else if(terminal_key_event.action != KEYBOARD_ACTION_RELEASE &&
           terminal_key_event.key == KEYBOARD_KEY_ENTER)
  {
       
    gridBuffer_char[terminal_aspects.cur.locationX][terminal_aspects.cur.locationY] = textChar;
    if(terminal_aspects.cur.locationY < 58)
    {
      terminal_aspects.cur.locationY++;

      if(terminal_aspects.cur.locationY > 30 + char_grid_buffer_V_draw_offset - 2 && char_grid_buffer_V_draw_offset < 30)
      {
        char_grid_buffer_V_draw_offset = terminal_aspects.cur.locationY - 30 + 2;
      }
    }else
    {
      gridBuffer_char[terminal_aspects.cur.locationX][terminal_aspects.cur.locationY] = textChar;
      scroll_terminal_up(1);

      if(terminal_aspects.cur.locationY > 30 + char_grid_buffer_V_draw_offset - 2 && char_grid_buffer_V_draw_offset < 30)
      {
        char_grid_buffer_V_draw_offset = terminal_aspects.cur.locationY - 30 + 2;
      }
    }

    Serial.print("\nLINE: ");
    line.trim();
    Serial.print(line);
    lastInputLine = terminal_aspects.cur.locationY;
    terminal_aspects.cur.locationX = leftBorder;
    terminal_funcs(line);

    /*for(int j = inputHistoryAmount; j > 0; j--;)
    {
      inputHistoryStr[j] = inputHistoryStr[j-1];
    }*/

    line = "";
    textLinePtr = 0;
    terminal_aspects.LP.drawn = false;

    terminalKeyEventConsume();
       
  }else
  {
   terminalKeyEventConsume();
  }

}

int prevCurX = 0;
int prevCurY = 0;

void blinkCursor(int x, int y, bool active)
{
  if (!active || x < 0 || x >= 40 || y < 0 || y >= 60) return;
  if ((uint32_t)(millis() - terminal_aspects.cur.ms) > (uint32_t)terminal_aspects.cur.blink_time_ms) {
    terminal_aspects.cur.showing = !terminal_aspects.cur.showing;
    terminal_aspects.cur.ms = millis();
  }
  if (prevCurX != x || prevCurY != y) {
    textChar = gridBuffer_char[x][y];
    prevCurX = x; prevCurY = y;
  }
  gridBuffer_char[x][y] = terminal_aspects.cur.showing ? terminal_aspects.cur.symbol : textChar;
}

void controls()
{
  if (!terminalKeyEventPending()) return;

  const uint16_t key = terminal_key_event.key;
  const uint8_t action = terminal_key_event.action;

  if (action != KEYBOARD_ACTION_PRESS && action != KEYBOARD_ACTION_REPEAT)
    return;

  if (key == KEYBOARD_KEY_UP)
  {
    if(char_grid_buffer_V_draw_offset > 0)
    {
      char_grid_buffer_V_draw_offset--;
    }
    terminalKeyEventConsume();
    return;
  }

  if (key == KEYBOARD_KEY_DOWN)
  {
    if(char_grid_buffer_V_draw_offset < 30)
    {
      char_grid_buffer_V_draw_offset++;
    }
    terminalKeyEventConsume();
    return;
  }

  if (key == KEYBOARD_KEY_LEFT)
  {
    if(textLinePtr > 0 &&
       (terminal_aspects.cur.locationX > leftBorder || terminal_aspects.cur.locationY > lastInputLine))
    {
      gridBuffer_char[terminal_aspects.cur.locationX][terminal_aspects.cur.locationY] = textChar;
      terminal_aspects.cur.locationX--;

      if(terminal_aspects.cur.locationY > lastInputLine && terminal_aspects.cur.locationX < leftBorder)
      {
        terminal_aspects.cur.locationY--;
        terminal_aspects.cur.locationX = rightBorder;
      }

      textLinePtr--;
    }

    terminalKeyEventConsume();
    return;
  }

  if (key == KEYBOARD_KEY_RIGHT)
  {
    if(textLinePtr >= line.length())
    {
      line.concat(" ");
    }

    gridBuffer_char[terminal_aspects.cur.locationX][terminal_aspects.cur.locationY] = textChar;
    terminal_aspects.cur.locationX++;
    textLinePtr++;

    if(terminal_aspects.cur.locationX > rightBorder)
    {
      terminal_aspects.cur.locationY++;
      terminal_aspects.cur.locationX = leftBorder;
    }

    terminalKeyEventConsume();
    return;
  }
}

void terminal(bool show, bool show_line_pointer, bool show_cursor)
{
  
  if(show == true)
  {

    draw_line_pointer(0, terminal_aspects.cur.locationY, show_line_pointer);

    controls();

    blinkCursor(terminal_aspects.cur.locationX, terminal_aspects.cur.locationY, show_cursor);    


    userInput();

    gpu_display_present();

  }
}
