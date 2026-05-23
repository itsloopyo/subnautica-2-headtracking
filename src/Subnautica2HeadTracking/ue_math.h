#pragma once
#include <cmath>

// UE5 POD math types and the quaternion/rotator conversions the mod needs.
// All doubles: this build is UE5 LWC, so FVector/FRotator are 3-double
// (FVector3d / FRotator3d). Declaring them as floats would overflow the
// engine's out-params and decode adjacent bytes as garbage.
namespace Subnautica2HeadTracking::ue
{
    struct FVector  { double X, Y, Z; };
    struct FRotator { double Pitch, Yaw, Roll; };
    struct FQuat4d  { double X, Y, Z, W; };

    // UE5 FRotator::Quaternion() reference implementation, byte-for-byte.
    // Inputs are an FRotator (Pitch around Y, Yaw around Z, Roll around X)
    // in degrees. See Engine/Source/Runtime/Core/Public/Math/Rotator.h.
    inline FQuat4d QuatFromEulerDeg(double pitchDeg, double yawDeg, double rollDeg)
    {
        constexpr double kPi = 3.14159265358979323846;
        const double half = kPi / 360.0;  // (PI/180)/2
        const double sp = std::sin(pitchDeg * half), cp = std::cos(pitchDeg * half);
        const double sy = std::sin(yawDeg   * half), cy = std::cos(yawDeg   * half);
        const double sr = std::sin(rollDeg  * half), cr = std::cos(rollDeg  * half);
        return FQuat4d{
             cr*sp*sy - sr*cp*cy,    // X
            -cr*sp*cy - sr*cp*sy,    // Y
             cr*cp*sy - sr*sp*cy,    // Z
             cr*cp*cy + sr*sp*sy     // W
        };
    }

    inline FQuat4d QuatMul(const FQuat4d& a, const FQuat4d& b)
    {
        return FQuat4d{
            a.W*b.X + a.X*b.W + a.Y*b.Z - a.Z*b.Y,
            a.W*b.Y - a.X*b.Z + a.Y*b.W + a.Z*b.X,
            a.W*b.Z + a.X*b.Y - a.Y*b.X + a.Z*b.W,
            a.W*b.W - a.X*b.X - a.Y*b.Y - a.Z*b.Z
        };
    }

    inline FQuat4d QuatInv(const FQuat4d& q)
    {
        return FQuat4d{-q.X, -q.Y, -q.Z, q.W};
    }

    // UE5 FQuat::Rotator() reference implementation. Matches
    // Engine/Source/Runtime/Core/Public/Math/QuatRotationTranslationMatrix.h
    // singularity handling (gimbal at +/-90 pitch).
    inline FRotator QuatToRotator(const FQuat4d& q)
    {
        constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
        const double SingularityTest = q.Z*q.X - q.W*q.Y;
        const double YawY = 2.0 * (q.W*q.Z + q.X*q.Y);
        const double YawX = 1.0 - 2.0 * (q.Y*q.Y + q.Z*q.Z);
        constexpr double ST = 0.4999995;
        auto normalize = [](double a) {
            while (a > 180.0)  a -= 360.0;
            while (a < -180.0) a += 360.0;
            return a;
        };
        FRotator r{};
        if (SingularityTest < -ST) {
            r.Pitch = -90.0;
            r.Yaw   = std::atan2(YawY, YawX) * kRadToDeg;
            r.Roll  = normalize(-r.Yaw - 2.0 * std::atan2(q.X, q.W) * kRadToDeg);
        } else if (SingularityTest > ST) {
            r.Pitch = 90.0;
            r.Yaw   = std::atan2(YawY, YawX) * kRadToDeg;
            r.Roll  = normalize(r.Yaw - 2.0 * std::atan2(q.X, q.W) * kRadToDeg);
        } else {
            r.Pitch = std::asin(2.0 * SingularityTest) * kRadToDeg;
            r.Yaw   = std::atan2(YawY, YawX) * kRadToDeg;
            r.Roll  = std::atan2(-2.0*(q.W*q.X + q.Y*q.Z),
                                 1.0 - 2.0*(q.X*q.X + q.Y*q.Y)) * kRadToDeg;
        }
        return r;
    }

    // Rotate a vector by a unit quaternion: v' = q * v * q^-1, expanded.
    inline FVector QuatRotateVec(const FQuat4d& q, const FVector& v)
    {
        const double xx = q.X*q.X, yy = q.Y*q.Y, zz = q.Z*q.Z;
        const double xy = q.X*q.Y, xz = q.X*q.Z, yz = q.Y*q.Z;
        const double wx = q.W*q.X, wy = q.W*q.Y, wz = q.W*q.Z;
        FVector r{};
        r.X = (1.0 - 2.0*(yy + zz))*v.X + 2.0*(xy - wz)*v.Y       + 2.0*(xz + wy)*v.Z;
        r.Y = 2.0*(xy + wz)*v.X       + (1.0 - 2.0*(xx + zz))*v.Y + 2.0*(yz - wx)*v.Z;
        r.Z = 2.0*(xz - wy)*v.X       + 2.0*(yz + wx)*v.Y         + (1.0 - 2.0*(xx + yy))*v.Z;
        return r;
    }
}
