#include "draw_funcs.h"
#include "globals.h"
#include "terminal.h"
#include "terminal_funcs.h"
#include "mms.h"
#include "program_runtime.h"
#include "SDmgr.h"
#include <FatFS.h>
#include "audio_runtime.h"
#include "native_runtime.h"
#include "usb_storage_mode.h"

String line;
uint32_t errors = 0;
String errorMessage;

int precedenceCheck(char op)
{
  if(op == '+')
  {
    return 2;
  }else if(op == '-')
  {
    return 2;
  }else if(op == '*')
  {
    return 3;
  }else if(op == '/')
  {
    return 3;
  }

  return -1;
}

int minZero(int n)
{
  if(n < 0)
  {
    return 0;
  }else
  {
    return n;
  }
}

double mathsSolver(String expr)
{
  expr.trim();

  int lastTokenType = -1;
  char lastToken;

  bool negateNum = false;

  int exprPtr = 0;

  String outStack[100];
  int outStackPtr = 0;

  String opStack[100];
  int opStackPtr = 0;

  while(exprPtr < (expr.length() ))
  {
    char op = expr.charAt(exprPtr);

    if(op == ' ')
    {
      exprPtr++;
    }else if(isDigit(op) || isAlpha(op))
    {
      lastToken = '$';
      bool alphaIncluded = false;

      if(lastTokenType == 0)
      {
        //Serial.print("\nInvalid Expression.");
        return -11;
      }

      lastTokenType = 0;

      int pCount = 0;

      while(isDigit(op) || op == '.' || isAlpha(op))
      {
        if(isAlpha(op))
        {
          alphaIncluded = true;
        }

        if(op == '.')
        {
          pCount++;
        }

        outStack[outStackPtr] = outStack[outStackPtr] + String(op); 
        exprPtr++;
        op = expr.charAt(exprPtr);
      }

      if(pCount > 1 && alphaIncluded == false)
      {
        //Serial.print("\nInvalid Number: ");
        
        //Serial.print(outStack[outStackPtr]);
        return -2;
      }

      if(pCount > 0 && alphaIncluded == true)
      {
        //Serial.print("\nInvalid Variable: ");
        
        //Serial.print(outStack[outStackPtr]);
        return -3;
      }

      if(outStack[outStackPtr].length() > 8 && alphaIncluded == true)
      {
        //Serial.print("\nInvalid Variable: ");
        
        //Serial.print(outStack[outStackPtr]);
        return -4;
      }

      if(alphaIncluded == true && negateNum == true)
      {
        //errors++;

        //errorMessage = "Invalid Expression: missplaced minus before variable.";
        
        return -5;
      }

      /*if(CHECK_VARIABLE_EXISTS(outStack[outStackPtr], "NULL") == -1 && alphaIncluded == true) //MAYBE WILL USE THIS LOGIC IN THE FUTURE AFTER DELETING THE OLD MEMORY MANAGEMENT SYSTEM?
      {
        //Serial.print("\nVariable does not exist: ");
        
        //Serial.print(outStack[outStackPtr]);
        return -6;
      }*/

      /*if(alphaIncluded == true)//MAYBE WILL USE THIS LOGIC IN THE FUTURE AFTER DELETING THE OLD MEMORY MANAGEMENT SYSTEM?
      {
        
        outStack[outStackPtr] = String(READ_VARIABLE(outStack[outStackPtr], -1));
        
      }else*/ if(negateNum == true)
      {
        outStack[outStackPtr] = String(outStack[outStackPtr].toDouble() * -1);
      }
      
      negateNum = false;
      outStackPtr++;
    }else if(op == '(')
    {
      opStack[opStackPtr] = String(op);
      
      opStackPtr++;
      exprPtr++;
    }else if(op == ')')
    {
      if(lastTokenType == 1)
      {
        //Serial.print("\nInvalid Expression.");
        return -7;
      }

      opStackPtr--;
      while(opStack[opStackPtr].charAt(0) != '(')
      {
        outStack[outStackPtr] += opStack[opStackPtr];

        outStackPtr++;

        opStackPtr--;
      }

      exprPtr++;

    }else if(op == '+' || op == '/' || op == '-' || op == '*')
    {
      if(lastTokenType == 1 && lastToken == '*' && op != '-' || lastTokenType == 1 && lastToken != '*')
      {
        //Serial.print("\nInvalid Expression.");
        return -8;
      }

      if(lastTokenType == -1 && op != '-')
      {
        //Serial.print("\nInvalid Expression.");
        return -9;
      }

      if(lastToken == '*' && op == '-' || op == '-' && lastTokenType == -1)
      {
        negateNum = true;
        exprPtr++;
      }else
      {
        lastTokenType = 1;

        if(precedenceCheck(opStack[minZero(opStackPtr-1)].charAt(0)) >= precedenceCheck(op))
        { 
          opStackPtr = minZero(opStackPtr-1);
          while(precedenceCheck(opStack[opStackPtr].charAt(0)) >= precedenceCheck(op))
          {
            outStack[outStackPtr] = opStack[opStackPtr];

            outStackPtr++;
            opStackPtr--;
          }
          
          opStackPtr++;
        }
        
        lastToken = op;
        opStack[opStackPtr] = String(op);
        opStackPtr++;
        exprPtr++;
      }
      
    }else
    {
      //Serial.print("\nInvalid Expression.");
      return -10;
    }
    
  }

  opStackPtr--;
  while(opStackPtr > -1)
  {
    outStack[outStackPtr] += opStack[opStackPtr];

    outStackPtr++;

    opStackPtr--;
  }

  String solvStack[100];
  int solvStackPtr = 0;

  for(int i = 0; i < outStackPtr; i++)
  {
    if(isDigit(outStack[i].charAt(1)) && outStack[i].charAt(0) == '-' || isDigit(outStack[i].charAt(0)))
    {
      solvStack[solvStackPtr] += outStack[i];
      solvStackPtr++;
    }else if(outStack[i].charAt(0) == '+' || outStack[i].charAt(0) == '/' || outStack[i].charAt(0) == '-' || outStack[i].charAt(0) == '*')
    {
      if(outStack[i].charAt(0) == '+')
      {
        solvStack[solvStackPtr-2] = String(solvStack[solvStackPtr-2].toDouble() + solvStack[solvStackPtr-1].toDouble());
      }else if(outStack[i].charAt(0) == '-')
      {
        solvStack[solvStackPtr-2] = String(solvStack[solvStackPtr-2].toDouble() - solvStack[solvStackPtr-1].toDouble());
      }else if(outStack[i].charAt(0) == '*')
      {
        solvStack[solvStackPtr-2] = String(solvStack[solvStackPtr-2].toDouble() * solvStack[solvStackPtr-1].toDouble());
      }else if(outStack[i].charAt(0) == '/')
      {
        solvStack[solvStackPtr-2] = String(solvStack[solvStackPtr-2].toDouble() / solvStack[solvStackPtr-1].toDouble());
      }

      solvStack[solvStackPtr-1] = "";

      solvStackPtr = solvStackPtr - 1;
    }
  }
  
  return solvStack[0].toDouble();
}

static String tokens[100];
static uint8_t tokenTypes[100];

uint8_t tokenTreeSize;

bool isValidSymbol(char ch)
{
  char symbols[32] = {'['   ,     ']'     ,   '('    ,    ')'   ,   ';'    ,   '-'   ,    '='    ,   '_'   ,    '+'   ,   '{'   ,   '}'    ,   '\\'   ,   '/'   ,   '!'   ,   '@'  ,   '#'  ,   '$'   ,   '%'   ,   '^'   ,   '&'   ,   '*'   ,   ':'   ,   ','   ,   '>'    ,    '<'   ,   '\''   ,    '\"'   ,   '|'   ,  '?'    ,  '`'    ,  '~'    ,  '.'};

  for(int i = 0; i < 32; i++)
  {
    if(ch == symbols[i])
    {
      return true;
    }
  }
  return false;
}

uint8_t codeTokenizer(String code)
{
  for(int i = 0; i < tokenTreeSize; i++)
  {
    tokens[i] = "";
  }

  code.trim();
  tokenTreeSize = 0;
  
  int codePtr = 0;
  uint8_t tokenPtr = 0;

  bool openQuotation = false;

  bool alphaIncluded = false;

  while(codePtr < code.length())
  {
    if(code.charAt(codePtr) == ' ' && openQuotation == false || code.charAt(codePtr) == '\n' && openQuotation == false || code.charAt(codePtr) == '\t' && openQuotation == false)
    {
      codePtr++;
    }else if(code.charAt(codePtr) == '\"')
    {
      openQuotation = !openQuotation;

      tokenTreeSize++;
      tokens[tokenPtr] = String(code.charAt(codePtr));
      tokenPtr++;
      codePtr++;
    }else if(openQuotation == true)
    {
      while(code.charAt(codePtr) != '\"' && codePtr < code.length())
      {
        tokens[tokenPtr] += String(code.charAt(codePtr));
        codePtr++;
      }

      tokenTreeSize++;
      tokenPtr++;
    }else if(isValidSymbol(code.charAt(codePtr)))
    {

      tokenTreeSize++;
      tokens[tokenPtr] = String(code.charAt(codePtr));
      tokenPtr++;
      codePtr++;
      
    }else if(isAlpha(code.charAt(codePtr)) || isDigit(code.charAt(codePtr)) || code.charAt(codePtr) == '.' || code.charAt(codePtr) == '_')
    {
      while(isAlpha(code.charAt(codePtr)) || isDigit(code.charAt(codePtr)) || code.charAt(codePtr) == '.' || code.charAt(codePtr) == '_')
      {
        tokens[tokenPtr] += String(code.charAt(codePtr));
        codePtr++;
      }
      
      tokenTreeSize++;
      tokenPtr++;
    }else
    {
      return -1;
    }

  }

  if(openQuotation == true)
  {
    Serial.print("\nCode is fucked because of missing double quotes.");
  }

  return 0;
}

void clearVBuffer(uint8_t color)
{
  clearVga4BitBuffer(color);
  presentVga4BitBuffer();
}

char gridBuffer_char[40][60];
uint8_t gridBuffer_charColor[40][60];
uint16_t gridBuffer_backColor[40][60];


int char_grid_buffer_V_draw_offset = 0;

int variableFormatValidator(String varName)
{
  varName.trim();

  if(varName.length() > 8)
  {
    return -1;
  }

  bool letterFirst = false;

  for(int i = 0; i < varName.length(); i++)
  {
    if(isDigit(varName.charAt(i)) == false && isAlpha(varName.charAt(i)) == false && varName.charAt(i) != '_')
    {
      return -2;
    }

    if(isDigit(varName.charAt(i)) && letterFirst == false)
    {
      return -3;
    }

    if(isAlpha(varName.charAt(0)) || varName.charAt(0) == '_')
    {
      letterFirst = true;
    }
  }

  return 1;
}

int8_t varTypeLUT(String varType)
{
  String varTypes[11] = {"DOUBLE", "UINT64_T", "INT64_T", "UINT32_T", "INT32_T", "UINT16_T", "INT16_T", "UINT8_T", "INT8_T", "BOOL", "CHAR"};

  for(int i = 0; i < 11; i++)
  {
    if(varType == varTypes[i])
    {
      return i;
    }
  }

  return -1;
}

static String selected_program;
static bool program_selected = false;
bool sdprog = false;
int8_t terminal_funcs(String code)
{
  code.trim();
  //code.toUpperCase();
  codeTokenizer(code);

  if(tokens[0] == "SD_INFO" && tokenTreeSize == 1) // show SD card info
  {
    SdBusGuard sdBusGuard;
    int8_t sdReady = sdEnsureReady();

    echo_message("=>SD CARD INFO:");
    echo_message("    MOUNTED:     " + String(sdReady == 1));

    if(sdReady == 1)
    {
      echo_message("    CARD SIZE:   " + String((float)firmwareSdCardSizeBytes() / 1000));
      echo_message("    TOTAL KBYTES: " + String((float)firmwareSdVolumeTotalBytes() / 1000));
      echo_message("    USED KBYTES:  " + String((float)firmwareSdVolumeUsedBytes() / 1000));
    }
    else
    {
      echo_message("    CARD SIZE:   0");
      echo_message("    TOTAL KBYTES: 0");
      echo_message("    USED KBYTES:  0");
    }

  }else if(tokens[0] == "SD_FILES" && tokenTreeSize == 1) // show SD card files
  {
    SdBusGuard sdBusGuard;
    if(sdEnsureReady() != 1)
    {
      echo_message("=>SD CARD NOT READY.");
      echo_message("");
      return 1;
    }
    
    FirmwareFile dir = SD.open("/", FIRMWARE_FILE_READ);

    echo_message("=>SD CARD FILES:");

    if(dir == false || dir.isDir() == false)
    {
      echo_message("=>FAILED TO OPEN SD ROOT DIR.");
      echo_message("");
      sdInitialized = false;

      if(dir)
      {
        dir.close();
      }

      return 1;
    }

    int fileCounter = 0;

    while(true)
    {
      FirmwareFile entry;
      entry.openNext(&dir, FIRMWARE_FILE_READ);

      if(entry == false)
      {
        break;
      }

      if(entry.isDir() == false)
      {
        echo_message("    " + String(fileCounter+1) + ". \"" + firmwareFileName(entry) + "\"");
        fileCounter++;
      }

      entry.close();
    }

    dir.close();

  }else if(tokens[0] == "FS_FILES" && tokenTreeSize == 1)
  {
    echo_message("=>FS FILES:");
    if (!flash_storage_begin())
    {
      echo_message(usb_storage_active()
                     ? "=>FLASH FILESYSTEM IS UNAVAILABLE WHILE USB STORAGE MODE IS ON."
                     : "=>FLASH IS NOT FAT-FORMATTED; USE USB_STORAGE ON.");
      echo_message("");
      return 1;
    }

    int fileCounter = 0;
    Dir dir = FatFS.openDir("/");
    while (dir.next())
    {
      if (dir.isFile())
      {
        echo_message("    " + String(fileCounter + 1) + ". \"" + dir.fileName() + "\"");
        fileCounter++;
      }
    }

  }else if(tokens[0] == "SELECT_PROG_SD" && tokens[1] == "\"" && tokens[3] == "\"" && tokenTreeSize == 4)
  {
    sdprog = true;
    if(tokens[2].endsWith(".narm"))
    {
      SdBusGuard sdBusGuard;
      if(sdEnsureReady() != 1)
      {
        echo_message("=>SD CARD NOT READY.");
        echo_message("");
        return 1;
      }
      
      FirmwareFile sdFile = SD.open(tokens[2].c_str(), FIRMWARE_FILE_READ);
      selected_program = tokens[2];
      
      if(sdFile == true)
      {
        program_selected = true;
        

        echo_message("=>SELECTED PROGRAM \"" + selected_program + "\"");
        echo_message("");
      }else 
      {
        echo_message("=>NO FILE NAMED \"" + selected_program + "\" FOUND IN SD.");
        echo_message("");
      }

      sdFile.close();
    }else
    {
      echo_message("=>TARGET FILE MUST BE .narm");
      echo_message("");
    }

  }else if(tokens[0] == "SELECT_PROG_FS" && tokens[1] == "\"" && tokens[3] == "\"" && tokenTreeSize == 4)
  {
    sdprog = false;
    if (tokens[2].endsWith(".narm"))
    {
      String filePath = tokens[2];

      if (!flash_storage_begin())
      {
        echo_message(usb_storage_active()
                       ? "=>FLASH FILESYSTEM IS UNAVAILABLE WHILE USB STORAGE MODE IS ON."
                       : "=>FLASH IS NOT FAT-FORMATTED; USE USB_STORAGE ON.");
        echo_message("");
        return 1;
      }

      if (!filePath.startsWith("/"))
      {
        filePath = "/" + filePath;
      }

      File fsFile = FatFS.open(filePath, "r");

      selected_program = tokens[2];

      if (fsFile)
      {
        program_selected = true;

        echo_message("=>SELECTED PROGRAM \"" + selected_program + "\"");
        echo_message("");
      }else
      {
        echo_message("=>NO FILE NAMED \"" + selected_program + "\" FOUND IN FS.");
        echo_message("");
      }

      fsFile.close();
    }else
    {
      echo_message("=>TARGET FILE MUST BE .narm");
      echo_message("");
    }

  }else if(tokens[0] == "RUN" && tokenTreeSize == 1)
  {
    if(program_selected == true)
    {
      if(sdprog == true)
      {
        if(sdEnsureReady() != 1)
        {
          echo_message("=>SD CARD NOT READY.");
          echo_message("");
          return 1;
        }

        launch_program_sd(selected_program);
      }else
      {
        launch_program_fs(selected_program);
      }
      
    }else
    {
      echo_message("=>NO PROGRAM SELECTED TO BE RUN");
      echo_message("");
    }

  }else if(tokens[0] == "MMBASIC" && tokenTreeSize == 1)
  {
    launch_program_fs("mmbasic.narm");

  }else if(tokens[0] == "FILEMGR" && tokenTreeSize == 1)
  {
    launch_program_fs("filemgr.narm");

  }else if(tokens[0] == "CALC" && tokenTreeSize == 1)
  {
    launch_program_fs("calc640.narm");

  }else if(tokens[0] == "USB_STORAGE" && tokenTreeSize == 2 &&
           (tokens[1] == "ON" || tokens[1] == "ENABLE"))
  {
    echo_message("=>PREPARING USB STORAGE...");
    const int32_t result = usb_storage_enable(0u);
    if (result == USB_STORAGE_RESULT_OK)
    {
      echo_message("=>USB STORAGE R5 ENABLED (FOREGROUND).");
      echo_message("=>FLASH STORAGE IS AVAILABLE TO THE PC.");
      echo_message("=>IF PROMPTED, USE WINDOWS FAT QUICK FORMAT.");
      echo_message("=>EJECT THE DRIVE BEFORE USB_STORAGE OFF.");
    }
    else if (result == USB_STORAGE_RESULT_UNCHANGED)
      echo_message("=>USB STORAGE MODE IS ALREADY ON.");
    else echo_message("=>FAILED TO ENABLE USB STORAGE MODE.");

  }else if(tokens[0] == "USB_STORAGE" && tokenTreeSize == 2 &&
           (tokens[1] == "OFF" || tokens[1] == "DISABLE"))
  {
    const int32_t result = usb_storage_disable(0u);
    if (result == USB_STORAGE_RESULT_OK)
      echo_message("=>USB STORAGE MODE DISABLED; FLASH FILESYSTEM REMOUNTED.");
    else if (result == USB_STORAGE_RESULT_UNCHANGED)
      echo_message("=>USB STORAGE MODE IS ALREADY OFF.");
    else if (result == USB_STORAGE_RESULT_BUSY)
      echo_message("=>EJECT RP2350FS FROM THE PC BEFORE TURNING STORAGE MODE OFF.");
    else echo_message("=>FAILED TO DISABLE USB STORAGE MODE.");

  }else if(tokens[0] == "USB_STORAGE" && tokenTreeSize == 2 && tokens[1] == "STATUS")
  {
    usb_storage_print_diagnostics();

  }else if(tokens[0] == "TASKMEM" && tokenTreeSize == 2 &&
           (tokens[1] == "ON" || tokens[1] == "OFF"))
  {
    const bool enabled = tokens[1] == "ON";
    native_memory_trace_enable(enabled);
    echo_message(enabled ? "=>NARM memory trace ON (serial, every 2s)" :
                           "=>NARM memory trace OFF");

  }else if(tokens[0] == "TASKDIAG" && tokenTreeSize == 1)
  {
    native_scheduler_print_diagnostics();

  }else if(tokens[0] == "TASKS" && tokenTreeSize == 1)
  {
    echo_message("=>NARM TASKS:");
    const uint32_t count = native_task_count();
    if (count == 0u) echo_message("   NONE");
    for (uint32_t i = 0; i < count; ++i)
    {
      NativeTaskInfo info = {};
      if (!native_task_info(i, info)) continue;
      const char *mode = info.selected_mode == NARM_RUN_MODE_AUTO ? "AUTO" :
                         info.selected_mode == NARM_RUN_MODE_BACKGROUND ? "BACKGROUND" :
                         "FOREGROUND";
      const char *state = info.state == NATIVE_PROCESS_READY ? "READY" :
                          info.state == NATIVE_PROCESS_RUNNING ? "RUNNING" :
                          info.state == NATIVE_PROCESS_WAITING ? "WAITING" :
                          info.state == NATIVE_PROCESS_SUSPENDED ? "SUSPENDED" :
                          info.state == NATIVE_PROCESS_LOADING ? "LOADING" :
                          info.state == NATIVE_PROCESS_FAULTED ? "FAULTED" : "EXITED";
      echo_message("   PID " + String(info.pid) + " " + String(info.name) +
                   " STATE=" + String(state) +
                   " MODE=" + String(mode) +
                   (info.focused ? " FOCUSED" : ""));
    }

  }else if(tokenTreeSize == 3 && tokens[0] == "TASK" && tokens[1] == "FOCUS")
  {
    echo_message(native_task_focus((uint32_t)tokens[2].toInt())
                   ? "=>TASK FOCUSED." : "=>TASK PID NOT FOUND.");

  }else if(tokenTreeSize == 3 && tokens[0] == "TASK" && tokens[1] == "CLOSE")
  {
    echo_message(native_task_close((uint32_t)tokens[2].toInt())
                   ? "=>TASK CLOSE REQUESTED." : "=>TASK PID NOT FOUND.");

  }else if(tokenTreeSize == 4 && tokens[0] == "TASK" && tokens[1] == "MODE")
  {
    NativeRunMode mode = NARM_RUN_MODE_AUTO;
    bool validMode = true;
    if(tokens[3] == "FOREGROUND") mode = NARM_RUN_MODE_FOREGROUND;
    else if(tokens[3] == "BACKGROUND") mode = NARM_RUN_MODE_BACKGROUND;
    else if(tokens[3] != "AUTO") validMode = false;
    echo_message(validMode && native_task_set_mode((uint32_t)tokens[2].toInt(), mode)
                   ? "=>TASK MODE CHANGED." : "=>INVALID TASK OR MODE.");

  }else if(tokens[0] == "CLR" && tokenTreeSize == 1) // 
  {
    memset(gridBuffer_char, ' ', sizeof(gridBuffer_char));
    terminal_aspects.cur.locationX = leftBorder;
    terminal_aspects.cur.locationY = 0;
    char_grid_buffer_V_draw_offset = 0;
    echo_message("====== UNNAMED RP2350 COMPUTER");
    echo_message("====== SYSTEM MEMORY: " + String((float)(memory_size*4)/(float)1000) + "KB");
    echo_message("====== PRIMARY RUNTIME: NARM");
    echo_message("");
  

  }else if(tokens[0] == "SYS_MEM" && tokenTreeSize == 1) // 
  {
    echo_message("=>PARTITIONS:");
    int used_sys_mem = 0;

    for(int i = 0; i < partition_amount; i++)
    {
      if(partitions[i].partition_type != VACANT)
      {
        int current_partition_size = ((partitions[i].end_addr - partitions[i].start_addr + 1) * 4);
        echo_message("   " + partitions[i].partition_name + " ; SIZE: " + String((float)current_partition_size/(float)1000) + "KB");
        used_sys_mem = used_sys_mem + current_partition_size;
      }
    }

    echo_message("=>TOTAL USE: " + String((float)used_sys_mem/(float)1000) + "KB OUT OF " + String((float)(memory_size*4)/(float)1000) + "KB");
  }else if(tokens[0] == "MALLOC" && tokenTreeSize == 3) //
  {
    alloc_partition(tokens[1], USER, tokens[2].toInt());
  }else if(tokens[0] == "AUDIO_VOICES" && tokenTreeSize == 1)
  {
    uint32_t allocated = 0;
    for(uint8_t voice = 0; voice < AUDIO_VOICE_CHANNELS; ++voice)
    {
      if(native_audio_voice_reserved(voice)) ++allocated;
    }
    echo_message("=>" + String(allocated) + " / " +
                 String((uint32_t)AUDIO_VOICE_CHANNELS) + " AUDIO VOICES ALLOCATED");
  }else if(tokens[0] == "PLAY_SOUND_SD" && tokens[2] == "\"" && tokens[4] == "\"" &&tokenTreeSize == 5) //
  {
    echo_message("=>TRYING TO PLAY SOUND.");
    if(!audio_output_ready())
    {
      echo_message("=>AUDIO OUTPUT IS NOT READY.");
    }
    else if(const long voice = tokens[1].toInt();
            voice < 0 || voice >= AUDIO_VOICE_CHANNELS)
    {
      echo_message("=>INVALID AUDIO VOICE.");
    }
    else if(native_audio_voice_reserved((uint8_t)voice))
    {
      echo_message("=>AUDIO VOICE IS RESERVED BY A PROGRAM.");
    }
    else if(play_sound_SD((uint8_t)voice, tokens[3].c_str()))
    {
      echo_message("=>SOUND QUEUED ON VOICE " + tokens[1] + ".");
    }
    else
    {
      echo_message("=>FAILED TO OPEN OR PARSE WAV FILE.");
    }
  }
  

  return 1;
}
