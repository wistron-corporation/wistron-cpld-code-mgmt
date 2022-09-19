#include "version.hpp"

#include "item_updater.hpp"
#include "xyz/openbmc_project/Common/error.hpp"

#include <openssl/evp.h>

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/log.hpp>

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace wistron
{
namespace software
{
namespace updater
{

using namespace sdbusplus::xyz::openbmc_project::Common::Error;
using namespace phosphor::logging;
using Argument = xyz::openbmc_project::Common::InvalidArgument;

using EVP_MD_CTX_Ptr =
    std::unique_ptr<EVP_MD_CTX, decltype(&::EVP_MD_CTX_free)>;

std::string Version::getId(const std::string& version)
{

    if (version.empty())
    {
        log<level::ERR>("Error version is empty");
        return {};
    }

    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    EVP_MD_CTX_Ptr ctx(EVP_MD_CTX_new(), &::EVP_MD_CTX_free);

    EVP_DigestInit(ctx.get(), EVP_sha512());
    EVP_DigestUpdate(ctx.get(), version.c_str(), strlen(version.c_str()));
    EVP_DigestFinal(ctx.get(), digest.data(), nullptr);

    // We are only using the first 8 characters.
    char mdString[9];
    snprintf(mdString, sizeof(mdString), "%02x%02x%02x%02x",
             (unsigned int)digest[0], (unsigned int)digest[1],
             (unsigned int)digest[2], (unsigned int)digest[3]);

    return mdString;
}

std::map<std::string, std::string>
    Version::getValue(const std::string& filePath,
                      std::map<std::string, std::string> keys)
{
    if (filePath.empty())
    {
        log<level::ERR>("Error filePath is empty");
        elog<InvalidArgument>(Argument::ARGUMENT_NAME("FilePath"),
                              Argument::ARGUMENT_VALUE(filePath.c_str()));
    }

    std::ifstream efile;
    std::string line;
    efile.exceptions(std::ifstream::failbit | std::ifstream::badbit |
                     std::ifstream::eofbit);

    try
    {
        efile.open(filePath);
        while (getline(efile, line))
        {
            for (auto& key : keys)
            {
                auto value = key.first + "=";
                auto keySize = value.length();
                if (line.compare(0, keySize, value) == 0)
                {
                    key.second = line.substr(keySize);
                    break;
                }
            }
        }
        efile.close();
    }
    catch (const std::exception& e)
    {
        if (!efile.eof())
        {
            log<level::ERR>("Error in reading file");
        }
        efile.close();
    }

    return keys;
}

std::string Version::getCPLDVersion(const std::string& releaseFilePath)
{
    std::string versionKey1 = "VERSION_ID=";
    std::string versionKey2 = "version=";
    std::string versionValue{};
    std::string version{};
    std::ifstream efile;
    std::string line;
    efile.open(releaseFilePath);

    while (getline(efile, line))
    {
        if (line.substr(0, versionKey1.size()).find(versionKey1) !=
            std::string::npos)
        {
            // Support quoted and unquoted values
            // 1. Remove the versionKey so that we process the value only.
            versionValue = line.substr(versionKey1.size());

            // 2. Look for a starting quote, then increment the position by 1 to
            //    skip the quote character. If no quote is found,
            //    find_first_of() returns npos (-1), which by adding +1 sets pos
            //    to 0 (beginning of unquoted string).
            std::size_t pos = versionValue.find_first_of('"') + 1;

            // 3. Look for ending quote, then decrease the position by pos to
            //    get the size of the string up to before the ending quote. If
            //    no quote is found, find_last_of() returns npos (-1), and pos
            //    is 0 for the unquoted case, so substr() is called with a len
            //    parameter of npos (-1) which according to the documentation
            //    indicates to use all characters until the end of the string.
            version =
                versionValue.substr(pos, versionValue.find_last_of('"') - pos);
            break;
        }
        else if (line.substr(0, versionKey2.size()).find(versionKey2) !=
            std::string::npos)
        {
            versionValue = line.substr(versionKey2.size());
            std::size_t pos = versionValue.find_first_of('"') + 1;
            version =
                versionValue.substr(pos, versionValue.find_last_of('"') - pos);
            break;
        }
    }
    efile.close();

    if (version.empty())
    {
        log<level::ERR>("CPLD current version is empty");
        elog<InternalFailure>();
    }

    return version;
}

bool Version::isFunctional()
{
    return versionStr == getCPLDVersion(CPLD_RELEASE_FILE);
}

void Delete::delete_()
{
    if (parent.eraseCallback)
    {
        parent.eraseCallback(parent.getId(parent.version()));
    }
}

void Version::updateDeleteInterface(sdbusplus::message_t& msg)
{
    std::string interface, chassisState;
    std::map<std::string, std::variant<std::string>> properties;

    msg.read(interface, properties);

    for (const auto& p : properties)
    {
        if (p.first == "CurrentPowerState")
        {
            chassisState = std::get<std::string>(p.second);
        }
    }
    if (chassisState.empty())
    {
        // The chassis power state property did not change, return.
        return;
    }

    if ((parent.isVersionFunctional(this->versionId)) &&
        (chassisState != CHASSIS_STATE_OFF))
    {
        if (deleteObject)
        {
            deleteObject.reset(nullptr);
        }
    }
    else
    {
        if (!deleteObject)
        {
            deleteObject = std::make_unique<Delete>(bus, objPath, *this);
        }
    }
}

} // namespace updater
} // namespace software
} // namespace wistron
