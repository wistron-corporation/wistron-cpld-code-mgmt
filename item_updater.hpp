#pragma once

#include "activation.hpp"
#include "version.hpp"
#include "xyz/openbmc_project/Collection/DeleteAll/server.hpp"

#include <sdbusplus/server.hpp>
#include <xyz/openbmc_project/Association/Definitions/server.hpp>
#include <xyz/openbmc_project/Common/FactoryReset/server.hpp>
#include <xyz/openbmc_project/Object/Enable/server.hpp>

#include <string>

namespace wistron
{
namespace software
{
namespace updater
{

using ItemUpdaterInherit = sdbusplus::server::object_t<
    sdbusplus::xyz::openbmc_project::Common::server::FactoryReset,
    sdbusplus::xyz::openbmc_project::Association::server::Definitions,
    sdbusplus::xyz::openbmc_project::Collection::server::DeleteAll>;

namespace MatchRules = sdbusplus::bus::match::rules;

using VersionClass = wistron::software::updater::Version;
using AssociationList =
    std::vector<std::tuple<std::string, std::string, std::string>>;

/** @class ItemUpdater
 *  @brief Manages the activation of the host version items.
 */
class ItemUpdater : public ItemUpdaterInherit
{
  public:
    /** @brief Constructs ItemUpdater
     *
     * @param[in] bus    - The D-Bus bus object
     * @param[in] path   - The D-Bus path
     */
    ItemUpdater(sdbusplus::bus_t& bus, const std::string& path) :
        ItemUpdaterInherit(bus, path.c_str()), bus(bus),
        versionMatch(bus,
                     MatchRules::interfacesAdded() +
                         MatchRules::path(SOFTWARE_OBJPATH),
                     std::bind(std::mem_fn(&ItemUpdater::createActivation),
                               this, std::placeholders::_1))
    {
        processCPLDSvf();

        // Emit deferred signal.
        emit_object_added();
    }

    ~ItemUpdater() = default;

    /** @brief Sets the given priority free by incrementing
     *  any existing priority with the same value by 1. It will then continue
     *  to resolve duplicate priorities caused by this increase, by increasing
     *  the priority by 1 until there are no more duplicate values.
     *
     *  @param[in] value - The priority that needs to be set free.
     *  @param[in] versionId - The Id of the version for which we
     *                         are trying to free up the priority.
     *  @return None
     */
    void freePriority(uint8_t value, const std::string& versionId);

    /**
     * @brief Create and populate the active PNOR Version.
     */
    void processCPLDSvf();

    /** @brief Deletes version
     *
     *  @param[in] entryId - Id of the version to delete
     *
     *  @return - Returns true if the version is deleted.
     */
    bool erase(std::string entryId);

    /**
     * @brief Erases any non-active cpld versions.
     */
    void deleteAll();

    /** @brief Brings the total number of active PNOR versions to
     *         ACTIVE_PNOR_MAX_ALLOWED -1. This function is intended to be
     *         run before activating a new PNOR version. If this function
     *         needs to delete any PNOR version(s) it will delete the
     *         version(s) with the highest priority, skipping the
     *         functional PNOR version.
     *
     *  @return - Return if space is freed or not
     */
    bool freeSpace();

    /** @brief Creates an active association to the
     *  newly active software .svf
     *
     * @param[in]  path - The path to create the association to.
     */
    void createActiveAssociation(const std::string& path);

    /** @brief Creates a updateable association to the
     *  "running" CPLD software .svf
     *
     * @param[in]  path - The path to create the association.
     */
    void createUpdateableAssociation(const std::string& path);

    /** @brief Updates the functional association to the
     *  new "running" CPLD .svf
     *
     * @param[in]  versionId - The id of the .svf to update the association to.
     */
    void updateFunctionalAssociation(const std::string& versionId);

    /** @brief Removes the associations from the provided software .svf path
     *
     * @param[in]  path - The path to remove the association from.
     */
    void removeAssociation(const std::string& path);

    /** @brief Check whether the provided .svf id is the functional one
     *
     * @param[in] - versionId - The id of the .svf to check.
     *
     * @return - Returns true if this version is currently functional.
     */
    bool isVersionFunctional(const std::string& versionId);

    /** @brief Determine the software version id
     *         from the symlink target (e.g. /media/ro-2a1022fe).
     *
     * @param[in] symlinkPath - The path of the symlink.
     * @param[out] id - The version id as a string.
     */
    static std::string determineId(const std::string& symlinkPath);

     /** @brief Callback function for Software.Version match.
     *  @details Creates an Activation D-Bus object.
     *
     * @param[in]  msg       - Data associated with subscribed signal
     */
    void createActivation(sdbusplus::message_t& msg);

    /** @brief Create Activation object */
    std::unique_ptr<Activation> createActivationObject(
        const std::string& path, const std::string& versionId,
        const std::string& extVersion,
        sdbusplus::xyz::openbmc_project::Software::server::Activation::
            Activations activationStatus,
        AssociationList& assocs);

    /** @brief Create Version object */
    std::unique_ptr<Version>
        createVersionObject(const std::string& objPath,
                            const std::string& versionId,
                            const std::string& versionString,
                            sdbusplus::xyz::openbmc_project::Software::server::
                                Version::VersionPurpose versionPurpose,
                            const std::string& filePath);

  private:
    /** @brief Persistent sdbusplus D-Bus bus connection. */
    sdbusplus::bus_t& bus;

    /** @brief Persistent map of Activation D-Bus objects and their
     * version id */
    std::map<std::string, std::unique_ptr<Activation>> activations;

    /** @brief Persistent map of Version D-Bus objects and their
     * version id */
    std::map<std::string, std::unique_ptr<Version>> versions;

    /** @brief sdbusplus signal match for Software.Version */
    sdbusplus::bus::match_t versionMatch;

    /** @brief This entry's associations */
    AssociationList assocs = {};

    /** @brief Host factory reset
     * Activation D-Bus object */
    void reset() override;

    /** @brief Creates a functional association to the
     *  "running" BMC software image
     *
     * @param[in]  path - The path to create the association to.
     */
    void createFunctionalAssociation(const std::string& path);
};

} // namespace updater
} // namespace software
} // namespace wistron
