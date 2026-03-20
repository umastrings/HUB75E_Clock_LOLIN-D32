/*==============================================================
 * hub75_display.cpp  —  시계 렌더링 구현
 *==============================================================*/

#include "hub75_display.h"
#include "hub75_framebuffer.h"
#include "hub75_panel.h"
#include <stdio.h>

static const char *kDay[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};

/* ── showMsg  —  원본 showError() 와 동일 ── */
void showMsg(const char *msg, u16 col) {
    clearScreen();
    setSize(1); setColor(col); setCursor(4, 28);
    printStr(msg);
    flipDMABuffer();
}

/* ── drawClock  —  원본 HUB75_Clock.ino 렌더링 블록과 동일 레이아웃 ── */
void drawClock(const struct tm *t) {
    char date[36], tim[6], sec[3];
    snprintf(date, sizeof(date), "%04d/%02d/%02d",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
    snprintf(tim,  sizeof(tim),  "%02d:%02d", t->tm_hour, t->tm_min);
    snprintf(sec,  sizeof(sec),  "%02d",      t->tm_sec);

    setSize(2); setColor(color565(0,   255, 255)); setCursor(4,   4);  printStr(date);
    setSize(2); setColor(color565(255, 255, 0));   setCursor(92,  24); printStr(kDay[t->tm_wday]);
    setSize(3); setColor(color565(255, 255, 255)); setCursor(2,   32); printStr(tim);
    setSize(2); setColor(color565(255, 128, 0));   setCursor(104, 42); printStr(sec);
}
