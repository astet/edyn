#ifndef EDYN_MATH_GEOM_HPP
#define EDYN_MATH_GEOM_HPP

#include "constants.hpp"
#include "quaternion.hpp"
#include <array>

namespace edyn {
/**
 * Geometric utilities.
 */


/**
 * @brief Computes the point in the segment `q(t) = q0 + t*(q1 - q0)` closest
 * to point `p`.
 * 
 * @param q0 Initial point in segment.
 * @param q1 End point in segment.
 * @param p The point.
 * @param t Outputs the parameter where `q(t)` gives the closest point to `p`.
 * @param q Outputs the point in `q(t)` closest to `p`.
 * @return The squared distance between `q(t)` an `p`.
 */
scalar closest_point_segment(const vector3 &q0, const vector3 &q1,
                             const vector3 &p, scalar &t, vector3 &q);

/**
 * @brief Computes the closest points `c1` and `c2` of segments 
 * `s1(s) = p1 + s*(q1 - p1)` and `s2(t) = p2 + t*(q2 - p2)`, 
 * where `0 <= s <= 1` and `0 <= t <= 1`.
 * 
 * @param p1 Initial point in the first segment.
 * @param q1 End point in the first segment.
 * @param p2 Initial point in the second segment.
 * @param q2 End point in the second segment.
 * @param s Outputs the parameter where `s1(s)` gives the closest point to `s2`.
 * @param t Outputs the parameter where `s2(t)` gives the closest point to `s1`.
 * @param c1 Outputs the point in `s1` closest to `s2`.
 * @param c2 Outputs the point in `s2` closest to `s1`.
 * @param num_points Optional pointer to store the number of closest points. If
 *        not `nullptr` and the segments are parallel, two closest points will 
 *        be generated if the projection of one segment onto the other is a range
 *        of points.
 * @param sp Outputs the parameter where `s1(s)` gives the closest point to `s2`
 *        if segments are parallel.
 * @param tp Outputs the parameter where `s2(t)` gives the closest point to `s1`
 *        if segments are parallel.
 * @param c1p Outputs the second point in `s1` closest to `s2` if segments are
 *        parallel.
 * @param c2p Outputs the second point in `s2` closest to `s1` if segments are
 *        parallel.
 * @return The squared distance between `s1(s)` and `s2(t)`.
 */
scalar closest_point_segment_segment(const vector3 &p1, const vector3 &q1, 
                                     const vector3 &p2, const vector3 &q2, 
                                     scalar &s, scalar &t, 
                                     vector3 &c1, vector3 &c2,
                                     size_t *num_points = nullptr,
                                     scalar *sp = nullptr, scalar *tp = nullptr, 
                                     vector3 *c1p = nullptr, vector3 *c2p = nullptr);

scalar closest_point_disc_line(const vector3 &cpos, const quaternion &corn,  scalar radius,
                               const vector3 &p0, const vector3 &p1, size_t &num_points, 
                               scalar &s0, vector3 &cc0, vector3 &cs0,
                               scalar &s1, vector3 &cc1, vector3 &cs1, 
                               vector3 &normal);

using closest_points_array = std::array<std::pair<vector3, vector3>, max_contacts>;

scalar closest_point_disc_disc(const vector3 &posA, const quaternion &ornA, scalar radiusA,
                               const vector3 &posB, const quaternion &ornB, scalar radiusB,
                               size_t &num_points, closest_points_array &, 
                               vector3 &normal);

}

#endif // EDYN_MATH_GEOM_HPP