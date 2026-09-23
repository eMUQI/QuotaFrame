#include "usage_ble/app_events.hpp"

namespace usage_panel {

bool AppEventQueue::begin(size_t depth)
{
    queue_ = xQueueCreate(depth, sizeof(AppEvent));
    return queue_ != nullptr;
}

bool AppEventQueue::send_usage(const UsageUpdate& value)
{
    AppEvent event{};
    event.type = AppEventType::Usage;
    event.usage = value;
    return queue_ && xQueueSend(queue_, &event, 0) == pdTRUE;
}

bool AppEventQueue::send_link(const LinkUpdate& value)
{
    AppEvent event{};
    event.type = AppEventType::Link;
    event.link = value;
    return queue_ && xQueueSend(queue_, &event, 0) == pdTRUE;
}

bool AppEventQueue::send_time_sync(const TimeSyncUpdate& value)
{
    AppEvent event{};
    event.type = AppEventType::TimeSync;
    event.time_sync = value;
    return queue_ && xQueueSend(queue_, &event, 0) == pdTRUE;
}

bool AppEventQueue::send_screen_toggle()
{
    AppEvent event{};
    event.type = AppEventType::ScreenToggle;
    return queue_ && xQueueSend(queue_, &event, 0) == pdTRUE;
}

bool AppEventQueue::send_screen_page(bool previous)
{
    AppEvent event{};
    event.type = AppEventType::ScreenPage;
    event.previous_page = previous;
    return queue_ && xQueueSend(queue_, &event, 0) == pdTRUE;
}

bool AppEventQueue::receive(AppEvent& event, TickType_t wait)
{
    return queue_ && xQueueReceive(queue_, &event, wait) == pdTRUE;
}

}  // namespace usage_panel
