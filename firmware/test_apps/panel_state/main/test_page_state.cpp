#include "usage_panel_state/page_state.hpp"
#include "unity.h"

using namespace usage_panel;

TEST_CASE("tabs map directly and invalid tabs preserve the page", "[panel_page]")
{
    TEST_ASSERT_EQUAL_UINT8(
        uint8_t(Page::Overview),
        uint8_t(page_from_tab(0, Page::Claude)));
    TEST_ASSERT_EQUAL_UINT8(
        uint8_t(Page::Codex),
        uint8_t(page_from_tab(1, Page::Overview)));
    TEST_ASSERT_EQUAL_UINT8(
        uint8_t(Page::Claude),
        uint8_t(page_from_tab(2, Page::Overview)));
    TEST_ASSERT_EQUAL_UINT8(
        uint8_t(Page::Claude),
        uint8_t(page_from_tab(3, Page::Claude)));
}

TEST_CASE("swipes cycle through all pages in both directions", "[panel_page]")
{
    TEST_ASSERT_EQUAL_UINT8(
        uint8_t(Page::Claude),
        uint8_t(page_after_swipe(Page::Overview, SwipeDirection::Previous)));
    TEST_ASSERT_EQUAL_UINT8(
        uint8_t(Page::Codex),
        uint8_t(page_after_swipe(Page::Overview, SwipeDirection::Next)));
    TEST_ASSERT_EQUAL_UINT8(
        uint8_t(Page::Claude),
        uint8_t(page_after_swipe(Page::Codex, SwipeDirection::Next)));
    TEST_ASSERT_EQUAL_UINT8(
        uint8_t(Page::Overview),
        uint8_t(page_after_swipe(Page::Claude, SwipeDirection::Next)));
    TEST_ASSERT_EQUAL_UINT8(
        uint8_t(Page::Codex),
        uint8_t(page_after_swipe(Page::Claude, SwipeDirection::Previous)));
}

TEST_CASE("page protocol names stay stable", "[panel_page]")
{
    TEST_ASSERT_EQUAL_STRING("overview", page_name(Page::Overview));
    TEST_ASSERT_EQUAL_STRING("codex", page_name(Page::Codex));
    TEST_ASSERT_EQUAL_STRING("claude", page_name(Page::Claude));
}
