#include "config.h"

#include "item_updater.hpp"
#include "activation.hpp"
#include "serialize.hpp"
#include "utils.hpp"
#include "version.hpp"

#include "xyz/openbmc_project/Common/error.hpp"

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/lg2.hpp>

#include <xyz/openbmc_project/Common/error.hpp>
#include <xyz/openbmc_project/Software/Image/error.hpp>

#include <filesystem>
#include <fstream>
#include <queue>
#include <set>
#include <string>

namespace wistron
{
namespace software
{
namespace updater
{
namespace server = sdbusplus::xyz::openbmc_project::Software::server;
namespace fs = std::filesystem;

PHOSPHOR_LOG2_USING;
using namespace phosphor::logging;
using namespace sdbusplus::xyz::openbmc_project::Common::Error;

std::unique_ptr<Activation> ItemUpdater::createActivationObject(
    const std::string& path, const std::string& versionId,
    const std::string& extVersion,
    sdbusplus::xyz::openbmc_project::Software::server::Activation::Activations
        activationStatus,
    AssociationList& assocs)
{
    return std::make_unique<Activation>(
        bus, path, *this, versionId, extVersion, activationStatus, assocs);
}

std::unique_ptr<Version> ItemUpdater::createVersionObject(
    const std::string& objPath, const std::string& versionId,
    const std::string& versionString,
    sdbusplus::xyz::openbmc_project::Software::server::Version::VersionPurpose
        versionPurpose,
    const std::string& filePath)
{
    auto version = std::make_unique<Version>(
        bus, objPath, *this, versionId, versionString, versionPurpose, filePath,
        std::bind(&ItemUpdater::erase, this, std::placeholders::_1));
    version->deleteObject = std::make_unique<Delete>(bus, objPath, *version);
    return version;
}

void ItemUpdater::createActivation(sdbusplus::message_t& m)
{
    using SVersion = server::Version;
    using VersionPurpose = SVersion::VersionPurpose;

    sdbusplus::message::object_path objPath;
    std::map<std::string, std::map<std::string, std::variant<std::string>>>
        interfaces;
    m.read(objPath, interfaces);

    std::string path(std::move(objPath));
    std::string filePath;
    auto purpose = VersionPurpose::Unknown;
    std::string version;

    for (const auto& intf : interfaces)
    {
        if (intf.first == VERSION_IFACE)
        {
            for (const auto& property : intf.second)
            {
                if (property.first == "Purpose")
                {
                    // Only process the Host and System images
                    auto value = SVersion::convertVersionPurposeFromString(
                        std::get<std::string>(property.second));

                    if (value == VersionPurpose::CPLD)
                    {
                        purpose = value;
                    }
                }
                else if (property.first == "Version")
                {
                    version = std::get<std::string>(property.second);
                }
            }
        }
        else if (intf.first == FILEPATH_IFACE)
        {
            for (const auto& property : intf.second)
            {
                if (property.first == "Path")
                {
                    filePath = std::get<std::string>(property.second);
                }
            }
        }
    }
    if ((filePath.empty()) || (purpose == VersionPurpose::Unknown))
    {
        return;
    }

    // Version id is the last item in the path
    auto pos = path.rfind("/");
    if (pos == std::string::npos)
    {
        error("No version id found in object path : {OBJPATH}", "OBJPATH", path.c_str());
        return;
    }

    auto versionId = path.substr(pos + 1);

    if (activations.find(versionId) == activations.end())
    {
        // Determine the Activation state by processing the given .svf dir.
        auto activationState = server::Activation::Activations::Invalid;
        AssociationList associations = {};

        activationState = server::Activation::Activations::Ready;
        // Create an association to the host inventory item
        associations.emplace_back(std::make_tuple(
            ACTIVATION_FWD_ASSOCIATION, ACTIVATION_REV_ASSOCIATION,
            CPLD_INVENTORY_PATH));

        fs::path manifestPath(filePath);
        manifestPath /= MANIFEST_FILE_NAME;
        std::string extendedVersion =
            (Version::getValue(
                 manifestPath.string(),
                 std::map<std::string, std::string>{{"extended_version", ""}}))
                .begin()
                ->second;

        auto activation = createActivationObject(
            path, versionId, extendedVersion, activationState, associations);
        activations.emplace(versionId, std::move(activation));

        auto versionPtr =
            createVersionObject(path, versionId, version, purpose, filePath);
        versions.emplace(versionId, std::move(versionPtr));
    }
    return;
}

void ItemUpdater::processCPLDSvf()
{
    // Check MEDIA_DIR and create if it does not exist
    try
    {
        if (!fs::is_directory(MEDIA_DIR))
        {
            fs::create_directory(MEDIA_DIR);
        }
    }
    catch (const fs::filesystem_error& e)
    {
        error("Failed to prepare dir");
        return;
    }

    // Read os-release from /etc/ to get the functional CPLD version
    auto functionalVersion = VersionClass::getCPLDVersion(CPLD_RELEASE_FILE);

    // Read pnor.toc from folders under /media/
    // to get Active Software Versions.
    for (const auto& iter : std::filesystem::directory_iterator(MEDIA_DIR))
    {
        auto activationState = server::Activation::Activations::Active;

        static const auto CPLD_SVF_PREFIX_LEN = strlen(CPLD_SVF_PREFIX);

        // Check if the CPLD_SVF_PREFIX is the prefix of the iter.path
        if (0 ==
            iter.path().native().compare(0, CPLD_SVF_PREFIX_LEN, CPLD_SVF_PREFIX))
        {
            // The versionId is extracted from the path
            // for example /media/cpld-2a1022fe.
            fs::path releaseFile(CPLD_RELEASE_FILE);
            auto cpldRelease = iter.path() / CPLD_RELEASE_FILE_NAME;

            if (!fs::is_regular_file(cpldRelease))
            {
                error("Failed to read cpldRelease {RELEASE}", "RELEASE", std::string(cpldRelease));

                // Try to get the version id from the mount directory name and
                // call to delete it as this version may be corrupted. 
                // The worst that can happen is that
                // erase() is called with an non-existent id and returns.
                auto id = iter.path().native().substr(CPLD_SVF_PREFIX_LEN);
                ItemUpdater::erase(id);
                continue;
            }

            auto version = VersionClass::getCPLDVersion(cpldRelease);
            if (version.empty())
            {
                error("Failed to read version from cpldRelease");
                // Try to delete the version, same as above if the
                // CPLD_SVF_PREFIX_LEN does not exist.
                
                auto id = iter.path().native().substr(CPLD_SVF_PREFIX_LEN);
                ItemUpdater::erase(id);
                continue;
            }

            auto id = VersionClass::getId(version);

            auto purpose = server::Version::VersionPurpose::CPLD;

            // Read os-release from /etc/ to get the CPLD extended version
            std::string extendedVersion = "";
            auto path = fs::path(SOFTWARE_OBJPATH) / id;
            auto isFunctional = false;
            
            // Create functional association if this is the functional
            // version
            if (version.compare(functionalVersion) == 0)
            {
                isFunctional = true;
                createFunctionalAssociation(path);
            }

            AssociationList associations = {};

            if (activationState == server::Activation::Activations::Active)
            {
                // Create an association to the host inventory item
                associations.emplace_back(std::make_tuple(
                    ACTIVATION_FWD_ASSOCIATION, ACTIVATION_REV_ASSOCIATION,
                    CPLD_INVENTORY_PATH));

                // Create an active association since this cpld is active
                createActiveAssociation(path);            
            }

            // All updateable firmware components must expose the updateable
            // association.
            createUpdateableAssociation(path);

            // Create Activation instance for this version.
            activations.insert(
                std::make_pair(id, std::make_unique<Activation>(
                                       bus, path, *this, id, extendedVersion,
                                       activationState, associations)));

            // Create Version instance for this version.
            auto versionPtr = std::make_unique<Version>(
                bus, path, *this, id, version, purpose, "",
                std::bind(&ItemUpdater::erase, this, std::placeholders::_1));
            
            if (!isFunctional)
            {
                versionPtr->deleteObject =
                    std::make_unique<Delete>(bus, path, *versionPtr);
            }
           
            versions.insert(std::make_pair(id, std::move(versionPtr)));

            // If Active, create RedundancyPriority instance for this version.
            if (activationState == server::Activation::Activations::Active)
            {
                uint8_t priority = std::numeric_limits<uint8_t>::max();
                if (isFunctional)
                {
                    priority = 0;
                }
                else
                {
                    error(
                        "Unable to restore priority from file for {VERSIONID}",
                        "VERSIONID", id);
                }
                
                activations.find(id)->second->redundancyPriority =
                    std::make_unique<RedundancyPriority>(
                        bus, path, *(activations.find(id)->second), priority);
            }
        }
    }

    // Look at the cpld symlink to determine if there is a functional cpld
    auto id = determineId(CPLD_ACTIVE_DIR);
    if (!id.empty())
    {
        updateFunctionalAssociation(id);
    }
    return;
}

void ItemUpdater::createActiveAssociation(const std::string& path)
{
    assocs.emplace_back(
        std::make_tuple(ACTIVE_FWD_ASSOCIATION, ACTIVE_REV_ASSOCIATION, path));
    associations(assocs);
}

void ItemUpdater::createUpdateableAssociation(const std::string& path)
{
    assocs.emplace_back(std::make_tuple(UPDATEABLE_FWD_ASSOCIATION,
                                        UPDATEABLE_REV_ASSOCIATION, path));
    associations(assocs);
}

void ItemUpdater::createFunctionalAssociation(const std::string& path)
{
    assocs.emplace_back(std::make_tuple(FUNCTIONAL_FWD_ASSOCIATION,
                                        FUNCTIONAL_REV_ASSOCIATION, path));
    associations(assocs);
}

void ItemUpdater::updateFunctionalAssociation(const std::string& versionId)
{
    std::string path = std::string{SOFTWARE_OBJPATH} + '/' + versionId;
    // remove all functional associations
    for (auto iter = assocs.begin(); iter != assocs.end();)
    {
        if ((std::get<0>(*iter)).compare(FUNCTIONAL_FWD_ASSOCIATION) == 0)
        {
            iter = assocs.erase(iter);
        }
        else
        {
            ++iter;
        }
    }

    createFunctionalAssociation(path);
}

void ItemUpdater::removeAssociation(const std::string& path)
{
    for (auto iter = assocs.begin(); iter != assocs.end();)
    {
        if ((std::get<2>(*iter)).compare(path) == 0)
        {
            iter = assocs.erase(iter);
            associations(assocs);
        }
        else
        {
            ++iter;
        }
    }
}

bool ItemUpdater::isVersionFunctional(const std::string& versionId)
{
    if (!std::filesystem::exists(PERSIST_DIR))
    {
        return false;
    }

    std::filesystem::path activeCPLD =
        std::filesystem::read_symlink(PERSIST_DIR + std::string("/cpld"));

    if (!std::filesystem::is_directory(activeCPLD))
    {
        return false;
    }

    if (activeCPLD.string().find(versionId) == std::string::npos)
    {
        return false;
    }

    // active cpld is the version we're checking
    return true;
}

void ItemUpdater::freePriority(uint8_t value, const std::string& versionId)
{
    //  Versions with the lowest priority in front
    std::priority_queue<std::pair<int, std::string>,
                        std::vector<std::pair<int, std::string>>,
                        std::greater<std::pair<int, std::string>>>
        versionsPQ;

    for (const auto& intf : activations)
    {
        if (intf.second->redundancyPriority)
        {
            versionsPQ.push(std::make_pair(
                intf.second->redundancyPriority.get()->priority(),
                intf.second->versionId));
        }
    }

    while (!versionsPQ.empty())
    {
        if (versionsPQ.top().first == value &&
            versionsPQ.top().second != versionId)
        {
            // Increase priority by 1 and update its value
            ++value;
            storeToFile(versionsPQ.top().second, value);
            auto it = activations.find(versionsPQ.top().second);
            it->second->redundancyPriority.get()->sdbusplus::xyz::
                openbmc_project::Software::server::RedundancyPriority::priority(
                    value);
        }
        versionsPQ.pop();
    }
}


bool ItemUpdater::erase(std::string entryId)
{
    if (isVersionFunctional(entryId))
    {
        log<level::ERR>(("Error: Version " + entryId +
                         " is currently active."
                         " Unable to remove.")
                            .c_str());
        return false;
    }

    // Removing entry in versions map
    auto it = versions.find(entryId);
    if (it == versions.end())
    {
        log<level::ERR>(("Error: Failed to find version " + entryId +
                         " in item updater versions map."
                         " Unable to remove.")
                            .c_str());
        return false;
    }

    // Removing entry in activations map
    auto ita = activations.find(entryId);
    if (ita == activations.end())
    {
        log<level::ERR>(("Error: Failed to find version " + entryId +
                         " in item updater activations map."
                         " Unable to remove.")
                            .c_str());
        return false;
    }

    versions.erase(entryId);
    removeAssociation(ita->second->path);
    activations.erase(entryId);
    removeFile(entryId);

    return true;
}

void ItemUpdater::deleteAll()
{
    std::vector<std::string> deletableVersions;

    for (const auto& activationIt : activations)
    {
        if (isVersionFunctional(activationIt.first))
        {
            continue;
        }
        else
        {
            ItemUpdater::erase(activationIt.first);
        }
    }
}

bool ItemUpdater::freeSpace()
{
    bool isSpaceFreed = false;
    //  Versions with the highest priority in front
    std::priority_queue<std::pair<int, std::string>,
                        std::vector<std::pair<int, std::string>>,
                        std::less<std::pair<int, std::string>>>
        versionsPQ;

    std::size_t count = 0;
    for (const auto& iter : activations)
    {
        if ((iter.second.get()->activation() ==
             server::Activation::Activations::Active) ||
            (iter.second.get()->activation() ==
             server::Activation::Activations::Failed))
        {
            count++;
            // Don't put the functional version on the queue since we can't
            // remove the "running" CPLD version.
            // If ACTIVE_CPLD_MAX_ALLOWED <= 1, there is only one active CPLD,
            // so remove functional version as well.
            // Don't delete the the Activation object that called this function.
            if (versions.find(iter.second->versionId)
                     ->second->isFunctional() &&
                 ACTIVE_CPLD_MAX_ALLOWED > 1)
            {
                continue;
            }

            // Failed activations don't have priority, assign them a large value
            // for sorting purposes.
            auto priority = 999;
            if (iter.second.get()->activation() ==
                    server::Activation::Activations::Active &&
                iter.second->redundancyPriority)
            {
                priority = iter.second->redundancyPriority.get()->priority();
            }

            versionsPQ.push(std::make_pair(priority, iter.second->versionId));
        }
    }

    // If the number of CPLD versions is over ACTIVE_CPLD_MAX_ALLOWED -1,
    // remove the highest priority one(s).
    while ((count >= ACTIVE_CPLD_MAX_ALLOWED) && (!versionsPQ.empty()))
    {
        erase(versionsPQ.top().second);
        versionsPQ.pop();
        count--;
        isSpaceFreed = true;
    }

    return isSpaceFreed;
}

std::string ItemUpdater::determineId(const std::string& symlinkPath)
{
    if (!std::filesystem::exists(symlinkPath))
    {
        return {};
    }

    auto target = std::filesystem::canonical(symlinkPath).string();

    // check to make sure the target really exists
    if (!std::filesystem::is_regular_file(target + "/" + CPLD_RELEASE_FILE_NAME))
    {
        return {};
    }
    // Get the cpld <id> from the symlink target
    // for example /media/cpld-2a1022fe
    static const auto CPLD_SVF_PREFIX_LEN = strlen(CPLD_SVF_PREFIX);
    return target.substr(CPLD_SVF_PREFIX_LEN);
}

void ItemUpdater::reset()
{
    utils::hiomapdSuspend(bus);

    for (const auto& it : activations)
    {
        auto dir = CPLD_SVF_PREFIX + it.first;
        if (std::filesystem::is_directory(dir))
        {
            for (const auto& iter : std::filesystem::directory_iterator(dir))
            {
                std::filesystem::remove_all(iter);
            }
        }
    }

    utils::hiomapdResume(bus);
}

} // namespace updater
} // namespace software
} // namespace wistron
