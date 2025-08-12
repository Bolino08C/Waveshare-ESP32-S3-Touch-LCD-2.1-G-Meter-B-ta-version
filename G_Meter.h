#pragma once

#include "lvgl.h"
#include "Gyro_QMI8658.h"

// Structure pour stocker les valeurs maximales (pics)
typedef struct {
    // Pics directionnels sur le plan (valeurs positives en G)
    float x_pos;   // droite
    float x_neg;   // gauche (magnitude positive)
    float y_pos;   // haut (avant)
    float y_neg;   // bas (freinage)

    // Pics absolus (magnitude) pour chaque axe et total
    float x_abs;
    float y_abs;
    float z_abs;
    float total;
} G_Max_Values;

// Valeurs instantanées courant
typedef struct {
    float x;
    float y;
    float z;
    float total;
} G_Current_Values;

// Variables globales
extern G_Max_Values g_max_values;
extern G_Current_Values g_current_values;

// Fonctions du G-meter
void G_Meter_Create(lv_obj_t *parent);
void G_Meter_Update(void);
void G_Meter_Reset_Max(void);
void G_Meter_Update_Display(void);
void G_Meter_Delete(void);

// Callback pour la mise à jour automatique
void G_Meter_Timer_Callback(lv_timer_t *timer);
