#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace game::pure {
struct InputFrame { std::vector<int> pressedActions; double axisX=0, axisY=0, leftTrigger=0, rightTrigger=0; bool connected=false; };
struct AudioRequest { std::string id; std::string path; int volume=100; int pitch=100; double pan=0.0; bool loop=false; };
struct RenderFrameInfo { std::uint64_t serial=0; int viewportWidth=1; int viewportHeight=1; double interpolation=0.0; };
class IInputSource { public: virtual ~IInputSource()=default; virtual InputFrame poll()=0; };
class IAudioBackend { public: virtual ~IAudioBackend()=default; virtual void play(const AudioRequest&)=0; virtual void stop(const std::string& id)=0; };
class IRenderBridge { public: virtual ~IRenderBridge()=default; virtual void present(const RenderFrameInfo&)=0; };
}
