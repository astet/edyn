#include "edyn/collision/collide.hpp"
#include "edyn/collision/collision_result.hpp"
#include "edyn/config/constants.hpp"
#include "edyn/math/scalar.hpp"
#include "edyn/math/vector3.hpp"
#include "edyn/shapes/triangle_shape.hpp"
#include "edyn/util/shape_util.hpp"
#include "edyn/math/vector2_3_util.hpp"
#include "edyn/math/math.hpp"

namespace edyn {

static void collide_polyhedron_triangle(
    const polyhedron_shape &poly, const triangle_mesh &mesh, size_t tri_idx,
    const collision_context &ctx, collision_result &result) {

    // The triangle vertices are shifted by the polyhedron's position so all
    // calculations are effectively done with the polyhedron in the origin.
    // The rotated mesh is used thus no rotations are necessary.
    const auto &pos_poly = ctx.posA;
    const auto &orn_poly = ctx.ornA;
    const auto &rmesh = *poly.rotated;

    auto tri_vertices_original = mesh.get_triangle_vertices(tri_idx);
    auto tri_normal = mesh.get_triangle_normal(tri_idx);

    // Shift vertices into A's positional object space.
    auto tri_vertices = tri_vertices_original;
    for (auto &v : tri_vertices) {
        v -= pos_poly;
    }

    triangle_feature tri_feature;
    size_t tri_feature_index;
    auto projection_poly = EDYN_SCALAR_MAX;
    auto projection_tri = -EDYN_SCALAR_MAX;
    auto sep_axis = vector3_zero;
    scalar distance = -EDYN_SCALAR_MAX;

    // Polyhedron face normals.
    for (size_t i = 0; i < poly.mesh->num_faces(); ++i) {
        auto normal = -rmesh.normals[i]; // Point towards polyhedron.
        auto vertex_idx = poly.mesh->first_vertex_index(i);
        auto &poly_vertex = rmesh.vertices[vertex_idx];

        // Find feature on triangle that's furthest along the opposite direction
        // of the face normal.
        triangle_feature feature;
        size_t feature_idx;
        scalar tri_proj;
        get_triangle_support_feature(tri_vertices, vector3_zero, normal,
                                     feature, feature_idx, tri_proj,
                                     support_feature_tolerance);

        auto dist = dot(poly_vertex - normal * tri_proj, normal);

        if (dist > distance) {
            distance = dist;
            projection_poly = dot(poly_vertex, normal);
            projection_tri = tri_proj;
            tri_feature = feature;
            tri_feature_index = feature_idx;
            sep_axis = normal;
        }
    }

    // Triangle face normal.
    {
        // Find point on polyhedron that's furthest along the opposite direction
        // of the triangle normal.
        auto proj_poly = -point_cloud_support_projection(rmesh.vertices, -tri_normal);
        auto proj_tri = dot(tri_vertices[0], tri_normal);
        auto dist = proj_poly - proj_tri;

        if (dist > distance) {
            distance = dist;
            projection_poly = proj_poly;
            projection_tri = proj_tri;
            tri_feature = triangle_feature::face;
            sep_axis = tri_normal;
        }
    }

    // Edge vs edge.
    for (size_t i = 0; i < poly.mesh->num_edges(); ++i) {
        auto [vertexA0, vertexA1] = poly.mesh->get_rotated_edge(rmesh, i);
        auto poly_edge = vertexA1 - vertexA0;

        for (size_t j = 0; j < 3; ++j) {
            auto v0 = tri_vertices[j];
            auto v1 = tri_vertices[(j + 1) % 3];
            auto tri_edge = v1 - v0;
            auto dir = cross(poly_edge, tri_edge);

            if (!try_normalize(dir)) {
                continue;
            }

            auto &vertexB0 = tri_vertices[j];

            // Polyhedron is located at the origin.
            if (dot(vector3_zero - vertexB0, dir) < 0) {
                // Make it point towards A.
                dir *= -1;
            }

            triangle_feature feature;
            size_t feature_idx;
            scalar proj_tri;
            get_triangle_support_feature(tri_vertices, vector3_zero, dir,
                                         feature, feature_idx, proj_tri,
                                         support_feature_tolerance);

            auto proj_poly = -point_cloud_support_projection(rmesh.vertices, -dir);
            auto dist = proj_poly - proj_tri;

            if (dist > distance) {
                distance = dist;
                projection_poly = proj_poly;
                projection_tri = proj_tri;
                tri_feature = feature;
                tri_feature_index = feature_idx;
                sep_axis = dir;
            }
        }
    }

    if (distance > ctx.threshold) {
        return;
    }

    if (mesh.ignore_triangle_feature(tri_idx, tri_feature, tri_feature_index, sep_axis)) {
        return;
    }

    auto polygon = point_cloud_support_polygon(
        rmesh.vertices.begin(), rmesh.vertices.end(), vector3_zero,
        sep_axis, projection_poly, true, support_feature_tolerance);

    auto contact_origin_tri = sep_axis * projection_tri;
    auto hull_tri = std::array<size_t, 3>{};
    size_t hull_tri_size = 0;

    switch (tri_feature) {
    case triangle_feature::face:
        std::iota(hull_tri.begin(), hull_tri.end(), 0);
        hull_tri_size = 3;
        break;
    case triangle_feature::edge:
        hull_tri[0] = tri_feature_index;
        hull_tri[1] = (tri_feature_index + 1) % 3;
        hull_tri_size = 2;
        break;
    case triangle_feature::vertex:
        hull_tri[0] = tri_feature_index;
        hull_tri_size = 1;
    }

    auto plane_vertices_tri = std::array<vector2, 3>{};
    for (size_t i = 0; i < 3; ++i) {
        auto &vertex = tri_vertices[i];
        auto vertex_tangent = to_object_space(vertex, contact_origin_tri, polygon.basis);
        auto vertex_plane = to_vector2_xz(vertex_tangent);
        plane_vertices_tri[i] = vertex_plane;
    }

    // If the closest triangle feature is its face, check if the vertices of the
    // convex hull of the closest vertices of the polyhedron lie within the
    // triangle.
    if (tri_feature == triangle_feature::face) {
        for (auto idxA : polygon.hull) {
            auto &pointA = polygon.vertices[idxA];

            if (point_in_triangle(tri_vertices, sep_axis, pointA)) {
                auto pivotA = to_object_space(pointA, vector3_zero, orn_poly);
                auto pivotB = project_plane(pointA, contact_origin_tri, sep_axis) + pos_poly;
                result.maybe_add_point({pivotA, pivotB, sep_axis, distance});
            }
        }
    }

    // If the boundary points of the polyhedron from a polygon (i.e. more than
    // 2 points) add contact points for the vertices of closest triangle feature
    // that lie inside of it.
    if (polygon.hull.size() > 2) {
        for (size_t i = 0; i < hull_tri_size; ++i) {
            auto idxB = hull_tri[i];
            auto vertex_idx = mesh.get_face_vertex_index(tri_idx, idxB);

            if (!mesh.in_vertex_voronoi(vertex_idx, sep_axis)) continue;

            auto &pointB = tri_vertices[idxB];

            if (point_in_polygonal_prism(polygon.vertices, polygon.hull, sep_axis, pointB)) {
                auto pivotB = tri_vertices_original[idxB];
                auto pivotA_world = project_plane(pointB, polygon.origin, sep_axis);
                auto pivotA = to_object_space(pivotA_world, vector3_zero, orn_poly);
                result.maybe_add_point({pivotA, pivotB, sep_axis, distance});
            }
        }
    }

    // Calculate 2D intersection of edges on the closest features.
    if (polygon.hull.size() > 1 && hull_tri_size > 1) {
        // If the feature is a polygon, it will be necessary to wrap around the
        // vertex array. If it is just one edge, then avoid calculating the same
        // segment-segment intersection twice.
        const auto size_poly = polygon.hull.size();
        const auto limit_poly = size_poly == 2 ? 1 : size_poly;
        const auto limit_tri = hull_tri_size == 2 ? 1 : hull_tri_size;
        scalar s[2], t[2];

        for (size_t i = 0; i < limit_poly; ++i) {
            auto idx0A = polygon.hull[i];
            auto idx1A = polygon.hull[(i + 1) % size_poly];
            auto &v0A = polygon.plane_vertices[idx0A];
            auto &v1A = polygon.plane_vertices[idx1A];

            for (size_t j = 0; j < limit_tri; ++j) {
                auto idx0B = hull_tri[j];
                auto edge_idx = mesh.get_face_edge_index(tri_idx, idx0B);

                if (!mesh.in_edge_voronoi(edge_idx, sep_axis)) continue;

                auto idx1B = hull_tri[(j + 1) % hull_tri_size];
                auto &v0B = plane_vertices_tri[idx0B];
                auto &v1B = plane_vertices_tri[idx1B];
                auto num_points = intersect_segments(v0A, v1A, v0B, v1B,
                                                     s[0], t[0], s[1], t[1]);

                for (size_t k = 0; k < num_points; ++k) {
                    auto pivotA_world = lerp(polygon.vertices[idx0A], polygon.vertices[idx1A], s[k]);
                    auto pivotA = to_object_space(pivotA_world, vector3_zero, orn_poly);
                    auto pivotB = lerp(tri_vertices_original[idx0B], tri_vertices_original[idx1B], t[k]);
                    result.maybe_add_point({pivotA, pivotB, sep_axis, distance});
                }
            }
        }
    }
}

void collide(const polyhedron_shape &poly, const triangle_mesh &mesh,
             const collision_context &ctx, collision_result &result) {
    const auto inset = vector3_one * -contact_breaking_threshold;
    const auto visit_aabb = ctx.aabbA.inset(inset);

    mesh.visit_triangles(visit_aabb, [&] (auto tri_idx) {
        collide_polyhedron_triangle(poly, mesh, tri_idx, ctx, result);
    });
}

}
