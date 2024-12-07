/**
lin 3D Printer Firmware
 * Copyright (c) 2020 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */
/**
 * DWIN by Creality3D
 */
#include "../../../inc/MarlinConfigPre.h"
#if ENABLED(DWIN_CREALITY_LCD)
#include "dwin.h"
#include "ui_position.h"  //UI位置
#if ANY(AUTO_BED_LEVELING_BILINEAR, AUTO_BED_LEVELING_LINEAR, AUTO_BED_LEVELING_3POINT) && DISABLED(PROBE_MANUALLY)
#define HAS_ONESTEP_LEVELING 1
#endif
#if ANY(BABYSTEPPING, HAS_BED_PROBE, HAS_WORKSPACE_OFFSET)
#define HAS_ZOFFSET_ITEM 1
#endif
#if !HAS_BED_PROBE && ENABLED(BABYSTEPPING)
#define JUST_BABYSTEP 1
#endif
#include <WString.h>
#include <stdio.h>
#include <string.h>
#include "../../fontutils.h"
#include "../../marlinui.h"
#include "../../../sd/cardreader.h"
#include "../../../MarlinCore.h"
#include "../../../core/serial.h"
#include "../../../core/macros.h"
#include "../../../gcode/queue.h"
#include "../../../module/temperature.h"
#include "../../../module/printcounter.h"
#include "../../../module/motion.h"
#include "../../../module/planner.h"
#if ENABLED(EEPROM_SETTINGS)
#include "../../../module/settings.h"
#endif
#if ENABLED(HOST_ACTION_COMMANDS)
#include "../../../feature/host_actions.h"
#endif
#if HAS_ONESTEP_LEVELING
#include "../../../feature/bedlevel/bedlevel.h"
#endif
#if HAS_BED_PROBE
#include "../../../module/probe.h"
#endif
#if EITHER(BABYSTEP_ZPROBE_OFFSET, JUST_BABYSTEP)
#include "../../../feature/babystep.h"
#endif
#if ENABLED(POWER_LOSS_RECOVERY)
#include "../../../feature/powerloss.h"
#endif

#include "lcd_rts.h"
#include "../../../module/AutoOffset.h"


#ifndef MACHINE_SIZE
  #define MACHINE_SIZE STRINGIFY(X_BED_SIZE) "x" STRINGIFY(Y_BED_SIZE) "x" STRINGIFY(Z_MAX_POS)
#endif
#ifndef CORP_WEBSITE
  #define CORP_WEBSITE WEBSITE_URL
#endif

#define CORP_WEBSITE_C    "www.creality.cn "
#define CORP_WEBSITE_E    "www.creality.com"
#define PAUSE_HEAT
#define CHECKFILAMENT true

#define USE_STRING_HEADINGS
//#define USE_STRING_TITLES

#define DWIN_FONT_MENU font8x16
#define DWIN_FONT_STAT font10x20
#define DWIN_FONT_HEAD font10x20
#define DWIN_MIDDLE_FONT_STAT font8x16
#define MENU_CHAR_LIMIT  24

// Print speed limit
#define MIN_PRINT_SPEED  10
#define MAX_PRINT_SPEED 999

// Feedspeed limit (max feedspeed = DEFAULT_MAX_FEEDRATE * 2)
#define MIN_MAXFEEDSPEED      1
#define MIN_MAXACCELERATION   1
#define MIN_MAXJERK           0.1
#define MIN_STEP              1

#define FEEDRATE_E      (60)

// Minimum unit (0.1) : multiple (10)
#define UNITFDIGITS 1
#define MINUNITMULT pow(10, UNITFDIGITS)

#define ENCODER_WAIT_MS                  20
#define DWIN_VAR_UPDATE_INTERVAL         1024
#define DACAI_VAR_UPDATE_INTERVAL        4048
#define HEAT_ANIMATION_FLASH            150  //加热动画刷新
#define DWIN_SCROLL_UPDATE_INTERVAL      SEC_TO_MS(0.5)  // rock_20210819
#define DWIN_REMAIN_TIME_UPDATE_INTERVAL SEC_TO_MS(20)

#define PRINT_SET_OFFSET   4  // rock_20211115
#define TEMP_SET_OFFSET    2  // rock_20211115
#define PID_VALUE_OFFSET   10
#if ENABLED(DWIN_CREALITY_480_LCD)
  #define JPN_OFFSET        -13  // rock_20211115
  constexpr uint16_t TROWS = 6, MROWS = TROWS - 1,        // Total rows, and other-than-Back
                     TITLE_HEIGHT = 30,                   // Title bar height
                     MLINE = 53,                          // Menu line height
                     LBLX = 58,                           // Menu item label X
                     MENU_CHR_W = 8, STAT_CHR_W = 10;
  #define MBASE(L) (49 + MLINE * (L))
#elif ENABLED(DWIN_CREALITY_320_LCD)
  #define JPN_OFFSET        -5  // rock_20211115
  constexpr uint16_t TROWS = 6, MROWS = TROWS - 1,        // Total rows, and other-than-Back
                     TITLE_HEIGHT = 24,                   // Title bar height
                     MLINE = 36,                          // Menu line height
                     LBLX = 42,                           // Menu item label X
                     MENU_CHR_W = 8, STAT_CHR_W = 10;
  #define MBASE(L) (34 + MLINE * (L))
#endif

#define font_offset    19
#define BABY_Z_VAR TERN(HAS_BED_PROBE, probe.offset.z, dwin_zoffset)

  char shift_name[LONG_FILENAME_LENGTH + 1];
  char current_file_name[30];
  static char *  print_name = card.longest_filename();
  static uint8_t print_len_name=strlen(print_name);
  int8_t shift_amt; // = 0
  millis_t shift_ms; // = 0
  static uint8_t left_move_index=0;

/* Value Init */
HMI_value_t HMI_ValueStruct;
HMI_Flag_t HMI_flag{0};
CRec CardRecbuf;        // rock_20211021
millis_t dwin_heat_time = 0;
uint8_t G29_level_num=0;//记录G29调平了多少个点，用来判断g29是否正常调平
bool end_flag=false; //防止反复刷新曲线完成指令
enum DC_language current_language;
volatile uint8_t checkkey = 0;
// 0 Without interruption, 1 runout filament paused 2 remove card pause
static bool temp_remove_card_flag = false, temp_cutting_line_flag = false/*,temp_wifi_print_flag=false*/;
typedef struct {
  uint8_t now, last;
  void set(uint8_t v) { now = last = v; }
  void reset() { set(0); }
  bool changed() { bool c = (now != last); if (c) last = now; return c; }
  bool dec() { if (now) now--; return changed(); }
  bool inc(uint8_t v) { if (now < (v - 1)) now++; else now = (v - 1); return changed(); }
} select_t;

typedef struct
{
  char filename[FILENAME_LENGTH];
  char longfilename[LONG_FILENAME_LENGTH];
} PrintFile_InfoTypeDef;

select_t select_page{0}, select_file{0}, select_print{0}, select_prepare{0}
         , select_control{0}, select_axis{0}, select_temp{0}, select_motion{0}, select_tune{0}
         , select_advset{0}, select_PLA{0}, select_ABS{0}
         , select_speed{0}
         , select_acc{0}
         , select_jerk{0}
         , select_step{0}
         , select_item{0}
         , select_language{0}
         ,select_hm_set_pid{0}
         ,select_set_pid{0}
         ,select_level{0}
         ,select_show_pic{0};


uint8_t index_file     = MROWS,
        index_prepare  = MROWS,
        index_control  = MROWS,
        index_leveling = MROWS,
        index_tune     = MROWS,
        index_advset   = MROWS,
        index_language = MROWS+2,
        index_temp     = MROWS,
        index_pid      = MROWS,
        index_select   = 0;
bool dwin_abort_flag = false; // Flag to reset feedrate, return to Home

constexpr float default_max_feedrate[]        = DEFAULT_MAX_FEEDRATE;
constexpr float default_max_acceleration[]    = DEFAULT_MAX_ACCELERATION;

#if HAS_CLASSIC_JERK
  constexpr float default_max_jerk[]          = { DEFAULT_XJERK, DEFAULT_YJERK, DEFAULT_ZJERK, DEFAULT_EJERK };
#endif

static uint8_t _card_percent = 0;
uint8_t Cloud_Progress_Bar = 0; // The cloud prints the transmitted progress bar data
uint32_t _remain_time = 0;      // rock_20210830

float default_nozzle_ptemp = DEFAULT_Kp;
float default_nozzle_itemp = DEFAULT_Ki;
float default_nozzle_dtemp = DEFAULT_Kd;

float default_hotbed_ptemp = DEFAULT_bedKp;
float default_hotbed_itemp = DEFAULT_bedKi;
float default_hotbed_dtemp = DEFAULT_bedKd;
uint16_t auto_bed_pid = 100, auto_nozzle_pid = 260;

#if ENABLED(PAUSE_HEAT)
  #if ENABLED(HAS_HOTEND)
    uint16_t resume_hotend_temp = 0;
  #endif
  #if ENABLED(HAS_HEATED_BED)
    uint16_t resume_bed_temp = 0;
  #endif
#endif

#if HAS_ZOFFSET_ITEM
  float dwin_zoffset = 0, last_zoffset = 0;
  float dwin_zoffset_edit = 0, last_zoffset_edit = 0,temp_zoffset_single=0; //当前点的调节前的调平值;
#endif

int16_t temphot = 0;
uint8_t afterprobe_fan0_speed = 0;
bool home_flag = false;
bool G29_flag = false;

#define DWIN_BOOT_STEP_EEPROM_ADDRESS 0x01   //设置开机引导步骤  
#define DWIN_LANGUAGE_EEPROM_ADDRESS  0x02   // Between 0x01 and 0x63 (EEPROM_OFFSET-1)                                           
#define DWIN_AUTO_BED_EEPROM_ADDRESS  0x03   //热床自动PID目标值
#define DWIN_AUTO_NOZZ_EEPROM_ADDRESS 0x05   //喷嘴自动PID目标值
 
void Draw_Leveling_Highlight(const bool sel) {
  HMI_flag.select_flag = sel;
  const uint16_t c1 = sel ? Button_Select_Color : Color_Bg_Black,
                 c2 = sel ? Color_Bg_Black : Button_Select_Color;
  // DWIN_Draw_Rectangle(0, c1, 25, 306, 126, 345);
  // DWIN_Draw_Rectangle(0, c1, 24, 305, 127, 346);
  DWIN_Draw_Rectangle(0, c1, BUTTON_EDIT_X,  BUTTON_EDIT_Y, BUTTON_EDIT_X+BUTTON_W-1,BUTTON_OK_Y+BUTTON_H-1);
  DWIN_Draw_Rectangle(0, c1, BUTTON_EDIT_X-1,  BUTTON_EDIT_Y-1,BUTTON_EDIT_X+BUTTON_W, BUTTON_EDIT_Y+BUTTON_H);
  // DWIN_Draw_Rectangle(0, c2, 146, 306, 246, 345);
  // DWIN_Draw_Rectangle(0, c2, 145, 305, 247, 346);
  DWIN_Draw_Rectangle(0, c2, BUTTON_OK_X, BUTTON_OK_Y, BUTTON_OK_X+BUTTON_W-1, BUTTON_OK_Y+BUTTON_H-1);
  DWIN_Draw_Rectangle(0, c2, BUTTON_OK_X-1, BUTTON_OK_Y-1, BUTTON_OK_X+BUTTON_W, BUTTON_OK_Y+BUTTON_H);
}
// RUN_AND_WAIT_GCODE_CMD("M24", true);
//pause_resume_feedstock(FEEDING_DEF_DISTANCE,FEEDING_DEF_SPEED);
static void pause_resume_feedstock(uint16_t _distance,uint16_t _feedRate)
{
   char cmd[20], str_1[16];
  current_position[E_AXIS] += _distance;
  line_to_current_position(feedRate_t(_feedRate));
  current_position[E_AXIS] -= _distance;
  memset(cmd,0,sizeof(cmd));
  sprintf_P(cmd, PSTR("G92.9E%s"), dtostrf(current_position[E_AXIS], 1, 3, str_1));
  gcode.process_subcommands_now(cmd);
  memset(cmd,0,sizeof(cmd));
     // Resume the feedrate
  sprintf_P(cmd, PSTR("G1F%d"), MMS_TO_MMM(feedrate_mm_s));
  gcode.process_subcommands_now(cmd);
}

void In_out_feedtock_level(uint16_t _distance,uint16_t _feedRate,bool dir)
{
  char cmd[20], str_1[16];
  float olde = current_position.e,differ_value=0;
  if(current_position.e<_distance)differ_value =(_distance-current_position.e);
  else differ_value=0;
  if(dir)
  {
    current_position.e +=_distance;
    line_to_current_position(_feedRate);
  }
  else  //回抽
  {
    current_position.e -=_distance;
    line_to_current_position(_feedRate);    
  }
    current_position.e = olde;
    planner.set_e_position_mm(olde);
    planner.synchronize();
    sprintf_P(cmd, PSTR("G1 F%s"), getStr(feedrate_mm_s)); //设置原来的速度
   gcode.process_subcommands_now(cmd);
}

void In_out_feedtock(uint16_t _distance,uint16_t _feedRate,bool dir)
{
  char cmd[20], str_1[16];
  float olde = current_position.e,differ_value=0;
  if(current_position.e<_distance)differ_value =(_distance-current_position.e);
  else differ_value=0;
  if(dir)
  {
    current_position.e +=_distance;
    line_to_current_position(_feedRate);
  }
  else  //回抽
  {
    if(differ_value)
    {
      current_position.e +=differ_value;
      line_to_current_position(FEEDING_DEF_SPEED); //速度太快有响声
      planner.synchronize();
    }
    current_position.e -=_distance;
    line_to_current_position(_feedRate);    
  }
    current_position.e = olde;
    planner.set_e_position_mm(olde);
    planner.synchronize();
    sprintf_P(cmd, PSTR("G1 F%s"), getStr(feedrate_mm_s)); //设置原来的速度
    gcode.process_subcommands_now(cmd);
    // RUN_AND_WAIT_GCODE_CMD(cmd, true);                  //Rock_20230821 
}
/*
0自动退料：  准备->自动退料 --->加热到260℃--》弹框提示正在退料中 ---> 
            E轴进料15mm---> E轴退料90mm---》弹窗提示人为取出料并降温到140℃----》
           窗口回退到准备页面
1自动进料： 准备->自动进料 ---> 先加热到240℃--》弹框提示人为  插入料（斜口45°）并按压
           ----> 点击确定 ---> E轴进料90mm ----》 窗口回退到准备页面并降温到140℃  
*/
static void Auto_in_out_feedstock(bool dir)  //0退料，1进料
{
  if(dir) //进料
  {
    Popup_Window_Feedstork_Tip(1); //进料提示
    //显示
    SET_HOTEND_TEMP(FEED_TEMPERATURE, 0);   //先加热到240℃
    WAIT_HOTEND_TEMP(60 * 5 * 1000, 5);     //等待喷嘴温度达到设定值
    Popup_Window_Feedstork_Finish(1);       //进料确认
    HMI_flag.Auto_inout_end_flag=true; 
    checkkey = AUTO_IN_FEEDSTOCK;   
    DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Confirm, 79, 264); //确定按钮
    //返回准备页面
  }
  else //退料
  {
    Popup_Window_Feedstork_Tip(0);           //退料提示
    SET_HOTEND_TEMP(EXIT_TEMPERATURE, 0);    //先加热到260℃
    WAIT_HOTEND_TEMP(60 * 5 * 1000, 5);      //等待喷嘴温度达到设定值
    In_out_feedtock(FEEDING_DEF_DISTANCE_1,FEEDING_DEF_SPEED,true);//进料15mm
    In_out_feedtock(IN_DEF_DISTANCE,FEEDING_DEF_SPEED/2,false); //回抽90mm
    Popup_Window_Feedstork_Finish(0);        //退料确认
    HMI_flag.Auto_inout_end_flag=true;
    checkkey = AUTO_OUT_FEEDSTOCK;
    DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Confirm, 79, 264); //确定按钮
    SET_HOTEND_TEMP(STOP_TEMPERATURE, 0);   //降温到140℃
    // WAIT_HOTEND_TEMP(60 * 5 * 1000, 5);    //等待喷嘴温度达到设定值
  }  
}
/*获取指定g文件信息 *short_file_name:短文件名 *file:文件信息指针 返回值*/
void get_file_info(char *short_file_name, PrintFile_InfoTypeDef *file)
{
  if(!card.isMounted())
  {
    return;
  }
  // 获取根目录
  card.getWorkDirName();
  if(card.filename[0] != '/')
  {
    card.cdup();
  }
  // 选择文件
  card.selectFileByName(short_file_name);
  // 获取gcode文件信息
  strcpy(file->filename, card.filename);
  strcpy(file->longfilename, card.longFilename);
}

inline bool HMI_IsJapanese() { return HMI_flag.language == DACAI_JAPANESE; }

void HMI_SetLanguageCache()
{
  // DWIN_JPG_CacheTo1(HMI_IsJapanese() ? Language_Chinese : Language_English);
}

void HMI_ResetLanguage()
{
  HMI_flag.language=Language_Max;
  HMI_flag.boot_step=Set_language;
  Save_Boot_Step_Value();//保存开机引导步骤
  BL24CXX::write(DWIN_LANGUAGE_EEPROM_ADDRESS, (uint8_t*)&HMI_flag.language, sizeof(HMI_flag.language)); 
  HMI_SetLanguageCache();
}
static void HMI_ResetDevice()
{
  // uint8_t  current_device = DEVICE_UNKNOWN; //暂时这样添加
  // BL24CXX::write(LASER_FDM_ADDR, (uint8_t *)&current_device, 1);
}
static void Read_Boot_Step_Value()
{
  #if BOTH(EEPROM_SETTINGS, IIC_BL24CXX_EEPROM)
  //先读取HMI_flag.boot_step的值，判定是否需要开机引导。
    BL24CXX::read(DWIN_BOOT_STEP_EEPROM_ADDRESS, (uint8_t*)&HMI_flag.boot_step, sizeof(HMI_flag.boot_step));
    //读取语言配置项
    BL24CXX::read(DWIN_LANGUAGE_EEPROM_ADDRESS, (uint8_t*)&HMI_flag.language, sizeof(HMI_flag.language));
  #endif
  if(HMI_flag.boot_step!=Boot_Step_Max)
  {
    HMI_flag.Need_boot_flag=true; //需要开机引导
    HMI_flag.boot_step=Set_language;
    HMI_flag.language = English;
  }
  else
  {
    HMI_flag.Need_boot_flag=false;//不需要开机引导
    HMI_StartFrame(true);         //跳转到主界面
  } 
}

void Save_Boot_Step_Value()
{
  #if BOTH(EEPROM_SETTINGS, IIC_BL24CXX_EEPROM) 
   BL24CXX::write(DWIN_BOOT_STEP_EEPROM_ADDRESS, (uint8_t*)&HMI_flag.boot_step, sizeof(HMI_flag.boot_step));
  #endif
}
static void Read_Auto_PID_Value()
{
  BL24CXX::read(DWIN_AUTO_BED_EEPROM_ADDRESS,(uint8_t*)&HMI_ValueStruct.Auto_PID_Value + 2, sizeof(HMI_ValueStruct.Auto_PID_Value[1]));
  BL24CXX::read(DWIN_AUTO_NOZZ_EEPROM_ADDRESS,(uint8_t*)&HMI_ValueStruct.Auto_PID_Value + 4, sizeof(HMI_ValueStruct.Auto_PID_Value[2]));
  LIMIT(HMI_ValueStruct.Auto_PID_Value[1], 60, BED_MAX_TARGET);
  LIMIT(HMI_ValueStruct.Auto_PID_Value[2], 100, thermalManager.hotend_max_target(0));
}
static void Save_Auto_PID_Value()
{
  BL24CXX::write(DWIN_AUTO_BED_EEPROM_ADDRESS,(uint8_t*)&HMI_ValueStruct.Auto_PID_Value+2, sizeof(HMI_ValueStruct.Auto_PID_Value[1]));
  BL24CXX::write(DWIN_AUTO_NOZZ_EEPROM_ADDRESS,(uint8_t*)&HMI_ValueStruct.Auto_PID_Value+4, sizeof(HMI_ValueStruct.Auto_PID_Value[2]));
}

void HMI_SetLanguage()
{
  // rock_901122 解决第一次上电 语言混乱的问题
  // if(HMI_flag.language > Portuguese)
  if(HMI_flag.Need_boot_flag||(HMI_flag.language < Chinese) || (HMI_flag.language >= Language_Max))
  {
    // Draw_Mid_Status_Area(true); //rock_20230529
    HMI_flag.language = English;
    select_language.reset();
    index_language = MROWS+1;
    Draw_Poweron_Select_language();
    checkkey = Poweron_select_language;
  }
  else
  {
    // HMI_StartFrame(true);
  }
  //HMI_SetLanguageCache();
}

static uint8_t Move_Language(uint8_t curr_language)
{
  switch (curr_language)
  {
    case Chinese:
      curr_language = English;
      break;

    case English:
      curr_language = German;
      break;

    case German:
      curr_language = Russian;
      break;

    case Russian:
      curr_language = French;
      break;

    case French:
      curr_language = Turkish;
      break;

    case Turkish:
      curr_language = Spanish;
      break;

    case Spanish:
      curr_language = Italian;
      break;

    case Italian:
      curr_language = Portuguese;
      break;

    case Portuguese:
      curr_language = Japanese;
      break;

     case Japanese:
      curr_language = Korean;
      break;
    case Korean:
      curr_language = Chinese;
      break;

    default:
      curr_language = English;
      break;
  }
  return curr_language;
}

void HMI_ToggleLanguage()
{
  HMI_flag.language = Move_Language(HMI_flag.language);
  SERIAL_ECHOLNPAIR("HMI_flag.language=: ", HMI_flag.language);
  // HMI_flag.language = HMI_IsJapanese() ? DWIN_ENGLISH : DACAI_JAPANESE;

  // HMI_SetLanguageCache();
  #if BOTH(EEPROM_SETTINGS, IIC_BL24CXX_EEPROM)
    BL24CXX::write(DWIN_LANGUAGE_EEPROM_ADDRESS, (uint8_t*)&HMI_flag.language, sizeof(HMI_flag.language));
  #endif
}

// 显示print中标题
static void Show_JPN_print_title(void)
{
  if(HMI_flag.language < Language_Max)
  {
    Clear_Title_Bar();
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Printing, TITLE_X, TITLE_Y);
  }
}

static void Show_JPN_pause_title(void)
{
  if(HMI_flag.language < Language_Max)
  {
    Clear_Title_Bar();
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Pausing, TITLE_X, TITLE_Y);
  }
}

#if ENABLED(SHOW_GRID_VALUES)  //

void DWIN_Draw_Z_Offset_Float(uint8_t size, uint16_t color,uint16_t bcolor, uint8_t iNum, uint8_t fNum, uint16_t x, uint16_t y, long value) 
{
  if (value < 0) 
  {
    DWIN_Draw_FloatValue(true, true, 0, size, color, bcolor, iNum, fNum, x+2, y, -value);
    DWIN_Draw_String(false, true, font6x12, color, bcolor, x+2, y, F("-"));
  }
  else 
  {    
    DWIN_Draw_FloatValue(true, true, 0, size, color, bcolor, iNum, fNum, x+1, y, value);
    DWIN_Draw_String(false, true, font6x12, color, bcolor, x, y, F(""));
  }
}
#endif
void DWIN_Draw_Signed_Float(uint8_t size, uint16_t bColor, uint8_t iNum, uint8_t fNum, uint16_t x, uint16_t y, long value)
{
  if (value < 0)
  {
    DWIN_Draw_FloatValue(true, true, 0, size, Color_White, bColor, iNum, fNum, x, y+2, -value);
    DWIN_Draw_String(false, true, font6x12, Color_White, bColor, x + 2, y + 2, F("-"));
  }
  else
  {
    DWIN_Draw_FloatValue(true, true, 0, size, Color_White, bColor, iNum, fNum, x, y+2, value);
    DWIN_Draw_String(false, true, font6x12, Color_White, bColor, x + 2, y + 2, F(""));
  }
}
void DWIN_Draw_Signed_Float_Temp(uint8_t size, uint16_t bColor, uint8_t iNum, uint8_t fNum, uint16_t x, uint16_t y, long value)
{
  if (value < 0)
  {
    DWIN_Draw_FloatValue(true, true, 0, size, Color_Yellow, bColor, iNum, fNum, x, y+2, -value);
    DWIN_Draw_String(false, true, font6x12, Color_Yellow, bColor, x + 2, y + 2, F("-"));
  }
  else
  {
    DWIN_Draw_FloatValue(true, true, 0, size, Color_Yellow, bColor, iNum, fNum, x, y+2, value);
    DWIN_Draw_String(false, true, font6x12, Color_Yellow, bColor, x + 2, y + 2, F(""));
  }
}
void ICON_Print()
{
  #if ENABLED(DWIN_CREALITY_480_LCD)

  #elif ENABLED(DWIN_CREALITY_320_LCD)
    // DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Pausing, TITLE_X, TITLE_Y);
    if (select_page.now == 0) 
    {
      DWIN_ICON_Not_Filter_Show(ICON, ICON_Print_1, ICON_PRINT_X, ICON_PRINT_Y);
      DWIN_Draw_Rectangle(0, Color_White, ICON_PRINT_X, ICON_PRINT_Y, ICON_PRINT_X+ICON_W, ICON_PRINT_Y+ICON_H);
      if (HMI_flag.language < Language_Max)
      {
        // rock_j print 1
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Print_1, WORD_PRINT_X, WORD_PRINT_Y);
      }      
    }
    else
    {
      DWIN_ICON_Not_Filter_Show(ICON, ICON_Print_0, ICON_PRINT_X, ICON_PRINT_Y);
      if (HMI_flag.language < Language_Max)
      {
        // rock_j print 0
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Print_0,  WORD_PRINT_X, WORD_PRINT_Y);
      }
    }
  #endif
}

void ICON_Prepare()
{
  #if ENABLED(DWIN_CREALITY_480_LCD)
 
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    if (select_page.now == 1)
    {
      DWIN_ICON_Not_Filter_Show(ICON, ICON_Prepare_1, ICON_PREPARE_X, ICON_PREPARE_Y);
      DWIN_Draw_Rectangle(0, Color_White, ICON_PREPARE_X, ICON_PREPARE_Y, ICON_PREPARE_X+ICON_W, ICON_PREPARE_Y+ICON_H);
      if (HMI_flag.language < Language_Max)
      {
        // DWIN_Frame_AreaCopy(1, 31, 447, 58, 460, 186, 201);
        // rock_j prepare 1
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Prepare_1, WORD_PREPARE_X, WORD_PREPARE_Y);
      }
      else
      {
        DWIN_Frame_AreaCopy(1, 13, 239, 55, 241, 117, 109);
      }
    }
    else
    {
      DWIN_ICON_Not_Filter_Show(ICON, ICON_Prepare_0, ICON_PREPARE_X, ICON_PREPARE_Y);
      if (HMI_flag.language < Language_Max)
      {
        // DWIN_Frame_AreaCopy(1, 31, 405, 58, 420, 186, 201);
        // rock_j prepare 0
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Prepare_0, WORD_PREPARE_X, WORD_PREPARE_Y);
      }
      else
      {
        DWIN_Frame_AreaCopy(1, 13, 203, 55, 205, 117, 109);
      }
    }
  #endif
}

void ICON_Control()
{
  #if ENABLED(DWIN_CREALITY_480_LCD)

  #elif ENABLED(DWIN_CREALITY_320_LCD)
    if (select_page.now == 2)
    {
      DWIN_ICON_Not_Filter_Show(ICON, ICON_Control_1, ICON_CONTROL_X, ICON_CONTROL_Y);      
      DWIN_Draw_Rectangle(0, Color_White,ICON_CONTROL_X, ICON_CONTROL_Y, ICON_CONTROL_X+ICON_W, ICON_CONTROL_Y+ICON_H);
      if(HMI_flag.language<Language_Max)
      {
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Control_1, WORD_CONTROL_X, WORD_CONTROL_Y);
      }
 
    }
    else
    {
      DWIN_ICON_Not_Filter_Show(ICON, ICON_Control_0, ICON_CONTROL_X, ICON_CONTROL_Y);
      if (HMI_flag.language<Language_Max)
      {
        // rock_j control 0
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Control_0, WORD_CONTROL_X, WORD_CONTROL_Y);
      }
      else
      {
        DWIN_Frame_AreaCopy(1, 56, 203, 88, 205, 32, 195);
      }
    }
  #endif
}

void ICON_StartInfo(bool show)
{
  if (show)
  {
    //DWIN_ICON_Not_Filter_Show(ICON, ICON_Info_1, 145, 246);
    DWIN_Draw_Rectangle(0, Color_White, 145, 246, 254, 345);
    if (HMI_flag.language < Language_Max)
    {
      // rock_j
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Info_1, 150, 318);
    }
    else
    {
      DWIN_Frame_AreaCopy(1, 132, 451, 159, 466, 186, 318);
    }
  }
  else
  {
    //DWIN_ICON_Not_Filter_Show(ICON, ICON_Info_0, 145, 246);
    if (HMI_flag.language < Language_Max)
    {
      // DWIN_Frame_AreaCopy(1, 91, 405, 118, 420, 186, 318);
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Info_0, 150, 318);
    }
    else
    {
      DWIN_Frame_AreaCopy(1, 132, 423, 159, 435, 186, 318);
    }
  }
}
void Draw_Menu_Line_UP(uint8_t line, uint8_t icon, uint8_t picID, bool more) 
{
  // Draw_Menu_Item(line, icon, picID, more);  
  if (picID) DWIN_ICON_Show(HMI_flag.language ,picID, 60, MBASE(line)+JPN_OFFSET);
  if (icon) Draw_Menu_Icon(line, icon);
  if (more) Draw_More_Icon(line);
  DWIN_Draw_Line(Line_Color, 16, MBASE(line) + 34, 256, MBASE(line) + 34);
}
// rock_20210726
void ICON_Leveling(bool show)
{
  #if ENABLED(DWIN_CREALITY_480_LCD)
  
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    if (show)
    {
      DWIN_ICON_Not_Filter_Show(ICON, ICON_Leveling_1, ICON_LEVEL_X, ICON_LEVEL_Y);
      DWIN_Draw_Rectangle(0, Color_White, ICON_LEVEL_X, ICON_LEVEL_Y, ICON_LEVEL_X+ICON_W, ICON_LEVEL_Y+ICON_H);
      if (HMI_flag.language < Language_Max)
      {
        if(HMI_flag.language == Russian)
        {
          DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Level_1, WORD_LEVEL_X, WORD_LEVEL_Y);
        }
        else
        {
          DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Level_1, WORD_LEVEL_X, WORD_LEVEL_Y);
        }
      }
      else
      {
        // rock
        DWIN_Frame_AreaCopy(1, 56, 242, 80, 238, 121, 195);
      }
    }
    else
    {
      DWIN_ICON_Not_Filter_Show(ICON, ICON_Leveling_0, ICON_LEVEL_X, ICON_LEVEL_Y);
      if (HMI_flag.language < Language_Max)
      {
        // 俄语词条偏长，往前移动30像素
        if(HMI_flag.language == Russian)
        {
          DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Level_0, WORD_LEVEL_X, WORD_LEVEL_Y);
        }
        else
        {
          DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Level_0, WORD_LEVEL_X, WORD_LEVEL_Y);
        }
      }
      else
      {
        DWIN_Frame_AreaCopy(1, 56, 169, 80, 170, 121, 195);
        // DWIN_Frame_AreaCopy(1, 84, 465, 120, 478, 182, 318);
      }
    }
  #endif
}

void ICON_Tune()
{
  #if ENABLED(DWIN_CREALITY_480_LCD)

  #elif ENABLED(DWIN_CREALITY_320_LCD)
    if (select_print.now == 0)
    {
      DWIN_ICON_Not_Filter_Show(ICON, ICON_Setup_1, ICON_SET_X, ICON_SET_Y);
      if (HMI_flag.language < Language_Max)
      {
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Setup_1, WORD_SET_X, WORD_SET_Y);
      }
      DWIN_Draw_Rectangle(0, Color_White, RECT_SET_X1, RECT_SET_Y1, RECT_SET_X2-1, RECT_SET_Y2-1);
    }
    else
    {
      DWIN_ICON_Not_Filter_Show(ICON, ICON_Setup_0, ICON_SET_X, ICON_SET_Y);
      if (HMI_flag.language<Language_Max)
      {
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Setup_0, WORD_SET_X, WORD_SET_Y);
      }
    }
  #endif
}

//打印暂停
void ICON_Pause()
{
  #if ENABLED(DWIN_CREALITY_480_LCD)
    
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    if (select_print.now == 1)
    {
      DWIN_ICON_Not_Filter_Show(ICON, ICON_Pause_1, ICON_PAUSE_X, ICON_PAUSE_Y);
      if (HMI_flag.language < Language_Max)
      {
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Pause_1, WORD_PAUSE_X, WORD_PAUSE_Y);
      }
      DWIN_Draw_Rectangle(0, Color_White, ICON_PAUSE_X, ICON_PAUSE_Y, ICON_PAUSE_X+68-1, ICON_PAUSE_Y+64-1);
    }
        else
    {
      DWIN_ICON_Not_Filter_Show(ICON, ICON_Pause_0, ICON_PAUSE_X, ICON_PAUSE_Y);
      if (HMI_flag.language < Language_Max)
      {
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Pause_0, WORD_PAUSE_X, WORD_PAUSE_Y);
      }
    }
  #endif
}
void Item_Control_Reset(const uint16_t line)
{
    if (HMI_flag.language<Language_Max)
    DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_Reset, LBLX, line + JPN_OFFSET);   //rock_j_info
}
//继续打印
void ICON_Continue()
{
  // rock_20211118
  Show_JPN_pause_title();
  #if ENABLED(DWIN_CREALITY_480_LCD)
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    if (select_print.now == 1)
    {
      DWIN_ICON_Not_Filter_Show(ICON, ICON_Continue_1, ICON_PAUSE_X, ICON_PAUSE_Y);
      if (HMI_flag.language < Language_Max)
      {
        DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_keep_print_1, WORD_PAUSE_X, WORD_PAUSE_Y);
      }
      DWIN_Draw_Rectangle(0, Color_White, ICON_PAUSE_X, ICON_PAUSE_Y, ICON_PAUSE_X+68-1, ICON_PAUSE_Y+64-1);
    }
    else
    {
      DWIN_ICON_Not_Filter_Show(ICON, ICON_Continue_0, ICON_PAUSE_X, ICON_PAUSE_Y);
      if (HMI_flag.language<Language_Max)
      {
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_keep_print_0, WORD_PAUSE_X, WORD_PAUSE_Y);
      }
    }
  #endif
}

void ICON_Stop()
{
  #if ENABLED(DWIN_CREALITY_480_LCD)
    
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    if (select_print.now == 2)
    {
      DWIN_ICON_Not_Filter_Show(ICON, ICON_Stop_1, ICON_STOP_X, ICON_STOP_Y);
      if (HMI_flag.language < Language_Max)
      {
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Stop_1, WORD_STOP_X, WORD_STOP_Y);
      }
      
      DWIN_Draw_Rectangle(0, Color_White, ICON_STOP_X, ICON_STOP_Y, ICON_STOP_X+68-1, ICON_STOP_Y+64-1);
    }
    else
    {
      DWIN_ICON_Not_Filter_Show(ICON, ICON_Stop_0, ICON_STOP_X, ICON_STOP_Y);
      if (HMI_flag.language<Language_Max)
      {
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Stop_0, WORD_STOP_X, WORD_STOP_Y);
      }
    }
  #endif
}

void Clear_Title_Bar()
{
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_Draw_Rectangle(1, Color_Bg_Blue, 0, 0, DWIN_WIDTH, 30);
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_Draw_Rectangle(1, Color_Bg_Blue, 0, 0, DWIN_WIDTH-1, 24);
    delay(2);
  #endif
}

void Clear_Remain_Time()
{
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_Draw_Rectangle(1, Color_Bg_Black, 176, 212, 200 + 20, 212 + 30);
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_Draw_Rectangle(1, Color_Bg_Black, NUM_RAMAIN_TIME_X, NUM_RAMAIN_TIME_Y, NUM_RAMAIN_TIME_X + 87, NUM_RAMAIN_TIME_Y + 24);
  #endif
}

void Clear_Print_Time()
{
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_Draw_Rectangle(1, Color_Bg_Black, 42, 212, 66 + 20, 212 + 30);
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_Draw_Rectangle(1, Color_Bg_Black, NUM_PRINT_TIME_X, NUM_PRINT_TIME_Y, NUM_PRINT_TIME_X + 87, NUM_PRINT_TIME_Y + 24);
  #endif
}

void Draw_Title(const char * const title)
{
  DWIN_Draw_String(false, false, DWIN_FONT_HEAD, Color_White, Color_Bg_Blue, 14, 4, (char*)title);
}

void Draw_Title(const __FlashStringHelper * title)
{
  DWIN_Draw_String(false, false, DWIN_FONT_HEAD, Color_White, Color_Bg_Blue, 14, 4, (char*)title);
}

void Clear_Menu_Area()
{
  #if ENABLED(DWIN_CREALITY_480_LCD)

  #elif ENABLED(DWIN_CREALITY_320_LCD)
    if(checkkey == Show_gcode_pic)
    {
      DWIN_Draw_Rectangle(1, All_Black, 0, 25, DWIN_WIDTH, STATUS_Y - 1);
    }
    else
    {
      DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, 25, DWIN_WIDTH, STATUS_Y - 1);
    }
  #endif
  delay(2);
}

void Clear_Main_Window()
{
  Clear_Title_Bar();
  Clear_Menu_Area();
  // DWIN_ICON_Show(ICON ,ICON_Clear_Screen, TITLE_X, TITLE_Y);  //rock_20220718
  // Draw_Mid_Status_Area(true); //rock_20230529 //rock_20220729
}

void Clear_Popup_Area()
{
  Clear_Title_Bar();
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, 31, DWIN_WIDTH, DWIN_HEIGHT);
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, 25, DWIN_WIDTH, DWIN_HEIGHT);
  #endif
}

void Draw_Popup_Bkgd_105()
{
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_Draw_Rectangle(1, Color_Bg_Window, 14, 105, 258, 374);
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_Draw_Rectangle(1, Color_Bg_Window, 8, 30, 232, 240);
  #endif
  delay(2);
}

void Draw_More_Icon(uint8_t line)
{
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_ICON_Show(ICON, ICON_More, 226, MBASE(line) - 3);
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_ICON_Show(ICON, ICON_More, 208, MBASE(line) - 3);
  #endif
}

void Draw_Language_Icon_AND_Word(uint8_t language,uint16_t line)
{
  // DWIN_ICON_Show(ICON, ICON_Word_CN + language, 65, line);
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_ICON_Show(ICON, ICON_Word_CN + language, 25, line);
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_ICON_Show(ICON, ICON_Word_CN + language, FIRST_X, line);
  #endif
  // DWIN_ICON_Not_Filter_Show(ICON, ICON_FLAG_CN + language, 25,line + 10);
}

void Draw_Menu_Cursor(const uint8_t line)
{
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_Draw_Rectangle(1, Rectangle_Color, 0, MBASE(line) - 18, 14, MBASE(line + 1) - 20);
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    // DWIN_Draw_Rectangle(1, Rectangle_Color, 0, MBASE(line) - 8, 14, MBASE(line + 1) - 10);
    DWIN_Draw_Rectangle(1, Rectangle_Color, 0, MBASE(line) - 8, 10, MBASE(line + 1) - 12);
    // DWIN_ICON_Not_Filter_Show(ICON, ICON_Rectangle, 17, 130);
  #endif
}
void Draw_laguage_Cursor(uint8_t line)
{
  DWIN_Draw_Rectangle(1, Rectangle_Color, REC_X1, line*LINE_INTERVAL+REC_Y1, REC_X2, REC_Y2+line*LINE_INTERVAL);
}
void Erase_Menu_Cursor(const uint8_t line)
{
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, MBASE(line) - 18, 14, MBASE(line + 1) - 20);
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, MBASE(line) - 8, 10, MBASE(line + 1) - 12);
  #endif
}

void Move_Highlight(const int16_t from, const uint16_t newline)
{
  Erase_Menu_Cursor(newline - from);
  Draw_Menu_Cursor(newline);
}
void Move_Highlight_Lguage(const int16_t from, const uint16_t newline)
{
  // Erase_Menu_Cursor(newline - from);
  uint8_t next_line=newline - from;
  // Draw_Menu_Cursor(newline);
  DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, next_line*REC_INTERVAL+REC_Y1, 10,REC_Y2+next_line*REC_INTERVAL);
  //  DWIN_Draw_Rectangle(1, Rectangle_Color, 0, MBASE(line) - 8, 10, MBASE(line + 1) - 12);
  DWIN_Draw_Rectangle(1, Rectangle_Color, REC_X1,newline*LINE_INTERVAL+REC_Y1, REC_X2, REC_Y2+newline*LINE_INTERVAL);
}
void Add_Menu_Line()
{
  Move_Highlight(1, MROWS);
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_Draw_Line(Line_Color, 16, MBASE(MROWS + 1) - 19, BLUELINE_X, MBASE(MROWS + 1) - 19);
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_Draw_Line(Line_Color, 16, MBASE(MROWS + 1) - 10, BLUELINE_X, MBASE(MROWS + 1) - 10);
  #endif
}

void Scroll_Menu(const uint8_t dir)
{
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_Frame_AreaMove(1, dir, MLINE, Color_Bg_Black, 15, 31, DWIN_WIDTH, 349);
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    if(checkkey == Selectlanguage) DWIN_Frame_AreaMove(1, dir, MLINE, Color_Bg_Black, 11, 25, DWIN_WIDTH-1, 315-1); //平移界面
    else DWIN_Frame_AreaMove(1, dir, MLINE, Color_Bg_Black, 11, 25, DWIN_WIDTH-1, 240); //平移界面
  #endif
  switch (dir)
  {
    case DWIN_SCROLL_DOWN: Move_Highlight(-1, 0); break;
    case DWIN_SCROLL_UP:   Add_Menu_Line(); break;
  }
}

void Scroll_Menu_Full(const uint8_t dir)
{
  //DWIN_Frame_AreaMove(1, dir, MLINE, Color_Bg_Black, 11, 25, DWIN_WIDTH-1, 240); //平移界面
  DWIN_Frame_AreaMove(1, dir, MLINE, Color_Bg_Black, 11, 25, DWIN_WIDTH-1, 320-1); //平移界面
  switch (dir)
  {
    // case DWIN_SCROLL_DOWN: Move_Highlight(-1, 0); break;
    // case DWIN_SCROLL_UP:   Add_Menu_Line();    break;
  }
}
void Scroll_Menu_Language(uint8_t dir)
{
  DWIN_Frame_AreaMove(1, dir, REC_INTERVAL, Color_Bg_Black, 11, 25,LINE_END_X+2, LINE_END_Y + 6 * LINE_INTERVAL); //平移界面
  switch (dir)
  {
    // case DWIN_SCROLL_DOWN: Move_Highlight_Lguage(-1, 0); break;
    // case DWIN_SCROLL_UP:   Add_Menu_Line(); break;
  }
}
//转换网格点
static xy_int8_t Converted_Grid_Point(uint8_t select_num)
{
  xy_int8_t grid_point;
  grid_point.x=select_num/GRID_MAX_POINTS_X;
  grid_point.y=select_num%GRID_MAX_POINTS_Y;
  return grid_point;
}
static void Toggle_Checkbox(xy_int8_t mesh_Curr,xy_int8_t mesh_Last,uint8_t dir)
{
  if(dir==DWIN_SCROLL_DOWN)
  {
    //将上一个点的背景置成相应颜色
    //再显示一次上一个点的数据
    //将当前框背景改成黑色
    //再显示一次当前点数据
  }
  else  //dir==DWIN_SCROLL_UP
  {
    //将上一个点的背景置成相应颜色
    //再显示一次上一个点的数据
    //将当前框背景改成黑色
    //再显示一次当前点数据
  }
}
//调平界面切换选中状态
static void Level_Scroll_Menu(const uint8_t dir,uint8_t select_num) 
{
  xy_int8_t mesh_Count, mesh_Last;
  mesh_Count=Converted_Grid_Point(select_num);    //转换网格点
  if(dir==DWIN_SCROLL_DOWN)mesh_Last=Converted_Grid_Point(select_num+1); 
  else mesh_Last=Converted_Grid_Point(select_num-1); //dir==DWIN_SCROLL_UP 
  //  PRINT_LOG("mesh_Count.x = ", mesh_Count.x, " mesh_Count.y = ", mesh_Count.y);
  //  PRINT_LOG("mesh_Last.x = ",mesh_Last.x, " mesh_Last.y = ",mesh_Last.y);
    //再显示一次上一个点的数据
    Draw_Dots_On_Screen(&mesh_Last,0,0);
    //将当前框背景改成黑色
    //再显示一次当前点数据
    Draw_Dots_On_Screen(&mesh_Count,1,Select_Block_Color); 
}
inline uint16_t nr_sd_menu_items()
{
  return card.get_num_Files() + !card.flag.workDirIsRoot;
}

void Draw_Menu_Icon(const uint8_t line, const uint8_t icon)
{
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_ICON_Not_Filter_Show(ICON, icon, 26, MBASE(line) - 3);
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_ICON_Not_Filter_Show(ICON, icon, 20, MBASE(line));
  #endif
  // DWIN_ICON_Show(ICON, icon, 26, MBASE(line) - 3);
}

void Erase_Menu_Text(const uint8_t line)
{
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_Draw_Rectangle(1, Color_Bg_Black, LBLX, MBASE(line) - 14, 271, MBASE(line) + 28);
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_Draw_Rectangle(1, Color_Bg_Black, LBLX, MBASE(line) - 8, 239, MBASE(line) + 18); //解决长文件名被覆盖的问题
  #endif
}

void Draw_Menu_Item(const uint8_t line, const uint8_t icon=0, const char * const label=nullptr, bool more=false)
{
  if (label) DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, LBLX, MBASE(line) - 1, (char*)label);
  if (icon) Draw_Menu_Icon(line, icon);
  if (more) Draw_More_Icon(line);
}

void Draw_Menu_Line(const uint8_t line, const uint8_t icon = 0, const char * const label = nullptr, bool more = false)
{
  Draw_Menu_Item(line, icon, label, more);
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_Draw_Line(Line_Color, 16, MBASE(line) + 34, 256, MBASE(line) + 34);
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_Draw_Line(Line_Color, 16, MBASE(line) + 26, BLUELINE_X, MBASE(line) + 26);
  #endif
}

void Draw_Chkb_Line(const uint8_t line, const bool mode)
{
  DWIN_Draw_Checkbox(Color_White, Color_Bg_Black, 225, MBASE(line) - 1, mode);
}

// The "Back" label is always on the first line
void Draw_Back_Label()
{
  if (HMI_flag.language < Language_Max)
  {
    #if ENABLED(DWIN_CREALITY_480_LCD)
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Back, 60, MBASE(0) + JPN_OFFSET + 6); // rock_j back
    #elif ENABLED(DWIN_CREALITY_320_LCD)
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Back, 42, MBASE(0) + JPN_OFFSET); // rock_j back
    #endif
  }
  else
  {
    DWIN_Frame_AreaCopy(1, 226, 179, BLUELINE_X, 189, LBLX, MBASE(0));
  }
}

// Draw "Back" line at the top
void Draw_Back_First(const bool is_sel=true)
{
  DWIN_Draw_Rectangle(1, Color_Bg_Black, 10, MBASE(0) - 9, 239, MBASE(0) + 18);
  Draw_Menu_Line(0, ICON_Back);
  Draw_Back_Label();
  if (is_sel) Draw_Menu_Cursor(0);
}

// Draw "temp" line at the top
static void Draw_Nozzle_Temp_Label(const bool is_sel=true)
{
  Draw_Menu_Line(0, ICON_SetEndTemp);
  HMI_ValueStruct.E_Temp = thermalManager.degTargetHotend(0);
  LIMIT(HMI_ValueStruct.E_Temp, HEATER_0_MINTEMP, thermalManager.hotend_max_target(0));
  DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(0)+TEMP_SET_OFFSET, HMI_ValueStruct.E_Temp);
  DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Hotend, 42, MBASE(0) + JPN_OFFSET);
}

inline bool Apply_Encoder(const ENCODER_DiffState &encoder_diffState, auto &valref)
{
  bool temp_var = false;
  if (encoder_diffState == ENCODER_DIFF_CW)
  {
    valref += EncoderRate.encoderMoveValue;
    if(Extruder == checkkey)
    {
      if(valref>(EXTRUDE_MAXLENGTH_e * MINUNITMULT))
      {
        valref=(EXTRUDE_MAXLENGTH_e * MINUNITMULT);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW)
  {
    valref -= EncoderRate.encoderMoveValue;
  }
  else if(encoder_diffState == ENCODER_DIFF_ENTER)
  {
    temp_var = true;
  }
  return temp_var;
}

//
// Draw Menus
//

#define MOTION_CASE_RATE   1
#define MOTION_CASE_ACCEL  2
#define MOTION_CASE_JERK   (MOTION_CASE_ACCEL + ENABLED(HAS_CLASSIC_JERK))
#define MOTION_CASE_STEPS  (MOTION_CASE_JERK + 1)
#define MOTION_CASE_TOTAL  MOTION_CASE_STEPS

#define PREPARE_CASE_MOVE  1
#define PREPARE_CASE_DISA  2
#define PREPARE_CASE_HOME  3
// #define PREPARE_CASE_HEIH (PREPARE_CASE_HOME + ENABLED(USE_AUTOZ_TOOL))
// #define PREPARE_CASE_HEIH (PREPARE_CASE_HOME + 1)
#define PREPARE_CASE_ZOFF  (PREPARE_CASE_HOME + ENABLED(HAS_ZOFFSET_ITEM))
#define PREPARE_CASE_INSTORK (PREPARE_CASE_ZOFF + ENABLED(HAS_HOTEND))
#define PREPARE_CASE_OUTSTORK (PREPARE_CASE_INSTORK + ENABLED(HAS_HOTEND))
#define PREPARE_CASE_PLA  (PREPARE_CASE_OUTSTORK + ENABLED(HAS_HOTEND))
#define PREPARE_CASE_ABS  (PREPARE_CASE_PLA + ENABLED(HAS_HOTEND))
#define PREPARE_CASE_COOL (PREPARE_CASE_ABS + EITHER(HAS_HOTEND, HAS_HEATED_BED))
#define PREPARE_CASE_LANG (PREPARE_CASE_COOL + 1)
#define PREPARE_CASE_TOTAL PREPARE_CASE_LANG

#define CONTROL_CASE_TEMP 1
#define  CONTROL_CASE_MOVE  (CONTROL_CASE_TEMP + 1)
#define CONTROL_CASE_SAVE  (CONTROL_CASE_MOVE + ENABLED(EEPROM_SETTINGS))
#define CONTROL_CASE_LOAD  (CONTROL_CASE_SAVE + ENABLED(EEPROM_SETTINGS))
#define CONTROL_CASE_SHOW_DATA (CONTROL_CASE_LOAD+1)     // 调平数据展示
#define CONTROL_CASE_RESET (CONTROL_CASE_SHOW_DATA + ENABLED(EEPROM_SETTINGS))

//#define CONTROL_CASE_ADVSET (CONTROL_CASE_RESET + 1)  //rock_20210726
//#define CONTROL_CASE_INFO  (CONTROL_CASE_ADVSET + 1)
#define CONTROL_CASE_INFO  (CONTROL_CASE_RESET + 1)
#define CONTROL_CASE_TOTAL CONTROL_CASE_INFO

#define TUNE_CASE_SPEED 1
#define TUNE_CASE_TEMP  (TUNE_CASE_SPEED + ENABLED(HAS_HOTEND))
#define TUNE_CASE_BED   (TUNE_CASE_TEMP + ENABLED(HAS_HEATED_BED))
#define TUNE_CASE_FAN   (TUNE_CASE_BED + ENABLED(HAS_FAN))
#define TUNE_CASE_ZOFF  (TUNE_CASE_FAN + ENABLED(HAS_ZOFFSET_ITEM))
#define TUNE_CASE_TOTAL TUNE_CASE_ZOFF

#define TEMP_CASE_TEMP  (0 + ENABLED(HAS_HOTEND))
#define TEMP_CASE_BED   (TEMP_CASE_TEMP + ENABLED(HAS_HEATED_BED))
#define TEMP_CASE_FAN   (TEMP_CASE_BED + ENABLED(HAS_FAN))
#define TEMP_CASE_PLA   (TEMP_CASE_FAN + ENABLED(HAS_HOTEND))
#define TEMP_CASE_ABS   (TEMP_CASE_PLA + ENABLED(HAS_HOTEND))

#define TEMP_CASE_HM_PID   (TEMP_CASE_ABS + 1)    // 手动PID设置 hand movement
#define TEMP_CASE_Auto_PID (TEMP_CASE_HM_PID + 1) // 自动PID设置

#define TEMP_CASE_TOTAL TEMP_CASE_Auto_PID

//手动PID相关的宏定义
#define HM_PID_CASE_NOZZ_P  1
#define HM_PID_CASE_NOZZ_I  (1 + HM_PID_CASE_NOZZ_P)
#define HM_PID_CASE_NOZZ_D  (1 + HM_PID_CASE_NOZZ_I)
#define HM_PID_CASE_BED_P   (1 + HM_PID_CASE_NOZZ_D)
#define HM_PID_CASE_BED_I   (1 + HM_PID_CASE_BED_P)
#define HM_PID_CASE_BED_D   (1 + HM_PID_CASE_BED_I)
#define HM_PID_CASE_TOTAL   HM_PID_CASE_BED_D

#define PREHEAT_CASE_TEMP (0 + ENABLED(HAS_HOTEND))
#define PREHEAT_CASE_BED  (PREHEAT_CASE_TEMP + ENABLED(HAS_HEATED_BED))
#define PREHEAT_CASE_FAN  (PREHEAT_CASE_BED + ENABLED(HAS_FAN))
#define PREHEAT_CASE_SAVE (PREHEAT_CASE_FAN + ENABLED(EEPROM_SETTINGS))
#define PREHEAT_CASE_TOTAL PREHEAT_CASE_SAVE

#define ADVSET_CASE_HOMEOFF   1
#define ADVSET_CASE_PROBEOFF  (ADVSET_CASE_HOMEOFF + ENABLED(HAS_ONESTEP_LEVELING))
#define ADVSET_CASE_HEPID     (ADVSET_CASE_PROBEOFF + ENABLED(HAS_HOTEND))
#define ADVSET_CASE_BEDPID    (ADVSET_CASE_HEPID + ENABLED(HAS_HEATED_BED))
#define ADVSET_CASE_PWRLOSSR  (ADVSET_CASE_BEDPID + ENABLED(POWER_LOSS_RECOVERY))
#define ADVSET_CASE_TOTAL     ADVSET_CASE_PWRLOSSR




//
// Draw Menus
//
void DWIN_Draw_Label(const uint16_t y, char *string) {
  DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, LBLX, y, string);
}
void DWIN_Draw_Label(const uint16_t y, const __FlashStringHelper *title) {
  DWIN_Draw_Label(y, (char*)title);
}

void draw_move_en(const uint16_t line) {
  #ifdef USE_STRING_TITLES
    DWIN_Draw_Label(line, F("Move"));
  #else
    DWIN_Frame_AreaCopy(1, 69, 61, 102, 71, LBLX, line); // "Move"
  #endif
}

void DWIN_Frame_TitleCopy(uint8_t id, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) { DWIN_Frame_AreaCopy(id, x1, y1, x2, y2, 14, 8); }
void Item_Prepare_Move(const uint8_t row)
{
  if (HMI_flag.language < Language_Max)
  {
    // DWIN_Frame_AreaCopy(1, 159, 70, 200, 84, LBLX, MBASE(row));
    #if ENABLED(DWIN_CREALITY_480_LCD)
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Move, 52, MBASE(row) + JPN_OFFSET);
    #elif ENABLED(DWIN_CREALITY_320_LCD)
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Move, 42, MBASE(row) + JPN_OFFSET);
    #endif
  }
  else
  {
    // "Move"
    draw_move_en(MBASE(row));
  }
  Draw_Menu_Line(row, ICON_Axis);
  Draw_More_Icon(row);
}

void Item_Prepare_Disable(const uint8_t row)
{
  if (HMI_flag.language < Language_Max)
  {
    // DWIN_Frame_AreaCopy(1, 204, 70, 259, 82, LBLX, MBASE(row));
    #if ENABLED(DWIN_CREALITY_480_LCD)
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_CloseMotion, 52, MBASE(row) + JPN_OFFSET + 2);
    #elif ENABLED(DWIN_CREALITY_320_LCD)
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_CloseMotion, 42, MBASE(row) + JPN_OFFSET + 2);
    #endif
  }
  else
  {
    #ifdef USE_STRING_TITLES
      DWIN_Draw_Label(MBASE(row), GET_TEXT_F(MSG_DISABLE_STEPPERS));
    #else
      DWIN_Frame_AreaCopy(1, 103, 59, 200, 74, LBLX, MBASE(row)); // "Disable Stepper"
    #endif
  }
  Draw_Menu_Line(row, ICON_CloseMotor);
}

void Item_Prepare_Home(const uint8_t row)
{
  if (HMI_flag.language < Language_Max)
  {
    // DWIN_Frame_AreaCopy(1, 0, 89, 41, 101, LBLX, MBASE(row));
    #if ENABLED(DWIN_CREALITY_480_LCD)
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Home, 52, MBASE(row) + JPN_OFFSET);
    #elif ENABLED(DWIN_CREALITY_320_LCD)
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Home, 42, MBASE(row) + JPN_OFFSET);
    #endif
  }
  else
  {
    #ifdef USE_STRING_TITLES
      DWIN_Draw_Label(MBASE(row), GET_TEXT_F(MSG_AUTO_HOME));
    #else
      DWIN_Frame_AreaCopy(1, 202, 61, 271, 71, LBLX, MBASE(row)); // "Auto Home"
    #endif
  }
  Draw_Menu_Line(row, ICON_Homing);
}

#if ANY(USE_AUTOZ_TOOL,USE_AUTOZ_TOOL_2)

  void Item_Prepare_Height(const uint8_t row)
  {
    if (HMI_flag.language < Language_Max)
    {
      // DWIN_Frame_AreaCopy(1, 0, 89, 41, 101, LBLX, MBASE(row));
      #if ENABLED(DWIN_CREALITY_480_LCD)
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_align_height, 52, MBASE(row) + JPN_OFFSET);
      #elif ENABLED(DWIN_CREALITY_320_LCD)
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_align_height, 42, MBASE(row) + JPN_OFFSET);
      #endif
    }
    else
    {
      #ifdef USE_STRING_TITLES
        DWIN_Draw_Label(MBASE(row), GET_TEXT_F(MSG_PRESSURE_HEIGHT));
      #else
        DWIN_Frame_AreaCopy(1, 202, 61, 271, 71, LBLX, MBASE(row)); // "Pressure height"
      #endif
    }
    Draw_Menu_Line(row, ICON_Alignheight);
  }

#endif

#if HAS_ZOFFSET_ITEM

  void Item_Prepare_Offset(const uint8_t row)
  {
    if (HMI_flag.language < Language_Max)
    {
      #if HAS_BED_PROBE
        #if ENABLED(DWIN_CREALITY_480_LCD)
          DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Zoffset, 52, MBASE(row) + JPN_OFFSET);
        #elif ENABLED(DWIN_CREALITY_320_LCD)
          DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Zoffset, 42, MBASE(row) + JPN_OFFSET);
        #endif
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 2, 2, VALUERANGE_X - 14, MBASE(row), probe.offset.z * 100);
      #else
        DWIN_Frame_AreaCopy(1, 43, 89, 98, 101, LBLX, MBASE(row));
      #endif
    }
    else
    {
      #if HAS_BED_PROBE
        #ifdef USE_STRING_TITLES
          DWIN_Draw_Label(MBASE(row), GET_TEXT_F(MSG_ZPROBE_ZOFFSET));
        #else
          DWIN_Frame_AreaCopy(1, 93, 179, 141, 189, LBLX, MBASE(row));    // "Z-Offset"
        #endif
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 2, 2, VALUERANGE_X - 14, MBASE(row), probe.offset.z * 100);
      #else
        #ifdef USE_STRING_TITLES
          DWIN_Draw_Label(MBASE(row), GET_TEXT_F(MSG_SET_HOME_OFFSETS));
        #else
          DWIN_Frame_AreaCopy(1, 1, 76, 106, 86, LBLX, MBASE(row));       // "Set home offsets"
        #endif
      #endif
    }
    Draw_Menu_Line(row, ICON_SetHome);
  }

#endif
void Item_Prepare_instork(const uint8_t row)
  {
    if (HMI_flag.language < Language_Max)
    {
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_IN_STORK, 42, MBASE(row) + JPN_OFFSET);    
      DWIN_ICON_Show(ICON, ICON_More, 208, MBASE(row) - 3);
    }
    Draw_Menu_Line(row, ICON_IN_STORK);
  }
  void Item_Prepare_outstork(const uint8_t row)
  {
    if (HMI_flag.language < Language_Max)
    {
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_OUT_STORK, 42, MBASE(row) + JPN_OFFSET);    
      DWIN_ICON_Show(ICON, ICON_More, 208, MBASE(row) - 3);
    }
    Draw_Menu_Line(row, ICON_OUT_STORK);
  }
#if HAS_HOTEND
  void Item_Prepare_PLA(const uint8_t row)
  {
    if (HMI_flag.language < Language_Max)
    {
      // DWIN_Frame_AreaCopy(1, 100, 89, 151, 101, LBLX, MBASE(row));
      #if ENABLED(DWIN_CREALITY_480_LCD)
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_PLA, 52, MBASE(row) + JPN_OFFSET);
      #elif ENABLED(DWIN_CREALITY_320_LCD)
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_PLA, 42, MBASE(row) + JPN_OFFSET);
      #endif
    }
    else
    {
      #ifdef USE_STRING_TITLES
        DWIN_Draw_Label(MBASE(row), F("Preheat " PREHEAT_1_LABEL));
      #else
        DWIN_Frame_AreaCopy(1, 107, 76, 156, 86, LBLX, MBASE(row));       // "Preheat"
        DWIN_Frame_AreaCopy(1, 157, 76, 181, 86, LBLX + 52, MBASE(row));  // "PLA"
      #endif
    }
    Draw_Menu_Line(row, ICON_PLAPreheat);
  }

  void Item_Prepare_ABS(const uint8_t row)
  {
    if (HMI_flag.language < Language_Max)
    {
      #if ENABLED(DWIN_CREALITY_480_LCD)
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_ABS, 52, MBASE(row) + JPN_OFFSET);
      #elif ENABLED(DWIN_CREALITY_320_LCD)
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_ABS, 42, MBASE(row) + JPN_OFFSET);
      #endif
    }
    else {
      #ifdef USE_STRING_TITLES
        DWIN_Draw_Label(MBASE(row), F("Preheat " PREHEAT_2_LABEL));
      #else
        DWIN_Frame_AreaCopy(1, 107, 76, 156, 86, LBLX, MBASE(row));       // "Preheat"
        DWIN_Frame_AreaCopy(1, 172, 76, 198, 86, LBLX + 52, MBASE(row));  // "ABS"
      #endif
    }
    Draw_Menu_Line(row, ICON_ABSPreheat);
  }
#endif

#if HAS_PREHEAT
  void Item_Prepare_Cool(const uint8_t row)
  {
    if (HMI_flag.language < Language_Max)
    {
      #if ENABLED(DWIN_CREALITY_480_LCD)
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Cool, 52, MBASE(row) + JPN_OFFSET);
      #elif ENABLED(DWIN_CREALITY_320_LCD)
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Cool, 42, MBASE(row) + JPN_OFFSET);
      #endif
      // DWIN_Frame_AreaCopy(1,   1, 104,  56, 117, LBLX, MBASE(row));
    }
    else
    {
      #ifdef USE_STRING_TITLES
        DWIN_Draw_Label(MBASE(row), GET_TEXT_F(MSG_COOLDOWN));
      #else
        DWIN_Frame_AreaCopy(1, 200,  76, 264,  86, LBLX, MBASE(row));      // "Cooldown"
      #endif
    }
    Draw_Menu_Line(row, ICON_Cool);
  }
#endif

void Item_Prepare_Lang(const uint8_t row)
{
  #if ENABLED(DWIN_CREALITY_480_LCD)
    if (HMI_flag.language<Language_Max)
    {
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_language, 52, MBASE(row) + JPN_OFFSET);
      DWIN_ICON_Show(ICON, ICON_More, 225, 314 - 3);
    }
    else 
    {
      #ifdef USE_STRING_TITLES
        DWIN_Draw_Label(MBASE(row), F("UI Language"));
      #else
        DWIN_Frame_AreaCopy(1, 0, 194, 121, 207, LBLX, MBASE(row)); // "Language selection"
      #endif
    }
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    if (HMI_flag.language < Language_Max)
    {
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_language, 42, MBASE(row) + JPN_OFFSET);
      DWIN_ICON_Show(ICON, ICON_More, 208, 212);
    }
    else 
    {
      #ifdef USE_STRING_TITLES
        DWIN_Draw_Label(MBASE(row), F("UI Language"));
      #else
        DWIN_Frame_AreaCopy(1, 0, 194, 121, 207, LBLX, MBASE(row)); // "Language selection"
      #endif
    }
  #endif
  Draw_Menu_Icon(row, ICON_Language);
  Draw_Menu_Line(row, ICON_Language);
}

void Draw_Prepare_Menu() 
{
  Clear_Main_Window();
  Draw_Mid_Status_Area(true); //rock_20230529 //更新一次全部参数
  HMI_flag.Refresh_bottom_flag=false;//标志不刷新底部参数
  const int16_t scroll = MROWS - index_prepare; // Scrolled-up lines
  #define PSCROL(L) (scroll + (L))
  #define PVISI(L)  WITHIN(PSCROL(L), 0, MROWS)

  if (HMI_flag.language < Language_Max)
  {
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Prepare, TITLE_X, TITLE_Y);
  }
  else 
  {
    #ifdef USE_STRING_HEADINGS
      Draw_Title(GET_TEXT_F(MSG_PREPARE));
    #else
      DWIN_Frame_TitleCopy(1, 178, 2, 229, 14); // "Prepare"
    #endif
  }

  if (PVISI(0)) Draw_Back_First(select_prepare.now == 0);                         // < Back
  if (PVISI(PREPARE_CASE_MOVE)) Item_Prepare_Move(PSCROL(PREPARE_CASE_MOVE));     // Move >
  if (PVISI(PREPARE_CASE_DISA)) Item_Prepare_Disable(PSCROL(PREPARE_CASE_DISA));  // Disable Stepper
  if (PVISI(PREPARE_CASE_HOME)) Item_Prepare_Home(PSCROL(PREPARE_CASE_HOME));     // Auto Home
  // #if ENABLED(USE_AUTOZ_TOOL)
  //   if (PVISI(PREPARE_CASE_HEIH)) Item_Prepare_Height(PSCROL(PREPARE_CASE_HEIH)); // pressure test height to obtain the Z offset value
  // #endif
  // #if ENABLED(USE_AUTOZ_TOOL_2)
  //   if (PVISI(PREPARE_CASE_HEIH)) Item_Prepare_Height(PSCROL(PREPARE_CASE_HEIH)); // pressure test height to obtain the Z offset value
  // #endif
  #if HAS_ZOFFSET_ITEM
    if (PVISI(PREPARE_CASE_ZOFF)) Item_Prepare_Offset(PSCROL(PREPARE_CASE_ZOFF)); // Edit Z-Offset / Babystep / Set Home Offset
  #endif
  if (PVISI(PREPARE_CASE_INSTORK)) Item_Prepare_instork(PSCROL(PREPARE_CASE_INSTORK)); // Edit Z-Offset / Babystep / Set Home Offset
  if (PVISI(PREPARE_CASE_OUTSTORK)) Item_Prepare_outstork(PSCROL(PREPARE_CASE_OUTSTORK)); // Edit Z-Offset / Babystep / Set Home Offset
  #if HAS_HOTEND
    if (PVISI(PREPARE_CASE_PLA)) Item_Prepare_PLA(PSCROL(PREPARE_CASE_PLA));      // Preheat PLA
    if (PVISI(PREPARE_CASE_ABS)) Item_Prepare_ABS(PSCROL(PREPARE_CASE_ABS));      // Preheat ABS
  #endif
  #if HAS_PREHEAT
    if (PVISI(PREPARE_CASE_COOL)) Item_Prepare_Cool(PSCROL(PREPARE_CASE_COOL));   // Cooldown
  #endif
  if (PVISI(PREPARE_CASE_LANG)) Item_Prepare_Lang(PSCROL(PREPARE_CASE_LANG));     // Language CN/EN

  if (select_prepare.now) Draw_Menu_Cursor(PSCROL(select_prepare.now));
}

void Item_Control_Info(const uint16_t line)
{
  if (HMI_flag.language < Language_Max)
  {
    // DWIN_Frame_AreaCopy(1, 231, 102, 258, 116, LBLX, line);  // rock_20210726
    #if ENABLED(DWIN_CREALITY_480_LCD)
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_info_new, LBLX, line + JPN_OFFSET);   // rock_j_info
    #elif ENABLED(DWIN_CREALITY_320_LCD)
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_info_new, LBLX, line + JPN_OFFSET);   // rock_j_info
    #endif
  }
  else
  {
    #ifdef USE_STRING_TITLES
      DWIN_Draw_Label(line, F("Info"));
    #else
      DWIN_Frame_AreaCopy(1, 0, 104, 24, 114, LBLX, line);
    #endif
  }
}

static void Item_Temp_HMPID(const uint16_t line)
{
  if (HMI_flag.language < Language_Max)
  {
    #if ENABLED(DWIN_CREALITY_480_LCD)
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_PID_Manually, LBLX + 2, line + JPN_OFFSET);   // rock_j_info
    #elif ENABLED(DWIN_CREALITY_320_LCD)
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_PID_Manually, 42, line + JPN_OFFSET);   // rock_j_info
    #endif
  }
}
static void Item_Temp_AutoPID(const uint16_t line)
{
  if (HMI_flag.language < Language_Max)
  {
    #if ENABLED(DWIN_CREALITY_480_LCD)
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Auto_PID, LBLX - 10, line + JPN_OFFSET + 5);   // rock_j_info
    #elif ENABLED(DWIN_CREALITY_320_LCD)
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Auto_PID, 42, line + JPN_OFFSET);   // rock_j_info
    #endif
  }
}

void Draw_Control_Menu()
{
  Clear_Main_Window();
  Draw_Mid_Status_Area(true); //rock_20230529 //更新一次全部参数
  HMI_flag.Refresh_bottom_flag=false;//标志刷新底部参数
  #if CONTROL_CASE_TOTAL >= 6
    const int16_t scroll = MROWS - index_control; // Scrolled-up lines
    #define CSCROL(L) (scroll + (L))
  #else
    #define CSCROL(L) (L)
  #endif
  #define CLINE(L)  MBASE(CSCROL(L))
  #define CVISI(L)  WITHIN(CSCROL(L), 0, MROWS)

  if (CVISI(0)) Draw_Back_First(select_control.now == 0);                         // < Back

  if (HMI_flag.language < Language_Max)
  {
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Control, TITLE_X, TITLE_Y);
    #if ENABLED(DWIN_CREALITY_480_LCD)
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Temp, 60, CLINE(CONTROL_CASE_TEMP) + JPN_OFFSET);
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Motion, 60, CLINE(CONTROL_CASE_MOVE) + JPN_OFFSET);
      #if ENABLED(EEPROM_SETTINGS)
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Store, 60, CLINE(CONTROL_CASE_SAVE) + JPN_OFFSET);
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Read, 60, CLINE(CONTROL_CASE_LOAD) + JPN_OFFSET);
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Reset, 60, CLINE(CONTROL_CASE_RESET) + JPN_OFFSET);
      #endif
    #elif ENABLED(DWIN_CREALITY_320_LCD)
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Temp, 42, CLINE(CONTROL_CASE_TEMP) + JPN_OFFSET);
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Motion, 42, CLINE(CONTROL_CASE_MOVE) + JPN_OFFSET);
      #if ENABLED(EEPROM_SETTINGS)
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Store, 42, CLINE(CONTROL_CASE_SAVE) + JPN_OFFSET);
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Read, 42, CLINE(CONTROL_CASE_LOAD) + JPN_OFFSET);
        DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_EDIT_LEVEL_DATA, 42, CLINE(CONTROL_CASE_SHOW_DATA)+JPN_OFFSET);
        // DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Reset, 42, CLINE(CONTROL_CASE_RESET) + JPN_OFFSET);
      #endif
    #endif
  }
  if(CVISI(CONTROL_CASE_RESET))DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Reset, 42, CLINE(CONTROL_CASE_RESET) + JPN_OFFSET);
  if (CVISI(CONTROL_CASE_INFO)) Item_Control_Info(CLINE(CONTROL_CASE_INFO));
  if (select_control.now && CVISI(select_control.now))
    Draw_Menu_Cursor(CSCROL(select_control.now));

  // Draw icons and lines
  #define _TEMP_ICON(N, I, M) do { \
    if (CVISI(N)) { \
      Draw_Menu_Line(CSCROL(N), I); \
      if (M) { \
        Draw_More_Icon(CSCROL(N)); \
             } \
    } \
  } while(0)

  _TEMP_ICON(CONTROL_CASE_TEMP, ICON_Temperature, true);
  _TEMP_ICON(CONTROL_CASE_MOVE, ICON_Motion, true);

  #if ENABLED(EEPROM_SETTINGS)
    _TEMP_ICON(CONTROL_CASE_SAVE, ICON_WriteEEPROM, false);
    _TEMP_ICON(CONTROL_CASE_LOAD, ICON_ReadEEPROM, false);
    _TEMP_ICON(CONTROL_CASE_SHOW_DATA, ICON_Edit_Level_Data, false);
    _TEMP_ICON(CONTROL_CASE_RESET, ICON_ResumeEEPROM, false);
  #endif
  _TEMP_ICON(CONTROL_CASE_INFO, ICON_Info, true);
}

static void Show_Temp_Default_Data(const uint8_t line,uint8_t index)
{
  switch (index)
  {
    case TEMP_CASE_TEMP:
      HMI_ValueStruct.E_Temp = thermalManager.degTargetHotend(0);
      LIMIT(HMI_ValueStruct.E_Temp, HEATER_0_MINTEMP, thermalManager.hotend_max_target(0));
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(line)+TEMP_SET_OFFSET, HMI_ValueStruct.E_Temp);
      break;
    case TEMP_CASE_BED:
      HMI_ValueStruct.Bed_Temp = thermalManager.degTargetBed();
      LIMIT(HMI_ValueStruct.Bed_Temp, BED_MINTEMP, BED_MAX_TARGET);
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(line)+TEMP_SET_OFFSET, HMI_ValueStruct.Bed_Temp);
      break;
    case TEMP_CASE_FAN:
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(line)+TEMP_SET_OFFSET, thermalManager.fan_speed[0]);
      break;
    default:
      break;
  }
}

void Draw_Tune_Menu()
{
  Clear_Main_Window();
  Draw_Mid_Status_Area(true); //rock_20230529 //更新一次全部参数
  if (HMI_flag.language < Language_Max)
  {
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Setup, TITLE_X, TITLE_Y); //设置
    #if ENABLED(DWIN_CREALITY_480_LCD)
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_PrintSpeed, 60, MBASE(TUNE_CASE_SPEED) + JPN_OFFSET);
      #if HAS_HOTEND
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Hotend, 60, MBASE(TUNE_CASE_TEMP) + JPN_OFFSET);
      #endif
      #if HAS_HEATED_BED
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Bedend, 60, MBASE(TUNE_CASE_BED) + JPN_OFFSET);
      #endif
      #if HAS_FAN
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Fan, 60, MBASE(TUNE_CASE_FAN) + JPN_OFFSET);
      #endif
      #if HAS_ZOFFSET_ITEM
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Zoffset, 60, MBASE(TUNE_CASE_ZOFF) + JPN_OFFSET);
      #endif
    #elif ENABLED(DWIN_CREALITY_320_LCD)
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_PrintSpeed, 42, MBASE(TUNE_CASE_SPEED) + JPN_OFFSET);
      #if HAS_HOTEND
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Hotend, 42, MBASE(TUNE_CASE_TEMP) + JPN_OFFSET);
      #endif
      #if HAS_HEATED_BED
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Bedend, 42, MBASE(TUNE_CASE_BED) + JPN_OFFSET);
      #endif
      #if HAS_FAN
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Fan, 42, MBASE(TUNE_CASE_FAN) + JPN_OFFSET);
      #endif
      #if HAS_ZOFFSET_ITEM
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Zoffset, 42, MBASE(TUNE_CASE_ZOFF) + JPN_OFFSET);
      #endif
    #endif
  }
  else
  {
    ;
  }

  //显示数据
  Draw_Back_First(select_tune.now == 0);
  if (select_tune.now) Draw_Menu_Cursor(select_tune.now);

  Draw_Menu_Line(TUNE_CASE_SPEED, ICON_Speed);
  DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(TUNE_CASE_SPEED) + PRINT_SET_OFFSET, feedrate_percentage);

  #if HAS_HOTEND
    Draw_Menu_Line(TUNE_CASE_TEMP, ICON_HotendTemp);
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(TUNE_CASE_TEMP) + PRINT_SET_OFFSET, thermalManager.degTargetHotend(0));
  #endif
  #if HAS_HEATED_BED
    Draw_Menu_Line(TUNE_CASE_BED, ICON_BedTemp);
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(TUNE_CASE_BED) + PRINT_SET_OFFSET, thermalManager.degTargetBed());
  #endif
  #if HAS_FAN
    Draw_Menu_Line(TUNE_CASE_FAN, ICON_FanSpeed);
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(TUNE_CASE_FAN) + PRINT_SET_OFFSET, thermalManager.fan_speed[0]);
  #endif
  #if HAS_ZOFFSET_ITEM
    Draw_Menu_Line(TUNE_CASE_ZOFF, ICON_Zoffset);
    DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 2, 2, VALUERANGE_X - 14, MBASE(TUNE_CASE_ZOFF), BABY_Z_VAR * 100);
  #endif
}

void draw_max_en(const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 245, 119, 271, 130, LBLX, line);   // "Max"
}
void draw_max_accel_en(const uint16_t line) {
  draw_max_en(line);
  DWIN_Frame_AreaCopy(1, 1, 135, 79, 145, LBLX + 30, line); // "Acceleration" rock_20210919
}
void draw_speed_en(const uint16_t inset, const uint16_t line) 
{
  DWIN_Frame_AreaCopy(1, 184, 119, 224, 132, LBLX + 30, line); // "Speed"
}
void draw_jerk_en(const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 64, 119, 106, 129, LBLX + 30, line); // "Jerk"
}
void draw_steps_per_mm(const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 1, 149, 120, 161, LBLX, line);   // "Steps-per-mm"
}
void say_x(const uint16_t inset, const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 95, 104, 102, 114, LBLX + inset, line); // "X"
}
void say_y(const uint16_t inset, const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 104, 104, 110, 114, LBLX + inset, line); // "Y"
}
void say_z(const uint16_t inset, const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 112, 104, 120, 114, LBLX + inset, line); // "Z"
}
void say_e(const uint16_t inset, const uint16_t line) {
  DWIN_Frame_AreaCopy(1, 237, 119, 244, 129, LBLX + inset, line); // "E"
}

void Draw_Motion_Menu() {
  Clear_Main_Window();  
  Draw_Mid_Status_Area(true); //rock_20230529
  HMI_flag.Refresh_bottom_flag=false;//标志刷新底部参数
  if (HMI_flag.language<Language_Max) 
  {
    // DWIN_Frame_TitleCopy(1, 1, 16, 28, 28);                                              // "Motion"
    DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_Motion_Title, TITLE_X, TITLE_Y);
    // DWIN_Frame_AreaCopy(1, 173, 133, 228, 147, LBLX, MBASE(MOTION_CASE_RATE));           // Max speed
    DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_MaxSpeed, 42, MBASE(MOTION_CASE_RATE)+JPN_OFFSET);
    // DWIN_Frame_AreaCopy(1, 173, 133, 200, 147, LBLX, MBASE(MOTION_CASE_ACCEL));         // Max...
    // DWIN_Frame_AreaCopy(1, 28, 149, 69, 161, LBLX + 27, MBASE(MOTION_CASE_ACCEL) + 1);  // Acceleration
    DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_Acc, 42, MBASE(MOTION_CASE_ACCEL)+JPN_OFFSET);
    #if HAS_CLASSIC_JERK
      // DWIN_Frame_AreaCopy(1, 173, 133, 200, 147, LBLX, MBASE(MOTION_CASE_JERK));        // Max
      // DWIN_Frame_AreaCopy(1, 1, 180, 28, 192, LBLX + 27, MBASE(MOTION_CASE_JERK) + 1);
      // DWIN_Frame_AreaCopy(1, 202, 133, 228, 147, LBLX + 54, MBASE(MOTION_CASE_JERK));   // Jerk
       DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_Corner, 42, MBASE(MOTION_CASE_JERK)+JPN_OFFSET);
    #endif
    // DWIN_Frame_AreaCopy(1, 153, 148, 194, 161, LBLX, MBASE(MOTION_CASE_STEPS));         // Flow ratio
    DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_Step, 42, MBASE(MOTION_CASE_STEPS)+JPN_OFFSET);
  }
  else 
  {
    #ifdef USE_STRING_HEADINGS
      Draw_Title(GET_TEXT_F(MSG_MOTION));
    #else
      DWIN_Frame_TitleCopy(1, 144, 16, 189, 26);                                        // "Motion"
    #endif
    #ifdef USE_STRING_TITLES
      DWIN_Draw_Label(MBASE(MOTION_CASE_RATE), F("Feedrate"));
      DWIN_Draw_Label(MBASE(MOTION_CASE_ACCEL), GET_TEXT_F(MSG_ACCELERATION));
      #if HAS_CLASSIC_JERK
        DWIN_Draw_Label(MBASE(MOTION_CASE_JERK), GET_TEXT_F(MSG_JERK));
      #endif
      DWIN_Draw_Label(MBASE(MOTION_CASE_STEPS), GET_TEXT_F(MSG_STEPS_PER_MM));
    #else
      draw_max_en(MBASE(MOTION_CASE_RATE)); draw_speed_en(27, MBASE(MOTION_CASE_RATE)); // "Max Speed"
      draw_max_accel_en(MBASE(MOTION_CASE_ACCEL));                                      // "Max Acceleration"
      #if HAS_CLASSIC_JERK
        draw_max_en(MBASE(MOTION_CASE_JERK)); draw_jerk_en(MBASE(MOTION_CASE_JERK));    // "Max Jerk"
      #endif
      draw_steps_per_mm(MBASE(MOTION_CASE_STEPS));                                      // "Steps-per-mm"
    #endif
  }

  Draw_Back_First(select_motion.now == 0);
  if (select_motion.now) Draw_Menu_Cursor(select_motion.now);

  uint8_t i = 0;
  #define _MOTION_ICON(N) Draw_Menu_Line(++i, ICON_MaxSpeed + (N) - 1)
  _MOTION_ICON(MOTION_CASE_RATE); Draw_More_Icon(i);
  _MOTION_ICON(MOTION_CASE_ACCEL); Draw_More_Icon(i);
  #if HAS_CLASSIC_JERK
    _MOTION_ICON(MOTION_CASE_JERK); Draw_More_Icon(i); Draw_Menu_Icon(MOTION_CASE_JERK,  ICON_MaxJerk);
  #endif
  _MOTION_ICON(MOTION_CASE_STEPS); Draw_More_Icon(i);Draw_Menu_Icon(MOTION_CASE_STEPS,  ICON_Step);
}

//
// Draw Popup Windows
//
#if HAS_HOTEND || HAS_HEATED_BED
  /*
    toohigh:报警类型： 1 过高  0 过低
    Error_id： 0 喷嘴 非0 热床
  */
  void DWIN_Popup_Temperature(const bool toohigh,int8_t Error_id)
  {
    Clear_Popup_Area();
    Draw_Popup_Bkgd_105();
    #if ENABLED(DWIN_CREALITY_480_LCD)
      if (toohigh)
      {
        DWIN_ICON_Show(ICON, ICON_TempTooHigh, 102, 165);
        delay(2);
        if (HMI_flag.language<Language_Max)
        {
          DWIN_ICON_Show(HMI_flag.language, LANGUAGE_TempHigh, 14, 222);
          delay(2);
        }
        else
        {
          DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, 36, 300, F("Nozzle or Bed temperature"));
          DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, 92, 300, F("is too high"));
        }
      }
      else
      {
        DWIN_ICON_Show(ICON, ICON_TempTooLow, 102, 165);
        delay(2);
        if (HMI_flag.language < Language_Max)
        {
          DWIN_ICON_Show(HMI_flag.language, LANGUAGE_TempLow, 14, 222);
          delay(5);
        }
      }
    #elif ENABLED(DWIN_CREALITY_320_LCD)
      if (toohigh)  //过高
      {
        DWIN_ICON_Show(ICON, ICON_TempTooHigh, 82, 45);
        delay(5);
        if(Error_id)  //热床
        {          
          if (HMI_flag.language<Language_Max)
          {
            DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Bed_HIGH, 14, 134);
            delay(5);
          }
        }
        else //喷嘴
        {
          if (HMI_flag.language<Language_Max)
          {
            DWIN_ICON_Show(HMI_flag.language, LANGUAGE_TempHigh, 14, 134);
            delay(5);
          }
        }
      }
      else //过低
      {
         DWIN_ICON_Show(ICON, ICON_TempTooLow, 82, 45);
          delay(2);
        if(Error_id)  //热床
        {
          if (HMI_flag.language < Language_Max)
          {
            DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Bed_LOW, 14, 134);
            delay(5);
          }
        }
        else //喷嘴
        {
          if (HMI_flag.language < Language_Max)
          {
            DWIN_ICON_Show(HMI_flag.language, LANGUAGE_TempLow, 14, 134);
            delay(5);
          }
        }      
      }
    #endif
  }

#endif

void Draw_Popup_Bkgd_60()
{
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_Draw_Rectangle(1, Color_Bg_Window, 14, 60, 258, 330);
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_Draw_Rectangle(1, Color_Bg_Window, 6, 30, 232, 240);
  #endif
}

#if HAS_HOTEND
  void Popup_Window_ETempTooLow()
  {
    Clear_Main_Window();
    Draw_Popup_Bkgd_60();
    #if ENABLED(DWIN_CREALITY_480_LCD)
 
    #elif ENABLED(DWIN_CREALITY_320_LCD)
      DWIN_ICON_Not_Filter_Show(ICON, ICON_TempTooLow, 94, 44);
      delay(2);
      if(HMI_flag.language < Language_Max)
      {
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_TempLow, 14, 136);
        delay(2);
        DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Confirm, 79, 194);
        delay(2);
      }
    #endif
  }
#endif

void Draw_PID_Error()
{
  Clear_Main_Window();
  Draw_Popup_Bkgd_60();
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_ICON_Show(ICON, ICON_TempTooLow, 102, 105);
    if(HMI_flag.language < Language_Max)
    {
      if(1 == HMI_flag.PID_ERROR_FLAG)
      {
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_TempLow, 14, 190);
      }
      else
      {
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Bed_LOW, 14, 190);
      }
      DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Confirm, 86, 280);
    }
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_ICON_Show(ICON, ICON_TempTooLow, 94, 44);
    if(HMI_flag.language < Language_Max)
    {
      if(1 == HMI_flag.PID_ERROR_FLAG)
      {
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_TempLow, 14, 136);
      }
      else
      {
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Bed_LOW, 14, 136);
      }
      DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Confirm, 79, 194);
    }
  #endif
}

void Popup_Window_Resume()
{
  Clear_Popup_Area();
  Draw_Popup_Bkgd_105();
  #if ENABLED(DWIN_CREALITY_480_LCD)
 
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    if (HMI_flag.language < Language_Max)
    {
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_PowerLoss, 14, 25 + 20);
      DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Cancel, 26, 194);
      DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Powerloss_go, 132, 194);
    }
    else
    {

    }
  #endif
}
void Leveling_Error()
{
  Clear_Main_Window();
  Draw_Popup_Bkgd_60();
  HMI_flag.Refresh_bottom_flag=true;//标志不刷新底部参数
  // DWIN_ICON_Show(ICON, ICON_TempTooHigh, 82, 45);  
  if(HMI_flag.language<Language_Max)
  {    
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_LEVEL_ERROR, WORD_LEVELING_ERR_X, WORD_LEVELING_ERR_Y);
    //  DWIN_ICON_Show(HMI_flag.language, LANGUAGE_TempHigh, 14, 134);
  }  
  delay(2);
  DWIN_ICON_Not_Filter_Show(ICON,ICON_LEVELING_ERR, ICON_LEVELING_ERR_X, ICON_LEVELING_ERR_Y);
  delay(2);
}
//参数：state:  
//enum Auto_Hight_Stage:uint8_t{Nozz_Start,Nozz_Hot,Nozz_Clear,Nozz_Hight,Nozz_Finish};
void Popup_Window_Height(uint8_t state)
{
  Clear_Main_Window();
  // Draw_Popup_Bkgd_60();
  HMI_flag.High_Status=state; //赋值对高状态，动态显示
  HMI_flag.Refresh_bottom_flag=true;//标志不刷新底部参数
  #if ENABLED(DWIN_CREALITY_480_LCD)
 
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    // DWIN_ICON_Show(ICON, ICON_BLTouch, 82, 45);

    //喷嘴加热；喷嘴清洁；喷嘴对高词条
    if (HMI_flag.language < Language_Max)
    {
    DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_NOZZLE_HOT, WORD_NOZZ_HOT_X, WORD_NOZZ_HOT_Y);  //喷嘴加热词条
    DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_NOZZLE_CLRAR, WORD_NOZZ_HOT_X, WORD_NOZZ_HOT_Y+WORD_Y_SPACE);  //喷嘴清洁词条
    DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_NOZZLE_HIGHT, WORD_NOZZ_HOT_X, WORD_NOZZ_HOT_Y+WORD_Y_SPACE+WORD_Y_SPACE);  //喷嘴清洁词条
    } 
    //喷嘴加热；喷嘴清洁；喷嘴对高词条 
      switch (state)
      {
        case Nozz_Start:
        DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_AUTO_HIGHT_TITLE, WORD_TITLE_X, WORD_TITLE_Y);  //对高中        
        DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_HEAT_HOT, ICON_NOZZ_HOT_X, ICON_NOZZ_HOT_Y); //喷嘴未加热图标
        DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_CLEAR_HOT, ICON_NOZZ_HOT_X,ICON_NOZZ_HOT_Y+ICON_Y_SPACE); //喷嘴未清洁图标
        DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_HIGH_HOT, ICON_NOZZ_HOT_X, ICON_NOZZ_HOT_Y+ICON_Y_SPACE+ICON_Y_SPACE); //喷嘴未对高图标
        break;
        case Nozz_Hot:
        DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_AUTO_HIGHT_TITLE, WORD_TITLE_X, WORD_TITLE_Y);  //对高中        
        DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_HEAT, ICON_NOZZ_HOT_X, ICON_NOZZ_HOT_Y); //喷嘴加热未完成图标
        DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_CLEAR_HOT, ICON_NOZZ_HOT_X, ICON_NOZZ_HOT_Y+ICON_Y_SPACE); //喷嘴清洁图标
        DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_HIGH_HOT, ICON_NOZZ_HOT_X, ICON_NOZZ_HOT_Y+ICON_Y_SPACE+ICON_Y_SPACE); //喷嘴对高图标        
        break;
       case Nozz_Clear:
        DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_AUTO_HIGHT_TITLE, WORD_TITLE_X, WORD_TITLE_Y);  //对高中        
        DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_HEAT, ICON_NOZZ_HOT_X, ICON_NOZZ_HOT_Y); //喷嘴加热未完成图标
        DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_CLEAR, ICON_NOZZ_HOT_X, ICON_NOZZ_HOT_Y+ICON_Y_SPACE); //喷嘴清洁图标
        DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_HIGH_HOT, ICON_NOZZ_HOT_X,  ICON_NOZZ_HOT_Y+ICON_Y_SPACE+ICON_Y_SPACE); //喷嘴对高图标        
        break;
        case Nozz_Hight:
        DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_AUTO_HIGHT_TITLE, WORD_TITLE_X, WORD_TITLE_Y);  //对高中        
        DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_HEAT, ICON_NOZZ_HOT_X, ICON_NOZZ_HOT_Y); //喷嘴加热未完成图标
        DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_CLEAR, ICON_NOZZ_HOT_X, ICON_NOZZ_HOT_Y+ICON_Y_SPACE); //喷嘴清洁图标
        DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_HIGH, ICON_NOZZ_HOT_X,  ICON_NOZZ_HOT_Y+ICON_Y_SPACE+ICON_Y_SPACE); //喷嘴对高图标
        break;
       case Nozz_Finish:
        DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_LEVEL_FINISH , WORD_TITLE_X, WORD_TITLE_Y);  //对高完成
        DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_HEAT, ICON_NOZZ_HOT_X, ICON_NOZZ_HOT_Y); //喷嘴加热未完成图标
        DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_CLEAR, ICON_NOZZ_HOT_X, ICON_NOZZ_HOT_Y+ICON_Y_SPACE); //喷嘴清洁图标
        DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_HIGH, ICON_NOZZ_HOT_X,  ICON_NOZZ_HOT_Y+ICON_Y_SPACE+ICON_Y_SPACE); //喷嘴对高图标
        break;

      default:
        break;
      }
    
    DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_LINE, LINE_X, LINE_Y);
    DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_LINE, LINE_X, LINE_Y+LINE_Y_SPACE);
  #endif
}

void Popup_Window_Home(const bool parking/*=false*/)
{
  Clear_Main_Window();
  Draw_Popup_Bkgd_60();
  HMI_flag.Refresh_bottom_flag=true;//标志不刷新底部参数
  #if ENABLED(DWIN_CREALITY_480_LCD)
 
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_ICON_Show(ICON, ICON_BLTouch, 82, 45);
    if (HMI_flag.language < Language_Max)
    {
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Homeing, 14, 134);
    }
  #endif
}

void Popup_Window_Feedstork_Tip(bool parking/*=false*/)
{
  Clear_Main_Window();
  Draw_Mid_Status_Area(true); //rock_20230529
  // Draw_Popup_Bkgd_60();
  #if ENABLED(DWIN_CREALITY_320_LCD)
  if(parking)
  {
    DWIN_ICON_Show(ICON, ICON_STORKING_TIP, 50, 25);
    if (HMI_flag.language < Language_Max)
    {
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_STORKING_TIP1, 24, 174); //提示斜口45°裁剪耗材
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_IN_TITLE, 29, 1);  //进料标题
    }
  }
  else 
  {
    DWIN_ICON_Show(ICON, ICON_OUT_STORKING_TIP, 50, 25);
    if (HMI_flag.language < Language_Max)
    {
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_OUT_STORKING_TIP2, 24, 174); //提示斜口45°裁剪耗材
       DWIN_ICON_Show(HMI_flag.language, LANGUAGE_OUT_TITLE, 29, 1);  //进料标题
    }
  }
  #endif
}
void Popup_Window_Feedstork_Finish(bool parking/*=false*/)
{
  // Clear_Main_Window();
  HMI_flag.Refresh_bottom_flag=true;//标志不刷新底部参数
  DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, 25, DWIN_WIDTH-1, 320 - 1);
  // Draw_Popup_Bkgd_60();
  #if ENABLED(DWIN_CREALITY_320_LCD)
  if(parking) //进料确认
  {
    DWIN_ICON_Show(ICON, ICON_STORKED_TIP, 50, 25);
    if (HMI_flag.language < Language_Max)
    {
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_STORKING_TIP2, 24, 174); //送料zhi
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_IN_TITLE, 29, 1);  //进料标题
    }    
  }
  else //退料确认
  {
    DWIN_ICON_Show(ICON, ICON_OUT_STORKED_TIP, 50, 25);
    if (HMI_flag.language < Language_Max)
    {
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_OUT_STORKED_TIP2, 24, 174); //提示斜口45°裁剪耗材
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_OUT_TITLE, 29, 1);  //进料标题
    }
    
  }
  
  #endif
}
// rock_20211025 上电回零提示
// static void Electricity_back_to_zero(const bool parking/*=false*/)
// {
//   Clear_Main_Window();
//   Draw_Popup_Bkgd_60();
//   DWIN_ICON_Show(ICON, ICON_BLTouch, 101, 105);
//   if (HMI_flag.language<Language_Max) 
//   {
//     // rock_20211025
//     DWIN_ICON_Show(ICON, ICON_Power_On_Home_C, 14, 205);
//   }
//   else
//   {
//     DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (250 - 8 * (parking ? 7 : 15)) / 2, 230, parking ? F("Parking") : F("Moving axis init..."));
//     DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (250 - 8 * 23) / 2, 260, F("This operation is forbidden!"));
//     DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, (250 - 8 * 23) / 2, 290, F("We need to go back to zero."));
//   }
// }
#if ENABLED(SHOW_GRID_VALUES)  //如果显示调平值
static uint16_t Choose_BG_Color(float offset_value)
{
  uint16_t fill_color;
  //填充颜色
  if(HMI_flag.Need_boot_flag)fill_color=Flat_Color;
  else
  {
    if((offset_value>Slope_Big_Max)||(offset_value<Slope_Big_Min))fill_color=Slope_Big; //
    else if((offset_value>Slope_Small_Max)||(offset_value<Slope_Small_Min))fill_color=Slope_Small; //
    else if((offset_value>Relatively_Flat_Max)||(offset_value<Relatively_Flat_Min))fill_color=Relatively_Flat; //
    else fill_color=Flat_Color; //
  }
    return fill_color;
}
/* 
   mesh_Count：需要处理的网格点的坐标
  Set_En: 1 需要手动设置背景色 0 自动设置背景色  2 
  Set_BG_Color：需要手动设置的背景色。
  注意： if(0==Set_En && 0!=Set_BG_Color) 表示 只改变字体的背景色为Set_BG_Color，不改变选中块颜色
*/
  void Draw_Dots_On_Screen(xy_int8_t *mesh_Count,uint8_t Set_En,uint16_t Set_BG_Color)
  { //计算位置，填充颜色，填充值 //Рассчитать позицию, цвет заливки, значение заливки
    uint16_t value_LU_x,value_LU_y; 
    uint16_t rec_LU_x,rec_LU_y,rec_RD_x,rec_RD_y;
    uint16_t rec_fill_color;
    float z_offset_value=0;
    static float first_num=0;
    if(HMI_flag.Need_boot_flag)z_offset_value=G29_level_num;
    else z_offset_value=z_values[mesh_Count->x][mesh_Count->y];
    if(checkkey!=Leveling&&checkkey!=Level_Value_Edit)return;//只有在调平界面才运行显示调平值 //Значение выравнивания может отображаться только в интерфейсе выравнивания.
     
    //计算矩形区域  //Вычислить площадь прямоугольника
    rec_LU_x=Rect_LU_X_POS+mesh_Count->x*X_Axis_Interval;
    // rec_LU_y=Rect_LU_Y_POS+mesh_Count->y*Y_Axis_Interval;
    rec_LU_y=Rect_LU_Y_POS-mesh_Count->y*Y_Axis_Interval;
    rec_RD_x=Rect_RD_X_POS+mesh_Count->x*X_Axis_Interval;
    // rec_RD_y=Rect_RD_Y_POS+mesh_Count->y*Y_Axis_Interval;
    rec_RD_y=Rect_RD_Y_POS-mesh_Count->y*Y_Axis_Interval;
    //补偿值的位置 //Положение компенсационного значения
    value_LU_x=rec_LU_x+1;
    // value_LU_y=rec_LU_y+4;
    value_LU_y=rec_LU_y+(rec_RD_y-rec_LU_y)/2-6;
    //填充颜色 //цвет заливки
    if(!Set_En)rec_fill_color=Choose_BG_Color(z_offset_value);//自动设置 //Устанавливается автоматически
    else if(1==Set_En)rec_fill_color=Set_BG_Color;  //手动填充选中块颜色 //Вручную залейте выбранный цвет блока,
    else rec_fill_color=Select_Color;  //

    // if(mesh_Count->x==0 && mesh_Count->y==0) z_offset_value=-8.36;
   
    if(2==Set_En) //  0==Set_En && 0==Set_BG_Color 才填充选中块颜色 //Заливка только выбранного цвета блока
    {
      DWIN_Draw_Rectangle(1, Color_Bg_Black, rec_LU_x, rec_LU_y, rec_RD_x, rec_RD_y);
      DWIN_Draw_Rectangle(0, rec_fill_color, rec_LU_x, rec_LU_y, rec_RD_x, rec_RD_y); //选中块为黑色 //Выбранный блок черный
      DWIN_Draw_Z_Offset_Float(font6x12,Color_White,rec_fill_color, 1, 2, value_LU_x, value_LU_y,z_offset_value*100);//左上角坐标 //Координаты левого верхнего угла
    }
    else if(1==Set_En)
    {
      // DWIN_Draw_Rectangle(1, Color_Bg_Black, rec_LU_x, rec_LU_y, rec_RD_x, rec_RD_y);
      DWIN_Draw_Rectangle(1, rec_fill_color, rec_LU_x, rec_LU_y, rec_RD_x, rec_RD_y);    
      DWIN_Draw_Z_Offset_Float(font6x12, Color_Bg_Black,rec_fill_color, 1, 2, value_LU_x, value_LU_y,z_offset_value*100);//左上角坐标 //Координаты левого верхнего угла
    }    
    else //  0==Set_En && 0==Set_BG_Color 才填充选中块颜色 //Заливка только выбранного цвета блока
    {
      // PRINT_LOG("rec_LU_x = ", rec_LU_x, "rec_LU_y =", rec_LU_y," rec_RD_x = ", rec_RD_x,"rec_RD_y = ", rec_RD_y);
      DWIN_Draw_Rectangle(1, Color_Bg_Black, rec_LU_x, rec_LU_y, rec_RD_x, rec_RD_y);
      DWIN_Draw_Rectangle(0, rec_fill_color, rec_LU_x, rec_LU_y, rec_RD_x, rec_RD_y);  
      if(HMI_flag.Need_boot_flag)
      {        
        if(z_offset_value<10)
        DWIN_Draw_Z_Offset_Float(font6x12, rec_fill_color,Color_Bg_Black, 2, 0, value_LU_x+2, value_LU_y,z_offset_value);//左上角坐标 //Координаты левого верхнего угла
        else 
        DWIN_Draw_Z_Offset_Float(font6x12, rec_fill_color,Color_Bg_Black, 2, 0, value_LU_x+6, value_LU_y,z_offset_value);//左上角坐标 //Координаты левого верхнего угла
      }
      else  
      {
        DWIN_Draw_Z_Offset_Float(font6x12, rec_fill_color/*Color_White*/,Color_Bg_Black, 1, 2, value_LU_x, value_LU_y,z_offset_value*100);//左上角坐标 //Координаты левого верхнего угла
      }
             
    }    
  }
  
  void Refresh_Leveling_Value()  //刷新调平值 //Обновить значения выравнивания
  {  
    xy_int8_t Grid_Count={0};
    for(Grid_Count.x=0;Grid_Count.x<GRID_MAX_POINTS_X;Grid_Count.x++)
    {
      // if(2==Grid_Count.x)delay(50); //必须有这个参数，否则会刷新不正常 //Этот параметр должен присутствовать, иначе обновление будет ненормальным.
      for(Grid_Count.y=0;Grid_Count.y<GRID_MAX_POINTS_Y;Grid_Count.y++)
      {        
        Draw_Dots_On_Screen(&Grid_Count,0,0);
        delay(20); //必须有这个参数，否则会刷新不正常 //Этот параметр должен присутствовать, иначе обновление будет ненормальным.
      }
    }
  }
#endif
#if HAS_ONESTEP_LEVELING

  void Popup_Window_Leveling()
  {
    Clear_Main_Window();
    Draw_Popup_Bkgd_60();
    #if ENABLED(DWIN_CREALITY_480_LCD)
    
     #elif ENABLED(DWIN_CREALITY_320_LCD)
      HMI_flag.Refresh_bottom_flag=true;//标志不刷新底部参数 
     DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, 25, DWIN_WIDTH, 320 - 1);
     Clear_Menu_Area();
     #if ENABLED(SHOW_GRID_VALUES)  //显示网格值
       //添加一个网格图
      DWIN_Draw_Rectangle(0, Color_Yellow, OUTLINE_LEFT_X, OUTLINE_LEFT_Y, OUTLINE_RIGHT_X, OUTLINE_RIGHT_Y);  //画外框  
      if(HMI_flag.Edit_Only_flag)
      {
        DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_LEVELING_EDIT, BUTTON_EDIT_X, BUTTON_EDIT_Y);  //0x97   编辑按钮
        DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Confirm,BUTTON_OK_X, BUTTON_OK_Y);// 确认按钮
        Draw_Leveling_Highlight(0); //默认选择0
      }   
    #else  //
      //Draw_Popup_Bkgd_60();
      //DWIN_ICON_Show(ICON, ICON_AutoLeveling, 82, 45);
    #endif
      if (HMI_flag.language < Language_Max) 
      {
        #if ENABLED(SHOW_GRID_VALUES)  //显示网格值
          if(checkkey == Control)
          DWIN_ICON_Show(HMI_flag.language, LANGUAGE_EDIT_DATA_TITLE, TITLE_X, TITLE_Y); //显示到顶部
        //  DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_leveing, WORD_TITLE_X, WORD_TITLE_Y);  //显示到顶部
          else 
          DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_leveing, WORD_TITLE_X, WORD_TITLE_Y);  //显示到顶部
        #else
          DWIN_ICON_Show(HMI_flag.language, LANGUAGE_leveing, 14, 134);
        #endif  
      }
    #endif
  }
#endif

void Draw_Show_G_Select_Highlight(const bool sel)
{
  HMI_flag.select_flag = sel;
  uint16_t c1 = sel ? Button_Select_Color : All_Black,
                 c2 = sel ?  All_Black : Button_Select_Color,
                 c3 = sel ?  All_Black : Button_Select_Color;

  #if ENABLED(DWIN_CREALITY_320_LCD) 

  switch (select_show_pic.now)
  {
    #if ENABLED(USER_LEVEL_CHECK)
    case 0:
      c1=All_Black;
      c2=All_Black;
      c3=Button_Select_Color;
    break;
    #else
     
    #endif
    case 1:
      c1=All_Black;
      c2=Button_Select_Color;
      c3=All_Black;
    break;
    case 2:
      c1=Button_Select_Color;
      c2=All_Black;
      c3=All_Black;
    break;
  default:
    break;
  }
  DWIN_Draw_Rectangle(0, c1, BUTTON_X-1, BUTTON_Y-1, BUTTON_X+82, BUTTON_Y+32);
  DWIN_Draw_Rectangle(0, c1, BUTTON_X-2, BUTTON_Y-2, BUTTON_X+83, BUTTON_Y+33);
  DWIN_Draw_Rectangle(0, c2, BUTTON_X+BUTTON_OFFSET_X-1, BUTTON_Y-1, BUTTON_X+BUTTON_OFFSET_X+82, BUTTON_Y+32);
  DWIN_Draw_Rectangle(0, c2, BUTTON_X+BUTTON_OFFSET_X-2, BUTTON_Y-2, BUTTON_X+BUTTON_OFFSET_X+83, BUTTON_Y+33);
  #if ENABLED(USER_LEVEL_CHECK)  //使用调平校准功能
  DWIN_Draw_Rectangle(0, c3, ICON_ON_OFF_X-1,ICON_ON_OFF_Y-1, ICON_ON_OFF_X+36, ICON_ON_OFF_Y+21);
  DWIN_Draw_Rectangle(0, c3, ICON_ON_OFF_X-2, ICON_ON_OFF_Y-2, ICON_ON_OFF_X+37,ICON_ON_OFF_Y+22);
  #endif
  #endif
}

void Draw_Select_Highlight(const bool sel)
{
  HMI_flag.select_flag = sel;
  const uint16_t c1 = sel ? Button_Select_Color : Color_Bg_Window,
                 c2 = sel ? Color_Bg_Window : Button_Select_Color;
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_Draw_Rectangle(0, c1, 25, 279, 126, 318);
    DWIN_Draw_Rectangle(0, c1, 24, 278, 127, 319);
    DWIN_Draw_Rectangle(0, c2, 145, 279, 246, 318);
    DWIN_Draw_Rectangle(0, c2, 144, 278, 247, 319);
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_Draw_Rectangle(0, c1, 25, 193, 108, 226);
    DWIN_Draw_Rectangle(0, c1, 24, 192, 109, 227);
    DWIN_Draw_Rectangle(0, c2, 131, 193, 214, 226);
    DWIN_Draw_Rectangle(0, c2, 130, 192, 215, 227);
  #endif
}

void Popup_window_PauseOrStop()
{
  Clear_Main_Window();
  Draw_Popup_Bkgd_60();
  HMI_flag.Refresh_bottom_flag=true;//标志不刷新底部参数
  #if ENABLED(DWIN_CREALITY_480_LCD)
   
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    if (HMI_flag.language < Language_Max)
    {
      if (select_print.now == 1)
      {
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_PausePrint, 14, 45);
      }
      else if (select_print.now == 2)
      {
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_StopPrint, 14, 45);
      }
      DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Confirm, 26, 194);
      DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Cancel, 132, 194);
    }
    else
    {
  
    }
    Draw_Select_Highlight(true);
  #endif
}
//开机引导弹出
void Popup_window_boot(uint8_t type_popup)
{
  Clear_Main_Window();
  //Draw_Popup_Bkgd_60();
  DWIN_Draw_Rectangle(1, Color_Bg_Window, POPUP_BG_X_LU,POPUP_BG_Y_LU,POPUP_BG_X_RD, POPUP_BG_Y_RD);
  HMI_flag.Refresh_bottom_flag=true;//标志不刷新底部参数
  #if ENABLED(DWIN_CREALITY_480_LCD)   
  #elif ENABLED(DWIN_CREALITY_320_LCD)
  switch (type_popup)
  {
    case Clear_nozz_bed:
      if (HMI_flag.language < Language_Max)
      {
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_CLEAR_HINT, WORD_HINT_CLEAR_X, WORD_HINT_CLEAR_Y);
        DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Confirm, BUTTON_BOOT_X, BUTTON_BOOT_Y);//确定按钮
        //增加一个白色选中块
        DWIN_Draw_Rectangle(0, Button_Select_Color, BUTTON_BOOT_X-1, BUTTON_BOOT_Y-1, BUTTON_BOOT_X+82, BUTTON_BOOT_Y+32);
        DWIN_Draw_Rectangle(0, Button_Select_Color, BUTTON_BOOT_X-2, BUTTON_BOOT_Y-2, BUTTON_BOOT_X+83, BUTTON_BOOT_Y+33);
      }
     break;
    case High_faild_clear:
      if (HMI_flag.language < Language_Max)
      {
        DWIN_ICON_Not_Filter_Show(ICON, ICON_HIGH_ERR, ICON_HIGH_ERR_X, ICON_HIGH_ERR_Y);//确定按钮
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_HIGH_ERR_CLEAR, WORD_HIGH_CLEAR_X, WORD_HIGH_CLEAR_Y);
        DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Confirm, BUTTON_BOOT_X, BUTTON_BOOT_Y);//确定按钮
        //增加一个白色选中块
        DWIN_Draw_Rectangle(0, Button_Select_Color, BUTTON_BOOT_X-1, BUTTON_BOOT_Y-1, BUTTON_BOOT_X+82, BUTTON_BOOT_Y+32);
        DWIN_Draw_Rectangle(0, Button_Select_Color, BUTTON_BOOT_X-2, BUTTON_BOOT_Y-2, BUTTON_BOOT_X+83, BUTTON_BOOT_Y+33);
      }
      break;
    case Level_faild_QR:
      if (HMI_flag.language < Language_Max)
      {
        if(Chinese == HMI_flag.language)
          DWIN_ICON_Not_Filter_Show(ICON, ICON_Level_ERR_QR_CH, ICON_LEVEL_ERR_X, ICON_LEVEL_ERR_Y);
        else
          DWIN_ICON_Not_Filter_Show(ICON, ICON_Level_ERR_QR_EN, ICON_LEVEL_ERR_X, ICON_LEVEL_ERR_Y);
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_SCAN_QR, WORD_SCAN_QR_X, WORD_SCAN_QR_Y);
      }
     break;
      case Boot_undone:
      if (HMI_flag.language < Language_Max)
      {
         DWIN_ICON_Show(HMI_flag.language, LANGUAGE_BOOT_undone, WORD_HINT_CLEAR_X, WORD_HINT_CLEAR_Y);
        DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Confirm, BUTTON_BOOT_X, BUTTON_BOOT_Y);//确定按钮
        //增加一个白色选中块
        DWIN_Draw_Rectangle(0, Button_Select_Color, BUTTON_BOOT_X-1, BUTTON_BOOT_Y-1, BUTTON_BOOT_X+82, BUTTON_BOOT_Y+32);
        DWIN_Draw_Rectangle(0, Button_Select_Color, BUTTON_BOOT_X-2, BUTTON_BOOT_Y-2, BUTTON_BOOT_X+83, BUTTON_BOOT_Y+33);
      }
      case CRTouch_err:
      if (HMI_flag.language < Language_Max)
      {
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_CRTouch_error, WORD_HINT_CLEAR_X, WORD_HINT_CLEAR_Y);
        DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Confirm, BUTTON_BOOT_X, BUTTON_BOOT_Y);//确定按钮
        //增加一个白色选中块
        DWIN_Draw_Rectangle(0, Button_Select_Color, BUTTON_BOOT_X-1, BUTTON_BOOT_Y-1, BUTTON_BOOT_X+82, BUTTON_BOOT_Y+32);
        DWIN_Draw_Rectangle(0, Button_Select_Color, BUTTON_BOOT_X-2, BUTTON_BOOT_Y-2, BUTTON_BOOT_X+83, BUTTON_BOOT_Y+33);
      }
     break;
    default:
      break;
  }
  #endif
}
void Draw_Printing_Screen()
{
  #if ENABLED(DWIN_CREALITY_480_LCD)
    if(HMI_flag.language < Language_Max)
    {
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Printing, TITLE_X, TITLE_Y); //打印中标题
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_PrintTime, 43, 181);  //打印时间
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_RemainTime, 177, 181); //剩余时间
    }
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    if(HMI_flag.language < Language_Max)
    {
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Printing, TITLE_X, TITLE_Y); //打印中标题
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_PrintTime, WORD_PRINT_TIME_X, WORD_PRINT_TIME_Y);  //打印时间
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_RemainTime, WORD_RAMAIN_TIME_X, WORD_RAMAIN_TIME_Y); //剩余时间
    }
  #endif
}

void Draw_Print_ProgressBar()
{
  // DWIN_ICON_Not_Filter_Show(ICON, ICON_Bar, 15, 98);
  // DWIN_Draw_Rectangle(1, BarFill_Color, 16 + _card_percent * 240 / 100, 98, 256, 110); //rock_20210917
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_ICON_Not_Filter_Show(Background_ICON, Background_min + _card_percent, 15, 98);

    DWIN_Draw_IntValue(true, true, 0, font8x16, Percent_Color, Color_Bg_Black, 3, 109, 133, _card_percent);
    DWIN_Draw_String(false, false, font8x16, Percent_Color, Color_Bg_Black, 133 + 15, 133 - 3, F("%"));   // rock_20220728
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_ICON_Not_Filter_Show(Background_ICON, BG_PRINTING_CIRCLE_MIN + _card_percent, ICON_PERCENT_X, ICON_PERCENT_Y);
    // DWIN_Draw_IntValue(true, true, 0, font8x16, Percent_Color, Color_Bg_Black, 3, NUM_PRECENT_X, NUM_PRECENT_Y, _card_percent);
    // DWIN_Draw_String(false, false, font8x16, Percent_Color, Color_Bg_Black, PRECENT_X, PRECENT_Y, F("%"));
  #endif
}

void Draw_Print_ProgressElapsed() 
{
  bool temp_flash_elapsed_time = false;
  static bool temp_elapsed_record_flag = true;
  duration_t elapsed = print_job_timer.duration(); // print timer
  #if ENABLED(DWIN_CREALITY_480_LCD)
    if(elapsed.value < 360000)   // rock_20210903
    {
      temp_flash_elapsed_time = false;
      //如果剩余时间》100h，就填充黑色并刷新时间
      if(temp_elapsed_record_flag != temp_flash_elapsed_time)
      {
        Clear_Print_Time();
      }
      DWIN_Draw_IntValue(true, true, 1, font8x16, Color_White, Color_Bg_Black, 2, 42, 212 + 3, elapsed.value / 3600);
      DWIN_Draw_IntValue(true, true, 1, font8x16, Color_White, Color_Bg_Black, 2, 66, 212 + 3, (elapsed.value % 3600) / 60);
      DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, 58 + 6, 211, F(":"));
    }
    else 
    {
      temp_flash_elapsed_time = true;
      if(temp_elapsed_record_flag != temp_flash_elapsed_time)
      {
        Clear_Print_Time();
      }
      DWIN_Draw_String(false, false, font6x12, Color_White, Color_Bg_Black, 42, 212 + 3, F(">100H"));
    }
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    if(elapsed.value < 360000)
    {
      temp_flash_elapsed_time = false;
      //如果剩余时间》100h，就填充黑色并刷新时间
      if(temp_elapsed_record_flag != temp_flash_elapsed_time)
      {
        Clear_Print_Time();
      }
      DWIN_Draw_IntValue(true, true, 1, font8x16, Color_White, Color_Bg_Black, 2, NUM_PRINT_TIME_X, NUM_PRINT_TIME_Y, elapsed.value / 3600);
      DWIN_Draw_IntValue(true, true, 1, font8x16, Color_White, Color_Bg_Black, 2, NUM_PRINT_TIME_X+24, NUM_PRINT_TIME_Y, (elapsed.value % 3600) / 60);
      DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, NUM_PRINT_TIME_X+22, NUM_PRINT_TIME_Y-3, F(":"));
    }
    else 
    {
      temp_flash_elapsed_time = true;
      if(temp_elapsed_record_flag != temp_flash_elapsed_time)
      {
        Clear_Print_Time();
      }
      DWIN_Draw_String(false, false, font6x12, Color_White, Color_Bg_Black, NUM_PRINT_TIME_X, NUM_PRINT_TIME_Y, F(">100H"));
    }
  #endif
  temp_elapsed_record_flag = temp_flash_elapsed_time;
}

void Draw_Print_ProgressRemain()
{
  bool temp_flash_remain_time = false;
  static bool temp_record_flag = true;
  #if ENABLED(DWIN_CREALITY_480_LCD)

  #elif ENABLED(DWIN_CREALITY_320_LCD)
    if(HMI_flag.cloud_printing_flag)
    {
      Clear_Remain_Time();
      DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, 153, 131 + 3, F("------"));
      return ;
    }
    if(_remain_time < 360000)   // rock_20210903
    {
      temp_flash_remain_time = false;
      if(temp_record_flag != temp_flash_remain_time)
      {
        Clear_Remain_Time();
      }
      DWIN_Draw_IntValue(true, true, 1, font8x16, Color_White, Color_Bg_Black, 2, NUM_RAMAIN_TIME_X, NUM_RAMAIN_TIME_Y, _remain_time / 3600);
      DWIN_Draw_IntValue(true, true, 1, font8x16, Color_White, Color_Bg_Black, 2, NUM_RAMAIN_TIME_X+24, NUM_RAMAIN_TIME_Y, (_remain_time % 3600) / 60);
      DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black,  NUM_RAMAIN_TIME_X+22, NUM_RAMAIN_TIME_Y-3, F(":"));
    }
    else
    {
      temp_flash_remain_time = true;
      if(temp_record_flag != temp_flash_remain_time)
      {
        Clear_Remain_Time();
      }
      DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, NUM_RAMAIN_TIME_X, NUM_RAMAIN_TIME_Y, F(">100H"));
    }
  #endif
  temp_record_flag = temp_flash_remain_time;
}

void Popup_window_Filament(void)
{
  Clear_Main_Window();
  Draw_Popup_Bkgd_60();
  HMI_flag.Refresh_bottom_flag=true;//标志不刷新底部参数
  #if ENABLED(DWIN_CREALITY_480_LCD)   
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    // DWIN_Draw_Rectangle(1, Color_Bg_Blue, 0, 0, 240, 24);
    // DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, 25, 240, 240);
    // DWIN_Draw_Rectangle(1, Color_Bg_Window, 14, 36, 239-13, 204);
    if(HMI_flag.language < Language_Max)
    {
      if(HMI_flag.filament_recover_flag)
      {
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_FilamentLoad, 14, 99); //rock_20220302
        // DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Powerloss_go, 26, 194);
      }
      else
      {
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_FilamentUseup, 14, 99);
        // DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Confirm, 26, 194);   // LANGUAGE_Powerloss_go
      }
      DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Confirm, 26, 194);
      DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_filament_cancel, 132, 194);
    }
    else
    {
      
    }
    Draw_Select_Highlight(true);
    // DWIN_Draw_Rectangle(0, Color_Bg_Window, 133, 169, 226, 202);
    // DWIN_Draw_Rectangle(0, Color_Bg_Window, 132, 168, 227, 203);
    // DWIN_Draw_Rectangle(0, Select_Color, 25, 169, 126, 202);
    // DWIN_Draw_Rectangle(0, Select_Color, 24, 168, 127, 203);
  #endif
}

void Popup_window_Remove_card(void)
{
  #if ENABLED(DWIN_CREALITY_480_LCD)

  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_Draw_Rectangle(1, Color_Bg_Blue, 0, 0, 240, 24);
    DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, 25, 240, 240);
    DWIN_Draw_Rectangle(1, Color_Bg_Window, 16, 30, 234, 240);
    // if(HMI_flag.language)
    if(HMI_flag.language < Language_Max)
    {
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Card_Remove_JPN, 16, 44);       // 显示拔卡异常界面
      DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Confirm, 79, 194); // 显示确认按钮
    }
    else
    {
     
    }
    DWIN_Draw_Rectangle(0, Select_Color, 79, 194, 161, 226);
    DWIN_Draw_Rectangle(0, Select_Color, 78, 193, 162, 227);
  #endif
}
#if ENABLED(USER_LEVEL_CHECK)  //使用调平校准功能
//缩减的G29
static void G29_small(void) //
{  
  xy_pos_t probe_position_lf,
           probe_position_rb;
   const float x_min = probe.min_x(), x_max = probe.max_x(),
               y_min = probe.min_y(), y_max = probe.max_y();

        probe_position_lf.set(parser.linearval('L', x_min), parser.linearval('F', y_min));
        probe_position_rb.set(parser.linearval('R', x_max), parser.linearval('B', y_max));

      // PRINT_LOG("probe_position_lf.x:",probe_position_lf.x,"probe_position_lf.y:",probe_position_lf.y);
      // PRINT_LOG("probe_position_rb.x:",probe_position_rb.x,"probe_position_rb.y:",probe_position_rb.y);

      // PRINT_LOG("probe_position_lf.x1:",(probe_position_rb.x-probe_position_lf.x)/3*1+probe_position_lf.x,"probe_position_lf.y1:",(probe_position_rb.y-probe_position_lf.y)/3*1+probe_position_lf.y);
      // PRINT_LOG("probe_position_lf.x2:",(probe_position_rb.x-probe_position_lf.x)/3*2+probe_position_lf.x,"probe_position_lf.y2:",(probe_position_rb.y-probe_position_lf.y)/3*2+probe_position_lf.y);
      

     

  float temp_z_arr[4]={0},temp_crtouch_z=0;
  uint8_t loop_index=0;
  xyz_float_t position_arr[4]={{probe_position_rb.x,probe_position_lf.y,5},{probe_position_lf.x,probe_position_lf.y,5},{probe_position_lf.x,probe_position_rb.y,5},{probe_position_rb.x,probe_position_rb.y,5}};
  // xyz_float_t position_arr[4]={{190,0,0},{30,30,0},{30,190,0},{190,190,0}};
   uint8_t loop_j=0;
   const xy_int8_t grid_temp[4]={{GRID_MAX_POINTS_X-1,0},{0,0},{0,GRID_MAX_POINTS_Y-1},{GRID_MAX_POINTS_X-1,GRID_MAX_POINTS_Y-1}};
  // //z_values[grid_temp[0].x][grid_temp[0].y]=0;
  RUN_AND_WAIT_GCODE_CMD(G28_STR, 1);
  SET_BED_LEVE_ENABLE(false); 
  SET_BED_TEMP(65);  //热床加热
  WAIT_BED_TEMP(60 * 5 * 1000, 2);
  DO_BLOCKING_MOVE_TO_Z(5, 5);  //将z轴抬起到5mm
  for(loop_j=0;loop_j<CHECK_POINT_NUM;loop_j++)   
  {
    probeByTouch(position_arr[loop_j], &temp_z_arr[loop_j]);
    //temp_z_arr[loop_j]=PROBE_PPINT_BY_TOUCH(position_arr[loop_j].x, position_arr[loop_j].y);
    PRINT_LOG("temp_z_arr[]:",temp_z_arr[loop_j]);
    PRINT_LOG("z_values[]:",z_values[grid_temp[loop_j].x][grid_temp[loop_j].y]);
    temp_crtouch_z=fabs(temp_z_arr[loop_j]-z_values[grid_temp[loop_j].x][grid_temp[loop_j].y]);//求浮点数的绝对值
    PRINT_LOG("temp_crtouch_z:",temp_crtouch_z);
    //DO_BLOCKING_MOVE_TO_Z(10, 5);  //将z轴抬起到5mm
    if(temp_crtouch_z>=CHECK_ERROR_MIN_VALUE)
    {
        loop_index++;
        if(loop_index>=CHECK_ERROR_NUM)
        {
          HMI_flag.Need_g29_flag=true; //需要重新调平 
          break;  //不需要其他的操作
        }
    }
    else if(temp_crtouch_z>=CHECK_ERROR_MAX_VALUE)
    {
      HMI_flag.Need_g29_flag=true; //需要重新调平    
      break;  //不需要其他的操作
    }
    PRINT_LOG("loop_index:",loop_index);
  }       

 SET_BED_LEVE_ENABLE(true);
  //算出需要校验的四个点的坐标
   //分别校准四个点的高度，保存到临时数组里面。
   //将临时数组的四个点坐标和上次测试的四个点的坐标分别对比，如果超过2个点的坐标超过了0.05，就重新G29。

   
}
#endif

void Goto_PrintProcess()
{
  checkkey = PrintProcess;
  Clear_Main_Window();
  HMI_flag.Refresh_bottom_flag=true;//标志不刷新底部参数
  // Draw_Mid_Status_Area(true); //rock_20230529
  Draw_Mid_Status_Area(true); //中部更新一次全部参数
  // DWIN_Draw_Rectangle(1, Line_Color, LINE_X_START, LINE_Y_START, LINE_X_END, LINE_Y_END); //划线1
  // DWIN_Draw_Rectangle(1, Line_Color, LINE_X_START, LINE_Y_START+LINE2_SPACE, LINE_X_END, LINE_Y_END+LINE2_SPACE); //划线2
  // DWIN_Draw_Rectangle(1, Line_Color, LINE_X_START, LINE_Y_START+LINE3_SPACE, LINE_X_END, LINE_Y_END+LINE3_SPACE); //划线3
  // 词条：打印中；打印时间；剩余时间
  Draw_Printing_Screen();
  // 设置界面
  ICON_Tune();
  if (printingIsPaused()&& !HMI_flag.cloud_printing_flag) ICON_Continue();
  // 暂停
  if (printingIsPaused())
  {
    Show_JPN_pause_title();  //显示标题
    ICON_Continue();
  }
  else
  {
    // 打印中
    Show_JPN_print_title();
    ICON_Pause();
  }
  // 停止按钮
  ICON_Stop();
  // Copy into filebuf string before entry
  char shift_name[LONG_FILENAME_LENGTH + 1];
  char *  name = card.longest_filename();

  #if ENABLED(DWIN_CREALITY_480_LCD)

  #elif ENABLED(DWIN_CREALITY_320_LCD)
  if(strlen(name)>30)
  {
    make_name_without_ext(shift_name,name,30); //将超过30字符的长文件名粘贴到新的字符串
    print_name=name;
    print_len_name=strlen(name);
    left_move_index=0; 
    int8_t npos = _MAX(0U, DWIN_WIDTH - strlen(shift_name) * MENU_CHR_W) / 2;    
    DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, npos, FIEL_NAME_Y, shift_name);
  }
  else  
  {
    int8_t npos = _MAX(0U, DWIN_WIDTH - strlen(name) * MENU_CHR_W) / 2;
    DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, npos, FIEL_NAME_Y, name);
  }
    DWIN_ICON_Show(ICON, ICON_PrintTime, ICON_PRINT_TIME_X, ICON_PRINT_TIME_Y);     // 打印时间图标
    DWIN_ICON_Show(ICON, ICON_RemainTime,ICON_RAMAIN_TIME_X, ICON_RAMAIN_TIME_Y);   // 剩余时间图标
  #endif 
  _card_percent = card.percentDone();
  Draw_Print_ProgressBar();      //显示当前打印进度
  Draw_Print_ProgressElapsed();  //显示当前时间
  Draw_Print_ProgressRemain();   //显示剩余时间
}

void Goto_MainMenu()
{  
  checkkey = MainMenu;
  Clear_Main_Window();
  HMI_flag.Refresh_bottom_flag=true;//标志不刷新底部参数
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_ICON_Show(ICON,ICON_LOGO,71,52);
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    // DWIN_ICON_Show(ICON, ICON_LOGO, LOGO_LITTLE_X, LOGO_LITTLE_Y);
  if (HMI_flag.language < Language_Max)
  {
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Main, TITLE_X, TITLE_Y);  // rock_j
  }
  #endif
  ICON_Print();
  ICON_Prepare();
  ICON_Control();
  TERN(HAS_ONESTEP_LEVELING, ICON_Leveling, ICON_StartInfo)(select_page.now == 3);  
}

inline ENCODER_DiffState get_encoder_state() 
{
  static millis_t Encoder_ms = 0;
  const millis_t ms = millis();
  if (PENDING(ms, Encoder_ms)) return ENCODER_DIFF_NO;
  const ENCODER_DiffState state = Encoder_ReceiveAnalyze();
  if (state != ENCODER_DIFF_NO) Encoder_ms = ms + ENCODER_WAIT_MS;
  return state;
}

void HMI_Plan_Move(const feedRate_t fr_mm_s)
{
  if (!planner.is_full())
  {
    planner.synchronize();
    planner.buffer_line(current_position, fr_mm_s, active_extruder);
    //DWIN_UpdateLCD();
  }
}

void HMI_Move_Done(const AxisEnum axis) 
{
  // rock_20211120
  // EncoderRate.enabled = false; 
  //checkkey =  Max_GUI;
  planner.synchronize();
  checkkey = AxisMove;
  switch (axis)
  {
    case X_AXIS:
    DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, VALUERANGE_X,  MBASE(1), HMI_ValueStruct.Move_X_scaled);
    break;
    case Y_AXIS:
    DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, VALUERANGE_X, MBASE(2), HMI_ValueStruct.Move_Y_scaled);
    break;
    case Z_AXIS:
    DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, VALUERANGE_X, MBASE(3), HMI_ValueStruct.Move_Z_scaled);
    break;
    case E_AXIS:
    DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, VALUERANGE_X, MBASE(4), HMI_ValueStruct.Move_E_scaled);
    break;
    default:
    break;
  }
  //DWIN_UpdateLCD();
}

void HMI_Move_X()
{
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO) 
  {
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Move_X_scaled))
    {
      DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, VALUERANGE_X,  MBASE(1), HMI_ValueStruct.Move_X_scaled);
      return HMI_Move_Done(X_AXIS);
    }
    LIMIT(HMI_ValueStruct.Move_X_scaled, (XY_BED_MIN_ZERO) * MINUNITMULT, (X_BED_SIZE) * MINUNITMULT);
    current_position.x = HMI_ValueStruct.Move_X_scaled / MINUNITMULT;
    DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, UNITFDIGITS, VALUERANGE_X,  MBASE(1), HMI_ValueStruct.Move_X_scaled);
    //delay(10);  //解决快速旋转会两个数值一起选中的问题。
    //DWIN_UpdateLCD();
    HMI_Plan_Move(homing_feedrate(X_AXIS));
  }
  
}

void HMI_Move_Y()
{
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO) 
  {
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Move_Y_scaled))
    {
      DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, VALUERANGE_X, MBASE(2), HMI_ValueStruct.Move_Y_scaled);
      return HMI_Move_Done(Y_AXIS);
    }
    LIMIT(HMI_ValueStruct.Move_Y_scaled, (XY_BED_MIN_ZERO) * MINUNITMULT, (Y_BED_SIZE) * MINUNITMULT);
    current_position.y = HMI_ValueStruct.Move_Y_scaled / MINUNITMULT;
    DWIN_Draw_Signed_Float(font8x16,Select_Color, 3, UNITFDIGITS, VALUERANGE_X, MBASE(2), HMI_ValueStruct.Move_Y_scaled);  
    //delay(10);  //解决快速旋转会两个数值一起选中的问题。
    //DWIN_UpdateLCD();
    HMI_Plan_Move(homing_feedrate(Y_AXIS));
  }
}

void HMI_Move_Z()
{  
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO) 
  {
    DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, UNITFDIGITS, VALUERANGE_X, MBASE(3), HMI_ValueStruct.Move_Z_scaled);
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Move_Z_scaled))
    {
      DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, VALUERANGE_X, MBASE(3), HMI_ValueStruct.Move_Z_scaled);
      return HMI_Move_Done(Z_AXIS);
    }
    // rock_20211025 Modified axis movement interface cannot move to negative values to prevent collisions
    LIMIT(HMI_ValueStruct.Move_Z_scaled, (Z_MIN_POS) * MINUNITMULT, (Z_MAX_POS) * MINUNITMULT);
    current_position.z = HMI_ValueStruct.Move_Z_scaled / MINUNITMULT;

    DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, UNITFDIGITS, VALUERANGE_X, MBASE(3), HMI_ValueStruct.Move_Z_scaled);
    //delay(10);  //解决快速旋转会两个数值一起选中的问题。
    //DWIN_UpdateLCD();
    HMI_Plan_Move(homing_feedrate(Z_AXIS));
  }
}

#if HAS_HOTEND

  void HMI_Move_E()
  {
    static float last_E_scaled = 0;
    ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
    if (encoder_diffState != ENCODER_DIFF_NO) 
    {
      if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Move_E_scaled))
      {
        last_E_scaled = HMI_ValueStruct.Move_E_scaled;
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, VALUERANGE_X, MBASE(4), HMI_ValueStruct.Move_E_scaled);
        return HMI_Move_Done(E_AXIS);
      }
      LIMIT(HMI_ValueStruct.Move_E_scaled, last_E_scaled - (EXTRUDE_MAXLENGTH_e) * MINUNITMULT, last_E_scaled + (EXTRUDE_MAXLENGTH_e) * MINUNITMULT);    
      current_position.e = HMI_ValueStruct.Move_E_scaled / MINUNITMULT;
      DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, UNITFDIGITS, VALUERANGE_X, MBASE(4), HMI_ValueStruct.Move_E_scaled);
      delay(10);  //解决快速旋转会两个数值一起选中的问题。
    //DWIN_UpdateLCD();
      HMI_Plan_Move(MMM_TO_MMS(FEEDRATE_E));
    }
  }

#endif

#if HAS_ZOFFSET_ITEM

  bool printer_busy() { return planner.movesplanned() || printingIsActive(); }

  void HMI_Zoffset() 
  {
    ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
    if (encoder_diffState != ENCODER_DIFF_NO) 
    {
      uint8_t zoff_line;
      switch (HMI_ValueStruct.show_mode)
      {
        case -4: zoff_line = PREPARE_CASE_ZOFF + MROWS - index_prepare; break;
        default: zoff_line = TUNE_CASE_ZOFF + MROWS - index_tune;
      }
      if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.offset_value))
      {
        LIMIT(HMI_ValueStruct.offset_value, (Z_PROBE_OFFSET_RANGE_MIN) * 100, (Z_PROBE_OFFSET_RANGE_MAX) * 100);
        last_zoffset = dwin_zoffset;
        dwin_zoffset = HMI_ValueStruct.offset_value / 100.0f;  //rock_20210726 
        EncoderRate.enabled = false;
        #if HAS_BED_PROBE
          probe.offset.z = dwin_zoffset;
            // millis_t start = millis();
            TERN_(EEPROM_SETTINGS, settings.save());
            // millis_t end = millis();
            // SERIAL_ECHOLNPAIR("pl write time:", end - start);          
        #endif
        checkkey = HMI_ValueStruct.show_mode == -4 ? Prepare : Tune;
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 2, 2, VALUERANGE_X - 14, MBASE(zoff_line), TERN(HAS_BED_PROBE, BABY_Z_VAR * 100, HMI_ValueStruct.offset_value));
        DWIN_UpdateLCD();
        return;
      }
      LIMIT(HMI_ValueStruct.offset_value, (Z_PROBE_OFFSET_RANGE_MIN) * 100, (Z_PROBE_OFFSET_RANGE_MAX) * 100);
      last_zoffset = dwin_zoffset;
      dwin_zoffset = HMI_ValueStruct.offset_value / 100.0f;
      #if EITHER(BABYSTEP_ZPROBE_OFFSET, JUST_BABYSTEP)
        // if (BABYSTEP_ALLOWED()) babystep.add_mm(Z_AXIS, dwin_zoffset - last_zoffset);   // rock_20220214
        // serialprintPGM("d:babystep\n");
        babystep.add_mm(Z_AXIS, dwin_zoffset - last_zoffset);
      #endif
      DWIN_Draw_Signed_Float(font8x16, Select_Color, 2, 2, VALUERANGE_X - 14, MBASE(zoff_line), HMI_ValueStruct.offset_value);
      DWIN_UpdateLCD();
    }
  }

#endif // HAS_ZOFFSET_ITEM

#if HAS_HOTEND
  void HMI_ETemp() 
  {
    ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
    if (encoder_diffState != ENCODER_DIFF_NO)
    {
      uint8_t temp_line;
      switch (HMI_ValueStruct.show_mode)
      {
        case -1: temp_line = select_temp.now  + MROWS - index_temp;break;
        case -2: temp_line = PREHEAT_CASE_TEMP; break;
        case -3: temp_line = PREHEAT_CASE_TEMP; break;
        default: temp_line = TUNE_CASE_TEMP + MROWS - index_tune;
      }
      
      if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.E_Temp))
      {
        EncoderRate.enabled = false;
         // E_Temp limit
        LIMIT(HMI_ValueStruct.E_Temp, HEATER_0_MINTEMP, thermalManager.hotend_max_target(0));
        if (HMI_ValueStruct.show_mode == -2)
        {
          checkkey = PLAPreheat;
          ui.material_preset[0].hotend_temp = HMI_ValueStruct.E_Temp;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(temp_line)+TEMP_SET_OFFSET, ui.material_preset[0].hotend_temp);
          return;
        }
        else if (HMI_ValueStruct.show_mode == -3)
        {
          checkkey = ABSPreheat;
          ui.material_preset[1].hotend_temp = HMI_ValueStruct.E_Temp;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(temp_line)+TEMP_SET_OFFSET, ui.material_preset[1].hotend_temp);
          return;
        }
        else if (HMI_ValueStruct.show_mode == -1) // Temperature
        {
          checkkey = TemperatureID;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(temp_line)+TEMP_SET_OFFSET, HMI_ValueStruct.E_Temp);
        }
        else
        {
          checkkey = Tune;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(temp_line)+PRINT_SET_OFFSET, HMI_ValueStruct.E_Temp);
        }
        #if ENABLED(USE_SWITCH_POWER_200W)
          while ((thermalManager.degTargetBed() > 0) && (ABS(thermalManager.degTargetBed() - thermalManager.degBed()) > TEMP_WINDOW))
          {
            idle();
          }
        #endif
        thermalManager.setTargetHotend(HMI_ValueStruct.E_Temp, 0);
        return;
      }
       // E_Temp limit
      LIMIT(HMI_ValueStruct.E_Temp, HEATER_0_MINTEMP, thermalManager.hotend_max_target(0));
      // E_Temp value
      if(0==HMI_ValueStruct.show_mode)
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, VALUERANGE_X, MBASE(temp_line)+PRINT_SET_OFFSET, HMI_ValueStruct.E_Temp);
      else
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, VALUERANGE_X, MBASE(temp_line)+TEMP_SET_OFFSET, HMI_ValueStruct.E_Temp); 
    }
  }

#endif // HAS_HOTEND

#if HAS_HEATED_BED

  void HMI_BedTemp()
  {
    ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
    if (encoder_diffState != ENCODER_DIFF_NO)
    {
      uint8_t bed_line;
      switch (HMI_ValueStruct.show_mode)
      {
        case -1: bed_line = select_temp.now  + MROWS - index_temp;break;
        case -2: bed_line = PREHEAT_CASE_BED; break;
        case -3: bed_line = PREHEAT_CASE_BED; break;
        default: bed_line = TUNE_CASE_BED + MROWS - index_tune;
      }
       // Bed_Temp limit
      LIMIT(HMI_ValueStruct.Bed_Temp, BED_MINTEMP, BED_MAX_TARGET);
      if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Bed_Temp))
      {
        EncoderRate.enabled = false;
           // Bed_Temp limit
        LIMIT(HMI_ValueStruct.Bed_Temp, BED_MINTEMP, BED_MAX_TARGET);
        if (HMI_ValueStruct.show_mode == -2) {
          checkkey = PLAPreheat;
          ui.material_preset[0].bed_temp = HMI_ValueStruct.Bed_Temp;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(bed_line)+TEMP_SET_OFFSET, ui.material_preset[0].bed_temp);
          return;
        }
        else if (HMI_ValueStruct.show_mode == -3) {
          checkkey = ABSPreheat;
          ui.material_preset[1].bed_temp = HMI_ValueStruct.Bed_Temp;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(bed_line)+TEMP_SET_OFFSET, ui.material_preset[1].bed_temp);
          return;
        }
        else if (HMI_ValueStruct.show_mode == -1)
        {
          checkkey = TemperatureID;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(bed_line)+TEMP_SET_OFFSET, HMI_ValueStruct.Bed_Temp);
        }
        else
        {
          checkkey = Tune;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(bed_line)+PRINT_SET_OFFSET, HMI_ValueStruct.Bed_Temp);
        }
        #if ENABLED(USE_SWITCH_POWER_200W) 
        while (((thermalManager.degTargetHotend(0) > 0) && ABS(thermalManager.degTargetHotend(0) - thermalManager.degHotend(0)) > TEMP_WINDOW))
        {
          idle();
        }
        #endif
        thermalManager.setTargetBed(HMI_ValueStruct.Bed_Temp);
        return;
      }
        // Bed_Temp limit
      LIMIT(HMI_ValueStruct.Bed_Temp, BED_MINTEMP, BED_MAX_TARGET);
      // Bed_Temp value
      if(0 == HMI_ValueStruct.show_mode)
      {
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, VALUERANGE_X, MBASE(bed_line) + PRINT_SET_OFFSET, HMI_ValueStruct.Bed_Temp);
      }
      else
      {
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, VALUERANGE_X, MBASE(bed_line)+TEMP_SET_OFFSET, HMI_ValueStruct.Bed_Temp);
      }
    }
  }

#endif // HAS_HEATED_BED

#if HAS_PREHEAT && HAS_FAN

  void HMI_FanSpeed() {
    ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
    if (encoder_diffState != ENCODER_DIFF_NO) {
      uint8_t fan_line;
      switch (HMI_ValueStruct.show_mode) {
        case -1: fan_line = select_temp.now  + MROWS - index_temp;break;
        case -2: fan_line = PREHEAT_CASE_FAN; break;
        case -3: fan_line = PREHEAT_CASE_FAN; break;
        // case -4: fan_line = TEMP_CASE_FAN + MROWS - index_temp;break;
        default: fan_line = TUNE_CASE_FAN + MROWS - index_tune;
      }

      if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Fan_speed))
      {
        EncoderRate.enabled = false;
        if (HMI_ValueStruct.show_mode == -2) {
          checkkey = PLAPreheat;
          ui.material_preset[0].fan_speed = HMI_ValueStruct.Fan_speed;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(fan_line)+TEMP_SET_OFFSET, ui.material_preset[0].fan_speed);
          return;
        }
        else if (HMI_ValueStruct.show_mode == -3) {
          checkkey = ABSPreheat;
          ui.material_preset[1].fan_speed = HMI_ValueStruct.Fan_speed;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(fan_line)+TEMP_SET_OFFSET, ui.material_preset[1].fan_speed);
          return;
        }
        else if (HMI_ValueStruct.show_mode == -1)
        {
          checkkey = TemperatureID;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(fan_line)+TEMP_SET_OFFSET, HMI_ValueStruct.Fan_speed);
        }
        else
        {
          checkkey = Tune;
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(fan_line) + PRINT_SET_OFFSET, HMI_ValueStruct.Fan_speed);
        }
        thermalManager.set_fan_speed(0, HMI_ValueStruct.Fan_speed);
        return;
      }
      // Fan_speed limit
      LIMIT(HMI_ValueStruct.Fan_speed, 0, 255);
      // Fan_speed value
      if(0 == HMI_ValueStruct.show_mode)
      {
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, VALUERANGE_X, MBASE(fan_line) + PRINT_SET_OFFSET, HMI_ValueStruct.Fan_speed);
      }
      else
      {
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, VALUERANGE_X, MBASE(fan_line)+TEMP_SET_OFFSET, HMI_ValueStruct.Fan_speed);
      }
    }
  }

#endif // HAS_PREHEAT && HAS_FAN

void HMI_PrintSpeed() {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO) {
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.print_speed)) {
      checkkey = Tune;
      EncoderRate.enabled = false;
      feedrate_percentage = HMI_ValueStruct.print_speed;
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(select_tune.now + MROWS - index_tune)+PRINT_SET_OFFSET, HMI_ValueStruct.print_speed);
      return;
    }
    // print_speed limit
    LIMIT(HMI_ValueStruct.print_speed, MIN_PRINT_SPEED, MAX_PRINT_SPEED);
    // print_speed value
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, VALUERANGE_X, MBASE(select_tune.now + MROWS - index_tune)+PRINT_SET_OFFSET, HMI_ValueStruct.print_speed);
  }
}

#define LAST_AXIS TERN(HAS_HOTEND, E_AXIS, Z_AXIS)

void HMI_MaxFeedspeedXYZE()
{
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO)
  {
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Max_Feedspeed))
    {
      checkkey = MaxSpeed;
      EncoderRate.enabled = false;
      if (WITHIN(HMI_flag.feedspeed_axis, X_AXIS, LAST_AXIS))
        planner.set_max_feedrate(HMI_flag.feedspeed_axis, HMI_ValueStruct.Max_Feedspeed);
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, VALUERANGE_X, MBASE(select_speed.now) + 3, HMI_ValueStruct.Max_Feedspeed);
      return;
    }
    // MaxFeedspeed limit
    if (WITHIN(HMI_flag.feedspeed_axis, X_AXIS, LAST_AXIS))
      NOMORE(HMI_ValueStruct.Max_Feedspeed, default_max_feedrate[HMI_flag.feedspeed_axis] * 2);
    if (HMI_ValueStruct.Max_Feedspeed < MIN_MAXFEEDSPEED) HMI_ValueStruct.Max_Feedspeed = MIN_MAXFEEDSPEED;
    // MaxFeedspeed value
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 4, VALUERANGE_X, MBASE(select_speed.now) + 3, HMI_ValueStruct.Max_Feedspeed);
  }
}

void HMI_MaxAccelerationXYZE()
{
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO)
  {
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Max_Acceleration)) 
    {
      checkkey = MaxAcceleration;
      EncoderRate.enabled = false;
      if (WITHIN(HMI_flag.acc_axis, X_AXIS, LAST_AXIS))
        planner.set_max_acceleration(HMI_flag.acc_axis, HMI_ValueStruct.Max_Acceleration);
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, VALUERANGE_X, MBASE(select_acc.now)+3, HMI_ValueStruct.Max_Acceleration);
      return;
    }
    // MaxAcceleration limit
    if (WITHIN(HMI_flag.acc_axis, X_AXIS, LAST_AXIS))
      NOMORE(HMI_ValueStruct.Max_Acceleration, default_max_acceleration[HMI_flag.acc_axis] * 2);
    if (HMI_ValueStruct.Max_Acceleration < MIN_MAXACCELERATION) HMI_ValueStruct.Max_Acceleration = MIN_MAXACCELERATION;
    // MaxAcceleration value
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 4, VALUERANGE_X, MBASE(select_acc.now) + 3, HMI_ValueStruct.Max_Acceleration);
  }
}

#if HAS_CLASSIC_JERK

  void HMI_MaxJerkXYZE()
  {
    ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
    if (encoder_diffState != ENCODER_DIFF_NO)
    {
      if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Max_Jerk_scaled)) 
      {
        checkkey = MaxJerk;
        EncoderRate.enabled = false;
        if (WITHIN(HMI_flag.jerk_axis, X_AXIS, LAST_AXIS))
        {
          planner.set_max_jerk(HMI_flag.jerk_axis, HMI_ValueStruct.Max_Jerk_scaled / 10);
          planner.max_jerk[HMI_flag.jerk_axis] = HMI_ValueStruct.Max_Jerk_scaled / 10;
        }

        DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, VALUERANGE_X, MBASE(select_jerk.now)+3, HMI_ValueStruct.Max_Jerk_scaled);
        return;
      }
      // MaxJerk limit
      if (WITHIN(HMI_flag.jerk_axis, X_AXIS, LAST_AXIS))
        NOMORE(HMI_ValueStruct.Max_Jerk_scaled, default_max_jerk[HMI_flag.jerk_axis] * 2 * MINUNITMULT);
      NOLESS(HMI_ValueStruct.Max_Jerk_scaled, (MIN_MAXJERK) * MINUNITMULT);
      // MaxJerk value
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, UNITFDIGITS, VALUERANGE_X, MBASE(select_jerk.now)+3, HMI_ValueStruct.Max_Jerk_scaled);
    }
  }

#endif // HAS_CLASSIC_JERK

void HMI_StepXYZE() {
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO) {
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Max_Step_scaled)) {
      checkkey = Step;
      EncoderRate.enabled = false;
      if (WITHIN(HMI_flag.step_axis, X_AXIS, LAST_AXIS))
        planner.settings.axis_steps_per_mm[HMI_flag.step_axis] = HMI_ValueStruct.Max_Step_scaled / 10;
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, VALUERANGE_X, MBASE(select_step.now)+3, HMI_ValueStruct.Max_Step_scaled);
      return;
    }
    // Step limit
    if (WITHIN(HMI_flag.step_axis, X_AXIS, LAST_AXIS))
      NOMORE(HMI_ValueStruct.Max_Step_scaled, 999.9 * MINUNITMULT);
    NOLESS(HMI_ValueStruct.Max_Step_scaled, MIN_STEP);
    // Step value
    DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, UNITFDIGITS, VALUERANGE_X, MBASE(select_step.now)+3, HMI_ValueStruct.Max_Step_scaled);
  }
}
static void Set_HM_Value_PID(uint8_t row)
{
  switch (row)
  {
    case 1:
      PID_PARAM(Kp, 0) = HMI_ValueStruct.HM_PID_Temp_Value/100;
      break;
    case 2:
      PID_PARAM(Ki, 0) = scalePID_i(HMI_ValueStruct.HM_PID_Temp_Value/100);
      break;
    case 3:
      PID_PARAM(Kd, 0) = scalePID_d(HMI_ValueStruct.HM_PID_Temp_Value/100);
      break;
    case 4:
      thermalManager.temp_bed.pid.Kp = HMI_ValueStruct.HM_PID_Temp_Value/100;
      break;
    case 5:
      thermalManager.temp_bed.pid.Ki = scalePID_i(HMI_ValueStruct.HM_PID_Temp_Value/100);;
      break;
    case 6:
      thermalManager.temp_bed.pid.Kd = scalePID_d(HMI_ValueStruct.HM_PID_Temp_Value/100);
      break;
    default:
      break;
  }
}

void HMI_HM_PID_Value_Set()// 手动设置PID值
{
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO) 
  {
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.HM_PID_Temp_Value)) 
    {
      // 点击了确认键
      checkkey = HM_SET_PID;
      EncoderRate.enabled = false;
      if (WITHIN(HMI_flag.HM_PID_ROW, 1, 6))
      {
        ;
      }
      HMI_ValueStruct.HM_PID_Value[select_hm_set_pid.now]=HMI_ValueStruct.HM_PID_Temp_Value/100;
      Set_HM_Value_PID(select_hm_set_pid.now);
      #if ENABLED(DWIN_CREALITY_480_LCD)
        DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 2, 200, MBASE(HMI_flag.HM_PID_ROW), HMI_ValueStruct.HM_PID_Temp_Value);
      #elif ENABLED(DWIN_CREALITY_320_LCD)
        DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 2, 172, MBASE(HMI_flag.HM_PID_ROW), HMI_ValueStruct.HM_PID_Temp_Value);
      #endif
      return;
    }
    HMI_ValueStruct.HM_PID_Value[select_hm_set_pid.now] = HMI_ValueStruct.HM_PID_Temp_Value/100;
    #if ENABLED(DWIN_CREALITY_480_LCD)
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 2, 200, MBASE(HMI_flag.HM_PID_ROW), HMI_ValueStruct.HM_PID_Temp_Value);
    #elif ENABLED(DWIN_CREALITY_320_LCD)
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 2, 172, MBASE(HMI_flag.HM_PID_ROW), HMI_ValueStruct.HM_PID_Temp_Value);
    #endif
  }
}

// 自动设置PID值
void HMI_AUTO_PID_Value_Set()
{
  char cmd[30] = {0};
  ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState != ENCODER_DIFF_NO) 
  {
    if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Auto_PID_Temp))
    {
      // 点击了确认键
      // 数据下标清零
      HMI_ValueStruct.Curve_index = 0;
      switch (select_set_pid.now)
      {
        end_flag=false; //防止反复刷新曲线完成指令指令 
        EncoderRate.enabled = false;
        #if ENABLED(DWIN_CREALITY_480_LCD)
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 210 + PID_VALUE_OFFSET, MBASE(select_set_pid.now), HMI_ValueStruct.Auto_PID_Temp);
        #elif ENABLED(DWIN_CREALITY_320_LCD)
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 192 + PID_VALUE_OFFSET, MBASE(select_set_pid.now), HMI_ValueStruct.Auto_PID_Temp);
        #endif
        case 1:
          checkkey = AUTO_SET_BED_PID;
          Draw_auto_bed_PID();
          LIMIT(HMI_ValueStruct.Auto_PID_Temp, 60, BED_MAX_TARGET);
          HMI_ValueStruct.Auto_PID_Value[select_set_pid.now] = HMI_ValueStruct.Auto_PID_Temp;
          sprintf_P(cmd, PSTR("M303 C8 E-1 S%d"), HMI_ValueStruct.Auto_PID_Temp);
          gcode.process_subcommands_now(cmd);
          break;
        case 2:
          checkkey = AUTO_SET_NOZZLE_PID;
          Draw_auto_nozzle_PID();
          LIMIT(HMI_ValueStruct.Auto_PID_Temp, 100, thermalManager.hotend_max_target(0));
          HMI_ValueStruct.Auto_PID_Value[select_set_pid.now] = HMI_ValueStruct.Auto_PID_Temp;
          // HMI_flag.PID_autotune_start=true;
          sprintf_P(cmd, PSTR("M303 C8 S%d"), HMI_ValueStruct.Auto_PID_Temp);
          gcode.process_subcommands_now(cmd);
          break;
        default:
          break;
      }
      return;
    }
    if(select_set_pid.now == 1)
    {
      LIMIT(HMI_ValueStruct.Auto_PID_Temp, 60, BED_MAX_TARGET);
    }
    else if(select_set_pid.now == 2)
    {
      LIMIT(HMI_ValueStruct.Auto_PID_Temp, 100, thermalManager.hotend_max_target(0));
    }
    #if ENABLED(DWIN_CREALITY_480_LCD)
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 210 + PID_VALUE_OFFSET, MBASE(select_set_pid.now), HMI_ValueStruct.Auto_PID_Temp);
    #elif ENABLED(DWIN_CREALITY_320_LCD)
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 192 + PID_VALUE_OFFSET, MBASE(select_set_pid.now), HMI_ValueStruct.Auto_PID_Temp);
    #endif
  }
}

// Draw X, Y, Z and blink if in an un-homed or un-trusted state
void _update_axis_value(const AxisEnum axis, const uint16_t x, const uint16_t y, const bool blink, const bool force)
{
  const bool draw_qmark = axis_should_home(axis),
             draw_empty = NONE(HOME_AFTER_DEACTIVATE, DISABLE_REDUCED_ACCURACY_WARNING) && !draw_qmark && !axis_is_trusted(axis);

  // Check for a position change
  static xyz_pos_t oldpos = { -1, -1, -1 };
  const float p = current_position[axis];
  const bool changed = oldpos[axis] != p;
  if (changed) oldpos[axis] = p;

  if (force || changed || draw_qmark || draw_empty)
  {
    if (blink && draw_qmark)
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, x, y, F(" ???.?"));
    else if (blink && draw_empty)
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, x, y, F("     "));
    else
    // DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, x, y, p * 10);
    // DWIN_Draw_Signed_Float(uint8_t size, uint16_t bColor, uint8_t iNum, uint8_t fNum, uint16_t x, uint16_t y, long value)
    DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, 1, x, y, p * 10);
  }
}

void _draw_xyz_position(const bool force)
{
  // SERIAL_ECHOPGM("Draw XYZ:");
  static bool _blink = false;
  const bool blink = !!(millis() & 0x400UL);
  if (force || blink != _blink)
  {
    _blink = blink;
    //SERIAL_ECHOPGM(" (blink)");
    #if ENABLED(DWIN_CREALITY_480_LCD)
      _update_axis_value(X_AXIS,  35, 459, blink, true);
      _update_axis_value(Y_AXIS, 120, 459, blink, true);
      _update_axis_value(Z_AXIS, 205, 459, blink, true);
    #elif ENABLED(DWIN_CREALITY_320_LCD)
      _update_axis_value(X_AXIS, 28, 296, blink, true);
      _update_axis_value(Y_AXIS, 119, 296, blink, true);
      _update_axis_value(Z_AXIS, 188, 296, blink, true);
    #endif
  }
  // SERIAL_EOL();
}

void update_variable()
{
  #if HAS_HOTEND
    static celsius_t _hotendtemp = 0, _hotendtarget = 0;
    const celsius_t hc = thermalManager.wholeDegHotend(0),
                    ht = thermalManager.degTargetHotend(0);
    const bool _new_hotend_temp = _hotendtemp != hc,
               _new_hotend_target = _hotendtarget != ht;
    if (_new_hotend_temp) _hotendtemp = hc;
    if (_new_hotend_target) _hotendtarget = ht;
  #endif
  #if HAS_HEATED_BED
    static celsius_t _bedtemp = 0, _bedtarget = 0;
    const celsius_t bc = thermalManager.wholeDegBed(),
                    bt = thermalManager.degTargetBed();
    const bool _new_bed_temp = _bedtemp != bc,
               _new_bed_target = _bedtarget != bt;
    if (_new_bed_temp) _bedtemp = bc;
    if (_new_bed_target) _bedtarget = bt;
  #endif
  #if HAS_FAN
    static uint8_t _fanspeed = 0;
    const bool _new_fanspeed = _fanspeed != thermalManager.fan_speed[0];
    if (_new_fanspeed) _fanspeed = thermalManager.fan_speed[0];
  #endif

    if(!ht) DWIN_ICON_Show(ICON, ICON_HotendTemp, 6, 245);
    if(!bt) DWIN_ICON_Show(ICON, ICON_BedTemp, 6, 268);
  #if ENABLED(DWIN_CREALITY_480_LCD)
  
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    if (checkkey == Tune)
    {
      // Tune page temperature update
      #if HAS_HOTEND
        if (_new_hotend_target)
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(TUNE_CASE_TEMP + MROWS - index_tune)+PRINT_SET_OFFSET, _hotendtarget);
      #endif
      #if HAS_HEATED_BED
        if (_new_bed_target)
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(TUNE_CASE_BED + MROWS - index_tune)+PRINT_SET_OFFSET, _bedtarget);
      #endif
      #if HAS_FAN
        if (_new_fanspeed)
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(TUNE_CASE_FAN + MROWS - index_tune)+PRINT_SET_OFFSET, _fanspeed);
      #endif
    }
    else if (checkkey == TemperatureID)
    {
      // Temperature page temperature update
      #if HAS_HOTEND
        if (_new_hotend_target)
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(TEMP_CASE_TEMP), _hotendtarget);
      #endif
      #if HAS_HEATED_BED
        if (_new_bed_target)
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(TEMP_CASE_BED), _bedtarget);
      #endif
      #if HAS_FAN
        if (_new_fanspeed)
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(TEMP_CASE_FAN), _fanspeed);
      #endif
    }

    // Bottom temperature update

    #if HAS_HOTEND
      // if (_new_hotend_temp)
      // {
      //   DWIN_Draw_Signed_Float(DWIN_FONT_STAT, Color_Bg_Black, 3, 0, 25, 245, _hotendtemp);  // rock_20210820
      // }
      // update for power continue first display celsius temp
      DWIN_Draw_Signed_Float(DWIN_FONT_STAT, Color_Bg_Black, 3, 0, 25, 245, _hotendtemp);

      if (_new_hotend_target)
      {
        DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 18 + 4 * STAT_CHR_W + 9, 247, _hotendtarget);
      }

      static int16_t _flow = planner.flow_percentage[0];
      if (_flow != planner.flow_percentage[0])
      {
        _flow = planner.flow_percentage[0];
        DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 84 + 2 * STAT_CHR_W, 274, _flow);
      }
    #endif

    #if HAS_HEATED_BED
      // if (_new_bed_temp)
      // {
      //   DWIN_Draw_Signed_Float(DWIN_FONT_STAT, Color_Bg_Black, 3, 0, 25, 272, _bedtemp);  // rock_20210820
      // }
      // update for power continue first display celsius temp
      DWIN_Draw_Signed_Float(DWIN_FONT_STAT, Color_Bg_Black, 3, 0, 25, 272, _bedtemp);

      // DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 28, 417, _bedtemp);
      if (_new_bed_target)
      {
        DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 18 + 4 * STAT_CHR_W + 9, 274, _bedtarget);
      }
    #endif

    static int16_t _feedrate = 100;
    if (_feedrate != feedrate_percentage)
    {
      _feedrate = feedrate_percentage;
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 99 + 2 * STAT_CHR_W, 247, _feedrate);
    }

    #if HAS_FAN
      if (_new_fanspeed) {
        _fanspeed = thermalManager.fan_speed[0];
        ;//DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 176 + 2 * STAT_CHR_W, 247, _fanspeed);
      }
    #endif

    static float _offset = 0;
    if (BABY_Z_VAR != _offset)
    {
      _offset = BABY_Z_VAR;
      if (BABY_Z_VAR < 0)
      {
        DWIN_Draw_FloatValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 2, 2, 191, 271, -_offset * 100);
        DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, 191, 269, F("-"));
      }
      else
      {
        DWIN_Draw_FloatValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 2, 2, 191, 271, _offset * 100);
        DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, 191, 269, F(" "));
      }
    }
  #endif
  _draw_xyz_position(false);
}


void update_middle_variable()
{
    #if HAS_HOTEND
    static celsius_t _hotendtemp = 0, _hotendtarget = 0;
    const celsius_t hc = thermalManager.wholeDegHotend(0),
                    ht = thermalManager.degTargetHotend(0);
    const bool _new_hotend_temp = _hotendtemp != hc,
               _new_hotend_target = _hotendtarget != ht;
    if (_new_hotend_temp) _hotendtemp = hc;
    if (_new_hotend_target) _hotendtarget = ht;
  #endif
  #if HAS_HEATED_BED
    static celsius_t _bedtemp = 0, _bedtarget = 0;
    const celsius_t bc = thermalManager.wholeDegBed(),
                    bt = thermalManager.degTargetBed();
    const bool _new_bed_temp = _bedtemp != bc,
               _new_bed_target = _bedtarget != bt;
    if (_new_bed_temp) _bedtemp = bc;
    if (_new_bed_target) _bedtarget = bt;
  #endif
  #if HAS_FAN
    static uint8_t _fanspeed = 0;
    const bool _new_fanspeed = _fanspeed != thermalManager.fan_speed[0];
    if (_new_fanspeed) _fanspeed = thermalManager.fan_speed[0];
  #endif

    if(!ht) DWIN_ICON_Show(ICON, ICON_HotendTemp, ICON_NOZZ_X, ICON_NOZZ_Y);
    if(!bt) DWIN_ICON_Show(ICON, ICON_BedTemp, ICON_BED_X, ICON_BED_Y);
  #if ENABLED(DWIN_CREALITY_480_LCD)
  
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    if (checkkey == Tune)
    {
      // Tune page temperature update
      #if HAS_HOTEND
        if (_new_hotend_target)
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(TUNE_CASE_TEMP + MROWS - index_tune)+PRINT_SET_OFFSET, _hotendtarget);
      #endif
      #if HAS_HEATED_BED
        if (_new_bed_target)
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(TUNE_CASE_BED + MROWS - index_tune)+PRINT_SET_OFFSET, _bedtarget);
      #endif
      #if HAS_FAN
        if (_new_fanspeed)
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(TUNE_CASE_FAN + MROWS - index_tune)+PRINT_SET_OFFSET, _fanspeed);
      #endif
    }
    else if (checkkey == TemperatureID)
    {
      #if HAS_HOTEND
        if (_new_hotend_target)
          ;//DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(TEMP_CASE_TEMP)+TEMP_SET_OFFSET, _hotendtarget);
      #endif
      #if HAS_HEATED_BED
        if (_new_bed_target)
          ;//DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(TEMP_CASE_BED)+TEMP_SET_OFFSET, _bedtarget);
      #endif
      #if HAS_FAN
        if (_new_fanspeed)
          ;//DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(TEMP_CASE_FAN)+TEMP_SET_OFFSET, _fanspeed);
      #endif
    }

    // Bottom temperature update

    #if HAS_HOTEND
      DWIN_Draw_Signed_Float_Temp(DWIN_FONT_STAT, Color_Bg_Black, 3, 0, 25, NUM_NOZZ_Y-2, _hotendtemp);
      if (_new_hotend_target)
      {
        DWIN_Draw_IntValue_N0SPACE(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 18 + 4 * STAT_CHR_W + 6, NUM_NOZZ_Y, _hotendtarget);
      }
      static int16_t _flow = planner.flow_percentage[0];
      if (_flow != planner.flow_percentage[0])
      {
        _flow = planner.flow_percentage[0];
        DWIN_Draw_IntValue_N0SPACE(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 84 + 2 * STAT_CHR_W+2,NUM_STEP_E_Y, _flow);
      }
    #endif

    #if HAS_HEATED_BED
      DWIN_Draw_Signed_Float_Temp(DWIN_FONT_STAT, Color_Bg_Black, 3, 0, 25, NUM_BED_Y-2, _bedtemp);
      if (_new_bed_target)
      {
        DWIN_Draw_IntValue_N0SPACE(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 18 + 4 * STAT_CHR_W + 6, NUM_BED_Y, _bedtarget);
      }
    #endif

    static int16_t _feedrate = 100;
    if (_feedrate != feedrate_percentage)
    {
      _feedrate = feedrate_percentage;
      DWIN_Draw_IntValue_N0SPACE(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 99 + 2 * STAT_CHR_W+2, NUM_SPEED_Y, _feedrate);
    }

    #if HAS_FAN
      if (_new_fanspeed) {
        _fanspeed = thermalManager.fan_speed[0];
        DWIN_Draw_IntValue_N0SPACE(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 175 + 2 * STAT_CHR_W, NUM_FAN_Y, _fanspeed);
      }
    #endif

    static float _offset = 0;
    if (BABY_Z_VAR != _offset)
    {
      _offset = BABY_Z_VAR;
      if (BABY_Z_VAR < 0)
      {
        DWIN_Draw_FloatValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 2, 2, 191, NUM_ZOFFSET_Y, -_offset * 100);
        DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, 191, NUM_ZOFFSET_Y-1, F("-"));
      }
      else
      {
        DWIN_Draw_FloatValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 2, 2, 191,  NUM_ZOFFSET_Y, _offset * 100);
        DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, 191,  NUM_ZOFFSET_Y-1, F(" "));
      }
    }

  #endif
}
/**
 * Read and cache the working directory.
 *
 * TODO: New code can follow the pattern of menu_media.cpp
 * and rely on Marlin caching for performance. No need to
 * cache files here.
 */

#ifndef strcasecmp_P
  #define strcasecmp_P(a, b) strcasecmp((a), (b))
#endif

void make_name_without_ext(char *dst, char *src, size_t maxlen=MENU_CHAR_LIMIT) {
  char * const name = card.longest_filename();
  size_t pos        = strlen(name); // index of ending nul

  // For files, remove the extension
  // which may be .gcode, .gco, or .g
  if (!card.flag.filenameIsDir)
    while (pos && src[pos] != '.') pos--; // find last '.' (stop at 0)

  size_t len = pos;   // nul or '.'
  if (len > maxlen) { // Keep the name short
    pos        = len = maxlen; // move nul down
    dst[--pos] = '.'; // insert dots
    dst[--pos] = '.';
    dst[--pos] = '.';
  }

  dst[len] = '\0';    // end it

  // Copy down to 0
  while (pos--) dst[pos] = src[pos];
}

void HMI_SDCardInit() { card.cdroot(); }

void MarlinUI::refresh() { /* Nothing to see here */ }

#define ICON_Folder ICON_More

#if ENABLED(SCROLL_LONG_FILENAMES)


  
  // Init the shift name based on the highlighted item
  void Init_Shift_Name()
  {
    const bool is_subdir = !card.flag.workDirIsRoot;
    const int8_t filenum = select_file.now - 1 - is_subdir; // Skip "Back" and ".."
    const uint16_t fileCnt = card.get_num_Files();
    if (WITHIN(filenum, 0, fileCnt - 1))
    {
      card.getfilename_sorted(SD_ORDER(filenum, fileCnt));
      char * const name = card.longest_filename();
      make_name_without_ext(shift_name, name, 100);
    }
  }

  void Init_SDItem_Shift() {
    shift_amt = 0;
    shift_ms  = select_file.now > 0 && strlen(shift_name) > MENU_CHAR_LIMIT
           ? millis() + 750UL : 0;
  }

#endif

/**
 * Display an SD item, adding a CDUP for subfolders.
 */
void Draw_SDItem(const uint16_t item, int16_t row=-1) 
{
  if (row < 0) row = item + 1 + MROWS - index_file;
  const bool is_subdir = !card.flag.workDirIsRoot;
  if (is_subdir && item == 0)
  {
    Draw_Menu_Line(row, ICON_Folder, "..");
    return;
  }

  card.getfilename_sorted(SD_ORDER(item - is_subdir, card.get_num_Files()));
  char * const name = card.longest_filename();

  #if ENABLED(SCROLL_LONG_FILENAMES)
    // Init the current selected name
    // This is used during scroll drawing
    if (item == select_file.now - 1) {
      make_name_without_ext(shift_name, name, 100);
      Init_SDItem_Shift();
    }
  #endif

  // Draw the file/folder with name aligned left
  char str[strlen(name) + 1];
  make_name_without_ext(str, name);
  Draw_Menu_Line(row, card.flag.filenameIsDir ? ICON_Folder : ICON_File, str);
}

#if ENABLED(SCROLL_LONG_FILENAMES)

  void Draw_SDItem_Shifted(uint8_t &shift) {
    // Limit to the number of chars past the cutoff
    const size_t len = strlen(shift_name);
    NOMORE(shift, _MAX(len - MENU_CHAR_LIMIT, 0U));

    // Shorten to the available space
    const size_t lastchar = _MIN((signed)len, shift + MENU_CHAR_LIMIT);

    const char c = shift_name[lastchar];
    shift_name[lastchar] = '\0';

    const uint8_t row = select_file.now + MROWS - index_file; // skip "Back" and scroll
     Erase_Menu_Text(row);
    Draw_Menu_Line(row, 0, &shift_name[shift]);

    shift_name[lastchar] = c;
  }

#endif

// Redraw the first set of SD Files
void Redraw_SD_List()
{
  select_file.reset();
  index_file = MROWS;

  // Leave title bar unchanged
  Clear_Menu_Area();
  Draw_Mid_Status_Area(true); //rock_20230529 //更新一次全部参数
  Draw_Back_First();

  if (card.isMounted())
  {
    // As many files as will fit
    LOOP_L_N(i, _MIN(nr_sd_menu_items(), MROWS))
      Draw_SDItem(i, i + 1);

    TERN_(SCROLL_LONG_FILENAMES, Init_SDItem_Shift());
  }
  // else
  // {
  //   DWIN_Draw_Rectangle(1, Color_Bg_Red, 10, MBASE(3) - 10, DWIN_WIDTH - 10, MBASE(4));
  //   DWIN_Draw_String(false, false, font16x32, Color_Yellow, Color_Bg_Red, ((DWIN_WIDTH) - 8 * 16) / 2, MBASE(3), F("No Media"));
  // }
}

bool DWIN_lcd_sd_status = false;
bool pause_action_flag = false;

void SDCard_Up() {
  card.cdup();
  Redraw_SD_List();
  DWIN_lcd_sd_status = false; // On next DWIN_Update
}

void SDCard_Folder(char * const dirname)
{
  card.cd(dirname);
  Redraw_SD_List();
  DWIN_lcd_sd_status = false; // On next DWIN_Update
}

//
// Watch for media mount / unmount
//
void HMI_SDCardUpdate()
{
  // The card pulling action is not detected when the interface returns to home, add ||HMI_flag.disallow_recovery_flag
  static uint8_t stat=false;
  if (HMI_flag.home_flag || HMI_flag.disallow_recovery_flag) 
  {
    return;
  }
  if (DWIN_lcd_sd_status != card.isMounted()) //flag.mounted
  {
    stat=false;
    DWIN_lcd_sd_status = card.isMounted();
    if (DWIN_lcd_sd_status)
    {
      if (checkkey == SelectFile)
      {
        Redraw_SD_List();
      }
    }
    else
    {
      // clean file icon
      if (checkkey == SelectFile)
      {
        Redraw_SD_List();
      }
      else if (checkkey == PrintProcess || checkkey == Tune || printingIsActive())
      {
        // TODO: Move card removed abort handling
        //       to CardReader::manage_media.
        // card.abortFilePrintSoon();
        // wait_for_heatup = wait_for_user = false;
        // dwin_abort_flag = true; // Reset feedrate, return to Home
      }
    }
    DWIN_UpdateLCD();
  }
  else 
  {
    // static uint8_t stat = uint8_t(IS_SD_INSERTED());
    // if(stat)
    // {
    //   SERIAL_ECHOLNPAIR("HMI_SDCardUpdate: ", DWIN_lcd_sd_status);
    //   card.mount();
    //   SERIAL_ECHOLNPAIR("card.isMounted(): ", card.isMounted());
    // }
    
  // flag.mounted
  }

}

//
// The status area is always on-screen, except during
// full-screen modal dialogs. (TODO: Keep alive during dialogs)
//
void Draw_Status_Area(bool with_update)
{
  DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, STATUS_Y, DWIN_WIDTH, DWIN_HEIGHT - 1);
  #if ENABLED(DWIN_CREALITY_480_LCD)
    #if HAS_HOTEND
      DWIN_ICON_Show(ICON, ICON_HotendTemp, 10, 383);
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 32, 388, thermalManager.wholeDegHotend(0));
      DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 29 + 3 * STAT_CHR_W + 5, 388, F("/"));
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 29 + 4 * STAT_CHR_W + 6, 388, thermalManager.degTargetHotend(0));

      DWIN_ICON_Show(ICON, ICON_StepE, 112, 417);
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 116 + 2 * STAT_CHR_W, 421, planner.flow_percentage[0]);
      DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 116 + 5 * STAT_CHR_W + 2, 419, F("%"));
    #endif

    #if HAS_HEATED_BED
      DWIN_ICON_Show(ICON, ICON_BedTemp, 10, 416);
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 32, 421, thermalManager.wholeDegBed());
      DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 29 + 3 * STAT_CHR_W + 5, 419, F("/"));
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 29 + 4 * STAT_CHR_W + 6, 421, thermalManager.degTargetBed());
    #endif

    DWIN_ICON_Show(ICON, ICON_Speed, 113, 383);
    DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 116 + 2 * STAT_CHR_W, 388, feedrate_percentage);
    DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 116 + 5 * STAT_CHR_W + 2, 386, F("%"));

    #if HAS_FAN
      DWIN_ICON_Show(ICON, ICON_FanSpeed, 187, 383);
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 195 + 2 * STAT_CHR_W, 388, thermalManager.fan_speed[0]);
    #endif

    #if HAS_ZOFFSET_ITEM
      DWIN_ICON_Show(ICON, ICON_Zoffset, 187, 416);
    #endif

    if (BABY_Z_VAR < 0)
    {
      DWIN_Draw_FloatValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 2, 2, 191,271, -BABY_Z_VAR * 100);
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, 191, 269, F("-"));
    }
    else
    {
      DWIN_Draw_FloatValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 2, 2, 207,271, BABY_Z_VAR * 100);
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, 207, 269, F(" "));
    }
    DWIN_Draw_Rectangle(1, Line_Color, 0, 449, DWIN_WIDTH, 451);
    DWIN_ICON_Show(ICON, ICON_MaxSpeedX,  10, 456);
    DWIN_ICON_Show(ICON, ICON_MaxSpeedY,  95, 456);
    DWIN_ICON_Show(ICON, ICON_MaxSpeedZ, 180, 456);
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    #if HAS_HOTEND
      DWIN_ICON_Show(ICON, ICON_HotendTemp, 6, 245);
      // DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 47, 245, thermalManager.wholeDegHotend(0));
      DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 22 + 3 * STAT_CHR_W + 5, 245, F("/"));
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 18 + 4 * STAT_CHR_W + 6, 247, thermalManager.degTargetHotend(0));

      DWIN_ICON_Show(ICON, ICON_StepE, 99, 268);
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 99 + 2 * STAT_CHR_W, 273, planner.flow_percentage[0]);
      DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 99 + 5 * STAT_CHR_W + 2, 272, F("%"));
    #endif

    #if HAS_HEATED_BED
      DWIN_ICON_Show(ICON, ICON_BedTemp, 6, 268);
      //DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 47, 272, thermalManager.wholeDegBed());
      DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 22 + 3 * STAT_CHR_W + 5, 272, F("/"));
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 18 + 4 * STAT_CHR_W + 6, 274, thermalManager.degTargetBed());
    #endif

    DWIN_ICON_Show(ICON, ICON_Speed, 99, 245);
    DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 99 + 2 * STAT_CHR_W, 247, feedrate_percentage);
    DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 99 + 5 * STAT_CHR_W + 2, 247, F("%"));

    #if HAS_FAN
      DWIN_ICON_Show(ICON, ICON_FanSpeed, 171, 245);
      DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 175 + 2 * STAT_CHR_W, 247, thermalManager.fan_speed[0]);
    #endif

    #if HAS_ZOFFSET_ITEM
      DWIN_ICON_Show(ICON, ICON_Zoffset, 171, 268);
    #endif

    if (BABY_Z_VAR < 0)
    {
      DWIN_Draw_FloatValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 2, 2, 191, 271, -BABY_Z_VAR * 100);
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, 191, 269, F("-"));
    }
    else
    {
      DWIN_Draw_FloatValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 2, 2, 191, 271, BABY_Z_VAR * 100);
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, 191, 269, F(" "));
    }
    DWIN_Draw_Rectangle(1, Line_Color, 0, 291, DWIN_WIDTH, 292);
    DWIN_ICON_Show(ICON, ICON_MaxSpeedX,  6, 296);
    DWIN_ICON_Show(ICON, ICON_MaxSpeedY, 99, 296);
    DWIN_ICON_Show(ICON, ICON_MaxSpeedZ, 171, 296);
  #endif
  _draw_xyz_position(true);
  if (with_update)
  {
    // update_variable(); //标志位为0才刷新底部参数
    update_middle_variable();
    DWIN_UpdateLCD();
    delay(5);
  }
}
//中部更新所有参数
void Draw_Mid_Status_Area(bool with_update)
{
  DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, STATUS_Y, DWIN_WIDTH, DWIN_HEIGHT - 1);
  HMI_flag.Refresh_bottom_flag=false;//标志刷新底部参数
   #if ENABLED(DWIN_CREALITY_480_LCD)
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    #if HAS_HOTEND
      DWIN_ICON_Show(ICON, ICON_HotendTemp, ICON_NOZZ_X, ICON_NOZZ_Y);
      // DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, NUM_NOZZ_X, ICON_NOZZ_Y, thermalManager.wholeDegHotend(0));
      // DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Black,NUM_NOZZ_X + 4 * MENU_CHR_W, ICON_NOZZ_Y, F("/"));
      // DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3,  NUM_NOZZ_X + 5 * MENU_CHR_W, ICON_NOZZ_Y, thermalManager.degTargetHotend(0));
      // DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 47, ICON_NOZZ_Y, thermalManager.wholeDegBed());;
      DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 22 + 3 * STAT_CHR_W + 5, NUM_NOZZ_Y-1, F("/"));
      DWIN_Draw_IntValue_N0SPACE(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 18 + 4 * STAT_CHR_W + 6, NUM_NOZZ_Y, thermalManager.degTargetHotend(0));

      DWIN_ICON_Show(ICON, ICON_StepE,ICON_STEP_E_X, ICON_STEP_E_Y);
      DWIN_Draw_IntValue_N0SPACE(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 99 + 2 * STAT_CHR_W+2, NUM_STEP_E_Y, planner.flow_percentage[0]);
      DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 99 + 5 * STAT_CHR_W,NUM_STEP_E_Y-1, F("%"));
      // DWIN_Draw_IntValue(true, true, 0, DWIN_MIDDLE_FONT_STAT, Color_White, Color_Bg_Black, 3,NUM_STEP_E_X, ICON_STEP_E_Y, planner.flow_percentage[0]);
      // DWIN_Draw_String(false, false, DWIN_MIDDLE_FONT_STAT, Color_White, Color_Bg_Black, NUM_STEP_E_X + 4 * STAT_CHR_W, ICON_STEP_E_Y, F("%"));
    #endif

    #if HAS_HEATED_BED
      DWIN_ICON_Show(ICON, ICON_BedTemp, ICON_BED_X, ICON_BED_Y);
      // DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, NUM_BED_X + STAT_CHR_W, ICON_BED_Y, thermalManager.wholeDegBed());
      // DWIN_Draw_String(false, false, DWIN_MIDDLE_FONT_STAT, Color_White, Color_Bg_Black, NUM_BED_X +4 * MENU_CHR_W, ICON_BED_Y, F("/"));
      // DWIN_Draw_IntValue(true, true, 0, DWIN_MIDDLE_FONT_STAT, Color_White, Color_Bg_Black, 3, NUM_BED_X+ 5*MENU_CHR_W,ICON_BED_Y, thermalManager.degTargetBed());
      // DWIN_Draw_IntValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 47, ICON_BED_Y, thermalManager.wholeDegBed());
      DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 22 + 3 * STAT_CHR_W + 5, NUM_BED_Y-1, F("/"));
      DWIN_Draw_IntValue_N0SPACE(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 18 + 4 * STAT_CHR_W + 6, NUM_BED_Y, thermalManager.degTargetBed());
    #endif

    DWIN_ICON_Show(ICON, ICON_Speed, ICON_SPEED_X, ICON_SPEED_Y);
    // DWIN_Draw_IntValue(true, true, 0, DWIN_MIDDLE_FONT_STAT, Color_White, Color_Bg_Black, 3,NUM_SPEED_X ,ICON_SPEED_Y, feedrate_percentage);
    // DWIN_Draw_String(false, false, DWIN_MIDDLE_FONT_STAT, Color_White, Color_Bg_Black, NUM_SPEED_X + 4*MENU_CHR_W, ICON_SPEED_Y, F("%"));
    DWIN_Draw_IntValue_N0SPACE(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 99 + 2 * STAT_CHR_W+2, NUM_SPEED_Y, feedrate_percentage);
    DWIN_Draw_String(false, false, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 99 + 5 * STAT_CHR_W, NUM_SPEED_Y-1, F("%"));

    #if HAS_FAN
      DWIN_ICON_Show(ICON, ICON_FanSpeed, ICON_FAN_X, ICON_FAN_Y);
      DWIN_Draw_IntValue_N0SPACE(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 3, 175 + 2 * STAT_CHR_W, NUM_FAN_Y, thermalManager.fan_speed[0]);
      // DWIN_Draw_IntValue(true, true, 0, DWIN_MIDDLE_FONT_STAT, Color_White, Color_Bg_Black, 3, NUM_FAN_X , ICON_FAN_Y, thermalManager.fan_speed[0]);
    #endif

    #if HAS_ZOFFSET_ITEM
      DWIN_ICON_Show(ICON, ICON_Zoffset, ICON_ZOFFSET_X, ICON_ZOFFSET_Y);
    #endif

     if (BABY_Z_VAR < 0)
    {
      DWIN_Draw_FloatValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 2, 2, 191, NUM_ZOFFSET_Y, -BABY_Z_VAR * 100);
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, 191, NUM_ZOFFSET_Y-1, F("-"));
    }
    else
    {
      DWIN_Draw_FloatValue(true, true, 0, DWIN_FONT_STAT, Color_White, Color_Bg_Black, 2, 2, 191, NUM_ZOFFSET_Y, BABY_Z_VAR * 100);
      DWIN_Draw_String(false, true, font8x16, Color_White, Color_Bg_Black, 191, NUM_ZOFFSET_Y-1, F(" "));
    }
    // DWIN_Draw_Rectangle(1, Line_Color, LINE3_X_START, LINE3_Y_START, LINE3_X_END, LINE3_Y_END); //划线3
    // DWIN_Draw_Rectangle(1, Line_Color, LINE3_X_START, LINE3_Y_START+LINE3_SPACE, LINE3_X_END, LINE3_Y_END+LINE3_SPACE); //划线4
  #endif

  if (with_update)
  {
    update_middle_variable(); //
    DWIN_UpdateLCD();
    delay(5);
  }
}
void HMI_StartFrame(const bool with_update) {
  Goto_MainMenu();
  // Draw_Mid_Status_Area(true); //rock_20230529
}

void Draw_Info_Menu() 
{
  Clear_Main_Window(); 
  HMI_flag.Refresh_bottom_flag=true;//标志不刷新底部参数
  #if ENABLED(DWIN_CREALITY_480_LCD)
    if (HMI_flag.language < Language_Max)
    {
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Info, TITLE_X, TITLE_Y);
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Back, 60, 42);
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Size, 81, 87);  //尺寸
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Version, 81, 158);  //版本
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Contact, 81, 229);  //联系我们
      
      DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, (DWIN_WIDTH - strlen(MACHINE_SIZE) * MENU_CHR_W) / 2+20, 120, F(MACHINE_SIZE));
      DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, (DWIN_WIDTH - strlen(SHORT_BUILD_VERSION) * MENU_CHR_W) / 2+20, 195, F(SHORT_BUILD_VERSION));
      if(Chinese == HMI_flag.language)
      {
        DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, (DWIN_WIDTH - strlen(CORP_WEBSITE_C) * MENU_CHR_W) / 2+20, 268, F(CORP_WEBSITE_C));
      }
      else
      {
        DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, (DWIN_WIDTH - strlen(CORP_WEBSITE_E) * MENU_CHR_W) / 2+20, 268, F(CORP_WEBSITE_E));
      }
      
    }
    else
    {;}
    Draw_Back_First();
    LOOP_L_N(i, 3) 
    {
      DWIN_ICON_Show(ICON, ICON_PrintSize + i, 20, 79 + i * 39);
      DWIN_Draw_Line(Line_Color, 16, MBASE(2) + i * 73, BLUELINE_X, 156 + i * 73);
    }
    
    
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    if (HMI_flag.language < Language_Max)
    {
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Info, TITLE_X, TITLE_Y);
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Back, 42, 26);
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Size, 66, 64);  // 尺寸
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Version, 66, 115);  // 版本
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Contact, 66, 178);  // 联系我们
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Hardware_Version, 66, 247);  //联系我们
      DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, (DWIN_WIDTH - strlen(MACHINE_SIZE) * MENU_CHR_W) / 2 + 20, 90, F(MACHINE_SIZE));
      DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, (DWIN_WIDTH - strlen(SHORT_BUILD_VERSION) * MENU_CHR_W) / 2 + 20, 149, F(SHORT_BUILD_VERSION));
      if(Chinese == HMI_flag.language)
      {
        DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, (DWIN_WIDTH - strlen(CORP_WEBSITE_C) * MENU_CHR_W) / 2 + 20, 215, F(CORP_WEBSITE_C));
      }
      else
      {
        DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, (DWIN_WIDTH - strlen(CORP_WEBSITE_E) * MENU_CHR_W) / 2 + 20, 215, F(CORP_WEBSITE_E));
      }
      DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, (DWIN_WIDTH - strlen(BOARD_INFO_NAME) * MENU_CHR_W) / 2+20, 283, F(BOARD_INFO_NAME));
    }
    else
    {;}
    Draw_Back_First();
    LOOP_L_N(i, 3) 
    {
      DWIN_Draw_Line(Line_Color, 16, MBASE(2) + i * 67, BLUELINE_X, 106 + i * 67);
    }
    DWIN_ICON_Show(ICON, ICON_PrintSize, ICON_SIZE_X, ICON_SIZE_Y);
    DWIN_ICON_Show(ICON, ICON_PrintSize + 1, ICON_SIZE_X, 261-67*2);
    DWIN_ICON_Show(ICON, ICON_PrintSize + 2, ICON_SIZE_X, 261-67);
    DWIN_ICON_Show(ICON, ICON_Hardware_version,ICON_SIZE_X, 261);
    DWIN_Draw_Line(Line_Color, 16, MBASE(2) + 3 * 67, BLUELINE_X, 106 + 3 * 67);
  #endif
}

void Draw_Print_File_Menu()
{
  Clear_Title_Bar();
  HMI_flag.Refresh_bottom_flag=false; //标志刷新底部参数
  if(HMI_flag.language<Language_Max)
  {
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_SelectFile, TITLE_X, TITLE_Y); //rock_j file select
  }
  Redraw_SD_List();
}

void HMI_Select_language()
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  if (encoder_diffState == ENCODER_DIFF_CW)
  {
    // SERIAL_ECHOLNPAIR("select_language.now1=: ", select_language.now);
    // SERIAL_ECHOLNPAIR("index_language=: ", index_language);
    if (select_language.inc(1 + LANGUAGE_TOTAL))
    {
      if (select_language.now > (MROWS+2) && select_language.now > index_language)
      {
        index_language = select_language.now;
        Scroll_Menu_Full(DWIN_SCROLL_UP);
        #if ENABLED(DWIN_CREALITY_480_LCD)
          DWIN_ICON_Show(ICON, ICON_Word_CN + index_language - 1, 25, MBASE(MROWS+2) + JPN_OFFSET);
        #elif ENABLED(DWIN_CREALITY_320_LCD)
          DWIN_ICON_Show(ICON, ICON_Word_CN + index_language - 1, WORD_LAGUAGE_X, MBASE(MROWS+2) + JPN_OFFSET);
        #endif
        //Draw_Menu_Line(MROWS+2);
      }
      else
      {
        Move_Highlight(1, select_language.now + MROWS+2 - index_language);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW)
  {
    if (select_language.dec())
    {
      if (select_language.now < index_language - (MROWS+2))
      {
        index_language--;
        Scroll_Menu_Full(DWIN_SCROLL_DOWN);
        // Scroll_Menu(DWIN_SCROLL_DOWN);        
        if (index_language == MROWS+2)
        {
          DWIN_Draw_Rectangle(1, Color_Bg_Black, 12, MBASE(0) - 9, 239, MBASE(0) + 18);
          DWIN_ICON_Not_Filter_Show(ICON, ICON_Back, WORD_LAGUAGE_X, MBASE(0));
          DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Back, 42, MBASE(0) + JPN_OFFSET); // rock_j back
        }
        else
        {
          #if ENABLED(DWIN_CREALITY_480_LCD)
            DWIN_ICON_Show(ICON, ICON_Word_CN + select_language.now - 1, 25, MBASE(0) + JPN_OFFSET);
          #elif ENABLED(DWIN_CREALITY_320_LCD)
            DWIN_ICON_Show(ICON, ICON_Word_CN + select_language.now - 1, WORD_LAGUAGE_X, MBASE(0) + JPN_OFFSET);
          #endif
        }
        // Draw_Menu_Line(0);
      }
      else
      {
        Move_Highlight(-1, select_language.now + MROWS+2 - index_language);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER)
  {
      switch (select_language.now)
      {
        case 0: // Back
        break;
        case 1: // Back
          HMI_flag.language = Chinese;
          break;
        case 2: // Back
          HMI_flag.language =English;
          break;
        case 3: // Back
          HMI_flag.language =German;
          break;
        case 4: // Back
          HMI_flag.language = Russian;
          break;
        case 5: // Back
          HMI_flag.language = French;
          break;
        case 6: // Back
          HMI_flag.language = Turkish;
          break;
        case 7: // Back
          HMI_flag.language = Spanish;
          break;
        case 8: // Back
          HMI_flag.language = Italian;
          break;
        case 9: // Back
          HMI_flag.language = Portuguese;
          break;
        case 10: // Back
          HMI_flag.language = Japanese;
          break;
         case 11: // Back
          HMI_flag.language = Korean;
          break;  
        default:
          {
          HMI_flag.language = English;
          }
          break;
      }
    #if BOTH(EEPROM_SETTINGS, IIC_BL24CXX_EEPROM)
      BL24CXX::write(DWIN_LANGUAGE_EEPROM_ADDRESS, (uint8_t*)&HMI_flag.language, sizeof(HMI_flag.language));
    #endif
    checkkey = Prepare;
    select_prepare.set(10);
    Draw_Prepare_Menu();
    HMI_flag.Refresh_bottom_flag=false;//标志不刷新底部参数
  }
  DWIN_UpdateLCD();
}

/* Main Process */
void HMI_MainMenu()
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if ((encoder_diffState == ENCODER_DIFF_NO) || HMI_flag.abort_end_flag) return;
  if (encoder_diffState == ENCODER_DIFF_CW)
  {
    if (select_page.inc(4))
    {
      switch (select_page.now)
      {
        case 0: ICON_Print(); break;
        case 1: ICON_Print(); ICON_Prepare(); break;
        case 2: ICON_Prepare(); ICON_Control(); break;
        case 3: ICON_Control(); TERN(HAS_ONESTEP_LEVELING, ICON_Leveling, ICON_StartInfo)(1); break;
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW)
  {
    if (select_page.dec())
    {
      switch (select_page.now)
      {
        case 0: ICON_Print(); ICON_Prepare(); break;
        case 1: ICON_Prepare(); ICON_Control(); break;
        case 2: ICON_Control(); TERN(HAS_ONESTEP_LEVELING, ICON_Leveling, ICON_StartInfo)(0); break;
        case 3: TERN(HAS_ONESTEP_LEVELING, ICON_Leveling, ICON_StartInfo)(1); break;
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER)
  {
    switch (select_page.now)
    {
      case 0: // Print File
        checkkey = SelectFile;
        Draw_Print_File_Menu();
        break;

      case 1: // Prepare
        checkkey = Prepare;
        select_prepare.reset();
        index_prepare = MROWS;
        Draw_Prepare_Menu();
        break;

      case 2: // Control
        checkkey = Control;
        select_control.reset();
        index_control = MROWS;
        Draw_Control_Menu();
        break;

      case 3: // Leveling or Info
        #if HAS_ONESTEP_LEVELING
          // checkkey = Leveling;
          // HMI_Leveling();
          checkkey = ONE_HIGH;
          #if ANY(USE_AUTOZ_TOOL,USE_AUTOZ_TOOL_2)
            queue.inject_P(PSTR("M8015"));
            // Popup_Window_Home();
            Popup_Window_Height(Nozz_Start);
          #endif
        #else
          checkkey = Info;
          Draw_Info_Menu();
        #endif
        break;
    }
  }
  
  DWIN_UpdateLCD();
}

void DC_Show_defaut_image()
{
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_ICON_Show(ICON, ICON_Defaut_Image, 36, 35);
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_ICON_Show(ICON, ICON_Defaut_Image, ICON_Defaut_Image_X, ICON_Defaut_Image_Y);
  #endif
}

static void Isplay_Estimated_Time(int time) //显示剩余时间。
{
  int h,m,s;
  char cmd[30]={0};
    h=time/3600;
    m=time%3600/60;
    s=time%60;
  if(h > 0)
  {
    // sprintf_P(cmd, PSTR("%dh%dm%ds"), h,m,s);
    sprintf_P(cmd, PSTR("%dh%dm"), h,m);
    DWIN_Draw_String(false,true,font8x16, Popup_Text_Color, Color_Bg_Black,  WORD_TIME_X+DATA_OFFSET_X, WORD_TIME_Y+DATA_OFFSET_Y, cmd);
  }
  else
  {
    sprintf_P(cmd, PSTR("%dm%ds"), m,s);
    DWIN_Draw_String(false,true,font8x16, Popup_Text_Color, Color_Bg_Black,  WORD_TIME_X+DATA_OFFSET_X, WORD_TIME_Y+DATA_OFFSET_Y, cmd);
  }
}

//图片预览详情信息显示
static void Image_Preview_Information_Show(uint8_t ret)
{
  #if ENABLED(DWIN_CREALITY_480_LCD)
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    // 显示标题
    if (HMI_flag.language < Language_Max)
    {
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Estimated_Time, WORD_TIME_X, WORD_TIME_Y);
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Filament_Used, WORD_LENTH_X, WORD_LENTH_Y);
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Layer_Height, WORD_HIGH_X, WORD_HIGH_Y);
      #if ENABLED(USER_LEVEL_CHECK)  //使用调平校准功能
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Level_Calibration,WORD_PRINT_CHECK_X, WORD_PRINT_CHECK_Y);
      if(HMI_flag.Level_check_flag) DWIN_ICON_Show(ICON, ICON_LEVEL_CALIBRATION_ON, ICON_ON_OFF_X, ICON_ON_OFF_Y);
      else DWIN_ICON_Show(ICON, ICON_LEVEL_CALIBRATION_OFF, ICON_ON_OFF_X, ICON_ON_OFF_Y);
      #endif
    }
    if(ret == PIC_MISS_ERR)
    {
      //没有图片预览数据
      DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Black, WORD_TIME_X+DATA_OFFSET_X+52, WORD_TIME_Y+DATA_OFFSET_Y, F("0"));
      DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Black, WORD_LENTH_X+DATA_OFFSET_X+52, WORD_LENTH_Y+DATA_OFFSET_Y, F("0"));
      DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Black, WORD_HIGH_X+DATA_OFFSET_X+52, WORD_HIGH_Y+DATA_OFFSET_Y, F("0"));
      // DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Black, 175, 183, F("0"));
    }
    else
    {
      int predict_time;
      float height1, height2, height3, volume, Filament;
      char char_buf[20];
      char char_buf1[20];
      char str_1[20] = {0};
      predict_time=atoi((char*)model_information.pre_time);
      height1 = atof((char*)model_information.MAXZ);
      height2 = atof((char*)model_information.MINZ);
      // height3 = height1 - height2;
      height3 = atof((char*)model_information.height);

      // sprintf(char_buf,"%.1f",height3);
      sprintf_P(char_buf, PSTR("%smm"), dtostrf(height3, 1, 1, str_1));
      Filament = atof((char*)model_information.filament);
      
      volume = 2.4040625 * Filament;
      // sprintf(char_buf1,"%.1f",volume);
      // sprintf_P(char_buf1, PSTR("%smm^3"), dtostrf(volume, 1, 1, str_1));
      Isplay_Estimated_Time(predict_time); // 显示剩余时间
      DWIN_Draw_String(false,true,font8x16, Popup_Text_Color, Color_Bg_Black, WORD_LENTH_X+DATA_OFFSET_X, WORD_LENTH_Y+DATA_OFFSET_Y, &model_information.filament[0]);
      DWIN_Draw_String(false,true,font8x16, Popup_Text_Color, Color_Bg_Black, WORD_HIGH_X+DATA_OFFSET_X, WORD_HIGH_Y+DATA_OFFSET_Y, &model_information.height[0]);  // 高度
      // DWIN_Draw_String(false,true,font8x16, Popup_Text_Color, Color_Bg_Black, 175, 183, char_buf1);   // 体积
    }
  
  #endif
}

// Select (and Print) File
void HMI_SelectFile()
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();

  const uint16_t hasUpDir = !card.flag.workDirIsRoot;

  if (encoder_diffState == ENCODER_DIFF_NO)
  {
    #if ENABLED(SCROLL_LONG_FILENAMES)
      if (shift_ms && select_file.now >= 1 + hasUpDir)
      {
        // Scroll selected filename every second
        const millis_t ms = millis();
        if (ELAPSED(ms, shift_ms)) {
          const bool was_reset = shift_amt < 0;
          shift_ms = ms + 375UL + was_reset * 250UL;  // ms per character
          uint8_t shift_new = shift_amt + 1;          // Try to shift by...
          Draw_SDItem_Shifted(shift_new);             // Draw the item
          if (!was_reset && shift_new == 0)           // Was it limited to 0?
            shift_ms = 0;                             // No scrolling needed
          else if (shift_new == shift_amt)            // Scroll reached the end
            shift_new = -1;                           // Reset
          shift_amt = shift_new;                      // Set new scroll
        }
      }
    #endif
    return;
  }

  // First pause is long. Easy.
  // On reset, long pause must be after 0.

  const uint16_t fullCnt = nr_sd_menu_items();

  if (encoder_diffState == ENCODER_DIFF_CW && fullCnt)
  {
    if (select_file.inc(1 + fullCnt)) 
    {
      const uint8_t itemnum = select_file.now - 1;              // -1 for "Back"
      if (TERN0(SCROLL_LONG_FILENAMES, shift_ms)) 
      {
        // If line was shifted
        Erase_Menu_Text(itemnum + MROWS - index_file);          // Erase and
        Draw_SDItem(itemnum - 1);                               // redraw
      }
      if (select_file.now > MROWS && select_file.now > index_file)
      {
        // Cursor past the bottom
        index_file = select_file.now;                           // New bottom line
        Scroll_Menu(DWIN_SCROLL_UP);
        #if ENABLED(DWIN_CREALITY_320_LCD)
          DWIN_Draw_Rectangle(1, All_Black, 42, MBASE(MROWS)-2, DWIN_WIDTH, MBASE(MROWS + 1) - 12);
        #endif
        Draw_SDItem(itemnum, MROWS);                            // Draw and init the shift name
      }
      else
      {
        Move_Highlight(1, select_file.now + MROWS - index_file); // Just move highlight
        TERN_(SCROLL_LONG_FILENAMES, Init_Shift_Name());         // ...and init the shift name
      }
      TERN_(SCROLL_LONG_FILENAMES, Init_SDItem_Shift());
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW && fullCnt)
  {
    if (select_file.dec())
    {
      const uint8_t itemnum = select_file.now - 1;              // -1 for "Back"
      if (TERN0(SCROLL_LONG_FILENAMES, shift_ms))
      {
        // If line was shifted
        Erase_Menu_Text(select_file.now + 1 + MROWS - index_file); // Erase and
        Draw_SDItem(itemnum + 1);                               // redraw
      }
      if (select_file.now < index_file - MROWS)
      {
        // Cursor past the top
        index_file--;                                           // New bottom line
        Scroll_Menu(DWIN_SCROLL_DOWN);
        #if ENABLED(DWIN_CREALITY_320_LCD)
          DWIN_Draw_Rectangle(1, All_Black, 42, 25, DWIN_WIDTH, 59);
        #endif
        if (index_file == MROWS)
        {
          Draw_Back_First();
          TERN_(SCROLL_LONG_FILENAMES, shift_ms = 0);
        }
        else
        {
          Draw_SDItem(itemnum, 0);                              // Draw the item (and init shift name)
        }
      }
      else
      {
        Move_Highlight(-1, select_file.now + MROWS - index_file); // Just move highlight
        TERN_(SCROLL_LONG_FILENAMES, Init_Shift_Name());        // ...and init the shift name
      }
      TERN_(SCROLL_LONG_FILENAMES, Init_SDItem_Shift());        // Reset left. Init timer.
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER)
  {
    if (select_file.now == 0)
    {
      // Back
      select_page.set(0);
      Goto_MainMenu();

    }
    else if (hasUpDir && select_file.now == 1)
    {
      // CD-Up
      SDCard_Up();
      goto HMI_SelectFileExit;
    }
    else
    {
      const uint16_t filenum = select_file.now - 1 - hasUpDir;
      card.getfilename_sorted(SD_ORDER(filenum, card.get_num_Files()));
      if (card.flag.filenameIsDir)
      {
        SDCard_Folder(card.filename);
        goto HMI_SelectFileExit;
      }
      else
      {
        checkkey = Show_gcode_pic;
        select_show_pic.reset();
        HMI_flag.select_flag = true;
        Clear_Main_Window();
        Draw_Mid_Status_Area(true); //rock_20230529
        if (HMI_flag.language < Language_Max)
        {
          #if ENABLED(DWIN_CREALITY_480_LCD)
            DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Confirm, 26, 315);
            DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Cancel, 146, 315);
          #elif ENABLED(DWIN_CREALITY_320_LCD)
            DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Confirm, BUTTON_X, BUTTON_Y);
            DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Cancel, BUTTON_X+BUTTON_OFFSET_X, BUTTON_Y);
          #endif
        }
        #if ENABLED(USER_LEVEL_CHECK)
        select_show_pic.now=0;//默认选择
        #else
        select_show_pic.now=1;//默认选择
        #endif
        Draw_Show_G_Select_Highlight(true);        
      }
      char * const name = card.longest_filename();
      char str[strlen(name) + 1];
      // 取消后缀 例如:filename.gcode 去掉.gocde
      make_name_without_ext(str, name);
      Draw_Title(str);
      uint8_t ret = PIC_MISS_ERR;
      //Ender-3v3 SE暂时不支持图片预览功能，防止卡死，显示默认预览图片
      ret = gcodePicDataSendToDwin(card.filename, VP_OVERLAY_PIC_PREVIEW_1, PRIWIEW_PIC_FORMAT_NEED, PRIWIEW_PIC_RESOLITION_NEED);
      // if(ret == PIC_MISS_ERR) 
      DC_Show_defaut_image(); //由于此项目没有图片预览数据，所以显示默认小机器人图片    
      Image_Preview_Information_Show(ret);// 图片预览详情信息显示
    }
  }
  HMI_SelectFileExit:
  DWIN_UpdateLCD();
}

/* Printing */
void HMI_Printing()
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO||HMI_flag.pause_action||HMI_flag.Level_check_start_flag) return;
//HMI_flag.pause_action 
  if (HMI_flag.done_confirm_flag) 
  {
    if(encoder_diffState == ENCODER_DIFF_ENTER) {
      HMI_flag.done_confirm_flag = false;
      dwin_abort_flag = true; // Reset feedrate, return to Home
    }
    return;
  }
  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) 
  {
    if (select_print.inc(3))
    {
      switch (select_print.now)
      {
        case 0: ICON_Tune(); break;
        case 1:
          ICON_Tune();
          if (printingIsPaused())
          {
            ICON_Continue();
          }
          else 
          {
            ICON_Pause();
          }
          break;
        case 2:
          if (printingIsPaused())
          {
            
            ICON_Continue();
          }
          else 
          {
            ICON_Pause();
          }
          ICON_Stop();
          break;
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) 
  {
    if (select_print.dec()) {
      switch (select_print.now) {
        case 0:
          ICON_Tune();
          if (printingIsPaused()) ICON_Continue();
          else ICON_Pause();
          break;
        case 1:
          if (printingIsPaused()) ICON_Continue();
          else ICON_Pause();
          ICON_Stop();
          break;
        case 2: ICON_Stop(); break;
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_print.now) 
    {
      case 0: // Tune
        checkkey = Tune;
        HMI_ValueStruct.show_mode = 0;
        select_tune.reset();
        HMI_flag.Refresh_bottom_flag=false;//标志刷新底部参数
        index_tune = MROWS;
        Draw_Tune_Menu();
        break;
      case 1: // Pause
        if (HMI_flag.pause_flag) 
        {   //确定 
          Show_JPN_print_title();
          ICON_Pause();
          char cmd[40];
          cmd[0] = '\0';
          #if BOTH(HAS_HEATED_BED, PAUSE_HEAT)
            //if (resume_bed_temp) sprintf_P(cmd, PSTR("M190 S%i\n"), resume_bed_temp); //rock_20210901 
          #endif
          #if BOTH(HAS_HOTEND, PAUSE_HEAT)
            //if (resume_hotend_temp) sprintf_P(&cmd[strlen(cmd)], PSTR("M109 S%i\n"), resume_hotend_temp);
          #endif
          if(HMI_flag.cloud_printing_flag && !HMI_flag.filement_resume_flag)
          {
            SERIAL_ECHOLN("M79 S3");
          }
          pause_resume_feedstock(FEEDING_DEF_DISTANCE, FEEDING_DEF_SPEED);
          // strcat_P(cmd, M24_STR);
          queue.inject("M24");
          // RUN_AND_WAIT_GCODE_CMD("M24", true);
          // queue.enqueue_now_P(PSTR("M24"));
          // gcode.process_subcommands_now_P(PSTR("M24"));
          Goto_PrintProcess();
        }
        else 
        {
          //取消
          HMI_flag.select_flag = true;
          checkkey = Print_window;
          Popup_window_PauseOrStop();
        }
        break;
      case 2: // Stop
        HMI_flag.select_flag = true;
        checkkey = Print_window;
        Popup_window_PauseOrStop();
        break;
      default: break;
    }
  }
  DWIN_UpdateLCD();
}

/* Pause and Stop window */
void HMI_PauseOrStop() 
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  if (encoder_diffState == ENCODER_DIFF_CW)
    Draw_Select_Highlight(false);
  else if (encoder_diffState == ENCODER_DIFF_CCW)
    Draw_Select_Highlight(true);
  else if (encoder_diffState == ENCODER_DIFF_ENTER)
  {
    if (select_print.now == 1) 
    { // pause window
      if (HMI_flag.select_flag) 
      {
        HMI_flag.pause_action = true;
        if(HMI_flag.cloud_printing_flag&&!HMI_flag.filement_resume_flag)
        {
          SERIAL_ECHOLN("M79 S2");   // 3:cloud print pause
        }
        Goto_PrintProcess();
        // queue.inject_P(PSTR("M25"));
        RUN_AND_WAIT_GCODE_CMD("M25", true); 
        ICON_Continue();
        // queue.enqueue_now_P(PSTR("M25"));
      }
      else
      {
        Goto_PrintProcess();
      }

    }
    else if (select_print.now == 2) { // stop window
      if (HMI_flag.select_flag) 
      {
        if (HMI_flag.home_flag) planner.synchronize(); // Wait for planner moves to finish!
        wait_for_heatup = wait_for_user = false;       // Stop waiting for heating/user

        HMI_flag.disallow_recovery_flag=true;  //不允许恢复数据
        print_job_timer.stop();
        thermalManager.disable_all_heaters();
        print_job_timer.reset();
        thermalManager.setTargetHotend(0, 0);
        thermalManager.setTargetBed(0);
        thermalManager.zero_fan_speeds();

        recovery.info.sd_printing_flag=false;        //rock_20210820 
        //rock_20210830  下面这句绝对不能要，会进行两次会主界面操作
        //dwin_abort_flag = true;                      // Reset feedrate, return to Home  
        #ifdef ACTION_ON_CANCEL
          host_action_cancel();
        #endif
        // BL24CXX::EEPROM_Reset(PLR_ADDR, (uint8_t*)&recovery.info, sizeof(recovery.info));//rock_20210812  清空 EEPROM
        // checkkey = Popup_Window;
         Popup_Window_Home(true);  //rock_20221018
        
        card.abortFilePrintSoon();                     // Let the main loop handle SD abort  //rock_20211020
        checkkey = Back_Main;
        if(HMI_flag.cloud_printing_flag)
        {
          HMI_flag.cloud_printing_flag=false;
          SERIAL_ECHOLN("M79 S4");
        }
      }
      else
        Goto_PrintProcess(); // cancel stop
    }
  }
  DWIN_UpdateLCD();
}
#if ENABLED(HAS_CHECKFILAMENT)
/* Check filament */
void HMI_Filament()
{
  ENCODER_DiffState encoder_diffState;
  if(READ(CHECKFILAMENT_PIN)&&HMI_flag.cloud_printing_flag)
  {
    // 防止抖动  rock_20210910
    delay(200);
    if(READ(CHECKFILAMENT_PIN))
    {
      SERIAL_ECHOLN(STR_BUSY_PAUSED_FOR_USER); 
    }
  }
  else if(!READ(CHECKFILAMENT_PIN)&&(!HMI_flag.filament_recover_flag))
  {
    // 防止抖动  rock_20210910
    delay(200);
    if(!READ(CHECKFILAMENT_PIN))
    {
      HMI_flag.filament_recover_flag=true;
      // 清除更新区
      Popup_window_Filament(); 
    }
  }
  else if(READ(CHECKFILAMENT_PIN)&&(HMI_flag.filament_recover_flag))
  {
    // 防止抖动  rock_20210910
    delay(200);
    if(READ(CHECKFILAMENT_PIN))
    {
      HMI_flag.filament_recover_flag = false;
      // 清除更新区
      Popup_window_Filament();
    }
  }

  encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  if(encoder_diffState == ENCODER_DIFF_CW)
  {
    HMI_flag.select_flag = 0;
    #if ENABLED(DWIN_CREALITY_480_LCD)
      DWIN_Draw_Rectangle(0, Color_Bg_Window, 25, 279, 126, 318);
      DWIN_Draw_Rectangle(0, Color_Bg_Window, 24, 278, 127, 319);
      DWIN_Draw_Rectangle(0, Select_Color, 145, 279, 246, 318);
      DWIN_Draw_Rectangle(0, Select_Color, 144, 278, 247, 319);
    #elif ENABLED(DWIN_CREALITY_320_LCD)
      // DWIN_Draw_Rectangle(0, Color_Bg_Window, 25, 169, 126, 202);
      // DWIN_Draw_Rectangle(0, Color_Bg_Window, 24, 168, 127, 203);
      // DWIN_Draw_Rectangle(0, Select_Color, 133, 169, 228, 202);
      // DWIN_Draw_Rectangle(0, Select_Color, 132, 168, 229, 203);
          Draw_Select_Highlight(false);
    // DWIN_Draw_Rectangle(0, Color_Bg_Window, 133, 169, 226, 202);
    // DWIN_Draw_Rectangle(0, Color_Bg_Window, 132, 168, 227, 203);
    // DWIN_Draw_Rectangle(0, Select_Color, 25, 169, 126, 202);
    // DWIN_Draw_Rectangle(0, Select_Color, 24, 168, 127, 203);
    #endif
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW)
  {
    // SERIAL_ECHOPGM("go to ENCODER_DIFF_CCW");
    HMI_flag.select_flag = 1;
    #if ENABLED(DWIN_CREALITY_480_LCD)
      DWIN_Draw_Rectangle(0, Color_Bg_Window, 145, 279, 246, 318);
      DWIN_Draw_Rectangle(0, Color_Bg_Window, 144, 278, 247, 319);
      DWIN_Draw_Rectangle(0, Select_Color, 25, 279, 126, 318);
      DWIN_Draw_Rectangle(0, Select_Color, 24, 278, 127, 319);
    #elif ENABLED(DWIN_CREALITY_320_LCD)
      // DWIN_Draw_Rectangle(0, Color_Bg_Window, 145, 169, 228, 202);
      // DWIN_Draw_Rectangle(0, Color_Bg_Window, 144, 168, 229, 203);
      // DWIN_Draw_Rectangle(0, Select_Color, 25, 169, 126, 202);
      // DWIN_Draw_Rectangle(0, Select_Color, 24, 168, 127, 203);
      Draw_Select_Highlight(true);
    #endif
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER)
  {
    // SERIAL_ECHOLNPAIR("HMI_flag.select_flag=: ", HMI_flag.select_flag);
    if(HMI_flag.select_flag) 
    {
      // if(READ(CHECKFILAMENT_PIN))
      if(READ(CHECKFILAMENT_PIN) == 0)
      {
        // 防止抖动  rock_20210910
        delay(200);
        // if(READ(CHECKFILAMENT_PIN))
        if(READ(CHECKFILAMENT_PIN) == 0)
        {
          // resume
          HMI_flag.cutting_line_flag=false;
          temp_cutting_line_flag=false; 
          if(HMI_flag.cloud_printing_flag)
          {
            // 解除断料状态，可以响应云指令
            HMI_flag.filement_resume_flag=false;
            // SERIAL_ECHOLN("M79 S3");
            print_job_timer.start();
            gcode.process_subcommands_now_P(PSTR("M24"));
            Goto_PrintProcess();
            // 暂停界面
            ICON_Pause();
          }
          else
          {
            if((!HMI_flag.remove_card_flag)&& (!temp_remove_card_flag)) 
            {
              pause_resume_feedstock(FEEDING_DEF_DISTANCE,FEEDING_DEF_SPEED);
              gcode.process_subcommands_now_P(PSTR("M24"));
              Goto_PrintProcess();
            }
          }
          // 可以接收GCOD指令
          HMI_flag.disable_queued_cmd=false;
        }
      }
    }
    else
    {
      // 不再提示
      HMI_flag.cutting_line_flag = false;
      temp_cutting_line_flag = false;
      // HMI_flag.select_flag=1; //设置成默认选中确定按钮
      //  Goto_PrintProcess();    //rock_21010914
      if (HMI_flag.home_flag) planner.synchronize(); // Wait for planner moves to finish!
      wait_for_heatup = wait_for_user = false;       // Stop waiting for heating/user
      // 不允许恢复数据
      HMI_flag.disallow_recovery_flag=true;
      // rock_20211017
      queue.clear();
      quickstop_stepper();
      print_job_timer.stop();
      thermalManager.disable_all_heaters();
      print_job_timer.reset();
      thermalManager.setTargetHotend(0, 0);
      thermalManager.setTargetBed(0);
      thermalManager.zero_fan_speeds();

      // rock_20210820
      recovery.info.sd_printing_flag=false;
      // rock_20210830  下面这句绝对不能要，会进行两次会主界面操作
      // Reset feedrate, return to Home
      // dwin_abort_flag = true;
      
      #ifdef ACTION_ON_CANCEL
        host_action_cancel();
      #endif
      Popup_Window_Home(true);
      // Let the main loop handle SD abort  // rock_20211020
      card.abortFilePrintSoon();
      checkkey = Back_Main;
      if(HMI_flag.cloud_printing_flag)
      {
        HMI_flag.cloud_printing_flag=false;
        SERIAL_ECHOLN("M79 S4");
        // rock_20211022  tell wif_box print stop
        // gcode.process_subcommands_now_P(PSTR("M79 S4"));
      }
    }
    // 断料恢复标志位清零
    HMI_flag.filament_recover_flag=false;
  }
  DWIN_UpdateLCD();
}

/* Remove_card_window */
void HMI_Remove_card()
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  //ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  if (encoder_diffState == ENCODER_DIFF_ENTER)
  {
    // 有卡
    if(IS_SD_INSERTED())
    {
      HMI_flag.remove_card_flag = false;
      temp_remove_card_flag = false;
      // #if ENABLED(PAUSE_HEAT)
      //   char cmd[20];
      // #endif
      gcode.process_subcommands_now_P(PSTR("M24"));
      Goto_PrintProcess();
      // recovery.info.sd_printing_flag=remove_card_flag;
    }
    else
    {
      // Remove_card_window_check();
    }
  }
  DWIN_UpdateLCD();
}
#endif
void CR_Touch_error_func()
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  //ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  if (encoder_diffState == ENCODER_DIFF_ENTER)
  {    
    //HMI_flag.cr_touch_error_flag=false;
    HAL_reboot(); // MCU复位进入BOOTLOADER
  }
}
//一个确定按钮的界面
void HMI_Confirm()
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  //ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  if (encoder_diffState == ENCODER_DIFF_ENTER)
  { 
    if(Set_language==HMI_flag.boot_step)
    {     
      HMI_flag.boot_step=Set_high; //当前步骤转换到对高状态
          Popup_Window_Height(Nozz_Start);//跳转到对高页面
          checkkey = ONE_HIGH; //进入对高逻辑
           #if ANY(USE_AUTOZ_TOOL,USE_AUTOZ_TOOL_2)
            queue.inject_P(PSTR("M8015"));
           #endif 
            // checkkey=Max_GUI;
    }
    else if(Set_high==HMI_flag.boot_step) //开机引导的对高失败
    {
      HMI_flag.boot_step=Set_high; //当前对高失败后，重复对高操作 
      Popup_Window_Height(Nozz_Start);//跳转到对高页面
      checkkey = ONE_HIGH;            //进入对高逻辑
      #if ANY(USE_AUTOZ_TOOL,USE_AUTOZ_TOOL_2)
            queue.inject_P(PSTR("M8015"));
      #endif          
    }
    else if(Set_levelling==HMI_flag.boot_step) //开机引导调平成功
    {
       HMI_flag.boot_step=Boot_Step_Max; //当前步骤设置到开机引导完成标志位并保存
       Save_Boot_Step_Value();//保存开机引导步骤
        HMI_flag.Need_boot_flag=false; //以后不需要开机引导了
        //HMI_flag.G29_finish_flag=false;  //退出编辑页面，进入调平页面开始也不允许转动旋钮      
        select_page.set(0);//跳转到主界面的动作
        Goto_MainMenu();
    }
    else if(Boot_Step_Max==HMI_flag.boot_step)//正常对高失败后
    {
      Popup_Window_Height(Nozz_Start);//跳转到对高页面
      checkkey = ONE_HIGH; //进入对高逻辑
      #if ANY(USE_AUTOZ_TOOL,USE_AUTOZ_TOOL_2)
            queue.inject_P(PSTR("M8015"));
      #endif          
      // HMI_flag.Need_boot_flag=false; //以后不需要开机引导了      
      // select_page.set(0);//跳转到主界面的动作
      // Goto_MainMenu();
    }
    else //不在开机引导步骤的，弹出对高失败界面点击了“确定”按钮，重新开始对高
    {
      Popup_Window_Height(Nozz_Start);//跳转到对高页面
      checkkey = ONE_HIGH; //进入对高逻辑
      #if ANY(USE_AUTOZ_TOOL,USE_AUTOZ_TOOL_2)
         queue.inject_P(PSTR("M8015"));
      #endif
    }
     #if BOTH(EEPROM_SETTINGS, IIC_BL24CXX_EEPROM)
        //Save_Boot_Step_Value();//保存开机引导步骤
      #endif
  }
}
void HMI_Auto_In_Feedstock()
{
   ENCODER_DiffState encoder_diffState = get_encoder_state();
  // if (encoder_diffState == ENCODER_DIFF_NO /*|| (!HMI_flag.Auto_inout_end_flag)*/) return;
  if (!HMI_flag.Auto_inout_end_flag) return;
  if (encoder_diffState == ENCODER_DIFF_ENTER) //点击确认进料完成
  {
    //  checkkey = Prepare;
    //  select_prepare.set(PREPARE_CASE_INSTORK);
    //  Draw_Prepare_Menu();
    // SERIAL_ECHOLNPAIR("HMI_flag.Auto_inout_end_flag:", HMI_flag.Auto_inout_end_flag);
    // if (planner.has_blocks_queued())return;
    HMI_flag.Auto_inout_end_flag=false;
    In_out_feedtock(50,FEEDING_DEF_SPEED*2,true); //40mm/s进料30mm 
    In_out_feedtock(IN_DEF_DISTANCE-50,FEEDING_DEF_SPEED,true); //进料60mm
    
    HMI_flag.Refresh_bottom_flag=false;//刷新底部参数
    checkkey = Prepare;
    select_prepare.set(PREPARE_CASE_INSTORK);
    Draw_Mid_Status_Area(true); //rock_20230529
    
    Draw_Prepare_Menu();
    // HMI_Auto_IN_Out_Feedstock();            //返回准备界面。
    SET_HOTEND_TEMP(STOP_TEMPERATURE, 0);   //降温到140℃
    //WAIT_HOTEND_TEMP(60 * 5 * 1000, 5);    //等待喷嘴温度达到设定值
    
  }
}

void HMI_Auto_IN_Out_Feedstock()
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  // if (encoder_diffState == ENCODER_DIFF_NO && HMI_flag.Auto_inout_end_flag) return;
  // if (encoder_diffState == ENCODER_DIFF_NO && (!HMI_flag.Auto_inout_end_flag)) return;
  if (!HMI_flag.Auto_inout_end_flag) return;
  if (encoder_diffState == ENCODER_DIFF_ENTER) //点击确认退料完成
  {
    HMI_flag.Refresh_bottom_flag=false;//刷新底部参数
    checkkey = Prepare;
    Draw_Mid_Status_Area(true); //rock_20230529
    select_prepare.set(PREPARE_CASE_OUTSTORK);
    Draw_Prepare_Menu();
    HMI_flag.Auto_inout_end_flag=false;
  }
}
void Draw_Move_Menu()
{
  Clear_Main_Window();
  HMI_flag.Refresh_bottom_flag=true;//标志不刷新底部参数
  if (HMI_flag.language < Language_Max)
  {
    // "Move"
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Move_Title, TITLE_X, TITLE_Y);
    // DWIN_Frame_AreaCopy(1, 58, 118, 106, 132, LBLX, MBASE(1));
    // DWIN_Frame_AreaCopy(1, 109, 118, 157, 132, LBLX, MBASE(2));
    // DWIN_Frame_AreaCopy(1, 160, 118, 209, 132, LBLX, MBASE(3));
    #if ENABLED(DWIN_CREALITY_480_LCD)
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_MoveX, 60, MBASE(1) + JPN_OFFSET);
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_MoveY, 60, MBASE(2) + JPN_OFFSET);
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_MoveZ, 60, MBASE(3) + JPN_OFFSET);
      #if HAS_HOTEND
        // DWIN_Frame_AreaCopy(1, 212, 118, 253, 131, LBLX, MBASE(4));
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_MoveE, 60, MBASE(4) + JPN_OFFSET);
      #endif
    #elif ENABLED(DWIN_CREALITY_320_LCD)
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_MoveX, 50, MBASE(1) + JPN_OFFSET);
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_MoveY, 50, MBASE(2) + JPN_OFFSET);
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_MoveZ, 50, MBASE(3) + JPN_OFFSET);
      #if HAS_HOTEND
        // DWIN_Frame_AreaCopy(1, 212, 118, 253, 131, LBLX, MBASE(4));
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_MoveE, 50, MBASE(4) + JPN_OFFSET);
      #endif
    #endif
  }
  else
  {
    #ifdef USE_STRING_HEADINGS
      Draw_Title(GET_TEXT_F(MSG_MOVE_AXIS));
    #else
      DWIN_Frame_TitleCopy(1, 231, 2, 265, 12);                     // "Move"
    #endif
    draw_move_en(MBASE(1)); say_x(36, MBASE(1));                    // "Move X"
    draw_move_en(MBASE(2)); say_y(36, MBASE(2));                    // "Move Y"
    draw_move_en(MBASE(3)); say_z(36, MBASE(3));                    // "Move Z"
    #if HAS_HOTEND
      DWIN_Frame_AreaCopy(1, 123, 192, 176, 202, LBLX, MBASE(4));   // "Extruder"
    #endif
  }

  Draw_Back_First(select_axis.now == 0);
  if (select_axis.now) Draw_Menu_Cursor(select_axis.now);

  // Draw separators and icons
  LOOP_L_N(i, 3 + ENABLED(HAS_HOTEND)) Draw_Menu_Line(i + 1, ICON_MoveX + i);
}

void Draw_AdvSet_Menu() {
  Clear_Main_Window();

  #if ADVSET_CASE_TOTAL >= 6
    const int16_t scroll = MROWS - index_advset; // Scrolled-up lines
    #define ASCROL(L) (scroll + (L))
  #else
    #define ASCROL(L) (L)
  #endif

#define AVISI(L)  WITHIN(ASCROL(L), 0, MROWS)

  Draw_Title(GET_TEXT_F(MSG_ADVANCED_SETTINGS));

  if (AVISI(0)) Draw_Back_First(select_advset.now == 0);
  if (AVISI(ADVSET_CASE_HOMEOFF)) Draw_Menu_Line(ASCROL(ADVSET_CASE_HOMEOFF), ICON_HomeOff, GET_TEXT(MSG_SET_HOME_OFFSETS),true);  // Home Offset >
  #if HAS_ONESTEP_LEVELING
    if (AVISI(ADVSET_CASE_PROBEOFF)) Draw_Menu_Line(ASCROL(ADVSET_CASE_PROBEOFF), ICON_ProbeOff, GET_TEXT(MSG_ZPROBE_OFFSETS),true);  // Probe Offset >
  #endif
  if (AVISI(ADVSET_CASE_HEPID)) Draw_Menu_Line(ASCROL(ADVSET_CASE_HEPID), ICON_PIDNozzle, "Hotend PID", false);  // Nozzle PID
  if (AVISI(ADVSET_CASE_BEDPID)) Draw_Menu_Line(ASCROL(ADVSET_CASE_BEDPID), ICON_PIDbed, "Bed PID", false);  // Bed PID
  #if ENABLED(POWER_LOSS_RECOVERY)
    if (AVISI(ADVSET_CASE_PWRLOSSR)) {
      Draw_Menu_Line(ASCROL(ADVSET_CASE_PWRLOSSR), ICON_Motion, "Power-loss recovery", false);  // Power-loss recovery
      Draw_Chkb_Line(ASCROL(ADVSET_CASE_PWRLOSSR), recovery.enabled);
    }
  #endif
  if (select_advset.now) Draw_Menu_Cursor(ASCROL(select_advset.now));
}

void Draw_HomeOff_Menu()
{
  Clear_Main_Window();
  Draw_Title(GET_TEXT_F(MSG_SET_HOME_OFFSETS));                 // Home Offsets
  Draw_Back_First(select_item.now == 0);
  Draw_Menu_Line(1, ICON_HomeOffX, GET_TEXT(MSG_HOME_OFFSET_X));  // Home X Offset
  DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, 1, VALUERANGE_X, MBASE(1), HMI_ValueStruct.Home_OffX_scaled);
  Draw_Menu_Line(2, ICON_HomeOffY, GET_TEXT(MSG_HOME_OFFSET_Y));  // Home Y Offset
  DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, 1, VALUERANGE_X, MBASE(2), HMI_ValueStruct.Home_OffY_scaled);
  Draw_Menu_Line(3, ICON_HomeOffZ, GET_TEXT(MSG_HOME_OFFSET_Z));  // Home Y Offset
  DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, 1, VALUERANGE_X, MBASE(3), HMI_ValueStruct.Home_OffZ_scaled);
  if (select_item.now) Draw_Menu_Cursor(select_item.now);
}

#if HAS_ONESTEP_LEVELING
  void Draw_ProbeOff_Menu()
  {
    Clear_Main_Window();
    Draw_Title(GET_TEXT_F(MSG_ZPROBE_OFFSETS));                 // Probe Offsets
    Draw_Back_First(select_item.now == 0);
    Draw_Menu_Line(1, ICON_ProbeOffX, GET_TEXT(MSG_ZPROBE_XOFFSET));  // Probe X Offset
    DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, 1, VALUERANGE_X, MBASE(1), HMI_ValueStruct.Probe_OffX_scaled);
    Draw_Menu_Line(2, ICON_ProbeOffY, GET_TEXT(MSG_ZPROBE_YOFFSET));  // Probe Y Offset
    DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, 1, VALUERANGE_X, MBASE(2), HMI_ValueStruct.Probe_OffY_scaled);
    if (select_item.now) Draw_Menu_Cursor(select_item.now);
  }
#endif

#if ENABLED(SPEAKER)
  #include "../../../libs/buzzer.h"
#endif

void HMI_AudioFeedback(const bool success=true)
{
  if (success)
  {
    #if ENABLED(SPEAKER)
      buzzer.tone(100, 659);
      buzzer.tone(10, 0);
      buzzer.tone(100, 698);
    #endif
  }
  else
  {
    #if ENABLED(SPEAKER)
      buzzer.tone(40, 440);
    #endif
  }
}

/* Prepare */
void HMI_Prepare()
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW)
  {
    if (select_prepare.inc(1 + PREPARE_CASE_TOTAL))
    {
      if(select_prepare.now > MROWS && select_prepare.now > index_prepare)
      {
        index_prepare = select_prepare.now;
        // Scroll up and draw a blank bottom line
        Scroll_Menu(DWIN_SCROLL_UP);
        #if ENABLED(DWIN_CREALITY_320_LCD)
          DWIN_Draw_Rectangle(1, All_Black, 50, 206, DWIN_WIDTH, 236);
        #endif
        // 显示新增的图标
        Draw_Menu_Icon(MROWS, ICON_Axis + select_prepare.now - 1);
        // Draw "More" icon for sub-menus  显示新的更多符号到新行
        if (index_prepare < 7) Draw_More_Icon(MROWS - index_prepare + 1);
        if (index_prepare == PREPARE_CASE_OUTSTORK)
        {
          Item_Prepare_outstork(MROWS);
          // Draw_More_Icon(MROWS - index_prepare + 1);
        }
        #if HAS_HOTEND
          if (index_prepare == PREPARE_CASE_PLA) Item_Prepare_PLA(MROWS);
          if (index_prepare == PREPARE_CASE_ABS) Item_Prepare_ABS(MROWS);
        #endif
        #if HAS_PREHEAT
          if (index_prepare == PREPARE_CASE_COOL) Item_Prepare_Cool(MROWS);
        #endif
        if (index_prepare == PREPARE_CASE_LANG) Item_Prepare_Lang(MROWS);
      }
      else
      {
        Move_Highlight(1, select_prepare.now + MROWS - index_prepare);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW)
  {
    if (select_prepare.dec())
    {
      if (select_prepare.now < index_prepare - MROWS)
      {
        index_prepare--;
        Scroll_Menu(DWIN_SCROLL_DOWN);
        DWIN_Draw_Rectangle(1, Color_Bg_Black, 180, MBASE(0)-8, 238, MBASE(0 + 1) - 12);//
        if (index_prepare == MROWS)
          Draw_Back_First();
        // else
          // Draw_Menu_Line(0, ICON_Axis + select_prepare.now - 1);

        // if (index_prepare < 6) Draw_More_Icon(MROWS - index_prepare + 1);
        else if (index_prepare == 6) Item_Prepare_Move(0);
        else if (index_prepare == 7) Item_Prepare_Disable(0);
        else if (index_prepare == 8) Item_Prepare_Home(0);   
        else if (index_prepare == 9)  Item_Prepare_Offset(0);     
        else if (index_prepare == 10)Item_Prepare_instork(0);    
      }
      else
      {
        Move_Highlight(-1, select_prepare.now + MROWS - index_prepare);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER)
  {
    switch (select_prepare.now) 
    {
      case 0: // Back
        select_page.set(1);
        Goto_MainMenu();
        break;
      case PREPARE_CASE_MOVE: // Axis move
        if(!HMI_flag.power_back_to_zero_flag)
        {
          // HMI_flag.power_back_to_zero_flag = true;  //Rock_20230914
          HMI_flag.leveling_edit_home_flag=false;
          checkkey = Last_Prepare;
          index_prepare = MROWS;
          select_prepare.now = PREPARE_CASE_MOVE;
          queue.inject_P(G28_STR);          
          Popup_Window_Home();
          break;
        }
        checkkey = AxisMove;
        select_axis.reset();
        Draw_Move_Menu();
        gcode.process_subcommands_now_P(PSTR("G92 E0"));
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, VALUERANGE_X, MBASE(1), current_position.x * MINUNITMULT);
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, VALUERANGE_X, MBASE(2), current_position.y * MINUNITMULT);
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, VALUERANGE_X, MBASE(3), current_position.z * MINUNITMULT);
        #if HAS_HOTEND
          HMI_ValueStruct.Move_E_scaled = current_position.e * MINUNITMULT;
          DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, 1, VALUERANGE_X, MBASE(4), HMI_ValueStruct.Move_E_scaled);
        #endif
    
        break;
      case PREPARE_CASE_DISA: // Disable steppers
        queue.inject_P(PSTR("M84"));
        // rock_20211224 Solve the problem of artificially dropping the Z axis and causing the platform to crash.
        gcode.process_subcommands_now_P(PSTR("G92.9 Z0"));
        break;
      case PREPARE_CASE_HOME: // Homing        
        //HMI_flag.power_back_to_zero_flag = true; // rock_20230914
        checkkey = Last_Prepare;
        index_prepare = MROWS;
        select_prepare.now = PREPARE_CASE_HOME;
        queue.inject_P(G28_STR); // G28 will set home_flag
        Popup_Window_Home();
        break;
      case PREPARE_CASE_INSTORK: // Pressure height  
        checkkey = Last_Prepare;
        //checkkey = AUTO_OUT_FEEDSTOCK;    
        Popup_Window_Home();
        gcode.process_subcommands_now_P(PSTR("G28"));
        checkkey = Last_Prepare;
        //checkkey = AUTO_IN_FEEDSTOCK; 
        index_prepare =select_prepare.now;
        //select_prepare.now = PREPARE_CASE_INSTORK;
        Auto_in_out_feedstock(true); //进料    
        break;
    case PREPARE_CASE_OUTSTORK: 
        checkkey = Last_Prepare; //防止回零过程中可以再准备界面切换
        //checkkey = AUTO_IN_FEEDSTOCK;    
        Popup_Window_Home();
        gcode.process_subcommands_now_P(PSTR("G28"));
        checkkey = Last_Prepare; //防止回零过程中可以再准备界面切换
        //checkkey = AUTO_OUT_FEEDSTOCK; 
        // index_prepare = MROWS;
        //select_prepare.now = PREPARE_CASE_OUTSTORK;
        index_prepare =select_prepare.now;
        Auto_in_out_feedstock(false); //退料
        break;
      #if HAS_ZOFFSET_ITEM
        case PREPARE_CASE_ZOFF:     // Z-offset
          #if EITHER(HAS_BED_PROBE, BABYSTEPPING)
            checkkey = Homeoffset;
            HMI_ValueStruct.show_mode = -4;
            HMI_ValueStruct.offset_value = BABY_Z_VAR * 100;
            DWIN_Draw_Signed_Float(font8x16, Select_Color, 2, 2, VALUERANGE_X - 14, MBASE(PREPARE_CASE_ZOFF + MROWS - index_prepare), HMI_ValueStruct.offset_value);
            EncoderRate.enabled = true;
          #else
            // Apply workspace offset, making the current position 0,0,0
            queue.inject_P(PSTR("G92 X0 Y0 Z0"));
            HMI_AudioFeedback();
          #endif
          break;
      #endif
      #if HAS_PREHEAT
        case PREPARE_CASE_PLA: // PLA preheat
          TERN_(HAS_HEATED_BED, thermalManager.setTargetBed(ui.material_preset[0].bed_temp));
          TERN_(HAS_FAN, thermalManager.set_fan_speed(0, ui.material_preset[0].fan_speed));
          #if ENABLED(USE_SWITCH_POWER_200W) 
            while (ABS(thermalManager.degTargetBed() - thermalManager.degBed()) > TEMP_WINDOW)
            {
              idle();
            }
          #endif
          TERN_(HAS_HOTEND, thermalManager.setTargetHotend(ui.material_preset[0].hotend_temp, 0));
          break;
        case PREPARE_CASE_ABS: // ABS preheat
          TERN_(HAS_HEATED_BED, thermalManager.setTargetBed(ui.material_preset[1].bed_temp));
          TERN_(HAS_FAN, thermalManager.set_fan_speed(0, ui.material_preset[1].fan_speed));
          #if ENABLED(USE_SWITCH_POWER_200W)
            while (ABS(thermalManager.degTargetBed() - thermalManager.degBed()) > TEMP_WINDOW)
            {
              idle();
            }
          #endif
          TERN_(HAS_HOTEND, thermalManager.setTargetHotend(ui.material_preset[1].hotend_temp, 0));
          break;
        case PREPARE_CASE_COOL: // Cool
          TERN_(HAS_FAN, thermalManager.zero_fan_speeds());
          #if HAS_HOTEND || HAS_HEATED_BED
            thermalManager.disable_all_heaters();
          #endif
          break;
      #endif
      case PREPARE_CASE_LANG: // Toggle Language
        // HMI_ToggleLanguage();
        // Draw_Prepare_Menu();
        // break;
        select_language.reset();
        index_language = MROWS+2;
        HMI_flag.Refresh_bottom_flag=true;//标志不刷新底部参数
        Draw_Select_language();
        checkkey = Selectlanguage;
        break;
      default: break;
    }
  }
  DWIN_UpdateLCD();
}

void Draw_Temperature_Menu()
{
  Clear_Main_Window();
  Draw_Mid_Status_Area(true); //rock_20230529
  #if TEMP_CASE_TOTAL >= 6
    const int16_t scroll = MROWS - index_temp; // Scrolled-up lines
    #define CSCROL(L) (scroll + (L))
  #else
    #define CSCROL(L) (L)
  #endif
  #define CLINE(L)  MBASE(CSCROL(L))
  #define CVISI(L)  WITHIN(CSCROL(L), 0, MROWS)
 
  if (CVISI(0)) Draw_Back_First(select_temp.now == 0);
  if (HMI_flag.language < Language_Max) 
  {
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Temp_Title, TITLE_X, TITLE_Y);
    #if HAS_HOTEND
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Hotend, 42, CLINE(TEMP_CASE_TEMP) + JPN_OFFSET);
    #endif
    #if HAS_HEATED_BED
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Bedend, 42,CLINE(TEMP_CASE_BED) + JPN_OFFSET);
    #endif
    #if HAS_FAN
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Fan, 42, CLINE(TEMP_CASE_FAN) + JPN_OFFSET);
    #endif
    #if HAS_HOTEND
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_PLASetup, 42, CLINE(TEMP_CASE_PLA) + JPN_OFFSET);
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_ABSSetup, 42, CLINE(TEMP_CASE_ABS) + JPN_OFFSET);
    #endif
  }
  else {;}
  if (CVISI(TEMP_CASE_HM_PID))Item_Temp_HMPID(CLINE(TEMP_CASE_HM_PID));
  if (CVISI(TEMP_CASE_Auto_PID))Item_Temp_AutoPID(CLINE(TEMP_CASE_Auto_PID));

  // if (select_temp.now) Draw_Menu_Cursor(select_temp.now);
  if (select_temp.now && CVISI(select_temp.now))
    Draw_Menu_Cursor(CSCROL(select_temp.now));
  // Draw icons and lines
  #define _TEMP_SET_ICON(N, I, M, Q) do { \
    if (CVISI(N)) { \
      Draw_Menu_Line(CSCROL(N), I); \
      if (M) { \
        Draw_More_Icon(CSCROL(N)); \
      } \
      if (Q) { \
         Show_Temp_Default_Data(CSCROL(N),N); \
      } \
    } \
  } while(0)

  _TEMP_SET_ICON(TEMP_CASE_TEMP, ICON_SetEndTemp, false, true);
  _TEMP_SET_ICON(TEMP_CASE_BED, ICON_SetBedTemp, false, true);
  _TEMP_SET_ICON(TEMP_CASE_FAN, ICON_FanSpeed, false, true);
  _TEMP_SET_ICON(TEMP_CASE_PLA, ICON_SetPLAPreheat, true, false);
  _TEMP_SET_ICON(TEMP_CASE_ABS, ICON_SetABSPreheat, true, false);
  _TEMP_SET_ICON(TEMP_CASE_HM_PID, ICON_HM_PID, true, false);
  _TEMP_SET_ICON(TEMP_CASE_Auto_PID, ICON_Auto_PID, true, false);
}

/* Control */
void HMI_Control() 
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) 
  {
    if (select_control.inc(1 + CONTROL_CASE_TOTAL)) 
    {
      if (select_control.now > MROWS && select_control.now > index_control) 
      {
        index_control = select_control.now;
        // Scroll up and draw a blank bottom line
        Scroll_Menu(DWIN_SCROLL_UP);

        switch (index_control) 
        {  // Last menu items
          case CONTROL_CASE_RESET:
          Item_Control_Reset(MBASE(MROWS));
           Draw_Menu_Icon(MROWS, ICON_ResumeEEPROM);
          break;
          case CONTROL_CASE_INFO:    // Info >
            Item_Control_Info(MBASE(MROWS));
            Draw_Menu_Icon(MROWS, ICON_Info);
            DWIN_ICON_Show(ICON, ICON_More, 208, MBASE(MROWS) - 3);
            break;
          default: break;
        }
      }
      else 
      {
        Move_Highlight(1, select_control.now + MROWS - index_control);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_control.dec()) {
      if (select_control.now < index_control - MROWS) {
        index_control--;
        Scroll_Menu(DWIN_SCROLL_DOWN);
        switch (index_control) {  // First menu items
          case MROWS :
          Draw_Back_First();
            break;
          case MROWS + 1: // Temperature >
            if (HMI_flag.language < Language_Max)
            {
              Draw_Menu_Icon(0, ICON_Temperature);
              Draw_More_Icon(0);
              // DWIN_Frame_AreaCopy(1, 57, 104,  84, 116, 60, 53);  //显示温度到左上角
              // DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_Temp_Title, TITLE_X, TITLE_Y);
              DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_Temp,  42, MBASE(0) + JPN_OFFSET);
              // DWIN_Draw_Line(Line_Color, 16, 49 + 33, BLUELINE_X, 49 + 34);

            }       
            break;
          case MROWS + 2: // Move >
            Draw_Menu_Line(0, ICON_Motion, GET_TEXT(MSG_MOTION), true);
            // DWIN_Frame_AreaCopy(1,  87, 104, 114, 116, 60, 155);   // Motion >
          default: break;
        }
      }
      else {
        Move_Highlight(-1, select_control.now + MROWS - index_control);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_control.now) {
      case 0: // Back
        select_page.set(2);
        Goto_MainMenu();
        break;
      case CONTROL_CASE_TEMP:  // Temperature
        checkkey = TemperatureID;
        HMI_ValueStruct.show_mode = -1;
        select_temp.reset();
        Draw_Temperature_Menu();
        break;
      case CONTROL_CASE_MOVE: // Motion
        checkkey = Motion;
        select_motion.reset();
        Draw_Motion_Menu();
        break;
      #if ENABLED(EEPROM_SETTINGS)
        case CONTROL_CASE_SAVE:
          {
            // Write EEPROM
            const bool success = settings.save();
            HMI_AudioFeedback(success);
          }
          break;
        case CONTROL_CASE_LOAD:
          {
            // Read EEPROM
            const bool success = settings.load();
            HMI_AudioFeedback(success);
          }
          break;
            #if HAS_LEVELING
        case CONTROL_CASE_SHOW_DATA:
          HMI_flag.G29_finish_flag=true;
          HMI_flag.Edit_Only_flag=true; 
          Popup_Window_Leveling();              
          checkkey = Leveling;
          Refresh_Leveling_Value();      //刷新调平值和颜色到屏幕上 
          
        break;
        #endif
        case CONTROL_CASE_RESET:
          // Reset EEPROM
          settings.reset();
          HMI_AudioFeedback();
          // 复位后语言要重新刷新一次
          // Draw_Control_Menu();
          Clear_Main_Window();
          #if ENABLED(DWIN_CREALITY_480_LCD)
            DWIN_ICON_Show(ICON, ICON_LOGO, 71, 52);
          #elif ENABLED(DWIN_CREALITY_320_LCD)
            //DWIN_ICON_Show(ICON, ICON_LOGO, 72, 36);
            DWIN_ICON_Not_Filter_Show(Background_ICON, Background_reset, 0, 25);
          #endif
          settings.save();
          delay(100);
          HMI_ResetLanguage();
          HMI_ValueStruct.Auto_PID_Value[1] = 100;  // PID数复位
          HMI_ValueStruct.Auto_PID_Value[2] = 260;  // PID数复位
          Save_Auto_PID_Value();
          // DWIN_SHOW_MAIN_PIC();
          HAL_reboot(); // MCU复位进入BOOTLOADER
          break;
      #endif
      /*
      // rock_20210727
      // Advanced Settings
      case CONTROL_CASE_ADVSET:
        checkkey = AdvSet;
        select_advset.reset();
        Draw_AdvSet_Menu();
        break;
      */
      case CONTROL_CASE_INFO: // Info
        checkkey = Info;
        Draw_Info_Menu();
        break;
      default: break;
    }
  }
  DWIN_UpdateLCD();
}

#if HAS_ONESTEP_LEVELING
//改变调平值 //Изменить значение выравнивания
void HMI_Levling_Change()
{
    uint16_t rec_LU_x,rec_LU_y,rec_RD_x,rec_RD_y,value_LU_x,value_LU_y; 
    ENCODER_DiffState encoder_diffState = get_encoder_state();
    if ((encoder_diffState == ENCODER_DIFF_NO)) return;
    else 
    {
      xy_int8_t mesh_Count=Converted_Grid_Point(select_level.now);    //转换网格点  //Преобразование точек сетки
      //计算矩形区域 //Вычислить площадь прямоугольника
      rec_LU_x=Rect_LU_X_POS+mesh_Count.x*X_Axis_Interval;
      // rec_LU_y=Rect_LU_Y_POS+mesh_Count.y*Y_Axis_Interval;
      rec_LU_y=Rect_LU_Y_POS-mesh_Count.y*Y_Axis_Interval;
      rec_RD_x=Rect_RD_X_POS+mesh_Count.x*X_Axis_Interval;
      // rec_RD_y=Rect_RD_Y_POS+mesh_Count.y*Y_Axis_Interval;
      rec_RD_y=Rect_RD_Y_POS-mesh_Count.y*Y_Axis_Interval;
      //补偿值的位置 //Положение компенсационного значения
      value_LU_x=rec_LU_x+1;
      // value_LU_y=rec_LU_y+4;
      value_LU_y=rec_LU_y+(rec_RD_y-rec_LU_y)/2-6;   
      if (Apply_Encoder(encoder_diffState, HMI_ValueStruct.Temp_Leveling_Value)) //点击了确认键 //Нажал кнопку подтверждения
      {
        checkkey = Leveling;
        Draw_Leveling_Highlight(true);  //默认选择框到编辑 //Поле выбора по умолчанию для редактирования
        // Refresh_Leveling_Value();    //刷新调平值和颜色到屏幕上 //Вывод значений и цветов выравнивания на экран
        z_values[mesh_Count.x][mesh_Count.y]=HMI_ValueStruct.Temp_Leveling_Value/100;
        // refresh_bed_level();
        // xy_int8_t mesh_Count=Converted_Grid_Point(select_level.now);    //转换网格点
        //SERIAL_ECHOLNPAIR("temp_zoffset_single:", temp_zoffset_single, );
        //  babystep.add_mm(Z_AXIS, -temp_zoffset_single); //撤回移动的 baby_steper
        // DO_BLOCKING_MOVE_TO_Z(3-temp_zoffset_single, 5);//每次都上升到5mm的高度再移动
        // HMI_ValueStruct.Temp_Leveling_Value=(HMI_ValueStruct.Temp_Leveling_Value/100-temp_zoffset_single)*100;
        // Draw_Dots_On_Screen(&mesh_Count,1,Select_Block_Color); 
        HMI_ValueStruct.Temp_Leveling_Value=z_values[mesh_Count.x][mesh_Count.y]*100;
        //SERIAL_ECHOLNPAIR("HMI_ValueStruct.Temp_Leveling_Value22:", z_values[mesh_Count.x][mesh_Count.y]);
        DWIN_Draw_Z_Offset_Float(font6x12,Color_White,Select_Color, 1, 2, value_LU_x, value_LU_y,HMI_ValueStruct.Temp_Leveling_Value);//左上角坐标 //Координаты верхнего левого угла 
        Draw_Dots_On_Screen(&mesh_Count,0,0); //将当前选中块置为不选中 //Сделать текущий выбранный блок невыделенным
        DO_BLOCKING_MOVE_TO_Z(5, 5);//每次都上升到5mm的高度再移动  //Каждый раз перед перемещением поднимайте на высоту 5 мм.        
      }
      else 
      {
        LIMIT(HMI_ValueStruct.Temp_Leveling_Value, (Z_PROBE_OFFSET_RANGE_MIN) * 100, (Z_PROBE_OFFSET_RANGE_MAX) * 100);
        last_zoffset_edit = dwin_zoffset_edit;
        dwin_zoffset_edit = HMI_ValueStruct.Temp_Leveling_Value / 100.0f;
        temp_zoffset_single+=(dwin_zoffset_edit - last_zoffset_edit);
        // babystep.add_mm(Z_AXIS, dwin_zoffset_edit - last_zoffset_edit); 
        DO_BLOCKING_MOVE_TO_Z(dwin_zoffset_edit, 5); 
        DWIN_Draw_Z_Offset_Float(font6x12,Color_White,Select_Color, 1, 2, value_LU_x, value_LU_y,HMI_ValueStruct.Temp_Leveling_Value);//左上角坐标       
        // Draw_Dots_On_Screen(&mesh_Count,2,Select_Color); //设置字体背景色，不改变选中块颜色 
      }
    }
}
//编辑调平数据页面 //Страница редактирования данных о выравнивании
  void HMI_Leveling_Edit()
  {
    uint16_t rec_LU_x,rec_LU_y,rec_RD_x,rec_RD_y,value_LU_x,value_LU_y; 
    ENCODER_DiffState encoder_diffState = get_encoder_state();
    if ((encoder_diffState == ENCODER_DIFF_NO)) return;
    if (encoder_diffState == ENCODER_DIFF_CW)
    {
      if (select_level.inc(GRID_MAX_POINTS_X*GRID_MAX_POINTS_Y))
      {
        Level_Scroll_Menu(DWIN_SCROLL_UP,select_level.now);        
      }
      //选中增加逻辑 //Выберите Добавить логику.
    }
    else if (encoder_diffState == ENCODER_DIFF_CCW)
    {
      //选中减少逻辑 //Проверьте логику сокращения
      if (select_level.dec()) 
      {
        Level_Scroll_Menu(DWIN_SCROLL_DOWN,select_level.now);
      }
    }
    else if (encoder_diffState == ENCODER_DIFF_ENTER) 
    {
       xy_int8_t mesh_Count=Converted_Grid_Point(select_level.now);    //转换网格点 //Преобразование точек сетки
           //计算矩形区域 //Вычислить площадь прямоугольника
      rec_LU_x=Rect_LU_X_POS+mesh_Count.x*X_Axis_Interval;
      // rec_LU_y=Rect_LU_Y_POS+mesh_Count.y*Y_Axis_Interval;
      rec_LU_y=Rect_LU_Y_POS-mesh_Count.y*Y_Axis_Interval;
      rec_RD_x=Rect_RD_X_POS+mesh_Count.x*X_Axis_Interval;
      // rec_RD_y=Rect_RD_Y_POS+mesh_Count.y*Y_Axis_Interval;
      rec_RD_y=Rect_RD_Y_POS-mesh_Count.y*Y_Axis_Interval;
      //补偿值的位置
      value_LU_x=rec_LU_x+1;
      // value_LU_y=rec_LU_y+4;
      value_LU_y=rec_LU_y+(rec_RD_y-rec_LU_y)/2-6;
      //临时代码  需要继续优化      
      // xy_int8_t mesh_Count=Converted_Grid_Point(select_level.now);    //转换网格点
      Draw_Dots_On_Screen(&mesh_Count,2,Select_Color); //设置字体背景色，不改变选中块颜色
      checkkey = Change_Level_Value;
      temp_zoffset_single=0; //当前点的调节前的调平值
      dwin_zoffset_edit=z_values[mesh_Count.x][mesh_Count.y];
      HMI_ValueStruct.Temp_Leveling_Value=z_values[mesh_Count.x][mesh_Count.y]*100;
      //SERIAL_ECHOLNPAIR("HMI_ValueStruct.Temp_Leveling_Value11:", z_values[mesh_Count.x][mesh_Count.y]);
       DWIN_Draw_Z_Offset_Float(font6x12,Color_White,Select_Color, 1, 2, value_LU_x, value_LU_y,HMI_ValueStruct.Temp_Leveling_Value);//左上角坐标       
      // Draw_Dots_On_Screen(&mesh_Count,1,Select_Block_Color);  
      DO_BLOCKING_MOVE_TO_XY(mesh_Count.x*G29_X_INTERVAL+G29_X_MIN, mesh_Count.y*G29_Y_INTERVAL+G29_Y_MIN, 100);
      DO_BLOCKING_MOVE_TO_Z(z_values[mesh_Count.x][mesh_Count.y], 5);    
    }
  }

  
  /* Leveling */
  void HMI_Leveling() 
  {
    ENCODER_DiffState encoder_diffState = get_encoder_state();  
    if ((encoder_diffState == ENCODER_DIFF_NO)|| !HMI_flag.G29_finish_flag) return;
    if (encoder_diffState == ENCODER_DIFF_CW)
      Draw_Leveling_Highlight(false);
    else if (encoder_diffState == ENCODER_DIFF_CCW)
      Draw_Leveling_Highlight(true);
    else if (encoder_diffState == ENCODER_DIFF_ENTER) 
    {
      if(HMI_flag.select_flag) //点击编辑 //Нажмите, чтобы изменить
      {
        if(!HMI_flag.power_back_to_zero_flag)
        {
          HMI_flag.leveling_edit_home_flag=true;//调平编辑回零中，禁止其他操作 //Выравнивание и редактирование обнуляются, а другие операции запрещаются.
          HMI_flag.power_back_to_zero_flag = true;
           checkkey = Last_Prepare;
           Popup_Window_Home();
           RUN_AND_WAIT_GCODE_CMD(G28_STR, 1);           
        }
        else
        {
          gcode.process_subcommands_now_P(PSTR("M420 S0"));
          checkkey=Level_Value_Edit;
          select_level.reset();
          xy_int8_t mesh_Count={0,0};
          Draw_Dots_On_Screen(&mesh_Count,1,Select_Block_Color);          
          EncoderRate.enabled = true;
          DO_BLOCKING_MOVE_TO_Z(5, 5);//每次都上升到5mm的高度再移动 //Каждый раз перед перемещением поднимайте на высоту 5 мм.
        }
      }
      else //点击确定 //Нажмите ОК
      {
        gcode.process_subcommands_now_P(PSTR("M420 S1"));
        refresh_bed_level();
        settings.save();   //保存编辑好的调平数据到EEPROM //Сохраните отредактированные данные нивелира в EEPROM.
        delay(100);
        HMI_flag.G29_finish_flag=false;  //退出编辑页面，进入调平页面开始也不允许转动旋钮 //Даже после выхода со страницы редактирования и входа на страницу прокачки поворот ручки не допускается.
        if(HMI_flag.Edit_Only_flag)
        {
          HMI_flag.Edit_Only_flag=false;
          checkkey = Control;
          select_control.set(CONTROL_CASE_SHOW_DATA);
          Draw_Control_Menu();
        }
        else
        {
          Goto_MainMenu();//回到主界面 //Вернитесь в основной интерфейс
        } 
        // HMI_flag.Refresh_bottom_flag=false;//标志不刷新底部参数
        // Draw_Mid_Status_Area(true); //rock_20230529 //更新一次全部参数       
      }
    }
  }

#endif

/* Axis Move */
void HMI_AxisMove()
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  #if ENABLED(PREVENT_COLD_EXTRUSION)
    // popup window resume
    if (HMI_flag.ETempTooLow_flag) 
    {
      if (encoder_diffState == ENCODER_DIFF_ENTER) 
      {
        HMI_flag.ETempTooLow_flag = false;
        HMI_ValueStruct.Move_X_scaled = current_position.x * MINUNITMULT;  // rock_20210827
        HMI_ValueStruct.Move_Y_scaled = current_position.y * MINUNITMULT;  // rock_20210827
        HMI_ValueStruct.Move_Z_scaled = current_position.z * MINUNITMULT;
        HMI_ValueStruct.Move_E_scaled = current_position.e * MINUNITMULT;
        Draw_Move_Menu();
        // DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, VALUERANGE_X, MBASE(1), HMI_ValueStruct.Move_X_scaled);
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, VALUERANGE_X, MBASE(1), HMI_ValueStruct.Move_X_scaled);
        // DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, VALUERANGE_X, MBASE(2), HMI_ValueStruct.Move_Y_scaled);
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, VALUERANGE_X, MBASE(2), HMI_ValueStruct.Move_Y_scaled);
        // DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 1, VALUERANGE_X, MBASE(3), HMI_ValueStruct.Move_Z_scaled);
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, UNITFDIGITS, VALUERANGE_X, MBASE(3), HMI_ValueStruct.Move_Z_scaled);
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, 1, VALUERANGE_X, MBASE(4), HMI_ValueStruct.Move_E_scaled);
        DWIN_UpdateLCD();
        return;
      }
      else return; //解决弹出低温窗口还能旋转的界面 rock_20230602   
    }
  #endif

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW)
  {
    if (select_axis.inc(1 + 3 + ENABLED(HAS_HOTEND))) Move_Highlight(1, select_axis.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW)
  {
    if (select_axis.dec()) Move_Highlight(-1, select_axis.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER)
  {
    switch (select_axis.now)
    {
      case 0: // Back
        checkkey = Prepare;
        select_prepare.set(1);
        index_prepare = MROWS;
        Draw_Prepare_Menu();
        break;
      case 1: // X axis move
        checkkey = Move_X;
        HMI_ValueStruct.Move_X_scaled = current_position.x * MINUNITMULT;
       // DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 1, VALUERANGE_X, MBASE(1), HMI_ValueStruct.Move_X_scaled);
        DWIN_Draw_Signed_Float(font8x16,Select_Color, 3, UNITFDIGITS, VALUERANGE_X,  MBASE(1), HMI_ValueStruct.Move_X_scaled);
        EncoderRate.enabled = true;
        break;
      case 2: // Y axis move
        checkkey = Move_Y;
        HMI_ValueStruct.Move_Y_scaled = current_position.y * MINUNITMULT;
        //DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 1, VALUERANGE_X, MBASE(2), HMI_ValueStruct.Move_Y_scaled);
        DWIN_Draw_Signed_Float(font8x16,Select_Color, 3, UNITFDIGITS, VALUERANGE_X, MBASE(2), HMI_ValueStruct.Move_Y_scaled);
        EncoderRate.enabled = true;
        break;
      case 3: // Z axis move
        checkkey = Move_Z;
        HMI_ValueStruct.Move_Z_scaled = current_position.z * MINUNITMULT;
        // DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 1, VALUERANGE_X, MBASE(3), HMI_ValueStruct.Move_Z_scaled);
        DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, UNITFDIGITS, VALUERANGE_X, MBASE(3), HMI_ValueStruct.Move_Z_scaled);
        EncoderRate.enabled = true;
        break;
        #if HAS_HOTEND
          case 4: // Extruder
            #ifdef PREVENT_COLD_EXTRUSION
              if (thermalManager.tooColdToExtrude(0))
              {
                HMI_flag.ETempTooLow_flag = true;
                Popup_Window_ETempTooLow();
                DWIN_UpdateLCD();
                return;
              }
            #endif
             checkkey = Extruder;
            HMI_ValueStruct.Move_E_scaled = current_position.e * MINUNITMULT;
            DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, 1, VALUERANGE_X, MBASE(4), HMI_ValueStruct.Move_E_scaled);
            EncoderRate.enabled = true;
            break;
        #endif
    }
  }
  DWIN_UpdateLCD();
}

/* TemperatureID */
void HMI_Temperature()
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  if (encoder_diffState == ENCODER_DIFF_CW) 
  {
    if (select_temp.inc(1 + TEMP_CASE_TOTAL)) 
    {
      if (select_temp.now > MROWS && select_temp.now > index_temp) 
      {
        index_temp = select_temp.now;
        
        // Scroll up and draw a blank bottom line
        Scroll_Menu(DWIN_SCROLL_UP);
        switch (index_temp)
        {
          // 手动PID设置
          case TEMP_CASE_HM_PID:
            Draw_Menu_Icon(MROWS, ICON_HM_PID);
            if (HMI_flag.language < Language_Max)
            {
              #if ENABLED(DWIN_CREALITY_480_LCD)
                DWIN_ICON_Show(HMI_flag.language, LANGUAGE_PID_Manually, 42, MBASE(MROWS) + JPN_OFFSET);
              #elif ENABLED(DWIN_CREALITY_320_LCD)
                DWIN_ICON_Show(HMI_flag.language, LANGUAGE_PID_Manually, 42, MBASE(MROWS) + JPN_OFFSET);
              #endif
            }
            break;
          case TEMP_CASE_Auto_PID:    // 自动PID设置
            Draw_Menu_Icon(MROWS, ICON_Auto_PID);
            if (HMI_flag.language < Language_Max)
            {
              #if ENABLED(DWIN_CREALITY_480_LCD)
              #elif ENABLED(DWIN_CREALITY_320_LCD)
                DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Auto_PID, 42, MBASE(MROWS) + JPN_OFFSET);
              #endif
            }
              // queue.inject_P(G28_STR); // G28 will set home_flag
              // // Popup_Window_Home();
            break;
          default:
            break;
        }
        Draw_More_Icon(MROWS);
      }
      else
      {
        Move_Highlight(1, select_temp.now + MROWS - index_temp);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW)
  {
    if (select_temp.dec()) 
    {
      if (select_temp.now < index_temp - MROWS)
      {
        index_temp--;
        Scroll_Menu(DWIN_SCROLL_DOWN);
        switch (select_temp.now)
        {
          case 0:
            Draw_Back_First();
            break;
          case 1:
            Draw_Nozzle_Temp_Label();
            break;
          default:
            break;
        }
      }
      else
      {
        Move_Highlight(-1, select_temp.now + MROWS - index_temp);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER)
  {
    switch (select_temp.now)
    {
      case 0: // Back
        checkkey = Control;
        select_control.set(1);
        index_control = MROWS;
        Draw_Control_Menu();
        break;
      #if HAS_HOTEND
        case TEMP_CASE_TEMP:  //Nozzle temperature
          checkkey = ETemp;
          HMI_ValueStruct.E_Temp = thermalManager.degTargetHotend(0);
          LIMIT(HMI_ValueStruct.E_Temp, HEATER_0_MINTEMP, thermalManager.hotend_max_target(0));
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, VALUERANGE_X, MBASE(select_temp.now + MROWS - index_temp)+TEMP_SET_OFFSET, HMI_ValueStruct.E_Temp);
          EncoderRate.enabled = true;
          HMI_ValueStruct.show_mode = -1;
          break;
      #endif
      #if HAS_HEATED_BED
        case TEMP_CASE_BED: // Bed temperature
          checkkey = BedTemp;
          HMI_ValueStruct.Bed_Temp = thermalManager.degTargetBed();
          LIMIT(HMI_ValueStruct.Bed_Temp, BED_MINTEMP, BED_MAX_TARGET);
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, VALUERANGE_X, MBASE(select_temp.now + MROWS - index_temp)+TEMP_SET_OFFSET, HMI_ValueStruct.Bed_Temp);
          EncoderRate.enabled = true;
          HMI_ValueStruct.show_mode = -1;
          break;
      #endif
      #if HAS_FAN
        case TEMP_CASE_FAN: // Fan speed
          checkkey = FanSpeed;
          HMI_ValueStruct.Fan_speed = thermalManager.fan_speed[0];
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, VALUERANGE_X, MBASE(select_temp.now + MROWS - index_temp)+TEMP_SET_OFFSET, HMI_ValueStruct.Fan_speed);
          EncoderRate.enabled = true;
          HMI_ValueStruct.show_mode = -1;  //解决选中数字错行的问题 20230721
          break;
      #endif
      #if HAS_HOTEND
        case TEMP_CASE_PLA: { // PLA preheat setting
          checkkey = PLAPreheat;
          select_PLA.reset();
          HMI_ValueStruct.show_mode = -2;

          Clear_Main_Window();
          Draw_Mid_Status_Area(true);
          HMI_flag.Refresh_bottom_flag=false;//标志刷新底部参数
          if (HMI_flag.language < Language_Max) 
          {
            DWIN_ICON_Show(HMI_flag.language, LANGUAGE_PLASetup_Title, TITLE_X, TITLE_Y);
            #if ENABLED(DWIN_CREALITY_480_LCD)
              
            #elif ENABLED(DWIN_CREALITY_320_LCD)
              DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Back, 42, 26);      // 返回
              DWIN_ICON_Show(HMI_flag.language, LANGUAGE_PLA_NOZZLE, 42, 84 - font_offset);  // +JPN_OFFSET
              #if HAS_HEATED_BED
                DWIN_ICON_Show(HMI_flag.language, LANGUAGE_PLA_BED, 42, 120 - font_offset);
              #endif
              #if HAS_FAN
                DWIN_ICON_Show(HMI_flag.language, LANGUAGE_PLA_FAN, 42, 156 - font_offset);
              #endif
              #if ENABLED(EEPROM_SETTINGS)
                DWIN_ICON_Show(HMI_flag.language, LANGUAGE_PLASetupSave, 42, 192 - font_offset);
              #endif
            #endif
          }
          else {  }

          Draw_Back_First();

          uint8_t i = 0;
          Draw_Menu_Line(++i, ICON_SetEndTemp);
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(i)+TEMP_SET_OFFSET, ui.material_preset[0].hotend_temp);
          #if HAS_HEATED_BED
            Draw_Menu_Line(++i, ICON_SetBedTemp);
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(i)+TEMP_SET_OFFSET, ui.material_preset[0].bed_temp);
          #endif
          #if HAS_FAN
            Draw_Menu_Line(++i, ICON_FanSpeed);
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(i)+TEMP_SET_OFFSET, ui.material_preset[0].fan_speed);
          #endif
          #if ENABLED(EEPROM_SETTINGS)
            Draw_Menu_Line(++i, ICON_WriteEEPROM);
          #endif
        } break;

        case TEMP_CASE_ABS:
        { // ABS preheat setting
          checkkey = ABSPreheat;
          select_ABS.reset();
          HMI_ValueStruct.show_mode = -3;
          Clear_Main_Window();
          Draw_Mid_Status_Area(true);
          HMI_flag.Refresh_bottom_flag=false;//标志刷新底部参数
          if (HMI_flag.language < Language_Max)
          {
            // DWIN_Frame_TitleCopy(1, 142, 16, 223, 29);                                        // "ABS Settings"
            DWIN_ICON_Show(HMI_flag.language,LANGUAGE_ABSSetup_Title, TITLE_X, TITLE_Y);
            #if ENABLED(DWIN_CREALITY_480_LCD)
              DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Back, 60, 42);      // 返回
              DWIN_ICON_Show(HMI_flag.language, LANGUAGE_ABS_NOZZLE, 60, 102 - font_offset); // +JPN_OFFSET
              #if HAS_HEATED_BED
                DWIN_ICON_Show(HMI_flag.language, LANGUAGE_ABS_BED, 60, 155 - font_offset);
              #endif
              #if HAS_FAN
                DWIN_ICON_Show(HMI_flag.language, LANGUAGE_ABS_FAN, 60, 208 - font_offset);
              #endif
              #if ENABLED(EEPROM_SETTINGS)
                DWIN_ICON_Show(HMI_flag.language, LANGUAGE_ABSSetupSave, 60, 261 - font_offset);
              #endif
            #elif ENABLED(DWIN_CREALITY_320_LCD)
              DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Back, 42, 26);      // 返回
              DWIN_ICON_Show(HMI_flag.language, LANGUAGE_ABS_NOZZLE, 42, 84 - font_offset);  // +JPN_OFFSET
              #if HAS_HEATED_BED
                DWIN_ICON_Show(HMI_flag.language, LANGUAGE_ABS_BED, 42, 120 - font_offset);
              #endif
              #if HAS_FAN
                DWIN_ICON_Show(HMI_flag.language, LANGUAGE_ABS_FAN, 42, 156 - font_offset);
              #endif
              #if ENABLED(EEPROM_SETTINGS)
                DWIN_ICON_Show(HMI_flag.language, LANGUAGE_ABSSetupSave, 42, 192 - font_offset);
              #endif
            #endif
          }
          else {}
          Draw_Back_First();
          uint8_t i = 0;
          Draw_Menu_Line(++i, ICON_SetEndTemp);
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(i)+TEMP_SET_OFFSET, ui.material_preset[1].hotend_temp);
          #if HAS_HEATED_BED
            Draw_Menu_Line(++i, ICON_SetBedTemp);
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(i)+TEMP_SET_OFFSET, ui.material_preset[1].bed_temp);
          #endif
          #if HAS_FAN
            Draw_Menu_Line(++i, ICON_FanSpeed);
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, VALUERANGE_X, MBASE(i)+TEMP_SET_OFFSET, ui.material_preset[1].fan_speed);
          #endif
          #if ENABLED(EEPROM_SETTINGS)
            Draw_Menu_Line(++i, ICON_WriteEEPROM);
          #endif
        }
        break;
      case TEMP_CASE_HM_PID:      // 手动PID设置
        checkkey = HM_SET_PID;
        // HMI_ValueStruct.show_mode = -1;
        select_hm_set_pid.reset();
        Draw_HM_PID_Set();
        break;
      case TEMP_CASE_Auto_PID:    // 自动PID设置
        checkkey = AUTO_SET_PID;
        select_set_pid.reset();
        Draw_Auto_PID_Set();
        break;
      #endif // HAS_HOTEND
    }
  }
  DWIN_UpdateLCD();
}

void Draw_auto_nozzle_PID()
{
  Clear_Main_Window();
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_ICON_Not_Filter_Show(Background_ICON, Auto_Set_Nozzle_PID, 0, 185);
    Draw_Mid_Status_Area(true); //rock_20230529
    if (HMI_flag.language < Language_Max)
    {
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Auto_Set_Nozzle_PID_Title, TITLE_X, TITLE_Y); // 标题
      Draw_Back_First(1);
      DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Auto_PID_ING, 0, MBASE(1) + JPN_OFFSET);
      DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Auto_NOZZ_EX, 26, 174 + JPN_OFFSET);
    }
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_ICON_Not_Filter_Show(Background_ICON, Auto_Set_Nozzle_PID, ICON_AUTO_X, ICON_AUTO_Y);
    Draw_Mid_Status_Area(true); //rock_20230529
    if (HMI_flag.language < Language_Max)
    {
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Auto_Set_Nozzle_PID_Title, TITLE_X, TITLE_Y); // 标题
      Draw_Back_First(1);
      DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Auto_PID_ING, WORD_AUTO_X, WORD_AUTO_Y);
      DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Auto_NOZZ_EX, WORD_TEMP_X, WORD_TEMP_Y);  //喷嘴温度 带选中色
    }
  #endif
}

void Draw_auto_bed_PID()
{
  Clear_Main_Window();
  #if ENABLED(DWIN_CREALITY_480_LCD)   
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_ICON_Not_Filter_Show(Background_ICON, Auto_Set_Bed_PID, ICON_AUTO_X, ICON_AUTO_Y);
    Draw_Mid_Status_Area(true); //rock_20230529
    if (HMI_flag.language < Language_Max)
    {
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Auto_Set_Bed_PID_Title, TITLE_X, TITLE_Y); // 标题
      Draw_Back_First(1);
      DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Auto_PID_ING,WORD_AUTO_X, WORD_AUTO_Y);
      DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Auto_BED_EX, WORD_TEMP_X, WORD_TEMP_Y);  //热床温度 带选中色
    }
  #endif
}

void Draw_HM_PID_Set()
{
   Clear_Main_Window();
   Draw_Mid_Status_Area(true); //rock_20230529
  if (HMI_flag.language < Language_Max)
  {
    DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_Set_PID_Manually, TITLE_X, TITLE_Y); //运动
    Draw_Back_First();
    LOOP_L_N(i, 5) Draw_Menu_Line(i + 1, ICON_HM_PID_NOZZ_P + i);
    auto say_max_speed = [](const uint16_t row) 
    {
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_MaxSpeed, 70, row);
    };
    // EEPROM里面读出来的值到显示到屏幕上
    HMI_ValueStruct.HM_PID_Value[1]=PID_PARAM(Kp, 0);
    HMI_ValueStruct.HM_PID_Value[2]=unscalePID_i(PID_PARAM(Ki, 0));
    HMI_ValueStruct.HM_PID_Value[3]=unscalePID_d(PID_PARAM(Kd, 0));
    HMI_ValueStruct.HM_PID_Value[4]=thermalManager.temp_bed.pid.Kp;
    HMI_ValueStruct.HM_PID_Value[5]=unscalePID_i(thermalManager.temp_bed.pid.Ki);
    HMI_ValueStruct.HM_PID_Value[6]=unscalePID_d(thermalManager.temp_bed.pid.Kd);
    for(uint8_t i = 1;i < 6;i ++)
    {
      #if ENABLED(DWIN_CREALITY_480_LCD)
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Nozz_P + i - 1, 60 - 5, MBASE(i) + JPN_OFFSET);
        DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 2, 200, MBASE(i), HMI_ValueStruct.HM_PID_Value[i] * 100);
      #elif ENABLED(DWIN_CREALITY_320_LCD)
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Nozz_P + i - 1, 60 - 5, MBASE(i) + JPN_OFFSET);
        DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 2, 172, MBASE(i), HMI_ValueStruct.HM_PID_Value[i] * 100);
      #endif
    }
  }
}

void Draw_Auto_PID_Set()
{
  Clear_Main_Window();
  Draw_Mid_Status_Area(true); //rock_20230529
  if (HMI_flag.language < Language_Max)
  {
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Set_Auto_PID, TITLE_X, TITLE_Y); //运动 
    Draw_Back_First();
    LOOP_L_N(i, 2) Draw_Menu_Line(i + 1, ICON_FLAG_MAX);        //不画图标
    // 单独画一次图标
    DWIN_ICON_Not_Filter_Show(ICON, ICON_Auto_PID_Bed, 26, MBASE(1) - 3);
    DWIN_ICON_Not_Filter_Show(ICON, ICON_Auto_PID_Nozzle, 26, MBASE(2) - 3);

    auto say_max_speed = [](const uint16_t row)
    {
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_MaxSpeed, 70, row);
    };
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Auto_Set_Bed_PID, 60-5, MBASE(1)+JPN_OFFSET);
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Auto_Set_Nozzle_PID, 60-5, MBASE(2)+JPN_OFFSET);
  }
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 210 + PID_VALUE_OFFSET, MBASE(1), HMI_ValueStruct.Auto_PID_Value[1]);
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black,3, 210 + PID_VALUE_OFFSET, MBASE(2), HMI_ValueStruct.Auto_PID_Value[2]);
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 192 + PID_VALUE_OFFSET, MBASE(1), HMI_ValueStruct.Auto_PID_Value[1]);
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 192 + PID_VALUE_OFFSET, MBASE(2), HMI_ValueStruct.Auto_PID_Value[2]);
  #endif
  Erase_Menu_Cursor(0);
  Draw_Menu_Cursor(select_set_pid.now);
}

void Draw_Max_Speed_Menu()
{
  Clear_Main_Window();
  HMI_flag.Refresh_bottom_flag=true;//标志刷新底部参数
  if (HMI_flag.language < Language_Max)
  {
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_mspeed_title, TITLE_X, TITLE_Y); // 运动
    auto say_max_speed = [](const uint16_t row)
    {
      DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_MaxSpeed, 70, row);
    };
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_MAX_SPEEDX, 42, MBASE(1) + JPN_OFFSET);
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_MAX_SPEEDY, 42, MBASE(2) + JPN_OFFSET);
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_MAX_SPEEDZ, 42, MBASE(3) + JPN_OFFSET);   // "Max speed"
    #if HAS_HOTEND
      DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_MAX_SPEEDE, 42, MBASE(4) + JPN_OFFSET);  // "Max speed"
    #endif
  }
  Draw_Back_First();
  LOOP_L_N(i, 3 + ENABLED(HAS_HOTEND)) Draw_Menu_Line(i + 1, ICON_MaxSpeedX + i);
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(1) + 3, planner.settings.max_feedrate_mm_s[X_AXIS]);
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(2) + 3, planner.settings.max_feedrate_mm_s[Y_AXIS]);
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(3) + 3, planner.settings.max_feedrate_mm_s[Z_AXIS]);
    #if HAS_HOTEND
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(4) + 3, planner.settings.max_feedrate_mm_s[E_AXIS]);
    #endif
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 192, MBASE(1) + 3, planner.settings.max_feedrate_mm_s[X_AXIS]);
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 192, MBASE(2) + 3, planner.settings.max_feedrate_mm_s[Y_AXIS]);
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 192, MBASE(3) + 3, planner.settings.max_feedrate_mm_s[Z_AXIS]);
    #if HAS_HOTEND
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 192, MBASE(4) + 3, planner.settings.max_feedrate_mm_s[E_AXIS]);
    #endif
  #endif
}

void Draw_Max_Accel_Menu()
{
  Clear_Main_Window();
  HMI_flag.Refresh_bottom_flag=true;//标志刷新底部参数
  if (HMI_flag.language<Language_Max)
  {
    // DWIN_Frame_TitleCopy(1, 1, 16, 28, 28); // "Acceleration"
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_maccel_title, TITLE_X, TITLE_Y);
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_MAX_ACCX, 42, MBASE(1) + JPN_OFFSET);
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_MAX_ACCY, 42, MBASE(2) + JPN_OFFSET);
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_MAX_ACCZ, 42, MBASE(3) + JPN_OFFSET);
    #if HAS_HOTEND
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_MAX_ACCE, 42, MBASE(4) + JPN_OFFSET);
    #endif
  }
  else
  {
    #ifdef USE_STRING_HEADINGS
      Draw_Title(GET_TEXT_F(MSG_ACCELERATION));
    #else
      DWIN_Frame_TitleCopy(1, 144, 16, 189, 26);          // "Acceleration"
    #endif
    #ifdef USE_STRING_TITLES
      DWIN_Draw_Label(MBASE(1), F("Max Accel X"));
      DWIN_Draw_Label(MBASE(2), F("Max Accel Y"));
      DWIN_Draw_Label(MBASE(3), F("Max Accel Z"));
      #if HAS_HOTEND
        DWIN_Draw_Label(MBASE(4), F("Max Accel E"));
      #endif
    #else
      draw_max_accel_en(MBASE(1)); say_x(110, MBASE(1));  // "Max Acceleration X"
      draw_max_accel_en(MBASE(2)); say_y(110, MBASE(2));  // "Max Acceleration Y"
      draw_max_accel_en(MBASE(3)); say_z(110, MBASE(3));  // "Max Acceleration Z"
      #if HAS_HOTEND
        draw_max_accel_en(MBASE(4)); say_e(110, MBASE(4)); // "Max Acceleration E"
      #endif
    #endif
  }
  Draw_Back_First();
  LOOP_L_N(i, 3 + ENABLED(HAS_HOTEND)) Draw_Menu_Line(i + 1, ICON_MaxAccX + i);
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(1)+3, planner.settings.max_acceleration_mm_per_s2[X_AXIS]);
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(2)+3, planner.settings.max_acceleration_mm_per_s2[Y_AXIS]);
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(3)+3, planner.settings.max_acceleration_mm_per_s2[Z_AXIS]);
    #if HAS_HOTEND
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 210, MBASE(4)+3, planner.settings.max_acceleration_mm_per_s2[E_AXIS]); //rock_20211009
    #endif
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 192, MBASE(1)+3, planner.settings.max_acceleration_mm_per_s2[X_AXIS]);
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 192, MBASE(2)+3, planner.settings.max_acceleration_mm_per_s2[Y_AXIS]);
    DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 192, MBASE(3)+3, planner.settings.max_acceleration_mm_per_s2[Z_AXIS]);
    #if HAS_HOTEND
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 4, 192, MBASE(4)+3, planner.settings.max_acceleration_mm_per_s2[E_AXIS]); //rock_20211009
    #endif
  #endif
}

#if HAS_CLASSIC_JERK
  void Draw_Max_Jerk_Menu()
  {
    Clear_Main_Window();
    HMI_flag.Refresh_bottom_flag=true;//标志刷新底部参数
    if (HMI_flag.language<Language_Max) {
      // DWIN_Frame_TitleCopy(1, 1, 16, 28, 28); // "Jerk"
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_mjerk_title, TITLE_X, TITLE_Y);//运动  jerk
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_MAX_CORNERX, 42, MBASE(1) + JPN_OFFSET);
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_MAX_CORNERY, 42, MBASE(2) + JPN_OFFSET);
      DWIN_ICON_Show(HMI_flag.language, LANGUAGE_MAX_CORNERZ, 42, MBASE(3) + JPN_OFFSET);

      #if HAS_HOTEND
        DWIN_ICON_Show(HMI_flag.language, LANGUAGE_MAX_CORNERE, 42, MBASE(4) + JPN_OFFSET);
      #endif
    }
    else {
      #ifdef USE_STRING_HEADINGS
        Draw_Title(GET_TEXT_F(MSG_JERK));
      #else
        DWIN_Frame_TitleCopy(1, 144, 16, 189, 26); // "Jerk"
      #endif
      #ifdef USE_STRING_TITLES
        DWIN_Draw_Label(MBASE(1), F("Max Jerk X"));
        DWIN_Draw_Label(MBASE(2), F("Max Jerk Y"));
        DWIN_Draw_Label(MBASE(3), F("Max Jerk Z"));
        #if HAS_HOTEND
          DWIN_Draw_Label(MBASE(4), F("Max Jerk E"));
        #endif
      #else
        draw_max_en(MBASE(1));          // "Max"
        draw_jerk_en(MBASE(1));         // "Jerk"
        DWIN_Frame_AreaCopy(1, 184, 119, 224, 132, LBLX + 77,MBASE(1)); // "Speed"
        //draw_speed_en(80, MBASE(1));    // "Speed"
        say_x(115+6, MBASE(1));           // "X"

        draw_max_en(MBASE(2));          // "Max"
        draw_jerk_en(MBASE(2));         // "Jerk"
        //draw_speed_en(80, MBASE(2));    // "Speed"
        DWIN_Frame_AreaCopy(1, 184, 119, 224, 132, LBLX + 77, MBASE(2)); // "Speed"
        say_y(115+6, MBASE(2));           // "Y"

        draw_max_en(MBASE(3));          // "Max"
        draw_jerk_en(MBASE(3));         // "Jerk"
        //draw_speed_en(80, MBASE(3));    // "Speed"
        DWIN_Frame_AreaCopy(1, 184, 119, 224, 132, LBLX + 77, MBASE(3)); // "Speed"
        say_z(115+6, MBASE(3));           // "Z"

        #if HAS_HOTEND
          draw_max_en(MBASE(4));        // "Max"
          draw_jerk_en(MBASE(4));       // "Jerk"
          //draw_speed_en(72, MBASE(4));  // "Speed"
          DWIN_Frame_AreaCopy(1, 184, 119, 224, 132, LBLX + 77, MBASE(4)); // "Speed"
          say_e(115+6, MBASE(4));         // "E"
        #endif
      #endif
    }

    Draw_Back_First();
    LOOP_L_N(i, 3 + ENABLED(HAS_HOTEND)) Draw_Menu_Line(i + 1, ICON_MaxSpeedJerkX + i);
    #if ENABLED(DWIN_CREALITY_480_LCD)
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 210, MBASE(1)+3, planner.max_jerk[X_AXIS] * MINUNITMULT+0.0002);
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 210, MBASE(2)+3, planner.max_jerk[Y_AXIS] * MINUNITMULT+0.0002);
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 210, MBASE(3)+3, planner.max_jerk[Z_AXIS] * MINUNITMULT+0.0002);
      #if HAS_HOTEND
        DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 210, MBASE(4)+3, planner.max_jerk[E_AXIS] * MINUNITMULT+0.0002);
      #endif
    #elif ENABLED(DWIN_CREALITY_320_LCD)
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 192, MBASE(1)+3, planner.max_jerk[X_AXIS] * MINUNITMULT+0.0002);
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 192, MBASE(2)+3, planner.max_jerk[Y_AXIS] * MINUNITMULT+0.0002);
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 192, MBASE(3)+3, planner.max_jerk[Z_AXIS] * MINUNITMULT+0.0002);
      #if HAS_HOTEND
        DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 192, MBASE(4)+3, planner.max_jerk[E_AXIS] * MINUNITMULT+0.0002);
      #endif
    #endif
  }
#endif

void Draw_Steps_Menu() 
{
  Clear_Main_Window();
  HMI_flag.Refresh_bottom_flag=true;//标志刷新底部参数
  if (HMI_flag.language < Language_Max)
  {
    // DWIN_Frame_TitleCopy(1, 1, 16, 28, 28); // "Steps per mm"
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_ratio_title, TITLE_X, TITLE_Y);
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Step_Per_X, 42, MBASE(1) + JPN_OFFSET);
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Step_Per_Y, 42, MBASE(2) + JPN_OFFSET);
    DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Step_Per_Z, 42, MBASE(3) + JPN_OFFSET);
    #if HAS_HOTEND
     DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Step_Per_E, 42, MBASE(4) + JPN_OFFSET);
    #endif
  }
  else
  {
    #ifdef USE_STRING_HEADINGS
      Draw_Title(GET_TEXT_F(MSG_STEPS_PER_MM));
    #else
      DWIN_Frame_TitleCopy(1, 144, 16, 189, 26); // "Steps per mm"
    #endif
    #ifdef USE_STRING_TITLES
      DWIN_Draw_Label(MBASE(1), F("Steps/mm X"));
      DWIN_Draw_Label(MBASE(2), F("Steps/mm Y"));
      DWIN_Draw_Label(MBASE(3), F("Steps/mm Z"));
      #if HAS_HOTEND
        DWIN_Draw_Label(MBASE(4), F("Steps/mm E"));
      #endif
    #else
      draw_steps_per_mm(MBASE(1)); say_x(103 + 16, MBASE(1)); // "Steps-per-mm X"  rock_20210907
      draw_steps_per_mm(MBASE(2)); say_y(103 + 16, MBASE(2)); // "Y"
      draw_steps_per_mm(MBASE(3)); say_z(103 + 16, MBASE(3)); // "Z"
      #if HAS_HOTEND
        draw_steps_per_mm(MBASE(4)); say_e(103 + 16, MBASE(4)); // "E"
      #endif
    #endif
  }

  Draw_Back_First();
  LOOP_L_N(i, 3 + ENABLED(HAS_HOTEND)) Draw_Menu_Line(i + 1, ICON_StepX + i);
  #if ENABLED(DWIN_CREALITY_480_LCD)
    DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 210, MBASE(1)+3, planner.settings.axis_steps_per_mm[X_AXIS] * MINUNITMULT+0.0002);
    DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 210, MBASE(2)+3, planner.settings.axis_steps_per_mm[Y_AXIS] * MINUNITMULT+0.0002);
    DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 210, MBASE(3)+3, planner.settings.axis_steps_per_mm[Z_AXIS] * MINUNITMULT+0.0002);
    #if HAS_HOTEND
      //rock_20210907  迪文屏显示浮点数424.9有异常所以+0.0002
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3,UNITFDIGITS, 210, MBASE(4)+3, (planner.settings.axis_steps_per_mm[E_AXIS] * MINUNITMULT+0.0002));
    #endif
  #elif ENABLED(DWIN_CREALITY_320_LCD)
    DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 192, MBASE(1) + 3, planner.settings.axis_steps_per_mm[X_AXIS] * MINUNITMULT+0.0002);
    DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 192, MBASE(2) + 3, planner.settings.axis_steps_per_mm[Y_AXIS] * MINUNITMULT+0.0002);
    DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, UNITFDIGITS, 192, MBASE(3) + 3, planner.settings.axis_steps_per_mm[Z_AXIS] * MINUNITMULT+0.0002);
    #if HAS_HOTEND
      //rock_20210907  迪文屏显示浮点数424.9有异常所以+0.0002
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3,UNITFDIGITS, 192, MBASE(4) + 3, (planner.settings.axis_steps_per_mm[E_AXIS] * MINUNITMULT+0.0002));
    #endif
  #endif
}

/* Motion */
void HMI_Motion()
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW)
  {
    if (select_motion.inc(1 + MOTION_CASE_TOTAL)) Move_Highlight(1, select_motion.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW)
  {
    if (select_motion.dec()) Move_Highlight(-1, select_motion.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER)
  {
    switch (select_motion.now)
    {
      case 0: // Back
        checkkey = Control;
        select_control.set(CONTROL_CASE_MOVE);
        index_control = MROWS;
        Draw_Control_Menu();
        break;
      case MOTION_CASE_RATE:   // Max speed
        checkkey = MaxSpeed;
        select_speed.reset();
        Draw_Max_Speed_Menu();
        break;
      case MOTION_CASE_ACCEL:  // Max acceleration
        checkkey = MaxAcceleration;
        select_acc.reset();
        Draw_Max_Accel_Menu();
        break;
      #if HAS_CLASSIC_JERK
        case MOTION_CASE_JERK: // Max jerk
          checkkey = MaxJerk;
          select_jerk.reset();
          Draw_Max_Jerk_Menu();
         break;
      #endif
      case MOTION_CASE_STEPS:  // Steps per mm
        checkkey = Step;
        select_step.reset();
        Draw_Steps_Menu();
        break;
      default: break;
    }
  }
  DWIN_UpdateLCD();
}

/* Advanced Settings */
void HMI_AdvSet()
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW)
  {
    if (select_advset.inc(1 + ADVSET_CASE_TOTAL))
    {
      if (select_advset.now > MROWS && select_advset.now > index_advset)
      {
        index_advset = select_advset.now;
        // Scroll up and draw a blank bottom line
        Scroll_Menu(DWIN_SCROLL_UP);
        //switch (index_advset)
        //{
        //  Redraw last menu items
        //  default: break;
        //}
      }
      else
      {
        Move_Highlight(1, select_advset.now + MROWS - index_advset);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW)
  {
    if (select_advset.dec())
    {
      if (select_advset.now < index_advset - MROWS)
      {
        index_advset--;
        Scroll_Menu(DWIN_SCROLL_DOWN);

        //switch (index_advset)
        //{ 
        //  Redraw first menu items
        //  default: break;
        //}
      }
      else
      {
        Move_Highlight(-1, select_advset.now + MROWS - index_advset);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER)
  {
    switch (select_advset.now) {
      case 0: // Back
      /*  //rock_20210727
        checkkey = Control;
        select_control.set(CONTROL_CASE_ADVSET);
        index_control = CONTROL_CASE_ADVSET;
        Draw_Control_Menu();
        */
        break;

      #if HAS_HOME_OFFSET
        case ADVSET_CASE_HOMEOFF:   // Home Offsets
          checkkey = HomeOff;
          select_item.reset();
          HMI_ValueStruct.Home_OffX_scaled = home_offset[X_AXIS] * 10;
          HMI_ValueStruct.Home_OffY_scaled = home_offset[Y_AXIS] * 10;
          HMI_ValueStruct.Home_OffZ_scaled = home_offset[Z_AXIS] * 10;
          Draw_HomeOff_Menu();
          break;
      #endif

      #if HAS_ONESTEP_LEVELING
        case ADVSET_CASE_PROBEOFF:   // Probe Offsets
          checkkey = ProbeOff;
          select_item.reset();
          HMI_ValueStruct.Probe_OffX_scaled = probe.offset.x * 10;
          HMI_ValueStruct.Probe_OffY_scaled = probe.offset.y * 10;
          Draw_ProbeOff_Menu();
          break;
      #endif

      #if HAS_HOTEND
        case ADVSET_CASE_HEPID:   // Nozzle PID Autotune
          thermalManager.setTargetHotend(ui.material_preset[0].hotend_temp, 0);
          thermalManager.PID_autotune(ui.material_preset[0].hotend_temp, H_E0, 10, true);
          break;
      #endif

      #if HAS_HEATED_BED
        case ADVSET_CASE_BEDPID:  // Bed PID Autotune
          thermalManager.setTargetBed(ui.material_preset[0].bed_temp);
          thermalManager.PID_autotune(ui.material_preset[0].bed_temp, H_BED, 10, true);
          break;
      #endif

      #if ENABLED(POWER_LOSS_RECOVERY)
        case ADVSET_CASE_PWRLOSSR:  // Power-loss recovery
          recovery.enable(!recovery.enabled);
          Draw_Chkb_Line(ADVSET_CASE_PWRLOSSR + MROWS - index_advset, recovery.enabled);
          break;
      #endif
      default: break;
    }
  }
  DWIN_UpdateLCD();
}

#if HAS_HOME_OFFSET

  /* Home Offset */
  void HMI_HomeOff()
  {
    ENCODER_DiffState encoder_diffState = get_encoder_state();
    if (encoder_diffState == ENCODER_DIFF_NO) return;

    // Avoid flicker by updating only the previous menu
    if (encoder_diffState == ENCODER_DIFF_CW)
    {
      if (select_item.inc(1 + 3)) Move_Highlight(1, select_item.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_CCW)
    {
      if (select_item.dec()) Move_Highlight(-1, select_item.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_ENTER)
    {
      switch (select_item.now) {
        case 0: // Back
          checkkey = AdvSet;
          select_advset.set(ADVSET_CASE_HOMEOFF);
          //Draw_AdvSet_Menu(); //rock_20210724
          break;
        case 1: // Home Offset X
          checkkey = HomeOffX;
          DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, 1, VALUERANGE_X, MBASE(1), HMI_ValueStruct.Home_OffX_scaled);
          EncoderRate.enabled = true;
          break;
        case 2: // Home Offset Y
          checkkey = HomeOffY;
          DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, 1, VALUERANGE_X, MBASE(2), HMI_ValueStruct.Home_OffY_scaled);
          EncoderRate.enabled = true;
          break;
        case 3: // Home Offset Z
          checkkey = HomeOffZ;
          DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, 1, VALUERANGE_X, MBASE(3), HMI_ValueStruct.Home_OffZ_scaled);
          EncoderRate.enabled = true;
          break;
        default: break;
      }
    }
    DWIN_UpdateLCD();
  }

  void HMI_HomeOffN(const AxisEnum axis, float &posScaled, const_float_t lo, const_float_t hi)
  {
    ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
    if (encoder_diffState != ENCODER_DIFF_NO)
    {
      if (Apply_Encoder(encoder_diffState, posScaled))
      {
        checkkey = HomeOff;
        EncoderRate.enabled = false;
        set_home_offset(axis, posScaled / 10);
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, 1, VALUERANGE_X, MBASE(select_item.now), posScaled);
        return;
      }
      LIMIT(posScaled, lo, hi);
      DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, UNITFDIGITS, VALUERANGE_X, MBASE(select_item.now), posScaled);
    }
  }

  void HMI_HomeOffX() { HMI_HomeOffN(X_AXIS, HMI_ValueStruct.Home_OffX_scaled, -500, 500); }
  void HMI_HomeOffY() { HMI_HomeOffN(Y_AXIS, HMI_ValueStruct.Home_OffY_scaled, -500, 500); }
  void HMI_HomeOffZ() { HMI_HomeOffN(Z_AXIS, HMI_ValueStruct.Home_OffZ_scaled,  -20,  20); }

#endif // HAS_HOME_OFFSET

#if HAS_ONESTEP_LEVELING
  /*Probe Offset */
  void HMI_ProbeOff()
  {
    ENCODER_DiffState encoder_diffState = get_encoder_state();
    if (encoder_diffState == ENCODER_DIFF_NO) return;

    // Avoid flicker by updating only the previous menu
    if (encoder_diffState == ENCODER_DIFF_CW)
    {
      if (select_item.inc(1 + 2)) Move_Highlight(1, select_item.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_CCW)
    {
      if (select_item.dec()) Move_Highlight(-1, select_item.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_ENTER)
    {
      switch (select_item.now)
      {
        case 0: // Back
          checkkey = AdvSet;
          select_advset.set(ADVSET_CASE_PROBEOFF);
          //Draw_AdvSet_Menu();
          break;
        case 1: // Probe Offset X
          checkkey = ProbeOffX;
          DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, 1, VALUERANGE_X, MBASE(1), HMI_ValueStruct.Probe_OffX_scaled);
          EncoderRate.enabled = true;
          break;
        case 2: // Probe Offset X
          checkkey = ProbeOffY;
          DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, 1, VALUERANGE_X, MBASE(2), HMI_ValueStruct.Probe_OffY_scaled);
          EncoderRate.enabled = true;
          break;
      }
    }
    DWIN_UpdateLCD();
  }

  void HMI_ProbeOffN(float &posScaled, float &offset_ref)
  {
    ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
    if (encoder_diffState != ENCODER_DIFF_NO)
    {
      if (Apply_Encoder(encoder_diffState, posScaled))
      {
        checkkey = ProbeOff;
        EncoderRate.enabled = false;
        offset_ref = posScaled / 10;
        DWIN_Draw_Signed_Float(font8x16, Color_Bg_Black, 3, 1, VALUERANGE_X, MBASE(select_item.now), posScaled);
        return;
      }
      LIMIT(posScaled, -500, 500);
      DWIN_Draw_Signed_Float(font8x16, Select_Color, 3, UNITFDIGITS, VALUERANGE_X, MBASE(select_item.now), posScaled);
    }
  }

  void HMI_ProbeOffX() { HMI_ProbeOffN(HMI_ValueStruct.Probe_OffX_scaled, probe.offset.x); }
  void HMI_ProbeOffY() { HMI_ProbeOffN(HMI_ValueStruct.Probe_OffY_scaled, probe.offset.y); }

#endif // HAS_ONESTEP_LEVELING

/* Info */
void HMI_Info()
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  if (encoder_diffState == ENCODER_DIFF_ENTER) {
    #if HAS_ONESTEP_LEVELING
      checkkey = Control;
      select_control.set(CONTROL_CASE_INFO);
      Draw_Control_Menu();
    #else
      select_page.set(3);
      Goto_MainMenu();
    #endif
    HMI_flag.Refresh_bottom_flag=false;//标志不刷新底部参数
  }
  DWIN_UpdateLCD();
}

/* Tune */
void HMI_Tune()
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW) {
    if (select_tune.inc(1 + TUNE_CASE_TOTAL)) {
      if (select_tune.now > MROWS && select_tune.now > index_tune) {
        index_tune = select_tune.now;
        Scroll_Menu(DWIN_SCROLL_UP);
      }
      else {
        Move_Highlight(1, select_tune.now + MROWS - index_tune);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW) {
    if (select_tune.dec()) {
      if (select_tune.now < index_tune - MROWS) {
        index_tune--;
        Scroll_Menu(DWIN_SCROLL_DOWN);
        if (index_tune == MROWS) Draw_Back_First();
      }
      else 
      {
        Move_Highlight(-1, select_tune.now + MROWS - index_tune);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) {
    switch (select_tune.now) {
      case 0: 
      { // Back
        select_print.set(0);
        Goto_PrintProcess();
      }
      break;
      case TUNE_CASE_SPEED: // Print speed
        checkkey = PrintSpeed;
        HMI_ValueStruct.print_speed = feedrate_percentage;
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, VALUERANGE_X, MBASE(TUNE_CASE_SPEED + MROWS - index_tune)+PRINT_SET_OFFSET, HMI_ValueStruct.print_speed);
        EncoderRate.enabled = true;
        break;
      #if HAS_HOTEND
        case TUNE_CASE_TEMP: // Nozzle temp
          checkkey = ETemp;
          HMI_ValueStruct.E_Temp = thermalManager.degTargetHotend(0);
          LIMIT(HMI_ValueStruct.E_Temp, HEATER_0_MINTEMP, thermalManager.hotend_max_target(0));
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, VALUERANGE_X, MBASE(TUNE_CASE_TEMP + MROWS - index_tune)+PRINT_SET_OFFSET, HMI_ValueStruct.E_Temp);
          EncoderRate.enabled = true;
          break;
      #endif
      #if HAS_HEATED_BED
        case TUNE_CASE_BED: // Bed temp
          checkkey = BedTemp;
          HMI_ValueStruct.Bed_Temp = thermalManager.degTargetBed();
          LIMIT(HMI_ValueStruct.Bed_Temp, BED_MINTEMP, BED_MAX_TARGET);
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, VALUERANGE_X, MBASE(TUNE_CASE_BED + MROWS - index_tune)+PRINT_SET_OFFSET, HMI_ValueStruct.Bed_Temp);
          EncoderRate.enabled = true;
          break;
      #endif
      #if HAS_FAN
        case TUNE_CASE_FAN: // Fan speed
          checkkey = FanSpeed;
          HMI_ValueStruct.Fan_speed = thermalManager.fan_speed[0];
          DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, VALUERANGE_X, MBASE(TUNE_CASE_FAN + MROWS - index_tune)+PRINT_SET_OFFSET, HMI_ValueStruct.Fan_speed);
          EncoderRate.enabled = true;
          break;
      #endif
      #if HAS_ZOFFSET_ITEM
        case TUNE_CASE_ZOFF: // Z-offset
          #if EITHER(HAS_BED_PROBE, BABYSTEPPING)
            checkkey = Homeoffset;
            HMI_ValueStruct.offset_value = BABY_Z_VAR * 100;
            DWIN_Draw_Signed_Float(font8x16, Select_Color, 2, 2, VALUERANGE_X - 14, MBASE(TUNE_CASE_ZOFF + MROWS - index_tune), HMI_ValueStruct.offset_value);
            EncoderRate.enabled = true;
          #else
            // Apply workspace offset, making the current position 0,0,0
            queue.inject_P(PSTR("G92 X0 Y0 Z0"));
            HMI_AudioFeedback();
          #endif
        break;
      #endif
      default: break;
    }
  }
  DWIN_UpdateLCD();
}

#if HAS_PREHEAT

  /* PLA Preheat */
  void HMI_PLAPreheatSetting() {
    ENCODER_DiffState encoder_diffState = get_encoder_state();
    if (encoder_diffState == ENCODER_DIFF_NO) return;

    // Avoid flicker by updating only the previous menu
    if (encoder_diffState == ENCODER_DIFF_CW) {
      if (select_PLA.inc(1 + PREHEAT_CASE_TOTAL)) Move_Highlight(1, select_PLA.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_CCW) {
      if (select_PLA.dec()) Move_Highlight(-1, select_PLA.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_ENTER) {
      switch (select_PLA.now) {
        case 0: // Back
          checkkey = TemperatureID;
          select_temp.set(TEMP_CASE_PLA);
          index_temp = MROWS;
          HMI_ValueStruct.show_mode = -1;
          Draw_Temperature_Menu();
          break;
        #if HAS_HOTEND
          case PREHEAT_CASE_TEMP: // Nozzle temperature
            checkkey = ETemp;
            HMI_ValueStruct.E_Temp = ui.material_preset[0].hotend_temp;
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, VALUERANGE_X, MBASE(PREHEAT_CASE_TEMP)+TEMP_SET_OFFSET, ui.material_preset[0].hotend_temp);
            EncoderRate.enabled = true;
            break;
        #endif
        #if HAS_HEATED_BED
          case PREHEAT_CASE_BED: // Bed temperature
            checkkey = BedTemp;
            HMI_ValueStruct.Bed_Temp = ui.material_preset[0].bed_temp;
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, VALUERANGE_X, MBASE(PREHEAT_CASE_BED)+TEMP_SET_OFFSET, ui.material_preset[0].bed_temp);
            EncoderRate.enabled = true;
            break;
        #endif
        #if HAS_FAN
          case PREHEAT_CASE_FAN: // Fan speed
            checkkey = FanSpeed;
            HMI_ValueStruct.Fan_speed = ui.material_preset[0].fan_speed;
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, VALUERANGE_X, MBASE(PREHEAT_CASE_FAN)+TEMP_SET_OFFSET, ui.material_preset[0].fan_speed);
            EncoderRate.enabled = true;
            break;
        #endif
        #if ENABLED(EEPROM_SETTINGS)
          case 4: { // Save PLA configuration
            const bool success = settings.save();
            HMI_AudioFeedback(success);
          } break;
        #endif
        default: break;
      }
    }
    DWIN_UpdateLCD();
  }

  /* ABS Preheat */
  void HMI_ABSPreheatSetting() {
    ENCODER_DiffState encoder_diffState = get_encoder_state();
    if (encoder_diffState == ENCODER_DIFF_NO) return;

    // Avoid flicker by updating only the previous menu
    if (encoder_diffState == ENCODER_DIFF_CW) {
      if (select_ABS.inc(1 + PREHEAT_CASE_TOTAL)) Move_Highlight(1, select_ABS.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_CCW) {
      if (select_ABS.dec()) Move_Highlight(-1, select_ABS.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_ENTER) {
      switch (select_ABS.now) {
        case 0: // Back
          checkkey = TemperatureID;
          select_temp.set(TEMP_CASE_ABS);
          index_temp = MROWS;
          HMI_ValueStruct.show_mode = -1;
          Draw_Temperature_Menu();
          break;
        #if HAS_HOTEND
          case PREHEAT_CASE_TEMP: // Set nozzle temperature
            checkkey = ETemp;
            HMI_ValueStruct.E_Temp = ui.material_preset[1].hotend_temp;
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, VALUERANGE_X, MBASE(PREHEAT_CASE_TEMP)+TEMP_SET_OFFSET, ui.material_preset[1].hotend_temp);
            EncoderRate.enabled = true;
            break;
        #endif
        #if HAS_HEATED_BED
          case PREHEAT_CASE_BED: // Set bed temperature
            checkkey = BedTemp;
            HMI_ValueStruct.Bed_Temp = ui.material_preset[1].bed_temp;
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, VALUERANGE_X, MBASE(PREHEAT_CASE_BED)+TEMP_SET_OFFSET, ui.material_preset[1].bed_temp);
            EncoderRate.enabled = true;
            break;
        #endif
        #if HAS_FAN
          case PREHEAT_CASE_FAN: // Set fan speed
            checkkey = FanSpeed;
            HMI_ValueStruct.Fan_speed = ui.material_preset[1].fan_speed;
            DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, VALUERANGE_X, MBASE(PREHEAT_CASE_FAN)+TEMP_SET_OFFSET, ui.material_preset[1].fan_speed);
            EncoderRate.enabled = true;
            break;
        #endif
        #if ENABLED(EEPROM_SETTINGS)
          case PREHEAT_CASE_SAVE: { // Save ABS configuration
            const bool success = settings.save();
            HMI_AudioFeedback(success);
          } break;
        #endif
        default: break;
      }
    }
    DWIN_UpdateLCD();
  }

#endif

/* Max Speed */
void HMI_MaxSpeed()
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW)
  {
    if (select_speed.inc(1 + 3 + ENABLED(HAS_HOTEND))) Move_Highlight(1, select_speed.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW)
  {
    if (select_speed.dec()) Move_Highlight(-1, select_speed.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER)
  {
    if (WITHIN(select_speed.now, 1, 4))
    {
      checkkey = MaxSpeed_value;
      HMI_flag.feedspeed_axis = AxisEnum(select_speed.now - 1);
      HMI_ValueStruct.Max_Feedspeed = planner.settings.max_feedrate_mm_s[HMI_flag.feedspeed_axis];
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 4, VALUERANGE_X, MBASE(select_speed.now) + 3, HMI_ValueStruct.Max_Feedspeed);
      EncoderRate.enabled = true;
    }
    else
    {
      // Back
      checkkey = Motion;
      select_motion.now = MOTION_CASE_RATE;
      Draw_Motion_Menu();
    }
  }
  DWIN_UpdateLCD();
}

/* Max Acceleration */
void HMI_MaxAcceleration()
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW)
  {
    if (select_acc.inc(1 + 3 + ENABLED(HAS_HOTEND))) Move_Highlight(1, select_acc.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW)
  {
    if (select_acc.dec()) Move_Highlight(-1, select_acc.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER)
  {
    if (WITHIN(select_acc.now, 1, 4)) 
    {
      checkkey = MaxAcceleration_value;
      HMI_flag.acc_axis = AxisEnum(select_acc.now - 1);
      HMI_ValueStruct.Max_Acceleration = planner.settings.max_acceleration_mm_per_s2[HMI_flag.acc_axis];
      DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 4, VALUERANGE_X, MBASE(select_acc.now) + 3, HMI_ValueStruct.Max_Acceleration);
      EncoderRate.enabled = true;
    }
    else
    {
      // Back
      checkkey = Motion;
      select_motion.now = MOTION_CASE_ACCEL;
      Draw_Motion_Menu();
    }
  }
  DWIN_UpdateLCD();
}

#if HAS_CLASSIC_JERK
  /* Max Jerk */
  void HMI_MaxJerk()
  {
    ENCODER_DiffState encoder_diffState = get_encoder_state();
    if (encoder_diffState == ENCODER_DIFF_NO) return;

    // Avoid flicker by updating only the previous menu
    if (encoder_diffState == ENCODER_DIFF_CW)
    {
      if (select_jerk.inc(1 + 3 + ENABLED(HAS_HOTEND))) Move_Highlight(1, select_jerk.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_CCW)
    {
      if (select_jerk.dec()) Move_Highlight(-1, select_jerk.now);
    }
    else if (encoder_diffState == ENCODER_DIFF_ENTER) 
    {
      if (WITHIN(select_jerk.now, 1, 4)) 
      {
        checkkey = MaxJerk_value;
        HMI_flag.jerk_axis = AxisEnum(select_jerk.now - 1);
        HMI_ValueStruct.Max_Jerk_scaled = planner.max_jerk[HMI_flag.jerk_axis] * MINUNITMULT;
        //   TO DO
        DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, UNITFDIGITS, VALUERANGE_X, MBASE(select_jerk.now)+3, HMI_ValueStruct.Max_Jerk_scaled);
        EncoderRate.enabled = true;
      }
      else
      {
        // Back
        checkkey = Motion;
        select_motion.now = MOTION_CASE_JERK;
        Draw_Motion_Menu();
      }
    }
    DWIN_UpdateLCD();
  }
#endif // HAS_CLASSIC_JERK

/* Step */
void HMI_Step() 
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;

  // Avoid flicker by updating only the previous menu
  if (encoder_diffState == ENCODER_DIFF_CW)
  {
    if (select_step.inc(1 + 3 + ENABLED(HAS_HOTEND))) Move_Highlight(1, select_step.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW)
  {
    if (select_step.dec()) Move_Highlight(-1, select_step.now);
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER)
  {
    if (WITHIN(select_step.now, 1, 4))
    {
      checkkey = Step_value;
      HMI_flag.step_axis = AxisEnum(select_step.now - 1);
      HMI_ValueStruct.Max_Step_scaled = planner.settings.axis_steps_per_mm[HMI_flag.step_axis] * MINUNITMULT;
      DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, UNITFDIGITS, VALUERANGE_X, MBASE(select_step.now)+3, HMI_ValueStruct.Max_Step_scaled);
      EncoderRate.enabled = true;
    }
    else 
    {
      // Back
      checkkey = Motion;
      select_motion.now = MOTION_CASE_STEPS;
      Draw_Motion_Menu();
    }
  }
  DWIN_UpdateLCD();
}
void HMI_Boot_Set()  //开机引导设置
{
      switch (HMI_flag.boot_step)
      {
        case Set_language:
          HMI_SetLanguage(); //上电设置语言
          break;
        case Set_high:      
          // Popup_Window_Height(Nozz_Start);//跳转到对高页面
          // checkkey = ONE_HIGH; //进入对高逻辑
          // #if ANY(USE_AUTOZ_TOOL,USE_AUTOZ_TOOL_2)
          // queue.inject_P(PSTR("M8015"));
          
          // checkkey=POPUP_CONFIRM;
          // // Popup_window_boot(High_faild_clear);  
          // // #endif
          // Popup_window_boot(Boot_undone);
          break;
        case Set_levelling:
        // checkkey=POPUP_CONFIRM;
        //   Popup_window_boot(Boot_undone);
          break;
        default:
          break;
      }    
}

void HMI_Init()
{
  HMI_SDCardInit();
  if((HMI_flag.language < Chinese) || (HMI_flag.language >= Language_Max)) //上电语言乱，就先设置成英文
  {
    HMI_flag.language = English;
  }
  DWIN_ICON_Not_Filter_Show(Background_ICON, Background_reset,0, 25);
  for (uint16_t t = Background_min; t <= Background_max-Background_min; t++)
  {
    #if ENABLED(DWIN_CREALITY_480_LCD)
      DWIN_ICON_Not_Filter_Show(Background_ICON, Background_min + t, 15, 260);
    #elif ENABLED(DWIN_CREALITY_320_LCD)
      DWIN_ICON_Not_Filter_Show(Background_ICON, Background_min + t, CREALITY_LOGO_X, CREALITY_LOGO_Y);
    #endif
    delay(30);
  } 
  Read_Boot_Step_Value();//读取开机引导步骤的值
  Read_Auto_PID_Value();
  if(HMI_flag.Need_boot_flag)HMI_Boot_Set();           //开机引导设置
  PRINT_LOG("HMI_flag.boot_step:",HMI_flag.boot_step,"HMI_flag.language:",HMI_flag.language);
  PRINT_LOG("HMI_ValueStruct.Auto_PID_Value[1]:",HMI_ValueStruct.Auto_PID_Value[1],"HMI_ValueStruct.Auto_PID_Value[2]:",HMI_ValueStruct.Auto_PID_Value[2]);
}

void DWIN_Update() 
{
  HMI_SDCardUpdate();       // SD card update
  if(!HMI_flag.disallow_recovery_flag)
  {
    // SD card print
    if(recovery.info.sd_printing_flag)
    {
      // rock_20210814
      Remove_card_window_check();
    }
    // SD card print
    if(HMI_flag.cloud_printing_flag||card.isPrinting())
    {
      //check filament update
      Check_Filament_Update();
    }
  }

  EachMomentUpdate();       // Status update

  DWIN_HandleScreen();      // Rotary encoder update

}

void Check_Filament_Update(void)
{
  static uint32_t lMs = millis();
  static uint8_t lNoMatCnt = 0;
  if((card.isPrinting()|| (!HMI_flag.filement_resume_flag))&& (!temp_cutting_line_flag))
  // if(card.isPrinting() && (!temp_cutting_line_flag) || !HMI_flag.filement_resume_flag)//SD卡打印或者云打印
  {
    if(millis() - lMs > 500)
    {
      lMs = millis();
      // exist material
      if(!READ(CHECKFILAMENT_PIN))
      {
        lNoMatCnt = 0;
        return;
      }
      else
      {
        // no material
        lNoMatCnt ++;
      }
      // READ(CHECKFILAMENT_PIN) == 0
      if(lNoMatCnt > 6)
      {
        lNoMatCnt = 0;
        if(HMI_flag.cloud_printing_flag)
        {
          // 断料状态挂到，缺料状态
          HMI_flag.filement_resume_flag=true;
          SERIAL_ECHOLN("M79 S2");
          // M25: Pause the SD print in progress.
          queue.inject_P(PSTR("M25"));
          //云打印暂停
          print_job_timer.pause();
        }
        else
        {
          // sd卡打印暂停
          #if ENABLED(POWER_LOSS_RECOVERY)
            if (recovery.enabled) recovery.save(true, false);
          #endif
          // M25: Pause the SD print in progress.
          queue.inject_P(PSTR("M25"));
        } 
        checkkey = Popup_Window;
        HMI_flag.cutting_line_flag=true;
        temp_cutting_line_flag=true;
        Popup_Window_Home();
      }
    }
  }
}

// The card monitoring
void Remove_card_window_check(void)
{
  static bool lSDCardStatus = false;
  // sd card inserted
  if(!lSDCardStatus && IS_SD_INSERTED())
  {
    lSDCardStatus = IS_SD_INSERTED();
  }
  else if(lSDCardStatus && !IS_SD_INSERTED())
  {
    // sd card removed
    lSDCardStatus = IS_SD_INSERTED();
    // remove SD card when printing
    if(IS_SD_PRINTING() && (!temp_remove_card_flag))
    {
      checkkey = Popup_Window;
      HMI_flag.remove_card_flag = true;
      temp_remove_card_flag = true;
      Popup_Window_Home();
      #if ENABLED(POWER_LOSS_RECOVERY)
        if (recovery.enabled) recovery.save(true, false);
      #endif

      // M25: Pause the SD print in progress.
      queue.inject_P(PSTR("M25"));
    }
  }
  else
  {
    // refresh sd card status
    lSDCardStatus = IS_SD_INSERTED();
  }
}

void EachMomentUpdate()
{
  static float card_Index=0;
  static bool heat_dir=false,heat_dir_bed=false,high_dir=false;
  static uint8_t heat_index=BG_NOZZLE_MIN,bed_heat_index=BG_BED_MIN;
  static millis_t next_var_update_ms = 0, next_rts_update_ms = 0,next_heat_flash_ms=0,next_heat_bed_flash_ms=0,next_high_ms=0,next_move_file_name_ms=0;
  const millis_t ms = millis();
  char *fileName = TERN(POWER_LOSS_RECOVERY, recovery.info.sd_filename, "");
  PrintFile_InfoTypeDef fileInfo = {0};
  if (ELAPSED(ms, next_var_update_ms))
  {
    next_var_update_ms = ms + DWIN_VAR_UPDATE_INTERVAL;
    if(!HMI_flag.Refresh_bottom_flag)
    {
      update_middle_variable();
      // update_variable(); //标志位为0才刷新底部参数
    }    
  }
  if(checkkey == PrintProcess)//打印中动态显示打印中的文件名
  {
     if(strlen(print_name)>30)
     {
        if(ELAPSED(ms, next_move_file_name_ms))
        {
          next_move_file_name_ms=ms+DWIN_VAR_UPDATE_INTERVAL;
          memcpy(current_file_name,print_name+left_move_index,30); 
          //DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, 0, FIEL_NAME_Y, current_file_name);
          DWIN_Draw_Rectangle(1, Color_Bg_Black, 0, FIEL_NAME_Y, DWIN_WIDTH-1, FIEL_NAME_Y+20);
          DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, 0, FIEL_NAME_Y, current_file_name);
          left_move_index++;
          if(left_move_index>=(print_len_name-30))left_move_index=0;
        }
     }
  }
  
  if (thermalManager.degTargetHotend(0) > 0) //喷嘴加热动画处理
  {
    if(ELAPSED(ms, next_heat_flash_ms))
    {
      next_heat_flash_ms=ms+HEAT_ANIMATION_FLASH;
      if(!heat_dir)
      {
        heat_index++;
       if((heat_index) == BG_NOZZLE_MAX )heat_dir=true;
      }
      else 
      {
        heat_index--;
        if((heat_index) == BG_NOZZLE_MIN )heat_dir=false;
      }
      if(!HMI_flag.Refresh_bottom_flag)DWIN_ICON_Show(Background_ICON, heat_index, ICON_NOZZ_X, ICON_NOZZ_Y);
    }
  }
  if (thermalManager.degTargetBed() > 0) //热床加热动画处理
  {
    if(ELAPSED(ms, next_heat_bed_flash_ms))
    {
      next_heat_bed_flash_ms=ms+HEAT_ANIMATION_FLASH;
      if(!heat_dir_bed)
      {
        bed_heat_index++;
        if((bed_heat_index) == BG_BED_MAX )heat_dir_bed=true;
      }
      else
      {
        bed_heat_index--;
        if((bed_heat_index) == BG_BED_MIN)heat_dir_bed=false;
      }
      if(!HMI_flag.Refresh_bottom_flag)DWIN_ICON_Show(Background_ICON, bed_heat_index, ICON_BED_X, ICON_BED_Y);
    }
  }
  if((HMI_flag.High_Status>Nozz_Start)&&(HMI_flag.High_Status<Nozz_Finish))  //动态显示对高过程中的状态  1S更新一次
  {
    if(ELAPSED(ms, next_high_ms)&&(checkkey==ONE_HIGH))
    {
      next_high_ms=ms+DWIN_VAR_UPDATE_INTERVAL;
      if(!high_dir)
      {
        high_dir=true;
        switch (HMI_flag.High_Status)
        {
        case Nozz_Hot:
          DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_HEAT_HOT, ICON_NOZZ_HOT_X, ICON_NOZZ_HOT_Y); //喷嘴未加热图标
          break;
        case Nozz_Clear:
          DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_CLEAR_HOT, ICON_NOZZ_HOT_X,ICON_NOZZ_HOT_Y+ICON_Y_SPACE); //喷嘴未清洁图标
          break;
        case Nozz_Hight:
          DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_HIGH_HOT, ICON_NOZZ_HOT_X, ICON_NOZZ_HOT_Y+ICON_Y_SPACE+ICON_Y_SPACE); //喷嘴未对高图标
          break;
        default:
          break;
        }
      }
      else 
      {
        high_dir=false;
        switch (HMI_flag.High_Status)
        {
        case Nozz_Hot:
          DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_HEAT, ICON_NOZZ_HOT_X, ICON_NOZZ_HOT_Y); //喷嘴加热未完成图标
          break;
        case Nozz_Clear:
          DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_CLEAR, ICON_NOZZ_HOT_X, ICON_NOZZ_HOT_Y+ICON_Y_SPACE); //喷嘴清洁图标
          break;
        case Nozz_Hight:
          DWIN_ICON_Not_Filter_Show(ICON, ICON_NOZZLE_HIGH, ICON_NOZZ_HOT_X,  ICON_NOZZ_HOT_Y+ICON_Y_SPACE+ICON_Y_SPACE); //喷嘴对高图标
          break;
        default:
          break;
        }
      }
    }
  }
  if (PENDING(ms, next_rts_update_ms)) return;
  next_rts_update_ms = ms + DWIN_SCROLL_UPDATE_INTERVAL;

  if (checkkey == PrintProcess)
  {
    // if print done
    if(HMI_flag.print_finish && !HMI_flag.done_confirm_flag)
    {
      HMI_flag.print_finish = false;
      HMI_flag.done_confirm_flag = true;
      //底部需要放新的显示，需要清除
      DWIN_Draw_Rectangle(1, Color_Bg_Black, CLEAR_50_X, CLEAR_50_Y, DWIN_WIDTH-1, STATUS_Y-1); 
      Clear_Title_Bar();  //清除标题
      HMI_flag.Refresh_bottom_flag=true;//标志不刷新底部参数
      TERN_(POWER_LOSS_RECOVERY, recovery.cancel());

      planner.finish_and_disable();

      // show percent bar and value
      // rock_20211122
      _card_percent = 100;
      _remain_time = 0;
      // 显示剩余时间
      Draw_Print_ProgressRemain();
      Draw_Print_ProgressBar(); 
      #if ENABLED(DWIN_CREALITY_480_LCD)
        // show print done confirm 
        if (HMI_flag.language < Language_Max)   // rock_20211120
        {
          DWIN_ICON_Show(HMI_flag.language, LANGUAGE_LEVEL_FINISH, TITLE_X, TITLE_Y);
          DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Confirm, 72, 302 - 19);
        }
    
      #elif ENABLED(DWIN_CREALITY_320_LCD)
        // show print done confirm 
        if (HMI_flag.language < Language_Max)   // rock_20211120
        {
          DWIN_ICON_Show(HMI_flag.language, LANGUAGE_LEVEL_FINISH, TITLE_X, TITLE_Y);
          DWIN_ICON_Not_Filter_Show(HMI_flag.language, LANGUAGE_Confirm, OK_BUTTON_X, OK_BUTTON_Y);
        }
      #endif
   }
    else if (HMI_flag.pause_flag != printingIsPaused()) 
    {
      // print status update
      HMI_flag.pause_flag = printingIsPaused();
      if(!HMI_flag.filement_resume_flag)
      {
        if (HMI_flag.pause_flag) 
        {
          // DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_Pausing, TITLE_X, TITLE_Y);
          Show_JPN_pause_title();         //rock_20211118
          ICON_Continue();
        }
        else 
        {
          // DWIN_ICON_Show(HMI_flag.language ,LANGUAGE_Printing, TITLE_X, TITLE_Y);
          Show_JPN_print_title();
          ICON_Pause();
        }
      }
    }
  }

  // pause after homing
  if (HMI_flag.pause_action && printingIsPaused() && !planner.has_blocks_queued()) 
  {
    if(!HMI_flag.cutting_line_flag)
    {
      
      #if ENABLED(PAUSE_HEAT)
        TERN_(HAS_HOTEND, resume_hotend_temp = thermalManager.degTargetHotend(0));
        TERN_(HAS_HEATED_BED, resume_bed_temp = thermalManager.degTargetBed());
        // rock_20210724 Test department request pause still heating
        // thermalManager.disable_all_heaters();
      #endif
      RUN_AND_WAIT_GCODE_CMD("G1 F1200 X0 Y0", true); 
      // queue.inject_P(PSTR("G1 F1200 X0 Y0"));
      HMI_flag.pause_action = false;
    }
  }
  // 联机打印是否暂停
  if(HMI_flag.online_pause_flag&& printingIsPaused() && !planner.has_blocks_queued())
  {
    HMI_flag.online_pause_flag=false;
    queue.inject_P(PSTR("G1 F1200 X0 Y0"));
  }

  // cutting after homing
  if (HMI_flag.remove_card_flag && printingIsPaused() && !planner.has_blocks_queued())
  {
    // HMI_flag.remove_card_flag = false;
    #if ENABLED(PAUSE_HEAT)
      TERN_(HAS_HOTEND, resume_hotend_temp = thermalManager.degTargetHotend(0));
      TERN_(HAS_HEATED_BED, resume_bed_temp = thermalManager.degTargetBed());

    // rock_20210724 测试部要求暂停仍然在加热
    // thermalManager.disable_all_heaters();
    #endif
    queue.inject_P(PSTR("G1 F1200 X0 Y0"));
    // check filament resume
    if(checkkey == Popup_Window)
    {
      checkkey = Remove_card_window;
      Popup_window_Remove_card();
    }
  }

  // cutting after homing  || HMI_flag.cloud_printing_flag
  if (HMI_flag.cutting_line_flag && printingIsPaused() && (!planner.has_blocks_queued()||HMI_flag.filement_resume_flag))
  {
    // 防止HMI_flag.filement_resume_flag置1进入，也要继续等待planer为空
    if(!planner.has_blocks_queued())
    {
      HMI_flag.cutting_line_flag = false;
      #if ENABLED(PAUSE_HEAT)
        TERN_(HAS_HOTEND, resume_hotend_temp = thermalManager.degTargetHotend(0));
        TERN_(HAS_HEATED_BED, resume_bed_temp = thermalManager.degTargetBed());
      #endif
      // rock_20211104 WIFI盒子会发回零指令
      queue.inject_P(PSTR("G1 F1200 X0 Y0"));
      // check filament resume
      if(checkkey == Popup_Window)
      {
        checkkey = Filament_window;
        Popup_window_Filament();
        // 禁止接收Gcode指令
        HMI_flag.disable_queued_cmd = true;
        // rock_20210917
        HMI_flag.select_flag=true;
      }
    }
  }

  //云打印状态更新
  if(HMI_flag.cloud_printing_flag && (checkkey == PrintProcess)&& !HMI_flag.filement_resume_flag)
  {
    static uint16_t last_Printtime = 0;
    static uint16_t last_card_percent = 0;
    static bool flag=0;
    duration_t elapsed = print_job_timer.duration(); // print timer
    const uint16_t min = (elapsed.value % 3600) / 60;
    //更新进度条
    _card_percent = Cloud_Progress_Bar;
    if(last_card_percent!=_card_percent)  //更新APP进度条
    {
      last_card_percent = _card_percent;
      Draw_Print_ProgressBar();
      Draw_Print_ProgressRemain();
    }
    //更新打印时间
    if (last_Printtime != min)
    { // 1 minute update
      SERIAL_ECHOLNPAIR(" elapsed.value=: ", elapsed.value);
      last_Printtime = min;
      Draw_Print_ProgressElapsed();
      //  _remain_time -= 60;  //剩余时间减去1分钟
      // Draw_Print_ProgressRemain();
    }
    if(_card_percent<=1 && !flag)
    {
      flag=true;
      // _remain_time=0; //rock_20210831  解决剩余时间不清零的问题。
      // Draw_Print_ProgressRemain();
    }
  }

  if (card.isPrinting() && checkkey == PrintProcess)
  {
    // print process
    const uint8_t card_pct = card.percentDone();
    // _card_percent=card.percentDone();
    static uint8_t last_cardpercentValue = 101;
    if (last_cardpercentValue != card_pct) 
    {
      // print percent
      last_cardpercentValue = card_pct;
      if (card_pct)
      {
        _card_percent = card_pct;
        Draw_Print_ProgressBar();
      }
    }

    duration_t elapsed = print_job_timer.duration(); // print timer

    // Print time so far
    static uint16_t last_Printtime = 0;
    const uint16_t min = (elapsed.value % 3600) / 60;
    if (last_Printtime != min) 
    { // 1 minute update
      last_Printtime = min;
      Draw_Print_ProgressElapsed();
    }

    // Estimate remaining time every 20 seconds
    static millis_t next_remain_time_update = 0;
    if (_card_percent >= 1 && ELAPSED(ms, next_remain_time_update) && !HMI_flag.heat_flag)   //rock_20210922
    {
      // _remain_time = (elapsed.value - dwin_heat_time) / (_card_percent * 0.01f) - (elapsed.value - dwin_heat_time);
      card_Index = card.getIndex();
       // card_Index = 1;
       // rock_20211115 解决偶尔剩余时间显示异常显示>100H 问题
      if(card_Index > 0)
      {
        _remain_time = ((elapsed.value - dwin_heat_time) * ((float)card.getFileSize() / card_Index)) - (elapsed.value - dwin_heat_time);
      }
      else
      {
        _remain_time = 0;
      }
      next_remain_time_update += DWIN_REMAIN_TIME_UPDATE_INTERVAL;
      Draw_Print_ProgressRemain();
    }
    else if(_card_percent<=1 && ELAPSED(ms, next_remain_time_update))
    {
      // rock_20210831  解决剩余时间不清零的问题。
      _remain_time = 0;
      Draw_Print_ProgressRemain();
    }
  }
  else if (dwin_abort_flag && !HMI_flag.home_flag) 
  {
    // Print Stop
    dwin_abort_flag = false;
    HMI_ValueStruct.print_speed = feedrate_percentage = 100;
    dwin_zoffset = BABY_Z_VAR;
    select_page.set(0);
    Goto_MainMenu();  // rock_20210831
  }
  #if ENABLED(POWER_LOSS_RECOVERY)
    else if (DWIN_lcd_sd_status && recovery.dwin_flag) 
    { // resume print before power off
      static bool recovery_flag = false;
      recovery.dwin_flag = false;
      recovery_flag = true;
      auto update_selection = [&](const bool sel) 
      {
        HMI_flag.select_flag = sel;
        const uint16_t c1 = sel ? Color_Bg_Window : Select_Color;
        const uint16_t c2 = sel ? Select_Color : Color_Bg_Window;
        #if ENABLED(DWIN_CREALITY_480_LCD)
          DWIN_Draw_Rectangle(0, c1, 25, 306, 126, 345);
          DWIN_Draw_Rectangle(0, c1, 24, 305, 127, 346);
          DWIN_Draw_Rectangle(0, c2, 145, 306, 246, 345);
          DWIN_Draw_Rectangle(0, c2, 144, 305, 247, 346);
        #elif ENABLED(DWIN_CREALITY_320_LCD)
          DWIN_Draw_Rectangle(0, c1, 25, 195, 108, 226);
          DWIN_Draw_Rectangle(0, c1, 24, 194, 109, 227);
          DWIN_Draw_Rectangle(0, c2, 132, 195, 214, 226);
          DWIN_Draw_Rectangle(0, c2, 131, 194, 215, 227);
        #endif
      };
      //BL24CXX::EEPROM_Reset(PLR_ADDR, (uint8_t*)&recovery.info, sizeof(recovery.info));  //rock_20210812  清空 EEPROM
      Popup_Window_Resume();
      update_selection(true);
      get_file_info(&fileName[1], &fileInfo);
      // TODO: Get the name of the current file from someplace
      //
      //(void)recovery.interrupted_file_exists();
      // char * const name = card.longest_filename();
      // const int8_t npos = _MAX(0U, DWIN_WIDTH - strlen(name) * (MENU_CHR_W)) / 2;
      // DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, npos, 252, name);
      const int8_t npos = _MAX(0U, DWIN_WIDTH - strlen(fileInfo.longfilename) * (MENU_CHR_W)) / 2;
      #if ENABLED(DWIN_CREALITY_480_LCD)
        DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, npos, 252, fileInfo.longfilename);
      #elif ENABLED(DWIN_CREALITY_320_LCD)
        DWIN_Draw_String(false, true, font8x16, Popup_Text_Color, Color_Bg_Window, npos, 152, fileInfo.longfilename);
      #endif
      DWIN_UpdateLCD();

      while (recovery_flag) 
      {
        ENCODER_DiffState encoder_diffState = Encoder_ReceiveAnalyze();
        if (encoder_diffState != ENCODER_DIFF_NO) 
        {
          if (encoder_diffState == ENCODER_DIFF_ENTER) 
          {
            recovery_flag = false;
            if (HMI_flag.select_flag) break;
            TERN_(POWER_LOSS_RECOVERY, queue.inject_P(PSTR("M1000C")));
            HMI_StartFrame(true);
            return;
          }
          else
          {
            update_selection(encoder_diffState == ENCODER_DIFF_CW);
          }
          DWIN_UpdateLCD();
        }
        HAL_watchdog_refresh();
      }
      select_print.set(0);
      HMI_ValueStruct.show_mode = 0;
      queue.inject_P(PSTR("M1000"));
      Goto_PrintProcess();
      Draw_Mid_Status_Area(true); //rock_20230529
      // tuqiang delete for power continue long filename
      // DWIN_Draw_String(false, false, font8x16, Color_White, Color_Bg_Black, npos, 60, fileInfo.longfilename);
    }
  #endif
  if(HMI_flag.Pressure_Height_end)  //一键对高完成
  {
    HMI_flag.Pressure_Height_end=false;
    HMI_flag.local_leveling_flag = true;
     checkkey = Leveling;
    //  HMI_Leveling();
    Popup_Window_Leveling();
    // DWIN_UpdateLCD();
    // pressure test height to obtain the Z offset value
    // queue.inject_P(PSTR("G28\nG29"));
    queue.inject_P(PSTR("G29"));
    // queue.inject_P(PSTR("M8015"));
    // Popup_Window_Home();
  }
  // DWIN_UpdateLCD(); //晓亮删除 ——20230207
  HAL_watchdog_refresh();
}

void Show_G_Pic(void)
{
  static bool flag=false;
  uint8_t index_show_pic=select_show_pic.now;
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  if (encoder_diffState == ENCODER_DIFF_CW)
  {
    #if ENABLED(USER_LEVEL_CHECK)
    if(select_show_pic.inc(3))
    {
      Draw_Show_G_Select_Highlight(false);
    }
    #else
    if(select_show_pic.inc(3))
    {
      if(select_show_pic.now==0)select_show_pic.now=1;
      Draw_Show_G_Select_Highlight(false);
    }
    #endif    
    index_show_pic=select_show_pic.now;    
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW)
  {
    #if ENABLED(USER_LEVEL_CHECK)
      if (select_show_pic.dec()) 
      {
        //if(-1==select_show_pic.now)select_show_pic.now=2;
      }
    #else
      if (select_show_pic.dec()) 
      {
        if(select_show_pic.now==0)select_show_pic.now=1;
      }
    #endif
    Draw_Show_G_Select_Highlight(true);
    index_show_pic=select_show_pic.now;
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER)
  {
    // 获取长文件名
    const bool is_subdir = !card.flag.workDirIsRoot;
    const uint16_t filenum = select_file.now - 1 - is_subdir;
    index_show_pic=select_show_pic.now;
    card.getfilename_sorted(SD_ORDER(filenum, card.get_num_Files()));
    // char * const name = card.longest_filename();
    // 短文件名
    // SERIAL_ECHOLN(card.filename);
    switch(index_show_pic)
    {
      case 2:
      // Reset highlight for next entry
      if(IS_SD_INSERTED()) //有卡 
      {
      select_print.reset();
      select_file.reset();
      // Start choice and print SD file
      HMI_flag.heat_flag = true;
      HMI_flag.print_finish = false;
      HMI_ValueStruct.show_mode = 0;
      //rock_20210819
      recovery.info.sd_printing_flag=true;
    
      card.openAndPrintFile(card.filename);
      Goto_PrintProcess();
      #if ENABLED(USER_LEVEL_CHECK)  //使用调平校准功能
        if(HMI_flag.Level_check_flag&&(!card.isPrinting()))
        {
              // Popup_Window_Home();
            HMI_flag.Level_check_start_flag=true; //调平校准开始标志位置1 
            HMI_flag.Need_g29_flag=false;
            G29_small(); //调平校准标志位置1
            if(HMI_flag.Need_g29_flag)
            {
              RUN_AND_WAIT_GCODE_CMD("G29", 1);
              HMI_flag.Need_g29_flag=false;//放在g29之后
            }
            else  RUN_AND_WAIT_GCODE_CMD(G28_STR, 1);
            HMI_flag.Level_check_start_flag=false; //调平校准开始标志位清除  
        }
      #endif
      }
      else 
      {
        checkkey = SelectFile;
        Draw_Print_File_Menu();
      }
      break;
      case 1:
        checkkey = SelectFile;
        Draw_Print_File_Menu();     
      break;
      #if ENABLED(USER_LEVEL_CHECK)  //使用调平校准功能
      case 0:
        if(!flag)
        {
          DWIN_ICON_Show(ICON, ICON_LEVEL_CALIBRATION_ON, ICON_ON_OFF_X, ICON_ON_OFF_Y);
          HMI_flag.Level_check_flag=true; 
          flag=(!flag);
        }         
         else 
         {
          flag=(!flag);
          DWIN_ICON_Show(ICON, ICON_LEVEL_CALIBRATION_OFF, ICON_ON_OFF_X, ICON_ON_OFF_Y);
          HMI_flag.Level_check_flag=false;
         }
      
      break;
      #endif
      default :
         
      break;
    }
  }
  DWIN_UpdateLCD();
}

void Draw_Select_language()
{
  uint8_t i;
  Clear_Main_Window();
  HMI_flag.Refresh_bottom_flag=true;//标志不刷新底部参数
  DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Title_Language, TITLE_X, TITLE_Y);
  Draw_Back_First();
  // index_select = 0;
  for(i = 0;i< 7;i++)
  {
    #if ENABLED(DWIN_CREALITY_480_LCD)
      Draw_Language_Icon_AND_Word(i, 92 + i * 52);
      DWIN_Draw_Line(Line_Color, 16, 136 + i * 52 + i, BLUELINE_X, 136 + i * 52 + i);
    #elif ENABLED(DWIN_CREALITY_320_LCD)
      Draw_Language_Icon_AND_Word(i, 66 + i * 36);
      DWIN_Draw_Line(Line_Color, 16, 93 + i * 36 + i, BLUELINE_X, 93 + i * 36 + i);
    #endif
  }
}

void Draw_Poweron_Select_language()
{
  uint8_t i;
  Clear_Main_Window();
  HMI_flag.Refresh_bottom_flag=true;//标志不刷新底部参数
  DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Title_Language, TITLE_X, TITLE_Y);
  // Draw_Menu_Cursor(0);
  Draw_laguage_Cursor(0);
  index_select = 0;
  DWIN_ICON_Show(ICON, ICON_Word_CN, FIRST_X, FIRST_Y);
  DWIN_ICON_Show(ICON, ICON_Word_CN+1, FIRST_X, FIRST_Y+WORD_INTERVAL);
  DWIN_ICON_Show(ICON, ICON_Word_CN+2, FIRST_X, FIRST_Y+2*WORD_INTERVAL);
  DWIN_ICON_Show(ICON, ICON_Word_CN+3, FIRST_X, FIRST_Y+3*WORD_INTERVAL);
  DWIN_ICON_Show(ICON, ICON_Word_CN+4, FIRST_X, FIRST_Y+4*WORD_INTERVAL);
  DWIN_ICON_Show(ICON, ICON_Word_CN+5, FIRST_X, FIRST_Y+5*WORD_INTERVAL);
  DWIN_ICON_Show(ICON, ICON_Word_CN+6, FIRST_X, FIRST_Y+6*WORD_INTERVAL);
  for(i = 0;i< 7;i++)
  {
    #if ENABLED(DWIN_CREALITY_480_LCD)
    #elif ENABLED(DWIN_CREALITY_320_LCD)
      // Draw_Language_Icon_AND_Word(i, FIRST_Y + i * WORD_INTERVAL);
      // DWIN_ICON_Show(ICON, ICON_Word_CN+i, FIRST_X, FIRST_Y+i*WORD_INTERVAL);
      DWIN_Draw_Line(Line_Color, LINE_START_X, LINE_START_Y + i * LINE_INTERVAL, LINE_END_X, LINE_END_Y + i * LINE_INTERVAL); 
      delay(2);     
    #endif
  }
}



void HMI_Poweron_Select_language()
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  if (encoder_diffState == ENCODER_DIFF_CW)
  {
    if (select_language.inc(LANGUAGE_TOTAL))
    {
      if (select_language.now > (MROWS+1) && select_language.now > index_language)
      {
        index_language = select_language.now;
        // Scroll up and draw a blank bottom line
        // Scroll_Menu_Full(DWIN_SCROLL_UP);
        Scroll_Menu_Language(DWIN_SCROLL_UP);
        // 显示新增的图标
        // Draw_Menu_Icon(MROWS, ICON_FLAG_CN + index_language);
        #if ENABLED(DWIN_CREALITY_480_LCD)
          DWIN_ICON_Show(ICON, ICON_Word_CN + index_language, 25, MBASE(MROWS) + JPN_OFFSET);
        #elif ENABLED(DWIN_CREALITY_320_LCD)
          DWIN_ICON_Show(ICON, ICON_Word_CN+6, FIRST_X, FIRST_Y+6*WORD_INTERVAL);
          DWIN_ICON_Show(ICON, ICON_Word_CN + index_language, FIRST_X, WORD_INTERVAL*(MROWS+1) + FIRST_Y);
        #endif
      }
      else
      {
        Move_Highlight_Lguage(1, select_language.now + MROWS+1 - index_language);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW)
  {
    if (select_language.dec())
    {
      if (select_language.now < index_language - (MROWS+1))
      {
        index_language--;
        Scroll_Menu_Language(DWIN_SCROLL_DOWN);
          #if ENABLED(DWIN_CREALITY_480_LCD)
            DWIN_ICON_Show(ICON, ICON_Word_CN + select_language.now - 1, 25, MBASE(0) + JPN_OFFSET);
          #elif ENABLED(DWIN_CREALITY_320_LCD)
            DWIN_ICON_Show(ICON, ICON_Word_CN + select_language.now, FIRST_X, WORD_INTERVAL*(0) + FIRST_Y);            
          #endif
      }
      else
      {
        Move_Highlight_Lguage(-1, select_language.now + MROWS+1 - index_language);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER) 
  {  
      switch (select_language.now)
      {
        case 0: // Back
          HMI_flag.language = Chinese;
          break;
        case 1: // Back
          HMI_flag.language =English;
          break;
        case 2: // Back
          HMI_flag.language =German;
          break;
        case 3: // Back
          HMI_flag.language = Russian;
          break;
        case 4: // Back
          HMI_flag.language = French;
          break;
        case 5: // Back
          HMI_flag.language = Turkish;
          break;
        case 6: // Back
          HMI_flag.language = Spanish;
          break;
        case 7: // Back
          HMI_flag.language = Italian;
          break;
        case 8: // Back
          HMI_flag.language = Portuguese;
          break;
        case 9: // Back
          HMI_flag.language = Japanese;
          break;
        case 10: // Back
          HMI_flag.language = Korean;
          break;
        default:
          {
            HMI_flag.language =English;
          }
          break;
      }
    #if BOTH(EEPROM_SETTINGS, IIC_BL24CXX_EEPROM)
      BL24CXX::write(DWIN_LANGUAGE_EEPROM_ADDRESS, (uint8_t*)&HMI_flag.language, sizeof(HMI_flag.language));
    #endif
    // SERIAL_ECHOLNPAIR("set_language_id:",set_language_id);
    checkkey=POPUP_CONFIRM;
    Popup_window_boot(Clear_nozz_bed);  
  }
  // DWIN_UpdateLCD();
}

void DWIN_HandleScreen() {
  // static millis_t timeout_ms = 0;
  // millis_t ms = millis();
  // if(ELAPSED(ms, timeout_ms))
  // {
  //   timeout_ms = ms + 1000;
  //   SERIAL_ECHOLNPAIR("checkkey:", checkkey);
  // }
  // static uint8_t prev_checkkey = 0;
  // if(prev_checkkey != checkkey)
  // {
  //   SERIAL_ECHOLNPAIR("prev_checkkey:", prev_checkkey, ", checkkey:", checkkey);
  //   prev_checkkey = checkkey;
  // }
  switch (checkkey) {
    case MainMenu:        HMI_MainMenu(); break;
    case SelectFile:      HMI_SelectFile(); break;
    case Prepare:         HMI_Prepare(); break;
    case Control:         HMI_Control(); break;
    case Leveling:
      #if ENABLED(SHOW_GRID_VALUES)  //如果需要显示调平网格值显示
          HMI_Leveling();
      #endif  
      break;
  #if ENABLED(SHOW_GRID_VALUES)  //如果需要显示调平网格值显示
    case Level_Value_Edit: HMI_Leveling_Edit();break;
    case Change_Level_Value: HMI_Levling_Change();break;
  #endif
    case PrintProcess:    HMI_Printing(); break;
    case Print_window:    HMI_PauseOrStop(); break;
    case AxisMove:        HMI_AxisMove(); break;
    #if ENABLED(HAS_CHECKFILAMENT)
      case Filament_window: HMI_Filament(); break;
    #endif
    case Remove_card_window: HMI_Remove_card(); break;
    case TemperatureID:   HMI_Temperature(); break;
    case Motion:          HMI_Motion(); break;
    case AdvSet:          HMI_AdvSet(); break;
    #if HAS_HOME_OFFSET
      case HomeOff:       HMI_HomeOff(); break;
      case HomeOffX:      HMI_HomeOffX(); break;
      case HomeOffY:      HMI_HomeOffY(); break;
      case HomeOffZ:      HMI_HomeOffZ(); break;
    #endif
    #if HAS_ONESTEP_LEVELING
      case ProbeOff:      HMI_ProbeOff(); break;
      case ProbeOffX:     HMI_ProbeOffX(); break;
      case ProbeOffY:     HMI_ProbeOffY(); break;
    #endif
    case Info:            HMI_Info(); break;
    case Tune:            HMI_Tune(); break;
    #if HAS_PREHEAT
      case PLAPreheat:    HMI_PLAPreheatSetting(); break;
      case ABSPreheat:    HMI_ABSPreheatSetting(); break;
    #endif
    case MaxSpeed:        HMI_MaxSpeed(); break;
    case MaxAcceleration: HMI_MaxAcceleration(); break;
    #if HAS_CLASSIC_JERK
      case MaxJerk:       HMI_MaxJerk(); break;
    #endif
    case Step:            HMI_Step(); break;
    case Move_X:          HMI_Move_X(); break;
    case Move_Y:          HMI_Move_Y(); break;
    case Move_Z:          HMI_Move_Z(); break;
    #if HAS_HOTEND
      case Extruder:      HMI_Move_E(); break;
      case ETemp:         HMI_ETemp(); break;
    #endif
    #if EITHER(HAS_BED_PROBE, BABYSTEPPING)
      case Homeoffset:    HMI_Zoffset(); break;
    #endif
    #if HAS_HEATED_BED
      case BedTemp:       HMI_BedTemp(); break;
    #endif
    #if HAS_PREHEAT && HAS_FAN
      case FanSpeed:      HMI_FanSpeed(); break;
    #endif
    case PrintSpeed:      HMI_PrintSpeed(); break;
    case MaxSpeed_value:  HMI_MaxFeedspeedXYZE(); break;
    case MaxAcceleration_value: HMI_MaxAccelerationXYZE(); break;
    #if HAS_CLASSIC_JERK
      case MaxJerk_value: HMI_MaxJerkXYZE(); break;
    #endif
    case Step_value:      HMI_StepXYZE(); break;
    case Show_gcode_pic:  Show_G_Pic();break;
    case Selectlanguage:  HMI_Select_language();break;
    case Poweron_select_language: HMI_Poweron_Select_language(); break;
    case HM_SET_PID:      HMI_Hm_Set_PID();break;               //手动设置PID；
    case AUTO_SET_PID:    HMI_Auto_set_PID();break;           //自动设置PID；
    case AUTO_SET_NOZZLE_PID: HMI_Auto_Bed_PID();break;      //HMI_Auto_Nozzle_PID();break; //自动设置喷嘴PID；
    case AUTO_SET_BED_PID: HMI_Auto_Bed_PID();break;      //自动设置热床PID 
    case HM_PID_Value:   HMI_HM_PID_Value_Set();break;
    case AUTO_PID_Value:   HMI_AUTO_PID_Value_Set();break;
    case PID_Temp_Err_Popup: HMI_PID_Temp_Error(); break;
    case AUTO_IN_FEEDSTOCK: HMI_Auto_In_Feedstock(); break; //进料完成确认界面
    case AUTO_OUT_FEEDSTOCK: HMI_Auto_IN_Out_Feedstock(); break; //完成确认返回准备界面界面
    case POPUP_CONFIRM:      HMI_Confirm(); break;  //单独一个确认按钮的界面
    default: break;
  }
  // NOP();
  // {
  //   static uint8_t prev_checkkey = 0;
  //   if(prev_checkkey != checkkey)
  //   {
  //     SERIAL_ECHOLNPAIR("DWIN_HandleScreen-prev_checkkey:", prev_checkkey, ", checkkey:", checkkey);
  //     prev_checkkey = checkkey;
  //   }
  // }
}

// Manually set PID; select_set_pid.reset();
void HMI_Hm_Set_PID(void)
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  if (encoder_diffState == ENCODER_DIFF_CW)
  {
    if (select_hm_set_pid.inc(1 + HM_PID_CASE_TOTAL))
    {
      if (select_hm_set_pid.now > MROWS && select_hm_set_pid.now > index_pid)
      {
        index_pid = select_hm_set_pid.now;
        Scroll_Menu(DWIN_SCROLL_UP);
        switch (index_pid) 
        {
          // Manual PID setting
          case HM_PID_CASE_BED_D:
            Draw_Menu_Icon(MROWS, ICON_HM_PID_Bed_D);
            if (HMI_flag.language < Language_Max)
            {
              #if ENABLED(DWIN_CREALITY_480_LCD)
                DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Bed_D, 60, MBASE(MROWS) + JPN_OFFSET);
                DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 2, 200, MBASE(5), HMI_ValueStruct.HM_PID_Value[6]*100);
              #elif ENABLED(DWIN_CREALITY_320_LCD)
                DWIN_ICON_Show(HMI_flag.language, LANGUAGE_Bed_D, 60 - 5, MBASE(MROWS) + JPN_OFFSET);
                DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Color_Bg_Black, 3, 2, 172, MBASE(5), HMI_ValueStruct.HM_PID_Value[6]*100);
              #endif
            }
            break;
          default:
            break;
        }
      }
      else
      {
        Move_Highlight(1, select_hm_set_pid.now + MROWS - index_pid);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW)
  {
    if (select_hm_set_pid.dec())
    {
      if (select_hm_set_pid.now < index_pid - MROWS)
      {
        index_pid--;
        Scroll_Menu(DWIN_SCROLL_DOWN);
        switch (select_hm_set_pid.now)
        {
          case 0:
            Draw_Back_First();
            break;
          default:
            break;
        }
      }
      else
      {
        Move_Highlight(-1,select_hm_set_pid.now + MROWS - index_pid);
      }
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER)
  {
    if (WITHIN(select_hm_set_pid.now, HM_PID_CASE_NOZZ_P, HM_PID_CASE_TOTAL))
    {
      checkkey = HM_PID_Value;
      HMI_flag.HM_PID_ROW = select_hm_set_pid.now + MROWS - index_pid;
      HMI_ValueStruct.HM_PID_Temp_Value = HMI_ValueStruct.HM_PID_Value[select_hm_set_pid.now] * 100;
      #if ENABLED(DWIN_CREALITY_480_LCD)
        DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 2, 200, MBASE(HMI_flag.HM_PID_ROW), HMI_ValueStruct.HM_PID_Temp_Value);
      #elif ENABLED(DWIN_CREALITY_320_LCD)
        DWIN_Draw_FloatValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 2, 172, MBASE(HMI_flag.HM_PID_ROW), HMI_ValueStruct.HM_PID_Temp_Value);
      #endif
      EncoderRate.enabled = true;
    }
    else
    {
      checkkey = TemperatureID;
      select_temp.set(TEMP_CASE_HM_PID);
      // index_temp = TEMP_CASE_HM_PID;
      Draw_Temperature_Menu();
      // Save the set PID parameters to EEPROM
      settings.save();
    }
  }
  DWIN_UpdateLCD();
}

static uint8_t Check_temp_low()
{
  HMI_flag.PID_ERROR_FLAG = false;
  if(thermalManager.wholeDegHotend(0) < 5 || thermalManager.wholeDegHotend(0) > 300)
  {
    HMI_flag.PID_ERROR_FLAG = 1;
    return HMI_flag.PID_ERROR_FLAG;
  }
  if(thermalManager.wholeDegBed() < 5 || thermalManager.wholeDegBed() > 110)
  {
    HMI_flag.PID_ERROR_FLAG = 2;
    return HMI_flag.PID_ERROR_FLAG;
  }
  return HMI_flag.PID_ERROR_FLAG;
}

// Manually set PID;
void HMI_Auto_set_PID(void)
{
  //  static millis_t timeout_ms = 0;
  // millis_t ms = millis();
  // if(ELAPSED(ms, timeout_ms))
  // {
  //   timeout_ms = ms + 1000;
  //   SERIAL_ECHOLNPAIR("HMI_Auto_set_PID");
  // }
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  if (encoder_diffState == ENCODER_DIFF_CW)
  {
    if (select_set_pid.inc(1 + 2))
    {
      Move_Highlight(1, select_set_pid.now);
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_CCW)
  {
    if (select_set_pid.dec())
    {
      Move_Highlight(-1, select_set_pid.now);
    }
  }
  else if (encoder_diffState == ENCODER_DIFF_ENTER)
  {
    if(select_set_pid.now)
    {
      #ifdef PREVENT_COLD_EXTRUSION
        // Temperature overrun detected HMI_flag.PID_ERROR_FLAG
        if(Check_temp_low())
        { 
          checkkey = PID_Temp_Err_Popup;
          Draw_PID_Error();
          // HMI_flag.ETempTooLow_flag = true;
          // Popup_Window_ETempTooLow();
          DWIN_UpdateLCD();
          return;
        }
      #endif
      // SERIAL_ECHOLNPAIR("HMI_Auto_set_PID:encoder_diffState:", encoder_diffState);
      checkkey = AUTO_PID_Value;
      if(select_set_pid.now==2) thermalManager.set_fan_speed(0, 255);//全速打开模型风扇
      HMI_ValueStruct.Auto_PID_Temp = HMI_ValueStruct.Auto_PID_Value[select_set_pid.now];
      #if ENABLED(DWIN_CREALITY_480_LCD)
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 210 + PID_VALUE_OFFSET, MBASE(select_set_pid.now), HMI_ValueStruct.Auto_PID_Temp);
      #elif ENABLED(DWIN_CREALITY_320_LCD)
        DWIN_Draw_IntValue(true, true, 0, font8x16, Color_White, Select_Color, 3, 192 + PID_VALUE_OFFSET, MBASE(select_set_pid.now), HMI_ValueStruct.Auto_PID_Temp);
      #endif
      // EncoderRate.enabled = true;//20230821暂时取消快速增减，解决热床数值跳变的问题
    }
    else
    {
      checkkey = TemperatureID;
      select_temp.set(TEMP_CASE_Auto_PID);
      // index_temp = TEMP_CASE_Auto_PID;
      Draw_Temperature_Menu();
    }
  }
  DWIN_UpdateLCD();
}

void HMI_PID_Temp_Error()
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  if (encoder_diffState == ENCODER_DIFF_ENTER) 
  {
    // Temperature overrun detected HMI_flag.PID_ERROR_FLAG
    if(!Check_temp_low())
    {
      checkkey = AUTO_SET_PID;
      select_set_pid.set(2);
      select_set_pid.now=2;
      Draw_Auto_PID_Set();
    }
  }
  DWIN_UpdateLCD();
}

// Automatically set the nozzle PID;
void HMI_Auto_Nozzle_PID(void)
{
  ENCODER_DiffState encoder_diffState = get_encoder_state();
  if (encoder_diffState == ENCODER_DIFF_NO) return;
  if (encoder_diffState == ENCODER_DIFF_ENTER)
  {
    checkkey = AUTO_SET_PID;
    select_set_pid.set(2);
    select_set_pid.now=2;
    Draw_Auto_PID_Set();
  }
  DWIN_UpdateLCD();
}

static void Update_Curve_DC()
{
  static int16_t temp_tem_value[41] = {0};
  static millis_t next_curve_update_ms = 0;
  const millis_t ms_curve = millis();

  if (ELAPSED(ms_curve, next_curve_update_ms))
  {
    if (HMI_ValueStruct.Curve_index > CURVE_POINT_NUM)
    {
      HMI_ValueStruct.Curve_index = CURVE_POINT_NUM;
      for(uint8_t num = 1;num <= CURVE_POINT_NUM;num++)
      {
        temp_tem_value[num - 1] = temp_tem_value[num];
      }
    }
    switch (checkkey)
    {
      case AUTO_SET_NOZZLE_PID:
        temp_tem_value[HMI_ValueStruct.Curve_index++] = thermalManager.wholeDegHotend(0);
        Draw_Curve_Set(Curve_Line_Width, Curve_Step_Size_X, Curve_Nozzle_Size_Y,Curve_Color_Nozzle);
        break;
      case AUTO_SET_BED_PID:
        temp_tem_value[HMI_ValueStruct.Curve_index++] = thermalManager.wholeDegBed();
        Draw_Curve_Set(Curve_Line_Width, Curve_Step_Size_X, Curve_Bed_Size_Y,Curve_Color_Bed);
        break;
    }
    next_curve_update_ms = ms_curve + DACAI_VAR_UPDATE_INTERVAL;
    Draw_Curve_Data(HMI_ValueStruct.Curve_index, temp_tem_value);
  }
  if(PENDING(ms_curve, next_curve_update_ms)) return;
}

// Set hot bed PID automatically
void HMI_Auto_Bed_PID(void)
{
  // static millis_t timeout_ms = 0;
  // millis_t ms = millis();
  // if(ELAPSED(ms, timeout_ms))
  // {
  //   timeout_ms = ms + 1000;
  //   SERIAL_ECHOLNPAIR("HMI_Auto_Bed_PID");
  // }
  static char cmd[30] = {0}, str_1[7] = {0}, str_2[7] = {0}, str_3[7] = {0};
  if((checkkey == AUTO_SET_BED_PID) || (checkkey == AUTO_SET_NOZZLE_PID))
  {
    // refresh data
    Update_Curve_DC();
    if(HMI_flag.PID_autotune_end)
    {
      if(!end_flag)
      {
        end_flag=true;
        thermalManager.set_fan_speed(0, 0);//关闭模型风扇
        DWIN_ICON_Not_Filter_Show(HMI_flag.language,LANGUAGE_Auto_PID_END, WORD_AUTO_X, WORD_AUTO_Y);
      } 
      if (Encoder_ReceiveAnalyze() == ENCODER_DIFF_ENTER)
      {
        // add M301 and M304 setup instructions
        switch (checkkey)
        {
          case AUTO_SET_NOZZLE_PID:
            sprintf_P(cmd, PSTR("M301 P%s I%s D%s"), dtostrf(auto_pid.Kp, 1, 2, str_1),dtostrf(auto_pid.Ki, 1, 2, str_2),dtostrf(auto_pid.Kd, 1, 2, str_3));
            // Set nozzle temperature data
            gcode.process_subcommands_now(cmd);
            break;
          case AUTO_SET_BED_PID:
            sprintf_P(cmd, PSTR("M304 P%s I%s D%s"), dtostrf(auto_pid.Kp, 1, 2, str_1),dtostrf(auto_pid.Ki, 1, 2, str_2),dtostrf(auto_pid.Kd, 1, 2, str_3));
            // Set hotbed temperature data
            gcode.process_subcommands_now(cmd);
            break;
          default:
            break;
        }
        memset(cmd, 0, sizeof(cmd));
        // settings save
        gcode.process_subcommands_now_P(PSTR("M500"));
        Save_Auto_PID_Value();
        // save PID
        checkkey = AUTO_SET_PID;
        SERIAL_ECHOLNPAIR("HMI_Auto_Bed_PID");
        SERIAL_ECHOLNPAIR("11checkkey:", checkkey);
        select_set_pid.set(1);
        select_set_pid.now = 1;
        Draw_Auto_PID_Set();
        HMI_flag.PID_autotune_end = false;
        // Remember to save and exit when you're done debugging
        // settings.save();
        SERIAL_ECHOLNPAIR("22checkkey:", checkkey);
      }
    }
  }
  // DWIN_UpdateLCD(); //晓亮删除——20230207
}

void DWIN_CompletedHoming()
{
  HMI_flag.home_flag = false;
  dwin_zoffset = TERN0(HAS_BED_PROBE, probe.offset.z);
  // PRINT_LOG("checkkey:",checkkey);
  if (checkkey == Last_Prepare)
  {
    if(HMI_flag.leveling_edit_home_flag)
    {
      HMI_flag.leveling_edit_home_flag=false;      
      checkkey=Level_Value_Edit;
      Popup_Window_Leveling();
      Draw_Leveling_Highlight(1); //默认编辑框              
          // checkkey = Leveling;
      Refresh_Leveling_Value();      //刷新调平值和颜色到屏幕上
      select_level.reset();
      xy_int8_t mesh_Count={0,0};

      Draw_Dots_On_Screen(&mesh_Count,1,Select_Block_Color);          
      EncoderRate.enabled = true;
       DO_BLOCKING_MOVE_TO_Z(5, 5);//每次都上升到5mm的高度再移动
    }
    else
    {
      // PRINT_LOG("(HMI_flag.leveling_edit_home_flag:",HMI_flag.leveling_edit_home_flag);
      checkkey = Prepare;
      // select_prepare.now = PREPARE_CASE_HOME;
      index_prepare = MROWS;
      Draw_Prepare_Menu();
    }
  }
  
  else if (checkkey == Back_Main)
  {
    HMI_ValueStruct.print_speed = feedrate_percentage = 100;
    planner.finish_and_disable();
    Goto_MainMenu();
  }
  DWIN_UpdateLCD();
}

void DWIN_CompletedHeight()
{
  HMI_flag.home_flag = false;
  checkkey = Prepare;
  dwin_zoffset = TERN0(HAS_BED_PROBE, probe.offset.z);
  Draw_Prepare_Menu();
  DWIN_UpdateLCD();
}

void DWIN_CompletedLeveling()
{
  if (checkkey == Leveling) Goto_MainMenu();
}

// GUI extension
void DWIN_Draw_Checkbox(uint16_t color, uint16_t bcolor, uint16_t x, uint16_t y, bool mode=false)
{
  DWIN_Draw_String(false, true, font8x16, Select_Color, bcolor, x + 4, y, F(mode ? "x" : " "));
  DWIN_Draw_Rectangle(0, color, x + 2, y + 2, x + 17, y + 17);
}

#endif // DWIN_CREALITY_LCD
