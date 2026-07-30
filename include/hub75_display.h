#ifndef HUB75_DISPLAY_H
#define HUB75_DISPLAY_H
/*==============================================================
 * hub75_display.h  —  시계 렌더링 선언
 *==============================================================*/

#include <time.h>
#include "hub75_config.h"

void showMsg(const char *msg, u16 col);
void drawClock(const struct tm *t);

#endif /* HUB75_DISPLAY_H */
