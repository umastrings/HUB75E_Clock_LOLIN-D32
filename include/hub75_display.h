#pragma once
/*==============================================================
 * hub75_display.h  —  시계 렌더링 선언
 *==============================================================*/

#include "hub75_config.h"
#include <time.h>

void showMsg(const char *msg, u16 col);
void drawClock(const struct tm *t);
