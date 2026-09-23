#include "button_controller.hpp"
#include "page_state.hpp"
#include "unity.h"

using namespace usage_panel;

TEST_CASE("page cycle and protocol names stay stable", "[page]")
{
    TEST_ASSERT_EQUAL_UINT8(uint8_t(Page::Codex), uint8_t(next_page(Page::Overview)));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(Page::Claude), uint8_t(next_page(Page::Codex)));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(Page::Overview), uint8_t(next_page(Page::Claude)));
    TEST_ASSERT_EQUAL_STRING("overview", page_name(Page::Overview));
    TEST_ASSERT_EQUAL_STRING("codex", page_name(Page::Codex));
    TEST_ASSERT_EQUAL_STRING("claude", page_name(Page::Claude));
}

TEST_CASE("front button advances the device page", "[button]")
{
    TEST_ASSERT_EQUAL_UINT8(
        uint8_t(Page::Codex), uint8_t(handle_front_button(Page::Overview)));
}
