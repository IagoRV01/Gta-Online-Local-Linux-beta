// Phase 12: the Camera Client. A GTA instance (on the Windows server PC, not the players' machines) that logs in as a
// camera (protocol v13 role), never plays: its story ped is hidden and frozen, the HUD is off, and a script camera shows
// what GodEye picks through the server (CameraSet): a CCTV camera, or a player framed as follow, helicopter, drone,
// cinematic, third person or chase. It sees every player, in any session. Phase 13 streams this picture to browsers.
#pragma once
#include <string>
#include "protocol/protocol.h"

namespace camera_client {

struct Settings {
    std::string host;
    int port = 4499;
    std::string name = "Camera 1";
    std::string tokenFile;      // the camera's account token (created by the server on its first login)
};

void Run(const Settings &s);                     // the camera client's own loop: never returns
void OnCameraSet(const proto::CameraSetMsg &m);  // network callback
bool Active();                                   // camera mode is on (keys and other modules stay out)

}  // namespace camera_client
