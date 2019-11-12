#ifndef EDYN_SYS_INTEGRATE_LINACC_HPP
#define EDYN_SYS_INTEGRATE_LINACC_HPP

#include <entt/entt.hpp>
#include "edyn/comp/linvel.hpp"
#include "edyn/comp/linacc.hpp"

namespace edyn {

void integrate_linacc(entt::registry &registry, scalar dt) {
    auto view = registry.view<linvel, const linacc>();
    view.each([&] (auto, linvel &vel, const linacc &acc) {
        vel += acc * dt;
    });
}

void integrate_linacc_priv(entt::registry &registry, scalar dt) {
    auto view = registry.view<linvel_priv, const linacc>();
    view.each([&] (auto, linvel_priv &vel, const linacc &acc) {
        vel += acc * dt;
    });
}

}

#endif // EDYN_SYS_INTEGRATE_LINACC_HPP