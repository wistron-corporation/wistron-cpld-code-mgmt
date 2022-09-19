#include "config.h"

#include "serialize.hpp"

#include <cereal/archives/json.hpp>
#include <sdbusplus/server.hpp>
#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/lg2.hpp>

#include <filesystem>
#include <fstream>

namespace wistron
{
namespace software
{
namespace updater
{

PHOSPHOR_LOG2_USING;
using namespace phosphor::logging;

void storeToFile(const std::string& versionId, uint8_t priority)
{
    auto bus = sdbusplus::bus::new_default();

    if (!std::filesystem::is_directory(PERSIST_DIR))
    {
        std::filesystem::create_directories(PERSIST_DIR);
    }

    // store one copy in /var/lib/obmc/wistron-pnor-code-mgmt/[versionId]
    auto varPath = PERSIST_DIR + versionId;
    std::ofstream varOutput(varPath.c_str());
    cereal::JSONOutputArchive varArchive(varOutput);
    varArchive(cereal::make_nvp("priority", priority));

    if (std::filesystem::is_directory(CPLD_SVF_PREFIX + versionId))
    {
        // store another copy in /media/cpld-[versionId]/[versionId]
        auto svfPath = CPLD_SVF_PREFIX + versionId + "/" + versionId;
        std::ofstream svfOutput(svfPath.c_str());
        cereal::JSONOutputArchive svfArchive(svfOutput);
        svfArchive(cereal::make_nvp("priority", priority));
    }
}

bool restoreFromFile(const std::string& versionId, uint8_t& priority)
{
    auto varPath = PERSIST_DIR + versionId;
    if (std::filesystem::exists(varPath))
    {
        std::ifstream varInput(varPath.c_str(), std::ios::in);
        try
        {
            cereal::JSONInputArchive varArchive(varInput);
            varArchive(cereal::make_nvp("priority", priority));
            return true;
        }
        catch (const cereal::RapidJSONException& e)
        {
            std::filesystem::remove(varPath);
        }
    }

    return false;
}

void removeFile(const std::string& versionId)
{
    // Delete the file /var/lib/wistron-cpld-code-mgmt/[versionId].
    // Note that removeFile() is called in the case of a version being deleted,
    // so the file /media/cpld-[versionId]/[versionId] will also be deleted
    // along with its surrounding directory.
    std::string path = PERSIST_DIR + versionId;
    if (std::filesystem::exists(path))
    {
        std::filesystem::remove(path);
    }
}

} // namespace updater
} // namespace software
} // namespace wistron
