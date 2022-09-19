#include "config.h"

#include "activation.hpp"

#include "item_updater.hpp"
#include "serialize.hpp"

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/lg2.hpp>

#include <sdbusplus/exception.hpp>
#include <xyz/openbmc_project/Common/error.hpp>
#include <xyz/openbmc_project/Software/Version/error.hpp>
#include <filesystem>

namespace wistron
{
namespace software
{
namespace updater
{

namespace softwareServer = sdbusplus::xyz::openbmc_project::Software::server;

PHOSPHOR_LOG2_USING;
using namespace phosphor::logging;
using InternalFailure =
    sdbusplus::xyz::openbmc_project::Common::Error::InternalFailure;

constexpr auto SYSTEMD_SERVICE = "org.freedesktop.systemd1";
constexpr auto SYSTEMD_OBJ_PATH = "/org/freedesktop/systemd1";

void Activation::subscribeToSystemdSignals()
{
    auto method = this->bus.new_method_call(SYSTEMD_SERVICE, SYSTEMD_OBJ_PATH,
                                            SYSTEMD_INTERFACE, "Subscribe");
    try
    {
        this->bus.call_noreply(method);
    }
    catch (const sdbusplus::exception_t& e)
    {
        if (e.name() != nullptr &&
            strcmp("org.freedesktop.systemd1.AlreadySubscribed", e.name()) == 0)
        {
            // If an Activation attempt fails, the Unsubscribe method is not
            // called. This may lead to an AlreadySubscribed error if the
            // Activation is re-attempted.
        }
        else
        {
            log<level::ERR>("Error subscribing to systemd",
                            entry("ERROR=%s", e.what()));
        }
    }
    return;
}

void Activation::unsubscribeFromSystemdSignals()
{
    auto method = this->bus.new_method_call(SYSTEMD_SERVICE, SYSTEMD_OBJ_PATH,
                                            SYSTEMD_INTERFACE, "Unsubscribe");
    this->bus.call_noreply(method);

    return;
}

void Activation::deleteImageManagerObject()
{
    // Get the Delete object for <versionID> inside image_manager
    constexpr auto versionServiceStr = "xyz.openbmc_project.Software.Version";
    constexpr auto deleteInterface = "xyz.openbmc_project.Object.Delete";
    std::string versionService;
    auto method = this->bus.new_method_call(MAPPER_BUSNAME, MAPPER_PATH,
                                            MAPPER_INTERFACE, "GetObject");

    method.append(path);
    method.append(std::vector<std::string>({deleteInterface}));

    std::map<std::string, std::vector<std::string>> mapperResponse;

    try
    {
        auto mapperResponseMsg = bus.call(method);
        mapperResponseMsg.read(mapperResponse);
        if (mapperResponse.begin() == mapperResponse.end())
        {
            log<level::ERR>("ERROR in reading the mapper response",
                            entry("VERSIONPATH=%s", path.c_str()));
            return;
        }
    }
    catch (const sdbusplus::exception_t& e)
    {
        log<level::ERR>("Error in Get Delete Object",
                        entry("VERSIONPATH=%s", path.c_str()));
        return;
    }

    // We need to find the wistron-cpld-software-manager's version service
    // to invoke the delete interface
    for (auto resp : mapperResponse)
    {
        if (resp.first.find(versionServiceStr) != std::string::npos)
        {
            versionService = resp.first;
        }
    }

    if (versionService.empty())
    {
        log<level::ERR>("Error finding version service");
        return;
    }

    // Call the Delete object for <versionID> inside image_manager
    method = this->bus.new_method_call(versionService.c_str(), path.c_str(),
                                       deleteInterface, "Delete");
    try
    {
        bus.call(method);
    }
    catch (const sdbusplus::exception_t& e)
    {
        if (e.name() != nullptr && strcmp("System.Error.ELOOP", e.name()) == 0)
        {
            // TODO: Error being tracked with openbmc/openbmc#3311
        }
        else
        {
            log<level::ERR>("Error performing call to Delete object path",
                            entry("ERROR=%s", e.what()),
                            entry("PATH=%s", path.c_str()));
        }
        return;
    }
}

bool Activation::checkApplyTimeImmediate()
{
    auto service = utils::getService(bus, applyTimeObjPath, applyTimeIntf);
    if (service.empty())
    {
        log<level::INFO>("Error getting the service name for Host .svf "
                         "ApplyTime. The Host needs to be manually rebooted to "
                         "complete the .svf activation if needed "
                         "immediately.");
    }
    else
    {

        auto method = bus.new_method_call(service.c_str(), applyTimeObjPath,
                                          dbusPropIntf, "Get");
        method.append(applyTimeIntf, applyTimeProp);

        try
        {
            auto reply = bus.call(method);

            std::variant<std::string> result;
            reply.read(result);
            auto applyTime = std::get<std::string>(result);
            if (applyTime == applyTimeImmediate)
            {
                return true;
            }
        }
        catch (const sdbusplus::exception_t& e)
        {
            log<level::ERR>("Error in getting ApplyTime",
                            entry("ERROR=%s", e.what()));
        }
    }
    return false;
}

uint8_t RedundancyPriority::priority(uint8_t value)
{
    storeToFile(parent.versionId, value);
    parent.parent.freePriority(value, parent.versionId);
    return softwareServer::RedundancyPriority::priority(value);
}

auto Activation::activation(Activations value) -> Activations
{
    if ((value != softwareServer::Activation::Activations::Active) &&
        (value != softwareServer::Activation::Activations::Activating))
    {
        redundancyPriority.reset(nullptr);
    }

    if (value == softwareServer::Activation::Activations::Activating)
    {
        parent.freeSpace();
        softwareServer::Activation::activation(value);

        if (svfCreated == false)
        {
            // Enable systemd signals
            subscribeToSystemdSignals();
            startActivation();
            return softwareServer::Activation::activation(value);
        }
        else if (svfCreated == true)
        {
            if (std::filesystem::is_directory(CPLD_SVF_PREFIX + versionId))
            {
                finishActivation();
                return softwareServer::Activation::activation(
                    softwareServer::Activation::Activations::Active);
            }
            else
            {
                activationBlocksTransition.reset(nullptr);
                activationProgress.reset(nullptr);
                return softwareServer::Activation::activation(
                    softwareServer::Activation::Activations::Failed);
            }
        }
    }
    else
    {
        activationBlocksTransition.reset(nullptr);
        activationProgress.reset(nullptr);
    }
    return softwareServer::Activation::activation(value);
}

auto Activation::requestedActivation(RequestedActivations value)
    -> RequestedActivations
{
    svfCreated = false;

    if ((value == softwareServer::Activation::RequestedActivations::Active) &&
        (softwareServer::Activation::requestedActivation() !=
         softwareServer::Activation::RequestedActivations::Active))
    {
        if ((softwareServer::Activation::activation() ==
             softwareServer::Activation::Activations::Ready) ||
            (softwareServer::Activation::activation() ==
             softwareServer::Activation::Activations::Failed))
        {
            Activation::activation(
                softwareServer::Activation::Activations::Activating);
        }
    }
    return softwareServer::Activation::requestedActivation(value);
}

void Activation::startActivation()
{
    if (!activationProgress)
    {
        activationProgress = std::make_unique<ActivationProgress>(bus, path);
    }

    if (!activationBlocksTransition)
    {
        activationBlocksTransition =
            std::make_unique<ActivationBlocksTransition>(bus, path);
    }

    auto svfUpadteServiceFile = "obmc-cpld-update-fw@" + versionId + ".service";
    auto method = bus.new_method_call(SYSTEMD_BUSNAME, SYSTEMD_PATH,
                                      SYSTEMD_INTERFACE, "StartUnit");
    method.append(svfUpadteServiceFile, "replace");
    
    try
    {
        auto reply = bus.call(method);
    }
    catch (const sdbusplus::exception::exception& e)
    {
        error("Error in trying to upgrade CPLD firmware: {ERROR}", "ERROR", e);
        report<InternalFailure>();
    }

    activationProgress->progress(10);
}

void Activation::unitStateChange(sdbusplus::message_t& msg)
{
    if (softwareServer::Activation::activation() !=
        softwareServer::Activation::Activations::Activating)
    {
        return;
    }

    uint32_t newStateID{};
    sdbusplus::message::object_path newStateObjPath;
    std::string newStateUnit{};
    std::string newStateResult{};

    // Read the msg and populate each variable
    msg.read(newStateID, newStateObjPath, newStateUnit, newStateResult);

    auto svfUpadteServiceFile = "obmc-cpld-update-fw@" + versionId + ".service";

    if (newStateUnit == svfUpadteServiceFile)
    {
        if(newStateResult == "done")
        {
            svfCreated = true;
            activationProgress->progress(80);
        }
        else if (newStateResult == "failed" || newStateResult == "dependency")
        {
            activation(softwareServer::Activation::Activations::Failed);
        }
    }

    if (svfCreated)
    {
        activation(softwareServer::Activation::Activations::Activating);
    }

    return;
}

void Activation::finishActivation()
{
    activationProgress->progress(90);

    // Set Redundancy Priority before setting to Active
    if (!redundancyPriority)
    {
        redundancyPriority =
            std::make_unique<RedundancyPriority>(bus, path, *this, 0);
    }

    activationProgress->progress(100);

    activationBlocksTransition.reset(nullptr);
    activationProgress.reset(nullptr);

    svfCreated = false;
    unsubscribeFromSystemdSignals();
    // Remove version object from .svf manager
    deleteImageManagerObject();
    // Create active association
    parent.createActiveAssociation(path);
    // Create updateable association as this
    // can be re-programmed.
    parent.createUpdateableAssociation(path);

    parent.updateFunctionalAssociation(versionId);
    info("CPLD firmware update complete.");
}

} // namespace updater
} // namespace software
} // namespace wistron
