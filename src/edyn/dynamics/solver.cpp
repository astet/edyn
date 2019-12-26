#include <entt/entity/view.hpp>
#include "edyn/dynamics/solver.hpp"
#include "edyn/sys/integrate_linacc.hpp"
#include "edyn/sys/integrate_linvel.hpp"
#include "edyn/sys/integrate_angvel.hpp"
#include "edyn/sys/apply_gravity.hpp"
#include "edyn/comp/orientation.hpp"
#include "edyn/comp/relation.hpp"
#include "edyn/comp/constraint.hpp"
#include "edyn/comp/constraint_row.hpp"
#include "edyn/comp/mass.hpp"
#include "edyn/comp/inertia.hpp"
#include "edyn/comp/linvel.hpp"
#include "edyn/comp/angvel.hpp"
#include "edyn/comp/delta_linvel.hpp"
#include "edyn/comp/delta_angvel.hpp"
#include "edyn/comp/island.hpp"
#include "edyn/dynamics/solver_stage.hpp"
#include "edyn/util/array.hpp"
#include "edyn/dynamics/island_util.hpp"

namespace edyn {

void on_construct_constraint(entt::entity entity, entt::registry &registry, constraint &con) {
    auto &rel = registry.get<relation>(entity);

    std::visit([&] (auto &&c) {
        // Initialize actual constraint.
        c.update(solver_stage_value_t<solver_stage::init>{}, entity, con, rel, registry, 0);
    }, con.var);
}

void on_destroy_constraint(entt::entity entity, entt::registry &registry) {
    auto &con = registry.get<constraint>(entity);

    // Destroy all constraint rows.
    for (size_t i = 0; i < con.num_rows; ++i) {
        registry.destroy(con.row[i]);
    }
}

scalar restitution_curve(scalar restitution, scalar relvel) {
    // TODO: figure out how to adjust the restitution when resting.
    scalar decay = 1;//std::clamp(-relvel * 1.52 - scalar(0.12), scalar(0), scalar(1));
    return restitution * decay;
}

void prepare(constraint_row &row, 
             scalar inv_mA, scalar inv_mB, 
             const matrix3x3 &inv_IA, const matrix3x3 &inv_IB,
             const vector3 &linvelA, const vector3 &linvelB,
             const vector3 &angvelA, const vector3 &angvelB) {
    auto J_invM_JT = dot(row.J[0], row.J[0]) * inv_mA +
                     dot(inv_IA * row.J[1], row.J[1]) +
                     dot(row.J[2], row.J[2]) * inv_mB +
                     dot(inv_IB * row.J[3], row.J[3]);
    row.eff_mass = 1 / J_invM_JT;

    auto relvel = dot(row.J[0], linvelA) + 
                  dot(row.J[1], angvelA) +
                  dot(row.J[2], linvelB) +
                  dot(row.J[3], angvelB);
    row.relvel = relvel;
    
    auto restitution = restitution_curve(row.restitution, row.relvel);
    row.rhs = -(row.error + relvel * (1 + restitution));
}

void warm_start(constraint_row &row, 
                scalar inv_mA, scalar inv_mB, 
                const matrix3x3 &inv_IA, const matrix3x3 &inv_IB,
                delta_linvel &dvA, delta_linvel &dvB,
                delta_angvel &dwA, delta_angvel &dwB) {    
    // Do not warm start when there's restitution since this constraint isn't 
    // going to rest and also to prevent adding energy to the system.
    if (restitution_curve(row.restitution, row.relvel) > 0) {
        return;
    }

    dvA += inv_mA * row.J[0] * row.impulse;
    dwA += inv_IA * row.J[1] * row.impulse;
    dvB += inv_mB * row.J[2] * row.impulse;
    dwB += inv_IB * row.J[3] * row.impulse;
}

void solve(constraint_row &row,
           scalar inv_mA, scalar inv_mB, 
           const matrix3x3 &inv_IA, const matrix3x3 &inv_IB,
           delta_linvel &dvA, delta_linvel &dvB,
           delta_angvel &dwA, delta_angvel &dwB) {
    auto delta_relvel = dot(row.J[0], dvA) + 
                        dot(row.J[1], dwA) +
                        dot(row.J[2], dvB) +
                        dot(row.J[3], dwB);
    auto restitution = restitution_curve(row.restitution, row.relvel + delta_relvel);
    auto delta_impulse = (row.rhs - delta_relvel * (1 + restitution)) * row.eff_mass;

    // Clamp `delta_impulse` for proper shock propagation when there's restitution.
    // This prevents contact constraints from 'sucking' and consequently 
    // eliminating the restitution effect.
    if (row.restitution > 0) {
        delta_impulse = std::clamp(delta_impulse, row.lower_limit, row.upper_limit);
    }

    auto impulse = row.impulse + delta_impulse;

    if (impulse < row.lower_limit) {
        delta_impulse = row.lower_limit - row.impulse;
        row.impulse = row.lower_limit;
    } else if (impulse > row.upper_limit) {
        delta_impulse = row.upper_limit - row.impulse;
        row.impulse = row.upper_limit;
    } else {
        row.impulse = impulse;
    }

    // Apply impulse.
    dvA += inv_mA * row.J[0] * delta_impulse;
    dwA += inv_IA * row.J[1] * delta_impulse;
    dvB += inv_mB * row.J[2] * delta_impulse;
    dwB += inv_IB * row.J[3] * delta_impulse;
}

void update_inertia(entt::registry &registry) {
    auto view = registry.view<dynamic_tag, const orientation, const inertia_inv, inertia_world_inv>(exclude_sleeping);
    view.each([] (auto, auto, const orientation& orn, const inertia_inv &inv_I, inertia_world_inv &inv_IW) {
        auto basis = to_matrix3x3(orn);
        inv_IW = scale(basis, inv_I) * transpose(basis);
    });
}

solver::solver(entt::registry &reg) 
    : registry(&reg)
{
    connections.push_back(reg.on_construct<constraint>().connect<&on_construct_constraint>());
    connections.push_back(reg.on_destroy<constraint>().connect<&on_destroy_constraint>());

    connections.push_back(reg.on_construct<relation>().connect<&island_on_construct_relation>());
    connections.push_back(reg.on_destroy<relation>().connect<&island_on_destroy_relation>());

    connections.push_back(reg.on_construct<linvel>().connect<&entt::registry::assign<delta_linvel>>(reg));
    connections.push_back(reg.on_destroy<linvel>().
        connect<entt::overload<void(entt::entity)>(&entt::registry::reset<delta_linvel>)>(reg));

    connections.push_back(reg.on_construct<angvel>().connect<&entt::registry::assign<delta_angvel>>(reg));
    connections.push_back(reg.on_destroy<angvel>().
        connect<entt::overload<void(entt::entity)>(&entt::registry::reset<delta_angvel>)>(reg));
}

void solver::update(uint64_t step, scalar dt) {
    // Apply forces and acceleration.
    integrate_linacc(*registry, dt);
    apply_gravity(*registry, dt);

    // Setup constraints.
    auto mass_inv_view = registry->view<const mass_inv, const inertia_world_inv>(exclude_sleeping);
    auto vel_view = registry->view<const linvel, const angvel>(exclude_sleeping);
    auto delta_view = registry->view<delta_linvel, delta_angvel>(exclude_sleeping);

    auto con_view = registry->view<const relation, constraint>(exclude_sleeping);
    con_view.each([&] (auto entity, const relation &rel, constraint &con) {
        auto [inv_mA, inv_IA] = mass_inv_view.get<const mass_inv, const inertia_world_inv>(rel.entity[0]);
        auto [inv_mB, inv_IB] = mass_inv_view.get<const mass_inv, const inertia_world_inv>(rel.entity[1]);
        auto [linvelA, angvelA] = vel_view.get<const linvel, const angvel>(rel.entity[0]);
        auto [linvelB, angvelB] = vel_view.get<const linvel, const angvel>(rel.entity[1]);
        auto [dvA, dwA] = delta_view.get<delta_linvel, delta_angvel>(rel.entity[0]);
        auto [dvB, dwB] = delta_view.get<delta_linvel, delta_angvel>(rel.entity[1]);

        std::visit([&] (auto &&c) {
            c.update(solver_stage_value_t<solver_stage::prepare>{}, entity, con, rel, *registry, dt);

            for (size_t i = 0; i < con.num_rows; ++i) {
                auto &row = registry->get<constraint_row>(con.row[i]);
                prepare(row, inv_mA, inv_mB, inv_IA, inv_IB, linvelA, linvelB, angvelA, angvelB);
                warm_start(row, inv_mA, inv_mB, inv_IA, inv_IB, dvA, dvB, dwA, dwB);
            }
        }, con.var);
    });

    // Solve constraints.
    auto row_view = registry->view<constraint_row>(exclude_sleeping);

    for (uint32_t i = 0; i < iterations; ++i) {
        // Prepare constraints for iteration.
        con_view.each([&] (auto entity, const relation &rel, constraint &con) {
            std::visit([&] (auto &&c) {
                c.update(solver_stage_value_t<solver_stage::iteration>{}, entity, con, rel, *registry, dt);
            }, con.var);
        });

        row_view.each([&] (auto, auto &row) {
            auto [inv_mA, inv_IA] = mass_inv_view.get<const mass_inv, const inertia_world_inv>(row.entity[0]);
            auto [inv_mB, inv_IB] = mass_inv_view.get<const mass_inv, const inertia_world_inv>(row.entity[1]);
            auto [dvA, dwA] = delta_view.get<delta_linvel, delta_angvel>(row.entity[0]);
            auto [dvB, dwB] = delta_view.get<delta_linvel, delta_angvel>(row.entity[1]);
            solve(row, inv_mA, inv_mB, inv_IA, inv_IB, dvA, dvB, dwA, dwB);
        });
    }

    // Apply constraint velocity correction.
    auto linvel_view = registry->view<dynamic_tag, linvel, delta_linvel>(exclude_sleeping);
    linvel_view.each([] (auto, auto, auto &vel, auto &delta) {
        vel += delta;
        delta = vector3_zero;
    });

    auto angvel_view = registry->view<dynamic_tag, angvel, delta_angvel>(exclude_sleeping);
    angvel_view.each([] (auto, auto, auto &vel, auto &delta) {
        vel += delta;
        delta = vector3_zero;
    });

    // Integrate velocities to obtain new transforms.
    integrate_linvel(*registry, dt);
    integrate_angvel(*registry, dt);

    // Update world-space moment of inertia.
    update_inertia(*registry);

    put_islands_to_sleep(*registry, step, dt);
}

}