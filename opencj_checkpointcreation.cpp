// File format:
// version=<num>
// route=<routeName>
// <idx>:<parentIdx>:<bigBrotherIdx>:<height>:<isFinish>:<isInAir>:<allowEle>:<allowAnyFPS>:<allowDoubleRPG>:<org0>:<org1>:<org2>:[org3]:[org4]:[org5]:[org6]:[org7]
// ^ this one repeats as many checkpoints there are
// org is in format of x,y,z
// indices are <0 if not applicable

// ===============================================================================
// Includes
// ===============================================================================

#include "shared.hpp"
#include <cctype>
#include <string>
#include <fstream>
#include <filesystem>
#include <algorithm>

// ===============================================================================
// Defines
// ===============================================================================

#define CPC_FILE_VERSION_1           1
#define CPC_FILE_VERSION             CPC_FILE_VERSION_1

// Keep the following values in sync with checkpointcreation.gsc
#define MAX_CHECKPOINTS_PER_ROUTE   512 // Arbitrary number for now, but cba make this whole code suitable for runtime re-allocation for now
#define MAX_ORGS_PER_CHECKPOINT       8
#define ROUTE_NAME_MAX_SIZE          20
#define FILE_LABEL_MAX_SIZE          32

// ===============================================================================
// Types
// ===============================================================================

typedef struct
{
    bool hasBigBrother; // Filled in by C code when a player adds another child checkpoint to the same parent
    bool hasParent; // Whether or not this checkpoint has a parent

    bool isFinish; // Whether or not this is (one of the) final checkpoint(s) for the route
    bool isInAir; // Whether or not this checkpoint can be triggered without the player being onGround
    bool allowEle; // Whether or not this checkpoint bypasses elevator restrictions without marking the run as "ele"
    bool allowAnyFPS; // Whether or not this checkpoint bypasses FPS restrictions without marking the run as "any FPS"
    bool allowDoubleRPG; // Whether or not this checkpoint bypass double RPG restrictions without pausing the player's run

    int bigBrotherIdx; // If checkpoint has a big brother, this is its index
    int parentIdx; // Index of the parent checkpoint for this checkpoint
    int height; // The height of the checkpoint

    int nrOrgs; // Number of filled in origins (so far)
    vec3_t orgs[MAX_ORGS_PER_CHECKPOINT];
} sCpcCheckpoint_t; // Keep in sync with checkpointcreation.gsc

typedef struct
{
    int nrCheckpoints; // The number of checkpoints the player has confirmed
    int selectedIdx; // Index of the checkpoint that is currently selected
    std::string strRouteName; // Name of the route the player is currently checkpointing for
    std::string strError; // In case of an error, this will contain the needed information for GSC to obtain
} sCpcInfo_t;

// ===============================================================================
// Global variables
// ===============================================================================

// Checkpoints that can be filled in by each client
static sCpcCheckpoint_t opencj_clientCpcCheckpoints[MAX_CLIENTS][MAX_CHECKPOINTS_PER_ROUTE] = {0};

// Global information, for example the route the player is currently checkpointing for
static sCpcInfo_t opencj_clientCpcInfo[MAX_CLIENTS] = {0}; 

// ===============================================================================
// Local (helper) functions
// ===============================================================================

using namespace std;

static float distance3D(const float *pOrg1, const float *pOrg2)
{
    float totalDiff = 0;
    for (int i = 0; i < 3; i++)
    {
        totalDiff += fabs(pOrg1[i] - pOrg2[i]);
    }
    return totalDiff;
}

static std::vector<std::string> split(std::string str, char delim)
{
    std::vector<std::string> result;
    std::string delimiter = std::string { delim };
    if (str.find(delimiter, 0) == string::npos)
    {
        return result;
    }

    size_t last = 0;
    size_t next = 0;
    while ((next = str.find(delimiter, last)) != string::npos)
    {
        result.push_back(str.substr(last, (next - last)));
        last = next + 1;
    }
    result.push_back(str.substr(last));

    return result;
}

static std::optional<std::string> customReadLine(int fd)
{
    char buf[256] = {0};
    int nrBytesRead = FS_ReadLine(buf, sizeof(buf), fd);
    if (nrBytesRead >= 0)
    {
        int len = strlen(buf);
        if (len > 0)
        {
            if (buf[len - 1] == '\n')
            {
                buf[len - 1] = '\0'; // Remove trailing newline
            }
        }
        return std::make_optional(std::string { buf });
    }

    return std::nullopt;
}

static void findAndSetBigBrother(int id, sCpcCheckpoint_t *pCheckpoint)
{
    if (pCheckpoint->hasParent)
    {
        // Determine if another checkpoint has same parent, if so, the first one will get to be the big brother
        for (int i = 0; i < opencj_clientCpcInfo[id].nrCheckpoints; i++)
        {
            const sCpcCheckpoint_t *pOtherCheckpoint = &opencj_clientCpcCheckpoints[id][i];
            if (pCheckpoint == pOtherCheckpoint)
            {
                continue; // Don't break here because we could be modifying a checkpoint that is not the last one
            }

            if (pOtherCheckpoint->hasParent && (pOtherCheckpoint->parentIdx == pCheckpoint->parentIdx))
            {
                pCheckpoint->hasBigBrother = true;
                if (pOtherCheckpoint->hasBigBrother)
                {
                    pCheckpoint->bigBrotherIdx = pOtherCheckpoint->bigBrotherIdx;
                }
                else
                {
                    pCheckpoint->bigBrotherIdx = i;
                }

                break;
            }
        }
    }
    else
    {
        pCheckpoint->hasBigBrother = false;
    }
}

// ===============================================================================
// GSC functions
// ===============================================================================

void _Gsc_Cpc_CleanupShared(int id)
{
    std::string strError = opencj_clientCpcInfo[id].strError;
    opencj_clientCpcInfo[id] = {}; // Don't want to clear a potential error, GSC needs to be informed of it
    opencj_clientCpcInfo[id].strError = strError;
    memset(&opencj_clientCpcCheckpoints[id], 0, sizeof(opencj_clientCpcCheckpoints[id]));
}

void Gsc_Cpc_Cleanup(int id) // Called upon player disconnect
{
    // TODO: if player had checkpoints selected, write it to a "tmp" file so they don't get screwed out of all their checkpoints upon crash

    int playerId;
    if (!stackGetParams("i", &playerId))
    {
        stackError("Gsc_Cpc_Cleanup expects 1 argument: playerId");
        return;
    }

    _Gsc_Cpc_CleanupShared(id);
}

void Gsc_Cpc_GetLastIndex(int id) // Obtain the index of the most recent checkpoint
{
    int nrCheckpoints = opencj_clientCpcInfo[id].nrCheckpoints;
    if (nrCheckpoints > 0)
    {
        stackPushInt(nrCheckpoints - 1);
    }
    else
    {
        stackPushUndefined();
    }
}

void Gsc_Cpc_Confirm(int id)
{
    // TODO: validate values
    sCpcCheckpoint_t *pCheckpoint = &opencj_clientCpcCheckpoints[id][opencj_clientCpcInfo[id].selectedIdx];
    if (pCheckpoint->nrOrgs < 3)
    {
        opencj_clientCpcInfo[id].strError = "Confirmation failed. Not enough origins were selected";
        stackPushBool(false);
        return;
    }

    // If this is a new checkpoint (and not an existing one that has been selected)..
    if (opencj_clientCpcInfo[id].selectedIdx == opencj_clientCpcInfo[id].nrCheckpoints)
    {
        opencj_clientCpcInfo[id].nrCheckpoints++;
    }

    // Regardless of what checkpoint was modified, select a new checkpoint now
    opencj_clientCpcInfo[id].selectedIdx = opencj_clientCpcInfo[id].nrCheckpoints;

    // It is possible that this checkpoint was a big brother, but due to changing parent it is no longer the case.
    for (int i = 0; i < opencj_clientCpcInfo[id].nrCheckpoints; i++)
    {
        sCpcCheckpoint_t *pOtherCheckpoint = &opencj_clientCpcCheckpoints[id][i];
        if (pCheckpoint == pOtherCheckpoint)
        {
            continue; // Don't break here because we could be modifying a checkpoint that is not the last one
        }

        // If the other checkpoint is this checkpoint's little brother...
        if (pOtherCheckpoint->hasBigBrother && (pOtherCheckpoint->bigBrotherIdx == i))
        {
            // And this checkpoint's parent changed to something else...
            if (!pCheckpoint->hasParent || (pCheckpoint->parentIdx != pOtherCheckpoint->parentIdx))
            {
                // Then find a new big brother for this checkpoint
                findAndSetBigBrother(id, pOtherCheckpoint);
            }
        }
    }

    // Now find and set a big brother for this checkpoint, if applicable
    findAndSetBigBrother(id, pCheckpoint);

    stackPushBool(true); // No error
}

void Gsc_Cpc_Select(int id)
{
    int idx = 0;
    if (!stackGetParams("i", &idx))
    {
        stackError("Gsc_Cpc_Select() expects 1 argument: idx");
        stackPushUndefined();
        return;
    }

    if (opencj_clientCpcInfo[id].nrCheckpoints > idx)
    {
        opencj_clientCpcInfo[id].selectedIdx = idx;
        stackPushInt(idx);
    }
    else
    {
        stackPushUndefined();
    }
}

void Gsc_Cpc_Count(int id)
{
    stackPushInt(opencj_clientCpcInfo[id].nrCheckpoints);
}

void Gsc_Cpc_SetRoute(int id)
{
    char *buf = NULL;
    if (!stackGetParams("s", &buf) )
    {
        stackError("Gsc_Cpc_SetRoute expects 1 argument: routeName");
        return;
    }

    if (strlen(buf) > ROUTE_NAME_MAX_SIZE)
    {
        stackError("Gsc_Cpc_SetRoute called with too long route name");
        return;
    }

    opencj_clientCpcInfo[id].strRouteName = std::string(buf);
}

void Gsc_Cpc_GetRoute(int id)
{
    if (opencj_clientCpcInfo[id].strRouteName.length() > 0)
    {
        stackPushString(opencj_clientCpcInfo[id].strRouteName.c_str());
    }
    else
    {
        stackPushUndefined();
    }
}

void Gsc_Cpc_GetClosestCheckpointIdx(int id)
{
    vec3_t org;
    if (!stackGetParams("v", &org))
    {
        stackError("Gsc_Cpc_GetClosestCheckpointIdx expects 1 argument: origin");
        stackPushUndefined();
        return;
    }

    int closestCheckpointIdx = -1;
    float closestDistance;
    for (int i = 0; i < opencj_clientCpcInfo[id].nrCheckpoints; i++)
    {
        const sCpcCheckpoint_t *pCheckpoint = &opencj_clientCpcCheckpoints[id][i];
        if (pCheckpoint->nrOrgs > 0)
        {
            // Gotta compare all the origins of the checkpoint, one of them may be closer than the current closest checkpoint origin
            for (int j = 0; j < pCheckpoint->nrOrgs; j++)
            {
                // First origin of first checkpoint gets a free pass, since there is nothing to compare with
                float distance = distance3D(org, pCheckpoint->orgs[j]);
                if ((closestCheckpointIdx == -1) || (distance < closestDistance))
                {
                    closestCheckpointIdx = i;
                    closestDistance = distance;
                }
            }
        }
    }

    if (closestCheckpointIdx != -1)
    {
        stackPushInt(closestCheckpointIdx);
    }
    else
    {
        stackPushUndefined();
    }
}

void Gsc_Cpc_ClearAll(int id)
{
    int nrCheckpoints = opencj_clientCpcInfo[id].nrCheckpoints;

    opencj_clientCpcInfo[id] = {};
    memset(&opencj_clientCpcCheckpoints[id], 0, sizeof(opencj_clientCpcCheckpoints[id]));

    stackPushInt(nrCheckpoints);
}

void Gsc_Cpc_GetErrorStr(int id)
{
    if (opencj_clientCpcInfo[id].strError.length() > 0)
    {
        stackPushString(opencj_clientCpcInfo[id].strError.c_str());
    }
    else
    {
        stackPushUndefined();
    }

    opencj_clientCpcInfo[id].strError = ""; // Clear error
}

static std::optional<std::string> _Gsc_Cpc_ImportExportShared(int id)
{
    char *szLabel = NULL;
    int playerId = 0;
    if (!stackGetParams("si", &szLabel, &playerId))
    {
        stackError("Gsc_Cpc_Import expects 2 arguments: <label> <playerId>");
        opencj_clientCpcInfo[id].strError = "Invalid arguments";
        stackPushBool(false);
        return std::nullopt;
    }

    if ((strlen(szLabel) >= FILE_LABEL_MAX_SIZE) || (strlen(szLabel) < 3))
    {
        opencj_clientCpcInfo[id].strError = "Label must be between 3 and 32 characters";
        stackPushBool(false);
        return std::nullopt;
    }

    std::string strCleanLabel = std::string(szLabel);
    strCleanLabel.erase(
        std::remove_if(strCleanLabel.begin(), strCleanLabel.end(), [](auto const &c) -> bool
            {
                return !std::isalnum(c) && (c != '_') && (c != '-');
            }
        ),
        strCleanLabel.end()
    );

    if (strCleanLabel.length() != strlen(szLabel))
    {
        opencj_clientCpcInfo[id].strError = "Only alphanumeric characters are allowed, as well as - and _";
        stackPushBool(false);
        return std::nullopt;
    }

    std::string strMapName = Cvar_FindVar("mapname")->string;
    std::string strFileName = std::to_string(playerId) + "_" + strMapName + "_" + strCleanLabel + ".cpc";
    std::filesystem::path base = "checkpointcreation";
    std::filesystem::path fileNamePath = strFileName;
    std::filesystem::path fullPath = base / fileNamePath;

    // Don't stackPushBool here, cause this is just a helper function and there may be more logic later
    return std::make_optional<std::string>(fullPath.generic_string());
}

void Gsc_Cpc_Import(int id)
{
    // Clear any checkpoints the player may have right now. Gsc already does a failsafe when calling import to check for any unsaved checkpoints.
    _Gsc_Cpc_CleanupShared(id);

    std::optional<std::string> oFullPath = _Gsc_Cpc_ImportExportShared(id);
    if (!oFullPath.has_value())
    {
        return; // Shared function will have set an error and pushed false
    }

    int fd = -1;
    int len = FS_FOpenFileRead(oFullPath.value().c_str(), &fd);
    bool isOk = true;
    std::string line;
    if ((fd >= 0) && (len > 0))
    {
        // Parse version
        std::optional<std::string> oLine = customReadLine(fd);
        if (!oLine.has_value() || (oLine.value().length() == 0))
        {
            opencj_clientCpcInfo[id].strError = "Import failed. No version line";
            isOk = false;
        }
        else
        {
            line = oLine.value();
        }

        if (isOk)
        {
            // version=<num>
            std::vector<std::string> parts = split(line, '=');
            if (parts.size() != 2)
            {
                opencj_clientCpcInfo[id].strError = "Import failed. No version";
                isOk = false;
            }

            if (isOk)
            {
                if ((parts[0] != "version") || (std::stoi(parts[1]) != CPC_FILE_VERSION))
                {
                    opencj_clientCpcInfo[id].strError = "Import failed. Failed to parse version";
                    isOk = false;
                }
            }
        }

        // Parse map name
        if (isOk)
        {
            std::optional<std::string> oLine = customReadLine(fd);
            if (!oLine.has_value() || (oLine.value().length() == 0))
            {
                opencj_clientCpcInfo[id].strError = "Import failed. No map name line";
                isOk = false;
            }
            else
            {
                line = oLine.value();
            }
        }

        if (isOk)
        {
            // version=<num>
            std::vector<std::string> parts = split(line, '=');
            if (parts.size() != 2)
            {
                opencj_clientCpcInfo[id].strError = "Import failed. No map name";
                isOk = false;
            }

            if (isOk)
            {
                std::string strMapName = Cvar_FindVar("mapname")->string;
                if ((parts[0] != "map") || (parts[1] != strMapName))
                {
                    opencj_clientCpcInfo[id].strError = "Import failed. File is for map: " + strMapName;
                    isOk = false;
                }
            }
        }

        // Parse routeName
        if (isOk)
        {
            std::optional<std::string> oLine = customReadLine(fd);
            if (!oLine.has_value() || (oLine.value().length() == 0))
            {
                opencj_clientCpcInfo[id].strError = "Import failed. No route name line";
                isOk = false;
            }
            else
            {
                line = oLine.value();
            }
        }
        if (isOk)
        {
            // route=<routeName>
            std::vector<std::string> parts = split(line, '=');
            if (parts.size() != 2)
            {
                opencj_clientCpcInfo[id].strError = "Import failed. No route name";
                isOk = false;
            }

            if (isOk)
            {
                if (parts[0] != "route")
                {
                    opencj_clientCpcInfo[id].strError = "Import failed. Failed to parse route name";
                    isOk = false;
                }
                else
                {
                    opencj_clientCpcInfo[id].strRouteName = parts[1];
                }
            }
        }

        // Parse the checkpoint data
        // <idx>:<parentIdx>:<bigBrotherIdx>:<height>:<isFinish>:<isInAir>:<allowEle>:<allowAnyFPS>:<allowDoubleRPG>:<org0>:<org1>:<org2>:[org3]:[org4]:[org5]:[org6]:[org7]
        int idx = 0;
        while (isOk)
        {
            oLine = customReadLine(fd);
            if (!oLine.has_value())
            {
                opencj_clientCpcInfo[id].strError = "Import failed. File read error (idx = " + std::to_string(idx) + ")";
                isOk = false;
                break; // Still need to close file
            }
            if (oLine.value().length() == 0)
            {
                // Done, EOF
                break;
            }
            line = oLine.value();

            std::vector<std::string> parts = split(line, ':');
            if (parts.size() < 12)
            {
                opencj_clientCpcInfo[id].strError = "Import failed. Corrupt checkpoint (idx = " + std::to_string(idx) + ")";
                isOk = false;
                break; // Still need to close file
            }

            idx = std::stoi(parts[0]);
            sCpcCheckpoint_t *pCheckpoint = &opencj_clientCpcCheckpoints[id][idx];
            int parentIdx = std::stoi(parts[1]);
            if (parentIdx != -1)
            {
                pCheckpoint->hasParent = true;
                pCheckpoint->parentIdx = parentIdx;
            }
            else
            {
                pCheckpoint->hasParent = false;
            }
            int bigBrotherIdx = std::stoi(parts[2]);
            if (bigBrotherIdx != -1)
            {
                pCheckpoint->hasBigBrother = true;
                pCheckpoint->bigBrotherIdx = bigBrotherIdx;
            }
            else
            {
                pCheckpoint->hasBigBrother = false;
            }
            pCheckpoint->height = std::stoi(parts[3]);
            pCheckpoint->isFinish = (std::stoi(parts[4]) != 0);
            pCheckpoint->isInAir = (std::stoi(parts[5]) != 0);
            pCheckpoint->allowEle = (std::stoi(parts[6]) != 0);
            pCheckpoint->allowAnyFPS = (std::stoi(parts[7]) != 0);
            pCheckpoint->allowDoubleRPG = (std::stoi(parts[8]) != 0);

            // Now just the origins remain
            int idxFirstOrg = 9;
            pCheckpoint->nrOrgs = (parts.size() - idxFirstOrg);
            for (int i = 0; i < pCheckpoint->nrOrgs; i++)
            {
                std::vector<std::string> orgParts = split(parts[idxFirstOrg + i], ',');
                if (orgParts.size() != 3)
                {
                    opencj_clientCpcInfo[id].strError = "Import failed. Checkpoint with malformed origin (idx = " + std::to_string(idx) + ")";
                    isOk = false;
                    break;
                }
                pCheckpoint->orgs[i][0] = std::stoi(orgParts[0]);
                pCheckpoint->orgs[i][1] = std::stoi(orgParts[1]);
                pCheckpoint->orgs[i][2] = std::stoi(orgParts[2]);
            }

            if (isOk)
            {
                opencj_clientCpcInfo[id].nrCheckpoints++;
            }
        }
    }
    else
    {
        opencj_clientCpcInfo[id].strError = "File with this label either does not exist, is not for this map or does not match your playerId";
        isOk = false;
    }

    if (fd >= 0)
    {
        FS_FCloseFile(fd);
    }

    if (!isOk)
    {
        _Gsc_Cpc_CleanupShared(id); // Get rid of the imported checkpoints
    }
    else
    {
        opencj_clientCpcInfo[id].selectedIdx = (opencj_clientCpcInfo[id].nrCheckpoints > 0) ? (opencj_clientCpcInfo[id].nrCheckpoints - 1) : 0;
    }

    stackPushBool(isOk);
}

void Gsc_Cpc_Export(int id)
{
    if (opencj_clientCpcInfo[id].nrCheckpoints <= 0)
    {
        opencj_clientCpcInfo[id].strError = "You have no confirmed checkpoints to export";
        stackPushBool(false);
        return;
    }

    std::optional<std::string> oFullPath = _Gsc_Cpc_ImportExportShared(id);
    if (!oFullPath.has_value())
    {
        return; // Shared function will have set an error and pushed false
    }

    // File may already exist. But could be because they're just continuing their checkpoints from another time, so overwrite.
    int fd = FS_FOpenFileWrite(oFullPath.value().c_str());
    if (fd >= 0)
    {
        FS_Printf(fd, "version=%d\n", CPC_FILE_VERSION);
        FS_Printf(fd, "map=%s\n", Cvar_FindVar("mapname")->string);
        FS_Printf(fd, "route=%s\n", opencj_clientCpcInfo[id].strRouteName.c_str());

        for (int i = 0; i < opencj_clientCpcInfo[id].nrCheckpoints; i++)
        {
            const sCpcCheckpoint_t *pCheckpoint = &opencj_clientCpcCheckpoints[id][i];

            FS_Printf(fd, "%d:%d:%d:%d:%d:%d:%d:%d:%d:", i, (pCheckpoint->hasParent ? pCheckpoint->parentIdx : -1), (pCheckpoint->hasBigBrother ? pCheckpoint->bigBrotherIdx : -1),
                pCheckpoint->height, (pCheckpoint->isFinish ? 1 : 0), (pCheckpoint->isInAir ? 1 : 0), (pCheckpoint->allowEle ? 1 : 0), (pCheckpoint->allowAnyFPS ? 1 : 0),
                (pCheckpoint->allowDoubleRPG ? 1 : 0));

            std::string strOrg = "";
            for (int j = 0; j < pCheckpoint->nrOrgs; j++)
            {
                const float *pOrg = &pCheckpoint->orgs[j][0];
                int x = std::round(pOrg[0]);
                int y = std::round(pOrg[1]);
                int z = std::round(pOrg[2]);
                strOrg += std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) + ":";
            }
            strOrg.pop_back(); // Remove trailing :
            FS_Printf(fd, "%s\n", strOrg.c_str());
        }
        FS_FCloseFile(fd);

        _Gsc_Cpc_CleanupShared(id); // Done with the checkpoints
        stackPushBool(true);
    }
    else
    {
        opencj_clientCpcInfo[id].strError = "Export failed. Failed to create file";
        stackPushBool(false);
    }
}

// Setter functions for checkpoint properties

void Gsc_Cpc_AddOrg(int id)
{
    vec3_t org;
    if (!stackGetParams("v", &org))
    {
        stackError("Gsc_Cpc_AddOrg() expects 1 argument: org");
        return;
    }

    int nrCheckpoints = opencj_clientCpcInfo[id].nrCheckpoints;
    sCpcCheckpoint_t *pCheckpoint = &opencj_clientCpcCheckpoints[id][nrCheckpoints];
    if (pCheckpoint->nrOrgs >= MAX_ORGS_PER_CHECKPOINT)
    {
        stackPushBool(false);
        return;
    }

    memcpy(&pCheckpoint->orgs[pCheckpoint->nrOrgs], &org, sizeof(pCheckpoint->orgs[pCheckpoint->nrOrgs]));
    pCheckpoint->nrOrgs++;
    stackPushBool(true);
}

void Gsc_Cpc_SetIsInAir(int id)
{
    int isInAir = 0;
    if (!stackGetParams("i", &isInAir))
    {
        stackError("Gsc_Cpc_SetIsInAir() expects 1 argument: isInAir");
        return;
    }

    int nrCheckpoints = opencj_clientCpcInfo[id].nrCheckpoints;
    sCpcCheckpoint_t *pCheckpoint = &opencj_clientCpcCheckpoints[id][nrCheckpoints];
    pCheckpoint->isInAir = (isInAir != 0);
}

void Gsc_Cpc_SetHeight(int id)
{
    int height = 0;
    if (!stackGetParams("i", &height))
    {
        stackError("Gsc_Cpc_SetHeight() expects 1 argument: height");
        return;
    }

    int nrCheckpoints = opencj_clientCpcInfo[id].nrCheckpoints;
    sCpcCheckpoint_t *pCheckpoint = &opencj_clientCpcCheckpoints[id][nrCheckpoints];
    pCheckpoint->height = height;
}

void Gsc_Cpc_SetParent(int id)
{
    int parentIdx = 0;
    if (!stackGetParams("i", &parentIdx))
    {
        stackError("Gsc_Cpc_SetParent() expects 1 argument: parentIdx");
        return;
    }

    int nrCheckpoints = opencj_clientCpcInfo[id].nrCheckpoints;
    sCpcCheckpoint_t *pCheckpoint = &opencj_clientCpcCheckpoints[id][nrCheckpoints];
    pCheckpoint->hasParent = true;
    pCheckpoint->parentIdx = parentIdx;
}

void Gsc_Cpc_SetIsFinish(int id)
{
    int isFinish = 0;
    if (!stackGetParams("i", &isFinish))
    {
        stackError("Gsc_Cpc_SetIsFinish() expects 1 argument: isFinish");
        return;
    }

    int nrCheckpoints = opencj_clientCpcInfo[id].nrCheckpoints;
    sCpcCheckpoint_t *pCheckpoint = &opencj_clientCpcCheckpoints[id][nrCheckpoints];
    pCheckpoint->isFinish = (isFinish != 0);
}

void Gsc_Cpc_SetAllowEle(int id)
{
    int allowEle = 0;
    if (!stackGetParams("i", &allowEle))
    {
        stackError("Gsc_Cpc_SetAllowEle() expects 1 argument: allowEle");
        return;
    }

    int nrCheckpoints = opencj_clientCpcInfo[id].nrCheckpoints;
    sCpcCheckpoint_t *pCheckpoint = &opencj_clientCpcCheckpoints[id][nrCheckpoints];
    pCheckpoint->allowEle = (allowEle != 0);
}

void Gsc_Cpc_SetAllowAnyFPS(int id)
{
    int allowAnyFPS = 0;
    if (!stackGetParams("i", &allowAnyFPS))
    {
        stackError("Gsc_Cpc_SetAllowAnyFPS() expects 1 argument: allowAnyFPS");
        return;
    }

    int nrCheckpoints = opencj_clientCpcInfo[id].nrCheckpoints;
    sCpcCheckpoint_t *pCheckpoint = &opencj_clientCpcCheckpoints[id][nrCheckpoints];
    pCheckpoint->allowAnyFPS = (allowAnyFPS != 0);
}

void Gsc_Cpc_SetAllowDoubleRPG(int id)
{
    int allowDoubleRPG = 0;
    if (!stackGetParams("i", &allowDoubleRPG))
    {
        stackError("Gsc_Cpc_SetAllowDoubleRPG() expects 1 argument: allowDoubleRPG");
        return;
    }

    int nrCheckpoints = opencj_clientCpcInfo[id].nrCheckpoints;
    sCpcCheckpoint_t *pCheckpoint = &opencj_clientCpcCheckpoints[id][nrCheckpoints];
    pCheckpoint->allowDoubleRPG = (allowDoubleRPG != 0);
}

// Getter functions for checkpoint properties

void Gsc_Cpc_GetOrgs(int id)
{
    const sCpcCheckpoint_t *pCheckpoint = &opencj_clientCpcCheckpoints[id][opencj_clientCpcInfo[id].selectedIdx];
    if (pCheckpoint->nrOrgs > 0)
    {
        stackMakeArray();
        for (int i = 0; i < pCheckpoint->nrOrgs; i++)
        {
            stackPushVector(pCheckpoint->orgs[i]);
            stackPushArrayNext();
        }
    }
    else
    {
        stackPushUndefined();
    }
}

void Gsc_Cpc_GetIsInAir(int id)
{
    const sCpcCheckpoint_t *pCheckpoint = &opencj_clientCpcCheckpoints[id][opencj_clientCpcInfo[id].selectedIdx];
    stackPushBool(pCheckpoint->isInAir);
}

void Gsc_Cpc_GetHeight(int id)
{
    const sCpcCheckpoint_t *pCheckpoint = &opencj_clientCpcCheckpoints[id][opencj_clientCpcInfo[id].selectedIdx];
    stackPushInt(pCheckpoint->height);
}

void Gsc_Cpc_GetParent(int id)
{
    const sCpcCheckpoint_t *pCheckpoint = &opencj_clientCpcCheckpoints[id][opencj_clientCpcInfo[id].selectedIdx];
    stackPushInt(pCheckpoint->parentIdx);
}

void Gsc_Cpc_GetIsFinish(int id)
{
    const sCpcCheckpoint_t *pCheckpoint = &opencj_clientCpcCheckpoints[id][opencj_clientCpcInfo[id].selectedIdx];
    stackPushBool(pCheckpoint->isFinish);
}

void Gsc_Cpc_GetAllowEle(int id)
{
    const sCpcCheckpoint_t *pCheckpoint = &opencj_clientCpcCheckpoints[id][opencj_clientCpcInfo[id].selectedIdx];
    stackPushBool(pCheckpoint->allowEle);
}

void Gsc_Cpc_GetAllowAnyFPS(int id)
{
    const sCpcCheckpoint_t *pCheckpoint = &opencj_clientCpcCheckpoints[id][opencj_clientCpcInfo[id].selectedIdx];
    stackPushBool(pCheckpoint->allowAnyFPS);
}

void Gsc_Cpc_GetAllowDoubleRPG(int id)
{
    const sCpcCheckpoint_t *pCheckpoint = &opencj_clientCpcCheckpoints[id][opencj_clientCpcInfo[id].selectedIdx];
    stackPushBool(pCheckpoint->allowDoubleRPG);
}
