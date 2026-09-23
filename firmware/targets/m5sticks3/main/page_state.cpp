#include "page_state.hpp"

namespace usage_panel {

Page next_page(Page page)
{
    switch (page) {
    case Page::Overview: return Page::Codex;
    case Page::Codex: return Page::Claude;
    default: return Page::Overview;
    }
}

const char* page_name(Page page)
{
    switch (page) {
    case Page::Codex: return "codex";
    case Page::Claude: return "claude";
    default: return "overview";
    }
}

}  // namespace usage_panel
