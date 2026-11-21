#pragma once

#include <cstdint>

#include "usage_core/usage_state.hpp"

namespace usage_panel {

enum class Page : uint8_t { Overview, Codex, Claude };

Page next_page(Page page);
const char* page_name(Page page, DisplayPair pair = DisplayPair::CodexClaude);

}  // namespace usage_panel
