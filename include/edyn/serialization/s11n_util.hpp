#ifndef EDYN_SERIALIZATION_S11N_UTIL_HPP
#define EDYN_SERIALIZATION_S11N_UTIL_HPP

#include <type_traits>

namespace edyn {

template<typename Archive, typename T>
void serialize(Archive &, T &);

template<typename Archive, typename Enum>
void serialize_enum(Archive &archive, Enum &value) {
    using underlying_type = std::underlying_type_t<Enum>;
    if constexpr(Archive::is_input::value) {
        underlying_type i;
        archive(i);
        value = Enum{i};
    } else {
        auto i = static_cast<underlying_type>(value);
        archive(i);
    }
}

}

#endif // EDYN_SERIALIZATION_S11N_UTIL_HPP
