#include "config.h"

#include "watch.hpp"

#include "item_updater.hpp"

#include <sys/inotify.h>
#include <unistd.h>

#include <phosphor-logging/log.hpp>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>

namespace wistron
{
namespace software
{
namespace updater
{

using namespace phosphor::logging;

Watch::Watch(sd_event* loop,
             std::function<void(const std::string&)> functionalCallback) :
    functionalCallback(functionalCallback),
    fd(inotifyInit())

{
    // Create PERSIST_DIR if doesn't exist.
    if (!std::filesystem::is_directory(PERSIST_DIR))
    {
        std::filesystem::create_directories(PERSIST_DIR);
    }

    wd = inotify_add_watch(fd(), PERSIST_DIR, IN_CREATE);
    if (-1 == wd)
    {
        auto error = errno;
        throw std::system_error(error, std::generic_category(),
                                "Error occurred during the inotify_init1");
    }

    decltype(eventSource.get()) sourcePtr = nullptr;
    auto rc = sd_event_add_io(loop, &sourcePtr, fd(), EPOLLIN, callback, this);

    eventSource.reset(sourcePtr);

    if (0 > rc)
    {
        throw std::system_error(-rc, std::generic_category(),
                                "Error occurred during the inotify_init1");
    }
}

Watch::~Watch()
{
    if ((-1 != fd()) && (-1 != wd))
    {
        inotify_rm_watch(fd(), wd);
    }
}

int Watch::callback(sd_event_source*, int fd, uint32_t revents, void* userdata)
{
    if (!(revents & EPOLLIN))
    {
        return 0;
    }

    constexpr auto maxBytes = 1024;
    uint8_t buffer[maxBytes];
    auto bytes = read(fd, buffer, maxBytes);
    if (0 > bytes)
    {
        auto error = errno;
        throw std::system_error(error, std::generic_category(),
                                "failed to read inotify event");
    }

    auto offset = 0;
    while (offset < bytes)
    {
        auto event = reinterpret_cast<inotify_event*>(&buffer[offset]);
        // Update the functional association on a cpld
        // active cpld symlink change
        std::filesystem::path path(CPLD_ACTIVE_DIR);
        path /= event->name;

        if (std::filesystem::is_directory(CPLD_ACTIVE_DIR) 
            && std::filesystem::equivalent(path, CPLD_ACTIVE_DIR))
        {
            auto id = ItemUpdater::determineId(path);
            static_cast<Watch*>(userdata)->functionalCallback(id);
        }
         
        offset += offsetof(inotify_event, name) + event->len;
    }

    return 0;
}

int Watch::inotifyInit()
{
    auto fd = inotify_init1(IN_NONBLOCK);

    if (-1 == fd)
    {
        auto error = errno;
        throw std::system_error(error, std::generic_category(),
                                "Error occurred during the inotify_init1");
    }

    return fd;
}

} // namespace updater
} // namespace software
} // namespace wistron
