#pragma once

// The live scene covers the full physical display behind the native UI.
// Camera shifts retain the established body placement without cropping tiles.
namespace SceneLayout {
constexpr int Top = 0;
constexpr int Bottom = 502;
constexpr int StandardOffset = 42;
constexpr int HomeOffset = 60;
constexpr int HomeShift = HomeOffset - StandardOffset;
}
