#pragma once

#include <glm/glm.hpp>
#include <glm/gtx/intersect.hpp>
#include "sceneStructs.h"
#include "utilities.h"

#define BOUNDING_BOX_ENABLE 1

/**
 * Handy-dandy hash function that provides seeds for random number generation.
 */
__host__ __device__ inline unsigned int utilhash(unsigned int a) {
    a = (a + 0x7ed55d16) + (a << 12);
    a = (a ^ 0xc761c23c) ^ (a >> 19);
    a = (a + 0x165667b1) + (a << 5);
    a = (a + 0xd3a2646c) ^ (a << 9);
    a = (a + 0xfd7046c5) + (a << 3);
    a = (a ^ 0xb55a4f09) ^ (a >> 16);
    return a;
}

// CHECKITOUT
/**
 * Compute a point at parameter value `t` on ray `r`.
 * Falls slightly short so that it doesn't intersect the object it's hitting.
 */
__host__ __device__ glm::vec3 getPointOnRay(Ray r, float t) {
    return r.origin + (t - .0001f) * glm::normalize(r.direction);
}

/**
 * Multiplies a mat4 and a vec4 and returns a vec3 clipped from the vec4.
 */
__host__ __device__ glm::vec3 multiplyMV(glm::mat4 m, glm::vec4 v) {
    return glm::vec3(m * v);
}

// CHECKITOUT
/**
 * Test intersection between a ray and a transformed cube. Untransformed,
 * the cube ranges from -0.5 to 0.5 in each axis and is centered at the origin.
 *
 * @param intersectionPoint  Output parameter for point of intersection.
 * @param normal             Output parameter for surface normal.
 * @param outside            Output param for whether the ray came from outside.
 * @return                   Ray parameter `t` value. -1 if no intersection.
 */
__host__ __device__ float boxIntersectionTest(Geom box, Ray r,
        glm::vec3 &intersectionPoint, glm::vec3 &normal, bool &outside) {
    Ray q;
    glm::vec3 ro;
    if (box.moving) {
        ro = r.origin - r.time * (box.target - box.translation);
    }
    else {
        ro = r.origin;
    }
    q.origin = multiplyMV(box.inverseTransform, glm::vec4(ro, 1.0f));
    q.direction = glm::normalize(multiplyMV(box.inverseTransform, glm::vec4(r.direction, 0.0f)));

    float tmin = -1e38f;
    float tmax = 1e38f;
    glm::vec3 tmin_n;
    glm::vec3 tmax_n;
    for (int xyz = 0; xyz < 3; ++xyz) {
        float qdxyz = q.direction[xyz];
        /*if (glm::abs(qdxyz) > 0.00001f)*/ {
            float t1 = (-0.5f - q.origin[xyz]) / qdxyz;
            float t2 = (+0.5f - q.origin[xyz]) / qdxyz;
            float ta = glm::min(t1, t2);
            float tb = glm::max(t1, t2);
            glm::vec3 n;
            n[xyz] = t2 < t1 ? +1 : -1;
            if (ta > 0 && ta > tmin) {
                tmin = ta;
                tmin_n = n;
            }
            if (tb < tmax) {
                tmax = tb;
                n[xyz] = -n[xyz];
                tmax_n = n;
            }
        }
    }

    if (tmax >= tmin && tmax > 0) {
        if (tmin <= 0) {
            tmin = tmax;
            tmin_n = tmax_n;
        }
        intersectionPoint = multiplyMV(box.transform, glm::vec4(getPointOnRay(q, tmin), 1.0f));
        if (box.moving) {
            intersectionPoint += r.time * (box.target - box.translation);
        }
        normal = glm::normalize(multiplyMV(box.invTranspose, glm::vec4(tmin_n, 0.0f)));
        return glm::length(r.origin - intersectionPoint);
    }
    return -1;
}

// CHECKITOUT
/**
 * Test intersection between a ray and a transformed sphere. Untransformed,
 * the sphere always has radius 0.5 and is centered at the origin.
 *
 * @param intersectionPoint  Output parameter for point of intersection.
 * @param normal             Output parameter for surface normal.
 * @param outside            Output param for whether the ray came from outside.
 * @return                   Ray parameter `t` value. -1 if no intersection.
 */
__host__ __device__ float sphereIntersectionTest(Geom sphere, Ray r,
        glm::vec3 &intersectionPoint, glm::vec3 &normal, bool &outside) {
    float radius = .5;

    glm::vec3 ro;
    if (sphere.moving) {
        ro = r.origin - r.time * (sphere.target - sphere.translation);
        ro = multiplyMV(sphere.inverseTransform, glm::vec4(ro, 1.0f));
    }
    else {
        ro = multiplyMV(sphere.inverseTransform, glm::vec4(r.origin, 1.0f));
    }
    glm::vec3 rd = glm::normalize(multiplyMV(sphere.inverseTransform, glm::vec4(r.direction, 0.0f)));

    Ray rt;
    rt.origin = ro;
    rt.direction = rd;

    float vDotDirection = glm::dot(rt.origin, rt.direction);
    float radicand = vDotDirection * vDotDirection - (glm::dot(rt.origin, rt.origin) - powf(radius, 2));
    if (radicand < 0) {
        return -1;
    }

    float squareRoot = sqrt(radicand);
    float firstTerm = -vDotDirection;
    float t1 = firstTerm + squareRoot;
    float t2 = firstTerm - squareRoot;

    float t = 0;
    if (t1 < 0 && t2 < 0) {
        return -1;
    } else if (t1 > 0 && t2 > 0) {
        t = min(t1, t2);
    } else {
        t = max(t1, t2);
    }

    glm::vec3 objspaceIntersection = getPointOnRay(rt, t);
    intersectionPoint = multiplyMV(sphere.transform, glm::vec4(objspaceIntersection, 1.f));
    if (sphere.moving) {
        intersectionPoint += r.time * (sphere.target - sphere.translation);
    }
    normal = glm::normalize(multiplyMV(sphere.invTranspose, glm::vec4(objspaceIntersection, 0.f)));

    return glm::length(r.origin - intersectionPoint);
}

__host__ __device__ bool boundingIntersectionTest(Ray r, glm::vec3 leftBottom, glm::vec3 rightTop) {

    float tmin = -1e38f;
    float tmax = 1e38f;
    for (int xyz = 0; xyz < 3; ++xyz) {
        float qdxyz = r.direction[xyz];
        /*if (glm::abs(qdxyz) > 0.00001f)*/ {
            float t1 = (leftBottom[xyz] - r.origin[xyz]) / qdxyz;
            float t2 = (rightTop[xyz] - r.origin[xyz]) / qdxyz;
            float ta = glm::min(t1, t2);
            float tb = glm::max(t1, t2);

            if (ta > 0 && ta > tmin) {
                tmin = ta;
            }
            if (tb < tmax) {
                tmax = tb;
            }
        }
    }

    if (tmax >= tmin && tmax > 0) {
        return true;
    }
    return false;
}

__host__ __device__ float triangleIntersect(const Ray& r, Triangle &tri, glm::vec3 &normal, bool &outside)
{
    //1. Ray-plane intersection
    float t = glm::dot(tri.n1, (tri.p1 - r.origin)) / glm::dot(tri.n1, r.direction);
    if (t < 0) {
        return -1;
    }

    glm::vec3 P = r.origin + t * r.direction;
    //2. Barycentric test
    float S = 0.5f * glm::length(glm::cross(tri.p1 - tri.p2, tri.p1 - tri.p3));
    float s1 = 0.5f * glm::length(glm::cross(P - tri.p2, P - tri.p3)) / S;
    float s2 = 0.5f * glm::length(glm::cross(P - tri.p3, P - tri.p1)) / S;
    float s3 = 0.5f * glm::length(glm::cross(P - tri.p1, P - tri.p2)) / S;
    float sum = s1 + s2 + s3;

    if (s1 >= 0 && s1 <= 1 && s2 >= 0 && s2 <= 1 && s3 >= 0 && s3 <= 1 && abs(sum - 1.0f) < 0.001f) {
        normal = s1 / S * tri.n1 + s2 / S * tri.n2 + s3 / S * tri.n3;
        return t;
    }
    return -1;
}

/**
 * Test intersection between a ray and a transformed mesh. 
 *
 * @param intersectionPoint  Output parameter for point of intersection.
 * @param normal             Output parameter for surface normal.
 * @param outside            Output param for whether the ray came from outside.
 * @return                   Ray parameter `t` value. -1 if no intersection.
 */
__host__ __device__ float meshIntersectionTest(Geom mesh, Ray r,
    glm::vec3& intersectionPoint, glm::vec3& normal, bool& outside, Triangle* triangles) {

    Ray rt;
    
    if (mesh.moving) {
        rt.origin = r.origin - r.time * (mesh.target - mesh.translation);
        rt.origin = multiplyMV(mesh.inverseTransform, glm::vec4(rt.origin, 1.0f));
    }
    else {
        rt.origin = multiplyMV(mesh.inverseTransform, glm::vec4(r.origin, 1.0f));
    }
    rt.direction = glm::normalize(multiplyMV(mesh.inverseTransform, glm::vec4(r.direction, 0.0f)));

#if BOUNDING_BOX_ENABLE
    if (!boundingIntersectionTest(rt, mesh.leftBottom, mesh.rightTop)) {
        return -1;
    }
#endif
    float t_min = FLT_MAX;
    bool isFound = false;
    glm::vec3 objspaceIntersection;
    glm::vec3 nnormal;
    int minId = -1;
    glm::vec3 minBary;
    for (int i = mesh.startIndex; i <= mesh.endIndex; i++) {
        glm::vec3 tvec;
        if (glm::intersectRayTriangle(rt.origin, rt.direction, triangles[i].p1, triangles[i].p2, triangles[i].p3, tvec)) {
            float t = tvec.z;
            if (t > 0 && t_min > t) {
                t_min = t;
                minId = i;
                minBary = tvec;
            }
        }
    }

    if (minId < 0) {
        return -1;
    }
    
    normal = triangles[minId].n1 * minBary.x + triangles[minId].n2 * minBary.y + triangles[minId].n3 * minBary.z;

    if (glm::dot(normal, rt.direction) > 0) {
        normal = -normal;
    }

    objspaceIntersection = getPointOnRay(rt, t_min);
    intersectionPoint = multiplyMV(mesh.transform, glm::vec4(objspaceIntersection, 1.f));
    if (mesh.moving) {
        intersectionPoint += r.time * (mesh.target - mesh.translation);
    }
    normal = glm::normalize(multiplyMV(mesh.invTranspose, glm::vec4(normal, 0.f)));

    return glm::length(r.origin - intersectionPoint);
}


