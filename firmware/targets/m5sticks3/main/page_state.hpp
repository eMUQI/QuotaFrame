#pragma once

#include <cstdint>

namespace usage_panel {

enum class Page : uint8_t { Overview, Codex, Claude };

Page next_page(Page page);
const char* page_name(Page page);

}  // namespace usage_panel
