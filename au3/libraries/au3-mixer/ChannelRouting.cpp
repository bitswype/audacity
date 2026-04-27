/**********************************************************************

  Audacity: A Digital Audio Editor

  ChannelRouting.cpp

*******************************************************************/

#include "ChannelRouting.h"

std::vector<TrackChannelAssignment> ComputeChannelAssignments(
    const std::vector<PlaybackOutputMask>& trackOutputMasks)
{
    std::vector<TrackChannelAssignment> result;
    result.reserve(trackOutputMasks.size());
    for (const auto& mask : trackOutputMasks) {
        result.push_back(TrackChannelAssignment { mask });
    }
    return result;
}
