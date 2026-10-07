#include "../src/affine.hpp"

#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>

namespace {

constexpr float pi = 3.14159265358979323846f;

bool near(float a, float b, float tolerance = 2.0e-4f) {
    return std::fabs(a - b) <= tolerance;
}

void makeZ(float degrees, float sx, float sy, float sz, float tx, float ty, float tz, float* out) {
    const float radians = degrees * pi / 180.0f;
    const float c = std::cos(radians), s = std::sin(radians);
    const float matrix[16] = {
        sx * c, sx * s, 0.0f, 0.0f,
        -sy * s, sy * c, 0.0f, 0.0f,
        0.0f, 0.0f, sz, 0.0f,
        tx, ty, tz, 1.0f};
    std::memcpy(out, matrix, sizeof(matrix));
}

void requireMatrixNear(const float* actual, const float* expected) {
    for (int i = 0; i != 16; ++i) assert(near(actual[i], expected[i]));
}

void testIdentityAndTranslation() {
    float a[16], b[16], out[16];
    makeZ(0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, a);
    makeZ(0.0f, 1.0f, 1.0f, 1.0f, 8.0f, -4.0f, 12.0f, b);
    assert(gurumin::interpolateAffine(a, b, 0.25f, out));
    assert(near(out[0], 1.0f) && near(out[5], 1.0f) && near(out[10], 1.0f));
    assert(near(out[12], 2.0f) && near(out[13], -1.0f) && near(out[14], 3.0f));
}

void testQuarterTurn() {
    float a[16], b[16], out[16];
    makeZ(0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, a);
    makeZ(90.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, b);
    assert(gurumin::interpolateAffine(a, b, 0.5f, out));
    const float rootHalf = std::sqrt(0.5f);
    assert(near(out[0], rootHalf) && near(out[1], rootHalf));
    assert(near(out[4], -rootHalf) && near(out[5], rootHalf));
}

void testShortestPathAcross180() {
    float a[16], b[16], out[16];
    makeZ(170.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, a);
    makeZ(-170.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, b);
    assert(gurumin::interpolateAffine(a, b, 0.5f, out));
    assert(near(out[0], -1.0f) && near(out[1], 0.0f));
    assert(near(out[4], 0.0f) && near(out[5], -1.0f));
}

void testNonUniformScaleAndEndpoints() {
    float a[16], b[16], out[16];
    makeZ(0.0f, 2.0f, 3.0f, 4.0f, -1.0f, 2.0f, 4.0f, a);
    makeZ(90.0f, 4.0f, 5.0f, 6.0f, 5.0f, 8.0f, 10.0f, b);
    assert(gurumin::interpolateAffine(a, b, 0.0f, out));
    requireMatrixNear(out, a);
    assert(gurumin::interpolateAffine(a, b, 1.0f, out));
    requireMatrixNear(out, b);
    assert(gurumin::interpolateAffine(a, b, 0.5f, out));
    const float rootHalf = std::sqrt(0.5f);
    assert(near(out[0], 3.0f * rootHalf) && near(out[1], 3.0f * rootHalf));
    assert(near(out[4], -4.0f * rootHalf) && near(out[5], 4.0f * rootHalf));
    assert(near(out[10], 5.0f));
    assert(near(out[12], 2.0f) && near(out[13], 5.0f) && near(out[14], 7.0f));
}

void testRejectsInvalidMatrices() {
    float identity[16], out[16];
    makeZ(0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, identity);
    float shear[16]; std::memcpy(shear, identity, sizeof(shear)); shear[1] = 0.25f;
    assert(!gurumin::interpolateAffine(shear, identity, 0.5f, out));
    float reflection[16]; std::memcpy(reflection, identity, sizeof(reflection)); reflection[0] = -1.0f;
    assert(!gurumin::interpolateAffine(reflection, identity, 0.5f, out));
    float nanMatrix[16]; std::memcpy(nanMatrix, identity, sizeof(nanMatrix));
    nanMatrix[0] = std::numeric_limits<float>::quiet_NaN();
    assert(!gurumin::interpolateAffine(nanMatrix, identity, 0.5f, out));
}

}  // namespace

int main() {
    testIdentityAndTranslation();
    testQuarterTurn();
    testShortestPathAcross180();
    testNonUniformScaleAndEndpoints();
    testRejectsInvalidMatrices();
}
