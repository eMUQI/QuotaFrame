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

const char* page_name(Page page, DisplayPair pair)
{
    switch (page) {
    case Page::Codex: return provider_name(display_provider(pair, 0));
    case Page::Claude: return provider_name(display_provider(pair, 1));
    default: return "overview";
    }
}

}  // namespace usage_panel
