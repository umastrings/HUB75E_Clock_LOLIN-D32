# src/CMakeLists.txt  —  ESP-IDF 컴포넌트(main) 등록
#
# src/ 의 .c 를 전부 수집하되, 별도 app_main 을 가진 테스트 파일은
# 제외한다 (app_main 중복 정의 방지).
# 테스트를 빌드하려면 아래 두 REMOVE_ITEM 줄을 서로 바꾼다.
#
# REQUIRES 를 적지 않은 것은 의도적이다 — main 컴포넌트는 빌드에 포함된
# 모든 컴포넌트를 자동으로 의존하므로 원본과 동일하게 생략한다.

FILE(GLOB_RECURSE app_sources ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

list(REMOVE_ITEM app_sources "${CMAKE_CURRENT_SOURCE_DIR}/test_all_on.c")
# list(REMOVE_ITEM app_sources "${CMAKE_CURRENT_SOURCE_DIR}/main.c")

idf_component_register(
    SRCS         ${app_sources}
    INCLUDE_DIRS "../include"
)
