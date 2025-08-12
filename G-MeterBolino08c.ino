/*Using LVGL with Arduino requires some extra steps:
 *Be sure to read the docs here: https://docs.lvgl.io/master/get-started/platforms/arduino.html  */

#include "Wireless.h"
#include "Gyro_QMI8658.h"
#include "RTC_PCF85063.h"
#include "SD_Card.h"
#include "LVGL_Driver.h"
#include "G_Meter.h"
#include "BAT_Driver.h"


void Driver_Loop(void *parameter)
{
  while(1)
  {
    QMI8658_Loop();
    RTC_Loop();
    BAT_Get_Volts();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
void Driver_Init()
{
  Flash_test();
  BAT_Init();
  I2C_Init();
  TCA9554PWR_Init(0x00);   
  Set_EXIO(EXIO_PIN8,Low);
  Backlight_Init();
  Set_Backlight(50);      //0~100 
  PCF85063_Init();
  QMI8658_Init(); 
  
  xTaskCreatePinnedToCore(
    Driver_Loop,     
    "Other Driver task",   
    4096,                
    NULL,                 
    3,                    
    NULL,                
    0                    
  );
}
void setup()
{
  Wireless_Test2();
  Driver_Init();
  LCD_Init();                                     // If you later reinitialize the LCD, you must initialize the SD card again !!!!!!!!!!
  SD_Init();                                      // It must be initialized after the LCD, and if the LCD is reinitialized later, the SD also needs to be reinitialized
  Lvgl_Init();

  // Créer le G-meter sur l'écran principal
  G_Meter_Create(lv_scr_act());
  
  // Créer un timer pour mettre à jour le G-meter
  lv_timer_create(G_Meter_Timer_Callback, 20, NULL); // Mise à jour toutes les 20ms
  
}

void loop()
{
  Lvgl_Loop();
  vTaskDelay(pdMS_TO_TICKS(5));
}
