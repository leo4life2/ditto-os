#pragma once
namespace Mecha {
enum Clip { Idle, Listening, Thinking, Speaking, Wave, Yes, No, Happy, Curious, Sleep, Surprise, Point, Celebrate, Sad, ClipCount };
inline constexpr const char* clips[]={"Idle","Listening","Thinking","Speaking","Wave","Yes","No","Happy","Curious","Sleep","Surprise","Point","Celebrate","Sad"};
inline constexpr int HeadNode=2;
inline constexpr int EarLeft=3;
inline constexpr int EarRight=4;
inline constexpr int EyeLeft=5;
inline constexpr int EyeRight=6;
inline constexpr int MouthNode=7;
inline constexpr int Triangles=4730;
}
