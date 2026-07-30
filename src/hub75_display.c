/*==============================================================
 * hub75_display.c  —  시계 렌더링 구현 (C)
 *==============================================================*/

#include <stdio.h>
#include "hub75_display.h"
#include "hub75_framebuffer.h"
#include "hub75_panel.h"

static const char *kDay[7] = { "SUN","MON","TUE","WED","THU","FRI","SAT" };

/* ── showMsg  —  화면 지우고 한 줄 메시지 + 즉시 flip ── */
void showMsg(const char *msg, u16 col)
{
    clearScreen();
    setSize(1);
    setColor(col);
    setCursor(4, 28);
    printStr(msg);
    flipDMABuffer();
}

/* ── drawClock
 *
 * size=3 문자 폭: (5+1)*3 = 18px,  높이 7*3 = 21px
 * size=2 문자 폭: (5+1)*2 = 12px,  높이 7*2 = 14px
 *
 * 레이아웃:
 *   HH  size=3, x=2      폭 36 → 끝 38
 *   ':' size=2, x=38     폭 12 → 끝 50   (size=3 대비 6px 절약)
 *       수직 중앙 보정 y = 32 + (21-14)/2 = 35
 *   MM  size=3, x=50     폭 36 → 끝 86
 *
 * [노트 14] date 버퍼가 36인 이유: 컴파일러는 %04d 인자를 int 전체
 * 범위로 추론해 최대 36바이트가 필요하다고 판단한다. 11로 두면
 * -Wformat-truncation 에러가 난다.                              ── */
void drawClock(const struct tm *t)
{
    char date[36], hh[12], mm[12], sec[12];

    snprintf(date, sizeof(date), "%04d/%02d/%02d",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
    snprintf(hh,  sizeof(hh),  "%02d", t->tm_hour);
    snprintf(mm,  sizeof(mm),  "%02d", t->tm_min);
    snprintf(sec, sizeof(sec), "%02d", t->tm_sec);

    int wd = t->tm_wday;
    if (wd < 0 || wd > 6) wd = 0;

    const u16 white = color565(255, 255, 255);

    /* 날짜 (cyan) / 요일 (yellow) */
    setSize(2); setColor(color565(0, 255, 255));   setCursor(4,  4);  printStr(date);
    setSize(2); setColor(color565(255, 255, 0));   setCursor(92, 24); printStr(kDay[wd]);

    /* 시:분 — 콜론만 작게(size=2) 찍어 가로 6px 절약 */
    setSize(3); setColor(white); setCursor(2,  32); printStr(hh);
    setSize(2); setColor(white); setCursor(38, 35); printStr(":");
    setSize(3); setColor(white); setCursor(50, 32); printStr(mm);

    /* 초 (orange) */
    setSize(2); setColor(color565(255, 128, 0)); setCursor(104, 42); printStr(sec);
}
