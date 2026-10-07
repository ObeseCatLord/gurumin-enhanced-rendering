#pragma once

#include <cmath>

namespace gurumin {
namespace detail {

struct AffineQuaternion {
    float x, y, z, w;
};

inline float dot3(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

inline bool decomposeAffine(const float* matrix, float* scale,
                            AffineQuaternion* rotation) {
    constexpr float affineTolerance = 1.0e-5f;
    constexpr float orthogonalTolerance = 1.0e-4f;
    constexpr float minimumScale = 1.0e-8f;
    for (int i = 0; i != 16; ++i) {
        if (!std::isfinite(matrix[i])) return false;
    }
    if (std::fabs(matrix[3]) > affineTolerance ||
        std::fabs(matrix[7]) > affineTolerance ||
        std::fabs(matrix[11]) > affineTolerance ||
        std::fabs(matrix[15] - 1.0f) > affineTolerance) return false;

    float basis[3][3] = {
        {matrix[0], matrix[1], matrix[2]},
        {matrix[4], matrix[5], matrix[6]},
        {matrix[8], matrix[9], matrix[10]},
    };
    for (int i = 0; i != 3; ++i) {
        scale[i] = std::sqrt(dot3(basis[i], basis[i]));
        if (!std::isfinite(scale[i]) || scale[i] <= minimumScale) return false;
        basis[i][0] /= scale[i];
        basis[i][1] /= scale[i];
        basis[i][2] /= scale[i];
    }
    if (std::fabs(dot3(basis[0], basis[1])) > orthogonalTolerance ||
        std::fabs(dot3(basis[0], basis[2])) > orthogonalTolerance ||
        std::fabs(dot3(basis[1], basis[2])) > orthogonalTolerance) return false;

    const float determinant = basis[0][0] * (basis[1][1] * basis[2][2] - basis[1][2] * basis[2][1]) -
                              basis[0][1] * (basis[1][0] * basis[2][2] - basis[1][2] * basis[2][0]) +
                              basis[0][2] * (basis[1][0] * basis[2][1] - basis[1][1] * basis[2][0]);
    if (!std::isfinite(determinant) || determinant <= 1.0f - orthogonalTolerance) return false;

    const float trace = basis[0][0] + basis[1][1] + basis[2][2];
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        rotation->w = 0.25f * s;
        rotation->x = (basis[1][2] - basis[2][1]) / s;
        rotation->y = (basis[2][0] - basis[0][2]) / s;
        rotation->z = (basis[0][1] - basis[1][0]) / s;
    } else if (basis[0][0] > basis[1][1] && basis[0][0] > basis[2][2]) {
        const float s = std::sqrt(1.0f + basis[0][0] - basis[1][1] - basis[2][2]) * 2.0f;
        rotation->w = (basis[1][2] - basis[2][1]) / s;
        rotation->x = 0.25f * s;
        rotation->y = (basis[0][1] + basis[1][0]) / s;
        rotation->z = (basis[2][0] + basis[0][2]) / s;
    } else if (basis[1][1] > basis[2][2]) {
        const float s = std::sqrt(1.0f + basis[1][1] - basis[0][0] - basis[2][2]) * 2.0f;
        rotation->w = (basis[2][0] - basis[0][2]) / s;
        rotation->x = (basis[0][1] + basis[1][0]) / s;
        rotation->y = 0.25f * s;
        rotation->z = (basis[2][1] + basis[1][2]) / s;
    } else {
        const float s = std::sqrt(1.0f + basis[2][2] - basis[0][0] - basis[1][1]) * 2.0f;
        rotation->w = (basis[0][1] - basis[1][0]) / s;
        rotation->x = (basis[2][0] + basis[0][2]) / s;
        rotation->y = (basis[2][1] + basis[1][2]) / s;
        rotation->z = 0.25f * s;
    }
    const float length = std::sqrt(rotation->x * rotation->x + rotation->y * rotation->y +
                                   rotation->z * rotation->z + rotation->w * rotation->w);
    if (!std::isfinite(length) || length <= minimumScale) return false;
    rotation->x /= length;
    rotation->y /= length;
    rotation->z /= length;
    rotation->w /= length;
    return true;
}

inline AffineQuaternion slerp(AffineQuaternion a, AffineQuaternion b, float alpha) {
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0f) {
        dot = -dot;
        b.x = -b.x; b.y = -b.y; b.z = -b.z; b.w = -b.w;
    }
    dot = dot > 1.0f ? 1.0f : dot;
    if (dot > 0.9995f) {
        AffineQuaternion result{
            a.x + alpha * (b.x - a.x), a.y + alpha * (b.y - a.y),
            a.z + alpha * (b.z - a.z), a.w + alpha * (b.w - a.w)};
        const float length = std::sqrt(result.x * result.x + result.y * result.y +
                                       result.z * result.z + result.w * result.w);
        result.x /= length; result.y /= length; result.z /= length; result.w /= length;
        return result;
    }
    const float theta = std::acos(dot);
    const float inverseSin = 1.0f / std::sin(theta);
    const float first = std::sin((1.0f - alpha) * theta) * inverseSin;
    const float second = std::sin(alpha * theta) * inverseSin;
    return {first * a.x + second * b.x, first * a.y + second * b.y,
            first * a.z + second * b.z, first * a.w + second * b.w};
}

}  // namespace detail

// Row-major D3DX row-vector matrices: rows 0..2 are scaled rotation axes and
// translation is at indices 12, 13 and 14.  Invalid inputs leave out untouched.
inline bool interpolateAffine(const float* a, const float* b, float alpha, float* out) {
    if (!a || !b || !out || !std::isfinite(alpha)) return false;

    float scaleA[3], scaleB[3];
    detail::AffineQuaternion rotationA, rotationB;
    if (!detail::decomposeAffine(a, scaleA, &rotationA) ||
        !detail::decomposeAffine(b, scaleB, &rotationB)) return false;

    const detail::AffineQuaternion rotation = detail::slerp(rotationA, rotationB, alpha);
    const float x = rotation.x, y = rotation.y, z = rotation.z, w = rotation.w;
    float result[16] = {
        1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y + z * w),        2.0f * (x * z - y * w),        0.0f,
        2.0f * (x * y - z * w),        1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z + x * w),        0.0f,
        2.0f * (x * z + y * w),        2.0f * (y * z - x * w),        1.0f - 2.0f * (x * x + y * y), 0.0f,
        a[12] + alpha * (b[12] - a[12]), a[13] + alpha * (b[13] - a[13]),
        a[14] + alpha * (b[14] - a[14]), 1.0f};
    for (int row = 0; row != 3; ++row) {
        const float scale = scaleA[row] + alpha * (scaleB[row] - scaleA[row]);
        result[row * 4] *= scale;
        result[row * 4 + 1] *= scale;
        result[row * 4 + 2] *= scale;
    }
    for (int i = 0; i != 16; ++i) out[i] = result[i];
    return true;
}

}  // namespace gurumin
