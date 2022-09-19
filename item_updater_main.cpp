#include "config.h"

#include "item_updater.hpp"
#include "watch.hpp"

#include <CLI/CLI.hpp>
#include <phosphor-logging/log.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/server/manager.hpp>
#include <sdeventplus/event.hpp>

#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace wistron
{
namespace software
{
namespace updater
{
void initializeService(sdbusplus::bus_t& bus)
{
    static sdbusplus::server::manager_t objManager(bus, SOFTWARE_OBJPATH);
    static wistron::software::updater::ItemUpdater updater(bus, SOFTWARE_OBJPATH);
    static Watch watch(
        bus.get_event(),
        std::bind(std::mem_fn(&ItemUpdater::updateFunctionalAssociation),
                  &updater, std::placeholders::_1));
    bus.request_name(BUSNAME_UPDATER);
}
} // namespace updater
} // namespace software
} // namespace openpower

int main(int argc, char* argv[])
{
    using namespace wistron::software::updater;
    using namespace phosphor::logging;
    auto bus = sdbusplus::bus::new_default();
    auto loop = sdeventplus::Event::get_default();

    bus.attach_event(loop.get(), SD_EVENT_PRIORITY_NORMAL);

    CLI::App app{"OpenPOWER host firmware manager"};
    
    CLI11_PARSE(app, argc, argv);

    initializeService(bus);

    try
    {
        auto rc = loop.loop();
        if (rc < 0)
        {
            log<level::ERR>("Error occurred during the sd_event_loop",
                            entry("RC=%d", rc));
            return -1;
        }
    }
    catch (const std::system_error& e)
    {
        log<level::ERR>(e.what());
        return -1;
    }

    return 0;
}
